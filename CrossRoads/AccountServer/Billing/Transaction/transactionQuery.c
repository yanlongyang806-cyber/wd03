#include "transactionQuery.h"
#include "timing.h"
#include "AccountTransactionLog.h"
#include "Account/billingAccount.h"
#include "billing.h"
#include "EString.h"
#include "error.h"
#include "UpdatePaymentMethod.h"
#include "vindicia.h"
#include "timing_profiler.h"
#include "StringUtil.h"

/************************************************************************/
/* Types                                                                */
/************************************************************************/

// Billing transaction data for btFetchTransactionByVID() and btFetchTransactionByMTID().
typedef struct btFetchTransaction_Data
{
	btFetchAccountTransactionCallback callback;			// Completion callback
	void *userData;										// Pointer to pass to callback
} btFetchTransaction_Data;

// Billing transaction data for btFetchAccountTransactions.
typedef struct btFetchAccountTransactions_Data
{
	btFetchAccountTransactionsCallback callback;		// Completion callback
	void *userData;										// Pointer to pass to callback
} btFetchAccountTransactions_Data;

// Billing transaction data for fetch delta transactions
typedef struct btFetchDelta_Data
{
	btFetchAccountTransactionsCallback callback;		// Completion callback
	void *userData;										// Pointer to pass to callback
	U32 uTimeSS2000;
	U32 uTimeSS2000End;
	unsigned int uCurPage;
	TransactionFilterFlags filters;
	EARRAY_OF(AccountTransactionInfo) eaTransactions;
} btFetchDelta_Data;


/************************************************************************/
/* Private functions                                                    */
/************************************************************************/

// Copy a vin__PaymentMethod to a PaymentMethod.
static void CopyPaymentMethodFromVindicia(SA_PARAM_NN_VALID const struct vin__PaymentMethod *source,
										  SA_PARAM_NN_VALID PaymentMethod *dest)
{
	if (!verify(source)) return;
	if (!verify(dest)) return;

	PERFINFO_AUTO_START_FUNC();

	// General information
	dest->VID = estrDupIfNonempty(source->VID);
	dest->accountHolderName = estrDupIfNonempty(source->accountHolderName);
	dest->customerSpecifiedType = estrDupIfNonempty(source->customerSpecifiedType);
	dest->customerDescription = estrDupIfNonempty(source->customerDescription);
	dest->currency = estrDupIfNonempty(source->currency);
	dest->active = *source->active;

	// Credit card information
	if (source->creditCard)
	{
		if (!dest->creditCard)
		{
			dest->creditCard = StructCreate(parse_CreditCard);
		}

		dest->creditCard->account = estrDupIfNonempty(source->creditCard->account);		// The middle digits should be marked out by Vindicia.
		dest->creditCard->expirationDate = estrDupIfNonempty(source->creditCard->expirationDate);
	}
	
	// PayPal information
	if (source->paypal)
	{
		if (!dest->payPal)
		{
			dest->payPal = StructCreate(parse_PayPal);
		}

		dest->payPal->cancelUrl = estrDupIfNonempty(source->paypal->cancelUrl);
		dest->payPal->emailAddress = estrDupIfNonempty(source->paypal->emailAddress);
		dest->payPal->returnUrl = estrDupIfNonempty(source->paypal->returnUrl);
	}

	// Direct Debit information
	if (source->directDebit)
	{
		if (!dest->directDebit)
		{
			dest->directDebit = StructCreate(parse_DirectDebit);
		}

		dest->directDebit->account = estrDupIfNonempty(source->directDebit->account);
		dest->directDebit->bankSortCode = estrDupIfNonempty(source->directDebit->bankSortCode);
		dest->directDebit->ribCode = estrDupIfNonempty(source->directDebit->ribCode);
	}

	// Address information
	if (source->billingAddress)
	{
		dest->addressName = estrDupIfNonempty(source->billingAddress->name);
		dest->addr1 = estrDupIfNonempty(source->billingAddress->addr1);
		dest->addr2 = estrDupIfNonempty(source->billingAddress->addr2);
		dest->city = estrDupIfNonempty(source->billingAddress->city);
		dest->county = estrDupIfNonempty(source->billingAddress->county);
		dest->district = estrDupIfNonempty(source->billingAddress->district);
		dest->postalCode = estrDupIfNonempty(source->billingAddress->postalCode);
		dest->country = estrDupIfNonempty(source->billingAddress->country);
		dest->phone = estrDupIfNonempty(source->billingAddress->phone);
	}

	PERFINFO_AUTO_STOP_FUNC();
}

