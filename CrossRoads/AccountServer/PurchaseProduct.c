#include "PurchaseProduct.h"

#include "AccountLog.h"
#include "AccountManagement.h"
#include "AccountReporting.h"
#include "AccountTransactionLog.h"
#include "AutoTransDefs.h"
#include "billing.h"
#include "Discount.h"
#include "ErrorStrings.h"
#include "KeyValues/KeyValueChain.h"
#include "KeyValues/KeyValues.h"
#include "KeyValues/VirtualCurrency.h"
#include "Money.h"
#include "objTransactions.h"
#include "Product.h"
#include "ProxyInterface/AccountProxy.h"
#include "sock.h"
#include "Steam.h"
#include "strings_opt.h"
#include "StringUtil.h"
#include "Transaction/billingTransaction.h"
#include "Transaction/transactionQuery.h"
#include "UpdatePaymentMethod.h"

#include "PurchaseProduct_h_ast.h"

#include "AutoGen/AccountServer_autotransactions_autogen_wrappers.h"

// Enable new purchase errors - this should only be done after game builds that support it are live.
static bool gbEnableNewPurchaseErrors = false;
AUTO_CMD_INT(gbEnableNewPurchaseErrors, EnableNewPurchaseErrors) ACMD_CMDLINE ACMD_CATEGORY(Account_Server);

/************************************************************************/
/* Macros                                                               */
/************************************************************************/

// Call the callback if it exists
#define DO_CALLBACK(result) if (pPurchaseSession->pCallbackInfo && pPurchaseSession->pCallbackInfo->pCallback) { PERFINFO_AUTO_START("Callback", 1); pPurchaseSession->pCallbackInfo->pCallback((result), pTrans, pPurchaseSession, pPurchaseSession->pCallbackInfo->pUserData); PERFINFO_AUTO_STOP(); } (0)

// Call the callback and return, cleaning up if it not pending and committing if it is success
#define DO_CALLBACK_AND_RETURN_INNER(result) DO_CALLBACK(result); if ((result) != PURCHASE_RESULT_PENDING) { freePurchaseSession(pPurchaseSession, (result)); } PERFINFO_AUTO_STOP_FUNC(); return
#define DO_CALLBACK_AND_RETURN(result) { DO_CALLBACK_AND_RETURN_INNER(result) (result); }
#define DO_CALLBACK_AND_RETURN_VOID(result) { DO_CALLBACK_AND_RETURN_INNER(result); }

// Special case: call the callback and don't return, but clean up the purchase session
#define DO_CALLBACK_NO_RETURN(result) DO_CALLBACK(result); if ((result) != PURCHASE_RESULT_PENDING) { freePurchaseSession(pPurchaseSession, (result)); } PERFINFO_AUTO_STOP_FUNC()

// If the check is not true, call the callback and give the error
#define CHECK(check, error) if (!(check)) { PERFINFO_AUTO_STOP(); DO_CALLBACK_AND_RETURN(error); }

// Determine if a letter is valid for a real currency
#define VALID_CURRENCY_CHAR(c) (((c) >= 'A' && (c) <= 'Z') || ((c) >= 'a' && (c) <= 'z'))

// Length of a valid real currency
#define VALID_CURRENCY_LENGTH 3

/************************************************************************/
/* Structures                                                           */
/************************************************************************/

// Flags
#define PURCHASEINFO_COMPLETE_TRANS		BIT(0) // Complete the billing transaction on cleanup
#define PURCHASEINFO_COMPLETE_COMMIT	BIT(1) // Commit on success
#define PURCHASEINFO_POINTS				BIT(2) // Uses points

// Stores callback information
typedef struct CallbackInfo
{
	PurchaseCallback pCallback;
	void *pUserData;
} CallbackInfo;

// Stores session information for a purchase
typedef struct PurchaseSession
{
	AccountInfo *pAccount;							// Cached pointer to the account doing the purchase
	EARRAY_OF(const ProductContainer) eaProducts;	// Cached array of products being purchased
	EARRAY_OF(TransactionLogKeyValueChange) eaKeyValueChanges; // Cached array of key-values being changed; can be reconstructed as long as the locks are intact
	BillingTransaction *pTrans;						// Billing transaction for the current purchase
	PurchaseInformation *pPurchaseInformation;		// Purchase information (minimal required to continue)
	CallbackInfo *pCallbackInfo;					// Callback information for after completion
} PurchaseSession;


/************************************************************************/
/* Private helper functions                                             */
/************************************************************************/

AUTO_TRANSACTION
ATR_LOCKS(pAccount, ".Earecentpurchaseamounts");
enumTransactionOutcome trRecordPurchaseAmount(ATR_ARGS, NOCONST(AccountInfo) *pAccount, SA_PARAM_NN_STR const char *pCurrency, float fAmount)
{
	NOCONST(RecentPurchaseAmount) *pAmount = StructCreateNoConst(parse_RecentPurchaseAmount);
	U32 uNow = timeSecondsSince2000();
	U32 uSpendingCapPeriod = billingGetSpendingCapPeriod();

	pAmount->fAmount = fAmount;
	pAmount->pCurrency = strdup(pCurrency);
	pAmount->uTime = uNow;

	// Remove old entries
	EARRAY_FOREACH_REVERSE_BEGIN(pAccount->eaRecentPurchaseAmounts, iCurAmount);
	{
		if (uNow - pAccount->eaRecentPurchaseAmounts[iCurAmount]->uTime > uSpendingCapPeriod)
		{
			StructDestroyNoConst(parse_RecentPurchaseAmount, eaRemoveFast(&pAccount->eaRecentPurchaseAmounts, iCurAmount));
		}
	}
	EARRAY_FOREACH_END;

	eaPush(&pAccount->eaRecentPurchaseAmounts, pAmount);

	return TRANSACTION_OUTCOME_SUCCESS;
}

// Not exact
static float getRecentPurchaseTotal(SA_PARAM_NN_VALID AccountInfo *pAccount, SA_PARAM_NN_STR const char *pCurrency)
{
	U32 uNow = 0;
	U32 uSpendingCapPeriod = 0;
	float fTotal = 0;

	PERFINFO_AUTO_START_FUNC();

	uNow = timeSecondsSince2000();
	uSpendingCapPeriod = billingGetSpendingCapPeriod();

	EARRAY_CONST_FOREACH_BEGIN(pAccount->eaRecentPurchaseAmounts, iCurAmount, iNumAmounts);
	{
		RecentPurchaseAmount *pAmount = pAccount->eaRecentPurchaseAmounts[iCurAmount];

		if (devassert(pAmount) && uNow - pAmount->uTime < uSpendingCapPeriod && !stricmp_safe(pAmount->pCurrency, pCurrency))
		{
			fTotal += pAmount->fAmount;
		}
	}
	EARRAY_FOREACH_END;

	PERFINFO_AUTO_STOP_FUNC();

	return fTotal;
}

