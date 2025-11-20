#include "Product.h"
#include "accountnet.h"
#include "AccountManagement.h"
#include "ProxyInterface/AccountProxy.h"
#include "AccountServer.h"
#include "Product/billingProduct.h"
#include "earray.h"
#include "estring.h"
#include "FolderCache.h"
#include "Money.h"
#include "objContainer.h"
#include "objTransactions.h"
#include "ProductKey.h"
#include "rpn.h"
#include "StashTable.h"
#include "StringUtil.h"

#include "Product_c_ast.h"
#include "AutoGen/AccountServer_autotransactions_autogen_wrappers.h"

void productLoadDefaultPermissions(void);

/************************************************************************/
/* Globals                                                              */
/************************************************************************/

static StashTable stProductsByName = NULL;


/************************************************************************/
/* Private functions                                                    */
/************************************************************************/

// Utility function to remove NULL and empty strings from an earray
void seaRemoveNULLAndEmpty(STRING_EARRAY *eArray)
{
	EARRAY_FOREACH_REVERSE_BEGIN((*eArray), i);
	{
		const char *pString = (*eArray)[i];

		if (!(pString && *pString))
		{
			free(eaRemove(eArray, i));
		}
	}
	EARRAY_FOREACH_END;
}

// Set a product's prices
AUTO_TRANS_HELPER;
void setProductPrices(ATH_ARG NOCONST(ProductContainer) *product, const char *pricesString)
{
	char *copy;
	char **prices = NULL;

	eaDestroyStructNoConst(&product->ppMoneyPrices, parse_MoneyContainer);

	if (!pricesString)
		return;

	copy = estrDup(pricesString);
	DoVariableListSeparation(&prices, copy, false);
	estrDestroy(&copy);

	EARRAY_FOREACH_BEGIN(prices, i);
		char *split = strchr(prices[i], ' ');
		if (split)
		{
			*split = 0;
			eaPush(&product->ppMoneyPrices, moneyToContainer(moneyCreate(prices[i], split + 1)));
			*split = ' ';
		}
		free(prices[i]);
	EARRAY_FOREACH_END;

	if (prices) eaDestroy(&prices);
}

// Set a product's categories
AUTO_TRANS_HELPER;
void setProductCategories(ATH_ARG NOCONST(ProductContainer) *product, const char *categoryString)
{
	if (!product) return;

	eaDestroyEString(&product->ppCategories);
	estrDestroy(&product->pCategoriesString);

	if (!categoryString) return;

	DivideString(categoryString, ",", &product->ppCategories,
		DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE|DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS|
		DIVIDESTRING_POSTPROCESS_REMOVEUNIQUE|DIVIDESTRING_POSTPROCESS_ESTRINGS);
	estrCopy2(&product->pCategoriesString, categoryString);
}

// Add a key value change to a product
AUTO_TRANS_HELPER;
void appendProductKeyValueChange(ATH_ARG NOCONST(ProductContainer) *product, const char *pair)
{
	char *copy = estrDup(pair);
	char *split = strchr(copy, '=');
	NOCONST(ProductKeyValueChangeContainer) *kvChange;
	char *temp = NULL, *temp2 = NULL;

	if (!split)
	{
		estrDestroy(&copy);
		return;
	}

	*split = 0;
	kvChange = StructCreateNoConst(parse_ProductKeyValueChangeContainer);
	estrCopy2(&kvChange->pKey, copy);
	*split = '=';

	if (estrLength(&kvChange->pKey) > 0 && kvChange->pKey[estrLength(&kvChange->pKey) - 1] == '+')
	{
		kvChange->change = true;
		kvChange->pKey[estrLength(&kvChange->pKey) - 1] = ' ';
	}

	estrTrimLeadingAndTrailingWhitespace(&kvChange->pKey);
	estrCopy2(&temp, split + 1);
	estrTrimLeadingAndTrailingWhitespace(&temp);
	if (estrLength(&temp) > 0 && temp[0] == '"')
	{
		estrCopy2(&temp2, temp + 1);
		estrCopy2(&temp, temp2);
	}
	if (estrLength(&temp) > 0 && temp[estrLength(&temp) - 1] == '"')
	{
		int index = estrLength(&temp) - 1;
		temp[index] = 0;
		estrCopy2(&temp2, temp);
		temp[index] = '"';
		estrCopy2(&temp, temp2);
	}
	estrAppendUnescaped(&kvChange->pValue, temp);
	estrDestroy(&temp);

	eaPush(&product->ppKeyValueChanges, kvChange);

	estrDestroy(&copy);
}