// Copy a vin__Transaction to a AccountTransactionInfo.
static void CopyTransactionFromVindicia(SA_PARAM_NN_VALID const struct vin__Transaction *source,
										SA_PARAM_NN_VALID AccountTransactionInfo *dest)
{
	EARRAY_OF(const ProductContainer) products = NULL;
	static struct soap *dummy = NULL; // Only used to store a temp buffer; used by some soap functions

	if (!verify(source)) return;
	if (!verify(dest)) return;

	if (!dummy) dummy = callocStruct(struct soap);
	if (!devassert(dummy)) return;

	PERFINFO_AUTO_START_FUNC();

	dest->vindiciaTransaction = source;

	// Product information
	if (source->merchantTransactionId)
	{
		products = AccountTransactionGetProductsByMTID(source->merchantTransactionId);
	}

	EARRAY_CONST_FOREACH_BEGIN(products, iCurProduct, iNumProducts);
	{
		const ProductContainer *pProduct = products[iCurProduct];

		if (!devassert(pProduct)) continue;

		eaPush(&dest->products, estrDup(pProduct->pName));
	}
	EARRAY_FOREACH_END;
	eaDestroy(&products);

	// General information
	dest->VID = estrDupIfNonempty(source->VID);
	dest->amount = estrDupIfNonempty(source->amount);
	dest->currency = estrDupIfNonempty(source->currency);
	dest->divisionNumber = estrDupIfNonempty(source->divisionNumber);
	dest->merchantTransactionId = estrDupIfNonempty(source->merchantTransactionId);
	dest->previousMerchantTransactionId = estrDupIfNonempty(source->previousMerchantTransactionId);
	dest->timestamp = *source->timestamp;
	dest->ecpTransactionType = estrDupIfNonempty(soap_vin__ECPTransactionType2s(dummy, *source->ecpTransactionType));

	dest->merchantAccountId = estrDupIfNonempty(source->account->merchantAccountId);
	if (dest->merchantAccountId)
	{
		dest->accountGUID = estrDup(billingSkipPrefix(dest->merchantAccountId));
	}
	
	dest->paymentProcessor = estrDupIfNonempty(source->paymentProcessor);
	dest->sourcePhoneNumber = estrDupIfNonempty(source->sourcePhoneNumber);
	if (source->shippingAddress)
	{
		dest->shippingAddressaddress1 = estrDupIfNonempty(source->shippingAddress->addr1);
		dest->shippingAddressaddress2 = estrDupIfNonempty(source->shippingAddress->addr2);
		dest->shippingAddresscity = estrDupIfNonempty(source->shippingAddress->city);
		dest->shippingAddressdistrict = estrDupIfNonempty(source->shippingAddress->district);
		dest->shippingAddresspostalCode = estrDupIfNonempty(source->shippingAddress->postalCode);
		dest->shippingAddresscountry = estrDupIfNonempty(source->shippingAddress->country);
		dest->shippingAddressphone = estrDupIfNonempty(source->shippingAddress->phone);
	}
	dest->merchantAffiliateId = estrDupIfNonempty(source->merchantAffiliateId);
	dest->merchantAffiliateSubId = estrDupIfNonempty(source->merchantAffiliateSubId);
	dest->userAgent = estrDupIfNonempty(source->userAgent);
	dest->note = estrDupIfNonempty(source->note);
	dest->preferredNotificationLanguage = estrDupIfNonempty(source->preferredNotificationLanguage);
	dest->sourceMacAddress = estrDupIfNonempty(source->sourceMacAddress);
	dest->sourceIp = estrDupIfNonempty(source->sourceIp);
	dest->billingStatementIdentifier = estrDupIfNonempty(source->billingStatementIdentifier);
	dest->verificationCode = estrDupIfNonempty(source->verificationCode);

	// Payment methods
	if (source->sourcePaymentMethod)
	{
		dest->sourcePaymentMethod = StructCreate(parse_PaymentMethod);
		CopyPaymentMethodFromVindicia(source->sourcePaymentMethod, dest->sourcePaymentMethod);
	}

	if (source->destPaymentMethod)
	{
		dest->destPaymentMethod = StructCreate(parse_PaymentMethod);
		CopyPaymentMethodFromVindicia(source->destPaymentMethod, dest->destPaymentMethod);
	}

	// Vindicia transaction status log
	if (source->statusLog && source->statusLog->__size)
	{
		int iCurLogEntry;

		eaSetSizeStruct(&dest->statusLog, parse_AccountTransactionStatus, source->statusLog->__size);

		for (iCurLogEntry = 0; iCurLogEntry < source->statusLog->__size; ++iCurLogEntry)
		{
			AccountTransactionStatus *pStatus = dest->statusLog[iCurLogEntry];

			pStatus->status
				= estrDupIfNonempty(soap_vin__TransactionStatusType2s(dummy, source->statusLog->__ptr[iCurLogEntry]->status));
			pStatus->timestamp = source->statusLog->__ptr[iCurLogEntry]->timestamp;
			pStatus->paymentMethodType
				= estrDupIfNonempty(soap_vin__PaymentMethodType2s(dummy, source->statusLog->__ptr[iCurLogEntry]->paymentMethodType));
			switch (source->statusLog->__ptr[iCurLogEntry]->paymentMethodType)
			{
				xcase vin__PaymentMethodType__CreditCard:
					if (source->statusLog->__ptr[iCurLogEntry]->creditCardStatus)
					{
						pStatus->authCode
							= estrDupIfNonempty(source->statusLog->__ptr[iCurLogEntry]->creditCardStatus->authCode);
						pStatus->avsCode = estrDupIfNonempty(source->statusLog->__ptr[iCurLogEntry]->creditCardStatus->avsCode);
						pStatus->cvnCode = estrDupIfNonempty(source->statusLog->__ptr[iCurLogEntry]->creditCardStatus->cvnCode);
					}

				xcase vin__PaymentMethodType__PayPal:
					if (source->statusLog->__ptr[iCurLogEntry]->payPalStatus)
					{
						pStatus->authCode = estrDupIfNonempty(source->statusLog->__ptr[iCurLogEntry]->payPalStatus->authCode);
						pStatus->token = estrDupIfNonempty(source->statusLog->__ptr[iCurLogEntry]->payPalStatus->token);
						pStatus->redirectUrl
							= estrDupIfNonempty(source->statusLog->__ptr[iCurLogEntry]->payPalStatus->redirectUrl);
					}

				xcase vin__PaymentMethodType__ECP:
					if (source->statusLog->__ptr[iCurLogEntry]->ecpStatus)
					{
						pStatus->authCode = estrDupIfNonempty(source->statusLog->__ptr[iCurLogEntry]->ecpStatus->authCode);
					}

				xcase vin__PaymentMethodType__DirectDebit:
					if (source->statusLog->__ptr[iCurLogEntry]->directDebitStatus)
					{
						pStatus->authCode
							= estrDupIfNonempty(source->statusLog->__ptr[iCurLogEntry]->directDebitStatus->authCode);
					}

				xcase vin__PaymentMethodType__Token:
					// Not supported

				xcase vin__PaymentMethodType__Boleto:
					if (source->statusLog->__ptr[iCurLogEntry]->boletoStatus)
					{
						pStatus->uri = estrDupIfNonempty(source->statusLog->__ptr[iCurLogEntry]->boletoStatus->uri);
					}

				xdefault:
					AssertOrAlert("ACCOUNTSERVER_VINDICIA_UNKNOWN_PM_TYPE", "Vindicia returned an unknown PM type (in " __FUNCTION__ ").");
			}
		}
	}

	PERFINFO_AUTO_STOP_FUNC();
}