static float getAccountSpendingCap(SA_PARAM_NN_VALID AccountInfo *pAccount, SA_PARAM_NN_STR const char *pCurrency)
{
	float fCap = -1;

	PERFINFO_AUTO_START_FUNC();

	EARRAY_CONST_FOREACH_BEGIN(pAccount->eaSpendingCaps, iCurCap, iNumCaps);
	{
		AccountSpendingCap *pCap = pAccount->eaSpendingCaps[iCurCap];

		if (devassert(pCap) && !stricmp_safe(pCap->pCurrency, pCurrency))
		{
			fCap = pCap->fAmount;
		}
	}
	EARRAY_FOREACH_END;

	if (fCap == -1)
	{
		fCap = billingGetSpendingCap(pCurrency);
	}

	PERFINFO_AUTO_STOP_FUNC();

	return fCap;
}

static bool exceedsSpendingCap(SA_PARAM_NN_VALID AccountInfo *pAccount, SA_PARAM_NN_STR const char *pCurrency, float fAmount)
{
	float fRecentTotal = 0.0f;
	float fCap = 0.0f;
	
	PERFINFO_AUTO_START_FUNC();
	fRecentTotal = getRecentPurchaseTotal(pAccount, pCurrency) + fAmount;
	fCap = getAccountSpendingCap(pAccount, pCurrency);

	if (fCap != -1)
	{
		PERFINFO_AUTO_STOP();
		return fRecentTotal > fCap;
	}

	PERFINFO_AUTO_STOP();
	return false;
}

