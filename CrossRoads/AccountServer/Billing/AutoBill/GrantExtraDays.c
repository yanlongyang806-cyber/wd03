#include "GrantExtraDays.h"
#include "AccountLog.h"
#include "AccountManagement.h"
#include "UpdateActiveSubscriptions.h"
#include "ActivateSubscription.h"
#include "AccountServer.h"
#include "GrantExtraDays_c_ast.h"
#include "timing.h"

/************************************************************************/
/* Globals                                                              */
/************************************************************************/

// Whether or not to set the autobill date when granting days instead of just giving them days
static bool gbGrantDaysUseDate = false;
AUTO_CMD_INT(gbGrantDaysUseDate, GrantDaysUseDate) ACMD_CMDLINE ACMD_CATEGORY(Account_Server_Billing);


/************************************************************************/
/* Types                                                                */
/************************************************************************/

// Stores data for our grant days attempt
AUTO_STRUCT;
typedef struct GrantDaysSession
{
	BillingTransaction *pTrans;		NO_AST			// Billing transaction
	bool bComplete;									// Whether or not to btComplete the billing transaction
	char *VID;						AST(ESTRING)	// VID we are extending
	AccountInfo *pAccount;			NO_AST			// Account to which the above VID belongs
	GrantDaysCallback pCallback;	NO_AST			// Callback for when we are done
	void *pUserData;				NO_AST			// Userdata for the callback
	int iDays;										// Days to give (positive) or take (negative) -- zero does nothing
	const CachedAccountSubscription *pSub; NO_AST	// Subscription being extended
	U32 uDate;
} GrantDaysSession;


/************************************************************************/
/* Private functions                                                    */
/************************************************************************/

// Does the callback if one exists and marks the transaction as failed if a failure message is provided
// Will also btComplete the transaction if it was created in this flow
static void FinishGrantDaysSession(SA_PARAM_NN_VALID GrantDaysSession *pGrantDays,
								   SA_PARAM_OP_STR const char *pFailMsg)
{
	bool bSuccess = pFailMsg ? false : true;

	PERFINFO_AUTO_START_FUNC();
	// Set the fail message if we were given one and it hasn't already been failed
	if (pFailMsg && pGrantDays->pTrans->result != BTR_FAILURE)
	{
		btFail(pGrantDays->pTrans, "%s", pFailMsg);
	}

	// Do the callback if one exists
	if (pGrantDays->pCallback) pGrantDays->pCallback(pGrantDays->pTrans->result == BTR_SUCCESS, pGrantDays->pTrans, pGrantDays->pUserData);
	PERFINFO_AUTO_STOP();
}

// Called after subscriptions have been updated for the user
static void SubscriptionsUpdated(bool bSuccess,
								 SA_PARAM_OP_VALID BillingTransaction *pTrans,
								 SA_PARAM_NN_VALID GrantDaysSession *pGrantDays)
{
	FinishGrantDaysSession(pGrantDays, NULL);
}

// Update the user's subscriptions
static void UpdateSubscriptions(SA_PARAM_NN_VALID BillingTransaction *pTrans)
{
	AutoBillTransactionData *pData;
	GrantDaysSession *pGrantDays = pTrans->userData;

	PERFINFO_AUTO_START_FUNC();
	pTrans->userData = btStructCreate(pTrans, parse_AutoBillTransactionData);
	pData = pTrans->userData;

	pData->pCallback = SubscriptionsUpdated;
	pData->pUserData = pGrantDays;
	pData->uAccountID = pGrantDays->pAccount->uID;

	btUpdateActiveSubscriptionsContinue(pTrans);
	PERFINFO_AUTO_STOP();
}

// Called after AutoBillGrantDays is finished
static void AutoBillGrantDaysComplete(SA_PARAM_NN_VALID BillingTransaction *pTrans)
{
	VindiciaXMLtoObjResult *pResult = NULL;
	GrantDaysSession *pGrantDays = pTrans->userData;
	struct abl__delayBillingByDaysResponse *pResponse;

	PERFINFO_AUTO_START_FUNC();
	pResult = vindiciaCreateResponse(pTrans, VINDICIA_TYPE(abl, delayBillingByDaysResponse));

	if(!pResult)
	{
		FinishGrantDaysSession(pGrantDays, "Could not get result from Vindicia.");
		PERFINFO_AUTO_STOP();
		return;
	}

	pResponse = pResult->pObj;
	BILLING_DEBUG_RESPONSE("abl__delayBillingByDays", pResponse);
	btFreeObjResult(pTrans, pResult);

	if(pResponse->_return_->returnCode != VINDICIA_SUCCESS_CODE)
	{
		FinishGrantDaysSession(pGrantDays, "Could not extend autobill in Vindicia.");
		PERFINFO_AUTO_STOP();
		return;
	}

	accountLog(pGrantDays->pAccount, "Subscription extended: [subscription:%s] (days %d)", pGrantDays->pSub->name, pGrantDays->iDays);

	UpdateSubscriptions(pTrans);
	PERFINFO_AUTO_STOP();
}

