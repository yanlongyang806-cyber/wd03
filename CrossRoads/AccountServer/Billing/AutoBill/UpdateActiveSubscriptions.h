#pragma once

#include "ActivateSubscription.h"  // for UpdateActiveCallback

typedef struct BillingTransaction BillingTransaction;

// Update active subscriptions
// NOTE: Does not self-complete ever: the callback must btComplete() or btContinue().
BillingTransaction * btUpdateActiveSubscriptions(U32 uAccountID, UpdateActiveCallback fpCallback, SA_PARAM_OP_VALID void *pUserData,
												 SA_PARAM_OP_VALID BillingTransaction *pTrans);

void btUpdateActiveSubscriptionsContinue(BillingTransaction *pTrans);

bool ShouldUpdateBillingFlag(SA_PARAM_NN_VALID const CachedAccountSubscription *pCachedSub);


typedef void (*UpdateBillingFlagCallback)(SA_PARAM_NN_VALID AccountInfo *pAccount,
										  SA_PARAM_NN_VALID NOCONST(CachedAccountSubscription) *pCachedSub,
										  SA_PARAM_OP_VALID BillingTransaction *pTrans,
										  bool bSuccess,
										  SA_PARAM_OP_VALID UserData pUserData);

void UpdateBillingFlag(SA_PARAM_NN_VALID AccountInfo *pAccount,
					   SA_PARAM_NN_VALID NOCONST(CachedAccountSubscription) *pCachedSub,
					   SA_PARAM_OP_VALID BillingTransaction *pTrans,
					   SA_PARAM_OP_VALID UpdateBillingFlagCallback pCallback,
					   SA_PARAM_OP_VALID UserData pUserData);