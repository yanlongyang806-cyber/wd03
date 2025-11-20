#include "AccountDataCache.h"

#include "accountnet.h"
#include "DiscountShared.h"
#include "Money.h"
#include "ResourceInfo.h"
#include "StashTable.h"
#include "timing.h"

#include "AccountDataCache_h_ast.h"

static U32 uAccountDataCacheProductsTime = MINUTES(1);
// Number of seconds the AccountDataCache products cache is considered "fresh"
AUTO_CMD_INT(uAccountDataCacheProductsTime, AccountDataCacheProductsTime) ACMD_CMDLINE;

static U32 uProductLastCacheTime = 0;
static U32 uProductCacheVersion = 0;
static U32 uCurAccountDataCacheProductsTimeout = 0;

U32 ADCGetProductCacheTime(void)
{
	return uProductLastCacheTime;
}

U32 ADCGetProductCacheVersion(void)
{
	return uProductCacheVersion;
}

// NEVER call these two functions from inside this file
// The idea is that the freshness of the cache only matters to users of the module - we don't care
// If the user never wants to bother with the freshness tracking, we should permit that
bool ADCProductsAreFresh(void)
{
	if (uCurAccountDataCacheProductsTimeout)
		return timeSecondsSince2000() < uCurAccountDataCacheProductsTimeout;
	else
		return true;
}

bool ADCProductsAreUpToDate(U32 uTimestamp, U32 uVersion)
{
	return (uTimestamp >= uProductLastCacheTime && uVersion >= uProductCacheVersion);
}

void ADCFreshenProducts(U32 uOverrideTimeout)
{
	if (uOverrideTimeout)
		uCurAccountDataCacheProductsTimeout = timeSecondsSince2000() + uOverrideTimeout;
	else if (uAccountDataCacheProductsTime)
		uCurAccountDataCacheProductsTimeout = timeSecondsSince2000() + uAccountDataCacheProductsTime;
	else
		uCurAccountDataCacheProductsTimeout = 0;
}

void ADCStaleProducts(void)
{
	uCurAccountDataCacheProductsTimeout = timeSecondsSince2000();
}

static AccountProxyProductList *spAllProductsList = NULL;

SA_RET_NN_VALID static StashTable adc_GetProductCacheTable(void)
{
	static StashTable table = NULL;

	if (!table) table = stashTableCreateWithStringKeys(1, StashDefault);
	return table;
}

const AccountProxyProductList *ADCGetProductsByCategory(const char *pCategory)
{
	AccountProxyProductList *pList = NULL;
	stashFindPointer(adc_GetProductCacheTable(), pCategory, &pList);
	return pList;
}

const AccountProxyProductList *ADCGetProducts(void)
{
	return spAllProductsList;
}

static void adc_AddProductToGlobalList(SA_PARAM_NN_VALID AccountProxyProduct *pProduct, U32 uTimestamp, U32 uVersion)
{
	if (!spAllProductsList)
	{
		spAllProductsList = StructCreate(parse_AccountProxyProductList);
		spAllProductsList->uTimestamp = uTimestamp;
		spAllProductsList->uVersion = uVersion;
	}

	eaPush(&spAllProductsList->ppList, pProduct);
}

static void adc_AddProductToCache(SA_PARAM_NN_STR const char *pCategory, SA_PARAM_NN_VALID AccountProxyProduct *pProduct, U32 uTimestamp, U32 uVersion)
{
	StashTable table = adc_GetProductCacheTable();
	AccountProxyProductList *pList = NULL;

	if (!stashFindPointer(table, pCategory, &pList))
	{
		pList = StructCreate(parse_AccountProxyProductList);
		pList->uTimestamp = uTimestamp;
		pList->uVersion = uVersion;
		stashReplaceStruct(table, pCategory, pList, parse_AccountProxyProductList);
	}

	eaPush(&pList->ppList, pProduct);
}

bool ADCReplaceProductCache(const AccountProxyProductList *pList)
{
	StashTable table = adc_GetProductCacheTable();

	// If this list is from an old timestamp or an old version, it's not new, so don't use it
	if (!pList || (pList->uTimestamp <= uProductLastCacheTime && pList->uVersion <= uProductCacheVersion))
		return false;

	PERFINFO_AUTO_START_FUNC();
	ADCClearProductCache();
	uProductLastCacheTime = pList->uTimestamp;
	uProductCacheVersion = pList->uVersion;

	EARRAY_CONST_FOREACH_BEGIN(pList->ppList, i, s1);
	{
		AccountProxyProduct *product = pList->ppList[i];

		adc_AddProductToGlobalList(StructClone(parse_AccountProxyProduct, product), pList->uTimestamp, pList->uVersion);

		EARRAY_CONST_FOREACH_BEGIN(product->ppCategories, j, s2);
		{
			adc_AddProductToCache(product->ppCategories[j], StructClone(parse_AccountProxyProduct, product), pList->uTimestamp, pList->uVersion);
		}
		EARRAY_FOREACH_END;
	}
	EARRAY_FOREACH_END;

	PERFINFO_AUTO_STOP();
	return true;
}

void ADCClearProductCache(void)
{
	static bool sbInitialized = false;
	stashClearStruct(adc_GetProductCacheTable(), parse_AccountProxyProductList);
	StructDestroySafe(parse_AccountProxyProductList, &spAllProductsList);

	if (!sbInitialized)
	{
		resRegisterDictionaryForStashTable("Product List", RESCATEGORY_OTHER, 0, adc_GetProductCacheTable(), parse_AccountProxyProductList);
		sbInitialized = true;
	}
}


