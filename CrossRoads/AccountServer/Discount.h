#pragma once
#include "Discount_h_ast.h"

typedef struct AccountInfo AccountInfo;
typedef struct ProductContainer ProductContainer;
typedef struct AccountProxyKeyValueInfoList AccountProxyKeyValueInfoList;

/************************************************************************/
/* Types                                                                */
/************************************************************************/

AST_PREFIX(PERSIST);
AUTO_STRUCT AST_CONTAINER;
typedef struct DiscountContainer
{
	const U32 uID; AST(KEY)
	CONST_STRING_MODIFIABLE pName;
	CONST_STRING_MODIFIABLE pCurrency;				// _CrypticPoints
	CONST_STRING_MODIFIABLE pProductInternalName;	// FightClub or StarTrek
	const U32 uPercentageDiscount;					// 3000 for 30%
	const bool bEnabled;
	CONST_STRING_MODIFIABLE pPrereqsInfix;			// Infix notation of prereqs
	CONST_STRING_EARRAY eaPrereqs;	AST(ESTRING)	// Reverse Polish Notation prerequisites
	const U32 uCreatedSS2000;
	CONST_STRING_MODIFIABLE pCreatedBy;
	CONST_STRING_EARRAY eaProducts;	AST(ESTRING)	// Array of products to discount
	const bool bBlacklistProducts;					// Treat the product list as a blacklist?
	CONST_STRING_EARRAY eaCategories; AST(ESTRING)	// Array of categories to discount
	const bool bBlacklistCategories;				// Treat the category list as a blacklist?
	const U32 uStartSS2000;
	const U32 uEndSS2000;

	AST_COMMAND("Apply Transaction","ServerMonTransactionOnEntity AccountServerDiscount $FIELD(UID) $STRING(Transaction String)")
} DiscountContainer;
AST_PREFIX();


/************************************************************************/
/* Modification                                                         */
/************************************************************************/

// Will replace any existing with the same currency, product internal name, and prereqs
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
				  SA_PARAM_OP_STR const char *pCreatedBy);

void setDiscountEnabled(U32 uID, bool bEnabled);

bool deleteDiscount(U32 uID);


/************************************************************************/
/* Listings                                                             */
/************************************************************************/

// Returns an earray pointer that should be destroyed using the function below
EARRAY_OF(DiscountContainer) getAllDiscounts(void);
EARRAY_OF(DiscountContainer) getActiveDiscounts(void);
EARRAY_OF(DiscountContainer) getScheduledDiscounts(void);
EARRAY_OF(DiscountContainer) getInactiveDiscounts(void);

bool discountIsActive(SA_PARAM_NN_VALID const DiscountContainer *pDiscount);
bool discountAppliesToProduct(SA_PARAM_NN_VALID const ProductContainer *pProduct,
								SA_PARAM_NN_VALID const DiscountContainer *pDiscount);

// Used to free the earray returned from above
void freeDiscountsArray(SA_PRE_NN_VALID SA_POST_FREE EARRAY_OF(DiscountContainer) *eaDiscounts);

// Returns percentage of discount for a particular product (optionally account-specific)
U32 getProductDiscountPercentage(SA_PARAM_OP_VALID AccountInfo *pAccount,
								 U32 uProductID,
								 SA_PARAM_NN_STR const char *pCurrency,
								 SA_PARAM_OP_VALID AccountProxyKeyValueInfoList *pKVList,
								 SA_PARAM_OP_STR const char *pProxy,
								 SA_PARAM_OP_STR const char *pCluster,
								 SA_PARAM_OP_STR const char *pEnvironment);

/************************************************************************/
/* Initialization                                                       */
/************************************************************************/

// Initialize all discounts
void initializeDiscounts(void);