// Set a product's key-value changes
AUTO_TRANS_HELPER;
void setProductKeyValueChanges(ATH_ARG NOCONST(ProductContainer) *product, const char *keyValueChangesString)
{
	char *cur, *start;
	char *copy = NULL;
	char *pair = NULL;
	bool insideQuotes = false;
	bool escaping = false;

	eaDestroyStructNoConst(&product->ppKeyValueChanges, parse_ProductKeyValueChangeContainer);

	if (!keyValueChangesString)
	{
		estrClear(&product->pKeyValueChangesString);
		return;
	}

	estrCopy2(&copy, keyValueChangesString);

	start = copy;
	for (cur = copy; *cur; cur++)
	{
		switch (*cur)
		{
			xcase ',':
		if (!insideQuotes)
		{
			*cur = 0;
			pair = NULL;
			estrCopy2(&pair, start);
			*cur = ',';
			start = cur + 1;
			appendProductKeyValueChange(product, pair);
			estrDestroy(&pair);
		}
		xcase '"':
		if (!escaping) insideQuotes = !insideQuotes;
		xcase '\\':
		escaping = !escaping;
		}
		if (*cur != '\\') escaping = false;
	}

	pair = NULL;
	estrCopy2(&pair, start);
	appendProductKeyValueChange(product, pair);
	estrDestroy(&pair);

	estrCopy2(&product->pKeyValueChangesString, keyValueChangesString);

	estrDestroy(&copy);
}

// Set a product's prerequisites
AUTO_TRANS_HELPER;
void setProductPrerequisites(ATH_ARG NOCONST(ProductContainer) *pProduct, const char *infix)
{
	estrCopy2(&pProduct->pPrerequisitesHuman, infix);
	eaDestroyEString(&pProduct->ppPrerequisites);
	infixToRPN(infix, &pProduct->ppPrerequisites, NULL);
	if (pProduct->pPrerequisitesHuman && pProduct->pPrerequisitesHuman[0] && !pProduct->ppPrerequisites)
	{
		eaPush(&pProduct->ppPrerequisites, estrDup("0")); // so that it cannot be evaluated
	}
	else
	{
		int iError = 0;
		AccountProxyKeysMeetRequirements(NULL, pProduct->ppPrerequisites, NULL, NULL, NULL, &iError, NULL);
		if (iError)
		{
			eaDestroyEString(&pProduct->ppPrerequisites);
			eaPush(&pProduct->ppPrerequisites, estrDup("0"));
		}
	}
}

// Removes a product localization
AUTO_TRANS_HELPER;
void removeProductLocalization(ATH_ARG NOCONST(ProductContainer) *pProduct, const char *pLanguageTag)
{
	EARRAY_CONST_FOREACH_BEGIN(pProduct->ppLocalizedInfo, i, s);
	if (!stricmp(pProduct->ppLocalizedInfo[i]->pLanguageTag, pLanguageTag))
	{
		StructDestroyNoConst(parse_ProductLocalizedInfo, eaRemove(&pProduct->ppLocalizedInfo, i));
		break;
	}
	EARRAY_FOREACH_END;
}

static void UpdateProduct(U32 uProductID)
{
	// Find the actual product in the DB
	const ProductContainer *pProductFromDB = findProductByID(uProductID);
	if (devassert(pProductFromDB))
	{
		btProductPush(pProductFromDB); // Push it to Vindicia
		ClearAccountProxyProductCache();
		accountsClearPermissionsCacheIfProductOwned(pProductFromDB->pName);
	}
}

// Callback that triggers after a product is imported
static void ProductCallbackDestroy(SA_PARAM_NN_VALID TransactionReturnVal *returnVal, SA_PARAM_NN_VALID ProductContainer *pProduct)
{
	U32 uProductID = 0;

	if (!verify(pProduct)) return;

	// Grab the product ID and destroy the struct
	StructDestroy(parse_ProductContainer, pProduct);

	if (!devassert(returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)) return;

	// Grab it from the return string
	uProductID = atoi(returnVal->pBaseReturnVals->returnString);

	UpdateProduct(uProductID);
}

static void ProductCallback(SA_PARAM_NN_VALID TransactionReturnVal *returnVal, SA_PARAM_NN_VALID ProductContainer *pProduct)
{
	if (!verify(pProduct)) return;
	if (!devassert(returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)) return;

	UpdateProduct(pProduct->uID);
}


/************************************************************************/
/* Transactions                                                         */
/************************************************************************/

AUTO_STRUCT;
typedef struct ProductEditRequest
{
	char *pName;							AST(ESTRING)
	char *pInternalName;					AST(ESTRING)
	char *pDescription;						AST(ESTRING)
	char *pBillingStatementIdentifier;		AST(ESTRING)
	char *pShards;							AST(ESTRING)
	char *pPermissions;						AST(ESTRING)
	char *pRequiredSubs;					AST(ESTRING)
	U32 uAccessLevel;
	char *pCategories;						AST(ESTRING)
	char *pKeyValueChanges;					AST(ESTRING)
	char *pItemID;							AST(ESTRING)
	char *pPrices;							AST(ESTRING)
	char *pPrerequisites;					AST(ESTRING)
	U32 uDaysGranted;
	U32 uFlags;
	char *pInternalSubGranted;				AST(ESTRING)
	char *pSubscriptionCategory;			AST(ESTRING)
	char *pActivationKeyPrefix;				AST(ESTRING)
	TaxClassification eTaxClassification;
	U32 uExpireDays;
	U64 qwOfferID;
	U8 contentID[XBOX_CONTENTID_SIZE];
	char *pRecruitUpgradedProduct;
	char *pRecruitBilledProduct;
	char *pReferredProduct;
	bool bRequiresNoSubs;
	bool bNoSpendingCap;
} ProductEditRequest;

