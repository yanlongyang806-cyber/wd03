#include "ActivateSubscription.h"
#include "AccountLog.h"
#include "AccountManagement.h"
#include "ProductKey.h"
#include "objContainer.h"
#include "Subscription.h"
#include "Account/billingAccount.h"
#include "timing.h"
#include "InternalSubs.h"
#include "Product.h"
#include "UpdateActiveSubscriptions.h"
#include "AccountServer.h"
#include "UpdatePaymentMethod.h"
#include "ActivateSubscription_c_ast.h"
#include "StringUtil.h"
#include "GrantExtraDays.h"

// Whether or not to validate payment methods other than PayPal
static bool gbValidateNonPayPalPM = true;
AUTO_CMD_INT(gbValidateNonPayPalPM, ValidateNonPayPalPM) ACMD_CMDLINE ACMD_CATEGORY(Account_Server_Billing);

// Whether or not to validate PayPal payment methods
static bool gbValidatePayPalPM = true;
AUTO_CMD_INT(gbValidatePayPalPM, ValidatePayPalPM) ACMD_CMDLINE ACMD_CATEGORY(Account_Server_Billing);

// Whether or not to subtract 1 day from game cards (to account for the duration)
static bool gbGameCardDaysMod = true;
AUTO_CMD_INT(gbGameCardDaysMod, GameCardDaysMod) ACMD_CMDLINE ACMD_CATEGORY(Account_Server_Billing);

static void CallbackOrComplete(BillingTransaction *pTrans)
{
	PERFINFO_AUTO_START_FUNC();
	if (pTrans)
	{
		AutoBillTransactionData *pData = pTrans->userData;

		if (pData->pCallback)
		{
			pData->pCallback(pTrans->result != BTR_FAILURE, pTrans, pData->pUserData);
			return;
		}
	}
	PERFINFO_AUTO_STOP();
}

static bool hasActiveSubscriptionByName(AccountInfo *account, const char *pSubInternalName)
{
	CachedAccountSubscription *pCachedSub = findAccountSubscriptionByInternalName(account, pSubInternalName);
	if(pCachedSub && getCachedSubscriptionStatus(pCachedSub) == SUBSCRIPTIONSTATUS_ACTIVE)
	{
		return true;
	}

	return false;
}

static bool shouldUpdateCacheFirst(SA_PARAM_NN_VALID const AccountInfo *account, SA_PARAM_NN_STR const char *pSubInternalName)
{
	CachedAccountSubscription *pCachedSub = findAccountSubscriptionByInternalName(account, pSubInternalName);

	if (!pCachedSub) return false;

	if (pCachedSub->nextBillingDateSS2000 < timeSecondsSince2000())
		return true;

	return false;
}

void btActivateSubscription_Finished(bool success,
									 SA_PARAM_OP_VALID BillingTransaction *pTrans,
									 SA_PARAM_OP_VALID AutoBillTransactionData *pData)
{
	if (!verify(pData)) return;
	if (!verify(pTrans)) return;

	PERFINFO_AUTO_START_FUNC();
	if (success)
	{
		pTrans->userData = pData;
		CallbackOrComplete(pTrans);
	}
	else
	{
		PERFINFO_AUTO_START("Failure Callback", 1);
		pData->pCallback(false, pTrans, pData->pUserData);
		PERFINFO_AUTO_STOP();
	}
	PERFINFO_AUTO_STOP();
}

