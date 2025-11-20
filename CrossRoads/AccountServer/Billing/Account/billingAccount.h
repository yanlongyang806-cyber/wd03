#ifndef BILLINGACCOUNT_H
#define BILLINGACCOUNT_H

#include "billing.h"
#include "accountServer.h"

typedef struct PaymentMethod PaymentMethod;

void btAccountPush(AccountInfo *pAccount);

bool btPopulateVinAccountFromAccount(SA_PARAM_NN_VALID BillingTransaction *pTrans, 
									 SA_PRE_NN_NN_VALID struct vin__Account **ppVinAccount,
									 U32 uAccountID);
#define btPopulateVinAccountResponseFromAccount(pTrans, pResponse, uAccountID) \
	btPopulateVinAccountFromAccount(pTrans, &pResponse->_account, uAccountID)

typedef void (*UpdatePaymentMethodCallback)(bool success,
											SA_PARAM_OP_VALID BillingTransaction *pTrans,
											SA_PARAM_OP_VALID const CachedPaymentMethod *pCache,
											SA_PARAM_OP_VALID void *userData);

// If you provide a billing transaction, it is your responsibility to btComplete it if it
// returned.  Otherwise, it will make one and complete it itself.
SA_RET_OP_VALID BillingTransaction *
btAccountUpdatePaymentMethod(SA_PARAM_NN_VALID AccountInfo *pAccount,
							 SA_PARAM_NN_VALID const PaymentMethod *pPaymentMethod,
							 SA_PARAM_OP_VALID BillingTransaction *pTrans,
							 SA_PARAM_OP_VALID UpdatePaymentMethodCallback pCallback,
							 SA_PARAM_OP_VALID void *userData);

struct ArrayOfPaymentMethods;

void btPopulatePaymentMethodCache(SA_PARAM_NN_VALID const AccountInfo *pAccountInfo,
								  SA_PARAM_OP_VALID struct ArrayOfPaymentMethods *pVPaymentMethodArray);


/************************************************************************/
/* Utility functions                                                    */
/************************************************************************/

bool btFetchAccountStep(U32 uAccountID, SA_PARAM_NN_VALID BillingTransaction *pTrans, SA_PARAM_NN_VALID BillingTransactionCompleteCB callback, SA_PARAM_OP_VALID void *userData);

// Push an account to Vindicia
bool btPushAccount(SA_PARAM_NN_VALID BillingTransaction *pTrans,
				   SA_PARAM_NN_VALID struct vin__Account *pVinAccount,
				   SA_PARAM_NN_VALID BillingTransactionCompleteCB callback,
				   SA_PARAM_OP_VALID void *userData);

#endif
