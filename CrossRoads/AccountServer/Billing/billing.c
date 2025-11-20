#include "estring.h"
#include "net.h"
#include "HttpLib.h"
#include "HttpClient.h"
#include "timing.h"
#include "sysutil.h"
#include "logging.h"
#include "stashtable.h"
#include "objContainer.h"
#include "Product.h"
#include "Subscription.h"
#include "billing.h"
#include "billing_h_ast.h"
#include "AccountServer.h"
#include "Product/billingProduct.h"
#include "Subscription/billingSubscription.h"
#include "AutoBill/UpdateActiveSubscriptions.h"
#include "AccountManagement.h"
#include "AutoBill/FetchChangedAutoBills.h"
#include "Transaction/transactionQuery.h"
#include "GlobalData.h"
#include "ProductKey.h"
#include "FloatAverager.h"
#include "Product.h"
#include "FolderCache.h"
#include "qsortG.h"
#include "Money.h"
#include "StringUtil.h"
#include "UpdatePaymentMethod.h"
#include "cmdparse.h"
#include "AccountReporting.h"
#include "redact.h"
#include "MemTrack.h"

// ---------------------------------------------------------------------------------------------------

static char sBillingCfgFilename[MAX_PATH] = "server/AccountServer/billingSettings.txt";

struct BillingConfiguration gBillingConfiguration = {0};

// ---------------------------------------------------------------------------------------------------

#define SET_BILLING_TRANS_STATE(STATE) { pTrans->state = STATE; pTrans->stateTime = GetTickCount(); }

static BillingTransaction **sppActiveBillingTransactions = NULL;
static BillingTransaction **sppCompletedBillingTransactions = NULL;
static StashTable stBillingTransactionUIDTable = NULL;

static U32 suIdleTransactionCount = 0;
static U32 *spAccountIDUpdateQueue = NULL;
static U32 suLastIdleTransactionQueueFill = 0;       // When did we last fill spAccountIDUpdateQueue?
static U32 suLastIdleTransactionQueueCompletion = 0; // When did spAccountIDUpdateQueue completely empty last?
static U32 suLastIdleTransactionQueueCompletionLength = 0;

static char *gpLastVindiciaResponse = NULL;
static U32 guLastBillingTransID = 0;

static void btDestroy(SA_PRE_NN_VALID SA_POST_P_FREE BillingTransaction *pTrans);
static void btComplete(BillingTransaction *pTrans);
static void btFree(void *pBlock);

// Debug tracking of completion within billingOnTransactionComplete().
// TODO cogden: Remove this after the "ultimate solution" to the btComplete() issue.
static bool bCurrentTransactionWasCompletedOrContinued;

static void billingSetLastVindiciaResponse(U32 uBillingTransID, SA_PARAM_OP_STR const char *pResponse)
{
	guLastBillingTransID = uBillingTransID;
	estrCopy2(&gpLastVindiciaResponse, pResponse);
}

SA_RET_OP_STR const char *billingGetLastVindiciaResponse(U32 uBillingTransID)
{
	if (!uBillingTransID || uBillingTransID == guLastBillingTransID)
		return gpLastVindiciaResponse;

	return NULL;
}

typedef enum IdleTransactionsState
{
	// Best enum prefix EVER

	ITS_EMPTY = 0,
	ITS_RUNNING,
	ITS_COMPLETE

} IdleTransactionsState;

static IdleTransactionsState seIdleTransactionState = ITS_EMPTY;
static bool gbShuttingDown = false;

bool gbBillingEnabled = true;
AUTO_CMD_INT(gbBillingEnabled,billingenabled) ACMD_ACCESSLEVEL(0) ACMD_EARLYCOMMANDLINE;

static void fillIdleQueue();

// Get a new trans ID
static U32 billingGetTransID(void)
{
	static U32 uID = 0;
	uID++;
	return uID;
}

bool billingIsEnabled()
{
	return gbBillingEnabled;
}

SA_RET_NN_VALID  const struct BillingConfiguration *billingGetConfig()
{
	return &gBillingConfiguration;
}

U32 billingGetDebugLevel()
{
	return gBillingConfiguration.debug;
}

bool billingSkipNonBillingAccounts()
{
	return (gBillingConfiguration.idleTransactionsSkipNonBillingAccounts != 0);
}

const FraudSettings * getFraudSettings(SA_PARAM_OP_STR const char *pCountry)
{
	EARRAY_CONST_FOREACH_BEGIN(gBillingConfiguration.fraudSettings, i, s);
		FraudSettings *settings = gBillingConfiguration.fraudSettings[i];

		if ((pCountry && settings->billingCountry && !strcmpi(settings->billingCountry, pCountry)) ||
			(!pCountry && !settings->billingCountry))
			return settings;

	EARRAY_FOREACH_END;

	if (pCountry) return getFraudSettings(NULL); // If the country wasn't found, try no country

	return NULL;
}

CONST_STRING_EARRAY getPurchaseLogKeys()
{
	return gBillingConfiguration.PurchaseLogKeys;
}

bool billingMapPWCurrency(const char * pCurrency, const char * * * eaOutCurrencies)
{
	EARRAY_CONST_FOREACH_BEGIN(gBillingConfiguration.ValidPWCurrencies, iCurCurrency, iNumCurrencies);
	{
		if (!stricmp_safe(pCurrency, gBillingConfiguration.ValidPWCurrencies[iCurCurrency]))
		{
			int idx = eaIndexedFindUsingString(&gBillingConfiguration.PWCurrencyDupe, pCurrency);
			if (idx >= 0 && eaOutCurrencies)
			{
				eaPushEArray(eaOutCurrencies, &gBillingConfiguration.PWCurrencyDupe[idx]->extraCurrencies);
			}
			eaPush(eaOutCurrencies, pCurrency);
			return true;
		}
	}
	EARRAY_FOREACH_END;

	return false;
}

static NetComm *billingComm()
{
	static NetComm	*comm;

	if (!comm)
		comm = commCreate(0,1);
	return comm;
}

// ---------------------------------------------------------------------------------------------------

static bool isBadHttpResponse(int code)
{
	// The HTTP response should always be 200, regardless of what the SOAP return code is.
	return code != 200;
}

static bool isVindiciaInternalError(int code)
{
	// Return code from 500 to 599 indicates an internal or back-end service Vindicia error
	return (code >= 500) && (code < 600);
}

static int VindiciaReponseCode(BillingTransaction *pTrans)
{
	PERFINFO_AUTO_START_FUNC();
	if(pTrans->response && *pTrans->response)
	{
		const char *pReturnString = strstr(pTrans->response, "ReturnCode\"");
		if(pReturnString)
		{
			while(*pReturnString && *pReturnString != '>')
				pReturnString++;

			if(*pReturnString)
			{
				const char *pEndOfString;
				char returnCode[10];

				pReturnString++;
				pEndOfString = strstr(pReturnString, "<");
				if(pEndOfString)
				{
					int len = (pEndOfString - pReturnString);
					if(len > 0)
					{
						int ret;
						strncpy(returnCode, pReturnString, len);
						ret = atoi(returnCode);
						PERFINFO_AUTO_STOP();
						return ret;
					}
				}
			}
		}
	}

	// No valid return code found.
	// If no valid conversion could be performed with atoi(), a zero value is returned
	PERFINFO_AUTO_STOP();
	return 0;
}

void BillingDebug_dbg(SA_PARAM_NN_STR const char *file, SA_PARAM_NN_STR const char *func, int line, bool skipExtra, FORMAT_STR const char *format, ...)
{
	U32 uDebugLevel;
	char *pText = NULL;
	static char *pLogLine = NULL;
	char *newline;

	PERFINFO_AUTO_START_FUNC();

	// Format debug message.
	estrStackCreate(&pText);
	estrGetVarArgs(&pText, format);
	if (!estrLength(&pText))
	{
		estrDestroy(&pText);
		PERFINFO_AUTO_START_FUNC();
		return;
	}

	// Print line number information if requested.
	uDebugLevel = billingGetDebugLevel();
	if (uDebugLevel > 1 && !skipExtra)
	{
		printf("\n%s (%d - %s):\n", strrchr(file, '\\') ? strrchr(file, '\\') + 1 : file, line, func);
	}

	// Print debug message to the screen.
	if (uDebugLevel)
		printf("%s", pText);

	// Log the debug messages, line-buffered.
	estrAppend(&pLogLine, &pText);
	newline = strrchr(pLogLine, '\n');
	if (newline)
	{
		*newline = 0;
		log_printf(LOG_ACCOUNT_SERVER_BILLING_TRANSACTIONS, "%s", pLogLine);
		estrRemove(&pLogLine, 0, newline - pLogLine + 1);
	}

	estrDestroy(&pText);
	PERFINFO_AUTO_STOP_FUNC();
}

void BillingDebugWarning_dbg(SA_PARAM_NN_STR const char *pFile, SA_PARAM_NN_STR const char *pFunction, int iLine, FORMAT_STR const char *pFormat, ...)
{
	char *pWarning = NULL;

	PERFINFO_AUTO_START_FUNC();
	estrGetVarArgs(&pWarning, pFormat);

	consolePushColor();
	consoleSetFGColor(COLOR_RED|COLOR_GREEN|COLOR_BRIGHT);
	BillingDebug_dbg(pFile, pFunction, iLine, false, "%s", pWarning);
	consolePopColor();

	estrDestroy(&pWarning);
	PERFINFO_AUTO_STOP();
}

static void DebugBillingCounts(void)
{
	PERFINFO_AUTO_START_FUNC();
	BILLING_DEBUG("\t\t\tTransactions: Q: %d   I: %d   A: %d   C: %d\n", 
		btGetQueuedTransactionCount(),
		btGetIdleTransactionCount(),
		btGetActiveTransactionCount(),
		btGetCompletedTransactionCount());
	PERFINFO_AUTO_STOP();
}

static void btCreateUniqueID(SA_PARAM_NN_VALID BillingTransaction *pTrans)
{
	U64 msSince1601   = microsecondsSince1601();
	int iRandomNumber = rand();
	int iProcessID    = _getpid();
	static int iEverIncrementingID = 0;

	if (!devassert(pTrans))
		return;
	
	iEverIncrementingID++;

	estrPrintf(&pTrans->webUID, "%"FORM_LL"d-%x-%x-%x", msSince1601, iProcessID, iRandomNumber, iEverIncrementingID);

	if (!devassert(pTrans->webUID))
		return;

	devassert(stashAddPointer(stBillingTransactionUIDTable, pTrans->webUID, pTrans, true));
}

SA_RET_OP_VALID BillingTransaction * btFindByUID(SA_PARAM_NN_STR const char *uid)
{
	BillingTransaction *pTrans = NULL;

	if (!uid || !*uid) return NULL;

	stashFindPointer(stBillingTransactionUIDTable, uid, &pTrans);
	return pTrans;
}

// ---------------------------------------------------------------------------------------------------

