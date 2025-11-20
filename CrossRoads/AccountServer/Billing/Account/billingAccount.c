#include "billingAccount.h"
#include "objContainerIO.h"
#include "vindicia.h"
#include "estring.h"
#include "AccountManagement.h"
#include "billingAccount_c_ast.h"
#include "UpdatePaymentMethod.h"
#include "error.h"
#include "StringUtil.h"

// -------------------------------------------------------------------------------------------
// Update Account

typedef struct UpdateAccountData
{
	// Things you may want to fill in when you create one of these
	U32 uAccountID;

} UpdateAccountData;

// -------------------------------------------------------------------------------------------

static void btAccountPush_Complete(BillingTransaction *pTrans)
{
	VindiciaXMLtoObjResult *pResult = NULL;
	
	PERFINFO_AUTO_START_FUNC();
	pResult = vindiciaCreateResponse(pTrans, VINDICIA_TYPE(acc, updateResponse));

	if(pResult)
	{
		struct acc__updateResponse *pResponse = pResult->pObj;
		BILLING_DEBUG_RESPONSE("acc__update", pResponse);
		btFreeObjResult(pTrans, pResult);
	}
	PERFINFO_AUTO_STOP();
}

void btPopulatePaymentMethodCache(SA_PARAM_NN_VALID const AccountInfo *pAccountInfo,
								  SA_PARAM_OP_VALID struct ArrayOfPaymentMethods *pVPaymentMethodArray)
{
	EARRAY_OF(CachedPaymentMethod) eaPaymentMethods = NULL;
	int numBefore;
	int numAfter;

	PERFINFO_AUTO_START_FUNC();

	if (pVPaymentMethodArray) {
		int i;

		for (i = 0; i < pVPaymentMethodArray->__size; i++)
		{
			struct vin__PaymentMethod *pPaymentMethod = pVPaymentMethodArray->__ptr[i];
			NOCONST(CachedPaymentMethod) *pCachedPaymentMethod = StructCreateNoConst(parse_CachedPaymentMethod);

			estrCopy2(&pCachedPaymentMethod->accountName, pPaymentMethod->accountHolderName);
			estrCopy2(&pCachedPaymentMethod->description, pPaymentMethod->customerDescription);

			if (pPaymentMethod->type)
			{
				enum vin__PaymentMethodType eVinType = *pPaymentMethod->type;

				switch (eVinType)
				{
					// Credit card
					xcase vin__PaymentMethodType__CreditCard:
						pCachedPaymentMethod->creditCard = StructCreateNoConst(parse_CachedCreditCard);

						if (pPaymentMethod->creditCard)
						{
							estrCopy2(&pCachedPaymentMethod->creditCard->expireDate, pPaymentMethod->creditCard->expirationDate);
							estrCopy2(&pCachedPaymentMethod->creditCard->bin, pPaymentMethod->creditCard->bin);
							estrCopy2(&pCachedPaymentMethod->creditCard->lastDigits, pPaymentMethod->creditCard->lastDigits);
							pCachedPaymentMethod->creditCard->numDigits = *pPaymentMethod->creditCard->accountLength;
						}

					// PayPal
					xcase vin__PaymentMethodType__PayPal:
						pCachedPaymentMethod->payPal = StructCreateNoConst(parse_CachedPayPal);

						if (pPaymentMethod->paypal)
						{
							estrCopy2(&pCachedPaymentMethod->payPal->emailAddress, pPaymentMethod->paypal->emailAddress);
						}

					// Direct debit
					xcase vin__PaymentMethodType__DirectDebit:
						pCachedPaymentMethod->directDebit = StructCreateNoConst(parse_CachedDirectDebit);

						if (pPaymentMethod->directDebit)
						{
							estrCopy2(&pCachedPaymentMethod->directDebit->account, pPaymentMethod->directDebit->account);
							estrCopy2(&pCachedPaymentMethod->directDebit->bankSortCode, pPaymentMethod->directDebit->bankSortCode);
							estrCopy2(&pCachedPaymentMethod->directDebit->ribCode, pPaymentMethod->directDebit->ribCode);
						}

					xdefault:
						// Unsupported type
						break;
				}
			}

			estrCopy2(&pCachedPaymentMethod->type, pPaymentMethod->customerSpecifiedType);
			estrCopy2(&pCachedPaymentMethod->VID, pPaymentMethod->VID);
			estrCopy2(&pCachedPaymentMethod->currency, pPaymentMethod->currency);

			if (pPaymentMethod->billingAddress)
			{
				estrCopy2(&pCachedPaymentMethod->billingAddress.address1, pPaymentMethod->billingAddress->addr1);
				estrCopy2(&pCachedPaymentMethod->billingAddress.address2, pPaymentMethod->billingAddress->addr2);
				estrCopy2(&pCachedPaymentMethod->billingAddress.city, pPaymentMethod->billingAddress->city);
				estrCopy2(&pCachedPaymentMethod->billingAddress.country, pPaymentMethod->billingAddress->country);
				estrCopy2(&pCachedPaymentMethod->billingAddress.district, pPaymentMethod->billingAddress->district);
				estrCopy2(&pCachedPaymentMethod->billingAddress.phone, pPaymentMethod->billingAddress->phone);
				estrCopy2(&pCachedPaymentMethod->billingAddress.postalCode, pPaymentMethod->billingAddress->postalCode);
			}

			eaPush(&eaPaymentMethods, CONTAINER_RECONST(CachedPaymentMethod, pCachedPaymentMethod));
		}
	}

	numBefore = pAccountInfo->personalInfo.ppPaymentMethods ? eaSize(&pAccountInfo->personalInfo.ppPaymentMethods) : 0;
	numAfter = eaPaymentMethods ? eaSize(&eaPaymentMethods) : 0;

	if (abs(numBefore - numAfter) > 1)
	{
		// Disabled because PayPal payment methods don't show up right away which can cause this to trigger falsely
		//AssertOrAlert("ACCOUNTSERVER_CACHEDPAYMENTMETHODS", "The account's payment methods have changed in number by %d! This probably shouldn't happen (+/- 1 is normal).", numAfter - numBefore);
	}

	updatePaymentMethodCache(pAccountInfo->uID, eaPaymentMethods);
	PERFINFO_AUTO_STOP();
}

