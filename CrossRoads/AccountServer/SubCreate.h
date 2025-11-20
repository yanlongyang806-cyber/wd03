#pragma once

/************************************************************************/
/* Forward declarations                                                 */
/************************************************************************/

typedef struct BillingTransaction BillingTransaction; // billing.h
typedef struct AccountInfo AccountInfo; // AccountServer.h
typedef struct SubscriptionContainer SubscriptionContainer; // Subscription.h
typedef struct PaymentMethod PaymentMethod; // UpdatePaymentMethod.h


/************************************************************************/
/* Types                                                                */
/************************************************************************/

// Possible sub create results
typedef enum {
	SUBCREATE_RESULT_SUCCESS = 0,
	SUBCREATE_RESULT_FAILURE,
	SUBCREATE_RESULT_INVALID_COMBO,
	SUBCREATE_RESULT_INVALID_PASSED_SUB,
	SUBCREATE_RESULT_CANT_EXTEND,
	SUBCREATE_RESULT_COULD_NOT_GIVE_INTERNAL,
	SUBCREATE_RESULT_COULD_NOT_CANCEL,
	SUBCREATE_RESULT_COULD_NOT_GIVE_SUB,
	SUBCREATE_RESULT_COULD_NOT_PURCHASE,
} SubCreateResult;

// Callback used by the sub create function
typedef void (*SubCreateCallback)(SubCreateResult eResult,
								  SA_PARAM_OP_VALID BillingTransaction *pTrans,
								  SA_PARAM_OP_VALID void *pUserData);


/************************************************************************/
/* Public functions                                                     */
/************************************************************************/

// Create a new subscription for the account with the extra days given
//
// If pTrans is provided, it is the responsibility of the caller to btComplete
// it after receiving a callback with a result that is success.  Otherwise,
// the billing transaction will be created and completed internally if needed. This is
// to allow the sub creation to be strung onto the beginning or end of other
// billing transactions if desired.
//
// This function further assumes that pAccount and pSubscription will be permanently valid
// pointers.
//
// uExtra update is used to give extra days to a new subscription, for game cards etc.
SA_RET_OP_VALID BillingTransaction *SubCreate(SA_PARAM_NN_VALID AccountInfo *pAccount,
										   SA_PARAM_NN_VALID const SubscriptionContainer *pSubscription,
										   unsigned int uExtraDays,
										   SA_PARAM_OP_VALID BillingTransaction *pTrans,
										   SA_PARAM_OP_VALID const PaymentMethod *pPaymentMethod,
										   SA_PARAM_OP_STR const char *pCurrency,
										   SA_PARAM_OP_STR const char *pIP,
										   SA_PARAM_OP_STR const char *pBankName,
										   SA_PARAM_OP_VALID SubCreateCallback pCallback,
										   SA_PARAM_OP_VALID void *pUserData);