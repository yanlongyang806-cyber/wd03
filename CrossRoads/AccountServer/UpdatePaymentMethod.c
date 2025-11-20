#include "UpdatePaymentMethod.h"
#include "Transaction/billingTransaction.h"
#include "Product.h"
#include "billing.h"
#include "UpdatePaymentMethod_c_ast.h"
#include "AccountServer.h"
#include "AccountLog.h"
#include "AccountManagement.h"
#include "Account/billingAccount.h"
#include "error.h"
#include "StringUtil.h"

/************************************************************************/
/* Macros                                                               */
/************************************************************************/

// Assumed:
//   pTrans
//   pCachedPaymentMethod
//   pUpdateInfo

// Call the callback if it exists
#define DO_CALLBACK(result) if (pUpdateInfo->pCallback) { PERFINFO_AUTO_START("Callback", 1); pUpdateInfo->pCallback((result), (result) == UPMR_Success ? pTrans : NULL, pCachedPaymentMethod, pUpdateInfo->pUserData); PERFINFO_AUTO_STOP(); } (0)

// Does the callback, cleans up and returns
#define DO_CALLBACK_AND_RETURN(result) { DO_CALLBACK(result); freeUpdateInfo((result), (pUpdateInfo)); PERFINFO_AUTO_STOP_FUNC(); return; }
#define DO_CALLBACK_AND_RETURN_PTRANS(result) { DO_CALLBACK(result); freeUpdateInfo((result), (pUpdateInfo)); PERFINFO_AUTO_STOP_FUNC(); return pTrans; }

// If the check is not true, call the callback and give the error
#define CHECK(check, error) if (!(check)) DO_CALLBACK_AND_RETURN(error);
#define CHECK_PTRANS(check, error) if (!(check)) DO_CALLBACK_AND_RETURN_PTRANS(error);

/************************************************************************/
/* Types                                                                */
/************************************************************************/

// Flags used in the UpdateInfo struct
#define UPMI_COMPLETE_TRANS BIT(0)

// Stores update information
AUTO_STRUCT;
typedef struct UpdateInfo
{
	U32 uFlags;
	PaymentMethod *pPaymentMethod;
	AccountInfo *pAccount;		 NO_AST
	STRING_EARRAY eaKnownPMVIDs; AST(ESTRING)
	BillingTransaction *pTrans;  NO_AST
	UpdatePMCallback pCallback;  NO_AST
	void *pUserData;			 NO_AST
} UpdateInfo;

// Stores information needed to deactivate a payment method
AUTO_STRUCT;
typedef struct DeactivateInfo
{
	AccountInfo *pAccount;		 NO_AST	// Account that is having the payment method deactivated
	PaymentMethod *pPaymentMethod;		// Payment method being deactivated
	U32 uNumTries;						// Number of tries remaining
	U32 uNumTriesTotal;					// Number of tries total
	BillingTransaction *pTrans;	 NO_AST // Transaction used
	UpdatePMCallback pCallback;  NO_AST
	void *pUserData;			 NO_AST
	U32 uLastTransID;
	STRING_EARRAY eaResponses; AST(ESTRING)
} DeactivateInfo;

typedef struct DeactivateUserData
{
	UpdateInfo *pUpdateInfo;
	UpdatePMResult eResult;
} DeactivateUserData;


/************************************************************************/
/* Private functions                                                    */
/************************************************************************/

