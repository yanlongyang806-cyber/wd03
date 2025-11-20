#ifndef CRYPTIC_ACCOUNTSERVER_TRANSACTIONCANCEL_H
#define CRYPTIC_ACCOUNTSERVER_TRANSACTIONCANCEL_H

typedef struct BillingTransaction BillingTransaction;
typedef struct AccountInfo AccountInfo;

typedef void (*btCancelTransactionCallback)(SA_PARAM_NN_VALID BillingTransaction *pTrans, bool bSuccess, SA_PARAM_OP_STR const char *Reason,
											SA_PARAM_OP_VALID void *userData);

// Attempt to cancel a transaction that was not been captured.
SA_RET_NN_VALID BillingTransaction * btCancelTransaction(
	SA_PARAM_NN_VALID AccountInfo *pAccount,
	SA_PARAM_NN_STR const char *pId,
	SA_PARAM_OP_VALID BillingTransaction *pTrans,
	btCancelTransactionCallback pCallback,
	SA_PARAM_OP_VALID void *pUserData);

#endif  // CRYPTIC_ACCOUNTSERVER_TRANSACTIONCANCEL_H
