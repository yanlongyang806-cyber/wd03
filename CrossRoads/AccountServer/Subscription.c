#include "Subscription.h"
#include "StashTable.h"
#include "earray.h"
#include "net/accountnet.h"
#include "EString.h"
#include "StringUtil.h"
#include "AccountServer.h"
#include "ProxyInterface/AccountProxy.h"
#include "Subscription/billingSubscription.h"
#include "AccountManagement.h"
#include "objContainer.h"
#include "objTransactions.h"
#include "AutoGen/AccountServer_autotransactions_autogen_wrappers.h"
#include "Money.h"

/************************************************************************/
/* Types                                                                */
/************************************************************************/

typedef struct SubscriptionCallbackHolder
{
	SubscriptionContainer *pSubscription;
	const SubscriptionContainer *pSubscriptionContainer;
	SubscriptionModificationCB pCallback;
	void *pUserData;
} SubscriptionCallbackHolder;


/************************************************************************/
/* Globals                                                              */
/************************************************************************/

static StashTable stSubscriptionsByName = NULL;

static StashTable stSubscriptionsByCategory = NULL;

void Subscription_DestroyContainers(void)
{
	objRemoveAllContainersWithType(GLOBALTYPE_ACCOUNTSERVER_SUBSCRIPTION);
	if (stSubscriptionsByName)
		stashTableClear(stSubscriptionsByName);
	if (stSubscriptionsByCategory)
		stashTableClear(stSubscriptionsByCategory);
}


/************************************************************************/
/* Private helper functions                                             */
/************************************************************************/

// Ensures a stash table exists and creates it if not
__forceinline static void ensureStashTablesExists(void)
{
	if (!stSubscriptionsByName)
		stSubscriptionsByName = stashTableCreateWithStringKeys(100, StashDeepCopyKeys);

	if (!stSubscriptionsByCategory)
		stSubscriptionsByCategory = stashTableCreateWithStringKeys(100, StashDeepCopyKeys);
}

// Add a subscription to the categories stash table
static void addSubscriptionToCategoriesStashTable(SA_PARAM_NN_VALID const SubscriptionContainer *pSubscription)
{
	assertmsg(pSubscription->uID, "Subscription missing ID.");

	EARRAY_CONST_FOREACH_BEGIN(pSubscription->ppCategories, i, s);
		StashElement pElement;

		if (stashFindElement(stSubscriptionsByCategory, pSubscription->ppCategories[i], &pElement))
		{
			INT_EARRAY pCategories = stashElementGetPointer(pElement);
			ea32PushUnique(&pCategories, pSubscription->uID);
			stashElementSetPointer(pElement, pCategories);
		}
		else
		{
			INT_EARRAY pCategories = NULL;
			ea32Push(&pCategories, pSubscription->uID);
			stashAddPointer(stSubscriptionsByCategory, pSubscription->ppCategories[i], pCategories, false);
		}
	EARRAY_FOREACH_END;
}

// Remove a subscription from the categories stash table
static void removeSubscriptionFromCategoriesStashTable(SA_PARAM_NN_VALID const SubscriptionContainer *pSubscription)
{
	if (pSubscription->ppCategories) return;

	EARRAY_CONST_FOREACH_BEGIN(pSubscription->ppCategories, i, s);
		StashElement pElement;

		if (stashFindElement(stSubscriptionsByCategory, pSubscription->ppCategories[i], &pElement))
		{
			CONTAINERID_EARRAY pCategories = stashElementGetPointer(pElement);
			int j;

			for (j = ea32Size(&pCategories) - 1; j >= 0; j--)
			{
				if (pCategories[j] == pSubscription->uID)
				{
					ea32Remove(&pCategories, j);
					break;
				}
			}

			if (!ea32Size(&pCategories))
			{
				ea32Destroy(&pCategories);
				stashRemovePointer(stSubscriptionsByCategory, pSubscription->ppCategories[i], NULL);
			}
			else
			{
				stashElementSetPointer(pElement, pCategories);
			}
		}
	EARRAY_FOREACH_END;
}

// Add a subscription to the stash tables
static void addSubscriptionToStashTables(SA_PARAM_NN_VALID const SubscriptionContainer *pSubscription)
{
	ensureStashTablesExists();
	stashAddInt(stSubscriptionsByName, pSubscription->pName, pSubscription->uID, false);
	addSubscriptionToCategoriesStashTable(pSubscription);
}