bool btPopulateVinAccountFromAccount(SA_PARAM_NN_VALID BillingTransaction *pTrans, 
                                     SA_PRE_NN_NN_VALID struct vin__Account **ppVinAccount,
									 U32 uAccountID)
{
	const AccountInfo *pAccountInfo = findAccountByID(uAccountID);
	struct vin__Account* pVinAcc = *ppVinAccount;
	struct vin__Address* pVinAddress = NULL;

	PERFINFO_AUTO_START_FUNC();

	if (!ppVinAccount)
	{
		AssertOrAlert("ACCOUNTSERVER_POPULATEACCOUNT", "Expected an account object from Vindicia but didn't get one!");
		PERFINFO_AUTO_STOP();
		return false;
	}

	if (!pAccountInfo)
	{
		PERFINFO_AUTO_STOP();
		return false;
	}

	if(pVinAcc && pVinAcc->VID && *pVinAcc->VID)
	{
		pVinAddress = (*ppVinAccount)->shippingAddress;
		if(!pVinAddress)
		{
			pVinAddress = btAlloc(pTrans, pVinAddress, struct vin__Address);
			pVinAcc->shippingAddress = pVinAddress;
		}
	}
	else
	{
		pVinAcc     = btAlloc(pTrans, pVinAcc, struct vin__Account);
		pVinAddress = btAlloc(pTrans, pVinAddress, struct vin__Address);

		*ppVinAccount = pVinAcc;
		pVinAcc->shippingAddress = pVinAddress;
	}

	pVinAcc->merchantAccountId = btGetMerchantAccountID(pTrans, pAccountInfo);

	pVinAcc->name = btSPrintf(pTrans, "%s %s", NULL_TO_EMPTY(pAccountInfo->personalInfo.firstName), NULL_TO_EMPTY(pAccountInfo->personalInfo.lastName));

	pVinAcc->emailAddress   = (char*)NULL_TO_EMPTY(pAccountInfo->personalInfo.email);
	pVinAddress->addr1      = (char*)NULL_TO_EMPTY(pAccountInfo->personalInfo.shippingAddress.address1);
	pVinAddress->addr2      = (char*)NULL_TO_EMPTY(pAccountInfo->personalInfo.shippingAddress.address2);
	pVinAddress->city       = (char*)NULL_TO_EMPTY(pAccountInfo->personalInfo.shippingAddress.city);
	pVinAddress->district   = (char*)NULL_TO_EMPTY(pAccountInfo->personalInfo.shippingAddress.district);
	pVinAddress->country    = (char*)NULL_TO_EMPTY(pAccountInfo->personalInfo.shippingAddress.country);
	pVinAddress->postalCode = (char*)NULL_TO_EMPTY(pAccountInfo->personalInfo.shippingAddress.postalCode);
	pVinAddress->phone      = (char*)NULL_TO_EMPTY(pAccountInfo->personalInfo.shippingAddress.phone);

	btPopulatePaymentMethodCache(pAccountInfo, pVinAcc->paymentMethods);

	PERFINFO_AUTO_STOP();
	return true;
}

