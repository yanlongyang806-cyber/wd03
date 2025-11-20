#include "Discount.h"

#include "DiscountShared.h"
#include "objSchema.h"
#include "objTransactions.h"
#include "timing.h"
#include "AutoTransDefs.h"
#include "AutoGen/AccountServer_autotransactions_autogen_wrappers.h"
#include "objContainer.h"
#include "StringUtil.h"
#include "rpn.h"
#include "AccountServer.h"
#include "ProxyInterface/AccountProxy.h"
#include "Product.h"
#include "TimedCallback.h"

// Consider 7 days (in seconds) to be the interesting threshold of scheduled discounts
#define DISCOUNT_SCHEDULE_THRESHOLD (7 * SECONDS_PER_DAY)

/************************************************************************/
/* Transactions                                                         */
/************************************************************************/

AUTO_TRANSACTION
ATR_LOCKS(pDiscount, ".Benabled");
enumTransactionOutcome trSetDiscountEnabled(ATR_ARGS, NOCONST(DiscountContainer) *pDiscount,
											int bEnabled)
{
	if (!verify(NONNULL(pDiscount))) return TRANSACTION_OUTCOME_FAILURE;

	pDiscount->bEnabled = bEnabled;

	return TRANSACTION_OUTCOME_SUCCESS;
}

/************************************************************************/
/* Modification                                                         */
/************************************************************************/

static void saveDiscount_CB(SA_PARAM_NN_VALID TransactionReturnVal *returnVal, void *pUserData)
{
	// Only needed when a discount actually changes, because the shard will already know the
	// start/end timestamps and whether or not the discount is enabled
	ClearAccountProxyDiscountCache();
}

// Will replace any existing with the same currency, product internal name, and prereqs
// (NO IT WON'T THIS IS LIES)
bool saveDiscount(SA_PARAM_OP_STR const char *pName,
				  SA_PARAM_NN_STR const char *pCurrency,
				  SA_PARAM_OP_STR const char *pProductInternalName,
				  SA_PARAM_NN_STR const char *pPrereqsInfix,
				  SA_PARAM_NN_STR const char *pProducts,
				  bool bBlacklistProducts,
				  SA_PARAM_NN_STR const char *pCategories,
				  bool bBlacklistCategories,
				  U32 uStartSS2000,
				  U32 uEndSS2000,
				  U32 uPercentageDiscount,
				  SA_PARAM_OP_STR const char *pCreatedBy)
{
	NOCONST(DiscountContainer) discount = {0};

	if (!verify(pCurrency && *pCurrency)) return false;
	if (!verify(pPrereqsInfix && *pPrereqsInfix)) return false;
	if (!verify(uPercentageDiscount)) return false;

	PERFINFO_AUTO_START_FUNC();

	if (pProductInternalName && !*pProductInternalName) pProductInternalName = NULL;

	StructInitNoConst(parse_DiscountContainer, &discount);

	discount.pCurrency = strdup(pCurrency);
	discount.pProductInternalName = pProductInternalName ? strdup(pProductInternalName) : NULL;
	discount.uPercentageDiscount = uPercentageDiscount;
	discount.pPrereqsInfix = strdup(pPrereqsInfix);
	discount.bEnabled = true;
	discount.pName = pName && *pName ? strdup(pName) : NULL;

	infixToRPN(pPrereqsInfix, &discount.eaPrereqs, NULL);

	DivideString(pProducts, ",", &discount.eaProducts,
		DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS|DIVIDESTRING_POSTPROCESS_REMOVEUNIQUE|
		DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE|DIVIDESTRING_POSTPROCESS_ESTRINGS);
	discount.bBlacklistProducts = bBlacklistProducts;

	DivideString(pCategories, ",", &discount.eaCategories,
		DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS|DIVIDESTRING_POSTPROCESS_REMOVEUNIQUE|
		DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE|DIVIDESTRING_POSTPROCESS_ESTRINGS);
	discount.bBlacklistCategories = bBlacklistCategories;

	discount.uStartSS2000 = uStartSS2000;
	discount.uEndSS2000 = uEndSS2000;

	discount.uCreatedSS2000 = timeSecondsSince2000();
	discount.pCreatedBy = pCreatedBy ? strdup(pCreatedBy) : NULL;

	objRequestContainerCreateLocal(objCreateManagedReturnVal(saveDiscount_CB, NULL), GLOBALTYPE_ACCOUNTSERVER_DISCOUNT, &discount);

	StructDeInitNoConst(parse_DiscountContainer, &discount);

	PERFINFO_AUTO_STOP_FUNC();

	return true;
}

void setDiscountEnabled(U32 uID, bool bEnabled)
{
	AutoTrans_trSetDiscountEnabled(objCreateManagedReturnVal(saveDiscount_CB, NULL), objServerType(), GLOBALTYPE_ACCOUNTSERVER_DISCOUNT, uID, bEnabled);
}

bool deleteDiscount(U32 uID)
{
	objRequestContainerDestroyLocal(objCreateManagedReturnVal(saveDiscount_CB, NULL), GLOBALTYPE_ACCOUNTSERVER_DISCOUNT, uID);
	return true;
}


