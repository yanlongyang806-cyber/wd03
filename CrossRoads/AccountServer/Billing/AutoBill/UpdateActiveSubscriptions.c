#include "UpdateActiveSubscriptions.h"
#include "timing.h"
#include "AccountLog.h"
#include "AccountManagement.h"
#include "ActivateSubscription.h"
#include "Subscription.h"
#include "AccountServer.h"
#include "StringUtil.h"

static void CallbackOrComplete(BillingTransaction *pTrans)
{
	PERFINFO_AUTO_START_FUNC();
	if (pTrans)
	{
		AutoBillTransactionData *pData = pTrans->userData;

		if (pData->pCallback)
		{
			pData->pCallback(true, pTrans, pData->pUserData);
			PERFINFO_AUTO_STOP();
			return;
		}
	}
	PERFINFO_AUTO_STOP();
}

static void updateAccountCachedSubscriptions(AutoBillTransactionData *pData)
{
	// The code below this function allocates all CachedSubscription data without using any of the btAlloc()
	// related calls. This is so we can steal all of the entries inside of pData->pSubList to give to the 
	// account server code. The goal of this function is to create a new SubList, move the members from
	// the old object into the new one (leaving the src sublist as an empty shell), and then handing
	// the new list object over.

	struct CachedAccountSubscriptionList *pFinalList = NULL;
	struct NOCONST(CachedAccountSubscriptionList) *pNonConstFinalList = NULL;

	PERFINFO_AUTO_START_FUNC();
	pFinalList = StructCreate(parse_CachedAccountSubscriptionList);
	pNonConstFinalList = CONTAINER_NOCONST(CachedAccountSubscriptionList, pFinalList);

	EARRAY_FOREACH_REVERSE_BEGIN(pData->pSubList->ppList, i);	
	{
		eaPush(&pNonConstFinalList->ppList, pData->pSubList->ppList[i]);
	}
	EARRAY_FOREACH_END;
	eaDestroy(&pData->pSubList->ppList);

	pNonConstFinalList->lastUpdatedSS2000 = timeSecondsSince2000();

	accountUpdateCachedSubscriptions(findAccountByID(pData->uAccountID), pFinalList); // This claims ownership over pFinalList
	PERFINFO_AUTO_STOP();
}

static void AddEntryAndPrune(NOCONST(CachedAccountSubscriptionList) *pSubList, NOCONST(CachedAccountSubscription) *pNewSub)
{
	bool bAdd = true;

	PERFINFO_AUTO_START_FUNC();

	EARRAY_FOREACH_REVERSE_BEGIN(pSubList->ppList, i);
	{
		if(!stricmp_safe(pSubList->ppList[i]->internalName, pNewSub->internalName))
		{
			if (shouldReplaceCachedSubscription(CONTAINER_RECONST(CachedAccountSubscription, pSubList->ppList[i]), CONTAINER_RECONST(CachedAccountSubscription, pNewSub)))
			{
				StructDestroyNoConst(parse_CachedAccountSubscription, eaRemoveFast(&pSubList->ppList, i));
			}
			else
			{
				bAdd = false;
			}
		}
	}
	EARRAY_FOREACH_END;

	if(bAdd)
	{
		eaPush(&pSubList->ppList, pNewSub);
	}
	else
	{
		StructDestroyNoConst(parse_CachedAccountSubscription, pNewSub);
	}
	PERFINFO_AUTO_STOP();
}