static void btAccountPush_FetchComplete(BillingTransaction *pTrans)
{
	bool bContinuing = false;
	VindiciaXMLtoObjResult *pResult = NULL;
	UpdateAccountData *pData = pTrans->userData;

	PERFINFO_AUTO_START_FUNC();
	pResult = vindiciaCreateResponse(pTrans, VINDICIA_TYPE(acc, fetchByMerchantAccountIdResponse));

	if(pResult)
	{
		struct acc__fetchByMerchantAccountIdResponse *pResponse = pResult->pObj;
		BILLING_DEBUG_RESPONSE("acc__fetchByMerchantAccountId", pResponse);
		btFreeObjResult(pTrans, pResult);

		if(btPopulateVinAccountResponseFromAccount(pTrans, pResponse, pData->uAccountID))
		{
			struct acc__update * pAccUpdate  = btAlloc(pTrans, pAccUpdate, struct acc__update);
			char *xml = NULL;

			pAccUpdate->_auth = getVindiciaAuth();
			pAccUpdate->_account = pResponse->_account;

			if(vindiciaObjtoXML(&xml, pAccUpdate, VO2X_OBJ(acc, update)))
			{
				btContinue(pTrans, "acc:update", xml, btAccountPush_Complete, pData);
				bContinuing = true;
			}

			estrDestroy(&xml);
		}
	}
	PERFINFO_AUTO_STOP();
}

void btAccountPush(AccountInfo *pAccount)
{
	char *xml = NULL;
	struct acc__fetchByMerchantAccountId *p = callocStruct(struct acc__fetchByMerchantAccountId);

	PERFINFO_AUTO_START_FUNC();
	accountSetBillingEnabled(pAccount);
	BILLING_DEBUG_START;

	p->_auth = getVindiciaAuth();
	p->_merchantAccountId = getMerchantAccountID(pAccount);

	if(vindiciaObjtoXML(&xml, p, VO2X_OBJ(acc, fetchByMerchantAccountId)))
	{
		BillingTransaction *pTrans = btCreate("acc:fetchByMerchantAccountId", xml, btAccountPush_FetchComplete, NULL, false);
		UpdateAccountData *pData   = btAllocUserData(pTrans, sizeof(struct UpdateAccountData));
		pData->uAccountID = pAccount->uID;
	}

	estrDestroy(&xml);
	estrDestroy(&p->_merchantAccountId);
	free(p);
	PERFINFO_AUTO_STOP();
}


/************************************************************************/
/* Update payment method                                                */
/************************************************************************/

AUTO_STRUCT;
typedef struct UpdatePaymentMethodData
{
	const AccountInfo *pAccountInfo;			NO_AST
	PaymentMethod * pPaymentMethodInfo;
	UpdatePaymentMethodCallback pCallback;		NO_AST
	void *pUserData;							NO_AST
	bool bCompleteTrans;
} UpdatePaymentMethodData;