/************************************************************************/
/* Listings                                                             */
/************************************************************************/

typedef bool (*DiscountCriteriaFunction)(const DiscountContainer *pDiscount);

bool discountIsActive(SA_PARAM_NN_VALID const DiscountContainer *pDiscount)
{
	U32 uNow = timeSecondsSince2000();
	return DiscountShared_IsActive(pDiscount, uNow);
}

static bool discountIsScheduled(SA_PARAM_NN_VALID const DiscountContainer *pDiscount)
{
	U32 uNow = timeSecondsSince2000();

	if (!pDiscount->bEnabled) return false;
	if (DiscountShared_HasStarted(pDiscount, uNow)) return false;
	if ((pDiscount->uStartSS2000 - uNow) <= DISCOUNT_SCHEDULE_THRESHOLD) return true;

	return false;
}

static bool discountIsInactive(SA_PARAM_NN_VALID const DiscountContainer *pDiscount)
{
	U32 uNow = timeSecondsSince2000();

	if (!pDiscount->bEnabled) return true;
	if (!DiscountShared_HasStarted(pDiscount, uNow) && (pDiscount->uStartSS2000 - uNow) > DISCOUNT_SCHEDULE_THRESHOLD) return true;
	if (DiscountShared_HasEnded(pDiscount, uNow)) return true;

	return false;
}

static EARRAY_OF(DiscountContainer) getDiscountsWithCriteria(SA_PARAM_OP_VALID DiscountCriteriaFunction pFunc)
{
	EARRAY_OF(DiscountContainer) eaDiscounts = NULL;

	PERFINFO_AUTO_START_FUNC();

	CONTAINER_FOREACH_BEGIN(GLOBALTYPE_ACCOUNTSERVER_DISCOUNT, container);
	{
		DiscountContainer *pDiscount = container ? container->containerData : NULL;

		if (!devassert(pDiscount)) continue;
		if (pFunc && !(*pFunc)(pDiscount)) continue;

		eaPush(&eaDiscounts, pDiscount);
	}
	CONTAINER_FOREACH_END;

	PERFINFO_AUTO_STOP();

	return eaDiscounts;
}

// Returns an earray pointer that should be destroyed using the function below
EARRAY_OF(DiscountContainer) getAllDiscounts(void)
{
	return getDiscountsWithCriteria(NULL);
}

EARRAY_OF(DiscountContainer) getActiveDiscounts(void)
{
	return getDiscountsWithCriteria(discountIsActive);
}

EARRAY_OF(DiscountContainer) getScheduledDiscounts(void)
{
	return getDiscountsWithCriteria(discountIsScheduled);
}

EARRAY_OF(DiscountContainer) getInactiveDiscounts(void)
{
	return getDiscountsWithCriteria(discountIsInactive);
}

// Used to free the earray returned from above
void freeDiscountsArray(SA_PRE_NN_VALID SA_POST_FREE EARRAY_OF(DiscountContainer) *eaDiscounts)
{
	eaDestroy(eaDiscounts); // Not eaDestroyStruct!
}

bool discountAppliesToProduct(SA_PARAM_NN_VALID const ProductContainer *pProduct,
								SA_PARAM_NN_VALID const DiscountContainer *pDiscount)
{
	return DiscountShared_AppliesToProduct(pDiscount->pProductInternalName, pDiscount->eaProducts, pDiscount->eaCategories,
		pDiscount->bBlacklistProducts, pDiscount->bBlacklistCategories, pProduct->pName, pProduct->pInternalName, pProduct->ppCategories);
}

U32 getProductDiscountPercentage(AccountInfo *pAccount,
								 U32 uProductID,
								 const char *pCurrency,
								 AccountProxyKeyValueInfoList *pKVList,
								 const char *pProxy,
								 const char *pCluster,
								 const char *pEnvironment)
{
	U32 uPercentage = 0;
	const ProductContainer *pProduct;

	PERFINFO_AUTO_START_FUNC();
	pProduct = findProductByID(uProductID);

	CONTAINER_FOREACH_BEGIN(GLOBALTYPE_ACCOUNTSERVER_DISCOUNT, container);
	{
		DiscountContainer *pDiscount = container ? container->containerData : NULL;

		if (!devassert(pDiscount)) continue;
		if (!discountIsActive(pDiscount)) continue;
		if (stricmp_safe(pCurrency, pDiscount->pCurrency)) continue;
		if (!discountAppliesToProduct(pProduct, pDiscount)) continue;
		if (!AccountProxyKeysMeetRequirements(pKVList, pDiscount->eaPrereqs, pProxy, pCluster, pEnvironment, NULL, NULL)) continue;

		uPercentage = MAX(uPercentage, pDiscount->uPercentageDiscount);
	}
	CONTAINER_FOREACH_END;
	PERFINFO_AUTO_STOP();

	return uPercentage;
}

/************************************************************************/
/* Initialization                                                       */
/************************************************************************/

// Initialize all discounts
void initializeDiscounts(void)
{
	objRegisterNativeSchema(GLOBALTYPE_ACCOUNTSERVER_DISCOUNT, parse_DiscountContainer, NULL, NULL, NULL, NULL, NULL);
}

#include "Discount_h_ast.c"