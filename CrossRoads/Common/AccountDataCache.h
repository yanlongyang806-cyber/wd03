#pragma once

#include "stdtypes.h"

typedef struct AccountProxyKeyValueInfo AccountProxyKeyValueInfo;
typedef struct Money Money;

/************************************************************************/
/* Account product caching                                              */
/************************************************************************/

#define XBOX_CONTENTID_SIZE 20
#define XBOX_CONTENTID_DEFAULT "0000000000000000000000000000000000000000" // in hex

AUTO_STRUCT;
typedef struct AccountProxyProductLocalizedInfo
{
	char *pLanguageTag;	AST(ESTRING) // IETF language tag
	char *pName;		AST(ESTRING) // Product name (UTF-8)
	char *pDescription;	AST(ESTRING) // Product description (UTF-8)
} AccountProxyProductLocalizedInfo;

AUTO_STRUCT;
typedef struct AccountProxyProduct
{
	U32 uID;
	char *pName;				   AST(ESTRING)
	char *pInternalName;		   AST(ESTRING)
	char *pDescription;			   AST(ESTRING)
	STRING_EARRAY ppCategories;	   AST(ESTRING)
	EARRAY_OF(Money) ppMoneyPrices;
	EARRAY_OF(Money) ppFullMoneyPrices;
	char *pItemID;				   AST(ESTRING) // Free-form string
	STRING_EARRAY ppPrerequisites; AST(ESTRING)
	EARRAY_OF(AccountProxyKeyValueInfo) ppKeyValues;
	EARRAY_OF(AccountProxyProductLocalizedInfo) ppLocalizedInfo;

	// XBOX Marketplace data
	U64	qwOfferID;	// Refer to XMARKETPLACE_CONTENTOFFER_INFO
	U8	contentId[XBOX_CONTENTID_SIZE]; // Refer to XMARKETPLACE_CONTENTOFFER_INFO
} AccountProxyProduct;

AUTO_STRUCT;
typedef struct AccountProxyProductList
{
	EARRAY_OF(AccountProxyProduct) ppList;
	U32 uTimestamp;
	U32 uVersion;
} AccountProxyProductList;

bool ADCProductsAreFresh(void);
bool ADCProductsAreUpToDate(U32 uTimestamp, U32 uVersion);
void ADCFreshenProducts(U32 uOverrideTimeout);
void ADCStaleProducts(void); // Make the cache stale

SA_RET_OP_VALID const AccountProxyProductList *ADCGetProductsByCategory(SA_PARAM_NN_STR const char *pCategory);
SA_RET_OP_VALID const AccountProxyProductList *ADCGetProducts(void);
bool ADCReplaceProductCache(SA_PARAM_OP_VALID const AccountProxyProductList *pList);
void ADCClearProductCache(void);
U32 ADCGetProductCacheVersion(void);
U32 ADCGetProductCacheTime(void);

/************************************************************************/
/* Account discount caching                                             */
/************************************************************************/

AUTO_STRUCT;
typedef struct AccountProxyDiscount
{
	U32 uID;
	char *pName;					AST(ESTRING)
	char *pInternalName;			AST(ESTRING)
	char *pCurrency;				AST(ESTRING)
	U32 uPercentageDiscount;

	bool bEnabled;
	U32 uStartSS2000;
	U32 uEndSS2000;

	STRING_EARRAY ppPrerequisites;	AST(ESTRING)
	STRING_EARRAY ppProducts;		AST(ESTRING)
	bool bBlacklistProducts;
	STRING_EARRAY ppCategories;		AST(ESTRING)
	bool bBlacklistCategories;
} AccountProxyDiscount;

AUTO_STRUCT;
typedef struct AccountProxyDiscountList
{
	EARRAY_OF(AccountProxyDiscount) ppList;
	U32 uTimestamp;
	U32 uVersion;
} AccountProxyDiscountList;

bool ADCDiscountsAreFresh(void);
bool ADCDiscountsAreUpToDate(U32 uTimestamp, U32 uVersion);
void ADCFreshenDiscounts(U32 uOverrideTimeout);
void ADCStaleDiscounts(void); // Make the cache stale

SA_RET_NN_VALID const AccountProxyDiscountList *ADCGetDiscounts(void);
void ADCClearDiscountCache(void);
bool ADCReplaceDiscountCache(SA_PARAM_OP_VALID const AccountProxyDiscountList *pDiscountList);
U32 ADCGetDiscountCacheVersion(void);
U32 ADCGetDiscountCacheTime(void);

/************************************************************************/
/* Discount application / manipulation                                  */
/************************************************************************/

bool ADCDiscountAppliesToProduct(SA_PARAM_NN_VALID const AccountProxyProduct *pProduct, SA_PARAM_NN_VALID const AccountProxyDiscount *pDiscount);