static void btInit(BillingTransaction *pTrans, const char *action, const char *xml, BillingTransactionCompleteCB callback, void *userData)
{
	PERFINFO_AUTO_START_FUNC();

	// Log this request.
	if (billingGetVindiciaLogLevel() >= 2)
		log_printf(LOG_ACCOUNT_SERVER_VINDICIA, "Transaction %lu request: %s", pTrans->ID, billingRedact(xml));

	estrCopy2(&pTrans->action, action);

	pTrans->callback = callback;
	pTrans->userData = userData;
	estrCopy2(&pTrans->request, xml);
	pTrans->result = BTR_NONE;

	if(billingIsEnabled())
	{
		pTrans->client = httpClientConnect(billingGetStunnelHost(), gBillingConfiguration.vindiciaPort, NULL, NULL, httpStockTextDataCB, NULL, billingComm(), false, 0);

		if (pTrans->client)
		{
			httpClientSetEString(pTrans->client, &pTrans->response);
			httpClientSetUserData(pTrans->client, pTrans->client);
			SET_BILLING_TRANS_STATE(BTS_CONNECTING);
		}
		else
		{
			AssertOrAlert("ACCOUNT_SERVER_VINDICIA_CONN_FAILED", "Could not connect to Vindicia at '%s' on port %d.", billingGetStunnelHost(), gBillingConfiguration.vindiciaPort);
			SET_BILLING_TRANS_STATE(BTS_ALREADY_FAILED);
		}
	}
	else
	{
		SET_BILLING_TRANS_STATE(BTS_DISABLED_AUTOFAIL);
	}
	pTrans->creationTime = pTrans->stateTime;

	PERFINFO_AUTO_STOP_FUNC();
}

// Called by both btContinue() and btDestroy()
static void btShutdown(SA_PARAM_NN_VALID BillingTransaction *pTrans)
{
	if (!devassert(pTrans)) return;

	PERFINFO_AUTO_START_FUNC();

	if(pTrans->client)
	{
		httpClientDestroy(&pTrans->client);
	}

	estrDestroy(&pTrans->request);
	estrDestroy(&pTrans->response);

	devassert(!pTrans->client);
	PERFINFO_AUTO_STOP();
}

void btContinue(BillingTransaction *pTrans, const char *action, const char *xml, BillingTransactionCompleteCB callback, void *userData)
{
	if (!verify(pTrans))
		return;
	
	PERFINFO_AUTO_START_FUNC();

	// Signal to billingOnTransactionComplete() that we called btContinue().
	bCurrentTransactionWasCompletedOrContinued = true;

	btShutdown(pTrans);
	btInit(pTrans, action, xml, callback, userData);
	PERFINFO_AUTO_STOP();
}

BillingTransaction * btCreateBlank(bool bCreateID)
{
	BillingTransaction *pTrans = NULL;

	PERFINFO_AUTO_START_FUNC();
	pTrans = callocStruct(struct BillingTransaction);
	pTrans->ID = billingGetTransID();

	if(billingIsEnabled() && bCreateID)
	{
		btCreateUniqueID(pTrans);
	}

	eaPush(&sppActiveBillingTransactions, pTrans);
	DebugBillingCounts();

	SET_BILLING_TRANS_STATE(BTS_IDLE);
	pTrans->creationTime = pTrans->stateTime;
	PERFINFO_AUTO_STOP();
	return pTrans;
}

BillingTransaction * btCreate(const char *action, const char *xml, BillingTransactionCompleteCB callback, void *userData, bool bCreateID)
{
	BillingTransaction *pTrans = NULL;
	
	PERFINFO_AUTO_START_FUNC();
	pTrans = btCreateBlank(bCreateID);
	btInit(pTrans, action, xml, callback, userData);
	PERFINFO_AUTO_STOP();
	return pTrans;
}

void btFail(BillingTransaction *pTrans, const char *format, ...)
{
	PERFINFO_AUTO_START_FUNC();
	if(format != NULL)
	{
		VA_START(args, format);
			estrClear(&pTrans->resultString);
			estrConcatfv(&pTrans->resultString, format, args);
		VA_END();

		BILLING_DEBUG_WARNING("Transaction failed: %s\n", pTrans->resultString);
	}
	else
	{
		BILLING_DEBUG_WARNING("Transaction failed.\n");
	}

	pTrans->result = BTR_FAILURE;
	SET_BILLING_TRANS_STATE(BTS_ALREADY_FAILED);
	PERFINFO_AUTO_STOP();
}

void btMarkIdle(BillingTransaction *pTrans)
{
	PERFINFO_AUTO_START_FUNC();
	if(!pTrans->idle)
	{
		BILLING_DEBUG("Active marked as idle...\n");
		suIdleTransactionCount++;

		// Idle transactions cannot be web transactions
		estrDestroy(&pTrans->webUID);
	}

	pTrans->idle = 1;
	PERFINFO_AUTO_STOP();
}

void btMarkSteamTransaction(BillingTransaction *pTrans)
{
	ADD_MISC_COUNT(1, __FUNCTION__);
	pTrans->steamTransaction = true;
}

void btCompleteSteamTransaction(BillingTransaction *pTrans, bool bSuccess)
{
	ADD_MISC_COUNT(1, __FUNCTION__);
	if (pTrans->steamTransaction)
	{
		if (bSuccess)
		{
			pTrans->steamTransactionResult = BTR_SUCCESS;
		}
		else
		{
			pTrans->steamTransactionResult = BTR_FAILURE;
		}
	}
}

void * btAllocUserData_dbg(BillingTransaction *pTrans, int size, MEM_DBG_PARMS_VOID)
{
	if (!verify(pTrans))
		return NULL;

	if (!verify(size > 0))
		return NULL;

	PERFINFO_AUTO_START_FUNC();
	pTrans->userData = btAlloc_dbg(pTrans, 1, size, MEM_DBG_PARMS_CALL_VOID);
	PERFINFO_AUTO_STOP();

	return pTrans->userData;
}

static void billingLogVindiciaReturn(U32 uTimeTaken, SA_PARAM_OP_STR const char *pReturnString)
{
	PERFINFO_AUTO_START_FUNC();
	BILLING_DEBUG("Transaction took ");
	consolePushColor();
	if (uTimeTaken < VINDICIA_TIMEOUT_GREEN_MAX)
		consoleSetFGColor(COLOR_GREEN|COLOR_BRIGHT);
	else if (uTimeTaken > VINDICIA_TIMEOUT_YELLOW_MAX)
		consoleSetFGColor(COLOR_RED|COLOR_BRIGHT);
	else
		consoleSetFGColor(COLOR_RED|COLOR_GREEN|COLOR_BRIGHT);
	BILLING_DEBUG_CONTINUE("%dms", uTimeTaken);
	consolePopColor();
	BILLING_DEBUG_CONTINUE(" [");
	consolePushColor();
	if (!stricmp_safe(pReturnString, VINDICIA_SUCCESS_MESSAGE))
		consoleSetFGColor(COLOR_GREEN|COLOR_BRIGHT);
	else
		consoleSetFGColor(COLOR_RED|COLOR_BRIGHT);
	BILLING_DEBUG_CONTINUE("%s", NULL_TO_EMPTY(pReturnString));
	consolePopColor();
	BILLING_DEBUG_CONTINUE("]\n");
	PERFINFO_AUTO_STOP();
}

static void billingHandleVindiciaInvalidResponse(SA_PARAM_NN_STR const char *pResponse)
{
	const char *pFault = vindiciaGetFault(pResponse);

	if (pFault)
	{
		if (strstr(pFault, "Application failed during request deserialization: Can't locate object method \"deserialize\" via package \"Vindicia::Soap::Deserializer::"))
		{
			static const char *pError = "Vindicia connection appears to not support version " VINDICIA_VERSION " of their SOAP API.  If this is a development machine, perhaps you need to connect to Vindicia's staging server instead of their prodtest server.";
			BILLING_DEBUG_WARNING("%s\n", pError);
			AssertOrAlert("ACCOUNTSERVER_INVALID_VINDICIA_VERSION", "%s", pError);
		}
		else
		{
			BILLING_DEBUG_WARNING("SOAP fault detected: %s\n", pFault);
			AssertOrAlert("ACCOUNTSERVER_VINDICIA_FAULT", "SOAP fault detected: %s", pFault);
		}
	}
	else
	{
		BILLING_DEBUG_WARNING("Unknown Vindicia response:\n%s\n\n", pResponse);
		AssertOrAlert("ACCOUNTSERVER_MISSING_VINDICIA_RETURN_STRING", "Unknown Vindicia response: %s", pResponse);
	}
}

static void billingOnTransactionComplete(BillingTransaction *pTrans)
{
	U32 time_taken;
	bool bFoundReturnString = false;
	int responseCode;

	if (!verify(pTrans))
		return;

	PERFINFO_AUTO_START_FUNC();

	// Calculate duration of this transaction.
	time_taken = GetTickCount() - pTrans->creationTime;

	// Log this response.
	if (billingGetVindiciaLogLevel() >= 2)
		log_printf(LOG_ACCOUNT_SERVER_VINDICIA, "Transaction %lu response: %s", pTrans->ID, billingRedact(pTrans->response));
	if (billingGetVindiciaLogLevel() >= 1)
		servLog(LOG_ACCOUNT_SERVER_VINDICIA, "VindiciaRoundtrip", "transaction %lu time %lu", pTrans->ID, time_taken);

	// Pull out the Vindicia response and replace our resultString with theirs, if possible
	// Result will always be BTR_SUCCESS if they responded, regardless of the result string
	// It is up to the callback to decide if the transaction was actually a failure.
	if(pTrans->response && *pTrans->response)
	{
		const char *pReturnString = strstr(pTrans->response, "returnString xsi:type=\"xsd:string\"");
		if(pReturnString)
		{
			while(*pReturnString && *pReturnString != '>')
				pReturnString++;

			if(*pReturnString)
			{
				const char *pEndOfString;

				pReturnString++;
				pEndOfString = strstr(pReturnString, "<");
				if(pEndOfString)
				{
					int len = (pEndOfString - pReturnString);
					if(len > 0)
					{
						estrReserveCapacity(&pTrans->resultString, len+1);
						strncpy_s(pTrans->resultString, len+1, pReturnString, len);
						pTrans->resultString[len] = 0;
						bFoundReturnString = true;
					}
				}
			}
		}
	}

	responseCode = VindiciaReponseCode(pTrans);

	if (isVindiciaInternalError(responseCode))
	{
		AssertOrAlert("ACCOUNTSERVER_VINDICIA_SOAP_FAIL",
			"Vindicia returned a SOAP response code %d\n"
			"with the ReturnString : %s", responseCode, pTrans->resultString);
	}

	if (pTrans->response && *pTrans->response && !bFoundReturnString)
	{
		billingHandleVindiciaInvalidResponse(pTrans->response);
		btFail(pTrans, "Invalid response from Vindicia.");
	}
	else if (pTrans->response)
	{
		billingLogVindiciaReturn(time_taken, pTrans->resultString);
		billingSetLastVindiciaResponse(pTrans->ID, pTrans->resultString);
	}

	// Send a warning alert if the transaction took longer than a certain threshold.
	if (pTrans->state != BTS_DISABLED_AUTOFAIL && time_taken > gBillingConfiguration.replyAlertSeconds*1000)
		ErrorOrAlert("ACCOUNTSERVER_VINDICIA_RESPONSE_SLOW", "Vindicia response took %lu ms", time_taken);

	SET_BILLING_TRANS_STATE(BTS_IDLE);

	// Call the transaction callback to handle this response, if there is a handler.
	if(pTrans->callback)
	{
		BillingTransactionCompleteCB cb = pTrans->callback;
		U32 uID = pTrans->ID;

		// Call the callback, and make sure it calls btComplete() or btContinue().
		PERFINFO_AUTO_START("Completion Callback", 1);
		bCurrentTransactionWasCompletedOrContinued = false;
		pTrans->callback(pTrans);
		PERFINFO_AUTO_STOP();

		// Happens on live and spams NetOps but should be harmless because of the code following it (it will be cleaned up)
		// Eventually, transactions will NEVER be completed, so this won't be a valid check anyway.
		//devassert(bCurrentTransactionWasCompletedOrContinued);
		if (!bCurrentTransactionWasCompletedOrContinued)
		{
			pTrans->callback = NULL;
			btComplete(pTrans);
		}
	}
	// Otherwise, just complete the transaction.
	else
	{
		btComplete(pTrans);
	}
	PERFINFO_AUTO_STOP();
}

