#ifndef BILLINGTRANSACTION_H
#define BILLINGTRANSACTION_H

#include "Money.h"

typedef struct BillingTransaction BillingTransaction;
typedef struct PaymentMethod PaymentMethod;
typedef struct AccountInfo AccountInfo;
typedef struct TransactionItem TransactionItem;

typedef void (*TransactionAuthCallback)(SA_PARAM_OP_VALID BillingTransaction *pTrans, SA_PARAM_OP_VALID void *userData);
typedef void (*TransactionFinishCallback)(bool success, U32 uFraudFlags, void *userData, SA_PARAM_OP_STR const char *merchantTransactionId);

// Perform an authorization
SA_ORET_OP_VALID BillingTransaction * btTransactionAuth(
	SA_PARAM_NN_VALID AccountInfo *pAccount, 
	SA_PARAM_NN_VALID EARRAY_OF(const TransactionItem) eaItems, 
	SA_PARAM_NN_STR const char *pCurrency, 
	SA_PARAM_NN_VALID const PaymentMethod *pPaymentMethod,
	SA_PARAM_NN_STR const char *pIP,
	SA_PARAM_OP_STR const char *pBankName,
	SA_PARAM_OP_VALID BillingTransaction *pTrans,
	bool bSendWholePM,
	SA_PARAM_NN_VALID TransactionAuthCallback callback,
	SA_PARAM_OP_VALID void *userData);

// Perform a capture
SA_ORET_OP_VALID BillingTransaction * btTransactionFinish(
	SA_PARAM_NN_VALID BillingTransaction *pTrans,
	bool bCapture,
	bool bCompleteTrans,
	SA_PARAM_OP_VALID TransactionFinishCallback callback,
	SA_PARAM_OP_VALID void *userData);

#endif