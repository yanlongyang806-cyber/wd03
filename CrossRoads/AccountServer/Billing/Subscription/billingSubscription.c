#include "Product/billingProduct.h"
#include "vindicia.h"
#include "estring.h"
#include "Product.h"
#include "billing.h"
#include "net/accountnet.h"
#include "Subscription.h"
#include "Money.h"
#include "billingSubscription.h"

// -------------------------------------------------------------------------------------------
// Update Product

typedef struct UpdateSubscriptionData
{
	char *pSubscriptionName;
	SubscriptionPushFinishCallback fpCallback;
	void *pUserData;
} UpdateSubscriptionData;

// -------------------------------------------------------------------------------------------

static void btSubscriptionPush_Complete(BillingTransaction *pTrans)
{
	VindiciaXMLtoObjResult *pResult = NULL;
	UpdateSubscriptionData *pData = pTrans->userData;
	bool success = false;

	PERFINFO_AUTO_START_FUNC();
	pResult = vindiciaCreateResponse(pTrans, VINDICIA_TYPE(bpl, updateResponse));

	if(pResult)
	{
		struct prd__updateResponse *pResponse = pResult->pObj;
		if (pResponse->_return_->returnCode == VINDICIA_SUCCESS_CODE)
			success = true;
		BILLING_DEBUG_RESPONSE("bpl__update", pResponse);
		btFreeObjResult(pTrans, pResult);
	}

	if (success)
	{
		if (pData->fpCallback)
		{
			PERFINFO_AUTO_START("Success Callback", 1);
			pData->fpCallback(pData->pUserData, true, NULL);
			PERFINFO_AUTO_STOP();
		}
	}
	else
	{
		if (pData->fpCallback)
		{
			PERFINFO_AUTO_START("Failure Callback", 1);
			pData->fpCallback(pData->pUserData, false, "Subscription update failure");
			PERFINFO_AUTO_STOP();
		}
	}
	PERFINFO_AUTO_STOP();
}