AUTO_TRANSACTION
ATR_LOCKS(product, ".*");
enumTransactionOutcome trProductEdit(ATR_ARGS, ATR_ALLOW_FULL_LOCK NOCONST(ProductContainer) *product, ProductEditRequest *pEdit)
{
	if (pEdit->pInternalName && *pEdit->pInternalName)
		estrCopy2(&product->pInternalName, pEdit->pInternalName);

	estrCopy2(&product->pDescription, pEdit->pDescription);
	estrCopy2(&product->pBillingStatementIdentifier, pEdit->pBillingStatementIdentifier);
	estrCopy2(&product->pActivationKeyPrefix, pEdit->pActivationKeyPrefix);

	eaClearEString(&product->ppShards);
	if (pEdit->pShards)
	{
		char buffer[256];
		sprintf(buffer, "shard: %s", pEdit->pShards ? pEdit->pShards : "");
		accountPermissionValueStringParse(buffer, &product->ppShards, NULL);
	}

	eaClearStructNoConst(&product->ppPermissions, parse_AccountPermissionValue);
	if (pEdit->pPermissions)
	{
		accountPermissionValueStringParse(pEdit->pPermissions, NULL, &(AccountPermissionValue **)product->ppPermissions);
	}

	eaClearEString(&product->ppRequiredSubscriptions);
	if (pEdit->pRequiredSubs)
	{
		accountPermissionValueStringParse(pEdit->pRequiredSubs, &product->ppRequiredSubscriptions, NULL);
	}

	setProductPrerequisites(product, pEdit->pPrerequisites);

	estrCopy2(&product->pItemID, pEdit->pItemID);
	estrCopy2(&product->pInternalSubGranted, pEdit->pInternalSubGranted);
	estrCopy2(&product->pSubscriptionCategory, pEdit->pSubscriptionCategory);

	product->uAccessLevel = pEdit->uAccessLevel;
	if (product->uFlags & PRODUCT_BILLING_SYNC) pEdit->uFlags |= PRODUCT_BILLING_SYNC; // Can't turn this off.
	product->uFlags = pEdit->uFlags;
	product->uDaysGranted = pEdit->uDaysGranted;
	setProductCategories(product, pEdit->pCategories);
	setProductKeyValueChanges(product, pEdit->pKeyValueChanges);
	setProductPrices(product, pEdit->pPrices);
	product->eTaxClassification = pEdit->eTaxClassification;
	product->uExpireDays = pEdit->uExpireDays;

	memcpy(product->xbox.contentId, pEdit->contentID, XBOX_CONTENTID_SIZE);
	product->xbox.qwOfferID = pEdit->qwOfferID;

	eaClearEx(&product->recruit.eaBilledProducts, NULL);
	if (pEdit->pRecruitBilledProduct)
		DoVariableListSeparation(&product->recruit.eaBilledProducts, pEdit->pRecruitBilledProduct, false);
	seaRemoveNULLAndEmpty(&product->recruit.eaBilledProducts);

	eaClearEx(&product->recruit.eaUpgradedProducts, NULL);
	if (pEdit->pRecruitUpgradedProduct)
		DoVariableListSeparation(&product->recruit.eaUpgradedProducts, pEdit->pRecruitUpgradedProduct, false);
	seaRemoveNULLAndEmpty(&product->recruit.eaUpgradedProducts);

	if (product->pReferredProduct)
	{
		free(product->pReferredProduct);
		product->pReferredProduct = NULL;
	}

	if (pEdit->pReferredProduct && *pEdit->pReferredProduct)
	{
		product->pReferredProduct = strdup(pEdit->pReferredProduct);
	}

	product->bRequiresNoSubs = pEdit->bRequiresNoSubs;
	product->bNoSpendingCap = pEdit->bNoSpendingCap;

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pProduct, ".Pplocalizedinfo");
enumTransactionOutcome trProductLocalize(ATR_ARGS, NOCONST(ProductContainer) *pProduct,
										 SA_PARAM_NN_STR const char *pLanguageTag,
										 SA_PARAM_NN_STR const char *pName,
										 SA_PARAM_NN_STR const char *pDescription)
{
	NOCONST(ProductLocalizedInfo) *pLocalized = StructCreateNoConst(parse_ProductLocalizedInfo);

	// Remove any that might already exist for this language tag
	removeProductLocalization(pProduct, pLanguageTag);

	// Add the new one
	estrCopy2(&pLocalized->pDescription, pDescription);
	estrCopy2(&pLocalized->pName, pName);
	estrCopy2(&pLocalized->pLanguageTag, pLanguageTag);
	eaPush(&pProduct->ppLocalizedInfo, pLocalized);

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pProduct, ".Pplocalizedinfo");
enumTransactionOutcome trProductUnlocalize(ATR_ARGS, NOCONST(ProductContainer) *pProduct,
										   SA_PARAM_NN_STR const char *pLanguageTag)
{
	removeProductLocalization(pProduct, pLanguageTag);

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pProduct, ".Ppkeyvalues");
enumTransactionOutcome trProductSetKeyValue(ATR_ARGS, NOCONST(ProductContainer) *pProduct,
											SA_PARAM_NN_STR const char *pKey,
											SA_PARAM_OP_STR const char *pValue)
{
	NOCONST(AccountProxyKeyValueInfoContainer) *pKeyInfo;

	EARRAY_CONST_FOREACH_BEGIN(pProduct->ppKeyValues, i, size);
		if (!stricmp(pProduct->ppKeyValues[i]->pKey, pKey))
		{
			if (!pValue || !*pValue)
			{
				// If no value was provided, try to remove the key
				pKeyInfo = eaRemove(&pProduct->ppKeyValues, i);

				if (pKeyInfo) StructDestroyNoConst(parse_AccountProxyKeyValueInfoContainer, pKeyInfo);
				return TRANSACTION_OUTCOME_SUCCESS;
			}
			else
			{
				// Replace existing value
				estrCopy2(&pProduct->ppKeyValues[i]->pValue, pValue);
				return TRANSACTION_OUTCOME_SUCCESS;
			}
		}
	EARRAY_FOREACH_END;

	// Add new value
	pKeyInfo = StructCreateNoConst(parse_AccountProxyKeyValueInfoContainer);
	estrCopy2(&pKeyInfo->pKey, pKey);
	estrCopy2(&pKeyInfo->pValue, pValue);
	eaPush(&pProduct->ppKeyValues, pKeyInfo);
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pProduct, ".*");
enumTransactionOutcome trProductReplace(ATR_ARGS, ATR_ALLOW_FULL_LOCK NOCONST(ProductContainer) *pProduct, NON_CONTAINER ProductContainer *pNewProduct)
{
	if (!verify(pProduct)) return TRANSACTION_OUTCOME_FAILURE;
	DECONST(U32, pNewProduct->uID) = pProduct->uID;
	StructReset(parse_ProductContainer, (ProductContainer*)pProduct);
	StructCopy(parse_ProductContainer, pNewProduct, (ProductContainer*)pProduct, 0, 0, 0);
	return TRANSACTION_OUTCOME_SUCCESS;
}