#define btHandleUnknownVindiciaResponse(pTrans, iReturnCode, pReturnString) btHandleUnknownVindiciaResponse_dbg(pTrans, iReturnCode, pReturnString, __FUNCTION__)
static void btHandleUnknownVindiciaResponse_dbg(SA_PARAM_NN_VALID BillingTransaction *pTrans,
											    int iReturnCode,
											    SA_PARAM_OP_STR const char *pReturnString,
											    SA_PARAM_NN_STR const char *pFunction)
{
	static char *pError = NULL;

	if (!verify(pTrans)) return;
	if (!verify(pFunction)) return;

	estrPrintf(&pError, "Received unknown response %d from Vindicia in %s: %s", iReturnCode,
		pFunction, pReturnString ? pReturnString : "none");

	AssertOrAlert("ACCOUNTSERVER_VINDICIA_RESPONSE", "%s", pError);
	btFail(pTrans, "Unknown response from Vindicia");

	BILLING_DEBUG_WARNING("%s\n", pError);
}

#define RequestFailureAssert(method) devassertmsgf(false, "Could not create %s request in " __FUNCTION__, method)

#define NOT_FOUND_CODE(iCode) ((iCode) == VINDICIA_SERVER_ERROR_CODE || (iCode) == VINDICIA_NOT_FOUND_CODE)