// Determines if a payment method matches a cached payment method
static bool PaymentMethodsMatch(SA_PARAM_NN_VALID const PaymentMethod *pUpdate, SA_PARAM_NN_VALID const CachedPaymentMethod *pCache)
{
	if (pUpdate->VID && pUpdate->VID[0])
	{
		if (strcmp(pCache->VID, pUpdate->VID)) return false;
		return true;
	}
	else if (pUpdate->creditCard &&
			 pUpdate->creditCard->account &&
			 pUpdate->creditCard->account[0] &&
			 strlen(pUpdate->creditCard->account) > 10 &&
			 pCache->creditCard)
	{
		// Credit Card
		if (pCache->creditCard->bin && pUpdate->creditCard->account && strncmp(pCache->creditCard->bin, pUpdate->creditCard->account, strlen(pCache->creditCard->bin))) return false;
		if (pCache->creditCard->lastDigits && pUpdate->creditCard->account && strcmp(pCache->creditCard->lastDigits, pUpdate->creditCard->account + strlen(pUpdate->creditCard->account) - strlen(pCache->creditCard->lastDigits))) return false;
		if (pCache->accountName && pUpdate->accountHolderName && strcmpi(pCache->accountName, pUpdate->accountHolderName)) return false;
		if (pCache->creditCard->expireDate && pUpdate->creditCard->expirationDate && strcmp(pCache->creditCard->expireDate, pUpdate->creditCard->expirationDate)) return false;
		if (pCache->creditCard->expireDate && !pUpdate->creditCard->expirationDate) return false;
		if (!pCache->creditCard->expireDate && pUpdate->creditCard->expirationDate) return false;
		if (pCache->creditCard->bin && !pUpdate->creditCard->account) return false;
		if (!pCache->creditCard->bin && pUpdate->creditCard->account) return false;
		return true;
	}
	else if (pUpdate->payPal && pCache->payPal)
	{
		// PayPal
		if (stricmp_safe(pUpdate->payPal->emailAddress, pCache->payPal->emailAddress)) return false;
		return true;
	}
	return false;
}

// Find a cached payment method that matches the provided one or NULL if none are found
SA_RET_OP_VALID static const CachedPaymentMethod *
FindCachedPaymentMethod(SA_PARAM_NN_VALID const AccountInfo *pAccountInfo,
						SA_PARAM_NN_VALID const PaymentMethod *pPaymentMethod)
{
	PERFINFO_AUTO_START_FUNC();
	EARRAY_CONST_FOREACH_BEGIN(pAccountInfo->personalInfo.ppPaymentMethods, i, s);
		const CachedPaymentMethod *cachedPM = pAccountInfo->personalInfo.ppPaymentMethods[i];

		if (PaymentMethodsMatch(pPaymentMethod, cachedPM))
		{
			PERFINFO_AUTO_STOP();
			return cachedPM;
		}

	EARRAY_FOREACH_END;

	PERFINFO_AUTO_STOP();
	return NULL;
}

// Calls the payment method callback if one exists
static bool CallUpdatePMCallback(bool bSuccess,
								 SA_PARAM_OP_VALID BillingTransaction *pTrans,
								 SA_PARAM_OP_VALID const CachedPaymentMethod *pCache,
								 SA_PARAM_NN_VALID const UpdatePaymentMethodData *pData)
{
	PERFINFO_AUTO_START_FUNC();
	if (pData->pCallback)
	{
		pData->pCallback(bSuccess, pTrans, pCache, pData->pUserData);
		PERFINFO_AUTO_STOP();
		return true;
	}

	PERFINFO_AUTO_STOP();
	return false;
}

// Respond to a failure
static void RespondToFail(SA_PARAM_OP_VALID BillingTransaction *pTrans,
						  SA_PARAM_OP_VALID UpdatePaymentMethodData *pUpdateData,
						  FORMAT_STR const char *pFailureMessageFormat,
						  ...)
{
	va_list ap;
	char *pFullFailureString = NULL;

	if (!pTrans)
		return;

	PERFINFO_AUTO_START_FUNC();
	va_start(ap, pFailureMessageFormat);
		estrConcatfv(&pFullFailureString, pFailureMessageFormat, ap);
	va_end(ap);

	btFail(pTrans, "%s", pFullFailureString);

	if (!pUpdateData)
		return;

	CallUpdatePMCallback(false, NULL, NULL, pUpdateData);
	PERFINFO_AUTO_STOP();
}

