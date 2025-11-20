#include "SubCreate.h"
#include "Subscription.h"
#include "SubCreate_c_ast.h"
#include "billing.h"
#include "AccountServer.h"
#include "InternalSubs.h"
#include "UpdatePaymentMethod.h"
#include "AccountManagement.h"
#include "AutoBill/CancelSubscription.h"
#include "PurchaseProduct.h"
#include "AutoBill/GrantExtraDays.h"
#include "AutoBill/ActivateSubscription.h"
#include "timing.h"
#include "StringUtil.h"
#include "Transaction/billingTransaction.h"
#include "AccountLog.h"

#include "PurchaseProduct_h_ast.h"

/************************************************************************/
/* Types                                                                */
/************************************************************************/

// Used for SubscriptionSession->uExistingTermEnd
#define TERM_END_NEVER 0
#define TERM_END_ERROR 1

AUTO_ENUM;
typedef enum SubscriptionPlanType
{
	ST_NONE = 0,	// No subscription
	ST_INTERNAL,	// Internal (trial, lifetime)
	ST_RECURRING,	// Normal recurring (Vindicia) subscription
	ST_GAMECARD,	// Game card (Vindicia) subscription
	ST_PURCHASE,	// Purchase (mock subscription that isn't giving an internal one)
} SubscriptionPlanType;

typedef struct SubscriptionSession SubscriptionSession;

// The type of dispatch function
typedef void (*SubscriptionDispatch)(SA_PARAM_NN_VALID SubscriptionSession *pSession);

AUTO_STRUCT;
typedef struct SubscriptionSession
{
	AccountInfo *pAccount;								NO_AST	// Passed account
	const SubscriptionContainer *pSubscription;			NO_AST	// Passed subscription
	U32 uExtraDays;												// Extra days to give the subscription
	BillingTransaction *pTrans;							NO_AST
	SubCreateCallback pCallback;						NO_AST
	void *pUserData;									NO_AST
	bool bCompleteTrans;										// Whether or not to btComplete the pTrans
	SubscriptionPlanType eExistingType;								// Existing sub type
	SubscriptionPlanType ePassedType;								// Passed sub type
	EARRAY_OF(SubscriptionDispatch) ppDispatchStack;	NO_AST	// A stack of dispatch functions to be called before success is returned
	U32 uExistingTermEnd;										// Time in seconds since 2000 of when the current term ends
	PaymentMethod *pPaymentMethod;
	char *pExistingVID;											// VID of existing sub, if game card/recurring
	char *pCurrency;
	char *pIP;
	char *pBankName;
} SubscriptionSession;

// Subscription operations
static void CancelExisting(SA_PARAM_NN_VALID SubscriptionSession *pSession);
static void CancelExistingAndCreateNew(SA_PARAM_NN_VALID SubscriptionSession *pSession);
static void CreateNew(SA_PARAM_NN_VALID SubscriptionSession *pSession);
static void ExtendExisting(SA_PARAM_NN_VALID SubscriptionSession *pSession);
static void Purchase(SA_PARAM_NN_VALID SubscriptionSession *pSession);


/************************************************************************/
/* Global variables                                                     */
/************************************************************************/

// Table of possible combinations
static struct {
	SubscriptionPlanType ePassedType;
	SubscriptionPlanType eExistingType;
	SubscriptionDispatch pDispatch;
} gDispatchTable[] = {
	{ST_GAMECARD,	ST_GAMECARD,	ExtendExisting},
	{ST_GAMECARD,	ST_RECURRING,	ExtendExisting},
	{ST_GAMECARD,	ST_INTERNAL,	ExtendExisting},
	{ST_GAMECARD,	ST_NONE,		CreateNew},
	{ST_RECURRING,	ST_GAMECARD,	CancelExistingAndCreateNew},
	{ST_RECURRING,	ST_RECURRING,	CancelExistingAndCreateNew},
	{ST_RECURRING,	ST_INTERNAL,	CreateNew},
	{ST_RECURRING,	ST_NONE,		CreateNew},
	{ST_INTERNAL,	ST_GAMECARD,	CancelExistingAndCreateNew},
	{ST_INTERNAL,	ST_RECURRING,	CancelExistingAndCreateNew},
	{ST_INTERNAL,	ST_INTERNAL,	CancelExistingAndCreateNew},
	{ST_INTERNAL,	ST_NONE,		CreateNew},
	{ST_PURCHASE,	ST_GAMECARD,	Purchase},
	{ST_PURCHASE,	ST_RECURRING,	Purchase},
	{ST_PURCHASE,	ST_INTERNAL,	Purchase},
	{ST_PURCHASE,	ST_NONE,		Purchase},
	{0,0,0}
};