static void btActivateSubscription_Complete(SA_PARAM_NN_VALID BillingTransaction *pTrans)
{
	VindiciaXMLtoObjResult *pResult = NULL;
	AutoBillTransactionData *pData = NULL;
	struct abl__updateResponse *pResponse = NULL;
	AccountInfo *pAccount = NULL;
	char *pError = NULL;
	const char *pErrorKey = NULL;
	const SubscriptionContainer *pSubscription = NULL;
	bool bAssertOnError = true;

	if (!verify(pTrans)) return;

	PERFINFO_AUTO_START_FUNC();

	pResult = vindiciaCreateResponse(pTrans, VINDICIA_TYPE(abl, updateResponse));
	pData = pTrans->userData;

	if (!devassert(pData))
	{
		estrStackCreate(&pError);
		estrPrintf(&pError, "Missing data in " __FUNCTION__);
		pErrorKey = "ACCOUNTSERVER_BILLING_AUTOBILL_MISSING_DATA";
		goto error;
	}

	pAccount = findAccountByID(pData->uAccountID);
	pSubscription = findSubscriptionByName(pData->pSubscriptionName);

	if (!devassert(pAccount && pSubscription))
	{
		estrStackCreate(&pError);
		estrPrintf(&pError, "Missing account or subscription data in " __FUNCTION__);
		pErrorKey = "ACCOUNTSERVER_BILLING_AUTOBILL_MISSING_DATA";
		goto error;
	}

	if (!pResult)
	{
		estrStackCreate(&pError);
		estrPrintf(&pError, "No response to AutoBill.Update request");
		pErrorKey = "ACCOUNTSERVER_BILLING_AUTOBILL_RESPONSE_MISSING";
		goto error;
	}

	pResponse = pResult->pObj;
	BILLING_DEBUG_RESPONSE("abl__update", pResponse);
	btFreeObjResult(pTrans, pResult);

	if(pResponse->_return_->returnCode != VINDICIA_SUCCESS_CODE)
	{
		estrStackCreate(&pError);
		if (pResponse->_return_->returnCode == VINDICIA_VALIDATION_FAILED)
		{
			if (pResponse->_authStatus && pResponse->_authStatus->payPalStatus)
			{
				estrPrintf(&pError, "AutoBill.Update request's return code is 402 for PayPal. This may mean PayPal isn't working.  Please contact Vindicia.");
				pErrorKey = "ACCOUNTSERVER_BILLING_AUTOBILL_PAYPAL_VALIDATION";
			}
			else
			{
				accountLog(pAccount, "Payment method validation failed, subscription not activated: [subscription:%s]", pData->pSubscriptionName);

				estrPrintf(&pError, "Payment method validation failed.");
				bAssertOnError = false;
			}
		}
		else
		{
			estrPrintf(&pError, "AutoBill.Update request's return code is not success: %d", pResponse->_return_->returnCode);
			pErrorKey = "ACCOUNTSERVER_BILLING_AUTOBILL_BAD_RESPONSE";
		}
		goto error;
	}
	
	accountLog(pAccount, "Subscription activated: [subscription:%s]", pData->pSubscriptionName);

	btSetVindiciaTransStatus(pTrans, pResponse->_authStatus);

	if (pResponse->_autobill && pResponse->_autobill->account && pResponse->_autobill->account->paymentMethods)
	{
		btPopulatePaymentMethodCache(pAccount, pResponse->_autobill->account->paymentMethods);
	}

	// If it is a PayPal transaction... (this was set by the above call)
	if (pTrans->payPalStatus)
	{
		// Add pending action to update cached subscriptions
		pTrans->uPendingActionID = accountAddPendingRefreshSubCache(pAccount, pResponse->_autobill->VID);
	}

	if (pData->uGameCardDays && !pTrans->payPalStatus)
	{
		btSetDate(pAccount, pResponse->_autobill->VID, pData->uStartTime, pTrans, true, btActivateSubscription_Finished, pData);
	}
	else
	{
		btUpdateActiveSubscriptionsContinue(pTrans);
	}

	devassert(!pError && !pErrorKey);

	PERFINFO_AUTO_STOP_FUNC();
	return;

error:
	if (devassert(pError))
	{
		btFail(pTrans, pError);
		BILLING_DEBUG_WARNING("%s\n", pError);
		
		if (bAssertOnError && devassert(pErrorKey))
		{
			AssertOrAlert(pErrorKey, "%s", pError);
		}

		estrDestroy(&pError);
	}

	if (pAccount && pSubscription)
	{
		accountRemoveExpectedSub(pAccount, pSubscription->pInternalName);
	}

	CallbackOrComplete(pTrans);
	PERFINFO_AUTO_STOP_FUNC();
}

