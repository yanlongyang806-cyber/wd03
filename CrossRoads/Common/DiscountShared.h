#pragma once

#include "stdtypes.h"

#define DiscountShared_HasStarted(discount, now)	(!discount->uStartSS2000 || now >= discount->uStartSS2000)
#define DiscountShared_HasEnded(discount, now)		(discount->uEndSS2000 && now >= discount->uEndSS2000)
#define DiscountShared_IsActive(discount, now)		(discount->bEnabled && DiscountShared_HasStarted(discount, now) && !DiscountShared_HasEnded(discount, now))

bool DiscountShared_AppliesToProduct(SA_PARAM_OP_STR const char *pDiscountInternalName,
	SA_PARAM_OP_VALID CONST_STRING_EARRAY ppDiscountProducts,
	SA_PARAM_OP_VALID CONST_STRING_EARRAY ppDiscountCategories,
	bool bBlacklistProducts,
	bool bBlacklistCategories,
	SA_PARAM_NN_STR const char *pProductName,
	SA_PARAM_NN_STR const char *pProductInternalName,
	SA_PARAM_NN_VALID CONST_STRING_EARRAY ppProductCategories);