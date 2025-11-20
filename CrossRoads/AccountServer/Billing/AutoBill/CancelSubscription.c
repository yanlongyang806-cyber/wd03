#include "AccountLog.h"
#include "AccountManagement.h"
#include "AccountServer.h"
#include "ActivateSubscription.h"
#include "Activity/billingActivity.h"
#include "CancelSubscription.h"
#include "CancelSubscription_c_ast.h"
#include "timing.h"
#include "UpdateActiveSubscriptions.h"
#include "UpdatePaymentMethod.h"

static void btCancelSubscription_Complete(BillingTransaction *pTrans)
{
	bool bContinuing = false;
	VindiciaXMLtoObjResult *pResult = NULL;
	AutoBillTransactionData *pData = pTrans->userData;

	PERFINFO_AUTO_START_FUNC();
	pResult = vindiciaCreateResponse(pTrans, VINDICIA_TYPE(abl, cancelResponse));

	if(pResult)
	{
		struct abl__cancelResponse *pResponse = pResult->pObj;
		BILLING_DEBUG_RESPONSE("abl__cancel", pResponse);
		btFreeObjResult(pTrans, pResult);

		if(pResponse->_return_->returnCode == VINDICIA_SUCCESS_CODE)
		{
			AccountInfo *pAccount = findAccountByID(pData->uAccountID);

			if (pAccount)
			{
				const CachedAccountSubscription *pSub = findAccountSubscriptionByVID(pAccount, pData->VID);

				if (pSub)
				{
					char dateTime[256];
					timeMakeLocalDateStringFromSecondsSince2000(dateTime, pSub->nextBillingDateSS2000);
					accountLog(pAccount, "Subscription cancelled: [subscription:%s] (ends %s)", pSub->name, dateTime);
					btActivityRecordCancellation(pAccount, pData->bMerchantInitiated, "Subscription cancelled");
				}

				if (pData->flags & FLAG_CANCEL_IMMEDIATE)
					accountStopCachedSub(pAccount->uID, pData->VID);
			}
			btUpdateActiveSubscriptionsContinue(pTrans);
			bContinuing = true;
		}
		else
		{
			btFail(pTrans, NULL);
		}
	}
	PERFINFO_AUTO_STOP();
}

void btCancelSubscriptionContinue(BillingTransaction *pTrans)
{
	char *xml = NULL;
	struct abl__cancel *p = NULL;
	AutoBillTransactionData *pData = pTrans->userData;

	PERFINFO_AUTO_START_FUNC();
	BILLING_DEBUG_START;

	p = callocStruct(struct abl__cancel);
	p->_auth          = getVindiciaAuth();
	p->_autobill      = callocStruct(struct vin__AutoBill);
	p->_autobill->VID = (char*)pData->VID;
	p->_disentitle    = pData->flags & FLAG_CANCEL_IMMEDIATE ? xsd__boolean__true_ : xsd__boolean__false_;
	p->_force         = xsd__boolean__true_;

	if(vindiciaObjtoXML(&xml, p, VO2X_OBJ(abl, cancel)))
	{
		btContinue(pTrans, "abl:cancel", xml, btCancelSubscription_Complete, pData);
	}

	estrDestroy(&xml);
	free(p->_autobill);
	free(p);
	PERFINFO_AUTO_STOP();
}

BillingTransaction *btCancelSubscription(U32 uAccountID, const char *VID, bool instant, bool bMerchantInitiated)
{
	BillingTransaction *pTrans = NULL;
	AutoBillTransactionData *pData = NULL;

	PERFINFO_AUTO_START_FUNC();
	pTrans = btCreateBlank(true);
	pData = btStructCreate(pTrans, parse_AutoBillTransactionData);
	pTrans->userData = pData;

	accountSetBillingEnabled(findAccountByID(uAccountID));

	pData->uAccountID = uAccountID;
	estrCopy2(&pData->VID, VID);
	pData->flags |= instant ? FLAG_CANCEL_IMMEDIATE : 0;
	pData->bMerchantInitiated = bMerchantInitiated;

	btCancelSubscriptionContinue(pTrans);

	PERFINFO_AUTO_STOP();
	return pTrans;
}


/************************************************************************/
/* New cancel code for super-sub-create                                 */
/************************************************************************/

AUTO_STRUCT;
typedef struct CancelSession
{
	BillingTransaction *pTrans; NO_AST
	bool bComplete;
	char *VID;	AST(ESTRING)
	bool bMerchantInitiated;
	AccountInfo *pAccount; NO_AST
	CancelCallback pCallback; NO_AST
	void *pUserData; NO_AST
} CancelSession;

