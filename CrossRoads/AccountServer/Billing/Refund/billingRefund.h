#ifndef BILLINGREFUND_H
#define BILLINGREFUND_H

#include "billing.h"

// Notification of btRefund result.
typedef void (*btRefundCallback)(SA_PARAM_NN_VALID BillingTransaction *pTrans, bool bSuccess, SA_PARAM_OP_STR const char *Reason,
								 SA_PARAM_OP_STR const char *pAmount, SA_PARAM_OP_STR const char *pCurrency, SA_PARAM_OP_VALID void *userData);

// Refund a transaction for a particular account.
// If amount is specified, a partial refund of this amount will be issued.
// If bRefundWithVindicia is false, the transaction will not actually be refunded, but only recorded as refunded with Vindicia.
BillingTransaction *btRefund(AccountInfo *account, SA_PARAM_NN_STR const char *transaction, SA_PARAM_NN_STR const char *amount,
							 bool bRefundWithVindicia, bool bMerchantInitiated, SA_PARAM_OP_STR const char *pOptionalSubVid,
							 bool pOptionalSubImmediate, SA_PARAM_OP_VALID BillingTransaction *pTrans, btRefundCallback callback,
							 SA_PARAM_OP_VALID void *userData);

#endif  // BILLINGREFUND_H
