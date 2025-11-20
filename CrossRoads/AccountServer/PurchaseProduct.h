#ifndef PURCHASEPRODUCT_H
#define PURCHASEPRODUCT_H

/************************************************************************/
/* Forward declarations                                                 */
/************************************************************************/

typedef struct BillingTransaction BillingTransaction; // billing.h
typedef struct PaymentMethod PaymentMethod; // AccountServer.h
typedef struct PurchaseSession PurchaseSession; // PurchaseProduct.c
typedef struct AccountInfo AccountInfo; // AccountServer.h
typedef struct TransactionItem TransactionItem; // billingTransaction.h
typedef enum PurchaseResult PurchaseResult; // accountnet.h
typedef enum TransactionProvider TransactionProvider; // accountnet.h

/************************************************************************/
/* Types                                                                */
/************************************************************************/

// Callback used by the purchase functions
typedef void (*PurchaseCallback)(PurchaseResult eResult,
								 SA_PARAM_OP_VALID BillingTransaction *pTrans,
								 PurchaseSession *pPurchaseSession,
								 SA_PARAM_OP_VALID void *pUserData);


/************************************************************************/
/* Public functions                                                     */
/************************************************************************/

AUTO_STRUCT;
typedef struct PurchaseDetails
{
	// Provider determines which set of members will be used
	TransactionProvider eProvider;
	char *pProxy;
	char *pCluster;
	char *pEnvironment;
	char *pIP;

	// For AS purchases
	PaymentMethod *pPaymentMethod;  NO_AST
	BillingTransaction *pTrans;  NO_AST
	char *pBankName;

	// For Steam purchases
	char *pOrderID; // only if already processed outside AS
	char *pTransactionID;
	U64 uSteamID;
	char *pLocCode;
	bool bWebPurchase;
} PurchaseDetails;

// Convenience method to populate a purchase method for an Account Server purchase
// If pPaymentMethod and pTrans parameters are provided, THEY WILL BE STOLEN
void PopulatePurchaseDetails(SA_PARAM_NN_VALID PurchaseDetails *pDetails,
							 SA_PARAM_OP_STR const char *pProxy,
							 SA_PARAM_OP_STR const char *pCluster,
							 SA_PARAM_OP_STR const char *pEnvironment,
							 SA_PARAM_OP_STR const char *pIP,
							 SA_PARAM_OP_VALID PaymentMethod *pPaymentMethod,
							 SA_PARAM_OP_VALID BillingTransaction *pTrans,
							 SA_PARAM_OP_STR const char *pBankName);

// Convenience method to populate a purchase method for a Steam purchase
void PopulatePurchaseDetailsSteam(SA_PARAM_NN_VALID PurchaseDetails *pDetails,
								  SA_PARAM_OP_STR const char *pProxy,
								  SA_PARAM_OP_STR const char *pCluster,
								  SA_PARAM_OP_STR const char *pEnvironment,
								  SA_PARAM_OP_STR const char *pIP,
								  SA_PARAM_OP_STR const char *pOrderID,
								  SA_PARAM_OP_STR const char *pTransactionID,
								  U64 uSteamID,
								  SA_PARAM_NN_STR const char *pLocCode,
								  bool bWebPurchase);

// Initiate a purchase
//
// If pTrans is provided, it is the responsibility of the caller to btComplete
// it after receiving a callback with a result that is successful after finalize.
PurchaseResult PurchaseProductLock(SA_PARAM_NN_VALID AccountInfo *pAccount,
								   EARRAY_OF(const TransactionItem) eaItems,
								   SA_PARAM_NN_STR const char *pCurrency,
								   unsigned int uActivationSuppressions,
								   bool bVerifyPrice,
								   SA_PARAM_NN_VALID PurchaseDetails *pDetails,
								   SA_PARAM_NN_VALID PurchaseCallback pCallback,
								   SA_PARAM_OP_VALID void *pUserData);

// Finalize a purchase
PurchaseResult PurchaseProductFinalize(SA_PARAM_NN_VALID PurchaseSession *pPurchaseSession,
									   bool bCapture,
									   SA_PARAM_OP_VALID PurchaseCallback pCallback,
									   SA_PARAM_OP_VALID void *pUserData);

// Performs a lock and finalize
//
// See notes for both above before using.
PurchaseResult PurchaseProduct(SA_PARAM_NN_VALID AccountInfo *pAccount,
							   EARRAY_OF(const TransactionItem) eaItems,
							   SA_PARAM_NN_STR const char *pCurrency,
							   unsigned int uActivationSuppressions,
							   bool bVerifyPrice,
							   SA_PARAM_NN_VALID PurchaseDetails *pDetails,
							   SA_PARAM_NN_VALID PurchaseCallback pCallback,
							   SA_PARAM_OP_VALID void *pUserData);

// Determine if a purchase is for points
bool PurchaseIsPoints(SA_PARAM_NN_VALID const PurchaseSession *pPurchaseSession);

// Retrieve the third-party order ID of a purchase
SA_RET_OP_STR const char *PurchaseOrderID(SA_PARAM_NN_VALID const PurchaseSession *pPurchaseSession);

// Retrieve the lock string of a purchase
SA_RET_OP_STR const char *PurchaseLockString(SA_PARAM_NN_VALID const PurchaseSession *pPurchaseSession);

// Mark a purchase as being finished later
U32 PurchaseDelaySession(SA_PARAM_NN_VALID PurchaseSession *pPurchaseSession);

// Create a session from an ID
SA_RET_OP_VALID BillingTransaction *
PurchaseRetrieveDelayedSession(SA_PARAM_NN_VALID const AccountInfo *pAccount,
							   U32 uPurchaseID,
							   SA_PARAM_OP_VALID BillingTransaction *pTrans,
							   SA_PARAM_NN_VALID PurchaseCallback pCallback,
							   SA_PARAM_OP_VALID void *pUserData);

// Get a fail message for a purchase result
SA_RET_NN_STR const char *purchaseResultMessage(PurchaseResult eResult);

#endif