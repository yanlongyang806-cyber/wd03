#include "AccountLog.h"
#include "AccountManagement.h"
#include "AutoBill/UpdateActiveSubscriptions.h"
#include "billingRefund.h"
#include "AutoBill/CancelSubscription.h"
#include "error.h"
#include "Money.h"
#include "Subscription.h"
#include "StringUtil.h"
#include "Transaction/transactionQuery.h"

// Billing transaction data for btRefund.
typedef struct btRefund_Data
{
	// Tracking
	btRefundCallback callback;					// Completion callback
	void *userData;								// Pointer to pass to callback
	bool bSelfComplete;							// True if the billing transaction should be btComplete()ed

	// Request information
	AccountInfo *account;						// Account associated with this refund
	const char *transaction;					// Identifier associated with the transaction to be refunded.
	const char *amount;							// Amount to refund
	bool bRefundWithVindicia;					// True if a refund should be performed, false if refund should only be reported
	bool bMerchantInititated;					// True if this refund request was initiated by the merchant, false if by the customer
	const char *pOptionalSubVid;				// If present, mark the refund as associated with this subscription VID.
	bool pOptionalSubInstant;					// If pOptionalSubVid is present, immediately disentitle the autobill associated with the subscription.

	// Discovered information
	const char *currency;
	const char *VID;
	const char *MTID;
	SubscriptionStatus subStatus;				// If pOptionalSubVid is valid, this is the status of the subscription.
} btRefund_Data;

// Verify successful cancellation.
static void VerifyCancel(bool success, SA_PARAM_OP_VALID BillingTransaction *pTrans, SA_PARAM_OP_VALID void *pUserData)
{
	if (!success)
		ErrorOrAlert("ACCOUNTSERVER_REFUND_CANCELSUBFAIL", "Unable to cancel a subscription following subscription transaction refund");
}

// Update subscriptions for this refund.
static void btRefund_UpdateSubscriptions(SA_PARAM_NN_VALID btRefund_Data *pData)
{
	PERFINFO_AUTO_START_FUNC();
	if (pData->pOptionalSubVid)
	{
		accountRecordSubscriptionRefund(pData->account, pData->pOptionalSubVid);
		if (pData->subStatus == SUBSCRIPTIONSTATUS_ACTIVE)
			btCancelSub(pData->account, pData->pOptionalSubVid, pData->pOptionalSubInstant, pData->bMerchantInititated, NULL, VerifyCancel, NULL);
	}
	PERFINFO_AUTO_STOP();
}

// Check the result of the refund request.
static void btRefund_RefundComplete(SA_PARAM_NN_VALID BillingTransaction *pTrans)
{
	btRefund_Data *data = pTrans->userData;
	VindiciaXMLtoObjResult *pResult;
	struct rfd__performResponse *pResponse;

	// Get refund response.
	PERFINFO_AUTO_START_FUNC();
	pResult = vindiciaCreateResponse(pTrans, VINDICIA_TYPE(rfd, performResponse));
	if (!pResult)
	{
		PERFINFO_AUTO_STOP();
		return;
	}
	btFreeObjResult(pTrans, pResult);
	pResponse = pResult->pObj;
	BILLING_DEBUG_RESPONSE("rfd__performResponse", pResponse);

	// Handle result.
	if (pResponse->_return_->returnCode == VINDICIA_PARTIAL_CONTENT || pResponse->_return_->returnCode == VINDICIA_BAD_REQUEST_CODE
		|| pResponse->_return_->returnCode == VINDICIA_NOT_FOUND_CODE)
	{
		// Problem issuing refund.
		if (pResponse->_refunds->__size != 1 && pResponse->_refunds->__size != 0)
			AssertOrAlert("ACCOUNTSERVER_VINDICIA_REFUND_COUNT", "Incorrect number of refunds processed by Vindicia: %d", pResponse->_refunds->__size);
		PERFINFO_AUTO_START("Failure Callback", 1);
		if (pResponse->_refunds->__size != 1)
			data->callback(pTrans, false, pResponse->_return_->returnString, NULL, NULL, data->userData);
		else
			data->callback(pTrans, false, pResponse->_refunds->__ptr[0]->note, NULL, NULL, data->userData);
		PERFINFO_AUTO_STOP();
		PERFINFO_AUTO_STOP();
		return;
	}
	else if (pResponse->_return_->returnCode != VINDICIA_SUCCESS_CODE)
	{
		AssertOrAlert("ACCOUNTSERVER_VINDICIA_RESPONSE", "Received unknown response %d from Vindicia in %s: %s", (int)pResponse->_return_->returnCode,
			__FUNCTION__, pResponse->_return_->returnString);
		btFail(pTrans, "Unknown response from Vindicia");
		PERFINFO_AUTO_STOP();
		return;
	}

	// Update subscriptions for this refund.
	btRefund_UpdateSubscriptions(data);

	// Record result.
	accountLog(data->account, "Refunded %s %s for transaction %s (%s)",
		data->amount, data->currency, data->VID, data->MTID);
	PERFINFO_AUTO_START("Success Callback", 1);
	data->callback(pTrans, true, NULL, data->amount, data->currency, data->userData);
	PERFINFO_AUTO_STOP();
	PERFINFO_AUTO_STOP();
}

