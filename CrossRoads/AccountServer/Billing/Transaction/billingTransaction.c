#include "billingTransaction.h"
#include "EString.h"
#include "objContainer.h"
#include "Account/billingAccount.h"
#include "vindicia.h"
#include "Product.h"
#include "AccountLog.h"
#include "AccountManagement.h"
#include "error.h"
#include "billing.h"
#include "billingTransaction_c_ast.h"
#include "UpdatePaymentMethod.h"
#include "Money.h"
#include "AccountReporting.h"
#include "StringUtil.h"

// Flags used in the TransactionInfo
#define TRANSACTIONINFO_COMPLETE BIT(0)

AUTO_STRUCT;
typedef struct TransactionInfo
{
	U32 uAccountID;
	EARRAY_OF(TransactionItem) eaItems;
	char *pCurrency;						AST(ESTRING)
	char *pIP;								AST(ESTRING)
	const PaymentMethod *pPaymentMethod;
	bool bSendWholePM;
	char *pBankName;						AST(ESTRING)
	TransactionAuthCallback callback;		NO_AST
	void *userData;							NO_AST
} TransactionInfo;

typedef struct TransactionFinishInfo
{
	void *callback;
	void *userData;
	bool bCapture;
	const char *pError;
	U32 uFlags;
	U32 uFraudFlags;
} TransactionFinishInfo;

/************************************************************************/
/* Begin a transaction                                                  */
/************************************************************************/

static bool btExpectedAuthResponseCode(int iCode)
{
	if (iCode == 500) return false;
	return true;
}

static void btTransactionAuth_Complete(BillingTransaction *pTrans)
{
	VindiciaXMLtoObjResult *pResult = NULL;
	TransactionInfo *pInfo = pTrans->userData;

	if (!pInfo) return;

	PERFINFO_AUTO_START_FUNC();
	pResult = vindiciaCreateResponse(pTrans, VINDICIA_TYPE(trn, authResponse));

	if(pResult)
	{
		struct trn__authResponse *pResponse = pResult->pObj;
		BILLING_DEBUG_RESPONSE("trn__auth", pResponse);
		btFreeObjResult(pTrans, pResult);

		if (pResponse && pResponse->_transaction)
		{
			if (!pResponse->_transaction->account)
			{
				BILLING_DEBUG_WARNING("Account object is missing from auth response from Vindicia!\n");
			}
			else
			{
				btPopulateVinAccountFromAccount(pTrans, &pResponse->_transaction->account, pInfo->uAccountID);
			}

			if(pResponse->_return_->returnCode == VINDICIA_SUCCESS_CODE)
			{
				if (pResponse->_transaction->statusLog &&
					pResponse->_transaction->statusLog->__ptr)
				{
					btSetVindiciaTransStatus(pTrans, pResponse->_transaction->statusLog->__ptr[0]);
				}

				pTrans->merchantTransactionID = btStrdup(pTrans, pResponse->_transaction->merchantTransactionId);

				pTrans->userData = pResponse->_transaction;
				PERFINFO_AUTO_START("Success Callback", 1);
				(*pInfo->callback)(pTrans, pInfo->userData);
				PERFINFO_AUTO_STOP();
				StructDestroy(parse_TransactionInfo, pInfo);
				PERFINFO_AUTO_STOP();
				return;
			}
			else
			{
				if (!btExpectedAuthResponseCode(pResponse->_return_->returnCode))
				{
					AssertOrAlert("ACCOUNTSERVER_BILLING_INVALID_RESPONSE_CODE",
						"Vindicia returned %d as a response code for Transaction.Auth for SOAP ID %s! Please contact Vindicia.",
						pResponse->_return_->returnCode, pResponse->_return_->soapId);
				}

				btFail(pTrans, "Authorization failed.");
				PERFINFO_AUTO_START("Failure Callback", 1);
				(*pInfo->callback)(NULL, pInfo->userData);
				PERFINFO_AUTO_STOP();
				StructDestroy(parse_TransactionInfo, pInfo);
				pTrans->userData = NULL;
				PERFINFO_AUTO_STOP();
				return;
			}
		}
		else
		{
			AssertOrAlert("ACCOUNTSERVER_BILLING_AUTH_COMPLETE_TRANS_MISSING", "Response or transaction missing from auth complete!");
		}
	}

	PERFINFO_AUTO_START("Failure Callback", 1);
	(*pInfo->callback)(NULL, pInfo->userData);
	PERFINFO_AUTO_STOP();

	StructDestroy(parse_TransactionInfo, pInfo);
	pTrans->userData = NULL;
	PERFINFO_AUTO_STOP();
}