static void btSubscriptionPush_FetchComplete(BillingTransaction *pTrans)
{
	bool bContinuing = false;
	VindiciaXMLtoObjResult *pResult = NULL;
	UpdateSubscriptionData *pData = pTrans->userData;

	PERFINFO_AUTO_START_FUNC();
	pResult = vindiciaCreateResponse(pTrans, VINDICIA_TYPE(bpl, fetchByMerchantBillingPlanIdResponse));

	if(pResult)
	{
		const SubscriptionContainer *pSub = findSubscriptionByName(pData->pSubscriptionName);
		struct bpl__fetchByMerchantBillingPlanIdResponse *pResponse = pResult->pObj;
		BILLING_DEBUG_RESPONSE("bpl__fetchByMerchantBillingPlanIdResponse", pResponse);
		btFreeObjResult(pTrans, pResult);

		if(pSub)
		{
			struct bpl__update *pBplUpdate = btAlloc(pTrans, pBplUpdate, struct bpl__update);
			pBplUpdate->_auth = getVindiciaAuth();

			if(pResponse->_billingPlan->VID && *pResponse->_billingPlan->VID)
			{
				BILLING_DEBUG("Found subscription '%s', updating...\n", pData->pSubscriptionName);
				pBplUpdate->_billingPlan = pResponse->_billingPlan;
			}
			else
			{
				BILLING_DEBUG("No match for subscription '%s', creating...\n", pData->pSubscriptionName);
				pBplUpdate->_billingPlan = btAlloc(pTrans, pBplUpdate->_billingPlan, struct vin__BillingPlan);
			}

			{
				int i;
				int iNumCycles = pSub->gameCard ? 1 : 0; // Repeat infinitely if not game card
				int iNumPrices = eaSize(&pSub->ppMoneyPrices);
				char *xml = NULL;
				struct vin__BillingPlan           *pPlan        = pBplUpdate->_billingPlan;
				struct vin__BillingPlanPeriod     *pPeriod      = btAlloc(pTrans, pPeriod, struct vin__BillingPlanPeriod);
				struct vin__MerchantEntitlementId *pEntitlement = btAlloc(pTrans, pEntitlement, struct vin__MerchantEntitlementId);
				enum vin__BillingPeriodType eBillingPeriodType;

				// Basic BillingPlan settings
				pPlan->merchantBillingPlanId      = btStrdupWithPrefix(pTrans, pData->pSubscriptionName);
				pPlan->description                = btStrdup(pTrans, NULL_TO_EMPTY(pSub->pDescription));
				pPlan->billingStatementIdentifier = btStrdup(pTrans, NULL_TO_EMPTY(pSub->pBillingStatementIdentifier));

				// Entitlement creation
				pPlan->merchantEntitlementIds = btAlloc(pTrans, pPlan->merchantEntitlementIds, struct ArrayOfMerchantEntitlementIds);
				pPlan->merchantEntitlementIds->__size = 1;
				pPlan->merchantEntitlementIds->__ptr  = &pEntitlement;
				pEntitlement->id = btStrdup(pTrans, pSub->pName);

				// BillingPlanPeriod creation
				pPlan->periods = btAlloc(pTrans, pPlan->periods, struct ArrayOfBillingPlanPeriods);
				pPlan->periods->__size = 1;
				pPlan->periods->__ptr = &pPeriod;
				switch(pSub->periodType)
				{
					xcase SPT_Year:  eBillingPeriodType = vin__BillingPeriodType__Year;
					xcase SPT_Month: eBillingPeriodType = vin__BillingPeriodType__Month;
					xcase SPT_Day:   eBillingPeriodType = vin__BillingPeriodType__Day;
				}
				pPeriod->type = &eBillingPeriodType;
				pPeriod->cycles = &iNumCycles;
				pPeriod->quantity = btAlloc(pTrans, pPeriod->quantity, int);
				*pPeriod->quantity = pSub->iPeriodAmount;

				// BillingPlanPrice creation
				if (iNumPrices > 0)
				{
					pPeriod->prices = btAlloc(pTrans, pPeriod->prices, struct ArrayOfBillingPlanPrices);
					pPeriod->prices->__size = iNumPrices;
					pPeriod->prices->__ptr  = btAllocCount(pTrans, pPeriod->prices->__ptr, struct vin__BillingPlanPrice*, iNumPrices);
					for(i=0; i<iNumPrices; i++)
					{
						const Money *pPrice = moneyContainerToMoneyConst(pSub->ppMoneyPrices[i]);
						pPeriod->prices->__ptr[i] = btAlloc(pTrans, pPeriod->prices->__ptr[i], struct vin__BillingPlanPrice);
						pPeriod->prices->__ptr[i]->amount = btMoneyRaw(pTrans, pPrice);
						pPeriod->prices->__ptr[i]->currency = btStrdup(pTrans, moneyCurrency(pPrice));
					}
				}

				if(vindiciaObjtoXML(&xml, pBplUpdate, VO2X_OBJ(bpl, update)))
				{
					btContinue(pTrans, "bpl:update", xml, btSubscriptionPush_Complete, pData);
					bContinuing = true;
				}
				estrDestroy(&xml);
			}
		}
	}

	if(!bContinuing)
	{
		if (pData->fpCallback)
		{
			PERFINFO_AUTO_START("Failure Callback", 1);
			pData->fpCallback(pData->pUserData, false, "Subscription fetch failure");
			PERFINFO_AUTO_STOP();
		}
	}

	PERFINFO_AUTO_STOP();
}

void btSubscriptionPush(SA_PARAM_NN_VALID const SubscriptionContainer *pSub,
						SA_PARAM_OP_VALID SubscriptionPushFinishCallback fpCallback,
						SA_PARAM_OP_VALID void *pUserData)
{
	char *xml = NULL;
	struct bpl__fetchByMerchantBillingPlanId *p = NULL;

	PERFINFO_AUTO_START_FUNC();
	BILLING_DEBUG_START;

	p = callocStruct(struct bpl__fetchByMerchantBillingPlanId);
	p->_auth = getVindiciaAuth();
	estrPrintf(&p->_merchantBillingPlanId, "%s%s", billingGetPrefix(), pSub->pName);

	if (verify(vindiciaObjtoXML(&xml, p, VO2X_OBJ(bpl, fetchByMerchantBillingPlanId))))
	{
		BillingTransaction *pTrans = btCreate("bpl:fetchByMerchantBillingPlanId", xml, btSubscriptionPush_FetchComplete, NULL, false);
		UpdateSubscriptionData *pData = btAllocUserData(pTrans, sizeof(struct UpdateSubscriptionData));
		pData->pSubscriptionName = btStrdup(pTrans, pSub->pName);
		pData->fpCallback = fpCallback;
		pData->pUserData = pUserData;
	}
	else if (fpCallback)
	{
		PERFINFO_AUTO_START("Failure Callback", 1);
		fpCallback(pUserData, false, "Internal subscription/Vindicia error");
		PERFINFO_AUTO_STOP();
	}

	estrDestroy(&xml);
	estrDestroy(&p->_merchantBillingPlanId);
	free(p);
	PERFINFO_AUTO_STOP();
}