AUTO_TRANSACTION
ATR_LOCKS(pAccount, ".Unextpurchaseid, .Pppurchaseinformation");
enumTransactionOutcome trPurchaseDelaySession(ATR_ARGS, NOCONST(AccountInfo) *pAccount, NON_CONTAINER const PurchaseInformationContainer *pPurchaseInformation)
{
	NOCONST(PurchaseInformationContainer) *pInfo = StructCloneDeConst(parse_PurchaseInformationContainer, pPurchaseInformation);

	devassert(pInfo);

	pInfo->uPurchaseID = pAccount->uNextPurchaseID;

	if (!pAccount->ppPurchaseInformation)
	{
		eaIndexedEnableNoConst(&pAccount->ppPurchaseInformation, parse_PurchaseInformationContainer);
	}

	eaIndexedAdd(&pAccount->ppPurchaseInformation, pInfo);

	pAccount->uNextPurchaseID++;

	while (!pAccount->uNextPurchaseID || eaIndexedFindUsingInt(&pAccount->ppPurchaseInformation, pAccount->uNextPurchaseID) >= 0)
	{
		pAccount->uNextPurchaseID++;
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pAccount, ".Pppurchaseinformation");
enumTransactionOutcome trDestroyDelayedSessionIfAny(ATR_ARGS, NOCONST(AccountInfo) *pAccount, U32 uPurchaseID)
{
	int index = eaIndexedFindUsingInt(&pAccount->ppPurchaseInformation, uPurchaseID);

	if (index < 0) return TRANSACTION_OUTCOME_SUCCESS;

	StructDestroyNoConst(parse_PurchaseInformationContainer, eaRemove(&pAccount->ppPurchaseInformation, index));

	return TRANSACTION_OUTCOME_SUCCESS;
}

static void PurchaseDestroyDelayedSessionIfAny(SA_PARAM_NN_VALID const PurchaseSession *pPurchaseSession)
{
	if (pPurchaseSession && pPurchaseSession->pAccount && pPurchaseSession->pPurchaseInformation)
	{
		 AutoTrans_trDestroyDelayedSessionIfAny(NULL, objServerType(), GLOBALTYPE_ACCOUNT, pPurchaseSession->pAccount->uID, pPurchaseSession->pPurchaseInformation->uPurchaseID);
	}
}

// Create callback information structure
SA_RET_NN_VALID static CallbackInfo *createCallbackInfo(SA_PARAM_OP_VALID PurchaseCallback pCallback, SA_PARAM_OP_VALID void *pUserData)
{
	CallbackInfo *pCallbackInfo = NULL;
	
	PERFINFO_AUTO_START_FUNC();
	pCallbackInfo = callocStruct(CallbackInfo);
	pCallbackInfo->pCallback = pCallback;
	pCallbackInfo->pUserData = pUserData;
	PERFINFO_AUTO_STOP();

	return pCallbackInfo;
}

// Free a callback information structure
static void freeCallbackInfo(SA_PARAM_NN_VALID CallbackInfo *pCallbackInfo)
{
	PERFINFO_AUTO_START_FUNC();
	free(pCallbackInfo);
	PERFINFO_AUTO_STOP();
}

// Create the purchase session
static PurchaseSession *createPurchaseSession(SA_PARAM_NN_VALID AccountInfo *pAccount)
{
	PurchaseSession *pSession = NULL;
	
	PERFINFO_AUTO_START_FUNC();
	pSession = callocStruct(PurchaseSession);
	if (devassert(pSession))
	{
		pSession->pPurchaseInformation = StructCreateNoConst(parse_PurchaseInformation);
		pSession->pCallbackInfo = callocStruct(CallbackInfo);
		pSession->pAccount = pAccount;
	}
	PERFINFO_AUTO_STOP();

	return pSession;
}

// Determine if a purchase result should result in a failure
static bool purchaseResultIsFailure(PurchaseResult eResult)
{
	if (eResult == PURCHASE_RESULT_PENDING) return false;
	if (eResult == PURCHASE_RESULT_COMMIT) return false;
	return true;
}

// Get the key used as currency if it is not a real currency
SA_RET_NN_VALID __forceinline static const char *getPointCurrency(SA_PARAM_NN_STR const char *pCurrency)
{
	assert(*pCurrency == POINT_CURRENCY_MARKER);
	return pCurrency + 1;
}

// Free a purchase session structure
static void freePurchaseSession(SA_PARAM_NN_VALID PurchaseSession *pPurchaseSession, PurchaseResult eResult)
{
	bool commit = (eResult == PURCHASE_RESULT_COMMIT);

	PERFINFO_AUTO_START_FUNC();
	PurchaseDestroyDelayedSessionIfAny(pPurchaseSession);

	if (commit)
	{
		ADD_MISC_COUNT(1, "Commit");
	}
	else
	{
		ADD_MISC_COUNT(1, "Rollback");
	}

	if (pPurchaseSession->eaProducts)
	{
		int curProduct;
		float fSpendingCapTotal = 0.0f;
		U32 uTransLogID = 0;

		devassert(pPurchaseSession->pAccount);
		devassert(pPurchaseSession->pPurchaseInformation);
		devassert(pPurchaseSession->pPurchaseInformation->eaActivations);
		devassert(eaSize(&pPurchaseSession->pPurchaseInformation->eaActivations) == eaSize(&pPurchaseSession->eaProducts));

		if (commit)
		{
			uTransLogID = AccountTransactionOpen(pPurchaseSession->pAccount->uID,
				pPurchaseSession->pPurchaseInformation->uFlags & PURCHASEINFO_POINTS ? TransLogType_MicroPurchase : TransLogType_CashPurchase,
				pPurchaseSession->pPurchaseInformation->pProxy,
				pPurchaseSession->pPurchaseInformation->eProvider,
				pPurchaseSession->pPurchaseInformation->pMerchantTransactionId,
				pPurchaseSession->pPurchaseInformation->pOrderID);
			AccountTransactionRecordKeyValueChanges(pPurchaseSession->pAccount->uID, uTransLogID, pPurchaseSession->eaKeyValueChanges, NULL);
		}

		for (curProduct = 0; curProduct < eaSize(&pPurchaseSession->eaProducts); curProduct++)
		{
			if (commit)
			{
				Money price;
				ActivationInfo pActivationInfo;

				const ProductContainer *pProduct = pPurchaseSession->eaProducts[curProduct];
				const char *pAmount = pPurchaseSession->pPurchaseInformation->eaProductPrices[curProduct];

				PERFINFO_AUTO_START("Commit Product", 1);
				StructInit(parse_ActivationInfo, &pActivationInfo);

				accountLog(pPurchaseSession->pAccount, "Product purchased: [product:%s] (currency: %s, price: %s)",
					pProduct->pName, pPurchaseSession->pPurchaseInformation->pCurrency, pAmount);

				if (!pProduct->bNoSpendingCap)
				{
					fSpendingCapTotal += atof(pAmount);
				}

				accountActivateProductCommit(pPurchaseSession->pAccount, pProduct,
					NULL, pPurchaseSession->pPurchaseInformation->uActivationSuppressions,
					pPurchaseSession->pPurchaseInformation->eaActivations[curProduct], &pActivationInfo, false);

				moneyInitFromStr(&price, pAmount, pPurchaseSession->pPurchaseInformation->pCurrency);
				accountReportPurchase(pPurchaseSession->pAccount, pProduct, &price);
				AccountTransactionRecordPurchase(pPurchaseSession->pAccount->uID, uTransLogID, pProduct->uID, &price);
				moneyDeinit(&price);

				if (pActivationInfo.pDistributedKey)
				{
					ProductKey productKey;
					bool success = findProductKey(&productKey, pActivationInfo.pDistributedKey);

					if (devassert(success))
					{
						ProductKeyInfo *pProductKeyInfo = makeProductKeyInfo(&productKey);

						if (devassert(pProductKeyInfo))
							eaPush(&pPurchaseSession->pTrans->distributedKeys, pProductKeyInfo);
					}
				}

				StructDeInit(parse_ActivationInfo, &pActivationInfo);
				PERFINFO_AUTO_STOP();
			}
			else
			{
				PERFINFO_AUTO_START("Rollback Product", 1);
				accountActivateProductRollback(pPurchaseSession->pAccount,
					pPurchaseSession->eaProducts[curProduct], pPurchaseSession->pPurchaseInformation->eaActivations[curProduct], false);
				PERFINFO_AUTO_STOP();
			}
		}

		if (commit)
		{
			AccountTransactionFinish(pPurchaseSession->pAccount->uID, uTransLogID);
		}

		// To apply the spending cap, we must meet four criteria:
		//	1) The purchase was successful
		//	2) The purchase was for more than zero
		//	3) The purchase was not made with points
		//	4) The purchase was provided by the Account Server
		if (commit &&
			fSpendingCapTotal &&	
			!(pPurchaseSession->pPurchaseInformation->uFlags & PURCHASEINFO_POINTS) &&
			pPurchaseSession->pPurchaseInformation->eProvider == TPROVIDER_AccountServer)
		{
			AutoTrans_trRecordPurchaseAmount(NULL, objServerType(), GLOBALTYPE_ACCOUNT,
				pPurchaseSession->pAccount->uID, pPurchaseSession->pPurchaseInformation->pCurrency, fSpendingCapTotal);
		}

		// Set the transaction's merchant transaction ID
		if (commit && pPurchaseSession->pTrans && pPurchaseSession->pPurchaseInformation->pMerchantTransactionId && *pPurchaseSession->pPurchaseInformation->pMerchantTransactionId)
		{
			pPurchaseSession->pTrans->merchantTransactionID = strdup(pPurchaseSession->pPurchaseInformation->pMerchantTransactionId);
		}

		eaDestroy(&pPurchaseSession->pPurchaseInformation->eaActivations); // Contents freed by commit or rollback
		eaDestroy(&pPurchaseSession->eaProducts); // Do not free contents
	}

	if (pPurchaseSession->pTrans && pPurchaseSession->pTrans->result != BTR_FAILURE && purchaseResultIsFailure(eResult))
	{
		btFail(pPurchaseSession->pTrans, "%s", purchaseResultMessage(eResult));
	}

	if (devassert(pPurchaseSession->pAccount))
	{
		accountLog(pPurchaseSession->pAccount, "Purchase result: %s", purchaseResultMessage(eResult));
	}

	StructDestroyNoConst(parse_PurchaseInformation, pPurchaseSession->pPurchaseInformation);
	AccountTransactionFreeKeyValueChanges(&pPurchaseSession->eaKeyValueChanges);
	freeCallbackInfo(pPurchaseSession->pCallbackInfo);
	free(pPurchaseSession);
	PERFINFO_AUTO_STOP();
}

// Does some sanity checking on the purchase session structure to make sure it is complete
__forceinline static void sanityCheckPurchaseSession(SA_PARAM_NN_VALID PurchaseSession *pPurchaseSession)
{
	assert(pPurchaseSession);
	assert(pPurchaseSession->pAccount);
	assert(pPurchaseSession->pPurchaseInformation);
	assert(pPurchaseSession->pPurchaseInformation->pCurrency);
	assert(pPurchaseSession->eaProducts);
	assert(pPurchaseSession->pPurchaseInformation->eaActivations);
}

// Determine if a currency is probably valid or not
static bool isValidCurrency(SA_PARAM_NN_STR const char *pCurrency)
{
	const char *curChar;
	int len = 0;

	if (!verify(pCurrency && *pCurrency)) return false;

	// If it is a point currency, we can't tell
	if (*pCurrency == POINT_CURRENCY_MARKER) return true;

	// Should contain only a 3-letter code
	for (curChar = pCurrency; *curChar; curChar++)
	{
		if (!VALID_CURRENCY_CHAR(*curChar)) return false;
		len++;
	}
	if (len != VALID_CURRENCY_LENGTH) return false;

	return true;
}

// Callback called after doing a Steam transaction auth
static bool PurchaseProductSteamLock_Callback(PurchaseResult eResult, PurchaseSession *pPurchaseSession, const char *pRedirectURL, U64 uOrderID, U64 uTransactionID)
{
	BillingTransaction *pTrans = pPurchaseSession->pTrans;

	PERFINFO_AUTO_START_FUNC();
	pPurchaseSession->pPurchaseInformation->pOrderID = uOrderID ? strdupf("%"FORM_LL"u", uOrderID) : NULL;
	pPurchaseSession->pPurchaseInformation->pMerchantTransactionId = uTransactionID ? strdupf("%"FORM_LL"u", uTransactionID) : NULL;

	if (pRedirectURL)
	{
		estrCopy2(&pTrans->redirectURL, pRedirectURL);
	}

	DO_CALLBACK_NO_RETURN(eResult);
	return false;
}

// Callback called after doing a real transaction
static void PurchaseProductLock_Callback(SA_PARAM_OP_VALID BillingTransaction *pTrans, SA_PARAM_NN_VALID PurchaseSession *pPurchaseSession)
{
	bool bSuccess = pTrans ? true : false;

	PERFINFO_AUTO_START_FUNC();
	pTrans = pPurchaseSession->pTrans;

	if (bSuccess)
	{
		if (pTrans->merchantTransactionID)
			pPurchaseSession->pPurchaseInformation->pMerchantTransactionId = strdup(pTrans->merchantTransactionID);

		DO_CALLBACK_AND_RETURN_VOID(PURCHASE_RESULT_PENDING);
	}

	DO_CALLBACK_AND_RETURN_VOID(PURCHASE_RESULT_AUTH_FAIL);
}

// Callback called after finishing a real transaction
static void PurchaseProductFinalize_Callback(bool success, U32 uFraudFlags, SA_PARAM_NN_VALID PurchaseSession *pPurchaseSession,
											 SA_PARAM_OP_STR const char *merchantTransactionId)
{
	BillingTransaction *pTrans = pPurchaseSession->pTrans;

	PERFINFO_AUTO_START_FUNC();
	pPurchaseSession->pPurchaseInformation->pMerchantTransactionId = strdup(merchantTransactionId ? merchantTransactionId : "");

	if (pPurchaseSession->pPurchaseInformation->uFlags & PURCHASEINFO_COMPLETE_COMMIT)
	{
		if (success)
		{
			DO_CALLBACK_AND_RETURN_VOID(PURCHASE_RESULT_COMMIT);
		}
		else
		{
			DO_CALLBACK_AND_RETURN_VOID(PURCHASE_RESULT_COMMIT_FAIL);
		}
	}
	else
	{
		if (success)
		{
			DO_CALLBACK_AND_RETURN_VOID(PURCHASE_RESULT_ROLLBACK);
		}
		else
		{
			DO_CALLBACK_AND_RETURN_VOID(PURCHASE_RESULT_ROLLBACK_FAIL);
		}
	}
}

// Callback called after the second stage of the PurchaseProduct function
static void PurchaseProduct_Callback2(PurchaseResult eResult,
									 SA_PARAM_OP_VALID BillingTransaction *pTrans,
									 PurchaseSession *pPurchaseSession,
									 SA_PARAM_NN_VALID CallbackInfo *pCallbackInfo)
{
	PERFINFO_AUTO_START_FUNC();
	PERFINFO_AUTO_START("Callback", 1);
	if (pCallbackInfo->pCallback) pCallbackInfo->pCallback(eResult, pTrans, pPurchaseSession, pCallbackInfo->pUserData);
	PERFINFO_AUTO_STOP();
	freeCallbackInfo(pCallbackInfo);
	PERFINFO_AUTO_STOP();
}

// Callback called after the first stage of the PurchaseProduct function
static void PurchaseProduct_Callback(PurchaseResult eResult,
									 SA_PARAM_OP_VALID BillingTransaction *pTrans,
									 PurchaseSession *pPurchaseSession,
									 SA_PARAM_NN_VALID CallbackInfo *pCallbackInfo)
{
	PERFINFO_AUTO_START_FUNC();
	if (eResult != PURCHASE_RESULT_PENDING)
	{
		PERFINFO_AUTO_START("Failure", 1);
		if (pCallbackInfo->pCallback) pCallbackInfo->pCallback(eResult, pTrans, pPurchaseSession, pCallbackInfo->pUserData);
		PERFINFO_AUTO_STOP();
		freeCallbackInfo(pCallbackInfo);
		PERFINFO_AUTO_STOP();
		return;
	}

	// If it is a PayPal transaction...
	if (pTrans && pTrans->payPalStatus)
	{
		U32 uPurchaseID = PurchaseDelaySession(pPurchaseSession);
		bool bCompleteTrans = !!(pPurchaseSession->pPurchaseInformation->uFlags & PURCHASEINFO_COMPLETE_TRANS);

		if (!uPurchaseID)
		{
			btFail(pTrans, "Could not delay transaction.");
			pTrans = NULL;

			eResult = PURCHASE_RESULT_DELAY_FAILURE;

			PERFINFO_AUTO_START("Paypal: Delay Failure", 1);
			if (pCallbackInfo->pCallback) pCallbackInfo->pCallback(eResult, pTrans, pPurchaseSession, pCallbackInfo->pUserData);
			PERFINFO_AUTO_STOP();
			freeCallbackInfo(pCallbackInfo);
			PERFINFO_AUTO_STOP();
			return;
		}

		eResult = PURCHASE_RESULT_PENDING_PAYPAL;

		pTrans->uPendingActionID = accountAddPendingFinishDelayedTrans(pPurchaseSession->pAccount, uPurchaseID);

		PERFINFO_AUTO_START("Paypal: Pending", 1);
		if (pCallbackInfo->pCallback) pCallbackInfo->pCallback(eResult, pTrans, pPurchaseSession, pCallbackInfo->pUserData);
		PERFINFO_AUTO_STOP();
		freeCallbackInfo(pCallbackInfo);
		PERFINFO_AUTO_STOP();
		return;
	}

	PurchaseProductFinalize(pPurchaseSession, true, PurchaseProduct_Callback2, pCallbackInfo);
}

void PurchaseRetrieveDelayedSession_Callback(SA_PARAM_NN_VALID BillingTransaction *pTrans, bool bKnownAccount,
										     SA_PARAM_OP_VALID AccountTransactionInfo *pAccountTrans, SA_PARAM_NN_VALID PurchaseSession *pSession)
{
	bool bSuccess = true;

	PERFINFO_AUTO_START_FUNC();
	pSession->pTrans->userData = pAccountTrans ? (void*)pAccountTrans->vindiciaTransaction : NULL;

	if (!pTrans) bSuccess = false;

	if (pTrans->result == BTR_FAILURE) bSuccess = false;

	if (!pAccountTrans) bSuccess = false;

	if (!bSuccess && pTrans && pTrans->result != BTR_FAILURE)
	{
		btFail(pTrans, "Could not retrieve the delayed transaction from Vindicia.");
	}

	PERFINFO_AUTO_START("Callback", 1);
	pSession->pCallbackInfo->pCallback(bSuccess ? PURCHASE_RESULT_RETRIEVED : PURCHASE_RESULT_RETRIEVE_FAILED,
		pTrans, pSession, pSession->pCallbackInfo->pUserData);
	PERFINFO_AUTO_STOP();
	PERFINFO_AUTO_STOP();
}

/************************************************************************/
/* Public functions                                                     */
/************************************************************************/

void PopulatePurchaseDetails(PurchaseDetails *pDetails,
							 const char *pProxy,
							 const char *pCluster,
							 const char *pEnvironment,
							 const char *pIP,
							 PaymentMethod *pPaymentMethod,
							 BillingTransaction *pTrans,
							 const char *pBankName)
{
	PERFINFO_AUTO_START_FUNC();
	StructReset(parse_PurchaseDetails, pDetails);
	pDetails->eProvider = TPROVIDER_AccountServer;
	pDetails->pProxy = StructAllocString(pProxy);
	pDetails->pCluster = StructAllocString(pCluster);
	pDetails->pEnvironment = StructAllocString(pEnvironment);
	pDetails->pIP = StructAllocString(pIP);
	pDetails->pPaymentMethod = pPaymentMethod;
	pDetails->pTrans = pTrans;
	pDetails->pBankName = StructAllocString(pBankName);
	PERFINFO_AUTO_STOP();
}

void PopulatePurchaseDetailsSteam(PurchaseDetails *pDetails,
								  const char *pProxy,
								  const char *pCluster,
								  const char *pEnvironment,
								  const char *pIP,
								  const char *pOrderID,
								  const char *pTransactionID,
								  U64 uSteamID,
								  const char *pLocCode,
								  bool bWebPurchase)
{
	PERFINFO_AUTO_START_FUNC();
	StructReset(parse_PurchaseDetails, pDetails);
	pDetails->eProvider = TPROVIDER_Steam;
	pDetails->pProxy = StructAllocString(pProxy);
	pDetails->pCluster = StructAllocString(pCluster);
	pDetails->pEnvironment = StructAllocString(pEnvironment);
	pDetails->pIP = StructAllocString(pIP);
	pDetails->pOrderID = StructAllocString(pOrderID);
	pDetails->pTransactionID = StructAllocString(pTransactionID);
	pDetails->uSteamID = uSteamID;
	pDetails->pLocCode = pLocCode ? StructAllocString(pLocCode) : StructAllocString("EN");
	pDetails->bWebPurchase = bWebPurchase;
	PERFINFO_AUTO_STOP();
}

// Initiate a purchase
PurchaseResult PurchaseProductLock(SA_PARAM_NN_VALID AccountInfo *pAccount,
								   EARRAY_OF(const TransactionItem) eaItems,
								   SA_PARAM_NN_STR const char *pCurrency,
								   unsigned int uActivationSuppressions,
								   bool bVerifyPrice,
								   SA_PARAM_NN_VALID PurchaseDetails *pDetails,
								   SA_PARAM_NN_VALID PurchaseCallback pCallback,
								   SA_PARAM_OP_VALID void *pUserData)
{
	PurchaseSession *pPurchaseSession = NULL;
	BillingTransaction *pTrans = pDetails->pTrans;
	Money total;
	float fSpendingCapTotal = 0;

	PERFINFO_AUTO_START_FUNC();

	// Initialize the total to 0
	moneyInitFromStr(&total, "0", pCurrency);

	// Copy fields over
	PERFINFO_AUTO_START("Init Purchase Session", 1);
	pPurchaseSession = createPurchaseSession(pAccount);
	pPurchaseSession->pCallbackInfo->pCallback = pCallback;
	pPurchaseSession->pCallbackInfo->pUserData = pUserData;
	pPurchaseSession->pTrans = pTrans;

	pPurchaseSession->pPurchaseInformation->pCurrency = StructAllocString(pCurrency);
	pPurchaseSession->pPurchaseInformation->uActivationSuppressions = uActivationSuppressions;
	pPurchaseSession->pPurchaseInformation->pProxy = StructAllocString(pDetails->pProxy);
	pPurchaseSession->pPurchaseInformation->pOrderID = StructAllocString(pDetails->pOrderID);
	pPurchaseSession->pPurchaseInformation->pMerchantTransactionId = StructAllocString(pDetails->pTransactionID);
	pPurchaseSession->pPurchaseInformation->eProvider = pDetails->eProvider;

	// Check input
	CHECK(pAccount, PURCHASE_RESULT_MISSING_ACCOUNT);
	CHECK(isValidCurrency(pCurrency), PURCHASE_RESULT_INVALID_CURRENCY);
	CHECK(eaItems, PURCHASE_RESULT_MISSING_PRODUCTS);

	// Lock products
	EARRAY_CONST_FOREACH_BEGIN(eaItems, iCurItem, iNumItems);
	{
		const TransactionItem *pItem = eaItems[iCurItem];
		const ProductContainer *pProduct = findProductByID(pItem->uProductID);
		AccountProxyProductActivation *pActivation = NULL;
		const Money *pPrice = NULL;
		char *pAmount = NULL;
		U32 uDiscountPercentage = 0;

		PERFINFO_AUTO_STOP_START("Lock Each Product", 1);

		CHECK(pProduct, PURCHASE_RESULT_INVALID_PRODUCT);
		CHECK(accountActivateProductLock(pAccount, pProduct, pDetails->pProxy, pDetails->pCluster, pDetails->pEnvironment, NULL, &pActivation) == APR_Success, PURCHASE_RESULT_CANNOT_ACTIVATE);

		eaPush(&pPurchaseSession->eaProducts, pProduct);
		eaPush(&pPurchaseSession->pPurchaseInformation->eaActivations, pActivation);
		ea32Push(&pPurchaseSession->pPurchaseInformation->eaProductIDs, pItem->uProductID);

		pPrice = getProductPrice(pProduct, pCurrency);
		CHECK(pPrice, PURCHASE_RESULT_INVALID_PRICE);

		if (!isRealCurrency(pCurrency))
		{
			if (pItem->uOverridePercentageDiscount)
			{
				uDiscountPercentage = pItem->uOverridePercentageDiscount;
			}
			else
			{
				AccountProxyKeyValueInfoList *pKVList = GetProxyKeyValueList(pAccount);
				uDiscountPercentage = getProductDiscountPercentage(pAccount, pItem->uProductID, pCurrency, pKVList, pDetails->pProxy, pDetails->pCluster, pDetails->pEnvironment);
				StructDestroy(parse_AccountProxyKeyValueInfoList, pKVList);
			}
		}

		// If this is a real currency, or there's no discount, then uDiscountPercentage == 0
		if (uDiscountPercentage)
		{
			Money realPrice;
			U32 realPoints = moneyCountPoints(pPrice);

			if (pItem->uOverridePercentageDiscount)
			{
				// In the case where the shard specified an override discount, the price it sent is what it thinks the base price of the product is
				// This is so that the AS can verify that the shard's product cache is still up to date (discount cache doesn't matter since we're overriding)
				// So we have to first check that the specified price matches the PRE-DISCOUNT price on the AS
				if (pItem->pPrice && !moneyInvalid(pItem->pPrice) && bVerifyPrice)
					CHECK(realPoints == moneyCountPoints(pItem->pPrice), PURCHASE_RESULT_PRICE_CHANGED);
				APPLY_DISCOUNT_EFFECT(realPoints, uDiscountPercentage);
			}
			else
			{
				// In the case where the shard did not specify an override discount, the price it sent is what it thinks the final price is
				// This is so that the AS can verify that the shard's product and discount caches are BOTH up to date
				// So we have to first apply the discount, then check that the specified price matches the POST-DISCOUNT price on the AS
				APPLY_DISCOUNT_EFFECT(realPoints, uDiscountPercentage);
				if (pItem->pPrice && !moneyInvalid(pItem->pPrice) && bVerifyPrice)
					CHECK(realPoints == moneyCountPoints(pItem->pPrice), PURCHASE_RESULT_PRICE_CHANGED);
			}

			moneyInitFromInt(&realPrice, realPoints, moneyCurrency(pPrice));
			estrFromMoneyRaw(&pAmount, &realPrice);
			moneyAdd(&total, &realPrice);
			moneyDeinit(&realPrice);
		}
		else
		{
			if (!isRealCurrency(pCurrency) && pItem->pPrice && !moneyInvalid(pItem->pPrice) && bVerifyPrice)
				CHECK(moneyCountPoints(pPrice) == moneyCountPoints(pItem->pPrice), PURCHASE_RESULT_PRICE_CHANGED);

			estrFromMoneyRaw(&pAmount, pPrice);
			moneyAdd(&total, pPrice);
		}

		eaPush(&pPurchaseSession->pPurchaseInformation->eaProductPrices, pAmount);

		if (!pProduct->bNoSpendingCap)
		{
			fSpendingCapTotal += atof(pAmount);
		}
	}
	EARRAY_FOREACH_END;

	// If it's a points currency, auth the point change locally
	if (!isRealCurrency(pCurrency))
	{
		AccountKeyValueResult eKeyValueResult;

		PERFINFO_AUTO_STOP_START("Payment: Points", 1);
		pPurchaseSession->pPurchaseInformation->uFlags |= PURCHASEINFO_POINTS;

		// Lock the amount
		eKeyValueResult = AccountKeyValue_Change(pAccount, getPointCurrency(pCurrency), -moneyCountPoints(&total), &pPurchaseSession->pPurchaseInformation->pPassword);

		if (gbEnableNewPurchaseErrors)
		{
			switch (eKeyValueResult)
			{
				xcase AKV_INVALID_RANGE: PERFINFO_AUTO_STOP(); DO_CALLBACK_AND_RETURN(PURCHASE_RESULT_INSUFFICIENT_POINTS);
				xcase AKV_LOCKED: PERFINFO_AUTO_STOP(); DO_CALLBACK_AND_RETURN(PURCHASE_RESULT_CURRENCY_LOCKED);
				xcase AKV_SUCCESS: break;
				xdefault: PERFINFO_AUTO_STOP(); DO_CALLBACK_AND_RETURN(PURCHASE_RESULT_UNKNOWN_ERROR);
			}
		}
		else
		{
			CHECK(eKeyValueResult == AKV_SUCCESS, PURCHASE_RESULT_INSUFFICIENT_POINTS);
		}
	}
	// If it's a real currency provided by the AS, do billing auth
	else if (pDetails->eProvider == TPROVIDER_AccountServer)
	{
		PERFINFO_AUTO_STOP_START("Payment: Vindicia", 1);
		if (fSpendingCapTotal)
		{
			CHECK(!exceedsSpendingCap(pAccount, pCurrency, fSpendingCapTotal), PURCHASE_RESULT_SPENDING_CAP_REACHED);
		}

		// Check the payment method (which is now required)
		CHECK(pDetails->pPaymentMethod, PURCHASE_RESULT_INVALID_PAYMENT_METHOD);
		if (pDetails->pPaymentMethod->VID && *pDetails->pPaymentMethod->VID)
			CHECK(isValidPaymentMethodVID(pAccount, pDetails->pPaymentMethod->VID), PURCHASE_RESULT_INVALID_PAYMENT_METHOD);

		// IP is required for Vindicia fraud scoring
		CHECK(pDetails->pIP, PURCHASE_RESULT_INVALID_IP);
		CHECK(isIp(pDetails->pIP), PURCHASE_RESULT_INVALID_IP);

		// Create the transaction if one wasn't provided
		if (!pPurchaseSession->pTrans) 
		{
			pPurchaseSession->pTrans = btCreateBlank(true);
			pPurchaseSession->pPurchaseInformation->uFlags |= PURCHASEINFO_COMPLETE_TRANS;
		}
		pTrans = pPurchaseSession->pTrans;

		// Initiate the purchase
		btTransactionAuth(pAccount,
			eaItems,
			pPurchaseSession->pPurchaseInformation->pCurrency,
			pDetails->pPaymentMethod,
			pDetails->pIP,
			pDetails->pBankName,
			pTrans,
			false,
			PurchaseProductLock_Callback, pPurchaseSession);
		
		PERFINFO_AUTO_STOP();
		PERFINFO_AUTO_STOP();
		return PURCHASE_RESULT_PENDING; // Don't call the callback yet
	}
	// If it's a real currency provided by Steam, call their init API
	else if (pDetails->eProvider == TPROVIDER_Steam)
	{
		// Don't call the callback yet
		PurchaseResult eResult;
		PERFINFO_AUTO_STOP_START("Payment: SteamWallet", 1);
		eResult = InitSteamTransaction(pAccount->uID, pDetails->uSteamID, pDetails->pProxy, NULL, pCurrency,
			pDetails->pLocCode, eaItems, pDetails->pIP, pDetails->bWebPurchase, PurchaseProductSteamLock_Callback, pPurchaseSession);
		PERFINFO_AUTO_STOP();
		PERFINFO_AUTO_STOP();
		return eResult;
	}

	// The purchase should now be pending
	PERFINFO_AUTO_STOP();
	DO_CALLBACK_AND_RETURN(PURCHASE_RESULT_PENDING);
}

static bool PurchaseHandleDelayedSteamSession(PurchaseResult eResult, SA_PARAM_NN_VALID PurchaseSession *pPurchaseSession, U64 uOrderID, U64 uTransactionID)
{
	static bool bAlerted = false;
	BillingTransaction *pTrans = pPurchaseSession->pTrans;

	PERFINFO_AUTO_START_FUNC();

	switch(eResult)
	{
	case PURCHASE_RESULT_PENDING:
		PERFINFO_AUTO_START("Refresh Locks", 1);
		EARRAY_FOREACH_BEGIN(pPurchaseSession->pPurchaseInformation->eaActivations, i);
		{
			AccountProxyProductActivation *pActivation = pPurchaseSession->pPurchaseInformation->eaActivations[i];

			if (!devassert(pActivation)) continue;

			EARRAY_FOREACH_BEGIN(pActivation->ppKeyLocks, j);
			{
				AccountProxyKeyLockPair *pPair = pActivation->ppKeyLocks[j];

				if (!devassert(pPair)) continue;

				if (AccountKeyValue_LockAgain(pPurchaseSession->pAccount, pPair->pKey, &pPair->pLock) != AKV_SUCCESS)
				{
					PERFINFO_AUTO_STOP();
					PERFINFO_AUTO_STOP();
					return false;
				}
			}
			EARRAY_FOREACH_END;
		}
		EARRAY_FOREACH_END;

		if (pPurchaseSession->pPurchaseInformation->uFlags & PURCHASEINFO_POINTS)
		{
			if (AccountKeyValue_LockAgain(pPurchaseSession->pAccount, getPointCurrency(pPurchaseSession->pPurchaseInformation->pCurrency),
				&pPurchaseSession->pPurchaseInformation->pPassword) != AKV_SUCCESS)
			{
				PERFINFO_AUTO_STOP();
				PERFINFO_AUTO_STOP();
				return false;
			}
		}

		PERFINFO_AUTO_STOP();
		PERFINFO_AUTO_STOP();
		return true;

	xcase PURCHASE_RESULT_STEAM_DISABLED:
		PERFINFO_AUTO_START("Steam Disabled", 1);
		if (!bAlerted)
		{
			bAlerted = true;
			AssertOrAlert("STEAM_STATE_CHANGED", "Could not resume an outstanding Steam request! "
				"The billing config must have changed while there were still pending transactions, which is a big no-no! "
				"All outstanding transactions will be rolled back.");
		}
		PERFINFO_AUTO_STOP();
		btCompleteSteamTransaction(pTrans, false);
		DO_CALLBACK_NO_RETURN(eResult);
		return false;

	xcase PURCHASE_RESULT_COMMIT:
	case PURCHASE_RESULT_ROLLBACK:
	default:
		bAlerted = false;
		ADD_MISC_COUNT(1, "Completed");
		btCompleteSteamTransaction(pTrans, eResult == PURCHASE_RESULT_COMMIT);
		DO_CALLBACK_NO_RETURN(eResult);
		return false;
	}
}

static void PopulatePurchaseSessionKeyValueChanges(PurchaseSession *pPurchaseSession)
{
	char **eaKeys = NULL;

	EARRAY_CONST_FOREACH_BEGIN(pPurchaseSession->eaProducts, iProduct, iNumProducts);
	{
		const ProductContainer *pProduct = pPurchaseSession->eaProducts[iProduct];

		EARRAY_CONST_FOREACH_BEGIN(pProduct->ppKeyValueChanges, iChange, iNumChanges);
		{
			eaPush(&eaKeys, strdup(pProduct->ppKeyValueChanges[iChange]->pKey));
		}
		EARRAY_FOREACH_END;
	}
	EARRAY_FOREACH_END;

	if (pPurchaseSession->pPurchaseInformation->uFlags & PURCHASEINFO_POINTS)
	{
		const char *pPointCurrency = getPointCurrency(pPurchaseSession->pPurchaseInformation->pCurrency);
		CurrencyPopulateKeyList(pPointCurrency, &eaKeys);
	}

	eaRemoveDuplicateStrings(&eaKeys);
	AccountTransactionGetKeyValueChanges(pPurchaseSession->pAccount, eaKeys, &pPurchaseSession->eaKeyValueChanges);
	eaDestroyEx(&eaKeys, NULL);
}

// Finalize a purchase
PurchaseResult PurchaseProductFinalize(SA_PARAM_NN_VALID PurchaseSession *pPurchaseSession,
									   bool bCapture,
									   SA_PARAM_OP_VALID PurchaseCallback pCallback,
									   SA_PARAM_OP_VALID void *pUserData)
{
	BillingTransaction *pTrans = pPurchaseSession->pTrans;
	float fSpendingCapTotal = 0; // Doesn't need to be precise

	PERFINFO_AUTO_START_FUNC();

	// Copy data
	pPurchaseSession->pCallbackInfo->pCallback = pCallback;
	pPurchaseSession->pCallbackInfo->pUserData = pUserData;
	pPurchaseSession->pPurchaseInformation->uFlags |= bCapture ? PURCHASEINFO_COMPLETE_COMMIT : 0;

	// Sanity checking
	sanityCheckPurchaseSession(pPurchaseSession);

	// Construct the list of key-value changes for the transaction log, before we commit any locks
	PopulatePurchaseSessionKeyValueChanges(pPurchaseSession);

	// If it's a point purchase, commit the point expenditure
	if (pPurchaseSession->pPurchaseInformation->uFlags & PURCHASEINFO_POINTS)
	{
		if (bCapture)
		{
			PERFINFO_AUTO_START("Payment: Capture Points", 1);
			// Commit the point change
			CHECK(AccountKeyValue_Commit(pPurchaseSession->pAccount, getPointCurrency(pPurchaseSession->pPurchaseInformation->pCurrency),
				pPurchaseSession->pPurchaseInformation->pPassword) == AKV_SUCCESS, PURCHASE_RESULT_COMMIT_FAIL);
			PERFINFO_AUTO_STOP();
		}
		else
		{
			PERFINFO_AUTO_START("Payment: Rollback Points", 1);
			// Rollback the point change
			CHECK(AccountKeyValue_Rollback(pPurchaseSession->pAccount, getPointCurrency(pPurchaseSession->pPurchaseInformation->pCurrency),
				pPurchaseSession->pPurchaseInformation->pPassword) == AKV_SUCCESS, PURCHASE_RESULT_ROLLBACK_FAIL);
			PERFINFO_AUTO_STOP();
		}
	}
	// If it's a real money purchase through the AS, finish the transaction
	else if (pPurchaseSession->pPurchaseInformation->eProvider == TPROVIDER_AccountServer)
	{
		if (bCapture)
			PERFINFO_AUTO_START("Payment: Capture Vindicia", 1);
		else
			PERFINFO_AUTO_START("Payment: Rollback Vindicia", 1);

		// Capture the transaction
		btTransactionFinish(pTrans, bCapture, false, PurchaseProductFinalize_Callback, pPurchaseSession);
		PERFINFO_AUTO_STOP();
		PERFINFO_AUTO_STOP();
		return PURCHASE_RESULT_PENDING; // Don't call the callback yet
	}
	// If it's a real money purchase through Steam, go through their API
	else if (pPurchaseSession->pPurchaseInformation->eProvider == TPROVIDER_Steam)
	{
		// But! Only do it if we're capturing
		// If we're rolling back, it's safe to simply let the previously-inited transaction rot
		if (bCapture)
		{
			PERFINFO_AUTO_START("Payment: Capture Steam", 1);
			// Add to Steam queue, don't call the callback until it's done
			btMarkSteamTransaction(pTrans);
			AddOrderToSteamQueue(pPurchaseSession->pAccount->uID, atoui64(pPurchaseSession->pPurchaseInformation->pOrderID),
				pPurchaseSession->pPurchaseInformation->pProxy, PurchaseHandleDelayedSteamSession, pPurchaseSession);
			PERFINFO_AUTO_STOP();
			PERFINFO_AUTO_STOP();
			return PURCHASE_RESULT_PENDING;
		}

		ADD_MISC_COUNT(1, "Payment: Rollback Steam");
	}

	// Commit the products and return
	DO_CALLBACK_AND_RETURN(bCapture ? PURCHASE_RESULT_COMMIT : PURCHASE_RESULT_ROLLBACK);
}

// Performs a lock and finalize
PurchaseResult PurchaseProduct(SA_PARAM_NN_VALID AccountInfo *pAccount,
							   EARRAY_OF(const TransactionItem) eaItems,
							   SA_PARAM_NN_STR const char *pCurrency,
							   unsigned int uActivationSuppressions,
							   bool bVerifyPrice,
							   SA_PARAM_NN_VALID PurchaseDetails *pDetails,
							   SA_PARAM_NN_VALID PurchaseCallback pCallback,
							   SA_PARAM_OP_VALID void *pUserData)
{
	return PurchaseProductLock(pAccount, eaItems, pCurrency, uActivationSuppressions, bVerifyPrice,
		pDetails, PurchaseProduct_Callback, createCallbackInfo(pCallback, pUserData));
}

// Determine if a purchase is for points
bool PurchaseIsPoints(SA_PARAM_NN_VALID const PurchaseSession *pPurchaseSession)
{
	return pPurchaseSession->pPurchaseInformation->uFlags & PURCHASEINFO_POINTS;
}

SA_RET_OP_STR const char *PurchaseOrderID(SA_PARAM_NN_VALID const PurchaseSession *pPurchaseSession)
{
	return pPurchaseSession->pPurchaseInformation->pOrderID;
}

SA_RET_OP_STR const char *PurchaseLockString(SA_PARAM_NN_VALID const PurchaseSession *pPurchaseSession)
{
	return pPurchaseSession->pPurchaseInformation->pPassword;
}

// Mark a purchase as being finished later
U32 PurchaseDelaySession(SA_PARAM_NN_VALID PurchaseSession *pPurchaseSession)
{
	U32 uPurchaseID = 0;
	
	PERFINFO_AUTO_START_FUNC();

	uPurchaseID = pPurchaseSession->pAccount->uNextPurchaseID;

	if (!pPurchaseSession->pPurchaseInformation) return 0;

	AutoTrans_trPurchaseDelaySession(NULL, objServerType(), GLOBALTYPE_ACCOUNT, pPurchaseSession->pAccount->uID, (PurchaseInformationContainer *)pPurchaseSession->pPurchaseInformation);

	if (pPurchaseSession->eaProducts)
	{
		eaDestroy(&pPurchaseSession->eaProducts);
	}

	PERFINFO_AUTO_STOP_FUNC();

	return uPurchaseID;
}

// Create a session from an ID
BillingTransaction *
PurchaseRetrieveDelayedSession(SA_PARAM_NN_VALID const AccountInfo *pAccount,
							   U32 uPurchaseID,
							   SA_PARAM_OP_VALID BillingTransaction *pTrans,
							   SA_PARAM_NN_VALID PurchaseCallback pCallback,
							   SA_PARAM_OP_VALID void *pUserData)
{
	int index;
	const PurchaseInformationContainer *pPurchaseInformation;
	PurchaseSession *pSession;
	int curProduct;

	PERFINFO_AUTO_START_FUNC();

	index = eaIndexedFindUsingInt(&pAccount->ppPurchaseInformation, uPurchaseID);
	if (index < 0) return NULL;

	pPurchaseInformation = pAccount->ppPurchaseInformation[index];

	devassert(pPurchaseInformation->uPurchaseID == uPurchaseID);

	pSession = callocStruct(PurchaseSession);
	pSession->pPurchaseInformation = StructCloneDeConst(parse_PurchaseInformation, pPurchaseInformation);
	pSession->pCallbackInfo = callocStruct(CallbackInfo);

	devassert(pSession->pCallbackInfo);

	pSession->pCallbackInfo->pCallback = pCallback;
	pSession->pCallbackInfo->pUserData = pUserData;

	pSession->pAccount = (AccountInfo *)pAccount;
	pSession->pTrans = pTrans;

	devassert(pSession->pPurchaseInformation);

	if (pTrans)
	{
		pSession->pPurchaseInformation->uFlags &= ~PURCHASEINFO_COMPLETE_TRANS;
	}
	else
	{
		pSession->pPurchaseInformation->uFlags |= PURCHASEINFO_COMPLETE_TRANS;
		pSession->pTrans = btCreateBlank(true);
		pTrans = pSession->pTrans;
	}

	for (curProduct = 0; curProduct < ea32Size(&pPurchaseInformation->eaProductIDs); curProduct++)
	{
		const ProductContainer *pProduct = findProductByID(pPurchaseInformation->eaProductIDs[curProduct]);

		if (pProduct)
			eaPush(&pSession->eaProducts, pProduct);
	}

	if (pSession->pPurchaseInformation->pMerchantTransactionId && pSession->pPurchaseInformation->eProvider == TPROVIDER_AccountServer)
	{
		btFetchTransactionByMTID(pSession->pPurchaseInformation->pMerchantTransactionId,
			pSession->pTrans, PurchaseRetrieveDelayedSession_Callback, pSession);
	}
	else
	{
		PERFINFO_AUTO_START("Callback", 1);
		pSession->pCallbackInfo->pCallback(PURCHASE_RESULT_RETRIEVED, pTrans, pSession, pSession->pCallbackInfo->pUserData);
		PERFINFO_AUTO_STOP();
	}

	PERFINFO_AUTO_STOP_FUNC();

	return pTrans;
}

// Get a fail message for a purchase result
SA_RET_NN_STR const char *purchaseResultMessage(PurchaseResult eResult)
{
	switch (eResult)
	{
		xcase PURCHASE_RESULT_COMMIT:
			return ACCOUNT_HTTP_SUCCESS;

		xcase PURCHASE_RESULT_CANNOT_ACTIVATE:
			return ACCOUNT_HTTP_PRODUCT_COULD_NOT_ACTIVATE;

		xcase PURCHASE_RESULT_MISSING_ACCOUNT:
			return ACCOUNT_HTTP_USER_NOT_FOUND;

		xcase PURCHASE_RESULT_INVALID_PAYMENT_METHOD:
			return ACCOUNT_HTTP_INVALID_PAYMENT_METHOD;

		xcase PURCHASE_RESULT_INVALID_PRODUCT:
			return ACCOUNT_HTTP_PRODUCT_NOT_FOUND;

		xcase PURCHASE_RESULT_INVALID_PRICE:
			return ACCOUNT_HTTP_INVALID_CURRENCY;

		xcase PURCHASE_RESULT_INSUFFICIENT_POINTS:
			return ACCOUNT_HTTP_INSUFFICIENT_POINTS;

		xcase PURCHASE_RESULT_MISSING_PRODUCTS:
			return ACCOUNT_HTTP_PRODUCT_NOT_FOUND;

		xcase PURCHASE_RESULT_INVALID_CURRENCY:
			return ACCOUNT_HTTP_INVALID_CURRENCY;

		xcase PURCHASE_RESULT_INVALID_IP:
			return ACCOUNT_HTTP_INVALID_IP;

		xcase PURCHASE_RESULT_SPENDING_CAP_REACHED:
			return ACCOUNT_HTTP_SPENDING_CAP_REACHED;

		xcase PURCHASE_RESULT_CURRENCY_LOCKED:
			return ACCOUNT_HTTP_CURRENCY_LOCKED;

		xcase PURCHASE_RESULT_UNKNOWN_ERROR:
			return ACCOUNT_HTTP_INTERNAL_ERROR;

		xcase PURCHASE_RESULT_AUTH_FAIL:
		case PURCHASE_RESULT_ROLLBACK:
		case PURCHASE_RESULT_ROLLBACK_FAIL:
		case PURCHASE_RESULT_COMMIT_FAIL:
		default:
			return ACCOUNT_HTTP_AUTH_FAILED;
	}
}

#include "PurchaseProduct_h_ast.c"