static void btUpdateActiveSubscriptions_ABFetchComplete(BillingTransaction *pTrans)
{
	VindiciaXMLtoObjResult *pResult = NULL;
	AutoBillTransactionData *pData = pTrans->userData;

	PERFINFO_AUTO_START_FUNC();
	pResult = vindiciaCreateResponse(pTrans, VINDICIA_TYPE(abl, fetchByAccountResponse));

	if(pResult)
	{
		const AccountInfo *pAccount = findAccountByID(pData->uAccountID);

		int i;
		struct abl__fetchByAccountResponse *pResponse = pResult->pObj;
		BILLING_DEBUG_RESPONSE("abl__fetchByAccount", pResponse);
		btFreeObjResult(pTrans, pResult);

		for(i=0; i<pResponse->_autobills->__size; i++)
		{
			struct vin__AutoBill *pAutoBill = pResponse->_autobills->__ptr[i];
			char *pInternalName = pAutoBill ? pAutoBill->merchantAffiliateId : NULL;
			NOCONST(CachedAccountSubscription) *pSub = NULL;
			const CachedAccountSubscription *pCachedSub = pAutoBill ? findAccountSubscriptionByVID(pAccount, pAutoBill->VID) : NULL;
			U32 uCreatedTime = pCachedSub ? pCachedSub->estimatedCreationTimeSS2000 : timeSecondsSince2000();
			U32 uPendingAction = 0;

			if (!pAutoBill)
				continue;

			if(!pInternalName || !pInternalName[0])
			{
				// BILLING_DEBUG("[VID#%s] Ignoring bogus AutoBill: no internal name associated (merchantAffiliateId).\n", pAutoBill->VID);
				// TODO JDRAGO Log
				continue;
			}

			EARRAY_CONST_FOREACH_BEGIN(pAccount->ppPendingActions, j, size);
			{
				const AccountPendingAction *pAction = pAccount->ppPendingActions[j];

				if (pAction &&
					pAction->eType == APAT_REFRESH_SUB_CACHE &&
					pAction->pRefreshSubCache &&
					!stricmp_safe(pAction->pRefreshSubCache->pSubscriptionVID, pAutoBill->VID))
				{
					uPendingAction = pAction->uID;
					break;
				}
			}
			EARRAY_FOREACH_END;

			pSub = createCachedAccountSubscriptionFromAutoBill(pAccount, pAutoBill);
			if(!pSub)
			{
				BILLING_DEBUG_WARNING("Could not cache autobill for the user %s and autobill VID %s.\n",
					pAccount->accountName, pAutoBill->VID);
				continue;
			}

			pSub->estimatedCreationTimeSS2000 = uCreatedTime;
			pSub->pendingActionID = uPendingAction;

			AddEntryAndPrune(pData->pSubList, pSub);
		}

		if(eaSize(&pData->pSubList->ppList) > 0)
		{
			updateAccountCachedSubscriptions(pData);
		}
	}

	StructDestroyNoConst(parse_CachedAccountSubscriptionList, pData->pSubList);
	CallbackOrComplete(pTrans);
	PERFINFO_AUTO_STOP();
}

void btUpdateActiveSubscriptionsContinue(BillingTransaction *pTrans)
{
	char *xml = NULL;
	struct abl__fetchByAccount *p = callocStruct(struct abl__fetchByAccount);
	AutoBillTransactionData *pData = pTrans->userData;

	PERFINFO_AUTO_START_FUNC();
	BILLING_DEBUG_START;

	p->_auth = getVindiciaAuth();
	p->_account = callocStruct(struct vin__Account);
	p->_account->merchantAccountId = btGetMerchantAccountID(pTrans, findAccountByID(pData->uAccountID));

	if(vindiciaObjtoXML(&xml, p, VO2X_OBJ(abl, fetchByAccount)))
	{
		btContinue(pTrans, "abl:fetchByAccount", xml, btUpdateActiveSubscriptions_ABFetchComplete, pData);
		pData->pSubList   = StructCreateNoConst(parse_CachedAccountSubscriptionList);
	}

	estrDestroy(&xml);
	free(p->_account);
	free(p);
	PERFINFO_AUTO_STOP();
}

// Update active subscriptions
// NOTE: Does not self-complete ever: the callback must btComplete() or btContinue().
BillingTransaction * btUpdateActiveSubscriptions(U32 uAccountID, UpdateActiveCallback fpCallback, SA_PARAM_OP_VALID void *pUserData,
												 SA_PARAM_OP_VALID BillingTransaction *pTrans)
{
	AutoBillTransactionData *pData;

	// Create a new transaction if necessary.
	PERFINFO_AUTO_START_FUNC();
	if (!pTrans)
		pTrans = btCreateBlank(true);
	pData = btStructCreate(pTrans, parse_AutoBillTransactionData);

	// Set up transaction state.
	pTrans->userData = pData;
	pData->uAccountID = uAccountID;
	pData->pCallback = fpCallback;
	pData->pUserData = pUserData;

	// Start transaction.
	btUpdateActiveSubscriptionsContinue(pTrans);
	PERFINFO_AUTO_STOP();
	return pTrans;
}

