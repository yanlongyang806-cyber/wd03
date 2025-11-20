#include "AccountLog.h"
#include "AccountManagement.h"
#include "billing.h"
#include "error.h"
#include "StringUtil.h"
#include "transactionCancel.h"
#include "transactionQuery.h"

// Billing transaction data for btCancelTransaction.
typedef struct btCancel_Data
{
	// Tracking
	btCancelTransactionCallback pCallback;		// Completion callback
	void *pUserData;							// Pointer to pass to callback
	bool bSelfComplete;							// True if the billing transaction should be btComplete()ed

	// Request information
	AccountInfo *pAccount;						// Account associated with this request
	const char *pId;							// Identifier associated with the transaction

	// Discovered information
	const char *pVid;
	const char *pMtid;
} btCancel_Data;

// Check the result of the cancellation request.
static void btCancelTransaction_CancelComplete(SA_PARAM_NN_VALID BillingTransaction *pTrans)
{
	btCancel_Data *data = pTrans->userData;
	VindiciaXMLtoObjResult *pResult;
	struct trn__cancelResponse *pResponse;

	// Get cancel response.
	PERFINFO_AUTO_START_FUNC();
	pResult = vindiciaCreateResponse(pTrans, VINDICIA_TYPE(trn, cancelResponse));
	if (!pResult)
	{
		PERFINFO_AUTO_STOP();
		return;
	}
	btFreeObjResult(pTrans, pResult);
	pResponse = pResult->pObj;
	BILLING_DEBUG_RESPONSE("trn__cancelResponse", pResponse);
	if (pResponse->_return_->returnCode != VINDICIA_SUCCESS_CODE)
	{
		AssertOrAlert("ACCOUNTSERVER_VINDICIA_RESPONSE", "Received unknown response %d from Vindicia in %s: %s", (int)pResponse->_return_->returnCode,
			__FUNCTION__, pResponse->_return_->returnString);
		btFail(pTrans, "Unknown response from Vindicia");
		PERFINFO_AUTO_STOP();
		return;
	}

	// Check for success.
	if (pResponse->_results->__size != 1)
	{
		AssertOrAlert("ACCOUNTSERVER_VINDICIA_RESPONSE_SIZE", "Improper response size %d from Vindicia in %s", (int)pResponse->_results->__size,
			__FUNCTION__);
		btFail(pTrans, "Unknown response from Vindicia");
		PERFINFO_AUTO_STOP();
		return;
	}
	switch (pResponse->_results->__ptr[0]->returnCode)
	{
		case VINDICIA_SUCCESS_CODE:
			// Success.
			break;
		case VINDICIA_BAD_REQUEST_CODE:
			PERFINFO_AUTO_START("VINDICIA_BAD_REQUEST_CODE", 1);
			data->pCallback(pTrans, false, "This transaction is already captured.", data->pUserData);
			PERFINFO_AUTO_STOP();
			break;
		case VINDICIA_NOT_ALLOWED:
			PERFINFO_AUTO_START("VINDICIA_NOT_ALLOWED", 1);
			data->pCallback(pTrans, false, "This transaction has not been authorized, or has already been cancelled.", data->pUserData);
			PERFINFO_AUTO_STOP();
			break;
		case VINDICIA_NOT_FOUND_CODE:
			// This should never happen.
		default:
			AssertOrAlert("ACCOUNTSERVER_VINDICIA_RESPONSE", "Received unknown response %d from Vindicia in %s: %s",
				(int)pResponse->_return_->returnCode, __FUNCTION__, pResponse->_return_->returnString);
			btFail(pTrans, "Unknown response from Vindicia");
			PERFINFO_AUTO_STOP();
			return;
	}
	if (pResponse->_results->__ptr[0]->returnCode != VINDICIA_SUCCESS_CODE)
	{
		PERFINFO_AUTO_STOP();
		return;
	}

	// Report successful result.
	accountLog(data->pAccount, "Cancelled transaction %s (%s)", data->pVid, data->pMtid);
	PERFINFO_AUTO_START("VINDICIA_SUCCESS_CODE", 1);
	data->pCallback(pTrans, true, NULL, data->pUserData);
	PERFINFO_AUTO_STOP();
	PERFINFO_AUTO_STOP();
}