/************************************************************************/
/* Private functions                                                    */
/************************************************************************/

// Destroy a Subscription Session
static void DestroySubscriptionSession(SA_PARAM_NN_VALID SubscriptionSession *pSession)
{
	if (pSession->ppDispatchStack)
	{
		// There are uncalled dispatch functions, but that's okay--just destroy the earray
		eaDestroy((cEArrayHandle *)&pSession->ppDispatchStack);
	}

	StructDestroy(parse_SubscriptionSession, pSession);
}

// Translate a result into a fail message
SA_RET_NN_STR static const char *TranslateFail(SubCreateResult eResult)
{
	switch (eResult)
	{
	xcase SUBCREATE_RESULT_FAILURE:
	default:
		return "Failure.";
	xcase SUBCREATE_RESULT_INVALID_COMBO:
		return "Invalid combination of existing and new subscription.";
	xcase SUBCREATE_RESULT_INVALID_PASSED_SUB:
		return "Invalid subscription passed.";
	xcase SUBCREATE_RESULT_CANT_EXTEND:
		return "Can't extend an internal subscription that never expires.";
	xcase SUBCREATE_RESULT_COULD_NOT_GIVE_INTERNAL:
		return "Cannot give internal sub -- possibly does not meet requirements.";
	xcase SUBCREATE_RESULT_COULD_NOT_CANCEL:
		return "Could not cancel existing subscription.";
	xcase SUBCREATE_RESULT_COULD_NOT_GIVE_SUB:
		return "Could not create new subscription.";
	xcase SUBCREATE_RESULT_COULD_NOT_PURCHASE:
		return "Could not purchase product.";
	}
}

// Finish the subscription session
static void FinishSession(SA_PARAM_NN_VALID SubscriptionSession *pSession,
						  SubCreateResult eResult)
{
	PERFINFO_AUTO_START_FUNC();
	if (pSession->pTrans->result == BTR_FAILURE)
	{
		pSession->pTrans = NULL;
	}

	if (eResult == SUBCREATE_RESULT_SUCCESS)
	{
		devassertmsg(!eaSize((cEArrayHandle *)&pSession->ppDispatchStack), "The subscription dispatch stack is not empty, but an attempt to return success has happened!  ContinueSession should have been called instead.");
	}
	else if (pSession->pTrans)
	{
		btFail(pSession->pTrans, TranslateFail(eResult));
	}

	if (pSession->pCallback)
		pSession->pCallback(eResult, pSession->pTrans, pSession->pUserData);

	DestroySubscriptionSession(pSession);
	PERFINFO_AUTO_STOP();
}

// Call/remove a dispatch function
static bool CallDispatch(SA_PARAM_NN_VALID SubscriptionSession *pSession)
{
	SubscriptionDispatch pDispatch;

	if (!pSession->ppDispatchStack) return false;

	PERFINFO_AUTO_START_FUNC();
	pDispatch = eaPop((cEArrayHandle *)&pSession->ppDispatchStack);

	if (!pDispatch)
	{
		PERFINFO_AUTO_STOP();
		return false;
	}

	pDispatch(pSession);

	PERFINFO_AUTO_STOP();
	return true;
}

// Push a dispatch function
static void PushDispatch(SA_PARAM_NN_VALID SubscriptionSession *pSession, SA_PARAM_NN_VALID SubscriptionDispatch pDispatch)
{
	eaPush((cEArrayHandle *)&pSession->ppDispatchStack, pDispatch);
}

// Finish a step of the session
static void ContinueSession(SA_PARAM_NN_VALID SubscriptionSession *pSession)
{
	PERFINFO_AUTO_START_FUNC();
	// Call the next step if there is any
	if (CallDispatch(pSession))
	{
		PERFINFO_AUTO_STOP();
		return;
	}

	// Otherwise, we're done with success!
	FinishSession(pSession, SUBCREATE_RESULT_SUCCESS);
	PERFINFO_AUTO_STOP();
}

// Determine the subscription type of a subscription container
static SubscriptionPlanType DetermineSubscriptionType(SA_PARAM_NN_VALID const SubscriptionContainer *pSubscription)
{
	if (pSubscription->gameCard) return ST_GAMECARD;
	if (pSubscription->uFlags & SUBSCRIPTION_MOCKPRODUCT)
	{
		const ProductContainer *pProduct = findProductByName(pSubscription->pProductName);

		if (!pProduct)
			return ST_PURCHASE; // Will fail later...

		if (pProduct->pInternalSubGranted && *pProduct->pInternalSubGranted)
			return ST_INTERNAL;

		return ST_PURCHASE;
	}
	return ST_RECURRING;
}