// Do autobill.grantdays
void AutoBillGrantDays(GrantDaysSession *pGrantDays)
{
	char *xml = NULL;
	struct abl__delayBillingByDays *p = NULL;

	PERFINFO_AUTO_START_FUNC();

	p = btAlloc(pGrantDays->pTrans, p, struct abl__delayBillingByDays);
	p->_auth			= getVindiciaAuth();
	p->_autobill		= btAlloc(pGrantDays->pTrans, p->_autobill, struct vin__AutoBill);
	p->_autobill->VID	= btStrdup(pGrantDays->pTrans, pGrantDays->VID);
	p->_delayDays		= pGrantDays->iDays;
	p->_movePermanently	= xsd__boolean__true_;

	if (vindiciaObjtoXML(&xml, p, VO2X_OBJ(abl, delayBillingByDays)))
	{
		btContinue(pGrantDays->pTrans, "abl:delayBillingByDays", xml, AutoBillGrantDaysComplete, pGrantDays);
	}
	else
	{
		FinishGrantDaysSession(pGrantDays, "Could not send delay request to Vindicia.");
	}

	estrDestroy(&xml);
	PERFINFO_AUTO_STOP();
}

// Called after AutoBillDelayToDate is finished
static void AutoBillDelayToDateComplete(SA_PARAM_NN_VALID BillingTransaction *pTrans)
{
	VindiciaXMLtoObjResult *pResult = NULL;
	GrantDaysSession *pGrantDays = pTrans->userData;
	struct abl__delayBillingToDateResponse *pResponse;

	PERFINFO_AUTO_START_FUNC();
	pResult = vindiciaCreateResponse(pTrans, VINDICIA_TYPE(abl, delayBillingToDateResponse));

	if(!pResult)
	{
		FinishGrantDaysSession(pGrantDays, "Could not get result from Vindicia.");
		PERFINFO_AUTO_STOP();
		return;
	}

	pResponse = pResult->pObj;
	BILLING_DEBUG_RESPONSE("abl__delayBillingToDate", pResponse);
	btFreeObjResult(pTrans, pResult);

	if(pResponse->_return_->returnCode != VINDICIA_SUCCESS_CODE)
	{
		FinishGrantDaysSession(pGrantDays, "Could not extend autobill in Vindicia.");
		PERFINFO_AUTO_STOP();
		return;
	}

	if (pGrantDays->pSub)
		accountLog(pGrantDays->pAccount, "Subscription extended: [subscription:%s] (days %d)", pGrantDays->pSub->name, pGrantDays->iDays);

	UpdateSubscriptions(pTrans);
	PERFINFO_AUTO_STOP();
}

// Delay autobill to date
void AutoBillDelayToDate(GrantDaysSession *pGrantDays)
{
	char *xml = NULL;
	struct abl__delayBillingToDate *p = NULL;
	U32 uNextDate = pGrantDays->uDate ? pGrantDays->uDate : pGrantDays->pSub->nextBillingDateSS2000 + pGrantDays->iDays * SECONDS_PER_DAY;
	char date[256];

	PERFINFO_AUTO_START_FUNC();
	p = btAlloc(pGrantDays->pTrans, p, struct abl__delayBillingToDate);

	if (uNextDate < timeSecondsSince2000())
	{
		FinishGrantDaysSession(pGrantDays, "Cannot take away days if it puts the next billing date to before today.");
		PERFINFO_AUTO_STOP();
		return;
	}

	timeMakeLocalDateNoTimeStringFromSecondsSince2000(date, uNextDate);

	p->_auth			= getVindiciaAuth();
	p->_autobill		= btAlloc(pGrantDays->pTrans, p->_autobill, struct vin__AutoBill);
	p->_autobill->VID	= btStrdup(pGrantDays->pTrans, pGrantDays->VID);
	p->_newBillingDate  = date;
	p->_movePermanently	= xsd__boolean__true_;

	if (vindiciaObjtoXML(&xml, p, VO2X_OBJ(abl, delayBillingToDate)))
	{
		btContinue(pGrantDays->pTrans, "abl:delayBillingToDate", xml, AutoBillDelayToDateComplete, pGrantDays);
	}
	else
	{
		FinishGrantDaysSession(pGrantDays, "Could not send delay request to Vindicia.");
	}

	estrDestroy(&xml);
	PERFINFO_AUTO_STOP();
}