void btCompletedOncePerFrame(BillingTransaction *pTrans)
{
	if(!pTrans)
		return;

	PERFINFO_AUTO_START_FUNC();
	if((!pTrans->steamTransaction || pTrans->steamTransactionResult != BTR_NONE) && (GetTickCount() - pTrans->stateTime) > (gBillingConfiguration.completedMaxAgeSeconds * 1000))
	{
		// Time to die.
		btDestroy(pTrans);
	}
	PERFINFO_AUTO_STOP();
}

void btActiveOncePerFrame(BillingTransaction *pTrans)
{
	DWORD now;

	if(!pTrans)
		return;

	PERFINFO_AUTO_START_FUNC();

	now = GetTickCount();
	switch(pTrans->state)
	{
		#define PERF_CASE(state) xcase state: PERFINFO_AUTO_START("state:"###state, 1);

		PERF_CASE(BTS_DISABLED_AUTOFAIL)
		{
			pTrans->result = BTR_FAILURE;
			estrPrintf(&pTrans->resultString, "Autofail, billing is disabled.");

			billingOnTransactionComplete(pTrans);
		}

		PERF_CASE(BTS_ALREADY_FAILED)
		{
			pTrans->result = BTR_FAILURE;
			// Assume the result string is already set

			btComplete(pTrans);
		}

		PERF_CASE(BTS_CONNECTING)
		{
			NetLink *link = httpClientGetLink(pTrans->client);
			if(linkConnected(link))
			{
				int len = estrLength(&pTrans->request);
				char *pHTTPHeader = NULL;

				PERFINFO_AUTO_START("Connected", 1);
				estrStackCreate(&pHTTPHeader);

				// Send alert if connecting took longer than alert threshold.
				if((now - pTrans->stateTime) > (gBillingConfiguration.connectAlertSeconds * 1000))
					ErrorOrAlert("ACCOUNTSERVER_VINDICIA_CONNECT_SLOW", "Connecting to Vindicia took %lu ms", now - pTrans->stateTime);

				estrPrintf(&pHTTPHeader,
					"POST /v" VINDICIA_VERSION "/soap.pl HTTP/1.0\r\n"
					"Host: soap.prodtest.sj.vindicia.com:443\r\n"
					"User-Agent: Cryptic; gSOAP 2.7.11\r\n"
					"Content-Type: text/xml; charset=utf-8\r\n"
					"Content-Length: %d\r\n"
					"\r\n",
					len);

				httpClientSendBytesRaw(pTrans->client, pHTTPHeader,  (int)strlen(pHTTPHeader));
				httpClientSendBytesRaw(pTrans->client, pTrans->request, len);

				SET_BILLING_TRANS_STATE(BTS_WAITING_FOR_REPLY);

				estrDestroy(&pHTTPHeader);
				PERFINFO_AUTO_STOP();
			}
			else
			{
				if(linkDisconnected(link))
				{
					PERFINFO_AUTO_START("Disconnected", 1);
					pTrans->result = BTR_FAILURE;
					estrPrintf(&pTrans->resultString, "Failed to connect (link disconnected).");
					AssertOrAlert("ACCOUNTSERVER_VINDICIA_CONNECT_FAIL", "Failed to connect to Vindicia for '%s' request.", pTrans->action);
					servLog(LOG_ACCOUNT_SERVER_VINDICIA, "VindiciaLinkProblem", "transaction %lu line %d time %lu",
						pTrans->ID, __LINE__, now - pTrans->stateTime);

					billingOnTransactionComplete(pTrans);
					PERFINFO_AUTO_STOP();
				}
				else
				{
					// Still waiting to connect, check for timeout
					if((now - pTrans->stateTime) > (gBillingConfiguration.connectTimeoutSeconds * 1000))
					{
						PERFINFO_AUTO_START("Timeout", 1);
						pTrans->result = BTR_FAILURE;
						estrPrintf(&pTrans->resultString, "Failed to connect (timeout: %ds).", gBillingConfiguration.connectTimeoutSeconds);
						AssertOrAlert("ACCOUNTSERVER_VINDICIA_CONNECT_TIMEOUT", "Timed out connecting to Vindicia (%lu ms) for '%s' request.", now - pTrans->stateTime, pTrans->action);
						servLog(LOG_ACCOUNT_SERVER_VINDICIA, "VindiciaLinkProblem", "transaction %lu line %d time %lu",
							pTrans->ID, __LINE__, now - pTrans->stateTime);

						billingOnTransactionComplete(pTrans);
						PERFINFO_AUTO_STOP();
					}
				}
			}
		}

		PERF_CASE(BTS_WAITING_FOR_REPLY)
		{
			NetLink *link = httpClientGetLink(pTrans->client);
			if(httpClientResponded(pTrans->client))
			{
				int code = httpClientGetResponseCode(pTrans->client);
				PERFINFO_AUTO_START("Responded", 1);
				if(httpResponseCodeIsSuccess(code))
				{
					ADD_MISC_COUNT(1, "Success");
					pTrans->result = BTR_SUCCESS;
					estrPrintf(&pTrans->resultString, "Transaction Complete");
				}
				else
				{
					ADD_MISC_COUNT(1, "Failure");
					pTrans->result = BTR_FAILURE;
					estrPrintf(&pTrans->resultString, "Transaction Failed (HTTP response code %d)", code);
					if (isBadHttpResponse(code))
					{
						AssertOrAlert("ACCOUNTSERVER_VINDICIA_HTTP_FAIL", "Vindicia returned HTTP response code %d", code);
						servLog(LOG_ACCOUNT_SERVER_VINDICIA, "VindiciaLinkProblem", "transaction %lu line %d time %lu",
							pTrans->ID, __LINE__, now - pTrans->stateTime);
					}
				}

				billingOnTransactionComplete(pTrans);
				PERFINFO_AUTO_STOP();
			}
			else if(linkDisconnected(link))
			{
				PERFINFO_AUTO_START("Disconnected", 1);
				pTrans->result = BTR_FAILURE;
				estrPrintf(&pTrans->resultString, "Connection dropped after sending request (link disconnected).");
				AssertOrAlert("ACCOUNTSERVER_VINDICIA_RESPONSE_DISCONNECT", "Vindicia disconnected without responding to '%s' request.", pTrans->action);
				servLog(LOG_ACCOUNT_SERVER_VINDICIA, "VindiciaLinkProblem", "transaction %lu line %d time %lu", pTrans->ID,
					__LINE__, now - pTrans->stateTime);

				billingOnTransactionComplete(pTrans);
				PERFINFO_AUTO_STOP();
			}
			else
			{
				// Still waiting to connect, check for timeout
				if((GetTickCount() - pTrans->stateTime) > (gBillingConfiguration.replyTimeoutSeconds * 1000))
				{
					PERFINFO_AUTO_START("Timeout", 1);
					pTrans->result = BTR_FAILURE;
					estrPrintf(&pTrans->resultString, "Reponse timeout (timeout: %ds).", gBillingConfiguration.replyTimeoutSeconds);
					AssertOrAlert("ACCOUNTSERVER_VINDICIA_RESPONSE_TIMEOUT", "Timed out waiting for response from Vindicia for '%s' request.", pTrans->action);
					servLog(LOG_ACCOUNT_SERVER_VINDICIA, "VindiciaLinkProblem", "transaction %lu line %d time %lu", pTrans->ID,
						__LINE__, now - pTrans->stateTime);

					billingOnTransactionComplete(pTrans);
					PERFINFO_AUTO_STOP();
				}
			}
		}

		PERF_CASE(BTS_IDLE)
		{
			btComplete(pTrans);
		}

		#undef PERF_CASE
	};
	PERFINFO_AUTO_STOP();
	PERFINFO_AUTO_STOP();
}

static void btDestroy(SA_PRE_NN_VALID SA_POST_P_FREE BillingTransaction *pTrans)
{
	devassert(pTrans);

	PERFINFO_AUTO_START_FUNC();

	if(pTrans->webUID)
	{
		stashRemovePointer(stBillingTransactionUIDTable, pTrans->webUID, NULL);
	}

	eaFindAndRemoveFast(&sppActiveBillingTransactions, pTrans);
	eaFindAndRemoveFast(&sppCompletedBillingTransactions, pTrans);

	DebugBillingCounts();

	EARRAY_FOREACH_BEGIN(pTrans->ppAllocatedMemoryBlocks, i);
		if(pTrans->ppAllocatedMemoryBlocks)
		{
			btFree(pTrans->ppAllocatedMemoryBlocks[i]);
		}
	EARRAY_FOREACH_END;
	eaDestroy(&pTrans->ppAllocatedMemoryBlocks);

	EARRAY_FOREACH_BEGIN(pTrans->ppResultsToCleanup, i);
	{
		vindiciaXMLtoObjResultDestroy(&pTrans->ppResultsToCleanup[i]);
	}
	EARRAY_FOREACH_END;
	eaDestroy(&pTrans->ppResultsToCleanup);

	EARRAY_FOREACH_BEGIN(pTrans->ppStructsToCleanup, i);
	{
		StructDeInitVoid(pTrans->ppStructsToCleanup[i]->pti, pTrans->ppStructsToCleanup[i]->ptr);
		btFree(pTrans->ppStructsToCleanup[i]);
	}
	EARRAY_FOREACH_END;
	eaDestroy(&pTrans->ppResultsToCleanup);

	estrDestroy(&pTrans->action);
	estrDestroy(&pTrans->resultString);
	estrDestroy(&pTrans->webUID);
	estrDestroy(&pTrans->request);
	estrDestroy(&pTrans->response);

	estrDestroy(&pTrans->steamCurrency);
	estrDestroy(&pTrans->steamState);
	estrDestroy(&pTrans->steamCountry);
	estrDestroy(&pTrans->steamStatus);
	estrDestroy(&pTrans->redirectURL);

	if (pTrans->webResponseTransactionInfo)
		eaDestroyStruct(&pTrans->webResponseTransactionInfo, parse_AccountTransactionInfo);

	if (pTrans->distributedKeys)
		eaDestroyStruct(&pTrans->distributedKeys, parse_ProductKeyInfo);

	btShutdown(pTrans);
	free(pTrans);
	PERFINFO_AUTO_STOP();
}

static void btComplete(BillingTransaction *pTrans)
{
	if (!verify(pTrans))
		return;

	PERFINFO_AUTO_START_FUNC();

	// Signal to billingOnTransactionComplete() that we called btComplete().
	bCurrentTransactionWasCompletedOrContinued = true;

	if (pTrans->result == BTR_NONE)
		pTrans->result = BTR_SUCCESS;

	// Track ratio of transaction successes to failures on certain transactions.
	if (pTrans->track)
		accountReportAuthAttempt(pTrans->result == BTR_SUCCESS, pTrans->ID, pTrans->webUID,
			pTrans->resultString, pTrans->uDebugAccountId);

	if(pTrans->idle)
	{
		suIdleTransactionCount--;
	}

	// Move it into a completed list
	devassertmsg(eaFind(&sppCompletedBillingTransactions, pTrans) == -1, "The same billing transaction was completed twice!");
	eaFindAndRemoveFast(&sppActiveBillingTransactions, pTrans);
	eaPush(&sppCompletedBillingTransactions, pTrans);

	DebugBillingCounts();
	PERFINFO_AUTO_STOP();
}