bool ShouldUpdateBillingFlag(SA_PARAM_NN_VALID const CachedAccountSubscription *pCachedSub)
{
	if (!verify(pCachedSub)) return false;

	if (pCachedSub->gameCard || pCachedSub->bBilled) return false;

	// We only want to mark it as billed if it started before one day from today
	// The extra day is there as leeway because Vindicia can update the entitlement before the start date by a few hours
	if (pCachedSub->startTimeSS2000 > timeSecondsSince2000() + SECONDS_PER_DAY) return false;

	return true;
}

typedef struct UpdateBillingInfo
{
	AccountInfo *pAccount;
	NOCONST(CachedAccountSubscription) *pCachedSub;
	UpdateBillingFlagCallback pCallback;
	UserData pUserData;
	BillingTransaction *pTrans;
} UpdateBillingInfo;

static void UpdateBillingFlagComplete(SA_PARAM_NN_VALID BillingTransaction *pTrans)
{
	VindiciaXMLtoObjResult *pResult = NULL;
	UpdateBillingInfo *pUpdate = pTrans->userData;
	NOCONST(CachedAccountSubscription) *pCachedSub = pUpdate->pCachedSub;
	AccountInfo *pAccount = pUpdate->pAccount;
	bool bSuccess = false;

	PERFINFO_AUTO_START_FUNC();
	pResult = vindiciaCreateResponse(pTrans, VINDICIA_TYPE(trn, fetchByAutobillResponse));

	if (!devassert(pAccount && pCachedSub)) goto out;

	if (pResult)
	{
		struct trn__fetchByAutobillResponse *pResponse = pResult->pObj;
		BILLING_DEBUG_RESPONSE("trn__fetchByAutobill", pResponse);
		btFreeObjResult(pTrans, pResult);

		bSuccess = true;

		if (pResponse->_transactions && pResponse->_transactions->__size > 0 &&
			pResponse->_transactions->__ptr)
		{
			int iCurTrans = 0;

			for (iCurTrans = 0; iCurTrans < pResponse->_transactions->__size; iCurTrans++)
			{
				struct vin__Transaction *pVinTrans = pResponse->_transactions->__ptr[iCurTrans];

				if (pVinTrans && pVinTrans->statusLog && pVinTrans->statusLog->__size > 0 &&
					pVinTrans->statusLog->__ptr)
				{
					int iCurTransStatus = 0;

					for (iCurTransStatus = 0; iCurTransStatus < pVinTrans->statusLog->__size; iCurTransStatus++)
					{
						struct vin__TransactionStatus *pVinStatus = pVinTrans->statusLog->__ptr[iCurTransStatus];

						if (pVinStatus && pVinStatus->status == vin__TransactionStatusType__Captured)
						{
							pCachedSub->bBilled = true;
							goto upgrade;
						}
					}
				}
			}
		}
	}
	else
	{
		AssertOrAlert("ACCOUNTSERVER_BILLING_NO_FETCHTRANS_RESPONSE", "Did not get a valid response to the fetch trans by autobill query.");
	}

upgrade:

	if (pCachedSub->bBilled)
	{
		const SubscriptionContainer *pSubscriptionPlan = findSubscriptionByName(pCachedSub->name);

		accountSetBilled(pAccount);

		// Give billed product
		if (devassert(pSubscriptionPlan) && pSubscriptionPlan->pBilledProductName && *pSubscriptionPlan->pBilledProductName)
		{
			const ProductContainer *pBilledProduct = findProductByName(pSubscriptionPlan->pBilledProductName);

			if (devassert(pBilledProduct))
			{
				ActivateProductResult eResult = accountActivateProduct(pAccount, pBilledProduct, NULL, NULL, NULL, NULL, NULL, 0, NULL, false);
				accountLog(pAccount, "Given [product:%s] for having been billed: %s",
					pBilledProduct->pName, StaticDefineIntRevLookupNonNull(ActivateProductResultEnum, eResult));
			}
		}

		// Handle recruits
		EARRAY_CONST_FOREACH_BEGIN(pAccount->ppProducts, iCurProduct, iNumProducts);
		{
			const AccountProductSub *pProductSub = pAccount->ppProducts[iCurProduct];
			const ProductContainer *pProduct = NULL;

			if (!devassert(pProductSub)) continue;

			pProduct = findProductByName(pProductSub->name);

			if (!devassert(pProduct)) continue;

			if (productHasSubscriptionListed(pProduct, pCachedSub->internalName))
			{
				EARRAY_CONST_FOREACH_BEGIN(pAccount->eaRecruiters, iCurRecruiter, iNumRecruiters);
				{
					const RecruiterContainer * pRecruiter = pAccount->eaRecruiters[iCurRecruiter];

					if (!devassert(pRecruiter)) continue;

					if (!stricmp_safe(pRecruiter->pProductInternalName, pProduct->pInternalName))
					{
						AccountInfo *pRecruiterAccount = findAccountByID(pRecruiter->uAccountID);

						if (devassert(pRecruiterAccount) &&
							accountUpgradeRecruit(pRecruiterAccount, pAccount->uID, pProduct->pInternalName,
							RS_Billed))
						{
							goto out; // break out of all the loops
						}
					}
				}
				EARRAY_FOREACH_END;
			}
		}
		EARRAY_FOREACH_END;
	}

out:

	if (pUpdate->pCallback)
	{
		PERFINFO_AUTO_START("Callback", 1);
		pUpdate->pCallback(pAccount, pCachedSub, pUpdate->pTrans, bSuccess, pUpdate->pUserData);
		PERFINFO_AUTO_STOP();
	}
	PERFINFO_AUTO_STOP();
}