/************************************************************************/
/* Search functions                                                     */
/************************************************************************/

// Find a product by name
const ProductContainer *findProductByName(const char *pName)
{
	const ProductContainer *pProduct = NULL;
	U32 uID = 0;

	PERFINFO_AUTO_START_FUNC();
	if (devassert(stProductsByName))
		stashFindInt(stProductsByName, pName, &uID);

	if (uID)
		pProduct = findProductByID(uID);
	PERFINFO_AUTO_STOP();

	return pProduct;
}

// Find a product by the product's ID
const ProductContainer *findProductByID(U32 uID)
{
	// !!! Cannot call accountClearPermissionsCache because this function is called (directly or indirectly) from accountConstructPermissions

	Container *con = objGetContainer(GLOBALTYPE_ACCOUNTSERVER_PRODUCT, uID);
	if (con) return (ProductContainer *)con->containerData;
	return NULL;
}


/************************************************************************/
/* Modifying functions                                                  */
/************************************************************************/

#define COPY_ESTRING_IF_PRESENT(dst, src) if (src) estrCopy2(dst, src)

// Save a product
bool productSave(SA_PARAM_NN_STR const char *pName,
				 SA_PARAM_NN_STR const char *pInternalName,
				 SA_PARAM_OP_STR const char *pDescription,
				 SA_PARAM_OP_STR const char *pBillingStatementIdentifier,
				 SA_PARAM_OP_STR const char *pShards,
				 SA_PARAM_OP_STR const char *pPermissions,
				 SA_PARAM_OP_STR const char *pRequiredSubs,
				 U32 uAccessLevel,
				 SA_PARAM_OP_STR const char *pCategories,
				 SA_PARAM_OP_STR const char *pKeyValueChanges,
				 SA_PARAM_OP_STR const char *pItemID,
				 SA_PARAM_OP_STR const char *pPrices,
				 SA_PARAM_OP_STR const char *pPrerequisites,
				 U32 uDaysGranted,
				 U32 uFlags,
				 SA_PARAM_OP_STR const char *pInternalSubGranted,
				 SA_PARAM_OP_STR const char *pSubscriptionCategory,
				 SA_PARAM_OP_STR const char *pActivationKeyPrefix,
				 TaxClassification eTaxClassification,
				 U32 uExpireDays,
				 U64 qwOfferID,
				 U8 contentID[XBOX_CONTENTID_SIZE],
				 SA_PARAM_OP_STR const char *pRecruitUpgradedProduct,
				 SA_PARAM_OP_STR const char *pRecruitBilledProduct,
				 SA_PARAM_OP_STR const char *pReferredProduct,
				 bool bRequiresNoSubs,
				 bool bNoSpendingCap)
{
	NOCONST(ProductContainer) *product = CONTAINER_NOCONST(ProductContainer, findProductByName(pName));

	if (product)
	{
		ProductEditRequest *pReq;

		pReq = StructCreate(parse_ProductEditRequest);

		COPY_ESTRING_IF_PRESENT(&pReq->pName, pName);
		COPY_ESTRING_IF_PRESENT(&pReq->pInternalName, pInternalName);
		COPY_ESTRING_IF_PRESENT(&pReq->pDescription, pDescription);
		COPY_ESTRING_IF_PRESENT(&pReq->pBillingStatementIdentifier, pBillingStatementIdentifier);
		COPY_ESTRING_IF_PRESENT(&pReq->pShards, pShards);
		COPY_ESTRING_IF_PRESENT(&pReq->pPermissions, pPermissions);
		COPY_ESTRING_IF_PRESENT(&pReq->pRequiredSubs, pRequiredSubs);
		COPY_ESTRING_IF_PRESENT(&pReq->pCategories, pCategories);
		COPY_ESTRING_IF_PRESENT(&pReq->pKeyValueChanges, pKeyValueChanges);
		COPY_ESTRING_IF_PRESENT(&pReq->pItemID, pItemID);
		COPY_ESTRING_IF_PRESENT(&pReq->pPrices, pPrices);
		COPY_ESTRING_IF_PRESENT(&pReq->pPrerequisites, pPrerequisites);
		COPY_ESTRING_IF_PRESENT(&pReq->pInternalSubGranted, pInternalSubGranted);
		COPY_ESTRING_IF_PRESENT(&pReq->pSubscriptionCategory, pSubscriptionCategory);
		COPY_ESTRING_IF_PRESENT(&pReq->pActivationKeyPrefix, pActivationKeyPrefix);

		pReq->bNoSpendingCap = bNoSpendingCap;
		pReq->bRequiresNoSubs = bRequiresNoSubs;
		pReq->uDaysGranted = uDaysGranted;
		pReq->uFlags = uFlags;
		pReq->uAccessLevel = uAccessLevel;
		pReq->eTaxClassification = eTaxClassification;
		pReq->uExpireDays = uExpireDays;
		pReq->qwOfferID = qwOfferID;
		memcpy(pReq->contentID, contentID, XBOX_CONTENTID_SIZE);

		pReq->pRecruitBilledProduct = strdup(pRecruitBilledProduct);
		pReq->pRecruitUpgradedProduct = strdup(pRecruitUpgradedProduct);
		pReq->pReferredProduct = strdup(pReferredProduct);

		AutoTrans_trProductEdit(objCreateManagedReturnVal(ProductCallback, product), objServerType(), GLOBALTYPE_ACCOUNTSERVER_PRODUCT, product->uID, pReq);

		StructDestroy(parse_ProductEditRequest, pReq);

		accountsClearPermissionsCacheIfProductOwned(pName);
	}
	else
	{
		char buffer[256];

		product = StructCreateNoConst(parse_ProductContainer);
		estrCopy2(&product->pItemID, pItemID);
		estrCopy2(&product->pName, pName);
		estrCopy2(&product->pInternalName, pInternalName);
		estrCopy2(&product->pDescription, pDescription);
		estrCopy2(&product->pBillingStatementIdentifier, pBillingStatementIdentifier);
		estrCopy2(&product->pInternalSubGranted, pInternalSubGranted);
		estrCopy2(&product->pSubscriptionCategory, pSubscriptionCategory);
		estrCopy2(&product->pActivationKeyPrefix, pActivationKeyPrefix);

		product->bNoSpendingCap = bNoSpendingCap;
		product->bRequiresNoSubs = bRequiresNoSubs;
		accountPermissionValueStringParse(pPermissions, NULL, &(AccountPermissionValue **)product->ppPermissions);
		sprintf(buffer, "shard: %s", pShards ? pShards : "");
		accountPermissionValueStringParse(buffer, &product->ppShards, NULL);
		product->uAccessLevel = uAccessLevel;
		product->uFlags = uFlags;
		product->uDaysGranted = uDaysGranted;
		setProductCategories(product, pCategories);
		setProductKeyValueChanges(product, pKeyValueChanges);
		setProductPrices(product, pPrices);
		setProductPrerequisites(product, pPrerequisites);
		product->eTaxClassification = eTaxClassification;
		product->uExpireDays = uExpireDays;
		memcpy(product->xbox.contentId, contentID, XBOX_CONTENTID_SIZE);
		product->xbox.qwOfferID = qwOfferID;

		if (pReferredProduct && *pReferredProduct)
		{
			product->pReferredProduct = strdup(pReferredProduct);
		}

		{
			char *pTempStr = strdup(pRecruitBilledProduct);
			DoVariableListSeparation(&product->recruit.eaBilledProducts, pTempStr, false);
			seaRemoveNULLAndEmpty(&product->recruit.eaBilledProducts);
			free(pTempStr);
			pTempStr = strdup(pRecruitUpgradedProduct);
			DoVariableListSeparation(&product->recruit.eaUpgradedProducts, pTempStr, false);
			seaRemoveNULLAndEmpty(&product->recruit.eaUpgradedProducts);
			free(pTempStr);
		}

		accountPermissionValueStringParse(pRequiredSubs, &product->ppRequiredSubscriptions, NULL);

		objRequestContainerCreateLocal(objCreateManagedReturnVal(ProductCallbackDestroy, product), GLOBALTYPE_ACCOUNTSERVER_PRODUCT, product);
	}

	return true;
}