// Creates or recreates the stash tables for quick lookup
static void recreateStashTables(void)
{
	ContainerIterator iter = {0};
	Container *currCon = NULL;
	U32 uSize;

	ensureStashTablesExists();
	stashTableClear(stSubscriptionsByName);

	uSize = objCountTotalContainersWithType(GLOBALTYPE_ACCOUNTSERVER_SUBSCRIPTION);

	objInitContainerIteratorFromType(GLOBALTYPE_ACCOUNTSERVER_SUBSCRIPTION, &iter);
	currCon = objGetNextContainerFromIterator(&iter);
	while (currCon)
	{
		SubscriptionContainer *pSub = (SubscriptionContainer *)currCon->containerData;
		addSubscriptionToStashTables(pSub);
		currCon = objGetNextContainerFromIterator(&iter);
	}
	objClearContainerIterator(&iter);
}

#define PART_OF_FLOAT(c) ((c >= '0' && c <= '9') || c == '.')
#define IS_ALPHA(c) ((c >= 'A' && c <= 'Z'))
#define MAX_NUM_STRING_LEN 64

AUTO_TRANS_HELPER;
void parseSubscriptionCategories(ATH_ARG NOCONST(SubscriptionContainer) *pSubscription, const char *pCategories)
{
	if (!pCategories || !pSubscription) return;

	eaDestroyEString(&pSubscription->ppCategories);
	DivideString(pCategories, ",", &pSubscription->ppCategories,
		DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE|DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS|
		DIVIDESTRING_POSTPROCESS_REMOVEUNIQUE|DIVIDESTRING_POSTPROCESS_ESTRINGS);
}

AUTO_TRANS_HELPER;
void parseSubscriptionPrices(ATH_ARG NOCONST(SubscriptionContainer) *subscription, const char *prices)
{
	char *pWorkingBuffer = NULL;
	char *pCurr = NULL;
	int iCount = eaSize(&subscription->ppMoneyPrices);

	// if an empty prices string is passed in, we should forget our prices
	eaDestroyStructNoConst(&subscription->ppMoneyPrices, parse_MoneyContainer);

	estrCopy2(&pWorkingBuffer, prices);
	pCurr = pWorkingBuffer;

	while(*pCurr)
	{
		if(IS_WHITESPACE(*pCurr) || (*pCurr == ','))
		{
			pCurr++;
			continue;
		}

		if(PART_OF_FLOAT(*pCurr))
		{
			char pNumString[MAX_NUM_STRING_LEN+1];
			int i = 0;

			while(PART_OF_FLOAT(*pCurr))
			{
				pNumString[i++] = *pCurr;
				pCurr++;
			}

			if(!(*pCurr))
				break;

			pNumString[i] = 0;

			while(IS_WHITESPACE(*pCurr))
				pCurr++;

			if(!(*pCurr)) 
			{
				break;
			}
			else
			{
				char *pCurrencyString = pCurr;
				if(pCurrencyString[0] 
				&& pCurrencyString[1] 
				&& pCurrencyString[2]
				&& IS_ALPHA(pCurrencyString[0])
					&& IS_ALPHA(pCurrencyString[1])
					&& IS_ALPHA(pCurrencyString[2]))
				{
					NOCONST(MoneyContainer) *pPrice;
					pCurrencyString[3] = 0;
					pCurr += 3;
					pPrice = moneyToContainer(moneyCreate(pNumString, pCurrencyString));
					eaPush(&subscription->ppMoneyPrices, pPrice);
				}
				else
				{
					break;
				}
			}
		}

		pCurr++;
	}

	estrDestroy(&pWorkingBuffer);
}

// Callback that triggers after a subscription is pushed
static void subscription_CB_push(void *pUserData, bool success, SA_PARAM_OP_VALID const char *pReason)
{
	SubscriptionCallbackHolder *pHolder = pUserData;

	if (!success)
	{
		AssertOrAlert("ACCOUNTSERVER_SUB_PUSH_FAIL", "The subscription plan %s was changed but the sync to Vindicia failed.  Please re-save the subscription plan to sync it.",
			pHolder->pSubscriptionContainer->pName);
	}

	if (pHolder->pCallback)
		pHolder->pCallback(pHolder->pSubscriptionContainer, success, pHolder->pUserData, pReason);
	free(pHolder);
}

