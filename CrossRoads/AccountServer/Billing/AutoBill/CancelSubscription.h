#pragma once
#include "billing.h"

typedef struct AutoBillTransactionData AutoBillTransactionData;
typedef struct CachedPaymentMethod CachedPaymentMethod;
typedef enum UpdatePMResult UpdatePMResult;

BillingTransaction *btCancelSubscription(U32 uAccountID, const char *VID, bool instant, bool bMerchantInitiated);


/************************************************************************/
/* Used for new super sub-create                                        */
/************************************************************************/

typedef void (*CancelCallback)(bool success,
							   SA_PARAM_OP_VALID BillingTransaction *pTrans,
							   SA_PARAM_OP_VALID void *pUserData);

SA_ORET_OP_VALID BillingTransaction *
btCancelSub(SA_PARAM_NN_VALID AccountInfo *pAccount,
			SA_PARAM_NN_STR const char *VID,
			bool bInstant,
			bool bMerchantInitiated,
			SA_PARAM_OP_VALID BillingTransaction *pTrans,
			SA_PARAM_OP_VALID CancelCallback pCallback,
			SA_PARAM_OP_VALID void *pUserData);