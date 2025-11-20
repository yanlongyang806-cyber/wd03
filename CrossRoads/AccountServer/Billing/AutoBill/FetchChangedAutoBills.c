#include "FetchChangedAutoBills.h"
#include "billing.h"
#include "timing.h"
#include "AccountManagement.h"
#include "Subscription.h"
#include "GlobalData.h"
#include "StringUtil.h"
#include "UpdateActiveSubscriptions.h"

// Maximum number of errors before giving up
#define FETCH_AUTOBILL_MAX_ERRORS 10

/************************************************************************/
/* Types                                                                */
/************************************************************************/

typedef struct FetchAutoBillsSession
{
	unsigned int uPage;
	FetchDeltaStats stats;
	FetchAutoBillsSinceCallback pCallback;
	void *pUserData;
} FetchAutoBillsSession;

typedef struct RebillInfo
{
	U32 uAccountID;
	NOCONST(CachedAccountSubscription) *pSubscription;
} RebillInfo;


/************************************************************************/
/* Private functions                                                    */
/************************************************************************/

static bool FetchAutoBillsPage(SA_PARAM_NN_VALID FetchAutoBillsSession *pSession);

// Create an autobills session
SA_RET_NN_VALID static __forceinline FetchAutoBillsSession *CreateFetchAutoBillsSession(void)
{
	FetchAutoBillsSession *pSession;
	PERFINFO_AUTO_START_FUNC();
	pSession = callocStruct(FetchAutoBillsSession);
	PERFINFO_AUTO_STOP();
	return pSession;
}

// Destroy an autobills session
static __forceinline void DestroyFetchAutoBillsSession(SA_PRE_NN_NN_VALID FetchAutoBillsSession **pSession,
													   bool bSuccess)
{
	if (!*pSession)
		return;

	PERFINFO_AUTO_START_FUNC();

	if ((*pSession)->pCallback)
	{
		(*pSession)->pCallback(bSuccess, (*pSession)->pUserData);
	}

	free(*pSession);
	*pSession = NULL;
	PERFINFO_AUTO_STOP();
}

static void UpdateCachedSubBillingUpdatedCallback(SA_PARAM_NN_VALID AccountInfo *pAccount,
												  SA_PARAM_NN_VALID NOCONST(CachedAccountSubscription) *pCachedSub,
												  SA_PARAM_OP_VALID BillingTransaction *pTrans,
												  bool bSuccess,
												  SA_PARAM_OP_VALID UserData pUserData)
{
	if (!verify(pAccount)) return;
	if (!verify(pCachedSub)) return;

	PERFINFO_AUTO_START_FUNC();
	if (bSuccess)
	{
		accountUpdateCachedSubscription(pAccount, pCachedSub);
		StructDestroyNoConst(parse_CachedAccountSubscription, pCachedSub);
	}
	PERFINFO_AUTO_STOP();
}

// Handle a single autobill
static bool HandleAutoBill(SA_PARAM_NN_VALID struct vin__AutoBill *pAutoBill)
{
	AccountInfo *pAccount = findAccountByMerchantAccountID(pAutoBill->account->merchantAccountId);
	NOCONST(CachedAccountSubscription) *pCachedSub = NULL;

	// This could happen if it is a dev account etc.
	if (!pAccount)
		return false;

	PERFINFO_AUTO_START_FUNC();
	BILLING_DEBUG("Updating AutoBill [VID:%s] for %s... ", pAutoBill->VID, pAccount->accountName);

	pCachedSub = createCachedAccountSubscriptionFromAutoBill(pAccount, pAutoBill);

	if (pCachedSub)
	{
		if (ShouldUpdateBillingFlag(CONTAINER_RECONST(CachedAccountSubscription, pCachedSub)))
		{
			UpdateBillingFlag(pAccount, pCachedSub, NULL, UpdateCachedSubBillingUpdatedCallback, NULL);
		}
		else
		{
			accountUpdateCachedSubscription(pAccount, pCachedSub);
			StructDestroyNoConst(parse_CachedAccountSubscription, pCachedSub);
		}

		BILLING_DEBUG("Done.\n");
		PERFINFO_AUTO_STOP();
		return true;
	}
	else
	{
		BILLING_DEBUG("Done.\n");
		PERFINFO_AUTO_STOP();
		return false;
	}
	PERFINFO_AUTO_STOP();
}

// Handle fetch delta error and determine if we should continue or bail
static bool FetchDeltaError(SA_PARAM_NN_VALID FetchAutoBillsSession *pSession, SA_PARAM_NN_VALID BillingTransaction *pTrans, SA_PARAM_NN_STR const char *pError)
{
	PERFINFO_AUTO_START_FUNC();
	eaPush(&pSession->stats.Errors, estrDup(pError));
	if (eaSize(&pSession->stats.Errors) > FETCH_AUTOBILL_MAX_ERRORS)
	{
		char *pErrors = NULL;

		estrStackCreate(&pErrors);

		estrConcatf(&pErrors, "Too many errors for autobill fetch delta response!\n");
		EARRAY_CONST_FOREACH_BEGIN(pSession->stats.Errors, i, size);
			estrConcatf(&pErrors, "Error %d: %s\n", i, pSession->stats.Errors[i]);
		EARRAY_FOREACH_END;

		btFail(pTrans, "Too many errors!");
		AssertOrAlert("ACCOUNTSERVER_BILLING_FETCH_AUTOBILLS_ERRORS", "%s", pErrors);
		DestroyFetchAutoBillsSession(&pSession, false);

		estrDestroy(&pErrors);
		PERFINFO_AUTO_STOP();
		return false;
	}

	PERFINFO_AUTO_STOP();
	return true;
}