// Check the result of the refund report request.
static void btRefund_ReportComplete(SA_PARAM_NN_VALID BillingTransaction *pTrans)
{
	btRefund_Data *data = pTrans->userData;
	VindiciaXMLtoObjResult *pResult;
	struct rfd__reportResponse *pResponse;

	// Get refund response.
	PERFINFO_AUTO_START_FUNC();
	pResult = vindiciaCreateResponse(pTrans, VINDICIA_TYPE(rfd, reportResponse));
	if (!pResult)
	{
		PERFINFO_AUTO_STOP();
		return;
	}
	btFreeObjResult(pTrans, pResult);
	pResponse = pResult->pObj;
	BILLING_DEBUG_RESPONSE("rfd__reportResponse", pResponse);

	// Handle result.
	if (pResponse->_return_->returnCode == VINDICIA_BAD_REQUEST_CODE)
	{
		// Problem issuing refund.
		PERFINFO_AUTO_START("Failure Callback", 1);
		data->callback(pTrans, false, pResponse->_return_->returnString, NULL, NULL, data->userData);
		PERFINFO_AUTO_STOP();
		PERFINFO_AUTO_STOP();
		return;
	}
	else if (pResponse->_return_->returnCode != VINDICIA_SUCCESS_CODE)
	{
		AssertOrAlert("ACCOUNTSERVER_VINDICIA_RESPONSE", "Received unknown response %d from Vindicia in %s: %s", (int)pResponse->_return_->returnCode,
			__FUNCTION__, pResponse->_return_->returnString);
		btFail(pTrans, "Unknown response from Vindicia");
		PERFINFO_AUTO_STOP();
		return;
	}

	// Update subscriptions for this refund.
	btRefund_UpdateSubscriptions(data);

	// Record result.
	accountLog(data->account, "Recorded refund %s %s for transaction %s (%s)",
		data->amount, data->currency, data->VID, data->MTID);
	PERFINFO_AUTO_START("Success Callback", 1);
	data->callback(pTrans, true, NULL, data->amount, data->currency, data->userData);
	PERFINFO_AUTO_STOP();
	PERFINFO_AUTO_STOP();
}