static void btAccountUpdatePaymentMethod_AccountFetched(BillingTransaction *pTrans)
{
	VindiciaXMLtoObjResult *pResult = NULL;
	UpdatePaymentMethodData *pData = pTrans->userData;
	struct acc__fetchByMerchantAccountIdResponse *pResponse;

	PERFINFO_AUTO_START_FUNC();
	pResult = vindiciaCreateResponse(pTrans, VINDICIA_TYPE(acc, fetchByMerchantAccountIdResponse));

	if(!pResult)
	{
		RespondToFail(pTrans, pData, "Failed to receive result from Vindicia.");
		PERFINFO_AUTO_STOP();
		return;
	}

	pResponse = pResult->pObj;
	BILLING_DEBUG_RESPONSE("acc__fetchByMerchantAccountId", pResponse);
	btFreeObjResult(pTrans, pResult);

	// Make sure that it was successful.
	if (pResponse->_return_->returnCode != VINDICIA_SUCCESS_CODE)
	{
		RespondToFail(pTrans, pData, "Failed to fetch account.");
		PERFINFO_AUTO_STOP();
		return;
	}

	// Synch the account data.
	btPopulateVinAccountResponseFromAccount(pTrans, pResponse, pData->pAccountInfo->uID);

	CallUpdatePMCallback(true, pTrans, FindCachedPaymentMethod(pData->pAccountInfo, pData->pPaymentMethodInfo), pData);
	PERFINFO_AUTO_STOP();
}

static void btAccountUpdatePaymentMethod_Complete(BillingTransaction *pTrans)
{
	VindiciaXMLtoObjResult *pResult = NULL;
	UpdatePaymentMethodData *pData = pTrans->userData;
	struct acc__updatePaymentMethodResponse *pResponse;

	PERFINFO_AUTO_START_FUNC();
	pResult = vindiciaCreateResponse(pTrans, VINDICIA_TYPE(acc, updatePaymentMethodResponse));

	if(!pResult)
	{
		RespondToFail(pTrans, pData, "Failed to receive result from Vindicia.");
		PERFINFO_AUTO_STOP();
		return;
	}

	pResponse = pResult->pObj;
	BILLING_DEBUG_RESPONSE("acc__updatePaymentMethod", pResponse);
	btFreeObjResult(pTrans, pResult);

	// Make sure that it was successful.
	if (pResponse->_return_->returnCode != VINDICIA_SUCCESS_CODE)
	{
		RespondToFail(pTrans, pData, "Failed [%d]: %s", pResponse->_return_->returnCode, pResponse->_return_->returnString);
		PERFINFO_AUTO_STOP();
		return;
	}

	// Fetch the account one last time because the response to this method may still include an inactive
	// payment method.
	if (!btFetchAccountStep(pData->pAccountInfo->uID, pTrans, btAccountUpdatePaymentMethod_AccountFetched, pData))
	{
		RespondToFail(pTrans, pData, "Could not fetch account.");
	}
	PERFINFO_AUTO_STOP();
}

static void btAccountUpdatePaymentMethod_FetchComplete(BillingTransaction *pTrans);

static void btAccountUpdatePaymentMethod_Pushed(BillingTransaction *pTrans)
{
	VindiciaXMLtoObjResult *pResult = NULL;
	UpdatePaymentMethodData *pData = pTrans->userData;
	struct acc__updateResponse *pResponse;

	PERFINFO_AUTO_START_FUNC();
	pResult = vindiciaCreateResponse(pTrans, VINDICIA_TYPE(acc, updateResponse));

	if(!pResult)
	{
		RespondToFail(pTrans, pData, "Failed to receive result from Vindicia.");
		PERFINFO_AUTO_STOP();
		return;
	}

	pResponse = pResult->pObj;
	BILLING_DEBUG_RESPONSE("acc__update", pResponse);
	btFreeObjResult(pTrans, pResult);

	// Make sure that it was successful.
	if (pResponse->_return_->returnCode != VINDICIA_SUCCESS_CODE)
	{
		RespondToFail(pTrans, pData, "Failed [%d]: %s", pResponse->_return_->returnCode, pResponse->_return_->returnString);
		PERFINFO_AUTO_STOP();
		return;
	}

	if (!btFetchAccountStep(pData->pAccountInfo->uID, pTrans, btAccountUpdatePaymentMethod_FetchComplete, pData))
	{
		RespondToFail(pTrans, pData, "Could not fetch account.");
	}
	PERFINFO_AUTO_STOP();
}