// Find an active cached subscription on an account by internal name
SA_RET_OP_VALID static const CachedAccountSubscription *FindCachedSubscription(SA_PARAM_NN_VALID const AccountInfo *pAccount,
																			SA_PARAM_NN_STR const char *pSubInternalName)
{
	if (!pAccount->pCachedSubscriptionList) return NULL;

	EARRAY_CONST_FOREACH_BEGIN(pAccount->pCachedSubscriptionList->ppList, i, size);
		const CachedAccountSubscription *pCachedSub = pAccount->pCachedSubscriptionList->ppList[i];

		if (!stricmp(pCachedSub->internalName, pSubInternalName) && getCachedSubscriptionStatus(pCachedSub) == SUBSCRIPTIONSTATUS_ACTIVE)
		{
			return pCachedSub;
		}
	EARRAY_FOREACH_END;

	return NULL;
}

// Determine the subscription type of an existing subscription on an account
static SubscriptionPlanType DetermineSubscriptionTypeFromName(SA_PARAM_NN_VALID const AccountInfo *pAccount,
														  SA_PARAM_NN_STR const char *pSubInternalName)
{
	const CachedAccountSubscription *pCachedSub = FindCachedSubscription(pAccount, pSubInternalName);
	const InternalSubscription *pInternalSub;

	if (pCachedSub)
	{
		if (pCachedSub->gameCard) return ST_GAMECARD;
		return ST_RECURRING;
	}

	pInternalSub = findInternalSub(pAccount->uID, pSubInternalName);

	if (pInternalSub) return ST_INTERNAL;

	return ST_NONE;
}

// Determine when the current subscription term ends
static U32 DetermineSubscriptionTermEnd(SA_PARAM_NN_VALID const AccountInfo *pAccount,
										SA_PARAM_NN_STR const char *pSubInternalName)
{
	const CachedAccountSubscription *pCachedSub = FindCachedSubscription(pAccount, pSubInternalName);
	const InternalSubscription *pInternalSub;

	if (pCachedSub)
	{
		return pCachedSub->nextBillingDateSS2000;
	}

	pInternalSub = findInternalSub(pAccount->uID, pSubInternalName);
	if (pInternalSub)
	{
		U32 uExpire = pInternalSub->uExpiration;

		if (!uExpire) return TERM_END_NEVER;
		return uExpire;
	}

	return TERM_END_ERROR;
}

// Create a Subscription Session
SA_RET_OP_VALID static SubscriptionSession *CreateSubscriptionSession(SA_PARAM_NN_VALID AccountInfo *pAccount,
																   SA_PARAM_NN_VALID const SubscriptionContainer *pSubscription,
																   unsigned int uExtraDays,
																   SA_PARAM_OP_VALID BillingTransaction *pTrans,
																   SA_PARAM_OP_VALID const PaymentMethod *pPaymentMethod,
																   SA_PARAM_OP_STR const char *pCurrency,
																   SA_PARAM_OP_STR const char *pIP,
																   SA_PARAM_OP_STR const char *pBankName,
																   SA_PARAM_OP_VALID SubCreateCallback pCallback,
																   SA_PARAM_OP_VALID void *pUserData)
{
	SubscriptionSession *pSession = NULL;
	const CachedAccountSubscription *pCached = NULL;

	PERFINFO_AUTO_START_FUNC();
	pSession = StructCreate(parse_SubscriptionSession);
	pCached = FindCachedSubscription(pAccount, pSubscription->pInternalName);

	if (!pSession)
	{
		PERFINFO_AUTO_STOP();
		return NULL;
	}

	pSession->pAccount = pAccount;
	pSession->pSubscription = pSubscription;
	pSession->uExtraDays = uExtraDays;
	pSession->pTrans = pTrans;
	pSession->pCallback = pCallback;
	pSession->pUserData = pUserData;
	pSession->ePassedType = DetermineSubscriptionType(pSubscription);
	pSession->eExistingType = DetermineSubscriptionTypeFromName(pAccount, pSubscription->pInternalName);
	pSession->uExistingTermEnd = DetermineSubscriptionTermEnd(pAccount, pSubscription->pInternalName);
	pSession->pPaymentMethod = StructClone(parse_PaymentMethod, pPaymentMethod);
	pSession->pCurrency = pCurrency ? strdup(pCurrency) : NULL;
	pSession->pIP = pIP ? strdup(pIP) : NULL;
	pSession->pBankName = pBankName ? strdup(pBankName) : NULL;

	pSession->bCompleteTrans = pTrans ? false : true;

	if (pSession->bCompleteTrans)
	{
		pSession->pTrans = btCreateBlank(true);
	}

	if (pCached)
	{
		pSession->pExistingVID = strdup(pCached->vindiciaID);
	}

	PERFINFO_AUTO_STOP();
	return pSession;
}