__forceinline static bool shouldUseCachedSubTime(SA_PARAM_NN_VALID const CachedAccountSubscription *pCachedSub)
{
	if (!pCachedSub) return false;

	if (getCachedSubscriptionStatus(pCachedSub) == SUBSCRIPTIONSTATUS_ACTIVE ||
		getCachedSubscriptionStatus(pCachedSub) == SUBSCRIPTIONSTATUS_SUSPENDED ||
		getCachedSubscriptionStatus(pCachedSub) == SUBSCRIPTIONSTATUS_CANCELLED ||
		getCachedSubscriptionStatus(pCachedSub) == SUBSCRIPTIONSTATUS_PENDINGCUSTOMER)
	{
		return true;
	}

	return false;
}

// Determine the start date for a subscription
static U32 determineSubStartSS2000(SA_PARAM_NN_VALID const AccountInfo *pAccountInfo,
								   SA_PARAM_NN_VALID const SubscriptionContainer *pSubscription,
								   U32 uGameCardDays)
{
	U32 uSecondsSince2000Start = 0;
	const InternalSubscription *pInternalSub = findInternalSub(pAccountInfo->uID, pSubscription->pInternalName);
	CachedAccountSubscription *pCachedSub =
		findAccountSubscriptionByInternalName(pAccountInfo, pSubscription->pInternalName);
	U32 uNow = timeSecondsSince2000();

	// If they have a cached sub that should contribute time, get it
	if (pCachedSub && shouldUseCachedSubTime(pCachedSub))
	{
		uSecondsSince2000Start = MAX(pCachedSub->nextBillingDateSS2000, pCachedSub->startTimeSS2000);
		uSecondsSince2000Start = MAX(uSecondsSince2000Start, uNow);
	}

	// Check internal subscriptions
	if (pInternalSub && pInternalSub->uExpiration > uSecondsSince2000Start)
	{
		uSecondsSince2000Start = pInternalSub->uExpiration;
	}

	if(!uSecondsSince2000Start)
	{
		// If we haven't decided on a start time yet, they've never had a subscription of
		// this type before, and are therefore eligible for the subscription's free days.

		uSecondsSince2000Start = uNow + SECONDS_PER_DAY * pSubscription->iInitialFreeDays;
	}

	if (uGameCardDays > 0)
	{
		// Minus one day, duration should be one day if game card
		uSecondsSince2000Start += (pSubscription->gameCard && gbGameCardDaysMod ? uGameCardDays - 1 : uGameCardDays) * SECONDS_PER_DAY;
	}

	return uSecondsSince2000Start;
}