// Mark this transaction to be tracked for purposes of high failure rate alerting.
void btTrackOutcome(BillingTransaction *pTrans)
{
	pTrans->track = true;
}

int btGetQueuedTransactionCount()
{
	return ea32Size(&spAccountIDUpdateQueue);
}

int btGetActiveTransactionCount()
{
	return eaSize(&sppActiveBillingTransactions);
}

int btGetCompletedTransactionCount()
{
	return eaSize(&sppCompletedBillingTransactions);
}

int btGetIdleTransactionCount()
{
	return suIdleTransactionCount;
}

U32 btLastIdleTransactionQueueFill(void)
{
	return suLastIdleTransactionQueueFill;
}

U32 btLastIdleTransactionQueueCompletion(void)
{
	return suLastIdleTransactionQueueCompletion;
}

U32 btLastIdleTransactionQueueCompletionLength(void)
{
	return suLastIdleTransactionQueueCompletionLength;
}

void btWaitForAllTransactions()
{
	while(btGetActiveTransactionCount() > 0)
	{
		Sleep(1);
		billingOncePerFrame();
	}
}

void btFreeObjResult(BillingTransaction *pTrans, VindiciaXMLtoObjResult *pRes)
{
	PERFINFO_AUTO_START_FUNC();
	devassert(pRes);
	devassert(eaFind(&pTrans->ppResultsToCleanup, pRes) == -1);
	eaPush(&pTrans->ppResultsToCleanup, pRes);
	PERFINFO_AUTO_STOP();
}

#define BILLING_MEMORY_STOMP 0xCEDECEDE

static void *btAlloc_internal(size_t count, size_t size, MEM_DBG_PARMS_VOID)
{
	// Can be replaced with other allocators, but you MUST also modify btFree below
	// Incidentally, I hate that there's no good macro to avoid specifically referencing _NORMAL_BLOCK here
	return _calloc_dbg(count, size, _NORMAL_BLOCK, MEM_DBG_PARMS_CALL_VOID);
}

void *btAlloc_dbg(BillingTransaction *pTrans, size_t count, size_t size, MEM_DBG_PARMS_VOID)
{
	if (!verify(pTrans))
		return NULL;

	PERFINFO_AUTO_START_FUNC();
	if (verify(size > 0))
	{
		void *pBlock = btAlloc_internal(count, size, MEM_DBG_PARMS_CALL_VOID);
		eaPush(&pTrans->ppAllocatedMemoryBlocks, pBlock);
		PERFINFO_AUTO_STOP();
		return pBlock;
	}
	PERFINFO_AUTO_STOP();

	return NULL;
}

void *btSoapAlloc(struct soap *soap, size_t size)
{
	if (!verify(soap))
		return NULL;

	PERFINFO_AUTO_START_FUNC();
	if (verify(size > 0))
	{
		void **ppBlocks = (void**)soap->user;
		void *pBlock = btAlloc_internal(1, size, MEM_DBG_PARMS_INIT_VOID);
		eaPush(&ppBlocks, pBlock);
		soap->user = (void*)ppBlocks;
		PERFINFO_AUTO_STOP();
		return pBlock;
	}

	PERFINFO_AUTO_STOP();
	return NULL;
}

static void btFree(void *pBlock)
{
	// If you change btAlloc_internal above, you MUST also change this
	// GetAllocSize() only works because we know this is a Cryptic allocation
	PERFINFO_AUTO_START_FUNC();
	memset(pBlock, BILLING_MEMORY_STOMP, GetAllocSize(pBlock));
	free(pBlock);
	PERFINFO_AUTO_STOP();
}

void btSoapFreeAllocs(struct soap *soap)
{
	void **ppBlocks = (void**)soap->user;
	PERFINFO_AUTO_START_FUNC();
	eaDestroyEx(&ppBlocks, btFree);
	soap->user = NULL;
	PERFINFO_AUTO_STOP();
}

void * btStructCreateVoid_dbg(BillingTransaction *pTrans, ParseTable pti[], MEM_DBG_PARMS_VOID)
{
	void *pRet;
	
	if (!verify(pti))
		return NULL;

	if (!verify(pTrans))
		return NULL;

	PERFINFO_AUTO_START_FUNC();
	pRet = btAlloc_internal(1, ParserGetTableSize(pti), MEM_DBG_PARMS_CALL_VOID);
	devassert(pRet);

	StructInit_dbg(pti, pRet, NULL, MEM_DBG_PARMS_CALL_VOID);
	btStructAutoCleanup(pTrans, pti, pRet);
	PERFINFO_AUTO_STOP();

	return pRet;
}

void btStructAutoCleanup(BillingTransaction *pTrans, ParseTable pti[], void *ptr)
{
	BillingTransactionStructCleanupData *pData;

	PERFINFO_AUTO_START_FUNC();
	pData = callocStruct(struct BillingTransactionStructCleanupData);

	if (!devassert(pData))
	{
		PERFINFO_AUTO_STOP();
		return;
	}

	pData->pti = pti;
	pData->ptr = ptr;

	devassert(ptr);
	devassert(eaFind(&pTrans->ppStructsToCleanup, pData) == -1);
	
	eaPush(&pTrans->ppStructsToCleanup, pData);
	PERFINFO_AUTO_STOP();
}

char * btStrdup_dbg(BillingTransaction *pTrans, const char *pStr, MEM_DBG_PARMS_VOID)
{
	if (!verify(pTrans))
		return NULL;

	PERFINFO_AUTO_START_FUNC();

	if(pStr)
	{
		int len = (int)strlen(pStr);
		char *pRet = btAlloc_dbg(pTrans, 1, len+1, MEM_DBG_PARMS_CALL_VOID);
		memcpy(pRet, pStr, len+1);
		PERFINFO_AUTO_STOP();
		return pRet;
	}

	PERFINFO_AUTO_STOP();
	return btStrdup_dbg(pTrans, "", MEM_DBG_PARMS_CALL_VOID);
}

char * btSPrintf_dbg(BillingTransaction *pTrans, MEM_DBG_PARMS_VOID, FORMAT_STR const char* format, ...)
{
	char *str = NULL;
	char *ret;

	PERFINFO_AUTO_START_FUNC();
	VA_START(args, format);
	estrStackCreateSize(&str, 1024);
	estrConcatfv(&str, format, args);
	VA_END();

	ret = btStrdup_dbg(pTrans, str, MEM_DBG_PARMS_CALL_VOID);
	estrDestroy(&str);
	PERFINFO_AUTO_STOP();
	return ret;
}

char * btMoneyRaw_dbg(BillingTransaction *pTrans, SA_PARAM_NN_VALID const Money *money, MEM_DBG_PARMS_VOID)
{
	char *amount = 0;
	char *result;

	PERFINFO_AUTO_START_FUNC();
	estrStackCreate(&amount);
	estrFromMoneyRaw(&amount, money);
	result = btStrdup_dbg(pTrans, amount, MEM_DBG_PARMS_CALL_VOID);
	estrDestroy(&amount);
	PERFINFO_AUTO_STOP();

	return result;
}

static void billingPushAllSubscriptions()
{
	EARRAY_OF(SubscriptionContainer) ppList = getSubscriptionList();

	EARRAY_FOREACH_BEGIN(ppList, i);
	{
		btSubscriptionPush(ppList[i], NULL, NULL);
	}
	EARRAY_FOREACH_END;

	eaDestroy(&ppList); // DO NOT FREE CONTENTS
}

U32 billingGetSpendingCapPeriod(void)
{
	return gBillingConfiguration.spendingCapPeriod;
}

float billingGetSpendingCap(SA_PARAM_NN_VALID const char *pCurrency)
{
	float fCap = -1;

	PERFINFO_AUTO_START_FUNC();

	EARRAY_CONST_FOREACH_BEGIN(gBillingConfiguration.spendingCap, iCurCap, iNumCaps);
	{
		SpendingCap *pCap = gBillingConfiguration.spendingCap[iCurCap];

		if (devassert(pCap) && !stricmp_safe(pCap->currency, pCurrency))
		{
			fCap = pCap->amount;
			break;
		}
	}
	EARRAY_FOREACH_END;

	PERFINFO_AUTO_STOP_FUNC();

	return fCap;
}

const char * billingGetPrefix(void)
{
	if(gBillingConfiguration.serverType == BillingServerType_Development)
	{
		if (!verify(gBillingConfiguration.prefix && *gBillingConfiguration.prefix))
			return "DEV";

		return gBillingConfiguration.prefix;
	}

	return "";
}

float billingGetAuthFailThreshold(void)
{
	return gBillingConfiguration.authFailThreshold;
}

U32 billingGetAuthFailPeriod(void)
{
	return gBillingConfiguration.authFailPeriod;
}

U32 billingGetAuthFailMinimumCount(void)
{
	return gBillingConfiguration.authFailMinimumCount;
}

float billingGetSteamFailThreshold(void)
{
	return gBillingConfiguration.steamFailThreshold;
}

U32 billingGetSteamFailPeriod(void)
{
	return gBillingConfiguration.steamFailPeriod;
}

U32 billingGetSteamFailMinimumCount(void)
{
	return gBillingConfiguration.steamFailMinimumCount;
}

const char * billingSkipPrefix(const char *pStr)
{
	if(gBillingConfiguration.serverType == BillingServerType_Development)
	{
		if(strstr(pStr, billingGetPrefix()) == pStr)
			return (pStr + strlen(billingGetPrefix()));
	}

	return pStr;
}

char * btStrdupWithPrefix_dbg(BillingTransaction *pTrans, const char *pStr, MEM_DBG_PARMS_VOID)
{
	PERFINFO_AUTO_START_FUNC();
	if(gBillingConfiguration.serverType == BillingServerType_Development)
	{
		if(pStr)
		{
			int len = (int)strlen(pStr);
			int prefixlen = (int)strlen(billingGetPrefix());
			char *pRet = btAlloc_dbg(pTrans, 1, len+prefixlen+1, MEM_DBG_PARMS_CALL_VOID);
			memcpy(pRet, billingGetPrefix(), prefixlen);
			memcpy(pRet+prefixlen, pStr, len+1);
			PERFINFO_AUTO_STOP();
			return pRet;
		}
		else
		{
			PERFINFO_AUTO_STOP();
			return btStrdup_dbg(pTrans, billingGetPrefix(), MEM_DBG_PARMS_CALL_VOID);
		}
	}

	PERFINFO_AUTO_STOP();
	return btStrdup_dbg(pTrans, pStr, MEM_DBG_PARMS_CALL_VOID);
}

static bool billingLoadConfiguration(void);

static void billingReloadConfiguration(const char *relpath, int when)
{
	if(!stricmp_safe(sBillingCfgFilename, relpath))
	{
		billingLoadConfiguration();
	}
}