__forceinline static void cleanTransactionInfo(SA_PRE_NN_NN_VALID TransactionInfo **ppInfo, SA_PARAM_NN_VALID BillingTransaction *pTrans)
{
	if (*ppInfo)
	{
		pTrans->userData = NULL;
		if ((*ppInfo)->callback) (*(*ppInfo)->callback)(NULL, (*ppInfo)->userData);
		StructDestroy(parse_TransactionInfo, (*ppInfo));
		*ppInfo = NULL;
	}
}

static void btTransactionAuth_AccountFetchComplete(BillingTransaction *pTrans);

static void btTransactionAuth_Pushed(BillingTransaction *pTrans)
{
	VindiciaXMLtoObjResult *pResult = NULL;
	TransactionInfo *pInfo = pTrans->userData;
	bool bContinuing = false;

	if (!pInfo) return;

	PERFINFO_AUTO_START_FUNC();
	pResult = vindiciaCreateResponse(pTrans, VINDICIA_TYPE(acc, updateResponse));

	if(pResult)
	{
		struct acc__fetchByMerchantAccountId *p = btAlloc(pTrans, p, struct acc__fetchByMerchantAccountId);
		struct acc__updateResponse *pResponse = pResult->pObj;
		const AccountInfo *pAccount = findAccountByID(pInfo->uAccountID);
		BILLING_DEBUG_RESPONSE("acc__update", pResponse);
		btFreeObjResult(pTrans, pResult);

		if (btFetchAccountStep(pInfo->uAccountID, pTrans, btTransactionAuth_AccountFetchComplete, pInfo))
		{
			bContinuing = true;
		}
	}

	if(!bContinuing)
	{
		cleanTransactionInfo(&pInfo, pTrans);
	}
	PERFINFO_AUTO_STOP();
}