// Replace a product
bool productReplace(SA_PARAM_NN_VALID ProductContainer *pProduct)
{
	const ProductContainer *pOldProduct = findProductByName(pProduct->pName);

	if (pOldProduct)
	{
		DECONST(U32, pProduct->uID) = pOldProduct->uID;
		AutoTrans_trProductReplace(objCreateManagedReturnVal(ProductCallbackDestroy, pProduct), objServerType(), GLOBALTYPE_ACCOUNTSERVER_PRODUCT, pOldProduct->uID, pProduct);
	}
	else
	{
		DECONST(U32, pProduct->uID) = 0;
		objRequestContainerCreateLocal(objCreateManagedReturnVal(ProductCallbackDestroy, pProduct), GLOBALTYPE_ACCOUNTSERVER_PRODUCT, pProduct);
	}

	return true;
}

void createProductFromStruct(SA_PARAM_NN_VALID ProductContainer *pProduct)
{
	ProductContainer * pNewProduct = StructClone(parse_ProductContainer, pProduct);
	objRequestContainerCreateLocal(objCreateManagedReturnVal(ProductCallbackDestroy, pNewProduct), GLOBALTYPE_ACCOUNTSERVER_PRODUCT, pNewProduct);
}

// Add or replace a localization entry
void productLocalize(U32 uProductID,
					 SA_PARAM_NN_STR const char *pLanguageTag,
					 SA_PARAM_NN_STR const char *pName,
					 SA_PARAM_NN_STR const char *pDescription)
{
	if (strlen(pLanguageTag) < 1) return;
	AutoTrans_trProductLocalize(NULL, objServerType(), GLOBALTYPE_ACCOUNTSERVER_PRODUCT, uProductID, pLanguageTag, pName, pDescription);
	ClearAccountProxyProductCache();
}