// Determine if a new payment method matches a cached one for unchangeable details
static bool doPaymentMethodsMatch(SA_PARAM_NN_VALID const PaymentMethod *pNewPaymentMethod, SA_PARAM_NN_VALID const CachedPaymentMethod *pCachedPaymentMethod)
{
	if (!pNewPaymentMethod) return false;
	if (!pCachedPaymentMethod) return false;

	if (pNewPaymentMethod->directDebit && pCachedPaymentMethod->directDebit)
	{
		const char *pAccountNumber = pCachedPaymentMethod->directDebit->account;
		const char *pNewAccountNumber = pNewPaymentMethod->directDebit->account;

		if (!pAccountNumber || !pNewAccountNumber) return false;

		// Advance account number past X's
		while (*pAccountNumber && *pAccountNumber == 'X' && *pNewAccountNumber)
		{
			pAccountNumber++;
			pNewAccountNumber++;
		}

		if (stricmp_safe(pAccountNumber, pNewAccountNumber)) return false;

		if (stricmp_safe(pCachedPaymentMethod->directDebit->bankSortCode, pNewPaymentMethod->directDebit->bankSortCode)) return false;

		if (stricmp_safe(pCachedPaymentMethod->directDebit->ribCode, pNewPaymentMethod->directDebit->ribCode)) return false;

		return true;
	}

	// This function only works with credit cards currently
	if (!pNewPaymentMethod->creditCard) return false;
	if (!pCachedPaymentMethod->creditCard) return false;

	if (!pNewPaymentMethod->creditCard->expirationDate) return false;
	if (!pCachedPaymentMethod->creditCard->expireDate) return false;

	if (!pNewPaymentMethod->creditCard->account) return false;
	if (!pCachedPaymentMethod->creditCard->bin) return false;
	if (!pCachedPaymentMethod->creditCard->lastDigits) return false;

	if (strlen(pNewPaymentMethod->creditCard->account) < strlen(pCachedPaymentMethod->creditCard->bin)) return false;
	if (strlen(pNewPaymentMethod->creditCard->account) < strlen(pCachedPaymentMethod->creditCard->lastDigits)) return false;

	if (strcmp(pNewPaymentMethod->creditCard->expirationDate, pCachedPaymentMethod->creditCard->expireDate)) return false;
	if (strncmp(pNewPaymentMethod->creditCard->account, pCachedPaymentMethod->creditCard->bin, strlen(pCachedPaymentMethod->creditCard->bin))) return false;
	if (strcmp(pNewPaymentMethod->creditCard->account + strlen(pNewPaymentMethod->creditCard->account) - strlen(pCachedPaymentMethod->creditCard->lastDigits), pCachedPaymentMethod->creditCard->lastDigits)) return false;
	return true;
}

SA_ORET_OP_VALID static BillingTransaction *deactivatePaymentMethod(SA_PARAM_NN_VALID DeactivateInfo *pDeactivate);

// Callback used for deactivation
static void deactivatePaymentMethodCallback(bool bSuccess,
											SA_PARAM_OP_VALID BillingTransaction *pTrans,
											SA_PARAM_OP_VALID const CachedPaymentMethod *pCache,
											SA_PARAM_NN_VALID DeactivateInfo *pDeactivate)
{
	PERFINFO_AUTO_START_FUNC();
	if (isValidPaymentMethodVID(pDeactivate->pAccount, pDeactivate->pPaymentMethod->VID))
	{
		const char *pLastVindiciaResponse = billingGetLastVindiciaResponse(pDeactivate->uLastTransID);

		if (pDeactivate->uLastTransID && pLastVindiciaResponse)
		{
			ANALYSIS_ASSUME(pLastVindiciaResponse);
			if (bSuccess)
			{
				eaPush(&pDeactivate->eaResponses, 
					estrDup(STACK_SPRINTF("Vindicia returned success code (with message: %s) but the payment method was still returned to us as being on the account.", pLastVindiciaResponse)));
			}
			else
			{
				eaPush(&pDeactivate->eaResponses, estrDup(pLastVindiciaResponse));
			}
		}
		else if (bSuccess)
		{
			eaPush(&pDeactivate->eaResponses, estrDup("Vindicia returned success code but the payment method was still returned to us as being on the account."));
		}
		else
		{
			eaPush(&pDeactivate->eaResponses, estrDup("Vindicia response missing."));
		}

		deactivatePaymentMethod(pDeactivate);
		PERFINFO_AUTO_STOP();
		return;
	}

	if (bSuccess)
	{
		if (pDeactivate->pCallback)
		{
			PERFINFO_AUTO_START("Success Callback", 1);
			pDeactivate->pCallback(UPMR_Success, pTrans, NULL, pDeactivate->pUserData);
			PERFINFO_AUTO_STOP();
		}
	}
	else
	{
		if (pDeactivate->uLastTransID)
		{
			const char *pLastVindiciaResponse = billingGetLastVindiciaResponse(pDeactivate->uLastTransID);

			if (pLastVindiciaResponse)
			{
				eaPush(&pDeactivate->eaResponses, estrDup(pLastVindiciaResponse));
			}
			else
			{
				eaPush(&pDeactivate->eaResponses, estrDup("Failed without getting a response from Vindicia."));
			}
		}
		else
		{
			eaPush(&pDeactivate->eaResponses, estrDup("Failed without creating a billing transaction."));
		}

		if (pDeactivate->pCallback)
		{
			PERFINFO_AUTO_START("Failure Callback", 1);
			pDeactivate->pCallback(UPMR_CouldNotDeactivate, NULL, NULL, pDeactivate->pUserData);
			PERFINFO_AUTO_STOP();
		}
	}

	if (pCache)
	{
		AssertOrAlert("ACCOUNTSERVER_DEACTIVATE_PAYMENTMETHOD", "Cached payment method found for inactivated payment method!");
	}

	StructDestroy(parse_DeactivateInfo, pDeactivate);
	PERFINFO_AUTO_STOP();
}