// Dispatch to the correct handling of the situation
static void DispatchSubscription(SA_PARAM_NN_VALID SubscriptionSession *pSession)
{
	int i = 0;

	PERFINFO_AUTO_START_FUNC();

	while (true)
	{
		if (!gDispatchTable[i].pDispatch) break;

		if (gDispatchTable[i].ePassedType == pSession->ePassedType &&
			gDispatchTable[i].eExistingType == pSession->eExistingType)
		{
			gDispatchTable[i].pDispatch(pSession);
			PERFINFO_AUTO_STOP();
			return;
		}

		i++;
	}

	FinishSession(pSession, SUBCREATE_RESULT_INVALID_COMBO);
	PERFINFO_AUTO_STOP();
}

// Purchase a product by ID
static void SubPurchaseProductID(SA_PARAM_NN_VALID SubscriptionSession *pSession,
								 U32 uProductID,
								 SA_PARAM_NN_VALID PurchaseCallback pCallback)
{
	PurchaseDetails purchaseDetails = {0};
	EARRAY_OF(TransactionItem) eaItems = NULL;
	TransactionItem *pItem = NULL;

	PERFINFO_AUTO_START_FUNC();
	pItem = StructCreate(parse_TransactionItem);
	pItem->uProductID = uProductID;
	eaPush(&eaItems, pItem);
	PopulatePurchaseDetails(&purchaseDetails, NULL, NULL, NULL, pSession->pIP, pSession->pPaymentMethod, pSession->pTrans, pSession->pBankName);
	PurchaseProduct(pSession->pAccount, eaItems, pSession->pCurrency, SUPPRESS_INTERNAL_SUB, false, &purchaseDetails, pCallback, pSession);
	StructDeInit(parse_PurchaseDetails, &purchaseDetails);
	eaDestroyStruct(&eaItems, parse_TransactionItem);
	PERFINFO_AUTO_STOP();
}


/************************************************************************/
/* Subscription operations                                              */
/************************************************************************/

static void CancelExisting_Callback(bool success,
							 SA_PARAM_OP_VALID BillingTransaction *pTrans,
							 SA_PARAM_NN_VALID SubscriptionSession *pSession)
{
	PERFINFO_AUTO_START_FUNC();
	if (success)
	{
		ContinueSession(pSession);
		PERFINFO_AUTO_STOP();
		return;
	}

	FinishSession(pSession, SUBCREATE_RESULT_COULD_NOT_CANCEL);
	PERFINFO_AUTO_STOP();
}

// Cancel the user's existing sub
static void CancelExisting(SA_PARAM_NN_VALID SubscriptionSession *pSession)
{
	devassertmsg(pSession->eExistingType != ST_NONE, "How can you cancel a subscription that doesn't exist?");

	PERFINFO_AUTO_START_FUNC();
	if (pSession->eExistingType == ST_INTERNAL)
	{
		// Remove any internal subscription that might exist
		devassertmsg(internalSubRemove(pSession->pAccount->uID, pSession->pSubscription->pInternalName), "Existing subscription type was marked as internal, but none exist?");
		ContinueSession(pSession);
		PERFINFO_AUTO_STOP();
		return;
	}

	btCancelSub(pSession->pAccount,
		pSession->pExistingVID,
		false,
		false,
		pSession->pTrans,
		CancelExisting_Callback,
		pSession);
	PERFINFO_AUTO_STOP();
}

// Cancel the user's existing sub and create a new one
static void CancelExistingAndCreateNew(SA_PARAM_NN_VALID SubscriptionSession *pSession)
{
	PERFINFO_AUTO_START_FUNC();
	PushDispatch(pSession, CreateNew);
	CancelExisting(pSession);
	PERFINFO_AUTO_STOP();
}