bool billingSetConfiguration(SA_PARAM_NN_VALID BillingConfiguration * pConfig)
{
	static bool bConfiguredBefore = false;

	if (!verify(pConfig)) return false;

	PERFINFO_AUTO_START_FUNC();

	// Make sure the prefix is set
	if ((pConfig->serverType == BillingServerType_Unknown)
		|| ((pConfig->serverType == BillingServerType_Development) && (!pConfig->prefix)))
	{
		AssertOrAlert("BILLING_SETTINGS", "Please provide a prefix for development mode servers.");
		PERFINFO_AUTO_STOP_FUNC();
		return false;
	}

	// Make sure the purchase log keys don't have duplicates
	eaQSort(pConfig->PurchaseLogKeys, strCmp);
	EARRAY_CONST_FOREACH_BEGIN(pConfig->PurchaseLogKeys, i, n);
	if (i > 0 && !stricmp(pConfig->PurchaseLogKeys[i], pConfig->PurchaseLogKeys[i-1]))
	{
		AssertOrAlert("BILLING_SETTINGS", "PurchaseLogKeys has duplicate elements.");
		PERFINFO_AUTO_STOP_FUNC();
		return false;
	}
	EARRAY_FOREACH_END;

	// You cannot change the username or password for Vindicia because it's cached away elsewhere
	if (bConfiguredBefore &&
		(stricmp(pConfig->vindiciaLogin, gBillingConfiguration.vindiciaLogin) ||
		stricmp(pConfig->vindiciaPassword, gBillingConfiguration.vindiciaPassword)))
	{
		AssertOrAlert("BILLING_SETTINGS", "Cannot change the billing login or password after startup.");
		PERFINFO_AUTO_STOP_FUNC();
		return false;
	}

	// Load settings.
	StructDeInit(parse_BillingConfiguration, &gBillingConfiguration);
	StructCopy(parse_BillingConfiguration, pConfig, &gBillingConfiguration, 0, 0, 0);

	bConfiguredBefore = true;

	PERFINFO_AUTO_STOP_FUNC();

	return true;
}

// Load the configuration file
static bool billingLoadConfiguration(void)
{
	static bool sbConfiguredBefore = false;
	BillingConfiguration newBillingConfiguration = {0};

	PERFINFO_AUTO_START_FUNC();

	// Migrate billing settings file if necessary.
	if (!sbConfiguredBefore)
	{
		char filepath[MAX_PATH];

		sprintf(filepath, "%sbilling_settings.txt", dbAccountDataDir());

		if (fileExists(filepath) && !fileExists(sBillingCfgFilename))
		{
			printf("Copying billing_settings.txt file to %s...\n", sBillingCfgFilename);
			ParserReadTextFile(filepath, parse_BillingConfiguration, &gBillingConfiguration, 0);
			ParserWriteTextFile(sBillingCfgFilename, parse_BillingConfiguration, &gBillingConfiguration, 0, 0);

			fileRenameToBak(filepath);
		}
	}

	// Read the billing settings.
	StructInit(parse_BillingConfiguration, &newBillingConfiguration);
	if (fileExists(sBillingCfgFilename))
	{
		ParserReadTextFile(sBillingCfgFilename, parse_BillingConfiguration, &newBillingConfiguration, 0);
	}

	// If the settings are not valid, return without using them.
	if (!billingSetConfiguration(&newBillingConfiguration))
	{
		AssertOrAlert("BILLING_SETTINGS", "Please provide a proper billing configuration in '%s'.\n"
			"\n"
			"This should be a textparser file with at least serverType and prefix set.\n"
			"The 'prefix' setting is not necessary on the Official server.\n"
			"Potential Configurations:\n"
			"\n"
			"{\n"
			"\tserverType Official\n"
			"}\n"
			"\n"
			"{\n"
			"\tserverType Development\n"
			"\tprefix jdrago\n"
			"}\n"
			"\n"
			"Do NOT use 'Official' on anything but the REAL account server! You have been warned!\n"
			, sBillingCfgFilename);
		StructDeInit(parse_BillingConfiguration, &newBillingConfiguration);
		PERFINFO_AUTO_STOP();
		return false;
	}

	StructDeInit(parse_BillingConfiguration, &newBillingConfiguration);
	if (!sbConfiguredBefore)
	{
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, sBillingCfgFilename, billingReloadConfiguration);
		sbConfiguredBefore = true;
	}
	else
	{
		printf("Billing settings reloaded.\n");
		log_printf(LOG_ACCOUNT_SERVER_GENERAL, "Billing settings reloaded.");
	}

	PERFINFO_AUTO_STOP();
	return true;
}

bool billingInit()
{
	loadstart_printf("Billing Interface: ");

	if(!billingIsEnabled())
	{
		loadend_printf("Disabled");
		return true;
	}

	stBillingTransactionUIDTable = stashTableCreateWithStringKeys(100, StashDeepCopyKeys);

	if (!billingLoadConfiguration()) return false;

	if(gBillingConfiguration.idleTransactionsQueuedOnStartup != 0)
	{
		fillIdleQueue();
	}

	loadend_printf("Enabled");
	return true;
}

void billingShutdown()
{
	gbShuttingDown = true;
	btWaitForAllTransactions();

	ea32Destroy(&spAccountIDUpdateQueue);
}

static bool queueSingleIdleTransaction()
{
	int queueCount = btGetQueuedTransactionCount();

	PERFINFO_AUTO_START_FUNC();
	if(queueCount > 0)
	{
		U32 uID = ea32Pop(&spAccountIDUpdateQueue);
		BillingTransaction *pIdleTrans = btUpdateActiveSubscriptions(uID, NULL, NULL, NULL);
		btMarkIdle(pIdleTrans);
		PERFINFO_AUTO_STOP();
		return true;
	}
	PERFINFO_AUTO_STOP();

	return false;
}

static void fillIdleQueue()
{
	ContainerIterator iter = {0};
	Container *currCon = NULL;
	int count = objCountTotalContainersWithType(GLOBALTYPE_ACCOUNT);

	PERFINFO_AUTO_START_FUNC();
	ea32SetCapacity(&spAccountIDUpdateQueue, count);

	objInitContainerIteratorFromType(GLOBALTYPE_ACCOUNT, &iter);
	currCon = objGetNextContainerFromIterator(&iter);
	while (currCon)
	{
		AccountInfo *pAccount = (AccountInfo*) currCon->containerData;
		
		if (pAccount->bBillingEnabled || !billingSkipNonBillingAccounts())
		{
			ea32Push(&spAccountIDUpdateQueue, pAccount->uID);
		}

		currCon = objGetNextContainerFromIterator(&iter);
	}
	objClearContainerIterator(&iter);
	BILLING_DEBUG("Filling idle queue ...\n");
	suLastIdleTransactionQueueFill = timeSecondsSince2000();

	seIdleTransactionState = ITS_RUNNING;
	PERFINFO_AUTO_STOP();
}