static void btActivateSubscription_AccountFetchComplete(BillingTransaction *pTrans)
{
	bool bContinuing = false;
	VindiciaXMLtoObjResult *pResult = NULL;
	AutoBillTransactionData *pData = pTrans->userData;

	struct AccountInfo *pAccountInfo = NULL;
	Container *pAccountContainer = objGetContainer(GLOBALTYPE_ACCOUNT, pData->uAccountID);
	const SubscriptionContainer *pSubscription = findSubscriptionByName(pData->pSubscriptionName);

	PERFINFO_AUTO_START_FUNC();
	pResult = vindiciaCreateResponse(pTrans, VINDICIA_TYPE(acc, fetchByMerchantAccountIdResponse));

	if(pResult && pAccountContainer && pSubscription && pSubscription->pProductName && *pSubscription->pProductName)
	{
		struct acc__fetchByMerchantAccountIdResponse *pResponse = pResult->pObj;
		BILLING_DEBUG_RESPONSE("acc__fetchByMerchantAccountId", pResponse);
		btFreeObjResult(pTrans, pResult);

		pAccountInfo = pAccountContainer->containerData;

		if(pAccountInfo && btPopulateVinAccountResponseFromAccount(pTrans, pResponse, pData->uAccountID))
		{
			// ---------------------------------------------------------------------------------------
			// Core SOAP objects
			struct vin__BillingPlan   *pBillingPlan   = btAlloc(pTrans, pBillingPlan, struct vin__BillingPlan);
			struct vin__Product       *pVinProduct    = btAlloc(pTrans, pVinProduct, struct vin__Product);
			struct vin__AutoBill      *pAutoBill      = btAlloc(pTrans, pAutoBill, struct vin__AutoBill);
			struct abl__update        *pAblUpdate     = btAlloc(pTrans, pAblUpdate, struct abl__update);

			struct vin__PaymentMethod *pPaymentMethod = NULL; // Optionally created later

			const CachedPaymentMethod *pCachedPM = NULL;
			const FraudSettings *pFraudSettings;

			// ---------------------------------------------------------------------------------------
			// Local Vars

			enum vin__PaymentMethodType ePaymentMethodType = vin__PaymentMethodType__CreditCard;
			SYSTEMTIME systemTimeData;
			int iBillingDay;

			timerSystemTimeFromSecondsSince2000(&systemTimeData,timeSecondsSince2000());
			iBillingDay = (int)systemTimeData.wDay;

			if (accountExpectsSub(pAccountInfo, pSubscription->pInternalName))
			{
				btFail(pTrans, "Already expecting sub with same internal name.");
				CallbackOrComplete(pTrans);
				PERFINFO_AUTO_STOP();
				return;
			}

			// ---------------------------------------------------------------------------------------
			// Fraud Settings

			if (pData->pPaymentMethod &&
				pData->pPaymentMethod->VID &&
				*pData->pPaymentMethod->VID)
			{
				pCachedPM = getCachedPaymentMethod(pAccountInfo, pData->pPaymentMethod->VID);
				if (!pCachedPM)
				{
					btFail(pTrans, "Could not find payment method.");
					CallbackOrComplete(pTrans);
					PERFINFO_AUTO_STOP();
					return;
				}
			}

			pFraudSettings = getFraudSettings(pCachedPM ? pCachedPM->billingAddress.country : NULL);
			if (!pFraudSettings)
			{
				btFail(pTrans, "Could not find fraud settings.");
				CallbackOrComplete(pTrans);
				PERFINFO_AUTO_STOP();
				return;
			}

			// ---------------------------------------------------------------------------------------
			// BillingPlan
			pBillingPlan->merchantBillingPlanId = btStrdupWithPrefix(pTrans, pData->pSubscriptionName);

			// ---------------------------------------------------------------------------------------
			// Product
			pVinProduct->merchantProductId = btStrdupWithPrefix(pTrans, pSubscription->pProductName);

			// ---------------------------------------------------------------------------------------
			// CreditCard and Payment Method

			pAblUpdate->_validatePaymentMethod = xsd__boolean__false_;
			if (!pSubscription->gameCard && devassert(pData->pPaymentMethod))
			{
				pPaymentMethod = btCreateVindiciaPaymentMethod(pAccountInfo, pTrans, pData->pPaymentMethod, false, pData->pBankName);
				if (devassert(pPaymentMethod))
				{
					CachedAccountSubscription *pCachedSub = findAccountSubscriptionByInternalName(pAccountInfo, pSubscription->pInternalName);
					if ((pPaymentMethod->paypal && gbValidatePayPalPM) ||
						(pCachedSub && gbValidateNonPayPalPM))
					{
						pAblUpdate->_validatePaymentMethod = xsd__boolean__true_;
					}
				}
			}

			// ---------------------------------------------------------------------------------------
			// AutoBill
			pAutoBill->account             = pResponse->_account; // Already populated, thanks btPopulateVinAccountResponseFromAccount()!
			pAutoBill->billingPlan         = pBillingPlan;
			pAutoBill->billingStatementIdentifier = btStrdup(pTrans, NULL_TO_EMPTY(pSubscription->pBillingStatementIdentifier));
			pAutoBill->currency            = btStrdup(pTrans, NULL_TO_EMPTY(pData->pCurrency));
			pAutoBill->merchantAffiliateId = btStrdup(pTrans, NULL_TO_EMPTY(pSubscription->pInternalName));
			pAutoBill->paymentMethod       = pPaymentMethod; // Might be NULL, this is OK
			pAutoBill->product             = pVinProduct;
			pAutoBill->sourceIp			   = btStrdup(pTrans, NULL_TO_EMPTY(pData->pIP));

			btSetInterestingNameValues(pTrans, &pAutoBill->nameValues, billingGetDivision(DT_Subscription, pSubscription->pInternalName),
				pData->pPaymentMethod ? pData->pPaymentMethod->country : NULL, pData->pBankName);

			// ---------------------------------------------------------------------------------------
			// Start Timestamp
			pAutoBill->startTimestamp = btAlloc(pTrans, pAutoBill->startTimestamp, time_t);
			pData->uStartTime = determineSubStartSS2000(pAccountInfo, pSubscription, pData->uGameCardDays);
			*pAutoBill->startTimestamp = timeMakeLocalTimeFromSecondsSince2000(pData->uStartTime);

			// ---------------------------------------------------------------------------------------
			// AutoBill::update()
			pAblUpdate->_auth                  = getVindiciaAuth();
			pAblUpdate->_autobill              = pAutoBill;
			pAblUpdate->_duplicateBehavior     = vin__DuplicateBehavior__SucceedIgnore;
			pAblUpdate->_minChargebackProbability = pFraudSettings->minChargebackProbability;

			// ---------------------------------------------------------------------------------------
			// Continue our transaction
			{
				char *xml = NULL;
				if(vindiciaObjtoXML(&xml, pAblUpdate, VO2X_OBJ(abl, update)))
				{
					accountAddExpectedSub(pAccountInfo, pSubscription->pInternalName);

					btContinue(pTrans, "abl:update", xml, btActivateSubscription_Complete, pData);
					bContinuing = true;
				}
				estrDestroy(&xml);
			}
		}
	}
	else
	{
		estrPrintf(&pTrans->resultString, "btActivateSubscription() failed to find all necessary objects.");
	}

	if(!bContinuing)
	{
		btFail(pTrans, "Could not update autobill.");
		CallbackOrComplete(pTrans);
	}
	PERFINFO_AUTO_STOP();
}

