#include "DiscountShared.h"

#include "earray.h"
#include "StringUtil.h"
#include "timing.h"

static bool discountShared_BlacklistsProduct(SA_PARAM_OP_STR const char *pDiscountInternalName,
	SA_PARAM_OP_VALID CONST_STRING_EARRAY ppDiscountProducts,
	SA_PARAM_OP_VALID CONST_STRING_EARRAY ppDiscountCategories,
	bool bBlacklistProducts,
	bool bBlacklistCategories,
	SA_PARAM_NN_STR const char *pProductName,
	SA_PARAM_NN_STR const char *pProductInternalName,
	SA_PARAM_NN_VALID CONST_STRING_EARRAY ppProductCategories)
{
	PERFINFO_AUTO_START_FUNC();

	// Does the internal product match? If not, the product is blacklisted.
	if (pDiscountInternalName && stricmp_safe(pProductInternalName, pDiscountInternalName))
	{
		PERFINFO_AUTO_STOP();
		return true;
	}

	// Is the product list a blacklist, and does it list this product? If so, the product is blacklisted.
	if (bBlacklistProducts)
	{
		if (eaFindString(&ppDiscountProducts, pProductName) > -1)
		{
			PERFINFO_AUTO_STOP();
			return true;
		}
	}

	// Is the category list a blacklist, and does it list any of this product's categories? If so, the product is blacklisted.
	if (bBlacklistCategories)
	{
		EARRAY_CONST_FOREACH_BEGIN(ppDiscountCategories, i, iNumCategories);
		{
			const char *pCategory = ppDiscountCategories[i];

			if (!devassert(pCategory)) continue;

			if (eaFindString(&ppProductCategories, pCategory) > -1)
			{
				PERFINFO_AUTO_STOP();
				return true;
			}
		}
		EARRAY_FOREACH_END;
	}

	// Otherwise, the product is not blacklisted.
	PERFINFO_AUTO_STOP();
	return false;
}

static bool discountShared_WhitelistsProduct(SA_PARAM_OP_STR const char *pDiscountInternalName,
	SA_PARAM_OP_VALID CONST_STRING_EARRAY ppDiscountProducts,
	SA_PARAM_OP_VALID CONST_STRING_EARRAY ppDiscountCategories,
	bool bBlacklistProducts,
	bool bBlacklistCategories,
	SA_PARAM_NN_STR const char *pProductName,
	SA_PARAM_NN_VALID CONST_STRING_EARRAY ppProductCategories)
{
	PERFINFO_AUTO_START_FUNC();

	// Is either list a whitelist? If not, the product is whitelisted.
	if ((!ppDiscountCategories || bBlacklistCategories) &&
		(!ppDiscountProducts || bBlacklistProducts))
	{
		PERFINFO_AUTO_STOP();
		return true;
	}

	// After we get through the above if statement, the following must be true:
	// (ppDiscountCategories && !bBlacklistCategories) || (ppDiscountProducts && !bBlacklistProducts)
	// In other words, at least one of the two lists must be a whitelist with entries

	// Is the product list a whitelist, and does it list this product? If so, the product is whitelisted.
	if (!bBlacklistProducts)
	{
		if (eaFindString(&ppDiscountProducts, pProductName) > -1)
		{
			PERFINFO_AUTO_STOP();
			return true;
		}
	}

	// Is the category list a whitelist, and does it list this product? If so, the product is whitelisted.
	if (!bBlacklistCategories)
	{
		EARRAY_CONST_FOREACH_BEGIN(ppDiscountCategories, i, iNumCategories);
		{
			const char *pCategory = ppDiscountCategories[i];

			if (eaFindString(&ppProductCategories, pCategory) > -1)
			{
				PERFINFO_AUTO_STOP();
				return true;
			}
		}
		EARRAY_FOREACH_END;
	}

	// Otherwise, the product is not whitelisted.
	PERFINFO_AUTO_STOP();
	return false;
}

bool DiscountShared_AppliesToProduct(SA_PARAM_OP_STR const char *pDiscountInternalName,
	SA_PARAM_OP_VALID CONST_STRING_EARRAY ppDiscountProducts,
	SA_PARAM_OP_VALID CONST_STRING_EARRAY ppDiscountCategories,
	bool bBlacklistProducts,
	bool bBlacklistCategories,
	SA_PARAM_NN_STR const char *pProductName,
	SA_PARAM_NN_STR const char *pProductInternalName,
	SA_PARAM_NN_VALID CONST_STRING_EARRAY ppProductCategories)
{
	return !discountShared_BlacklistsProduct(pDiscountInternalName, ppDiscountProducts, ppDiscountCategories, bBlacklistProducts, bBlacklistCategories, pProductName, pProductInternalName, ppProductCategories)
		&& discountShared_WhitelistsProduct(pDiscountInternalName, ppDiscountProducts, ppDiscountCategories, bBlacklistProducts, bBlacklistCategories, pProductName, ppProductCategories);
}