// Returns a pointer to a function static string containing deactivation information meant for error messages
SA_RET_NN_STR static const char *deactivationInfoString(SA_PARAM_NN_VALID const DeactivateInfo *pDeactivate)
{
	static char *pMsg = NULL;

	estrClear(&pMsg);
	estrConcatf(&pMsg, "Account: '%s'\n", pDeactivate->pAccount->accountName);
	estrConcatf(&pMsg, "Account GUID: '%s'\n", pDeactivate->pAccount->globallyUniqueID);
	estrConcatf(&pMsg, "Payment method VID: '%s'\n", pDeactivate->pPaymentMethod->VID);
	estrConcatf(&pMsg, "Number of errors recorded: %d\n", eaSize(&pDeactivate->eaResponses));

	EARRAY_CONST_FOREACH_BEGIN(pDeactivate->eaResponses, i, size);
		estrConcatf(&pMsg, "Response %d: '%s'\n", i + 1, pDeactivate->eaResponses[i]);
	EARRAY_FOREACH_END;

	return pMsg;
}

// Handles deactivation
static BillingTransaction *deactivatePaymentMethod(DeactivateInfo *pDeactivate)
{
	if (!devassert(pDeactivate->pAccount && pDeactivate->pPaymentMethod)) return NULL;

	PERFINFO_AUTO_START_FUNC();

	if (!pDeactivate->uNumTries)
	{
		AssertOrAlert("ACCOUNTSERVER_DEACTIVATE_PAYMENT_METHOD_FAIL", "Could not deactivate payment method after %d tries.\n%s",
			pDeactivate->uNumTriesTotal, deactivationInfoString(pDeactivate));

		if (pDeactivate->pCallback)
		{
			PERFINFO_AUTO_START("Callback", 1);
			pDeactivate->pCallback(UPMR_CouldNotDeactivate, NULL, NULL, pDeactivate->pUserData);
			PERFINFO_AUTO_STOP();
		}

		StructDestroy(parse_DeactivateInfo, pDeactivate);
		PERFINFO_AUTO_STOP();
		return NULL;
	}

	pDeactivate->uNumTries--;
	pDeactivate->pPaymentMethod->active = false;

	pDeactivate->pTrans = btAccountUpdatePaymentMethod(pDeactivate->pAccount,
		pDeactivate->pPaymentMethod,
		pDeactivate->pTrans,
		deactivatePaymentMethodCallback, pDeactivate);

	if (pDeactivate->pTrans)
	{
		pDeactivate->uLastTransID = pDeactivate->pTrans->ID;
	}
	else
	{
		pDeactivate->uLastTransID = 0;
	}

	PERFINFO_AUTO_STOP();
	return pDeactivate->pTrans;
}

// Create update information structure
SA_RET_NN_VALID static UpdateInfo *createUpdateInfo(SA_PARAM_OP_VALID UpdatePMCallback pCallback, SA_PARAM_OP_VALID void *pUserData)
{
	UpdateInfo *pUpdateInfo = StructCreate(parse_UpdateInfo);

	pUpdateInfo->pCallback = pCallback;
	pUpdateInfo->pUserData = pUserData;
	return pUpdateInfo;
}