static void btHandleSingleTransaction(SA_PARAM_NN_VALID BillingTransaction *pTrans,
									  int iReturnCode,
									  SA_PARAM_OP_STR const char *pReturnString,
									  SA_PARAM_NN_VALID struct vin__Transaction *pVinTrans)
{
	btFetchTransaction_Data *data = pTrans->userData;
	AccountTransactionInfo *pInfo = NULL;

	if (!verify(pTrans)) return;
	if (!verify(pVinTrans)) return;

	PERFINFO_AUTO_START_FUNC();

	if (NOT_FOUND_CODE(iReturnCode))
	{
		if (devassert(data->callback))
		{
			PERFINFO_AUTO_START("Failure Callback", 1);
			data->callback(pTrans, false, NULL, data->userData);
			PERFINFO_AUTO_STOP();
		}
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}
	else if (iReturnCode != VINDICIA_SUCCESS_CODE)
	{
		btHandleUnknownVindiciaResponse(pTrans, iReturnCode, pReturnString);
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// Copy transaction data.
	pInfo = btAlloc(pTrans, pInfo, AccountTransactionInfo);
	CopyTransactionFromVindicia(pVinTrans, pInfo);

	// Return to original caller.
	if (devassert(data->callback))
	{
		PERFINFO_AUTO_START("Success Callback", 1);
		data->callback(pTrans, true, pInfo, data->userData);
		PERFINFO_AUTO_STOP();
	}

	PERFINFO_AUTO_STOP_FUNC();
}


/************************************************************************/
/* Get a list of transactions for an account.                           */
/************************************************************************/

// Continue processing btFetchAccountTransactions by returning list of transactions.
static void btFetchAccountTransactions_TransactionFetchComplete(SA_PARAM_NN_VALID BillingTransaction *pTrans)
{
	btFetchAccountTransactions_Data *data = pTrans->userData;
	VindiciaXMLtoObjResult *pResult = NULL;
	struct trn__fetchByAccountResponse *pResponse = NULL;
	EARRAY_OF(AccountTransactionInfo) eaInfo = NULL;

	PERFINFO_AUTO_START_FUNC();

	// Get transaction fetch response.
	pResult = vindiciaCreateResponse(pTrans, VINDICIA_TYPE(trn, fetchByAccountResponse));
	if (!pResult)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}
	btFreeObjResult(pTrans, pResult);
	pResponse = pResult->pObj;
	BILLING_DEBUG_RESPONSE("trn__fetchByAccount", pResponse);

	if (pResponse->_return_->returnCode != VINDICIA_SUCCESS_CODE)
	{
		btHandleUnknownVindiciaResponse(pTrans, pResponse->_return_->returnCode, pResponse->_return_->returnString);
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// Populate list of transactions.
	if (pResponse->_transactions->__size)
	{
		eaSetSizeStruct(&eaInfo, parse_AccountTransactionInfo, pResponse->_transactions->__size);

		EARRAY_CONST_FOREACH_BEGIN(eaInfo, iCurInfo, iNumInfo);
		{
			AccountTransactionInfo *pInfo = eaInfo[iCurInfo];

			if (!devassert(pInfo)) continue;

			CopyTransactionFromVindicia(pResponse->_transactions->__ptr[iCurInfo], pInfo);
		}
		EARRAY_FOREACH_END;
	}

	// Return to original caller.
	if (devassert(data->callback))
	{
		PERFINFO_AUTO_START("Success Callback", 1);
		data->callback(pTrans, true, eaInfo, data->userData);
		PERFINFO_AUTO_STOP();
	}

	PERFINFO_AUTO_STOP_FUNC();
}

// Continue processing btFetchAccountTransactions by getting account fetch response and requesting a list of transactions.
static void btFetchAccountTransactions_AccountFetchComplete(SA_PARAM_NN_VALID BillingTransaction *pTrans)
{
	static const char *pVinMethod = VINDICIA_CALL_NAME(trn, fetchByAccount);
	btFetchAccountTransactions_Data *data = pTrans->userData;
	VindiciaXMLtoObjResult *pResult = NULL;
	struct acc__fetchByMerchantAccountIdResponse *pResponse = NULL;
	struct trn__fetchByAccount request = {0};
	char *xmlRequest = NULL;

	PERFINFO_AUTO_START_FUNC();

	// Get account fetch response.
	pResult = vindiciaCreateResponse(pTrans, VINDICIA_TYPE(acc, fetchByMerchantAccountIdResponse));
	if (!pResult)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}
	btFreeObjResult(pTrans, pResult);
	pResponse = pResult->pObj;
	BILLING_DEBUG_RESPONSE("acc__fetchByMerchantAccountId", pResponse);

	if (pResponse->_return_->returnCode == VINDICIA_BAD_REQUEST_CODE)
	{
		// Account not found, so there are no transactions.
		pTrans->result = BTR_SUCCESS;

		if (devassert(data->callback))
		{
			PERFINFO_AUTO_START("Failure Callback", 1);
			data->callback(pTrans, false, 0, data->userData);
			PERFINFO_AUTO_STOP();
		}
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}
	else if (pResponse->_return_->returnCode != VINDICIA_SUCCESS_CODE)
	{
		btHandleUnknownVindiciaResponse(pTrans, pResponse->_return_->returnCode, pResponse->_return_->returnString);
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// Request the list of transactions
	request._auth = getVindiciaAuth();
	request._account = pResponse->_account;

	if (vindiciaObjtoXML(&xmlRequest, &request, VO2X_OBJ(trn, fetchByAccount)))
	{
		btContinue(pTrans, pVinMethod, xmlRequest, btFetchAccountTransactions_TransactionFetchComplete, data);
	}
	else
	{
		RequestFailureAssert(pVinMethod);
	}

	estrDestroy(&xmlRequest);

	PERFINFO_AUTO_STOP_FUNC();
}

// Get a list of transactions that have been performed by an account.
// callback will be called with an EARRAY of the struct, which it must destroy.
// Start off by getting the account ID for this object.
BillingTransaction *
btFetchAccountTransactions(U32 uAccountID,
						   BillingTransaction *pTrans,
						   btFetchAccountTransactionsCallback callback,
						   void *userData)
{
	bool bAccountFetchInitiated = false;
	btFetchAccountTransactions_Data *data = NULL;

	if (!verify(callback)) return pTrans;

	PERFINFO_AUTO_START_FUNC();
	BILLING_DEBUG_START;

	// Set up billing transaction.
	if (!pTrans)
	{
		pTrans = btCreateBlank(true);
	}
	data = btAlloc(pTrans, data, btFetchAccountTransactions_Data);
	data->callback = callback;
	data->userData = userData;

	// Request the account object for this account ID.
	bAccountFetchInitiated = btFetchAccountStep(uAccountID, pTrans, btFetchAccountTransactions_AccountFetchComplete, data);
	devassert(bAccountFetchInitiated);

	PERFINFO_AUTO_STOP_FUNC();

	return pTrans;
}


/************************************************************************/
/* Look up a transaction by VID                                         */
/************************************************************************/

// Continue processing btFetchAccountTransactions by returning list of transactions.
static void btFetchTransactionByVID_TransactionFetchComplete(SA_PARAM_NN_VALID BillingTransaction *pTrans)
{
	VindiciaXMLtoObjResult *pResult = NULL;
	struct trn__fetchByVidResponse *pResponse = NULL;

	PERFINFO_AUTO_START_FUNC();

	// Get transaction fetch response.
	pResult = vindiciaCreateResponse(pTrans, VINDICIA_TYPE(trn, fetchByVidResponse));
	if (!pResult)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}
	btFreeObjResult(pTrans, pResult);
	pResponse = pResult->pObj;
	BILLING_DEBUG_RESPONSE("trn__fetchByVidResponse", pResponse);

	btHandleSingleTransaction(pTrans, pResponse->_return_->returnCode, pResponse->_return_->returnString, pResponse->_transaction);

	PERFINFO_AUTO_STOP_FUNC();
}