// Perform a refund.
static void btRefund_Refund(SA_PARAM_NN_VALID BillingTransaction *pTrans, btRefund_Data *data, SA_PARAM_NN_VALID AccountTransactionInfo *info)
{
	char *xmlRequest = NULL;
	bool result;
	Money m;

	// Make sure that this is the proper account.
	PERFINFO_AUTO_START_FUNC();
	if (stricmp_safe(btGetMerchantAccountID(pTrans, data->account), info->merchantAccountId))
	{
		PERFINFO_AUTO_START("Account Mismatch", 1);
		data->callback(pTrans, false, "Provided account does not match requested transaction", NULL, NULL, data->userData);
		PERFINFO_AUTO_STOP();
		PERFINFO_AUTO_STOP();
		return;
	}

	// Make sure the refund amount is in an acceptable format.
	moneyInitFromStr(&m, data->amount, info->currency);
	if (moneyInvalid(&m))
	{
		PERFINFO_AUTO_START("Bad Money Format", 1);
		data->callback(pTrans, false, "The refund amount is not in the correct format.", NULL, NULL, data->userData);
		PERFINFO_AUTO_STOP();
		PERFINFO_AUTO_STOP();
		return;
	}

	// Make sure this subscription is reasonable to refund.
	if (data->pOptionalSubVid)
	{
		const CachedAccountSubscription *cachedSub = findAccountSubscriptionByVID(data->account, data->pOptionalSubVid);
		const SubscriptionContainer *subscription;

		// Make sure the account has a subscription of this type.
		if (!cachedSub)
		{
			PERFINFO_AUTO_START("No Sub", 1);
			data->callback(pTrans, false, "This account does not have this subscription.", NULL, NULL, data->userData);
			PERFINFO_AUTO_STOP();
			PERFINFO_AUTO_STOP();
			return;
		}
		data->subStatus = getCachedSubscriptionStatus(cachedSub);

		// Make sure that this subscription is associated with this transaction.
		subscription = findSubscriptionByID(cachedSub->uSubscriptionID);
		if (!subscription || stricmp_safe(subscription->pInternalName, info->merchantAffiliateId))
		{
			PERFINFO_AUTO_START("Unrelated Sub", 1);
			data->callback(pTrans, false, "This subscription is not associated with this transaction.", NULL, NULL, data->userData);
			PERFINFO_AUTO_STOP();
			PERFINFO_AUTO_STOP();
			return;
		}
	}

	// Save some data.
	data->currency = info->currency;
	data->VID = info->VID;
	data->MTID = info->merchantTransactionId;

	// Process the refund.
	if (data->bRefundWithVindicia)
	{
		struct rfd__perform *pRequest;
		ADD_MISC_COUNT(1, "Refund");
		pRequest = callocStruct(struct rfd__perform);
		pRequest->_auth = getVindiciaAuth();
		pRequest->_refunds = callocStruct(struct ArrayOfRefunds);
		pRequest->_refunds->__size = 1;
		pRequest->_refunds->__ptr = callocStruct(struct vin__Refund *);
		pRequest->_refunds->__ptr[0] = callocStruct(struct vin__Refund);
		pRequest->_refunds->__ptr[0]->transaction = callocStruct(struct vin__Transaction);
		pRequest->_refunds->__ptr[0]->transaction->VID = info->VID;
		pRequest->_refunds->__ptr[0]->amount = (char *)data->amount;
		pRequest->_refunds->__ptr[0]->currency = (char *)info->currency;
		result = vindiciaObjtoXML(&xmlRequest, pRequest, VO2X_OBJ(rfd, perform));
		devassert(result);
		free(pRequest->_refunds->__ptr[0]->transaction);
		free(pRequest->_refunds->__ptr[0]);
		free(pRequest->_refunds->__ptr);
		free(pRequest->_refunds);
		free(pRequest);
		btContinue(pTrans, "rfd:perform", xmlRequest, btRefund_RefundComplete, data);
	}
	else
	{
		struct rfd__report *pRequest;
		ADD_MISC_COUNT(1, "Report");
		pRequest = callocStruct(struct rfd__report);
		pRequest->_auth = getVindiciaAuth();
		pRequest->_refunds = callocStruct(struct ArrayOfRefunds);
		pRequest->_refunds->__size = 1;
		pRequest->_refunds->__ptr = callocStruct(struct vin__Refund *);
		pRequest->_refunds->__ptr[0] = callocStruct(struct vin__Refund);
		pRequest->_refunds->__ptr[0]->transaction = callocStruct(struct vin__Transaction);
		pRequest->_refunds->__ptr[0]->transaction->VID = info->VID;
		pRequest->_refunds->__ptr[0]->amount = (char *)data->amount;
		pRequest->_refunds->__ptr[0]->currency = (char *)info->currency;
		result = vindiciaObjtoXML(&xmlRequest, pRequest, VO2X_OBJ(rfd, report));
		devassert(result);
		free(pRequest->_refunds->__ptr[0]->transaction);
		free(pRequest->_refunds->__ptr[0]);
		free(pRequest->_refunds->__ptr);
		free(pRequest->_refunds);
		free(pRequest);
		btContinue(pTrans, "rfd:report", xmlRequest, btRefund_ReportComplete, data);
	}
	estrDestroy(&xmlRequest);
	PERFINFO_AUTO_STOP();
}