void UpdateBillingFlag(SA_PARAM_NN_VALID AccountInfo *pAccount,
					   SA_PARAM_NN_VALID NOCONST(CachedAccountSubscription) *pCachedSub,
					   SA_PARAM_OP_VALID BillingTransaction *pTrans,
					   SA_PARAM_OP_VALID UpdateBillingFlagCallback pCallback,
					   SA_PARAM_OP_VALID UserData pUserData)
{
	char *xml = NULL;
	struct trn__fetchByAutobill *p = NULL;
	UpdateBillingInfo *pUpdate = NULL;
	bool bPassedTrans = pTrans ? true : false;

	PERFINFO_AUTO_START_FUNC();

	if (!verify(pAccount)) goto error;
	if (!verify(pCachedSub)) goto error;
	if (!verify(ShouldUpdateBillingFlag(CONTAINER_RECONST(CachedAccountSubscription, pCachedSub)))) goto error;

	pTrans = pTrans ? pTrans : btCreateBlank(false);
	
	p = btAlloc(pTrans, p, struct trn__fetchByAutobill);

	p->_auth = getVindiciaAuth();
	p->_autobill = btAlloc(pTrans, p->_autobill, struct vin__AutoBill);
	p->_autobill->VID = pCachedSub->vindiciaID;

	pUpdate = btAllocUserData(pTrans, sizeof(UpdateBillingInfo));
	pUpdate->pAccount = pAccount;
	pUpdate->pCachedSub = pCachedSub;
	pUpdate->pCallback = pCallback;
	pUpdate->pUserData = pUserData;
	pUpdate->pTrans = bPassedTrans ? pTrans : NULL;

	if (vindiciaObjtoXML(&xml, p, VO2X_OBJ(trn, fetchByAutobill)))
	{
		btContinue(pTrans, "trn:fetchByAutobill", xml, UpdateBillingFlagComplete, pUpdate);
	}
	else
	{
		goto error;
	}

	estrDestroy(&xml);

	PERFINFO_AUTO_STOP();
	return;

error:
	if (pCallback)
	{
		PERFINFO_AUTO_START("Failure Callback", 1);
		pCallback(pAccount, pCachedSub, pTrans, false, pUserData);
		PERFINFO_AUTO_STOP();
	}
	PERFINFO_AUTO_STOP();
}