/************************************************************************/
/* Public functions                                                     */
/************************************************************************/

BillingTransaction *
btGrantDays(AccountInfo *pAccount,
			const char *VID,
			int iDays,
			BillingTransaction *pTrans,
			GrantDaysCallback pCallback,
			void *pUserData)
{
	GrantDaysSession *pGrantDays;
	bool bComplete = false;

	PERFINFO_AUTO_START_FUNC();
	BILLING_DEBUG_START;

	accountLog(pAccount, "Attempting to grant %d days to subscription VID %s.", iDays, VID);

	if (!pTrans)
	{
		bComplete = true;
		pTrans = btCreateBlank(true);
	}

	pGrantDays = btStructCreate(pTrans, parse_GrantDaysSession);
	pGrantDays->pTrans = pTrans;
	pGrantDays->bComplete = bComplete;
	pGrantDays->pAccount = pAccount;
	pGrantDays->pCallback = pCallback;
	pGrantDays->pUserData = pUserData;
	pGrantDays->iDays = iDays;
	pGrantDays->pSub = findAccountSubscriptionByVID(pAccount, VID);
	estrCopy2(&pGrantDays->VID, VID);

	// Make sure this account owns this subscription
	if (!pGrantDays->pSub)
	{
#define btGrantDaysFailureMsg "Account does not own subscription being extended."
		BILLING_DEBUG_WARNING("%s\n", btGrantDaysFailureMsg);
		accountLog(pAccount, btGrantDaysFailureMsg);
		FinishGrantDaysSession(pGrantDays, btGrantDaysFailureMsg);
#undef btGrantDaysFailureMsg
		PERFINFO_AUTO_STOP();
		return pTrans;
	}

	if (!iDays)
	{
		// No days to take or give, so we're done!
		BILLING_DEBUG_WARNING("Attempting to grant no days!\n");
		FinishGrantDaysSession(pGrantDays, NULL);
		PERFINFO_AUTO_STOP();
		return pTrans;
	}

	if (iDays > 0 && !gbGrantDaysUseDate)
	{
		// We're going to do the normal AutoBill.GrantDays() stuff
		AutoBillGrantDays(pGrantDays);
	}
	else
	{
		// We're going to see if we can delay the existing autobill
		AutoBillDelayToDate(pGrantDays);
	}

	PERFINFO_AUTO_STOP();
	return pTrans;
}

BillingTransaction *
btSetDate(AccountInfo *pAccount,
		  const char *VID,
		  U32 uDate,
		  BillingTransaction *pTrans,
		  bool bForce,
		  GrantDaysCallback pCallback,
		  void *pUserData)
{
	GrantDaysSession *pGrantDays = NULL;
	bool bComplete = false;

	PERFINFO_AUTO_START_FUNC();
	BILLING_DEBUG_START;

	if (!verify(pAccount)) return pTrans;
	if (!verify(VID && *VID)) return pTrans;
	if (!verify(uDate)) return pTrans;
	if (!verify(pTrans)) return pTrans;

	if (!pTrans)
	{
		bComplete = true;
		pTrans = btCreateBlank(true);
	}

	pGrantDays = btStructCreate(pTrans, parse_GrantDaysSession);
	pGrantDays->pTrans = pTrans;
	pGrantDays->bComplete = bComplete;
	pGrantDays->pAccount = pAccount;
	pGrantDays->pCallback = pCallback;
	pGrantDays->pUserData = pUserData;
	pGrantDays->uDate = uDate;
	pGrantDays->pSub = findAccountSubscriptionByVID(pAccount, VID);
	estrCopy2(&pGrantDays->VID, VID);

	// Make sure this account owns this subscription
	if (!pGrantDays->pSub && !bForce)
	{
#define btSetDateFailureMsg "Account does not own subscription being extended."
		BILLING_DEBUG_WARNING("%s\n", btSetDateFailureMsg);
		accountLog(pAccount, btSetDateFailureMsg);
		FinishGrantDaysSession(pGrantDays, btSetDateFailureMsg);
#undef btSetDateFailureMsg
		PERFINFO_AUTO_STOP();
		return pTrans;
	}

	// We're going to see if we can delay the existing autobill
	AutoBillDelayToDate(pGrantDays);

	PERFINFO_AUTO_STOP();
	return pTrans;
}

#include "GrantExtraDays_c_ast.c"