// Callback that triggers after a subscription is imported
// note: we can't make a statement that pHolder will be freed (in all cases) after this function is called.
static void subscription_CB_import(SA_PARAM_NN_VALID TransactionReturnVal *returnVal, SA_PRE_NN_VALID SubscriptionCallbackHolder *pHolder)
{
	if (returnVal->eOutcome == TRANSACTION_OUTCOME_FAILURE)
	{
		if (pHolder->pCallback) pHolder->pCallback(pHolder->pSubscription, false, pHolder->pUserData, NULL);
		SAFE_FREE(pHolder);
	}
	else if (returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		U32 uID = pHolder->pSubscription->uID ? pHolder->pSubscription->uID : atoi(returnVal->pBaseReturnVals->returnString);
		const SubscriptionContainer *pSubscription = pHolder->pSubscription->uID ? pHolder->pSubscription : findSubscriptionByID(uID);

		if (pSubscription)
		{
			pHolder->pSubscriptionContainer = pSubscription;
			btSubscriptionPush(pSubscription, subscription_CB_push, pHolder);
			addSubscriptionToCategoriesStashTable(pSubscription);
		}
		else
			SAFE_FREE(pHolder);
	}
	if (pHolder)
	{
		ANALYSIS_ASSUME(pHolder);
		if (!pHolder->pSubscription->uID)
		{
			StructDestroy(parse_SubscriptionContainer, pHolder->pSubscription);
		}
	}
}

// Import a single subscription
static void importSubscription(SA_PARAM_NN_VALID SubscriptionContainer *pSubscription,
							   SA_PARAM_OP_VALID SubscriptionModificationCB pCB,
							   SA_PARAM_OP_VALID void *pUserData)
{
	SubscriptionCallbackHolder *pHolder = callocStruct(SubscriptionCallbackHolder);
	pHolder->pSubscription = pSubscription;
	pHolder->pCallback = pCB;
	pHolder->pUserData = pUserData;
	objRequestContainerCreateLocal(objCreateManagedReturnVal(subscription_CB_import, pHolder), GLOBALTYPE_ACCOUNTSERVER_SUBSCRIPTION, pSubscription);
}

// This triggers after a subscription is added
static void postAdd(SA_PARAM_NN_VALID Container *con, SA_PARAM_NN_VALID SubscriptionContainer *pSubscription)
{
	addSubscriptionToStashTables(pSubscription);
}


/************************************************************************/
/* Transactions                                                         */
/************************************************************************/

AUTO_TRANSACTION
ATR_LOCKS(subscription, ".*");
enumTransactionOutcome trSubscriptionEdit(ATR_ARGS, ATR_ALLOW_FULL_LOCK NOCONST(SubscriptionContainer) *subscription,
										  SA_PARAM_NN_STR const char *pName,
										  SA_PARAM_NN_STR const char *pInternalName,
										  SA_PARAM_OP_STR const char *pDescription,
										  SA_PARAM_OP_STR const char *pBillingStatementIdentifier,
										  SA_PARAM_OP_STR const char *pProductName,
										  int iInitialfreedays,
										  SA_PARAM_OP_STR const char *pPrices,
										  SA_PARAM_OP_STR const char *pCategories,
										  U32 uFlags,
										  SA_PARAM_OP_STR const char *pBilledProductName)
{
	if (pInternalName && *pInternalName)
		estrCopy2(&subscription->pInternalName, pInternalName);

	if(pDescription)
	{
		estrCopy2(&subscription->pDescription, pDescription);
	}

	if(pBillingStatementIdentifier)
	{
		estrCopy2(&subscription->pBillingStatementIdentifier, pBillingStatementIdentifier);
	}

	if(pProductName)
	{
		estrCopy2(&subscription->pProductName, pProductName);
	}

	subscription->iInitialFreeDays = iInitialfreedays;

	if(pPrices)
	{
		parseSubscriptionPrices(subscription, pPrices);
	}

	if (pCategories)
	{
		parseSubscriptionCategories(subscription, pCategories);
	}

	if (pBilledProductName)
	{
		if (subscription->pBilledProductName)
		{
			free(subscription->pBilledProductName);
			subscription->pBilledProductName = NULL;
		}

		if (*pBilledProductName)
		{
			subscription->pBilledProductName = strdup(pBilledProductName);
		}
	}

	subscription->uFlags = uFlags;

	return TRANSACTION_OUTCOME_SUCCESS;
}