// Remove a localization entry
void productUnlocalize(U32 uProductID, SA_PARAM_NN_STR const char *pLanguageTag)
{
	if (strlen(pLanguageTag) < 1) return;
	AutoTrans_trProductUnlocalize(NULL, objServerType(), GLOBALTYPE_ACCOUNTSERVER_PRODUCT, uProductID, pLanguageTag);
	ClearAccountProxyProductCache();
}

// Set a product's key-value
void productSetKeyValue(U32 uProductID, SA_PARAM_NN_STR const char *pKey, SA_PARAM_OP_STR const char *pValue)
{
	AutoTrans_trProductSetKeyValue(NULL, objServerType(), GLOBALTYPE_ACCOUNTSERVER_PRODUCT, uProductID, pKey, pValue);
	ClearAccountProxyProductCache();
}


/************************************************************************/
/* List functions                                                       */
/************************************************************************/

// Get a list of all subscriptions -- DO NOT FREE THE CONTENTS; ONLY eaDestroy IT!
SA_RET_OP_VALID EARRAY_OF(ProductContainer) getProductList(U32 uFlags)
{
	EARRAY_OF(ProductContainer) eaProducts = NULL;
	ContainerIterator iter = {0};
	Container *currCon = NULL;
	U32 uSize;

	uSize = objCountTotalContainersWithType(GLOBALTYPE_ACCOUNTSERVER_PRODUCT);
	objInitContainerIteratorFromType(GLOBALTYPE_ACCOUNTSERVER_PRODUCT, &iter);
	currCon = objGetNextContainerFromIterator(&iter);
	while (currCon)
	{
		ProductContainer *pProduct = (ProductContainer *)currCon->containerData;

		if (!(pProduct->uFlags & PRODUCT_DONT_ASSOCIATE) && uFlags & PRODUCTS_ASSOCIATIVE)
			eaPush(&eaProducts, pProduct);
		else if (pProduct->uFlags & PRODUCT_DONT_ASSOCIATE && uFlags & PRODUCTS_NOT_ASSOCIATIVE)
			eaPush(&eaProducts, pProduct);

		currCon = objGetNextContainerFromIterator(&iter);
	}
	objClearContainerIterator(&iter);

	return eaProducts;
}