// Translates the enum into a fail message
static const char *getFailMessage(UpdatePMResult eResult)
{
	switch(eResult)
	{
		xcase UPMR_Success: return "Success.";
		xcase UPMR_AVSFailed: return "AVS failed validation.";
		xcase UPMR_CVNFailed: return "CVN failed validation.";
		xcase UPMR_AuthorizationFailed: return "Authorization failed.";
		xcase UPMR_NoValidationProduct: return "Missing validation product.";
		xcase UPMR_FinishFailed: return "Validation cancel transaction failed.";
		xcase UPMR_InvalidAccount: return "Invalid account.";
		xcase UPMR_NoPMAdded: return "No payment method was added by Vindicia.";
		xcase UPMR_TooManyPMsAdded: return "Too many payment methods were added by Vindicia.";
	}
	return "Unknown error.";
}

// Frees an update info structure
static __forceinline void freeUpdateInfo(UpdatePMResult eResult,
										 SA_PARAM_NN_VALID UpdateInfo *pUpdateInfo)
{
	PERFINFO_AUTO_START_FUNC();
	if (pUpdateInfo->pAccount)
	{
		accountLog(pUpdateInfo->pAccount, "Update payment method result: %s", getFailMessage(eResult));
	}

	if (pUpdateInfo->pTrans && eResult != UPMR_Success)
	{
		if (pUpdateInfo->pTrans->result != BTR_FAILURE)
			btFail(pUpdateInfo->pTrans, getFailMessage(eResult));
	}

	StructDestroy(parse_UpdateInfo, pUpdateInfo);
	PERFINFO_AUTO_STOP();
}

static void UpdatePaymentMethod_DeactivatedCallback(UpdatePMResult eResult,
													SA_PARAM_OP_VALID BillingTransaction *pTrans,
													SA_PARAM_OP_VALID const CachedPaymentMethod *pCachedPaymentMethod,
													SA_PARAM_NN_VALID DeactivateUserData *pUserData)
{
	UpdateInfo *pUpdateInfo = pUserData->pUpdateInfo;
	UpdatePMResult eActualResult = pUserData->eResult;

	PERFINFO_AUTO_START_FUNC();
	free(pUserData);

	DO_CALLBACK_AND_RETURN(eActualResult);
}