static void btTransactionAuth_AccountFetchComplete(BillingTransaction *pTrans)
{
	bool bContinuing = false;
	VindiciaXMLtoObjResult *pResult = NULL;
	TransactionInfo *pInfo = pTrans->userData;

	AccountInfo *pAccountInfo = NULL;
	Container *pAccountContainer = pInfo ? objGetContainer(GLOBALTYPE_ACCOUNT, pInfo->uAccountID) : NULL;

	if (!pInfo) return;

	PERFINFO_AUTO_START_FUNC();
	pResult = vindiciaCreateResponse(pTrans, VINDICIA_TYPE(acc, fetchByMerchantAccountIdResponse));

	if(pResult && pAccountContainer)
	{
		struct acc__fetchByMerchantAccountIdResponse *pResponse = pResult->pObj;
		BILLING_DEBUG_RESPONSE("acc__fetchByMerchantAccountId", pResponse);
		btFreeObjResult(pTrans, pResult);

		pAccountInfo = pAccountContainer->containerData;

		if(pAccountInfo && btPopulateVinAccountResponseFromAccount(pTrans, pResponse, pInfo->uAccountID))
		{
			if (pResponse->_account->VID && *pResponse->_account->VID)
			{
				// ---------------------------------------------------------------------------------------
				// Core SOAP objects
				struct vin__Transaction		   *pTransaction   = btAlloc(pTrans, pTransaction, struct vin__Transaction);
				struct vin__PaymentMethod	   *pPaymentMethod;
				struct trn__auth			   *pAuth		   = btAlloc(pTrans, pAuth, struct trn__auth);
				struct ArrayOfTransactionItems *pTransItems	   = btAlloc(pTrans, pTransItems, struct ArrayOfTransactionItems);
				int iCurItem;
				const FraudSettings *pFraudSettings = NULL;
				Money totalPrice;
				const char *pDivision = NULL;

				moneyInitFromStr(&totalPrice, "0", pInfo->pCurrency);
				
				// ---------------------------------------------------------------------------------------
				// Fraud Settings

				if (pInfo->pPaymentMethod->VID && *pInfo->pPaymentMethod->VID && !pInfo->pPaymentMethod->precreated)
				{
					const CachedPaymentMethod *pCachedPM = getCachedPaymentMethod(pAccountInfo, pInfo->pPaymentMethod->VID);

					if (!pCachedPM)
					{
						BILLING_DEBUG_WARNING("Could not find the VID of the payment method in the list of cached payment methods for the account.\n");
						btFail(pTrans, "Could not find the payment method for the VID provided.");
						cleanTransactionInfo(&pInfo, pTrans);
						return;
					}

					BILLING_DEBUG("Using cached payment method billing country for fraud settings: %s\n", pCachedPM->billingAddress.country);

					pFraudSettings = getFraudSettings(pCachedPM->billingAddress.country);
				}
				else if (pInfo->pPaymentMethod->country && *pInfo->pPaymentMethod->country)
				{
					BILLING_DEBUG("Using new payment method billing country for fraud settings: %s\n", pInfo->pPaymentMethod->country);

					pFraudSettings = getFraudSettings(pInfo->pPaymentMethod->country);
				}
				else
				{
					BILLING_DEBUG_WARNING("The billing country could not be determined for the transaction!\n");
					btFail(pTrans, "Billing country could not be determined.");
					cleanTransactionInfo(&pInfo, pTrans);
					PERFINFO_AUTO_STOP();
					return;
				}

				if (!pFraudSettings)
				{
					AssertOrAlert("ACCOUNTSERVER_MISSING_FRAUD_SETTINGS", "Fraud settings are incomplete!");
					btFail(pTrans, "Could not find fraud settings for the billing country provided.");
					cleanTransactionInfo(&pInfo, pTrans);
					PERFINFO_AUTO_STOP();
					return;
				}

				// ---------------------------------------------------------------------------------------
				// Products

				pTransItems->__size = eaSize(&pInfo->eaItems);
				pTransItems->__ptr = btAllocCount(pTrans, pTransItems->__ptr, struct vin__TransactionItem *, pTransItems->__size);
				for (iCurItem = 0; iCurItem < pTransItems->__size; iCurItem++)
				{
					const TransactionItem *pItem = pInfo->eaItems[iCurItem];
					const ProductContainer *pProduct = findProductByID(pItem->uProductID);
					const Money *pPrice = NULL;
					struct vin__TransactionItem *pVinItem = btAlloc(pTrans, pVinItem, struct vin__TransactionItem);

					pTransItems->__ptr[iCurItem] = pVinItem;

					// Ensure the product is valid
					if (!pProduct || !pProduct->pInternalName || !*pProduct->pInternalName)
					{
						AssertOrAlert("ACCOUNTSERVER_INVALID_PURCHASE", "Attempt to purchase invalid product: %d", pItem->uProductID);
						btFail(pTrans, "Could not find required product.");
						cleanTransactionInfo(&pInfo, pTrans);
						PERFINFO_AUTO_STOP();
						return;
					}

					BILLING_DEBUG("Attempting to purchase: %s\n", pProduct->pName);

					// Get the price of the product
					if (pItem->pPrice)
					{
						if (!stricmp_safe(moneyCurrency(pItem->pPrice), pInfo->pCurrency))
						{
							pPrice = pItem->pPrice;
						}
						else
						{
							btFail(pTrans, "Invalid price override.");
							cleanTransactionInfo(&pInfo, pTrans);
							PERFINFO_AUTO_STOP();
							return;
						}
					}
					else
					{
						pPrice = getProductPrice(pProduct, pInfo->pCurrency);
						if (!pPrice)
						{
							AssertOrAlert("ACCOUNTSERVER_INVALID_PURCHASE_PRICE", "Could not find a price in %s for the product %s!\n", pInfo->pCurrency, pProduct->pName);
							btFail(pTrans, "Could not find required price.");
							cleanTransactionInfo(&pInfo, pTrans);
							PERFINFO_AUTO_STOP();
							return;
						}
					}

					moneyAdd(&totalPrice, pPrice);

					// Get the division used for the purchase from the first product
					if (iCurItem == 0)
					{
						pDivision = billingGetDivision(DT_Product, pProduct->pInternalName);
					}
					else if (stricmp_safe(pDivision, billingGetDivision(DT_Product, pProduct->pInternalName)) != 0)
					{
						char *pError = NULL;
						int iCurErrItem = 0;

						estrStackCreate(&pError);

						estrPrintf(&pError, "A purchase involving %d products [", pTransItems->__size);
						for (iCurErrItem = 0; iCurErrItem < pTransItems->__size; iCurErrItem++)
						{
							if (iCurErrItem > 0)
							{
								estrConcatf(&pError, ", ");
							}
							estrConcatf(&pError, "%d", pInfo->eaItems[iCurErrItem]->uProductID);
						}
						estrConcatf(&pError, "] could not be routed to a single bank division. This indicates a web site bug.");

						AssertOrAlert("ACCOUNTSERVER_INVALID_DIVISION", "%s", pError);
						btFail(pTrans, pError);
						estrDestroy(&pError);
						cleanTransactionInfo(&pInfo, pTrans);
						PERFINFO_AUTO_STOP();
						return;
					}

					// Populate the Vindicia transaction item
					pVinItem->name = btStrdup(pTrans, pProduct->pDescription);
					pVinItem->price = btMoneyRaw(pTrans, pPrice);
					pVinItem->quantity = 1;
					pVinItem->sku = btStrdupWithPrefix(pTrans, pProduct->pName);
					pVinItem->taxClassification = btAlloc(pTrans, pVinItem->taxClassification, enum vin__TaxClassification);
					*pVinItem->taxClassification = btConvertTaxClassification(pProduct->eTaxClassification);
				}

				// ---------------------------------------------------------------------------------------
				// Payment Method

				pPaymentMethod = btCreateVindiciaPaymentMethod(pAccountInfo, pTrans, pInfo->pPaymentMethod, pInfo->bSendWholePM, pInfo->pBankName);

				// ---------------------------------------------------------------------------------------
				// Transaction

				pTransaction->account						  = pResponse->_account;
				pTransaction->amount						  = btMoneyRaw(pTrans, &totalPrice);
				pTransaction->currency						  = convertCurrencyCase(btStrdup(pTrans, pInfo->pCurrency));
				pTransaction->transactionItems				  = pTransItems;
				pTransaction->sourcePaymentMethod			  = pPaymentMethod;
				pTransaction->sourceIp						  = btStrdup(pTrans, pInfo->pIP);				
				pTransaction->shippingAddress				  = pResponse->_account->shippingAddress;

				btSetInterestingNameValues(pTrans, &pTransaction->nameValues, pDivision,
					pInfo->pPaymentMethod ? pInfo->pPaymentMethod->country : NULL, pInfo->pBankName);

				// ---------------------------------------------------------------------------------------
				// Transaction:Auth()

				pAuth->_auth					   = getVindiciaAuth();
				pAuth->_minChargebackProbability   = pFraudSettings->minChargebackProbability;
				pAuth->_transaction				   = pTransaction;
				
				// ---------------------------------------------------------------------------------------
				// Continue our transaction
				{
					char *xml = NULL;
					moneyDeinit(&totalPrice);
					if(vindiciaObjtoXML(&xml, pAuth, VO2X_OBJ(trn, auth)))
					{
						btContinue(pTrans, "trn:auth", xml, btTransactionAuth_Complete, pInfo);
						bContinuing = true;
					}
					estrDestroy(&xml);
				}
			}
			else
			{
				// Account doesn't exist, push it.
				struct acc__update * pAccUpdate  = btAlloc(pTrans, pAccUpdate, struct acc__update);
				char *xml = NULL;

				pAccUpdate->_auth = getVindiciaAuth();
				pAccUpdate->_account = pResponse->_account;

				if(vindiciaObjtoXML(&xml, pAccUpdate, VO2X_OBJ(acc, update)))
				{
					btContinue(pTrans, "acc:update", xml, btTransactionAuth_Pushed, pInfo);
					bContinuing = true;
				}

				estrDestroy(&xml);
			}
		}
	}
	else
	{
		estrPrintf(&pTrans->resultString, "btTransactionAuth() failed to find all necessary objects.");
	}

	if(!bContinuing)
	{
		cleanTransactionInfo(&pInfo, pTrans);
	}
	PERFINFO_AUTO_STOP();
}