// Get transaction information for a VID.
BillingTransaction *
btFetchTransactionByVID(const char *VID,
						BillingTransaction *pTrans,
						btFetchAccountTransactionCallback callback,
						void *userData)
{
	static const char *pVinMethod = VINDICIA_CALL_NAME(trn, fetchByVid);
	btFetchTransaction_Data *data = NULL;
	struct trn__fetchByVid request = {0};
	char *xmlRequest = NULL;

	if (!verify(VID && *VID)) return pTrans;
	if (!verify(callback)) return pTrans;

	PERFINFO_AUTO_START_FUNC();
	BILLING_DEBUG_START;

	// Set up transaction.
	if (!pTrans)
	{
		pTrans = btCreateBlank(true);
	}
	data = btAlloc(pTrans, data, btFetchTransaction_Data);
	data->callback = callback;
	data->userData = userData;

	// Request the list of transactions
	request._auth = getVindiciaAuth();
	request._vid = (char *)VID;

	if (vindiciaObjtoXML(&xmlRequest, &request, VO2X_OBJ(trn, fetchByVid)))
	{
		btContinue(pTrans, pVinMethod, xmlRequest, btFetchTransactionByVID_TransactionFetchComplete, data);
	}
	else
	{
		RequestFailureAssert(pVinMethod);
	}
	
	estrDestroy(&xmlRequest);

	PERFINFO_AUTO_STOP_FUNC();

	return pTrans;
}