// Removes a subscription localization
AUTO_TRANS_HELPER;
void removeSubscriptionLocalization(ATH_ARG NOCONST(SubscriptionContainer) *pSubscription, const char *pLanguageTag)
{
	EARRAY_CONST_FOREACH_BEGIN(pSubscription->ppLocalizedInfo, i, s);
		if (!stricmp(pSubscription->ppLocalizedInfo[i]->pLanguageTag, pLanguageTag))
		{
			eaRemove(&pSubscription->ppLocalizedInfo, i);
			break;
		}
	EARRAY_FOREACH_END;
}

AUTO_TRANSACTION
ATR_LOCKS(pSubscription, ".Pplocalizedinfo");
enumTransactionOutcome trSubscriptionLocalize(ATR_ARGS, NOCONST(SubscriptionContainer) *pSubscription,
										 SA_PARAM_NN_STR const char *pLanguageTag,
										 SA_PARAM_NN_STR const char *pName,
										 SA_PARAM_NN_STR const char *pDescription)
{
	NOCONST(SubscriptionLocalizedInfo) *pLocalized = StructCreateNoConst(parse_SubscriptionLocalizedInfo);

	// Remove any that might already exist for this language tag
	removeSubscriptionLocalization(pSubscription, pLanguageTag);

	// Add the new one
	estrCopy2(&pLocalized->pDescription, pDescription);
	estrCopy2(&pLocalized->pName, pName);
	estrCopy2(&pLocalized->pLanguageTag, pLanguageTag);
	eaPush(&pSubscription->ppLocalizedInfo, pLocalized);

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pSubscription, ".Pplocalizedinfo");
enumTransactionOutcome trSubscriptionUnlocalize(ATR_ARGS, NOCONST(SubscriptionContainer) *pSubscription,
										   SA_PARAM_NN_STR const char *pLanguageTag)
{
	removeSubscriptionLocalization(pSubscription, pLanguageTag);

	return TRANSACTION_OUTCOME_SUCCESS;
}

/************************************************************************/
/* Search functions                                                     */
/************************************************************************/

// Find a subscription by the subscription's name
SA_RET_OP_VALID const SubscriptionContainer *findSubscriptionByName(SA_PARAM_NN_STR const char *pName)
{
	U32 uID;
	if (stashFindInt(stSubscriptionsByName, pName, &uID))
		return findSubscriptionByID(uID);
	return NULL;
}

// Find a subscription by the subscription's ID
SA_RET_OP_VALID const SubscriptionContainer *findSubscriptionByID(U32 uID)
{
	Container *con = objGetContainer(GLOBALTYPE_ACCOUNTSERVER_SUBSCRIPTION, uID);
	if (con) return (SubscriptionContainer *)con->containerData;
	return NULL;
}

// Find all subscription with a category -- eaDestroy NOT eaDestroyStruct
SA_RET_OP_VALID EARRAY_OF(const SubscriptionContainer) findSubscriptionsByCategory(SA_PARAM_NN_STR const char *pCategory)
{
	INT_EARRAY eaContainerIDs;
	if (stashFindPointer(stSubscriptionsByCategory, pCategory, &eaContainerIDs))
	{
		int i;
		EARRAY_OF(const SubscriptionContainer) ret = NULL;

		for (i = 0; i < ea32Size(&eaContainerIDs); i++)
		{
			const SubscriptionContainer *pSubscription = findSubscriptionByID(eaContainerIDs[i]);

			if (devassertmsg(pSubscription, "Subscription found in category stash table that does not exist."))
			{
				eaPush(&ret, pSubscription);
			}
		}

		return ret;
	}
	return NULL;
}


/************************************************************************/
/* Modifying functions                                                  */
/************************************************************************/