// Callback called after finishing a real transaction
static void UpdatePaymentMethod_FinishCallback(bool success, U32 uFraudFlags, SA_PARAM_NN_VALID UpdateInfo *pUpdateInfo,
											   SA_PARAM_OP_STR const char *merchantTransactionId)
{
	const CachedPaymentMethod *pCachedPaymentMethod = NULL;
	BillingTransaction *pTrans = pUpdateInfo->pTrans;
	int found = 0;

	PERFINFO_AUTO_START_FUNC();

	// Find the new payment method
	if (pUpdateInfo->pPaymentMethod->VID && *pUpdateInfo->pPaymentMethod->VID)
	{
		pCachedPaymentMethod = getCachedPaymentMethod(pUpdateInfo->pAccount, pUpdateInfo->pPaymentMethod->VID);
		CHECK(pCachedPaymentMethod || pUpdateInfo->pPaymentMethod->precreated, UPMR_NoPMAdded);
	}
	else
	{
		// See if we got back a new VID
		EARRAY_CONST_FOREACH_BEGIN(pUpdateInfo->pAccount->personalInfo.ppPaymentMethods, i, size);
			CachedPaymentMethod *pCachedPM = pUpdateInfo->pAccount->personalInfo.ppPaymentMethods[i];

			if (eaFindString(&pUpdateInfo->eaKnownPMVIDs, pCachedPM->VID) < 0)
			{
				if (pCachedPM->creditCard)
				{
					pCachedPaymentMethod = pUpdateInfo->pAccount->personalInfo.ppPaymentMethods[i];
					found++;
				}
			}
		EARRAY_FOREACH_END;

		// If no VID was found, see if we updated an existing one
		if (found < 1)
		{
			EARRAY_CONST_FOREACH_BEGIN(pUpdateInfo->pAccount->personalInfo.ppPaymentMethods, i, size);
				if (doPaymentMethodsMatch(pUpdateInfo->pPaymentMethod, pUpdateInfo->pAccount->personalInfo.ppPaymentMethods[i]))
				{
					pCachedPaymentMethod = pUpdateInfo->pAccount->personalInfo.ppPaymentMethods[i];
					found++;
					break;
				}
			EARRAY_FOREACH_END;
		}

		if (found < 1)
		{
			BILLING_DEBUG_WARNING("It appears that Vindicia did not add the new payment method!\n");
		}
		CHECK(found > 0 && pCachedPaymentMethod, UPMR_NoPMAdded);
		if (found > 1)
		{
			BILLING_DEBUG_WARNING("It appears that Vindicia has somehow added %d new payment methods!\n", found);
		}
		CHECK(found < 2, UPMR_TooManyPMsAdded);
	}

	// Check fraud flags
	if (!success || uFraudFlags & INVALID_AVS || uFraudFlags & INVALID_CVN)
	{
		DeactivateInfo *pDeactivate = StructCreate(parse_DeactivateInfo);
		DeactivateUserData *pUserData = callocStruct(DeactivateUserData);

		if (!success)
			BILLING_DEBUG_WARNING("Card could not be added because authorization was not successful.\n");
		else if (uFraudFlags & INVALID_AVS)
			BILLING_DEBUG_WARNING("Card does not pass AVS configured requirements--deactivating.\n");
		else
			BILLING_DEBUG_WARNING("Card does not pass CVN configured requirements--deactivating.\n");

		pUserData->pUpdateInfo = pUpdateInfo;

		if (!success)
		{
			if (pTrans)
			{
				pUserData->eResult = UPMR_FinishFailed;
			}
			else
			{
				pUserData->eResult = UPMR_AuthorizationFailed;
			}
		}
		else if (uFraudFlags & INVALID_AVS)
		{
			pUserData->eResult = UPMR_AVSFailed;
		}
		else if (uFraudFlags & INVALID_CVN)
		{
			pUserData->eResult = UPMR_CVNFailed;
		}

		// Disable the payment method
		pDeactivate->pPaymentMethod = StructClone(parse_PaymentMethod, pUpdateInfo->pPaymentMethod);
		if (!pDeactivate->pPaymentMethod->VID)
		{
			pDeactivate->pPaymentMethod->VID = estrDup(pCachedPaymentMethod->VID);
		}
		pDeactivate->pPaymentMethod->active = false;
		pDeactivate->pAccount = pUpdateInfo->pAccount;
		pDeactivate->uNumTries = billingGetDeactivatePaymentMethodTries();
		pDeactivate->uNumTriesTotal = pDeactivate->uNumTries;
		pDeactivate->pTrans = pTrans;
		pDeactivate->pCallback = UpdatePaymentMethod_DeactivatedCallback;
		pDeactivate->pUserData = pUserData;

		deactivatePaymentMethod(pDeactivate);
		PERFINFO_AUTO_STOP();
		return;
	}

	DO_CALLBACK_AND_RETURN(UPMR_Success);
}

// Callback called after doing an auth
static void UpdatePaymentMethod_AuthCallback(SA_PARAM_OP_VALID BillingTransaction *pTrans, SA_PARAM_NN_VALID UpdateInfo *pUpdateInfo)
{
	const CachedPaymentMethod *pCachedPaymentMethod = NULL;

	PERFINFO_AUTO_START_FUNC();

	if (!pTrans)
	{
		// The transaction has already been failed/completed, or should have been.
		pUpdateInfo->pTrans = NULL;
		UpdatePaymentMethod_FinishCallback(false, 0, pUpdateInfo, NULL);
		PERFINFO_AUTO_STOP();
		return;
	}

	btTransactionFinish(pTrans, false, false, UpdatePaymentMethod_FinishCallback, pUpdateInfo);
	PERFINFO_AUTO_STOP();
}