/************************************************************************/
/* Look up a transaction by MTID                                        */
/************************************************************************/

// Continue processing btFetchAccountTransactions by returning list of transactions.
static void btFetchTransactionByMTID_TransactionFetchComplete(SA_PARAM_NN_VALID BillingTransaction *pTrans)
{
	VindiciaXMLtoObjResult *pResult = NULL;
	struct trn__fetchByMerchantTransactionIdResponse *pResponse = NULL;

	PERFINFO_AUTO_START_FUNC();

	// Get transaction fetch response.
	pResult = vindiciaCreateResponse(pTrans, VINDICIA_TYPE(trn, fetchByMerchantTransactionIdResponse));
	if (!pResult)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}
	btFreeObjResult(pTrans, pResult);
	pResponse = pResult->pObj;
	BILLING_DEBUG_RESPONSE("trn__fetchByMerchantTransactionIdResponse", pResponse);

	btHandleSingleTransaction(pTrans, pResponse->_return_->returnCode, pResponse->_return_->returnString, pResponse->_transaction);

	PERFINFO_AUTO_STOP_FUNC();
}

// Get transaction information for a MTID.
BillingTransaction *
btFetchTransactionByMTID(const char *MTID,
						 BillingTransaction *pTrans,
						 btFetchAccountTransactionCallback callback,
						 void *userData)
{
	static const char *pVinMethod = VINDICIA_CALL_NAME(trn, fetchByMerchantTransactionId);
	btFetchTransaction_Data *data = NULL;
	struct trn__fetchByMerchantTransactionId request = {0};
	char *xmlRequest = NULL;

	if (!verify(MTID && *MTID)) return pTrans;
	if (!verify(callback)) return pTrans;

	PERFINFO_AUTO_START_FUNC();
	BILLING_DEBUG_START;

	// Set up transaction.
	if (!pTrans)
	{
		pTrans = btCreateBlank(true);
	}
	data = btAlloc(pTrans, data, btFetchTransaction_Data);
	data->callback = callback;
	data->userData = userData;

	// Request the list of transactions
	request._auth = getVindiciaAuth();
	request._merchantTransactionId = (char *)MTID;

	if (vindiciaObjtoXML(&xmlRequest, &request, VO2X_OBJ(trn, fetchByMerchantTransactionId)))
	{
		btContinue(pTrans, pVinMethod, xmlRequest, btFetchTransactionByMTID_TransactionFetchComplete, data);
	}
	else
	{
		RequestFailureAssert(pVinMethod);
	}

	estrDestroy(&xmlRequest);

	PERFINFO_AUTO_STOP_FUNC();
	
	return pTrans;
}