// Add a new subscription primarily from a SubscriptionContainer struct
void subscriptionAddStruct(SA_PARAM_NN_VALID SubscriptionCreateData *pData)
{
	NOCONST(SubscriptionContainer) *subscription;
	
	subscription = CONTAINER_NOCONST(SubscriptionContainer, findSubscriptionByName(pData->subscription.pName));
	if (subscription)
		return;
	subscription = StructCloneDeConst(parse_SubscriptionContainer, &pData->subscription);

	if (pData->pPriceString)
	{
		parseSubscriptionPrices(subscription, pData->pPriceString);
	}

	if (pData->pCategoryString)
	{
		parseSubscriptionCategories(subscription, pData->pCategoryString);
	}
	importSubscription((SubscriptionContainer *)subscription, NULL, NULL);
}

// Add a new subscription
void subscriptionAdd(SA_PARAM_NN_STR const char *pName,
					 SA_PARAM_NN_STR const char *pInternalName,
					 SA_PARAM_OP_STR const char *pDescription,
					 SA_PARAM_OP_STR const char *pBillingStatementIdentifier,
					 SA_PARAM_OP_STR const char *pProductName,
					 SubscriptionPeriodType ePeriodType,
					 int iPeriodAmount,
					 int iInitialfreedays,
					 SA_PARAM_OP_STR const char *pPrices,
					 U32 uGameCard,
					 SA_PARAM_OP_STR const char *pCategories,
					 U32 uFlags,
					 SA_PARAM_OP_STR const char *pBilledProductName,
					 SA_PARAM_OP_VALID SubscriptionModificationCB pCB,
					 SA_PARAM_OP_VALID void *pUserData)
{
	NOCONST(SubscriptionContainer) *subscription = CONTAINER_NOCONST(SubscriptionContainer, findSubscriptionByName(pName));

	if (subscription)
	{
		if (pCB) pCB((SubscriptionContainer *)subscription, false, pUserData, NULL);
		return;
	}

	subscription = StructCreateNoConst(parse_SubscriptionContainer);
	estrCopy2(&subscription->pName, pName);
	estrCopy2(&subscription->pInternalName, pInternalName);

	if(pDescription)
	{
		estrCopy2(&subscription->pDescription, pDescription);
	}

	if(pBillingStatementIdentifier)
	{
		estrCopy2(&subscription->pBillingStatementIdentifier, pBillingStatementIdentifier);
	}

	if(pProductName)
	{
		estrCopy2(&subscription->pProductName, pProductName);
	}

	subscription->periodType = ePeriodType;
	subscription->iPeriodAmount = iPeriodAmount;
	subscription->iInitialFreeDays = iInitialfreedays;

	if(pPrices)
	{
		parseSubscriptionPrices(subscription, pPrices);
	}

	if (pCategories)
	{
		parseSubscriptionCategories(subscription, pCategories);
	}

	subscription->uFlags = uFlags;

	subscription->gameCard = uGameCard;

	if (pBilledProductName && *pBilledProductName)
	{
		subscription->pBilledProductName = strdup(pBilledProductName);
	}

	importSubscription((SubscriptionContainer *)subscription, pCB, pUserData);
}

// Edit an existing subscription
void subscriptionEdit(SA_PARAM_NN_STR const char *pName,
					  SA_PARAM_NN_STR const char *pInternalName,
					  SA_PARAM_OP_STR const char *pDescription,
					  SA_PARAM_OP_STR const char *pBillingStatementIdentifier,
					  SA_PARAM_OP_STR const char *pProductName,
					  int iInitialfreedays,
					  SA_PARAM_OP_STR const char *pPrices,
					  SA_PARAM_OP_STR const char *pCategories,
					  U32 uFlags,
					  SA_PARAM_OP_STR const char *pBilledProductName,
					  SA_PARAM_OP_VALID SubscriptionModificationCB pCB,
					  SA_PARAM_OP_VALID void *pUserData)
{
	NOCONST(SubscriptionContainer) *subscription = CONTAINER_NOCONST(SubscriptionContainer, findSubscriptionByName(pName));
	SubscriptionCallbackHolder *pHolder;

	if (!subscription)
	{
		if (pCB) pCB(NULL, false, pUserData, NULL);
		return;
	}

	pHolder = callocStruct(SubscriptionCallbackHolder);
	pHolder->pCallback = pCB;
	pHolder->pUserData = pUserData;
	pHolder->pSubscription = (SubscriptionContainer *)subscription;

	removeSubscriptionFromCategoriesStashTable((const SubscriptionContainer *)subscription);
	AutoTrans_trSubscriptionEdit(objCreateManagedReturnVal(subscription_CB_import, pHolder), objServerType(), GLOBALTYPE_ACCOUNTSERVER_SUBSCRIPTION, subscription->uID,
		pName, pInternalName, pDescription, pBillingStatementIdentifier, pProductName, iInitialfreedays,
		pPrices, pCategories, uFlags, pBilledProductName);

	accountsClearPermissionsCacheIfSubOwned(pName);
}

