#pragma once
#include "billing.h"
#include "AccountServer.h"
#include "ActivateSubscription_h_ast.h"

#define FLAG_CANCEL_IMMEDIATE	   BIT(1)

typedef void (*UpdateActiveCallback)(bool bSuccess,
									 SA_PARAM_OP_VALID BillingTransaction *pTrans,
									 SA_PARAM_OP_VALID void *pUserData);

AUTO_STRUCT;
typedef struct AutoBillTransactionData
{
	// For helping determine transaction chain state
	U32 flags;

	// For all transactions
	U32 uAccountID;

	// For activating a new subscription
	char *pSubscriptionName;	AST(ESTRING)
	char *pCurrency;			AST(ESTRING)
	const PaymentMethod *pPaymentMethod;
	char *pIP;					AST(ESTRING)
	char *pBankName;			AST(ESTRING)

	// For cancellation / grant days
	char *VID;					AST(ESTRING)
	bool bMerchantInitiated;

	// For grant days
	U32 uDays;
	U32 uGameCardDays;

	// For updating subscription data
	NOCONST(CachedAccountSubscriptionList) *pSubList; NO_AST

	UpdateActiveCallback pCallback; NO_AST
	void *pUserData; NO_AST
	UpdateActiveCallback pBackupCallback; NO_AST
	void *pBackupUserData; NO_AST

	U32 uStartTime;
} AutoBillTransactionData;

typedef struct PaymentMethod PaymentMethod;
typedef enum UpdatePMResult UpdatePMResult;

/************************************************************************/
/* Used for new super sub-create                                        */
/************************************************************************/

typedef void (*ActivateSubCallback)(bool success,
									SA_PARAM_OP_VALID BillingTransaction *pTrans,
									SA_PARAM_OP_VALID void *pUserData);

SA_ORET_OP_VALID BillingTransaction *
btActivateSub(U32 uAccountID,
			  SA_PARAM_NN_STR const char *pSubscriptionName, 
			  SA_PARAM_OP_STR const char *pCurrency,
			  SA_PARAM_OP_VALID const PaymentMethod *pPaymentMethod,
			  SA_PARAM_NN_STR const char *ip,
			  SA_PARAM_OP_STR const char *pBankName,
			  unsigned int uExtraDays,
			  BillingTransaction *pTrans,
			  SA_PARAM_OP_VALID ActivateSubCallback pCallback,
			  SA_PARAM_OP_VALID void *pUserData);