BillingTransaction * btTransactionAuth(
	SA_PARAM_NN_VALID AccountInfo *pAccount, 
	SA_PARAM_NN_VALID EARRAY_OF(const TransactionItem) eaItems,
	SA_PARAM_NN_STR const char *pCurrency, 
	SA_PARAM_NN_VALID const PaymentMethod *pPaymentMethod,
	SA_PARAM_NN_STR const char *pIP,
	SA_PARAM_OP_STR const char *pBankName,
	SA_PARAM_OP_VALID BillingTransaction *pTrans,
	bool bSendWholePM,
	SA_PARAM_NN_VALID TransactionAuthCallback callback,
	SA_PARAM_OP_VALID void *userData)
{
	TransactionInfo *pInfo;

	if (!devassertmsg(pCurrency, "Must provide a currency to authorize.") ||
		!devassertmsg(pPaymentMethod, "Must provide a payment method to authorize."))
	{
		if (callback) callback(NULL, userData);
		return NULL;
	}

	PERFINFO_AUTO_START_FUNC();

	accountSetBillingEnabled(pAccount);

	BILLING_DEBUG_START;

	pInfo = StructCreate(parse_TransactionInfo);
	pInfo->uAccountID = pAccount->uID;
	pInfo->pCurrency = estrDup(pCurrency);
	pInfo->pPaymentMethod = StructClone(parse_PaymentMethod, pPaymentMethod);
	pInfo->callback = callback;
	pInfo->userData = userData;
	pInfo->pIP = estrDup(pIP);
	pInfo->bSendWholePM = bSendWholePM;
	pInfo->pBankName = pBankName ? estrDup(pBankName) : NULL;
	eaCopyStructs(&eaItems, &pInfo->eaItems, parse_TransactionItem);

	pTrans = pTrans ? pTrans : btCreateBlank(true);
	btTrackOutcome(pTrans);

	if (!btFetchAccountStep(pAccount->uID, pTrans, btTransactionAuth_AccountFetchComplete, pInfo))
	{
		StructDestroy(parse_TransactionInfo, pInfo);
		pTrans->userData = NULL;
		PERFINFO_AUTO_START("Failure Callback", 1);
		if (callback) callback(NULL, userData);
		PERFINFO_AUTO_STOP();
		return NULL;
	}

	PERFINFO_AUTO_STOP();
	return pTrans;
}