// Refund transaction if MTID lookup succeeded.
static void btRefund_FetchByMTID(SA_PARAM_NN_VALID BillingTransaction *pTrans, bool bKnownAccount,
								SA_PARAM_OP_VALID AccountTransactionInfo *info, SA_PARAM_OP_VALID void *userData)
{
	btRefund_Data *data = userData;
	devassert(data);
	PERFINFO_AUTO_START_FUNC();
	if (bKnownAccount)
		btRefund_Refund(pTrans, data, info);
	else
	{
		PERFINFO_AUTO_START("Unknown Account", 1);
		data->callback(pTrans, false, "Lookup failed", NULL, NULL, data->userData);
		PERFINFO_AUTO_STOP();
	}
	PERFINFO_AUTO_STOP();
}

// Refund transaction if VID lookup succeeded.
static void btRefund_FetchByVID(SA_PARAM_NN_VALID BillingTransaction *pTrans, bool bKnownAccount,
												  SA_PARAM_OP_VALID AccountTransactionInfo *info, SA_PARAM_OP_VALID void *userData)
{
	btRefund_Data *data = userData;
	devassert(data);
	PERFINFO_AUTO_START_FUNC();
	if (bKnownAccount)
		btRefund_Refund(pTrans, data, info);
	else
		btFetchTransactionByMTID(data->transaction, pTrans, btRefund_FetchByMTID, data);
	PERFINFO_AUTO_STOP();
}

// Make sure this is a valid subscription.
static void btRefund_UpdateActiveSubs(bool bSuccess, SA_PARAM_OP_VALID BillingTransaction *pTrans, SA_PARAM_OP_VALID void *pUserData)
{
	btRefund_Data *data = pUserData;
	devassert(data);

	PERFINFO_AUTO_START_FUNC();

	// Make sure updating worked.
	if (!bSuccess)
	{
		PERFINFO_AUTO_START("Update Failed", 1);
		data->callback(pTrans, false, "Updating subscriptions for this account failed.", NULL, NULL, data->userData);
		PERFINFO_AUTO_STOP();
		PERFINFO_AUTO_STOP();
		return;
	}

	// Proceed with looking up transaction VID.
	btFetchTransactionByVID(data->transaction, pTrans, btRefund_FetchByVID, data);
	PERFINFO_AUTO_STOP();
}

// Refund a transaction for a particular account.
// Start off by trying to look up the VID.
BillingTransaction *btRefund(AccountInfo *account, SA_PARAM_NN_STR const char *transaction, SA_PARAM_NN_STR const char *amount,
							 bool bRefundWithVindicia, bool bMerchantInitiated, SA_PARAM_OP_STR const char *pOptionalSubVid,
							 bool pOptionalSubImmediate, SA_PARAM_OP_VALID BillingTransaction *pTrans, btRefundCallback callback,
							 SA_PARAM_OP_VALID void *userData)
{
	btRefund_Data *data;
	bool bSelfComplete = false;
	BillingTransaction *ret = NULL;

	// Refuse to attempt refunds on accounts that are not billing-enabled.
	if (!account->bBillingEnabled)
		return NULL;

	PERFINFO_AUTO_START_FUNC();
	BILLING_DEBUG_START;

	// Set up transaction and save refund information.
	devassert(account && transaction && *transaction);
	if (!pTrans)
	{
		pTrans = btCreateBlank(true);
		bSelfComplete = true;
	}
	data = btAlloc(pTrans, data, btRefund_Data);
	data->callback = callback;
	data->userData = userData;
	data->account = account;
	data->transaction = btStrdup(pTrans, transaction);
	data->amount = btStrdup(pTrans, amount);
	data->bRefundWithVindicia = bRefundWithVindicia;
	data->bMerchantInititated = bMerchantInitiated;
	data->pOptionalSubVid = pOptionalSubVid && *pOptionalSubVid ? btStrdup(pTrans, pOptionalSubVid) : NULL;
	data->pOptionalSubInstant = pOptionalSubImmediate;
	data->bSelfComplete = bSelfComplete;

	// If a subscription VID is provided, update subscriptions first.
	if (pOptionalSubVid && *pOptionalSubVid)
	{
		ret = btUpdateActiveSubscriptions(account->uID, btRefund_UpdateActiveSubs, data, pTrans);
		PERFINFO_AUTO_STOP();
		return ret;
	}

	// Try to fetch this transaction by VID.
	ret = btFetchTransactionByVID(transaction, pTrans, btRefund_FetchByVID, data);
	PERFINFO_AUTO_STOP();
	return ret;
}