// Add or replace a localization entry
void subscriptionLocalize(U32 uSubID,
					 SA_PARAM_NN_STR const char *pLanguageTag,
					 SA_PARAM_NN_STR const char *pName,
					 SA_PARAM_NN_STR const char *pDescription)
{
	if (strlen(pLanguageTag) < 1) return;
	AutoTrans_trSubscriptionLocalize(NULL, objServerType(), GLOBALTYPE_ACCOUNTSERVER_SUBSCRIPTION, uSubID, pLanguageTag, pName, pDescription);
}

// Remove a localization entry
void subscriptionUnlocalize(U32 uSubID, SA_PARAM_NN_STR const char *pLanguageTag)
{
	if (strlen(pLanguageTag) < 1) return;
	AutoTrans_trSubscriptionUnlocalize(NULL, objServerType(), GLOBALTYPE_ACCOUNTSERVER_SUBSCRIPTION, uSubID, pLanguageTag);
}


/************************************************************************/
/* List functions                                                       */
/************************************************************************/

// Get a list of all subscriptions -- DO NOT FREE THE CONTENTS; ONLY eaDestroy IT!
SA_RET_OP_VALID EARRAY_OF(SubscriptionContainer) getSubscriptionList(void)
{
	EARRAY_OF(SubscriptionContainer) list = NULL;
	ContainerIterator iter = {0};
	Container *currCon = NULL;
	U32 uSize;

	uSize = objCountTotalContainersWithType(GLOBALTYPE_ACCOUNTSERVER_SUBSCRIPTION);
	objInitContainerIteratorFromType(GLOBALTYPE_ACCOUNTSERVER_SUBSCRIPTION, &iter);
	currCon = objGetNextContainerFromIterator(&iter);
	while (currCon)
	{
		SubscriptionContainer *pSub = (SubscriptionContainer *)currCon->containerData;
		eaPush(&list, pSub);
		currCon = objGetNextContainerFromIterator(&iter);
	}
	objClearContainerIterator(&iter);

	return list;
}


/************************************************************************/
/* Initialization                                                       */
/************************************************************************/

// Initialize everything relating to subscriptions
void initializeSubscriptions(void)
{
	objRegisterNativeSchema(GLOBALTYPE_ACCOUNTSERVER_SUBSCRIPTION, parse_SubscriptionContainer, NULL, NULL, NULL, NULL, NULL);
	objRegisterContainerTypeAddCallback(GLOBALTYPE_ACCOUNTSERVER_SUBSCRIPTION, postAdd);
	recreateStashTables();
}


/************************************************************************/
/* Utility functions                                                    */
/************************************************************************/

// Create a categories ESTRING from a subscription
SA_RET_OP_STR char * getSubscriptionCategoryString(SA_PARAM_NN_VALID const SubscriptionContainer *pSubscription)
{
	char *ret = NULL;

	EARRAY_CONST_FOREACH_BEGIN(pSubscription->ppCategories, i, s);
		if (ret) estrConcatf(&ret, ", ");
		estrConcatf(&ret, "%s", pSubscription->ppCategories[i]);
	EARRAY_FOREACH_END;

	return ret;
}

// Returns a string representation of the given period type
SA_RET_NN_STR const char *getSubscriptionPeriodName(SubscriptionPeriodType eType)
{
	switch (eType)
	{
		xcase SPT_Day: return "Day";
		xcase SPT_Month: return "Month";
		xcase SPT_Year: return "Year";
	}
	return "Unknown";
}

#include "Subscription_h_ast.c"