/************************************************************************/
/* Finalize a transaction                                               */
/************************************************************************/

static void btTransactionFinish_Complete(BillingTransaction *pTrans)
{
	TransactionFinishInfo *holder = pTrans->userData;
	VindiciaXMLtoObjResult *pResult = NULL;
	bool success = false;
	const char *merchantTransactionId = NULL;

	PERFINFO_AUTO_START_FUNC();
	pResult = holder->bCapture ?
		vindiciaCreateResponse(pTrans, VINDICIA_TYPE(trn, captureResponse)) :
		vindiciaCreateResponse(pTrans, VINDICIA_TYPE(trn, cancelResponse));

	if(pResult)
	{
		if (holder->bCapture)
		{
			struct trn__captureResponse *pResponse = pResult->pObj;
			success = pResponse->_return_->returnCode == VINDICIA_SUCCESS_CODE;
			if (devassert(pResponse->_results->__size == 1))
			{
				merchantTransactionId = pResponse->_results->__ptr[0]->merchantTransactionId;
			}
			BILLING_DEBUG_RESPONSE("trn__capture", pResponse);
		}
		else
		{
			struct trn__cancelResponse *pResponse = pResult->pObj;
			success = pResponse->_return_->returnCode == VINDICIA_SUCCESS_CODE;
			if (devassert(pResponse->_results->__size == 1))
			{
				merchantTransactionId = pResponse->_results->__ptr[0]->merchantTransactionId;
			}
			BILLING_DEBUG_RESPONSE("trn__cancel", pResponse);
		}

		btFreeObjResult(pTrans, pResult);
	}

	if (holder->pError)
	{
		btFail(pTrans, "%s", holder->pError);
		success = false;
	}

	PERFINFO_AUTO_START("Callback", 1);
	if (holder->callback)(*(TransactionFinishCallback)holder->callback)(success, holder->uFraudFlags, holder->userData, merchantTransactionId);
	PERFINFO_AUTO_STOP();

	free(holder);
	PERFINFO_AUTO_STOP();
}

// Converts a transaction status into a string
SA_RET_NN_STR __forceinline static const char * TransactionStatusToString(enum vin__TransactionStatusType type)
{
	switch (type)
	{
	case vin__TransactionStatusType__New: return "New";
	case vin__TransactionStatusType__AuthorizationPending: return "Authorization Pending";
	case vin__TransactionStatusType__AuthorizedPending: return "Authorized Pending";
	case vin__TransactionStatusType__Authorized: return "Authorized";
	case vin__TransactionStatusType__AuthorizedForValidation: return "Authorized For Validation";
	case vin__TransactionStatusType__Cancelled: return "Cancelled";
	case vin__TransactionStatusType__Captured: return "Captured";
	case vin__TransactionStatusType__Settled: return "Settled";
	case vin__TransactionStatusType__Refunded: return "Refunded";
	case vin__TransactionStatusType__Pending: return "Pending";
	}
	AssertOrAlert("ACCOUNTSERVER_BILLING_INVALID_TRANSACTION_STATUS", "Transaction status is of an invalid type!");
	return "Unknown";
}