static void CreateNew_PurchaseCallback(PurchaseResult eResult,
									   SA_PARAM_OP_VALID BillingTransaction *pTrans,
									   PurchaseSession *pPurchaseSession,
									   SA_PARAM_NN_VALID SubscriptionSession *pSession)
{
	const ProductContainer *pProduct;
	U32 uExpirationDate = 0;

	devassertmsg(pSession->ePassedType == ST_INTERNAL, "It should only be possible to get here if you are buying an internal sub!");

	PERFINFO_AUTO_START_FUNC();

	if (eResult != PURCHASE_RESULT_COMMIT)
	{
		FinishSession(pSession, SUBCREATE_RESULT_COULD_NOT_GIVE_INTERNAL);
		PERFINFO_AUTO_STOP();
		return;
	}

	pProduct = findProductByName(pSession->pSubscription->pProductName);
	if (!pProduct)
	{
		FinishSession(pSession, SUBCREATE_RESULT_INVALID_PASSED_SUB);
		PERFINFO_AUTO_STOP();
		return;
	}

	if (pProduct->uDaysGranted)
	{
		U32 uExtraDays = pSession->uExtraDays;
		uExpirationDate = pSession->uExistingTermEnd;

		// Add the free days of the product to the free days passed
		uExtraDays += pProduct->uDaysGranted;

		uExpirationDate += uExtraDays * SECONDS_PER_DAY + timeSecondsSince2000();
	}

	devassert(pProduct->pInternalSubGranted);

	if (pSession->pSubscription->pInternalName && *pSession->pSubscription->pInternalName && stricmp_safe(pSession->pSubscription->pInternalName, pProduct->pInternalSubGranted))
		AssertOrAlert("ACCOUNTSERVER_MOCK_SUB_MISMATCH", "Warning: Mock sub has internal name %s but product given gives internal sub %s!", pSession->pSubscription->pInternalName, pProduct->pInternalSubGranted);

	devassert(internalSubCreate(pSession->pAccount, pProduct->pInternalSubGranted, uExpirationDate, pProduct->uID));

	ContinueSession(pSession);
	PERFINFO_AUTO_STOP();
}

static void CreateNew_Callback(bool success,
							   SA_PARAM_OP_VALID BillingTransaction *pTrans,
							   SA_PARAM_NN_VALID SubscriptionSession *pSession)
{
	PERFINFO_AUTO_START_FUNC();
	if (success)
	{
		ContinueSession(pSession);
		PERFINFO_AUTO_STOP();
		return;
	}

	FinishSession(pSession, SUBCREATE_RESULT_COULD_NOT_GIVE_SUB);
	PERFINFO_AUTO_STOP();
}

// Create a new sub
static void CreateNew(SA_PARAM_NN_VALID SubscriptionSession *pSession)
{
	PERFINFO_AUTO_START_FUNC();
	if (pSession->ePassedType == ST_INTERNAL)
	{
		// Create a new internal subscription
		const ProductContainer *pProduct = findProductByName(pSession->pSubscription->pProductName);

		if (!pProduct)
		{
			FinishSession(pSession, SUBCREATE_RESULT_INVALID_PASSED_SUB);
			PERFINFO_AUTO_STOP();
			return;
		}

		SubPurchaseProductID(pSession, pProduct->uID, CreateNew_PurchaseCallback);
		PERFINFO_AUTO_STOP();
		return;
	}

	if (pSession->ePassedType == ST_GAMECARD && !pSession->uExtraDays)
	{
		// Do nothing if we would create a game card and there are no days to give
		ContinueSession(pSession);
		PERFINFO_AUTO_STOP();
		return;
	}

	btActivateSub(pSession->pAccount->uID,
		pSession->pSubscription->pName,
		pSession->pCurrency,
		pSession->pPaymentMethod,
		pSession->pIP,
		pSession->pBankName,
		pSession->uExtraDays,
		pSession->pTrans,
		CreateNew_Callback,
		pSession);
	PERFINFO_AUTO_STOP();
}

static void ExtendExisting_Callback(bool success,
									SA_PARAM_OP_VALID BillingTransaction *pTrans,
									SA_PARAM_NN_VALID SubscriptionSession *pSession)
{
	PERFINFO_AUTO_START_FUNC();
	if (success)
	{
		ContinueSession(pSession);
		PERFINFO_AUTO_STOP();
		return;
	}

	FinishSession(pSession, SUBCREATE_RESULT_CANT_EXTEND);
	PERFINFO_AUTO_STOP();
}