static void btActivateSubscriptionContinue_SubsUpdated(bool bSuccess,
													   SA_PARAM_OP_VALID BillingTransaction *pTrans,
													   SA_PARAM_OP_VALID void *pUserData)
{
	PERFINFO_AUTO_START_FUNC();
	if (bSuccess && pTrans)
	{
		AutoBillTransactionData *pData = pTrans->userData;
		AccountInfo *pAccountInfo = findAccountByID(pData->uAccountID);

		char *xml = NULL;
		struct acc__fetchByMerchantAccountId *p = callocStruct(struct acc__fetchByMerchantAccountId);

		pData->pCallback = pData->pBackupCallback;
		pData->pUserData = pData->pBackupUserData;
		pData->pBackupCallback = NULL;
		pData->pBackupUserData = NULL;

		// Start by looking up our account information. We're going to create it on the fly if it isn't up there.

		p->_auth = getVindiciaAuth();
		p->_merchantAccountId = btGetMerchantAccountID(pTrans, pAccountInfo);

		if(vindiciaObjtoXML(&xml, p, VO2X_OBJ(acc, fetchByMerchantAccountId)))
		{
			btContinue(pTrans, "acc:fetchByMerchantAccountId", xml, btActivateSubscription_AccountFetchComplete, pData);
		}

		estrDestroy(&xml);
		free(p);
	}
	PERFINFO_AUTO_STOP();
}