static void btAccountUpdatePaymentMethod_FetchComplete(BillingTransaction *pTrans)
{
	VindiciaXMLtoObjResult *pResult = NULL;
	UpdatePaymentMethodData *pData = pTrans->userData;
	struct acc__fetchByMerchantAccountIdResponse *pResponse;
	struct acc__updatePaymentMethod * pAccUpdate;
	char *xml = NULL;
	const PaymentMethod *pUpdate;

	PERFINFO_AUTO_START_FUNC();
	pResult = vindiciaCreateResponse(pTrans, VINDICIA_TYPE(acc, fetchByMerchantAccountIdResponse));

	if(!pResult)
	{
		RespondToFail(pTrans, pData, "Failed to receive result from Vindicia.");
		PERFINFO_AUTO_STOP();
		return;
	}

	pResponse = pResult->pObj;
	BILLING_DEBUG_RESPONSE("acc__fetchByMerchantAccountId", pResponse);
	btFreeObjResult(pTrans, pResult);

	// Make sure that it was successful.
	if (pResponse->_return_->returnCode != VINDICIA_SUCCESS_CODE)
	{
		RespondToFail(pTrans, pData, "Failed [%d]: %s", pResponse->_return_->returnCode, pResponse->_return_->returnString);
		PERFINFO_AUTO_STOP();
		return;
	}

	// Populate the account's cache with what we got back from Vindicia and populate the account object
	// with what we have
	if(!btPopulateVinAccountResponseFromAccount(pTrans, pResponse, pData->pAccountInfo->uID))
	{
		RespondToFail(pTrans, pData, "Could not populate the account cache from the Vindicia response.");
		PERFINFO_AUTO_STOP();
		return;
	}

	// Make sure Vindicia knows about the account
	if (!pResponse->_account->VID || !*pResponse->_account->VID)
	{
		// Account doesn't exist, push it.
		if (!btPushAccount(pTrans, pResponse->_account, btAccountUpdatePaymentMethod_Pushed, pData))
		{
			RespondToFail(pTrans, pData, "Could not push account to Vindicia.");
			PERFINFO_AUTO_STOP();
			return;
		}
	}

	pAccUpdate = btAlloc(pTrans, pAccUpdate, struct acc__updatePaymentMethod);
	pUpdate = pData->pPaymentMethodInfo;

	// Make sure the payment method is valid
	if (pUpdate->VID && pUpdate->VID[0] && !isValidPaymentMethodVID(findAccountByID(pData->pAccountInfo->uID), pUpdate->VID))
	{
		RespondToFail(pTrans, pData, "Invalid payment method VID.");
		PERFINFO_AUTO_STOP();
		return;
	}

	pAccUpdate->_auth = getVindiciaAuth();
	pAccUpdate->_account = pResponse->_account;
	pAccUpdate->_numCycles = 0;
	pAccUpdate->_paymentMethod = btCreateVindiciaPaymentMethod(pData->pAccountInfo, pTrans, pUpdate, true, NULL);

	// The following is false so that we don't update *ALL* autobills on the account, even when they aren't
	// using the same payment method
	pAccUpdate->_replaceOnAllAutoBills = xsd__boolean__false_;

	pAccUpdate->_updateBehavior = pUpdate->active ? vin__PaymentUpdateBehavior__Validate : vin__PaymentUpdateBehavior__Update;

	if(vindiciaObjtoXML(&xml, pAccUpdate, VO2X_OBJ(acc, updatePaymentMethod)))
		btContinue(pTrans, "acc:updatePaymentMethod", xml, btAccountUpdatePaymentMethod_Complete, pData);
	else
		RespondToFail(pTrans, pData, "Could not send update payment method request to Vindicia.");

	estrDestroy(&xml);
	PERFINFO_AUTO_STOP();
}