// Response from fetching a page of autobills
static void FetchAutoBillsPageResponse(SA_PARAM_NN_VALID BillingTransaction *pTrans)
{
	VindiciaXMLtoObjResult *pResult = NULL;
	FetchAutoBillsSession *pSession = pTrans->userData;
	int curAutoBill;
	struct abl__fetchDeltaSinceResponse *pResponse;

	PERFINFO_AUTO_START_FUNC();
	pResult = vindiciaCreateResponse(pTrans, VINDICIA_TYPE(abl, fetchDeltaSinceResponse));

	if (!pResult)
	{
		if (!FetchDeltaError(pSession, pTrans, "Response for autobill fetch delta page is missing."))
		{
			PERFINFO_AUTO_STOP();
			return;
		}

		FetchAutoBillsPage(pSession);
		PERFINFO_AUTO_STOP();
		return;
	}

	pResponse = pResult->pObj;
	BILLING_DEBUG_RESPONSE("abl__fetchDeltaSince", pResponse);
	btFreeObjResult(pTrans, pResult);

	if (pResponse->_return_->returnCode != VINDICIA_SUCCESS_CODE)
	{
		if (!FetchDeltaError(pSession, pTrans, STACK_SPRINTF("Response code for autobill fetch delta page is %d.", pResponse->_return_->returnCode)))
		{
			PERFINFO_AUTO_STOP();
			return;
		}

		FetchAutoBillsPage(pSession);
		PERFINFO_AUTO_STOP();
		return;
	}

	BILLING_DEBUG("Fetched %d AutoBills.  Processing...\n", pResponse->_autobills->__size);

	for (curAutoBill = 0; curAutoBill < pResponse->_autobills->__size; ++curAutoBill)
	{
		if (HandleAutoBill(pResponse->_autobills->__ptr[curAutoBill]))
		{
			pSession->stats.NumProcessed++;
		}
	}

	pSession->stats.NumTotal += pResponse->_autobills->__size;

	if (pResponse->_autobills->__size < pSession->stats.PageSize)
	{
		// We're done fetching changed auto-bills
		BILLING_DEBUG("Finished fetching AutoBills.\n");
		pSession->stats.FinishedTime = timeSecondsSince2000();
		billingLogFetchDeltaStats(FDT_AUTOBILLS, &pSession->stats);
		DestroyFetchAutoBillsSession(&pSession, true);
		PERFINFO_AUTO_STOP();
		return;
	}

	pSession->uPage++;
	FetchAutoBillsPage(pSession);
	PERFINFO_AUTO_STOP();
}

// Fetch a single page of autobills
static bool FetchAutoBillsPage(SA_PARAM_NN_VALID FetchAutoBillsSession *pSession)
{
	BillingTransaction *pTrans = btCreateBlank(false);
	struct abl__fetchDeltaSince *p;
	char *xml = NULL;
	bool bReturn = false;

	PERFINFO_AUTO_START_FUNC();
	BILLING_DEBUG("Fetching page %d of AutoBills...\n", pSession->uPage);

	// Create the request
	p = btAlloc(pTrans, p, struct abl__fetchDeltaSince);
	p->_auth = getVindiciaAuth();
	p->_timestamp = timeMakeLocalTimeFromSecondsSince2000(pSession->stats.Time);
	p->_endTimestamp = timeMakeLocalTimeFromSecondsSince2000(pSession->stats.StartedTime);
	p->_page = pSession->uPage;
	p->_pageSize = pSession->stats.PageSize;

	if(vindiciaObjtoXML(&xml, p, VO2X_OBJ(abl, fetchDeltaSince)))
	{
		btContinue(pTrans, "abl:fetchDeltaSince", xml, FetchAutoBillsPageResponse, pSession);
		bReturn = true;
	}
	else
	{
		btFail(pTrans, "Could not fetch AutoBills page.");
		DestroyFetchAutoBillsSession(&pSession, false);
	}

	estrDestroy(&xml);
	PERFINFO_AUTO_STOP();
	return bReturn;
}


/************************************************************************/
/* Public functions                                                     */
/************************************************************************/

void btFetchAutoBillsSince(U32 uSecondsSince2000, SA_PARAM_OP_VALID FetchAutoBillsSinceCallback pCallback, SA_PARAM_OP_VALID void *pUserData)
{
	FetchAutoBillsSession *pSession;

	PERFINFO_AUTO_START_FUNC();
	BILLING_DEBUG_START;

	BILLING_DEBUG("Fetching AutoBills since %s, %d at a time...\n",
		timeGetLocalDateStringFromSecondsSince2000(uSecondsSince2000),
		billingGetFetchDeltaAutobillPageSize());

	pSession = CreateFetchAutoBillsSession();
	pSession->stats.Time = uSecondsSince2000;
	pSession->stats.StartedTime = timeSecondsSince2000();
	pSession->stats.PageSize = billingGetFetchDeltaAutobillPageSize();
	pSession->pCallback = pCallback;
	pSession->pUserData = pUserData;
	FetchAutoBillsPage(pSession);
	PERFINFO_AUTO_STOP();
}