/************************************************************************/
/* Fetch changed transactions                                           */
/************************************************************************/

#define TRANS_DELTA_PAGE_SIZE 10

static bool btVinTransMeetsFilters(SA_PARAM_NN_VALID const struct vin__Transaction *pVinTrans, TransactionFilterFlags filters)
{
	if (!verify(pVinTrans)) return false;

	if (!pVinTrans->account) return false;

	if (!pVinTrans->account->merchantAccountId) return false;

	if (strncmp(pVinTrans->account->merchantAccountId, billingGetPrefix(), strlen(billingGetPrefix()))) return false;

	if (filters & TFF_Cancelled)
	{
		if (!pVinTrans->statusLog) return false;
		if (!pVinTrans->statusLog->__ptr) return false;
		if (!pVinTrans->statusLog->__ptr[0]) return false;
		if (pVinTrans->statusLog->__ptr[0]->status != vin__TransactionStatusType__Cancelled) return false;
	}

	if (filters & TFF_DirectDebit)
	{
		if (!pVinTrans->sourcePaymentMethod) return false;
		if (!pVinTrans->sourcePaymentMethod->directDebit) return false;
	}

	if (filters & TFF_NotValidationProduct)
	{
		int iCurItem = 0;

		if (!pVinTrans->transactionItems) return true;
		if (!pVinTrans->transactionItems->__ptr) return true;
		if (!pVinTrans->transactionItems->__size) return true;

		for (iCurItem = 0; iCurItem < pVinTrans->transactionItems->__size; iCurItem++)
		{
			struct vin__TransactionItem *pItem = pVinTrans->transactionItems->__ptr[iCurItem];

			if (!devassert(pItem)) continue;

			if (!strncmp(pItem->sku, billingGetPrefix(), strlen(billingGetPrefix())))
			{
				if (!stricmp_safe(pItem->sku + strlen(billingGetPrefix()), billingGetValidationProduct()))
				{
					return false;
				}
			}
		}
	}

	return true;
}

static void btFetchChangedTransactionsSince_Page(SA_PARAM_NN_VALID BillingTransaction *pTrans);