static void processIdleTransactions()
{
	if(gbShuttingDown)
	{
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	switch(seIdleTransactionState)
	{
		#define PERF_CASE(state) xcase state: PERFINFO_AUTO_START("state:"###state, 1);

		PERF_CASE(ITS_EMPTY)
		{
			// Wait for the next filling cycle
		}

		PERF_CASE(ITS_RUNNING)
		{
			while(btGetIdleTransactionCount() < (int)gBillingConfiguration.idleTransactionCountLimit)
			{
				if(!queueSingleIdleTransaction())
				{
					break;
				}
			}

			if(btGetIdleTransactionCount() == 0)
			{
				seIdleTransactionState = ITS_COMPLETE;
			}
		}

		PERF_CASE(ITS_COMPLETE)
		{
			suLastIdleTransactionQueueCompletion = timeSecondsSince2000();
			suLastIdleTransactionQueueCompletionLength = (suLastIdleTransactionQueueCompletion - suLastIdleTransactionQueueFill);

			// TODO JDRAGO Log the last complete refresh?
			BILLING_DEBUG("Idle queue emptied! Last Complete Refresh Took %d seconds.\n", suLastIdleTransactionQueueCompletionLength);

			seIdleTransactionState = ITS_EMPTY;
		}

		#undef PERF_CASE
	}

	PERFINFO_AUTO_STOP();
	PERFINFO_AUTO_STOP();
}

void UpdateSubscriptionCheck(void)
{
	static int lastDayOfWeekChecked = -1;
	static int lastHourChecked = -1;
	int currentDayOfWeek, currentHour;
	struct tm timeStruct;

	PERFINFO_AUTO_START_FUNC();

	timeMakeLocalTimeStructFromSecondsSince2000(timeSecondsSince2000(), &timeStruct);
	currentDayOfWeek = timeStruct.tm_wday;
	currentHour      = timeStruct.tm_hour;

	if (gBillingConfiguration.nightlyMode != BillingNightlyMode_None)
	{
		if (currentDayOfWeek != lastDayOfWeekChecked)
		{
			if ((currentHour == (int)gBillingConfiguration.nightlyProcessingHour) && (seIdleTransactionState != ITS_RUNNING))
			{
				switch (gBillingConfiguration.nightlyMode) {
					xcase BillingNightlyMode_IdleTransactions:
						fillIdleQueue();
					xcase BillingNightlyMode_FetchDelta:
						billingUpdateAccountSubscriptions(0);
				}
				
				lastDayOfWeekChecked = currentDayOfWeek;
			}
		}
	}
	else
	{
		// We're doing hourly instead

		if (currentHour != lastHourChecked)
		{
			billingUpdateAccountSubscriptions(0);
			lastHourChecked = currentHour;
		}
	}
	PERFINFO_AUTO_STOP();
}

void billingOncePerFrame()
{
	PERFINFO_AUTO_START_FUNC();
	coarseTimerAddInstance(NULL, __FUNCTION__);

	commMonitor(billingComm());

	UpdateSubscriptionCheck();

	PERFINFO_AUTO_START("Active Transactions", 1);
	EARRAY_FOREACH_REVERSE_BEGIN(sppActiveBillingTransactions, i);
	{
		btActiveOncePerFrame(sppActiveBillingTransactions[i]);
	}
	EARRAY_FOREACH_END;

	PERFINFO_AUTO_STOP_START("Completed Transactions", 1);

	EARRAY_FOREACH_REVERSE_BEGIN(sppCompletedBillingTransactions, i);
	{
		btCompletedOncePerFrame(sppCompletedBillingTransactions[i]);
	}
	EARRAY_FOREACH_END;

	PERFINFO_AUTO_STOP_START("Idle Transactions", 1);

	processIdleTransactions();
	PERFINFO_AUTO_STOP();

	coarseTimerStopInstance(NULL, __FUNCTION__);
	PERFINFO_AUTO_STOP();
}

SA_RET_NN_STR char * getMerchantAccountID(SA_PARAM_NN_VALID const AccountInfo *pAccount)
{
	char *estr = NULL;

	if (!verify(pAccount))
		return strdup("");

	if (!verify(pAccount->globallyUniqueID && *pAccount->globallyUniqueID))
		return strdup("");

	estrPrintf(&estr, "%s%s", billingGetPrefix(), pAccount->globallyUniqueID);
	return estr;
}

SA_RET_NN_STR char * btGetMerchantAccountID(SA_PARAM_NN_VALID BillingTransaction *pTrans, SA_PARAM_NN_VALID const AccountInfo *pAccount)
{
	char *estrMerchantAccountID = getMerchantAccountID(pAccount);
	char *pRet = btStrdup(pTrans, estrMerchantAccountID);
	estrDestroy(&estrMerchantAccountID);
	return pRet;
}

bool parseFraudSettings(SA_PARAM_NN_STR const char *pBillingCountry,
						SA_PARAM_NN_STR const char *pCVN,
						SA_PARAM_NN_STR const char *pAVS,
						SA_PARAM_OP_VALID int *pMinChargeback, // out
						SA_PARAM_OP_VALID U32 *pFlags // out
						)
{
	static const enumDivideStringPostProcessFlags eProcessFlags = DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE|
		DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS|
		DIVIDESTRING_POSTPROCESS_REMOVEUNIQUE|DIVIDESTRING_POSTPROCESS_ESTRINGS;

	STRING_EARRAY eaAcceptedCVN = NULL;
	STRING_EARRAY eaAcceptedAVS = NULL;
	const FraudSettings *pFraudSettings = getFraudSettings(pBillingCountry);
	bool bFoundAVS = false;
	bool bFoundCVN = false;

	PERFINFO_AUTO_START_FUNC();

	if (!pFraudSettings)
	{
		AssertOrAlert("ACCOUNTSERVER_BILLING_MISSING_FRAUD_SETTINGS", "Fraud settings are not configured.");
		PERFINFO_AUTO_STOP();
		return false;
	}

	DivideString(pFraudSettings->cvnAccept, ",", &eaAcceptedCVN, eProcessFlags);
	DivideString(pFraudSettings->avsAccept, ",", &eaAcceptedAVS, eProcessFlags);

	bFoundCVN = *pCVN ? eaFindString(&eaAcceptedCVN, pCVN) != -1 : false;
	bFoundAVS = *pAVS ? eaFindString(&eaAcceptedAVS, pAVS) != -1 : false;

	if (eaAcceptedCVN) eaDestroyEString(&eaAcceptedCVN);
	if (eaAcceptedAVS) eaDestroyEString(&eaAcceptedAVS);

	if (!*pCVN && pFraudSettings->acceptMissing) bFoundCVN = true;
	if (!*pAVS && pFraudSettings->acceptMissing) bFoundAVS = true;

	if (pFlags)
	{
		if (!bFoundCVN) *pFlags |= INVALID_CVN;
		if (!bFoundAVS) *pFlags |= INVALID_AVS;
	}

	if (pMinChargeback) *pMinChargeback = pFraudSettings->minChargebackProbability;

	PERFINFO_AUTO_STOP();
	return true;
}

SA_RET_OP_VALID const char *billingGetValidationProduct(void)
{
	return gBillingConfiguration.validationProduct;
}

U32 billingGetDeactivatePaymentMethodTries(void)
{
	return gBillingConfiguration.deactivatePaymentMethodTries;
}

U32 billingGetMaxActivities(void)
{
	return gBillingConfiguration.maxActivities;
}

U32 billingGetActivityPeriod(void)
{
	return gBillingConfiguration.activityPeriod;
}

int billingGetVindiciaLogLevel(void)
{
	return gBillingConfiguration.vindiciaLogLevel;
}

SA_RET_OP_VALID const char *billingGetStunnelHost(void)
{
	return gBillingConfiguration.stunnelHost;
}

SA_RET_OP_STR const char *billingGetSteamURL(bool bLive)
{
	if (!gBillingConfiguration.steamConfig)
	{
		return NULL;
	}

	if (bLive)
	{
		return gBillingConfiguration.steamConfig->liveURL;
	}
	else
	{
		return gBillingConfiguration.steamConfig->sandboxURL;
	}
}

U32 billingGetSteamPort(void)
{
	return gBillingConfiguration.steamConfig ? gBillingConfiguration.steamConfig->port : 0;
}

SA_RET_OP_STR const char *billingGetSteamVersion(void)
{
	return gBillingConfiguration.steamConfig ? gBillingConfiguration.steamConfig->apiVersion : NULL;
}

SA_RET_OP_VALID const SteamGameConfig *billingGetSteamGameConfig(SA_PARAM_NN_VALID const char *pProduct)
{
	return eaIndexedGetUsingString(&gBillingConfiguration.steamGameConfigs, pProduct);
}

SA_RET_OP_VALID const ShardConfig *billingGetShardConfig(SA_PARAM_NN_VALID const char *pProxy)
{
	return eaIndexedGetUsingString(&gBillingConfiguration.shardConfigs, pProxy);
}

U32 billingGetFetchDeltaAutobillPageSize(void)
{
	return gBillingConfiguration.fetchDeltaAutobillPageSize;
}

BillingServerType billingGetServerType(void)
{
	return gBillingConfiguration.serverType;
}

SA_RET_OP_STR const char * billingGetDivision(DivisionType eType, SA_PARAM_OP_STR const char * pInternalName)
{
	if (!pInternalName) return NULL;

	PERFINFO_AUTO_START_FUNC();

	EARRAY_CONST_FOREACH_BEGIN(gBillingConfiguration.divisions, iCurDivision, iNumDivisions);
	{
		Division * pDivision = gBillingConfiguration.divisions[iCurDivision];

		if (eType == DT_Subscription)
		{
			EARRAY_CONST_FOREACH_BEGIN(pDivision->subscriptionInternalNames, iCurSub, iNumSubs);
			{
				if (!stricmp_safe(pDivision->subscriptionInternalNames[iCurSub], pInternalName))
				{
					PERFINFO_AUTO_STOP();
					return pDivision->name;
				}
			}
			EARRAY_FOREACH_END;
		}
		else if (eType == DT_Product)
		{
			EARRAY_CONST_FOREACH_BEGIN(pDivision->productInternalNames, iCurSub, iNumSubs);
			{
				if (!stricmp_safe(pDivision->productInternalNames[iCurSub], pInternalName))
				{
					PERFINFO_AUTO_STOP();
					return pDivision->name;
				}
			}
			EARRAY_FOREACH_END;
		}
		else
		{
			AssertOrAlert("ACCOUNT_SERVER_DIVISION", "Invalid division type requested.");
		}
	}
	EARRAY_FOREACH_END;

	PERFINFO_AUTO_STOP();
	return NULL;
}

SA_RET_OP_VALID AccountInfo * findAccountByMerchantAccountID(SA_PARAM_NN_STR const char *pMerchantID)
{
	const char *prefix = billingGetPrefix();
	unsigned int i;
	size_t prefixLen = strlen(prefix);

	if (strlen(pMerchantID) <= prefixLen) return NULL;

	for (i = 0; i < prefixLen; ++i)
	{
		if (prefix[i] != pMerchantID[i]) return NULL;
	}

	pMerchantID += prefixLen;

	return findAccountByGUID(pMerchantID);
}

typedef struct FetchDeltaTimes
{
	U32 uStartTime;
	U32 uFetchTime;
} FetchDeltaTimes;

// Set to false if, for some reason, the Account Server thinks it is doing a fetch delta when it isn't
static bool gbFetchDeltaRunning = false;
AUTO_CMD_INT(gbFetchDeltaRunning, FetchDeltaRunning) ACMD_CMDLINE ACMD_CATEGORY(Account_Server_Billing);

static void billingUpdateAccountSubscriptionsContinue(bool bSuccess, SA_PARAM_NN_VALID FetchDeltaTimes *pTimes)
{
	PERFINFO_AUTO_START_FUNC();
	if (bSuccess)
	{
		asgSetLastNightlyWalk(pTimes->uStartTime);
		BILLING_DEBUG("Finished fetch delta process.\n");
	}
	else
	{
		BILLING_DEBUG("Gave up on fetch delta process.\n");
	}

	gbFetchDeltaRunning = false;

	free(pTimes);
	PERFINFO_AUTO_STOP();
}

void billingUpdateAccountSubscriptions(U32 uSecondsSince2000)
{
	FetchDeltaTimes *pTimes;

	PERFINFO_AUTO_START_FUNC();
	
	if (gbFetchDeltaRunning)
	{
		BILLING_DEBUG_WARNING("Not starting fetch delta process because it is already running.\n");
		PERFINFO_AUTO_STOP();
		return;
	}

	BILLING_DEBUG("Starting fetch delta process.\n");

	pTimes = callocStruct(FetchDeltaTimes);

	if (!uSecondsSince2000) uSecondsSince2000 = asgGetLastNightlyWalk();
	if (!uSecondsSince2000) uSecondsSince2000 = timeSecondsSince2000();

	pTimes->uFetchTime = uSecondsSince2000;
	pTimes->uStartTime = timeSecondsSince2000();

	gbFetchDeltaRunning = true;

	btFetchAutoBillsSince(uSecondsSince2000, billingUpdateAccountSubscriptionsContinue, pTimes);
	PERFINFO_AUTO_STOP();
}

// Convert a tax classification into it's Vindicia form
enum vin__TaxClassification btConvertTaxClassification(TaxClassification eTaxClassification)
{
	switch (eTaxClassification)
	{
		xcase TCPhysicalGoods: return vin__TaxClassification__PhysicalGoods;
		xcase TCDownloadableExecutableSoftware: return vin__TaxClassification__DownloadableExecutableSoftware;
		xcase TCDownloadableElectronicData: return vin__TaxClassification__DownloadableElectronicData;
		xcase TCService: return vin__TaxClassification__Service;
		xcase TCTaxExempt: return vin__TaxClassification__TaxExempt;
		xcase TCOtherTaxable: return vin__TaxClassification__OtherTaxable;
	}

	AssertOrAlert("ACCOUNTSERVER_TAXCLASSIFICATION", "An attempt to translate a tax classification into its Vindicia form was attempted on an invalid type! This probably means a product is not configured properly.");

	return vin__TaxClassification__TaxExempt;
}

// Log fetch delta stats
void billingLogFetchDeltaStats(FetchDeltaType eType, SA_PARAM_NN_VALID const FetchDeltaStats *pStats)
{
	char fetchDate[256];
	char startDate[256];
	char endDate[256];

	PERFINFO_AUTO_START_FUNC();
	timeMakeLocalDateStringFromSecondsSince2000(fetchDate, pStats->Time);
	timeMakeLocalDateStringFromSecondsSince2000(startDate, pStats->StartedTime);
	timeMakeLocalDateStringFromSecondsSince2000(endDate, pStats->FinishedTime);

	BILLING_DEBUG("Fetched from: %s\n", fetchDate);
	BILLING_DEBUG("Started on: %s\n", startDate);
	BILLING_DEBUG("Finished on: %s\n", endDate);
	BILLING_DEBUG("Total processed: %"FORM_LL"d\n", pStats->NumProcessed);
	BILLING_DEBUG("Total received (okay to be higher): %"FORM_LL"d\n", pStats->NumTotal);
	BILLING_DEBUG("Page size: %"FORM_LL"d\n", pStats->PageSize);

	asgSetFetchDeltaStats(eType, pStats);
	PERFINFO_AUTO_STOP();
}

SA_RET_NN_VALID struct vin__PaymentMethod *
btCreateVindiciaPaymentMethod(SA_PARAM_OP_VALID const AccountInfo *pAccount,
							  SA_PARAM_NN_VALID BillingTransaction *pTrans,
							  SA_PARAM_NN_VALID const PaymentMethod *pPaymentMethod,
							  bool bForceAll,
							  SA_PARAM_OP_STR const char * pBankName)
{
	struct vin__PaymentMethod *pVinPM = NULL;
	
	PERFINFO_AUTO_START_FUNC();
	pVinPM = btAlloc(pTrans, pVinPM, struct vin__PaymentMethod);
	devassert(pVinPM);

	if (pPaymentMethod->VID && *pPaymentMethod->VID && !bForceAll)
	{
		pVinPM->VID = btStrdup(pTrans, pPaymentMethod->VID);
	}
	else
	{
		pVinPM->VID = btStrdupNonNULL(pTrans, pPaymentMethod->VID);
		pVinPM->accountHolderName = btStrdupNonNULL(pTrans, pPaymentMethod->accountHolderName);
		pVinPM->active = btAlloc(pTrans, pVinPM->active, enum vin__PaymentMethodType);
		*pVinPM->active = pPaymentMethod->active ? xsd__boolean__true_ : xsd__boolean__false_;
		pVinPM->billingAddress = btAlloc(pTrans, pVinPM->billingAddress, struct vin__Address);
		pVinPM->billingAddress->addr1 = btStrdupNonNULL(pTrans, pPaymentMethod->addr1);
		pVinPM->billingAddress->addr2 = btStrdupNonNULL(pTrans, pPaymentMethod->addr2);
		pVinPM->billingAddress->city = btStrdupNonNULL(pTrans, pPaymentMethod->city);
		pVinPM->billingAddress->country = btStrdupNonNULL(pTrans, pPaymentMethod->country);
		pVinPM->billingAddress->county = btStrdupNonNULL(pTrans, pPaymentMethod->county);
		pVinPM->billingAddress->district = btStrdupNonNULL(pTrans, pPaymentMethod->district);
		pVinPM->billingAddress->name = btStrdupNonNULL(pTrans, pPaymentMethod->addressName);
		pVinPM->billingAddress->phone = btStrdupNonNULL(pTrans, pPaymentMethod->phone);
		pVinPM->billingAddress->postalCode = btStrdupNonNULL(pTrans, pPaymentMethod->postalCode);

		if (pPaymentMethod->creditCard)
		{
			devassertmsg(!pPaymentMethod->payPal, "Cannot have both credit card and PayPal at the same time!");
			devassertmsg(!pPaymentMethod->directDebit, "Cannot have both credit card and Direct Debit at the same time!");

			pVinPM->creditCard = btAlloc(pTrans, pVinPM->creditCard, struct vin__CreditCard);
			pVinPM->creditCard->account = btStrdupNonNULL(pTrans, pPaymentMethod->creditCard->account);
			pVinPM->creditCard->expirationDate = btStrdupNonNULL(pTrans, pPaymentMethod->creditCard->expirationDate);

			if (pPaymentMethod->creditCard->CVV2)
			{
				pVinPM->nameValues = btAlloc(pTrans, pVinPM->nameValues, struct ArrayOfNameValuePairs);
				pVinPM->nameValues->__size = 1;
				pVinPM->nameValues->__ptr = btAlloc(pTrans, pVinPM->nameValues->__ptr, struct vin__NameValuePair *);
				*pVinPM->nameValues->__ptr = btAlloc(pTrans, *pVinPM->nameValues->__ptr, struct vin__NameValuePair);
				pVinPM->nameValues->__ptr[0]->name = btStrdup(pTrans, "CVN");
				pVinPM->nameValues->__ptr[0]->value = btStrdupNonNULL(pTrans, pPaymentMethod->creditCard->CVV2);
			}

			pVinPM->type = btAlloc(pTrans, pVinPM->type, enum vin__PaymentMethodType);
			*pVinPM->type = vin__PaymentMethodType__CreditCard;
		}
		else if (pPaymentMethod->payPal)
		{
			devassertmsg(!pPaymentMethod->creditCard, "Cannot have both credit card and PayPal at the same time!");
			devassertmsg(!pPaymentMethod->directDebit, "Cannot have both PayPal and Direct Debit at the same time!");

			pVinPM->paypal = btAlloc(pTrans, pVinPM->paypal, struct vin__PayPal);
			pVinPM->paypal->cancelUrl = btStrdupNonNULL(pTrans, pPaymentMethod->payPal->cancelUrl);
			pVinPM->paypal->emailAddress = btStrdupNonNULL(pTrans, pPaymentMethod->payPal->emailAddress);
			pVinPM->paypal->password = btStrdupNonNULL(pTrans, pPaymentMethod->payPal->password);
			pVinPM->paypal->returnUrl = btStrdupNonNULL(pTrans, pPaymentMethod->payPal->returnUrl);

			pVinPM->type = btAlloc(pTrans, pVinPM->type, enum vin__PaymentMethodType);
			*pVinPM->type = vin__PaymentMethodType__PayPal;
		}
		else if (pPaymentMethod->directDebit)
		{
			devassertmsg(!pPaymentMethod->payPal, "Cannot have both  Direct Debit and PayPal at the same time!");
			devassertmsg(!pPaymentMethod->creditCard, "Cannot have both credit card and Direct Debit at the same time!");

			pVinPM->directDebit = btAlloc(pTrans, pVinPM->directDebit, struct vin__DirectDebit);
			pVinPM->directDebit->account = btStrdupNonNULL(pTrans, pPaymentMethod->directDebit->account);
			pVinPM->directDebit->bankSortCode = btStrdupNonNULL(pTrans, pPaymentMethod->directDebit->bankSortCode);
			pVinPM->directDebit->countryCode = btStrdupNonNULL(pTrans, pPaymentMethod->country);
			pVinPM->directDebit->ribCode = btStrdupNonNULL(pTrans, pPaymentMethod->directDebit->ribCode);

			pVinPM->type = btAlloc(pTrans, pVinPM->type, enum vin__PaymentMethodType);
			*pVinPM->type = vin__PaymentMethodType__DirectDebit;
		}
		else if (pPaymentMethod->VID && *pPaymentMethod->VID && pAccount)
		{	
			if (pPaymentMethod->precreated)
			{
				// This happens when the payment method was already created in Vindicia by
				// the billing platform. After creating it, we end up here while doing the
				// $1 auth/cancel.
				pVinPM->type = btAlloc(pTrans, pVinPM->type, enum vin__PaymentMethodType);
				if (pBankName && *pBankName) // Only EDD provides a bank name
				{					
					*pVinPM->type = vin__PaymentMethodType__DirectDebit;
				}
				else // Currently, PayPal or other types of PMs are not supported
				{    // and we have no good way of detecting them, so default to Credit Card
					*pVinPM->type = vin__PaymentMethodType__CreditCard;
				}
			}
			else
			{
				// See if we can determine the type by looking at a cached PM
				const CachedPaymentMethod *pCachedPM = getCachedPaymentMethod(pAccount, pPaymentMethod->VID);

				if (pCachedPM)
				{
					if (pCachedPM->creditCard)
					{
						pVinPM->type = btAlloc(pTrans, pVinPM->type, enum vin__PaymentMethodType);
						*pVinPM->type = vin__PaymentMethodType__CreditCard;
					}
					else if (pCachedPM->payPal)
					{
						pVinPM->type = btAlloc(pTrans, pVinPM->type, enum vin__PaymentMethodType);
						*pVinPM->type = vin__PaymentMethodType__PayPal;
					}
					else if (pCachedPM->directDebit)
					{
						pVinPM->type = btAlloc(pTrans, pVinPM->type, enum vin__PaymentMethodType);
						*pVinPM->type = vin__PaymentMethodType__DirectDebit;
					}

					devassertmsg(pVinPM->type, "Could not determine payment method type using cache.");
				}
				else
				{
					BILLING_DEBUG_WARNING("VID of payment method not found on account.");
					AssertOrAlert("ACCOUNTSERVER_INVALID_PM_VID",
						"VID (%s) of payment method not found for account (%s). This indicates a bug in the web site or account server.",
						pPaymentMethod->VID, pAccount->accountName);
				}
			}
		}
		else
		{
			static const char *pError = "Invalid payment method conversion.";
			BILLING_DEBUG_WARNING("%s", pError);
			AssertOrAlert("ACCOUNTSERVER_INVALID_PM_CONVERSION", "%s", pError);
		}

		pVinPM->currency = convertCurrencyCase(btStrdup(pTrans, pPaymentMethod->currency));
		pVinPM->customerDescription = btStrdupNonNULL(pTrans, pPaymentMethod->customerDescription);
		pVinPM->customerSpecifiedType = btStrdupNonNULL(pTrans, pPaymentMethod->customerSpecifiedType);
	}

	PERFINFO_AUTO_STOP();
	return pVinPM;
}

void btSetVindiciaTransStatus(SA_PARAM_NN_VALID BillingTransaction *pTrans,
							  SA_PARAM_OP_VALID struct vin__TransactionStatus *pStatus)
{
	if (!pStatus) return;

	if (pStatus->payPalStatus)
	{
		struct vin__TransactionStatusPayPal *pPayPal = pStatus->payPalStatus;

		pTrans->payPalStatus = btAlloc(pTrans, pTrans->payPalStatus, PayPalStatus);
		pTrans->payPalStatus->authCode = btStrdup(pTrans, pPayPal->authCode);
		pTrans->payPalStatus->redirectUrl = btStrdup(pTrans, pPayPal->redirectUrl);
		pTrans->payPalStatus->token = btStrdup(pTrans, pPayPal->token);
	}
}

// Convert a Vindicia subscription status to ours
SubscriptionStatus convertSubStatusFromVindicia(enum vin__AutoBillStatus eStatus)
{
	switch(eStatus)
	{
		xcase vin__AutoBillStatus__Active:    return SUBSCRIPTIONSTATUS_ACTIVE;
		xcase vin__AutoBillStatus__Suspended: return SUBSCRIPTIONSTATUS_SUSPENDED;
		xcase vin__AutoBillStatus__Cancelled: return SUBSCRIPTIONSTATUS_CANCELLED;
		xcase vin__AutoBillStatus__Upgraded:  return SUBSCRIPTIONSTATUS_ACTIVE;
		xcase vin__AutoBillStatus__PendingCustomerAction: return SUBSCRIPTIONSTATUS_PENDINGCUSTOMER;
	}

	return SUBSCRIPTIONSTATUS_INVALID;
}

// Determine if a Vindicia expiration date is in a valid format or not
static bool isValidVindiciaExpirationDate(SA_PARAM_OP_STR const char *pDate)
{
	if (!pDate) return false;
	if (!*pDate) return false;
	if (strlen(pDate) == 6) return true;
	return false;
}

// Get the expiration month from a Vindicia expiration date
static int getVindiciaExpireMonth(SA_PARAM_OP_STR const char *pExpireDate)
{
	int iMonth = 0;

	if (!isValidVindiciaExpirationDate(pExpireDate)) return -1;
	
	iMonth = atoi(pExpireDate + 4);

	if (iMonth < 1 || iMonth > 12) iMonth = -1;

	return iMonth;
}

// Get the expiration year from a Vindicia expiration date
static int getVindiciaExpireYear(SA_PARAM_OP_STR const char *pExpireDate)
{
	int iYear = 0;
	char pYearString[5];
	
	if (!isValidVindiciaExpirationDate(pExpireDate)) return -1;

	strncpy(pYearString, pExpireDate, 4);
	pYearString[4] = 0;

	return atoi(pYearString);
}

// Populate a cached account subscription payment method information from an autobill
void populateSubPaymentMethodFromVindicia(SA_PARAM_NN_VALID NOCONST(CachedAccountSubscription) *pCachedSub,
										  SA_PARAM_NN_VALID const struct vin__PaymentMethod *pVinPaymentMethod)
{
	struct vin__CreditCard *pVinCC = NULL;

	if (!verify(pCachedSub)) return;
	if (!verify(pVinPaymentMethod)) return;

	estrCopy2(&pCachedSub->PaymentMethodVID, pVinPaymentMethod->VID);

	// Nothing else to do if it isn't a credit card
	if (!pVinPaymentMethod->creditCard) return;
	if (!pVinPaymentMethod->type) return;
	if (*pVinPaymentMethod->type != vin__PaymentMethodType__CreditCard) return;

	pVinCC = pVinPaymentMethod->creditCard;

	estrCopy2(&pCachedSub->creditCardLastDigits, pVinCC->lastDigits);
	pCachedSub->creditCardExpirationMonth = getVindiciaExpireMonth(pVinCC->expirationDate);
	pCachedSub->creditCardExpirationYear  = getVindiciaExpireYear(pVinCC->expirationDate);
}

SA_RET_OP_VALID static NOCONST(CachedAccountSubscription) *
createCachedAccountSubscription(SA_PARAM_OP_VALID const CachedAccountSubscription *pExistingCachedSub)
{
	NOCONST(CachedAccountSubscription) *pCachedSub = StructCreateNoConst(parse_CachedAccountSubscription);

	// Fill in the bits from an existing cached sub if we have one
	if (pExistingCachedSub)
	{
		// Once billing is on once, it should always stay on
		pCachedSub->bBilled = pExistingCachedSub->bBilled;

		// Better defaults, if we have them
		pCachedSub->entitlementEndTimeSS2000 = pExistingCachedSub->entitlementEndTimeSS2000;
		pCachedSub->estimatedCreationTimeSS2000 = pExistingCachedSub->estimatedCreationTimeSS2000;
		pCachedSub->nextBillingDateSS2000 = pExistingCachedSub->nextBillingDateSS2000;
	}

	return pCachedSub;
}

static U32 getVindiciaTimestamp(SA_PARAM_OP_VALID const time_t *pVinTime)
{
	return pVinTime ? timeGetSecondsSince2000FromWindowsTime32(MAX(*pVinTime, 0)) : 0;
}

// Create a cached account subscription from a Vindicia autobill
SA_RET_OP_VALID NOCONST(CachedAccountSubscription) *
createCachedAccountSubscriptionFromAutoBill(
	SA_PARAM_NN_VALID const AccountInfo *pAccount,
	SA_PARAM_NN_VALID struct vin__AutoBill *pAutoBill)
{
	NOCONST(CachedAccountSubscription) *pCachedSub = NULL;
	const CachedAccountSubscription *pExistingCachedSub = NULL;
	const SubscriptionContainer *pSubscriptionPlan = NULL;
	const char *pSubscriptionName = NULL;

	if (!verify(pAccount)) return NULL;
	if (!verify(pAutoBill)) return NULL;

	if(!pAutoBill->billingPlan || !pAutoBill->billingPlan->merchantEntitlementIds)
		return NULL;

	if(pAutoBill->billingPlan->merchantEntitlementIds->__size < 1)
		return NULL;

	if (!pAutoBill->billingPlan->merchantEntitlementIds->__ptr)
		return NULL;

	if (!pAutoBill->billingPlan->merchantEntitlementIds->__ptr[0])
		return NULL;

	pSubscriptionName = pAutoBill->billingPlan->merchantEntitlementIds->__ptr[0]->id;
	if (!pSubscriptionName || !*pSubscriptionName)
		return NULL;

	pSubscriptionPlan = findSubscriptionByName(pSubscriptionName);
	if (!pSubscriptionPlan)
		return NULL;

	if(strcmp(pSubscriptionPlan->pInternalName, pAutoBill->merchantAffiliateId))
		return NULL;

	if(!pAutoBill->startTimestamp)
		return NULL;

	PERFINFO_AUTO_START_FUNC();

	pExistingCachedSub = findAccountSubscriptionByVID(pAccount, pAutoBill->VID);
	pCachedSub = createCachedAccountSubscription(pExistingCachedSub);
	pCachedSub->uSubscriptionID = pSubscriptionPlan->uID;
	estrCopy2(&pCachedSub->name, pSubscriptionPlan->pName);
	estrCopy2(&pCachedSub->internalName, pSubscriptionPlan->pInternalName);
	estrCopy2(&pCachedSub->vindiciaID, pAutoBill->VID);
	pCachedSub->gameCard = pSubscriptionPlan->gameCard;
	pCachedSub->vindiciaStatus = pAutoBill->status ? convertSubStatusFromVindicia(*pAutoBill->status) : SUBSCRIPTIONSTATUS_CANCELLED;

	pCachedSub->startTimeSS2000 = getVindiciaTimestamp(pAutoBill->startTimestamp);
	if (!pCachedSub->startTimeSS2000)
	{
		AssertOrAlert("ACCOUNTSERVER_VINDICIA_AUTOBILL_START_INVALID",
			"Vindicia returned an autobill (vid:%s) with an invalid start timestamp (account:%s).  Please contact Vindicia.",
			pAutoBill->VID ? pAutoBill->VID : "missing", pAccount->globallyUniqueID);
	}
	
	pCachedSub->entitlementEndTimeSS2000 = getVindiciaTimestamp(pAutoBill->endTimestamp);
	if (!pCachedSub->entitlementEndTimeSS2000 &&
		pCachedSub->vindiciaStatus != SUBSCRIPTIONSTATUS_PENDINGCUSTOMER &&
		pCachedSub->vindiciaStatus != SUBSCRIPTIONSTATUS_SUSPENDED)
	{
		if (pAutoBill->paymentMethod && pAutoBill->paymentMethod->type &&
			*pAutoBill->paymentMethod->type == vin__PaymentMethodType__PayPal)
		{
			// Vindicia will sometimes return AutoBills that are both active
			// and have an end timestamp in 1969 for PayPal AutoBills that
			// have not yet entered the PendingCustomerAction state.
			// This is "by design": see Vindicia Jira CTS-1501
			pCachedSub->entitlementEndTimeSS2000 = pCachedSub->startTimeSS2000;
		}
		else
		{
			AssertOrAlert("ACCOUNTSERVER_VINDICIA_AUTOBILL_END_INVALID",
				"Vindicia returned an autobill (vid:%s) with an invalid end timestamp (account:%s).  Please contact Vindicia.",
				pAutoBill->VID ? pAutoBill->VID : "missing", pAccount->globallyUniqueID);
		}
	}

	pCachedSub->nextBillingDateSS2000 = 0;

	// Try to get the next billing date from the included future rebills.
	if (pAutoBill->futureRebills && pAutoBill->futureRebills->__ptr && pAutoBill->futureRebills->__size > 0)
	{
		int iCurTransaction = 0;
		U32 uMinNextBillingDate = 0;

		for (iCurTransaction = 0; iCurTransaction < pAutoBill->futureRebills->__size; iCurTransaction++)
		{
			const struct vin__Transaction *pTransaction = pAutoBill->futureRebills->__ptr[iCurTransaction];

			if (pTransaction)
			{
				U32 uTimestamp = getVindiciaTimestamp(pTransaction->timestamp);

				if (uTimestamp)
				{
					uMinNextBillingDate = uMinNextBillingDate ? MIN(uMinNextBillingDate, uTimestamp) : uTimestamp;
				}
			}
		}

		pCachedSub->nextBillingDateSS2000 = uMinNextBillingDate;
	}

	// If we still don't have a next billing date, use the entitlement end time or start time.
	if (!pCachedSub->nextBillingDateSS2000)
	{
		pCachedSub->nextBillingDateSS2000 = MAX(pCachedSub->startTimeSS2000, pCachedSub->entitlementEndTimeSS2000);
	}

	// Populate the credit card information if it is available.
	if(pAutoBill->paymentMethod)
	{
		populateSubPaymentMethodFromVindicia(pCachedSub, pAutoBill->paymentMethod);
	}

	PERFINFO_AUTO_STOP();
	return pCachedSub;
}

void btInitializeNameValues(SA_PARAM_NN_VALID BillingTransaction *pTrans,
							SA_PARAM_NN_VALID struct ArrayOfNameValuePairs **ppPairs,
							unsigned int uSize)
{
	unsigned int uCurPair = 0;

	if (!verify(pTrans)) return;
	if (!verify(ppPairs)) return;
	if (!uSize) return;

	PERFINFO_AUTO_START_FUNC();
	*ppPairs = btAlloc(pTrans, *ppPairs, struct ArrayOfNameValuePairs);
	(*ppPairs)->__size = uSize;
	(*ppPairs)->__ptr = btAllocCount(pTrans, (*ppPairs)->__ptr, struct vin__NameValuePair *, uSize);
	
	for (uCurPair = 0; uCurPair < uSize; uCurPair++)
	{
		(*ppPairs)->__ptr[uCurPair] = btAlloc(pTrans, (*ppPairs)->__ptr[uCurPair], struct vin__NameValuePair);
	}
	PERFINFO_AUTO_STOP();
}

void btSetNameValuePair(SA_PARAM_NN_VALID BillingTransaction *pTrans,
						SA_PARAM_NN_VALID struct ArrayOfNameValuePairs *pPairs,
						unsigned int uIndex,
						SA_PARAM_OP_STR const char *pName,
						SA_PARAM_OP_STR const char *pValue)
{
	if (!verify(pTrans)) return;
	if (!verify(pPairs)) return;
	if (!verify(pPairs->__ptr)) return;
	if (!verify(pPairs->__ptr[uIndex])) return;

	PERFINFO_AUTO_START_FUNC();
	pPairs->__ptr[uIndex]->name = btStrdupNonNULL(pTrans, pName);
	pPairs->__ptr[uIndex]->value = btStrdupNonNULL(pTrans, pValue);
	PERFINFO_AUTO_STOP();
}

// Requires three name-value slots
void btSetMandateNameValues(SA_PARAM_NN_VALID BillingTransaction *pTrans,
							SA_PARAM_NN_VALID struct ArrayOfNameValuePairs *pPairs,
							unsigned int uIndexStart,
							SA_PARAM_OP_STR const char *pMandateVersion,
							SA_PARAM_OP_STR const char *pBankName)
{
	PERFINFO_AUTO_START_FUNC();
	btSetNameValuePair(pTrans, pPairs, uIndexStart++, "vin:MandateFlag", "1");
	btSetNameValuePair(pTrans, pPairs, uIndexStart++, "vin:MandateVersion", pMandateVersion);
	btSetNameValuePair(pTrans, pPairs, uIndexStart++, "vin:MandateBankName", pBankName);
	PERFINFO_AUTO_STOP();
}

void btSetInterestingNameValues(SA_PARAM_NN_VALID BillingTransaction *pTrans,
								SA_PARAM_NN_VALID struct ArrayOfNameValuePairs **ppPairs,
								SA_PARAM_OP_STR const char *pDivision,
								SA_PARAM_OP_STR const char *pBillingCountry,
								SA_PARAM_OP_STR const char *pBankName)
{
	int iNumKeyValues = 0;
	bool bIncludeMandate = pBankName && *pBankName && mandateExistsForCountry(pBillingCountry);
	int iCurKeyValue = 0;

	PERFINFO_AUTO_START_FUNC();

	if (pDivision)
	{
		iNumKeyValues += 1;
	}

	if (bIncludeMandate)
	{
		iNumKeyValues += VINDICIA_MANDATE_NUM_NAME_VALUES;
	}

	if (iNumKeyValues > 0)
	{
		btInitializeNameValues(pTrans, ppPairs, iNumKeyValues);

		if (pDivision)
		{
			btSetNameValuePair(pTrans, *ppPairs, iCurKeyValue, VINDICIA_DIVISION_KEY_NAME, pDivision);
			iCurKeyValue += 1;
		}

		if (bIncludeMandate)
		{
			btSetMandateNameValues(pTrans, *ppPairs, iCurKeyValue,
				getMandateVersion(pBillingCountry), pBankName);
			iCurKeyValue += VINDICIA_MANDATE_NUM_NAME_VALUES;
		}
	}

	devassert(iCurKeyValue == iNumKeyValues);
	PERFINFO_AUTO_STOP();
}

const char *getMandateVersion(const char *pCountry)
{
	int iIndex = -1;

	if (!verify(pCountry)) return NULL;

	iIndex = eaIndexedFindUsingString(&gBillingConfiguration.directDebitMandates, pCountry);

	if (iIndex < 0) return NULL;

	if (!devassert(gBillingConfiguration.directDebitMandates[iIndex])) return NULL;

	return gBillingConfiguration.directDebitMandates[iIndex]->version;
}

#include "billing_h_ast.c"