// Get a list of product names as an earray of estrings
SA_RET_OP_VALID STRING_EARRAY getProductNameList(U32 uFlags)
{
	STRING_EARRAY eaProductNames = NULL;
	EARRAY_OF(ProductContainer) eaProducts = getProductList(uFlags);

	if (!eaProducts) return eaProductNames;

	EARRAY_CONST_FOREACH_BEGIN(eaProducts, i, s);
		eaPush(&eaProductNames, estrDup(eaProducts[i]->pName));
	EARRAY_FOREACH_END;

	eaDestroy(&eaProducts); // Do not free contents

	return eaProductNames;
}


/************************************************************************/
/* Initialization                                                       */
/************************************************************************/

extern bool gbDoZenConversion;

AUTO_TRANSACTION
ATR_LOCKS(pProduct, ".Ppmoneyprices, .Uflags");
enumTransactionOutcome trProductZenFixup(ATR_ARGS, NOCONST(ProductContainer) *pProduct)
{
	EARRAY_CONST_FOREACH_BEGIN(pProduct->ppMoneyPrices, iPrice, iNumPrices);
	{
		Money *pPrice = moneyContainerToMoney(pProduct->ppMoneyPrices[iPrice]);

		if (isRealCurrency(moneyCurrency(pPrice))) continue;
		moneyZenConvert(pPrice);
	}
	EARRAY_FOREACH_END;

	pProduct->uFlags |= PRODUCT_ZEN_PRICE_FIXED;
	return TRANSACTION_OUTCOME_SUCCESS;
}

// Scan all products and do fixup as appropriate
void scanProducts(void)
{
	if (!gbDoZenConversion) return;

	loadstart_printf("Scanning products...");

	CONTAINER_FOREACH_BEGIN(GLOBALTYPE_ACCOUNTSERVER_PRODUCT, pContainer);
	{
		ProductContainer *pProduct = pContainer->containerData;

		if (!devassert(pProduct)) continue;
		if (pProduct->uFlags & PRODUCT_ZEN_PRICE_FIXED) continue;

		AutoTrans_trProductZenFixup(NULL, GLOBALTYPE_ACCOUNTSERVER, GLOBALTYPE_ACCOUNTSERVER_PRODUCT, pProduct->uID);
	}
	CONTAINER_FOREACH_END;

	loadend_printf("...done.");
}

static void productAddCB(Container *con, ProductContainer *pProduct)
{
	stashAddInt(stProductsByName, pProduct->pName, pProduct->uID, true);
}

static void productRemoveCB(Container *con, ProductContainer *pProduct)
{
	stashRemoveInt(stProductsByName, pProduct->pName, NULL);
}

// Initialize all products
void initializeProducts(void)
{
	objRegisterNativeSchema(GLOBALTYPE_ACCOUNTSERVER_PRODUCT, parse_ProductContainer, NULL, NULL, NULL, NULL, NULL);
	objRegisterContainerTypeAddCallback(GLOBALTYPE_ACCOUNTSERVER_PRODUCT, productAddCB);
	objRegisterContainerTypeRemoveCallback(GLOBALTYPE_ACCOUNTSERVER_PRODUCT, productRemoveCB);
	stProductsByName = stashTableCreateWithStringKeys(256, StashDefault);
	productLoadDefaultPermissions();
}


/************************************************************************/
/* Utility functions                                                    */
/************************************************************************/

// Converts an earray of account permissions into a string
void concatPermissionString(CONST_EARRAY_OF(AccountPermissionValue) values, char **estr)
{
	// !!! Cannot call accountClearPermissionsCache because this function is called (directly or indirectly) from accountConstructPermissions

	int i,size;
	const char *curType = NULL;

	size = eaSize(&values);
	for (i=0; i<size; i++)
	{
		const AccountPermissionValue * value = values[i];
		if (curType && stricmp(curType, value->pType) == 0)
		{
			estrConcatf(estr, ",%s", value->pValue);
		}
		else
		{
			if (curType || (*estr && **estr))
				estrConcatf(estr, ";%s: %s", value->pType, value->pValue);
			else
				estrConcatf(estr, "%s: %s", value->pType, value->pValue);
			curType = value->pType;
		}
	}
}

#define DESC_TCNotApplicable					"Not Applicable"
#define DESC_TCPhysicalGoods					"Physical Good"
#define DESC_TCDownloadableExecutableSoftware	"Downloadable Executable Software"
#define DESC_TCDownloadableElectronicData		"Downloadable Electronic Data"
#define DESC_TCService							"Service"
#define DESC_TCTaxExempt						"Exempt"
#define DESC_TCOtherTaxable						"Other Taxable"

// Convert a string to a tax classification
TaxClassification convertTaxClassificationFromString(SA_PARAM_NN_STR const char *pTaxClassification)
{
	if (!stricmp(pTaxClassification, DESC_TCNotApplicable)) return TCNotApplicable;
	if (!stricmp(pTaxClassification, DESC_TCPhysicalGoods)) return TCPhysicalGoods;
	if (!stricmp(pTaxClassification, DESC_TCDownloadableExecutableSoftware)) return TCDownloadableExecutableSoftware;
	if (!stricmp(pTaxClassification, DESC_TCDownloadableElectronicData)) return TCDownloadableElectronicData;
	if (!stricmp(pTaxClassification, DESC_TCService)) return TCService;
	if (!stricmp(pTaxClassification, DESC_TCTaxExempt)) return TCTaxExempt;
	if (!stricmp(pTaxClassification, DESC_TCOtherTaxable)) return TCOtherTaxable;
	return TCNotApplicable;
}

