#pragma once
#include "UpdatePaymentMethod_h_ast.h"

/************************************************************************/
/* Forward declarations                                                 */
/************************************************************************/

typedef struct BillingTransaction BillingTransaction;
typedef struct CachedPaymentMethod CachedPaymentMethod;
typedef struct PaymentMethod PaymentMethod;
typedef struct AccountInfo AccountInfo;


/************************************************************************/
/* Types                                                                */
/************************************************************************/

// Result returned to the callback
AUTO_ENUM;
typedef enum UpdatePMResult {
	UPMR_Success = 0,
	UPMR_AVSFailed,
	UPMR_CVNFailed,
	UPMR_AuthorizationFailed,
	UPMR_NoValidationProduct,
	UPMR_FinishFailed,
	UPMR_InvalidAccount,
	UPMR_NoPMAdded,
	UPMR_TooManyPMsAdded,
	UPMR_CouldNotDeactivate,
} UpdatePMResult;

// Callback used by the purchase functions
typedef void (*UpdatePMCallback)(UpdatePMResult eResult,
								 SA_PARAM_OP_VALID BillingTransaction *pTrans,
								 SA_PARAM_OP_VALID const CachedPaymentMethod *pCachedPaymentMethod,
								 SA_PARAM_OP_VALID void *pUserData);


/************************************************************************/
/* Public functions                                                     */
/************************************************************************/

// Update a payment method on an account
//
// If pTrans is provided, it is the responsibility of the caller to btComplete
// it after receiving a callback if the result is success.  Otherwise, it will
// have already been failed and completed.
SA_ORET_OP_VALID BillingTransaction *UpdatePaymentMethod(SA_PARAM_NN_VALID AccountInfo *pAccount,
														 SA_PARAM_NN_VALID const PaymentMethod *pPaymentMethod,
														 SA_PARAM_NN_STR const char *pIP,
														 SA_PARAM_OP_STR const char *pBankName,
														 SA_PARAM_OP_VALID BillingTransaction *pTrans,
														 SA_PARAM_OP_VALID UpdatePMCallback pCallback,
														 SA_PARAM_OP_VALID void *pUserData);