static void btCancelSub_Finish(SA_PARAM_NN_VALID CancelSession *pCancel, SA_PARAM_OP_STR const char *pFailMsg)
{
	bool bSuccess = pFailMsg ? false : true;

	PERFINFO_AUTO_START_FUNC();
	if (!bSuccess)
	{
		btFail(pCancel->pTrans, "%s", pFailMsg);
	}

	if (pCancel->pCallback) pCancel->pCallback(bSuccess && pCancel->pTrans->result == BTR_SUCCESS, pCancel->pTrans, pCancel->pUserData);
	PERFINFO_AUTO_STOP();
}

static void btCancelSub_Updated(bool bSuccess,
								SA_PARAM_OP_VALID BillingTransaction *pTrans,
								SA_PARAM_NN_VALID CancelSession *pCancel)
{
	btCancelSub_Finish(pCancel, NULL);
}

static void btCancelSub_Complete(SA_PARAM_NN_VALID BillingTransaction *pTrans)
{
	VindiciaXMLtoObjResult *pResult = NULL;
	CancelSession *pCancel = pTrans->userData;
	struct abl__cancelResponse *pResponse;
	const CachedAccountSubscription *pSub;
	AutoBillTransactionData *pData;

	PERFINFO_AUTO_START_FUNC();
	pResult = vindiciaCreateResponse(pTrans, VINDICIA_TYPE(abl, cancelResponse));

	if(!pResult)
	{
		btCancelSub_Finish(pCancel, "Could not get result from Vindicia.");
		PERFINFO_AUTO_STOP();
		return;
	}

	pResponse = pResult->pObj;
	BILLING_DEBUG_RESPONSE("abl__cancel", pResponse);
	btFreeObjResult(pTrans, pResult);

	if(pResponse->_return_->returnCode != VINDICIA_SUCCESS_CODE)
	{
		btCancelSub_Finish(pCancel, "Could not cancel autobill in Vindicia.");
		PERFINFO_AUTO_STOP();
		return;
	}

	pSub = findAccountSubscriptionByVID(pCancel->pAccount, pCancel->VID);

	if (pSub)
	{
		char dateTime[256];
		timeMakeLocalDateStringFromSecondsSince2000(dateTime, pSub->nextBillingDateSS2000);
		accountLog(pCancel->pAccount, "Subscription cancelled: [subscription:%s] (ends %s)", pSub->name, dateTime);
		btActivityRecordCancellation(pCancel->pAccount, pCancel->bMerchantInitiated, "Subscription cancelled");
	}

	pTrans->userData = btStructCreate(pTrans, parse_AutoBillTransactionData);
	pData = pTrans->userData;

	pData->pCallback = btCancelSub_Updated;
	pData->pUserData = pCancel;
	pData->uAccountID = pCancel->pAccount->uID;

	btUpdateActiveSubscriptionsContinue(pTrans);
	PERFINFO_AUTO_STOP();
}

BillingTransaction *
btCancelSub(AccountInfo *pAccount,
			const char *VID,
			bool bInstant,
			bool bMerchantInitiated,
			BillingTransaction *pTrans,
			CancelCallback pCallback,
			void *pUserData)
{
	CancelSession *pCancel;
	bool bComplete = false;
	char *xml = NULL;
	struct abl__cancel *p;

	PERFINFO_AUTO_START_FUNC();
	BILLING_DEBUG_START;

	if (!pTrans)
	{
		bComplete = true;
		pTrans = btCreateBlank(true);
	}

	pCancel = btStructCreate(pTrans, parse_CancelSession);
	pCancel->pTrans = pTrans;
	pCancel->bComplete = bComplete;
	pCancel->pAccount = pAccount;
	pCancel->pCallback = pCallback;
	pCancel->pUserData = pUserData;
	estrCopy2(&pCancel->VID, VID);
	pCancel->bMerchantInitiated = bMerchantInitiated;

	p = btAlloc(pTrans, p, struct abl__cancel);

	p->_auth          = getVindiciaAuth();
	p->_autobill      = btAlloc(pTrans, p->_autobill, struct vin__AutoBill);
	p->_autobill->VID = (char*)VID;
	p->_disentitle    = bInstant ? xsd__boolean__true_ : xsd__boolean__false_;
	p->_force         = xsd__boolean__true_;

	if(vindiciaObjtoXML(&xml, p, VO2X_OBJ(abl, cancel)))
	{
		btContinue(pTrans, "abl:cancel", xml, btCancelSub_Complete, pCancel);
	}
	else
	{
		btCancelSub_Finish(pCancel, "Could not send cancel request to Vindicia.");
	}

	estrDestroy(&xml);

	PERFINFO_AUTO_STOP();
	return pTrans;
}

#include "CancelSubscription_c_ast.c"