void btActivateSubscriptionContinue(BillingTransaction *pTrans)
{
	AutoBillTransactionData *pData = pTrans->userData;
	AccountInfo *pAccountInfo = findAccountByID(pData->uAccountID);
	const SubscriptionContainer *pSub = findSubscriptionByName(pData->pSubscriptionName);

	PERFINFO_AUTO_START_FUNC();
	BILLING_DEBUG_START;

	if(pAccountInfo && pSub)
	{
		ANALYSIS_ASSUME(pAccountInfo);
		accountSetBillingEnabled(pAccountInfo);

		if(hasActiveSubscriptionByName(pAccountInfo, pSub->pInternalName))
		{
			btFail(pTrans, "Account '%s' already has an active subscription with the internal name '%s'.", pAccountInfo->accountName, pSub->pInternalName);
			CallbackOrComplete(pTrans);
			PERFINFO_AUTO_STOP();
			return;
		}
		else
		{
			pData->pBackupCallback = pData->pCallback;
			pData->pBackupUserData = pData->pUserData;
			if (shouldUpdateCacheFirst(pAccountInfo, pSub->pInternalName))
			{
				pData->pCallback = btActivateSubscriptionContinue_SubsUpdated;
				pData->pUserData = NULL;
				btUpdateActiveSubscriptionsContinue(pTrans);
			}
			else
			{
				btActivateSubscriptionContinue_SubsUpdated(true, pTrans, NULL);
			}
		}
	}
	else
	{
		if(!pAccountInfo)
		{
			btFail(pTrans, "Couldn't find account information for id %d\n", pData->uAccountID);
			CallbackOrComplete(pTrans);
			PERFINFO_AUTO_STOP();
			return;
		}
		else
		{
			btFail(pTrans, "Couldn't find subscription for sub name '%s'\n", pData->pSubscriptionName);
			CallbackOrComplete(pTrans);
			PERFINFO_AUTO_STOP();
			return;
		}
	}
	PERFINFO_AUTO_STOP();
}


/************************************************************************/
/* Used for new super sub-create                                        */
/************************************************************************/

AUTO_STRUCT;
typedef struct ActivateSubSession
{
	BillingTransaction *pTrans;		NO_AST
	bool bComplete;
	ActivateSubCallback pCallback;	NO_AST
	void *pUserData;				NO_AST
} ActivateSubSession;

static void btActivateSub_Finish(SA_PARAM_NN_VALID ActivateSubSession *pActivateSub, SA_PARAM_OP_STR const char *pFailMsg)
{
	bool bSuccess = pFailMsg ? false : true;

	PERFINFO_AUTO_START_FUNC();
	if (!bSuccess)
	{
		btFail(pActivateSub->pTrans, "%s", pFailMsg);
	}

	if (pActivateSub->pCallback) pActivateSub->pCallback(bSuccess && pActivateSub->pTrans->result == BTR_SUCCESS, pActivateSub->pTrans, pActivateSub->pUserData);
	PERFINFO_AUTO_STOP();
}

static void btActivateSub_Updated(bool bSuccess,
								  SA_PARAM_OP_VALID BillingTransaction *pTrans,
								  SA_PARAM_NN_VALID ActivateSubSession *pActivateSub)
{
	btActivateSub_Finish(pActivateSub, NULL);
}

BillingTransaction *
btActivateSub(U32 uAccountID,
			  const char *pSubscriptionName, 
			  const char *pCurrency,
			  const PaymentMethod *pPaymentMethod,
			  const char *ip,
			  const char *pBankName,
			  unsigned int uExtraDays,
			  BillingTransaction *pTrans,
			  ActivateSubCallback pCallback,
			  void *pUserData)
{
	bool bComplete = false;
	ActivateSubSession *pActivateSub;
	AutoBillTransactionData *pData;

	PERFINFO_AUTO_START_FUNC();

	if (!pTrans)
	{
		bComplete = true;
		pTrans = btCreateBlank(true);
	}

	pActivateSub = btStructCreate(pTrans, parse_ActivateSubSession);
	pActivateSub->pTrans = pTrans;
	pActivateSub->bComplete = bComplete;
	pActivateSub->pCallback = pCallback;
	pActivateSub->pUserData = pUserData;

	pData = btStructCreate(pTrans, parse_AutoBillTransactionData);

	pData->uAccountID = uAccountID;
	estrCopy2(&pData->pSubscriptionName, pSubscriptionName);
	estrCopy2(&pData->pCurrency, pCurrency);
	estrCopy2(&pData->pIP, ip);
	pData->pBankName = pBankName ? estrDup(pBankName) : NULL;
	pData->pPaymentMethod = StructClone(parse_PaymentMethod, pPaymentMethod);
	pData->uGameCardDays = uExtraDays;
	pData->pCallback = btActivateSub_Updated;
	pData->pUserData = pActivateSub;

	pTrans->userData = pData;

	btActivateSubscriptionContinue(pTrans);

	PERFINFO_AUTO_STOP();
	return pTrans;
}

#include "ActivateSubscription_h_ast.c"
#include "ActivateSubscription_c_ast.c"