// Push all known payment method VIDs into an earray of estrings
SA_RET_OP_VALID static STRING_EARRAY getKnownPMVIDs(SA_PARAM_NN_VALID const AccountInfo *pAccount)
{
	STRING_EARRAY eaKnown = NULL;

	EARRAY_CONST_FOREACH_BEGIN(pAccount->personalInfo.ppPaymentMethods, i, size);
		eaPush(&eaKnown, estrDup(pAccount->personalInfo.ppPaymentMethods[i]->VID));
	EARRAY_FOREACH_END;

	return eaKnown;
}


/************************************************************************/
/* Public functions                                                     */
/************************************************************************/

// Update a payment method on an account
//
// If pTrans is provided, it is the responsibility of the caller to btComplete
// it after receiving a callback if the result is success.  Otherwise, it will
// have already been failed and completed.
BillingTransaction *UpdatePaymentMethod(AccountInfo *pAccount,
										const PaymentMethod *pPaymentMethod,
										const char *pIP,
										const char *pBankName,
										BillingTransaction *pTrans,
										UpdatePMCallback pCallback,
										void *pUserData)
{
	UpdateInfo *pUpdateInfo = NULL;
	const CachedPaymentMethod *pCachedPaymentMethod = NULL;
	const ProductContainer *pProduct = NULL;
	const char *pProductName = billingGetValidationProduct();
	EARRAY_OF(TransactionItem) eaItems = NULL;
	TransactionItem *pItem = NULL;

	PERFINFO_AUTO_START_FUNC();
	pUpdateInfo = createUpdateInfo(pCallback, pUserData);

	accountLog(pAccount, "Attempted payment method update.");

	// Skip the auth/cancel if this is a deactivation attempt
	if (!pPaymentMethod->active)
	{
		DeactivateInfo *pDeactivate = StructCreate(parse_DeactivateInfo);
		pDeactivate->pPaymentMethod = StructClone(parse_PaymentMethod, pPaymentMethod);
		pDeactivate->pAccount = pAccount;
		pDeactivate->uNumTries = 1; // Only once because this is an explicit attempt and not an implicit one
		pDeactivate->uNumTriesTotal = 1;
		pDeactivate->pTrans = pTrans;
		pDeactivate->pCallback = pCallback;
		pDeactivate->pUserData = pUserData;
		if (!pDeactivate->pTrans)
			pDeactivate->pTrans = btCreateBlank(true);
		PERFINFO_AUTO_STOP();
		return deactivatePaymentMethod(pDeactivate);
	}

	// Create the pTrans if needed
	if (!pTrans)
	{
		pTrans = btCreateBlank(true);
		pUpdateInfo->uFlags |= UPMI_COMPLETE_TRANS;
	}
	pUpdateInfo->pTrans = pTrans;

	// Find the account
	CHECK_PTRANS(pAccount->uID, UPMR_InvalidAccount);
	pUpdateInfo->pAccount = pAccount;
	CHECK_PTRANS(pUpdateInfo->pAccount, UPMR_InvalidAccount);
	pUpdateInfo->eaKnownPMVIDs = getKnownPMVIDs(pUpdateInfo->pAccount);
	pUpdateInfo->pPaymentMethod = StructClone(parse_PaymentMethod, pPaymentMethod);

	// Find the product to perform the auth with
	CHECK_PTRANS(pProductName && *pProductName, UPMR_NoValidationProduct);
	pProduct = findProductByName(pProductName);
	CHECK_PTRANS(pProduct, UPMR_NoValidationProduct);

	pItem = StructCreate(parse_TransactionItem);
	pItem->uProductID = pProduct->uID;
	eaPush(&eaItems, pItem);

	// Do the auth
	btTransactionAuth(pAccount,
		eaItems,
		pPaymentMethod->currency,
		pPaymentMethod,
		pIP,
		pBankName,
		pTrans,
		true,
		UpdatePaymentMethod_AuthCallback, pUpdateInfo);
	eaDestroyStruct(&eaItems, parse_TransactionItem);

	PERFINFO_AUTO_STOP();
	return pTrans;
}

#include "UpdatePaymentMethod_c_ast.c"
#include "UpdatePaymentMethod_h_ast.c"