// Cancel a transaction.
static void btCancelTransaction_Cancel(SA_PARAM_NN_VALID BillingTransaction *pTrans, btCancel_Data *data, SA_PARAM_NN_VALID AccountTransactionInfo *info)
{
	char *xmlRequest = NULL;
	struct trn__cancel request;
	struct ArrayOfTransactions array;
	struct vin__Transaction transaction, *pTransaction;
	bool result;

	PERFINFO_AUTO_START_FUNC();

	// Make sure that this is the proper account.
	if (stricmp_safe(btGetMerchantAccountID(pTrans, data->pAccount), info->merchantAccountId))
	{
		PERFINFO_AUTO_START("Account Mismatch", 1);
		data->pCallback(pTrans, false, "Provided account does not match requested transaction", data->pUserData);
		PERFINFO_AUTO_STOP();
		return;
	}

	// Save some data.
	data->pVid = info->VID;
	data->pMtid = info->merchantTransactionId;

	// Process the cancellation.
	memset(&transaction, 0, sizeof(transaction));
	request._auth = getVindiciaAuth();
	request._transactions = &array;
	request._transactions->__size = 1;
	request._transactions->__ptr = &pTransaction;
	request._transactions->__ptr[0] = &transaction;
	request._transactions->__ptr[0]->VID = info->VID;
	estrStackCreate(&xmlRequest);
	result = vindiciaObjtoXML(&xmlRequest, &request, VO2X_OBJ(trn, cancel));
	devassert(result);
	btContinue(pTrans, "trn:cancel", xmlRequest, btCancelTransaction_CancelComplete, data);
	estrDestroy(&xmlRequest);
	PERFINFO_AUTO_STOP();
}

// Cancel transaction if MTID lookup succeeded.
static void btCancelTransaction_FetchByMTID(SA_PARAM_NN_VALID BillingTransaction *pTrans, bool bKnownAccount,
								 SA_PARAM_OP_VALID AccountTransactionInfo *pInfo, SA_PARAM_OP_VALID void *pUserData)
{
	btCancel_Data *data = pUserData;
	devassert(data);
	PERFINFO_AUTO_START_FUNC();
	if (bKnownAccount)
		btCancelTransaction_Cancel(pTrans, data, pInfo);
	else
	{
		PERFINFO_AUTO_START("Failure Callback", 1);
		data->pCallback(pTrans, false, "Transaction lookup failed", data->pUserData);
		PERFINFO_AUTO_STOP();
	}
	PERFINFO_AUTO_STOP();
}

// Cancel transaction if VID lookup succeeded.
static void btCancelTransaction_FetchByVID(SA_PARAM_NN_VALID BillingTransaction *pTrans, bool bKnownAccount,
								 SA_PARAM_OP_VALID AccountTransactionInfo *pInfo, SA_PARAM_OP_VALID void *pUserData)
{
	btCancel_Data *data = pUserData;
	devassert(data);
	PERFINFO_AUTO_START_FUNC();
	if (bKnownAccount)
		btCancelTransaction_Cancel(pTrans, data, pInfo);
	else
		btFetchTransactionByMTID(data->pId, pTrans, btCancelTransaction_FetchByMTID, data);
	PERFINFO_AUTO_STOP();
}

// Attempt to cancel a transaction that was not been captured.
SA_RET_NN_VALID BillingTransaction * btCancelTransaction(
	SA_PARAM_NN_VALID AccountInfo *pAccount,
	SA_PARAM_NN_STR const char *pId,
	SA_PARAM_OP_VALID BillingTransaction *pTrans,
	btCancelTransactionCallback pCallback,
	SA_PARAM_OP_VALID void *pUserData)
{
	btCancel_Data *data;
	bool bSelfComplete = false;
	BillingTransaction *ret = NULL;

	PERFINFO_AUTO_START_FUNC();
	BILLING_DEBUG_START;

	// Set up transaction.
	devassert(pAccount && pId && *pId);
	if (!pTrans)
	{
		pTrans = btCreateBlank(true);
		bSelfComplete = true;
	}
	data = btAlloc(pTrans, data, btCancel_Data);
	data->pCallback = pCallback;
	data->pUserData = pUserData;
	data->pAccount = pAccount;
	data->pId = btStrdup(pTrans, pId);
	data->bSelfComplete = bSelfComplete;

	// Try to fetch this transaction by VID.
	ret = btFetchTransactionByVID(pId, pTrans, btCancelTransaction_FetchByVID, data);
	PERFINFO_AUTO_STOP();
	return ret;
}