// Handle the status array and return the appropriate authoritative one
SA_RET_OP_VALID static const struct vin__TransactionStatus * 
HandleStatusArray(SA_PARAM_NN_VALID AccountInfo *pAccountInfo,
			      SA_PARAM_NN_VALID struct ArrayOfTransactionStatuses *pStatusLog,
			      SA_PARAM_OP_STR const char *pCurrency)
{
	int i;
	struct vin__TransactionStatus *pStatus = NULL;

	PERFINFO_AUTO_START_FUNC();

	if (!pStatusLog->__ptr)
	{
		AssertOrAlert("ACCOUNTSERVER_BILLING_STATUS_MISSING", "The status log is missing in the Vindicia response, so it could not be logged or acted upon.");
		PERFINFO_AUTO_STOP();
		return NULL;
	}

	// The first one is the authoritative one
	if (pStatusLog->__ptr[0])
	{
		if (pStatusLog->__ptr[0]->status != vin__TransactionStatusType__Authorized)
		{
			BILLING_DEBUG_WARNING("Transaction log status is not authorized: %s\n", TransactionStatusToString(pStatusLog->__ptr[0]->status));
			accountLog(pAccountInfo, "Transaction log status is not authorized: %s", TransactionStatusToString(pStatusLog->__ptr[0]->status));
		}
		else if (pStatusLog->__ptr[0]->creditCardStatus)
		{
			pStatus = pStatusLog->__ptr[0];
			accountLog(pAccountInfo, "Credit card status: %s (authCode: '%s', AVSCode: '%s', CVNCode: '%s')",
				TransactionStatusToString(pStatusLog->__ptr[0]->status),
				NULL_TO_EMPTY(pStatusLog->__ptr[0]->creditCardStatus->authCode),
				NULL_TO_EMPTY(pStatusLog->__ptr[0]->creditCardStatus->avsCode),
				NULL_TO_EMPTY(pStatusLog->__ptr[0]->creditCardStatus->cvnCode));
			accountReportTransactionCodes(pAccountInfo, NULL_TO_EMPTY(pCurrency),
				pStatusLog->__ptr[0]->creditCardStatus->authCode,
				pStatusLog->__ptr[0]->creditCardStatus->avsCode,
				pStatusLog->__ptr[0]->creditCardStatus->cvnCode);
		}
		else if (pStatusLog->__ptr[0]->payPalStatus)
		{
			pStatus = pStatusLog->__ptr[0];
			accountLog(pAccountInfo, "PayPal status: %s (authCode: '%s')",
				TransactionStatusToString(pStatusLog->__ptr[0]->status),
				NULL_TO_EMPTY(pStatusLog->__ptr[0]->payPalStatus->authCode));
		}
		else if (pStatusLog->__ptr[0]->directDebitStatus)
		{
			pStatus = pStatusLog->__ptr[0];
			accountLog(pAccountInfo, "Direct debit status: %s (authCode: '%s')",
				TransactionStatusToString(pStatusLog->__ptr[0]->status),
				NULL_TO_EMPTY(pStatusLog->__ptr[0]->directDebitStatus->authCode));
		}
		else
		{
			BILLING_DEBUG_WARNING("Payment method specific status is missing from Vindicia authorization response!\n");
			accountLog(pAccountInfo, "Authorization missing payment method specific status.");
		}
	}

	// Go ahead and debug-log them all, though
	for (i = 0; i < pStatusLog->__size; i++)
	{
		if (pStatusLog->__ptr[i])
		{
			struct vin__TransactionStatusCreditCard *pCreditCardStatus = pStatusLog->__ptr[i]->creditCardStatus;
			struct vin__TransactionStatusPayPal *pPayPalStatus = pStatusLog->__ptr[i]->payPalStatus;
			struct vin__TransactionStatusDirectDebit *pDirectDebitStatus = pStatusLog->__ptr[i]->directDebitStatus;

			if (pCreditCardStatus)
			{
				BILLING_DEBUG("\t(%d - %s) AuthCode: '%s' AVSCode: '%s' CVNCode: '%s'\n", i,
					TransactionStatusToString(pStatusLog->__ptr[i]->status),
					NULL_TO_EMPTY(pCreditCardStatus->authCode),
					NULL_TO_EMPTY(pCreditCardStatus->avsCode),
					NULL_TO_EMPTY(pCreditCardStatus->cvnCode));
			}
			else if (pPayPalStatus)
			{
				BILLING_DEBUG("\t(%d - %s) AuthCode: '%s'\n", i,
					TransactionStatusToString(pStatusLog->__ptr[i]->status),
					NULL_TO_EMPTY(pPayPalStatus->authCode));
			}
			else if (pDirectDebitStatus)
			{
				BILLING_DEBUG("\t(%d - %s) AuthCode: '%s'\n", i,
					TransactionStatusToString(pStatusLog->__ptr[i]->status),
					NULL_TO_EMPTY(pDirectDebitStatus->authCode));
			}
			else
			{
				BILLING_DEBUG("\t(%d - %s) Missing payment method specific status!\n", i,
					TransactionStatusToString(pStatusLog->__ptr[i]->status));
			}
		}
	}

	PERFINFO_AUTO_STOP();
	return pStatus;
}