// Convert a tax classification to a string
SA_RET_OP_STR const char *convertTaxClassificationToString(TaxClassification eClassification)
{
	switch (eClassification)
	{
		xcase TCNotApplicable: return DESC_TCNotApplicable;
		xcase TCPhysicalGoods: return DESC_TCPhysicalGoods;
		xcase TCDownloadableExecutableSoftware: return DESC_TCDownloadableExecutableSoftware;
		xcase TCDownloadableElectronicData: return DESC_TCDownloadableElectronicData;
		xcase TCService: return DESC_TCService;
		xcase TCTaxExempt: return DESC_TCTaxExempt;
		xcase TCOtherTaxable: return DESC_TCOtherTaxable;
	}

	return DESC_TCNotApplicable;
}

// Get an HTTP enum list of tax classifications in the proper order
SA_RET_OP_STR const char *taxClassificationEnumString(void)
{
	return DESC_TCNotApplicable "|" DESC_TCPhysicalGoods "|" DESC_TCDownloadableExecutableSoftware "|" DESC_TCDownloadableElectronicData "|" DESC_TCService "|" DESC_TCTaxExempt "|" DESC_TCOtherTaxable;
}

// Determine if a product grants a permission
bool productGrantsPermission(SA_PARAM_NN_VALID const ProductContainer *pProduct, SA_PARAM_NN_STR const char *pPermission)
{
	if (!verify(pProduct)) return false;
	if (!verify(pPermission && *pPermission)) return false;

	EARRAY_CONST_FOREACH_BEGIN(pProduct->ppPermissions, iCurPermission, iNumPermissions);
	{
		const AccountPermissionValue *pValue = pProduct->ppPermissions[iCurPermission];

		if (!devassert(pValue)) continue;

		if (!stricmp_safe(pValue->pType, pPermission)) return true;
	}
	EARRAY_FOREACH_END;

	return false;
}

// Determine if a product has a subscription listed as required
bool productHasSubscriptionListed(SA_PARAM_NN_VALID const ProductContainer *pProduct,
								  SA_PARAM_NN_STR const char *pSubInternalName)
{
	if (!verify(pProduct)) return false;

	EARRAY_CONST_FOREACH_BEGIN(pProduct->ppRequiredSubscriptions, iCurRequiredSub, iNumReqSubs);
	{
		if (!stricmp_safe(pProduct->ppRequiredSubscriptions[iCurRequiredSub], pSubInternalName))
		{
			return true;
		}
	}
	EARRAY_FOREACH_END;

	return false;
}

// Get the price of a product in a given currency
SA_RET_OP_VALID const Money *getProductPrice(SA_PARAM_NN_VALID const ProductContainer *pProduct,
										  SA_PARAM_NN_STR const char *pCurrency)
{
	if (!verify(pProduct)) return NULL;
	if (!verify(pCurrency)) return NULL;

	EARRAY_CONST_FOREACH_BEGIN(pProduct->ppMoneyPrices, iNumPrices, s);
	{
		if (!strcmpi(moneyCurrency(moneyContainerToMoneyConst(pProduct->ppMoneyPrices[iNumPrices])), pCurrency))
		{
			return moneyContainerToMoneyConst(pProduct->ppMoneyPrices[iNumPrices]);
		}
	}
	EARRAY_FOREACH_END;

	return NULL;
}

static ProductDefaultPermissionList gDefaultPermissionlist = {0};
static char *gpProductDefaultPermFile = "server/Account_DefaultPermissions.txt";
static void productDefaultPermissionsReload(const char *relpath, int when)
{
	if(!stricmp(gpProductDefaultPermFile, relpath))
	{
		productLoadDefaultPermissions();
	}
}

void productLoadDefaultPermissions(void)
{
	static bool sbInitialized = false;
	if(!sbInitialized)
	{
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "server/*.txt", productDefaultPermissionsReload);
		sbInitialized = true;
	}

	StructDeInit(parse_ProductDefaultPermissionList, &gDefaultPermissionlist);
	StructInit(parse_ProductDefaultPermissionList, &gDefaultPermissionlist);
	ParserReadTextFile(gpProductDefaultPermFile, parse_ProductDefaultPermissionList, &gDefaultPermissionlist, 0);
	// Does nothing special on success or failure
	// Cached permissions will be recalculated when they expire
}

ProductDefaultPermission * const * productGetDefaultPermission(void)
{
	return gDefaultPermissionlist.ppProductPermissions;
}

#include "Product_h_ast.c"
#include "Product_c_ast.c"