static void btFetchChangedTransactionsSince_PageComplete(SA_PARAM_NN_VALID BillingTransaction *pTrans)
{
	VindiciaXMLtoObjResult *pResult = NULL;
	struct trn__fetchDeltaSinceResponse *pResponse = NULL;
	btFetchDelta_Data *pData = pTrans->userData;

	PERFINFO_AUTO_START_FUNC();

	// Get transaction fetch response.
	pResult = vindiciaCreateResponse(pTrans, VINDICIA_TYPE(trn, fetchDeltaSinceResponse));
	if (!pResult)
	{
		btFail(pTrans, "Could not get response from Vindicia.");
		goto error;
	}
	btFreeObjResult(pTrans, pResult);
	pResponse = pResult->pObj;
	BILLING_DEBUG_RESPONSE("trn__fetchDeltaSinceResponse", pResponse);

	if (pResponse->_return_->returnCode != VINDICIA_SUCCESS_CODE)
	{
		btHandleUnknownVindiciaResponse(pTrans, pResponse->_return_->returnCode, pResponse->_return_->returnString);
		goto error;
	}

	if (pResponse->_transactions && pResponse->_transactions->__size)
	{
		int iCurTrans = 0;

		BILLING_DEBUG("Fetched %d transactions.\n", pResponse->_transactions->__size);

		for (iCurTrans = 0; iCurTrans < pResponse->_transactions->__size; iCurTrans++)
		{
			struct vin__Transaction *pVinTrans = pResponse->_transactions->__ptr[iCurTrans];
			AccountTransactionInfo *pInfo = NULL;

			if (!devassert(pVinTrans)) continue;

			if (btVinTransMeetsFilters(pVinTrans, pData->filters))
			{
				pInfo = StructCreate(parse_AccountTransactionInfo);

				CopyTransactionFromVindicia(pVinTrans, pInfo);

				eaPush(&pData->eaTransactions, pInfo);
			}
		}

		if (TRANS_DELTA_PAGE_SIZE == pResponse->_transactions->__size)
		{
			// There might be more to get
			btFetchChangedTransactionsSince_Page(pTrans);
			PERFINFO_AUTO_STOP_FUNC();
			return;
		}
	}

	if (devassert(pData->callback))
	{
		PERFINFO_AUTO_START("Success Callback", 1);
		pData->callback(pTrans, true, pData->eaTransactions, pData->userData);
		PERFINFO_AUTO_STOP();
	}

	PERFINFO_AUTO_STOP_FUNC();

	return;

error:
	eaDestroyStruct(&pData->eaTransactions, parse_AccountTransactionInfo);

	if (devassert(pData->callback))
	{
		PERFINFO_AUTO_START("Failure Callback", 1);
		pData->callback(pTrans, false, NULL, pData->userData);
		PERFINFO_AUTO_STOP();
	}

	PERFINFO_AUTO_STOP_FUNC();
}

static void btFetchChangedTransactionsSince_Page(SA_PARAM_NN_VALID BillingTransaction *pTrans)
{
	static const char *pVinMethod = VINDICIA_CALL_NAME(trn, fetchDeltaSince);
	btFetchDelta_Data *pData = pTrans->userData;
	struct trn__fetchDeltaSince request = {0};
	char *xmlRequest = NULL;

	PERFINFO_AUTO_START_FUNC();

	BILLING_DEBUG("Fetching page %d of transactions.\n", pData->uCurPage);

	request._auth = getVindiciaAuth();
	request._page = pData->uCurPage++;
	request._pageSize = TRANS_DELTA_PAGE_SIZE;
	request._timestamp = timeMakeLocalTimeFromSecondsSince2000(pData->uTimeSS2000);
	request._endTimestamp = timeMakeLocalTimeFromSecondsSince2000(pData->uTimeSS2000End);

	if (vindiciaObjtoXML(&xmlRequest, &request, VO2X_OBJ(trn, fetchDeltaSince)))
	{
		btContinue(pTrans, pVinMethod, xmlRequest, btFetchChangedTransactionsSince_PageComplete, pData);
	}
	else
	{
		RequestFailureAssert(pVinMethod);
	}

	estrDestroy(&xmlRequest);

	PERFINFO_AUTO_STOP_FUNC();
}

BillingTransaction *
btFetchChangedTransactionsSince(U32 uTimeSS2000,
								U32 uTimeSS2000End,
								TransactionFilterFlags filters,
								BillingTransaction *pTrans,
								btFetchAccountTransactionsCallback pCallback,
								void *pUserData)
{
	btFetchDelta_Data *pData = NULL;

	if (!verify(uTimeSS2000 > 0)) return pTrans;
	if (!verify(pCallback)) return pTrans;

	PERFINFO_AUTO_START_FUNC();
	BILLING_DEBUG_START;

	if (!pTrans)
	{
		pTrans = btCreateBlank(true);
	}
	pData = btAlloc(pTrans, pData, btFetchDelta_Data);
	pData->callback = pCallback;
	pData->userData = pUserData;
	pData->uCurPage = 0;
	pData->uTimeSS2000 = uTimeSS2000;
	pData->uTimeSS2000End = uTimeSS2000End;
	pData->filters = filters;
	pTrans->userData = pData;

	btFetchChangedTransactionsSince_Page(pTrans);

	PERFINFO_AUTO_STOP_FUNC();

	return pTrans;
}

#include "transactionQuery_h_ast.c"