SA_RET_OP_VALID BillingTransaction *
btAccountUpdatePaymentMethod(SA_PARAM_NN_VALID AccountInfo *pAccount,
							 SA_PARAM_NN_VALID const PaymentMethod *pPaymentMethod,
							 SA_PARAM_OP_VALID BillingTransaction *pTrans,
							 SA_PARAM_OP_VALID UpdatePaymentMethodCallback pCallback,
							 SA_PARAM_OP_VALID void *userData)
{
	UpdatePaymentMethodData *data;
	bool made = false;

	PERFINFO_AUTO_START_FUNC();

	// Create the billing transaction if one wasn't provided
	if (!pTrans)
	{
		pTrans = btCreateBlank(true);
		made = true;
	}

	data = btStructCreate(pTrans, parse_UpdatePaymentMethodData);

	data->pPaymentMethodInfo = StructClone(parse_PaymentMethod, pPaymentMethod);
	data->pAccountInfo = pAccount;
	data->pCallback = pCallback;
	data->pUserData = userData;
	data->bCompleteTrans = made;

	accountSetBillingEnabled(pAccount);

	BILLING_DEBUG_START;

	if (!btFetchAccountStep(pAccount->uID, pTrans, btAccountUpdatePaymentMethod_FetchComplete, data))
	{
		RespondToFail(pTrans, data, "Could not fetch account.");
		PERFINFO_AUTO_STOP();
		return NULL;
	}

	PERFINFO_AUTO_STOP();
	return pTrans;
}


/************************************************************************/
/* Utility functions                                                    */
/************************************************************************/

bool btFetchAccountStep(U32 uAccountID, SA_PARAM_NN_VALID BillingTransaction *pTrans, SA_PARAM_NN_VALID BillingTransactionCompleteCB callback, SA_PARAM_OP_VALID void *userData)
{
	char *xml = NULL;
	struct acc__fetchByMerchantAccountId *p = NULL;
	bool ret = false;
	const AccountInfo *pAccount = NULL;

	PERFINFO_AUTO_START_FUNC();

	pAccount = findAccountByID(uAccountID);
	if (!pAccount)
	{
		PERFINFO_AUTO_STOP();
		return false;
	}

	p = callocStruct(struct acc__fetchByMerchantAccountId);

	// Record account ID associated with this transaction for debugging purposes.
	pTrans->uDebugAccountId = uAccountID;

	p->_auth = getVindiciaAuth();
	p->_merchantAccountId = btGetMerchantAccountID(pTrans, pAccount);

	ret = vindiciaObjtoXML(&xml, p, VO2X_OBJ(acc, fetchByMerchantAccountId));
	if(ret)
	{
		btContinue(pTrans, "acc:fetchByMerchantAccountId", xml, callback, userData);
	}

	estrDestroy(&xml);
	free(p);
	PERFINFO_AUTO_STOP();
	return ret;
}

// Push an account to Vindicia
bool btPushAccount(SA_PARAM_NN_VALID BillingTransaction *pTrans,
				   SA_PARAM_NN_VALID struct vin__Account *pVinAccount,
				   SA_PARAM_NN_VALID BillingTransactionCompleteCB callback,
				   SA_PARAM_OP_VALID void *userData)
{
	struct acc__update *pAccUpdate = NULL;
	char *xml = NULL;
	bool ret;

	PERFINFO_AUTO_START_FUNC();
	pAccUpdate  = callocStruct(struct acc__update);
	pAccUpdate->_auth = getVindiciaAuth();
	pAccUpdate->_account = pVinAccount;

	ret = vindiciaObjtoXML(&xml, pAccUpdate, VO2X_OBJ(acc, update));
	if (ret)
	{
		btContinue(pTrans, "acc:update", xml, callback, userData);
	}

	estrDestroy(&xml);
	free(pAccUpdate);
	PERFINFO_AUTO_STOP();
	return ret;
}

#include "AutoGen/billingAccount_c_ast.c"