SA_RET_OP_VALID __forceinline static BillingTransaction * btTransactionFinishFail(
	SA_PARAM_OP_VALID BillingTransaction *pTrans,
	bool bComplete,
	SA_PARAM_OP_VALID TransactionFinishCallback callback,
	SA_PARAM_OP_VALID void *userData,
	SA_PARAM_OP_STR const char *pFailMessage
	)
{
	PERFINFO_AUTO_START_FUNC();
	if (pTrans)
	{
		pTrans->userData = NULL;
		if (pFailMessage) btFail(pTrans, pFailMessage);
	}
	PERFINFO_AUTO_START("Callback", 1);
	if (callback) callback(false, 0, userData, NULL);
	PERFINFO_AUTO_STOP();
	PERFINFO_AUTO_STOP();
	return pTrans;
}

BillingTransaction * btTransactionFinish(BillingTransaction *pTrans, bool bCapture, bool bCompleteTrans, SA_PARAM_OP_VALID TransactionFinishCallback callback, SA_PARAM_OP_VALID void *userData)
{
	void *pVinObject = NULL;
	AccountInfo *pAccountInfo = NULL;
	const char *pError = NULL;
	U32 uFraudFlags = 0;
	const struct vin__TransactionStatus *pStatus = NULL;
	struct vin__Transaction *pVinTrans = NULL;

	if (!devassertmsg(pTrans, "Must provide a transaction to finish.")) return NULL;

	PERFINFO_AUTO_START_FUNC();
	BILLING_DEBUG_START;

	pVinTrans = pTrans->userData;
	pTrans->userData = NULL;

	if (!devassert(pVinTrans))
	{
		PERFINFO_AUTO_STOP();
		return NULL;
	}
		
	// Make sure the account exists
	if (pVinTrans->account)
		pAccountInfo = findAccountByMerchantAccountID(pVinTrans->account->merchantAccountId);

	if (!pAccountInfo)
	{
		PERFINFO_AUTO_STOP();
		return btTransactionFinishFail(pTrans, bCompleteTrans, callback, userData, "Could not find account.");
	}

	// Get the status of the auth
	if (pVinTrans->statusLog)
		pStatus = HandleStatusArray(pAccountInfo, pVinTrans->statusLog, pVinTrans->currency);

	if (!pStatus)
	{
		if (pVinTrans->statusLog &&
			pVinTrans->statusLog->__ptr &&
			pVinTrans->statusLog->__ptr[0] &&
			pVinTrans->statusLog->__ptr[0]->status)
		{
			const char *pPmtType = "unknown";
			if (pVinTrans->statusLog->__ptr[0]->creditCardStatus) pPmtType = "credit card";
			if (pVinTrans->statusLog->__ptr[0]->payPalStatus) pPmtType = "PayPal";
			if (pVinTrans->statusLog->__ptr[0]->directDebitStatus) pPmtType = "direct debit";

			PERFINFO_AUTO_STOP();
			return btTransactionFinishFail(pTrans, bCompleteTrans, callback, userData,
				STACK_SPRINTF("Transaction status is not currently in the authorized state (it is '%s' with a %s payment method)",
					TransactionStatusToString(pVinTrans->statusLog->__ptr[0]->status),
					pPmtType));
		}
		else
		{
			PERFINFO_AUTO_STOP();
			return btTransactionFinishFail(pTrans, bCompleteTrans, callback, userData,
				"Transaction status is missing.");
		}
	}

	if (pStatus->creditCardStatus)
	{
		const char *pBillingCountry = NULL;
		bool bExistingPaymentMethodUsed = false;
		int iMinChargebackProb = 0;

		// Get the billing country
		if (pVinTrans->sourcePaymentMethod && pVinTrans->sourcePaymentMethod->VID && *pVinTrans->sourcePaymentMethod->VID)
		{
			const CachedPaymentMethod *pCachedPM = getCachedPaymentMethod(pAccountInfo, pVinTrans->sourcePaymentMethod->VID);

			if (pCachedPM)
			{
				pBillingCountry = pCachedPM->billingAddress.country;
			}
			else if (pVinTrans->sourcePaymentMethod->billingAddress &&
				pVinTrans->sourcePaymentMethod->billingAddress->country)
			{
				pBillingCountry = pVinTrans->sourcePaymentMethod->billingAddress->country;
			}
			else
			{
				BILLING_DEBUG_WARNING("Could not find the payment method in the account's list of valid payment methods.\n");
				PERFINFO_AUTO_STOP();
				return btTransactionFinishFail(pTrans, bCompleteTrans, callback, userData, "Could not find payment method.");
			}

			bExistingPaymentMethodUsed = true;
		}
		if (!pBillingCountry || !(*pBillingCountry))
		{
			BILLING_DEBUG_WARNING("Could not find a billing country to use for the payment method.\n");
			PERFINFO_AUTO_STOP();
			return btTransactionFinishFail(pTrans, bCompleteTrans, callback, userData, "Could not find billing country.");
		}

		// Parse the fraud settings
		if (!parseFraudSettings(pBillingCountry, 
			pStatus->creditCardStatus->cvnCode, pStatus->creditCardStatus->avsCode, &iMinChargebackProb, &uFraudFlags))
		{
			BILLING_DEBUG_WARNING("Card did not pass fraud settings.\n");
			PERFINFO_AUTO_STOP();
			return btTransactionFinishFail(pTrans, bCompleteTrans, callback, userData, "Could not find parse fraud settings.");
		}

		// Check against fraud settings
		if (bCapture)
		{
			if (!VALID_MIN_CHARGEBACK(iMinChargebackProb))
			{
				bCapture = false;
				pError = "Failed to get a valid minimum charge-back probability.";
				BILLING_DEBUG_WARNING("Failed to find minimum charge-back probability for %s.  This implies incomplete configuration.\n", pBillingCountry);
			}
			else if (uFraudFlags & INVALID_AVS)
			{
				bCapture = false;
				pError = "Invalid AVS.";
				BILLING_DEBUG_WARNING("Failed AVS configured requirements for %s.\n", pBillingCountry);
			}
			else if (uFraudFlags & INVALID_CVN && !bExistingPaymentMethodUsed)
			{
				bCapture = false;
				pError = "Invalid CVN.";
				BILLING_DEBUG_WARNING("Failed CVN configured requirements for %s.\n", pBillingCountry);
			}
		}
	}
	else
	{
		BILLING_DEBUG_WARNING("Skipping fraud checks because it is not a credit card payment.\n");
	}

	if (bCapture)
	{
		struct trn__capture *trn = btAlloc(pTrans, trn, struct trn__capture);
		trn->_auth = getVindiciaAuth();
		trn->_transactions = btAlloc(pTrans, trn->_transactions, struct ArrayOfTransactions);
		trn->_transactions->__size = 1;
		trn->_transactions->__ptr = btAlloc(pTrans, trn->_transactions->__ptr, struct vin__Transaction *);
		*trn->_transactions->__ptr = pVinTrans;
		pVinObject = trn;
	}
	else
	{
		struct trn__cancel *trn = btAlloc(pTrans, trn, struct trn__cancel);
		trn->_auth = getVindiciaAuth();
		trn->_transactions = btAlloc(pTrans, trn->_transactions, struct ArrayOfTransactions);
		trn->_transactions->__size = 1;
		trn->_transactions->__ptr = btAlloc(pTrans, trn->_transactions->__ptr, struct vin__Transaction *);
		*trn->_transactions->__ptr = pVinTrans;
		pVinObject = trn;
	}

	{
		char *xml = NULL;

		if( (bCapture && vindiciaObjtoXML(&xml, pVinObject, VO2X_OBJ(trn, capture))) ||
		   (!bCapture && vindiciaObjtoXML(&xml, pVinObject, VO2X_OBJ(trn, cancel))))
		{
			TransactionFinishInfo *holder = callocStruct(TransactionFinishInfo);
			holder->callback = callback;
			holder->userData = userData;
			holder->bCapture = bCapture;
			holder->pError = pError;
			holder->uFlags |= bCompleteTrans ? TRANSACTIONINFO_COMPLETE : 0;
			holder->uFraudFlags = uFraudFlags;

			btContinue(pTrans, bCapture ? "trn:capture" : "trn:cancel", xml, btTransactionFinish_Complete, holder);
		}
		else
		{
			if (callback) callback(false, 0, userData, NULL);
		}

		estrDestroy(&xml);
	}

	PERFINFO_AUTO_STOP();
	return pTrans;
}

#include "billingTransaction_c_ast.c"