// Extending the user's existing sub
static void ExtendExisting(SA_PARAM_NN_VALID SubscriptionSession *pSession)
{
	devassertmsg(pSession->eExistingType != ST_NONE, "How can you extend a subscription that doesn't exist?");
	PERFINFO_AUTO_START_FUNC();

	if (pSession->ePassedType == ST_GAMECARD && !pSession->uExtraDays)
	{
		// Do nothing if we would create a game card and there are no days to give
		ContinueSession(pSession);
		PERFINFO_AUTO_STOP();
		return;
	}

	if (pSession->eExistingType == ST_INTERNAL)
	{
		// Extend an internal subscription by destroying the current one and creating a new one
		const InternalSubscription *pExistingInternal = findInternalSub(pSession->pAccount->uID, pSession->pSubscription->pInternalName);

		devassertmsg(pExistingInternal, "Attempting to extend an internal subscription that doesn't exist? This should be impossible!");

		// Only extend internal subs that expire
		if (pExistingInternal->uExpiration)
		{
			// Extend the existing internal sub
			U32 uExpiration = pExistingInternal->uExpiration + pSession->uExtraDays * SECONDS_PER_DAY;
			U32 uProductID = pExistingInternal->uProductID;

			devassert(internalSubRemove(pSession->pAccount->uID, pSession->pSubscription->pInternalName));
			devassert(internalSubCreate(pSession->pAccount, pSession->pSubscription->pInternalName, uExpiration, uProductID));
		}

		ContinueSession(pSession);
		PERFINFO_AUTO_STOP();
		return;
	}

	btGrantDays(pSession->pAccount,
				pSession->pExistingVID,
				pSession->uExtraDays,
				pSession->pTrans,
				ExtendExisting_Callback,
				pSession);
	PERFINFO_AUTO_STOP();
}

static void Purchase_PurchaseCallback(PurchaseResult eResult,
									  SA_PARAM_OP_VALID BillingTransaction *pTrans,
									  PurchaseSession *pPurchaseSession,
									  SA_PARAM_NN_VALID SubscriptionSession *pSession)
{
	PERFINFO_AUTO_START_FUNC();
	if (eResult != PURCHASE_RESULT_COMMIT && eResult != PURCHASE_RESULT_PENDING_PAYPAL)
	{
		FinishSession(pSession, SUBCREATE_RESULT_COULD_NOT_PURCHASE);
		PERFINFO_AUTO_STOP();
		return;
	}

	ContinueSession(pSession);
	PERFINFO_AUTO_STOP();
}

// Purchase a mock subscription that isn't giving an internal one
static void Purchase(SA_PARAM_NN_VALID SubscriptionSession *pSession)
{
	// Purchase a product
	const ProductContainer *pProduct = findProductByName(pSession->pSubscription->pProductName);
	U32 *eaProductIDs = NULL;

	PERFINFO_AUTO_START_FUNC();
	if (!pProduct)
	{
		FinishSession(pSession, SUBCREATE_RESULT_INVALID_PASSED_SUB);
		PERFINFO_AUTO_STOP();
		return;
	}

	SubPurchaseProductID(pSession, pProduct->uID, Purchase_PurchaseCallback);
	PERFINFO_AUTO_STOP();
}


/************************************************************************/
/* Public functions                                                     */
/************************************************************************/

// Create a new subscription for the account with the extra days given
SA_RET_OP_VALID BillingTransaction *SubCreate(SA_PARAM_NN_VALID AccountInfo *pAccount,
										   SA_PARAM_NN_VALID const SubscriptionContainer *pSubscription,
										   unsigned int uExtraDays,
										   SA_PARAM_OP_VALID BillingTransaction *pTrans,
										   SA_PARAM_OP_VALID const PaymentMethod *pPaymentMethod,
										   SA_PARAM_OP_STR const char *pCurrency,
										   SA_PARAM_OP_STR const char *pIP,
										   SA_PARAM_OP_STR const char *pBankName,
										   SA_PARAM_OP_VALID SubCreateCallback pCallback,
										   SA_PARAM_OP_VALID void *pUserData)
{
	SubscriptionSession *pSession = NULL;
	
	PERFINFO_AUTO_START_FUNC();

	pSession = CreateSubscriptionSession(pAccount,
		pSubscription, uExtraDays, pTrans, pPaymentMethod, pCurrency, pIP, pBankName, pCallback, pUserData);

	if (pSession)
	{
		pTrans = pSession->pTrans;

		DispatchSubscription(pSession);
	}

	PERFINFO_AUTO_STOP_FUNC();

	return pTrans;
}

#include "SubCreate_c_ast.c"