static U32 uAccountDataCacheDiscountsTime = MINUTES(1);
// Number of seconds the AccountDataCache discounts cache is considered "fresh"
AUTO_CMD_INT(uAccountDataCacheDiscountsTime, AccountDataCacheDiscountsTime) ACMD_CMDLINE;

static U32 uDiscountLastCacheTime = 0;
static U32 uDiscountCacheVersion = 0;
static U32 uCurAccountDataCacheDiscountsTimeout = 0;

U32 ADCGetDiscountCacheTime(void)
{
	return uDiscountLastCacheTime;
}

U32 ADCGetDiscountCacheVersion(void)
{
	return uDiscountCacheVersion;
}

// NEVER call these two functions from inside this file
// The idea is that the freshness of the cache only matters to users of the module - we don't care
// If the user never wants to bother with the freshness tracking, we should permit that
bool ADCDiscountsAreFresh(void)
{
	if (uCurAccountDataCacheDiscountsTimeout)
		return timeSecondsSince2000() < uCurAccountDataCacheDiscountsTimeout;
	else
		return true;
}

bool ADCDiscountsAreUpToDate(U32 uTimestamp, U32 uVersion)
{
	return (uTimestamp >= uDiscountLastCacheTime && uVersion >= uDiscountCacheVersion);
}

void ADCFreshenDiscounts(U32 uOverrideTimeout)
{
	if (uOverrideTimeout)
		uCurAccountDataCacheDiscountsTimeout = timeSecondsSince2000() + uOverrideTimeout;
	else if (uAccountDataCacheDiscountsTime)
		uCurAccountDataCacheDiscountsTimeout = timeSecondsSince2000() + uAccountDataCacheDiscountsTime;
	else
		uCurAccountDataCacheDiscountsTimeout = 0;
}

void ADCStaleDiscounts(void)
{
	uCurAccountDataCacheDiscountsTimeout = timeSecondsSince2000();
}

SA_RET_NN_VALID static StashTable adc_GetDiscountCacheTable(void)
{
	static StashTable sDiscountCache = NULL;

	if (!sDiscountCache)
	{
		sDiscountCache = stashTableCreateWithStringKeys(1, StashDefault);
	}

	return sDiscountCache;
}

// NOT SAFE to use with more than one discount at a time, obviously
SA_RET_NN_VALID static const char *adc_MakeAccountDiscountKey(SA_PARAM_NN_VALID const AccountProxyDiscount *pDiscount)
{
	static char *pKey = NULL;
	estrPrintf(&pKey, "Accountserver_Discount_%d", pDiscount->uID);
	return pKey;
}

static void adc_AddDiscountToCache(SA_PARAM_NN_VALID AccountProxyDiscount *pDiscount)
{
	StashTable pDiscountCache = adc_GetDiscountCacheTable();
	const char *pKey = adc_MakeAccountDiscountKey(pDiscount);
	stashReplaceStruct(pDiscountCache, pKey, pDiscount, parse_AccountProxyDiscount);
}

int adc_DiscountComparator(const AccountProxyDiscount **ppLeft, const AccountProxyDiscount **ppRight)
{
	if ((!ppLeft || !*ppLeft) && (!ppRight || !*ppRight)) return 0;
	else if (!ppLeft || !*ppLeft) return 1;
	else if (!ppRight || !*ppRight) return -1;
	else return (*ppRight)->uPercentageDiscount - (*ppLeft)->uPercentageDiscount;
}

const AccountProxyDiscountList *ADCGetDiscounts(void)
{
	StashTable pDiscountCache = adc_GetDiscountCacheTable();
	static AccountProxyDiscountList *pList = NULL;

	if (!pList)
	{
		pList = StructCreate(parse_AccountProxyDiscountList);
	}

	pList->uTimestamp = uDiscountLastCacheTime;
	pList->uVersion = uDiscountCacheVersion;
	eaClear(&pList->ppList);

	FOR_EACH_IN_STASHTABLE(pDiscountCache, AccountProxyDiscount, pDiscount)
	{
		eaPush(&pList->ppList, pDiscount);
	}
	FOR_EACH_END

	eaQSort(pList->ppList, adc_DiscountComparator);

	return pList;
}

void ADCClearDiscountCache(void)
{
	StashTable pDiscountCache = adc_GetDiscountCacheTable();
	stashClearStruct(pDiscountCache, parse_AccountProxyDiscount);
}

bool ADCReplaceDiscountCache(const AccountProxyDiscountList *pList)
{
	StashTable pDiscountCache = adc_GetDiscountCacheTable();

	if (!pList || (pList->uTimestamp <= uDiscountLastCacheTime && pList->uVersion <= uDiscountCacheVersion))
		return false;

	PERFINFO_AUTO_START_FUNC();
	ADCClearDiscountCache();
	uDiscountLastCacheTime = pList->uTimestamp;
	uDiscountCacheVersion = pList->uVersion;

	EARRAY_CONST_FOREACH_BEGIN(pList->ppList, i, s);
	{
		AccountProxyDiscount *pDiscount = pList->ppList[i];
		adc_AddDiscountToCache(StructClone(parse_AccountProxyDiscount, pDiscount));
	}
	EARRAY_FOREACH_END;

	PERFINFO_AUTO_STOP();
	return true;
}

bool ADCDiscountAppliesToProduct(const AccountProxyProduct *pProduct, const AccountProxyDiscount *pDiscount)
{
	return DiscountShared_AppliesToProduct(pDiscount->pInternalName, pDiscount->ppProducts, pDiscount->ppCategories, pDiscount->bBlacklistProducts,
		pDiscount->bBlacklistCategories, pProduct->pName, pProduct->pInternalName, pProduct->ppCategories);
}

#include "AccountDataCache_h_ast.c"