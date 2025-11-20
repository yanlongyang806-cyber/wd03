#ifndef PRODUCT_H
#define PRODUCT_H

#include "AccountDataCache.h"

/************************************************************************/
/* Forward declarations                                                 */
/************************************************************************/

typedef struct LegacyProductKeyGroup LegacyProductKeyGroup;
typedef struct LegacyProductKeyBatch LegacyProductKeyBatch;
typedef struct AccountPermission AccountPermission;
typedef struct AccountInfo AccountInfo;
typedef struct AccountPermissionValue AccountPermissionValue;
typedef struct PriceContainer PriceContainer;
typedef struct MoneyContainer MoneyContainer;
typedef struct AccountProxyKeyValueInfoContainer AccountProxyKeyValueInfoContainer;


/************************************************************************/
/* Types                                                                */
/************************************************************************/

#define PRODUCT_EXPORT_VERSION 1 // Increment whenever the product structure changes

// Flags used for products
#define PRODUCT_DEPRECATED_1	BIT(0) // Deprecated flag - DO NOT REUSE!!! This exists in some environments
#define PRODUCT_BILLING_SYNC	BIT(1) // Will be sent to Vindicia
#define PRODUCT_DONT_ASSOCIATE	BIT(2) // Will not be associated with an account upon activation
#define PRODUCT_ZEN_PRICE_FIXED BIT(3) // Prices have been converted to Zen
#define PRODUCT_MARK_BILLED		BIT(4) // Mark an account billed if it activates this product

// Stores key-value modifications made during product activation
AUTO_STRUCT AST_CONTAINER;
typedef struct ProductKeyValueChangeContainer
{
	CONST_STRING_MODIFIABLE pKey;	AST(PERSIST ESTRING ADDNAMES(Key))
	CONST_STRING_MODIFIABLE pValue;	AST(PERSIST ESTRING ADDNAMES(Value)) // Will be converted to an int if appropriate
	const int change;				AST(PERSIST) // If true, change instead of set (only applicable to ints)
} ProductKeyValueChangeContainer;

// Stores the localized information for a product (one per language)
AUTO_STRUCT AST_CONTAINER;
typedef struct ProductLocalizedInfo
{
	CONST_STRING_MODIFIABLE pLanguageTag;	AST(PERSIST ESTRING NAME(LanguageTag)) // IETF language tag
	CONST_STRING_MODIFIABLE pName;			AST(PERSIST ESTRING NAME(Name)) // Product name (UTF-8)
	CONST_STRING_MODIFIABLE pDescription;	AST(PERSIST ESTRING NAME(Description)) // Product description (UTF-8)
} ProductLocalizedInfo;

// Tax classification
AUTO_ENUM;
typedef enum TaxClassification
{
	TCNotApplicable = 0,
	TCPhysicalGoods,
	TCDownloadableExecutableSoftware,
	TCDownloadableElectronicData,
	TCService,
	TCTaxExempt,
	TCOtherTaxable,
} TaxClassification;

AUTO_STRUCT AST_CONTAINER;
typedef struct ProductXBoxInfoContainer
{
	const U64	qwOfferID;						AST(PERSIST)
	const U8	contentId[XBOX_CONTENTID_SIZE];	AST(PERSIST)
} ProductXBoxInfoContainer;

AUTO_STRUCT AST_CONTAINER AST_IGNORE(pUpgradedProduct) AST_IGNORE(pBilledProduct);
typedef struct ProductRecruitInfoContainer
{
	// The recruiter will get the first of the products in the following array that can be given
	// successfully whenever a recruit is either upgraded or billed
	CONST_STRING_EARRAY eaUpgradedProducts;		AST(PERSIST)
	CONST_STRING_EARRAY eaBilledProducts;		AST(PERSIST)
} ProductRecruitInfoContainer;

// Stores a product
AUTO_STRUCT AST_CONTAINER;
typedef struct ProductContainer
{
	const U32 uID;												AST(PERSIST KEY)
	CONST_STRING_MODIFIABLE pName;								AST(PERSIST ESTRING) // This is unique and used internally
	CONST_STRING_MODIFIABLE pInternalName;						AST(PERSIST ESTRING) // This is used for the permissions
	CONST_STRING_MODIFIABLE pDescription;						AST(PERSIST ESTRING) // Internal description (comments)
	CONST_STRING_MODIFIABLE pBillingStatementIdentifier;		AST(PERSIST ESTRING)
	CONST_STRING_MODIFIABLE pSubscriptionCategory;				AST(PERSIST ESTRING)
	CONST_STRING_MODIFIABLE pVindiciaID;						AST(PERSIST ESTRING)
	CONST_STRING_EARRAY ppRequiredSubscriptions;				AST(PERSIST ESTRING)
	const bool bRequiresNoSubs;									AST(PERSIST)
	CONST_STRING_EARRAY ppShards;								AST(PERSIST ESTRING)
	CONST_EARRAY_OF(AccountPermissionValue) ppPermissions;		AST(PERSIST)
	const U32 uAccessLevel;										AST(PERSIST)
	CONST_STRING_EARRAY ppCategories;							AST(PERSIST ESTRING) // Used to get lists of products by category
	CONST_STRING_MODIFIABLE pCategoriesString;					AST(PERSIST ESTRING)
	CONST_EARRAY_OF(ProductKeyValueChangeContainer) ppKeyValueChanges;	AST(PERSIST) // Used to change a key's value
	CONST_STRING_MODIFIABLE pKeyValueChangesString;				AST(PERSIST ESTRING)
	CONST_EARRAY_OF(MoneyContainer) ppMoneyPrices;				AST(PERSIST)
	CONST_STRING_MODIFIABLE pItemID;							AST(PERSIST ESTRING) // Free-form string to represent in-game items
	CONST_STRING_EARRAY ppPrerequisites;						AST(PERSIST ESTRING) // Reverse Polish Notation prerequisites string (space separated)
	CONST_STRING_MODIFIABLE pPrerequisitesHuman;				AST(PERSIST ESTRING) // Human-friendly infix version (for those who don't think in RPN)
	const U32 uDaysGranted;										AST(PERSIST) // Number of days to grant if it is a game card
	CONST_STRING_MODIFIABLE pInternalSubGranted;				AST(PERSIST ESTRING) // Internal sub to be granted on activation, if any
	const U32 uFlags;											AST(PERSIST) // Flags for the product
	CONST_EARRAY_OF(ProductLocalizedInfo) ppLocalizedInfo;		AST(PERSIST) // Stores localized information for the product
	CONST_STRING_MODIFIABLE pActivationKeyPrefix;				AST(PERSIST ESTRING) // Prefix of the key to distribute upon activation
	CONST_EARRAY_OF(AccountProxyKeyValueInfoContainer) ppKeyValues; AST(PERSIST FORCE_CONTAINER) // Read-only key-values
	const TaxClassification eTaxClassification;					AST(PERSIST) // Tax classification
	const U32 uExpireDays;										AST(PERSIST) // Number of days until the product 'expires' off of the account
	const bool bNoSpendingCap;									AST(PERSIST)

	// X-Box
	const ProductXBoxInfoContainer xbox;						AST(PERSIST)

	// Recruit stuff
	const ProductRecruitInfoContainer recruit;					AST(PERSIST)
	CONST_STRING_MODIFIABLE pReferredProduct;					AST(PERSIST)

	// Obsolete members.
	CONST_EARRAY_OF(PriceContainer) ppPrices;					AST(PERSIST FORCE_CONTAINER)  // Deprecated: Use ppMoneyPrices instead.

	AST_COMMAND("Apply Transaction","ServerMonTransactionOnEntity AccountServerProduct $FIELD(UID) $STRING(Transaction String)")
} ProductContainer;

typedef void (*ProductModificationCB)(SA_PARAM_OP_VALID const ProductContainer *pProduct, bool bSuccess, SA_PARAM_OP_VALID void *pUserData);

#include "Product_h_ast.h"


/************************************************************************/
/* Search functions                                                     */
/************************************************************************/

// Find a product by name
SA_RET_OP_VALID const ProductContainer *findProductByName(SA_PARAM_NN_STR const char *pName);

// Find a product by the product's ID
SA_RET_OP_VALID const ProductContainer *findProductByID(U32 uID);


/************************************************************************/
/* Modifying functions                                                  */
/************************************************************************/

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
				 bool bNoSpendingCap);

// Replace a product
bool productReplace(SA_PARAM_NN_VALID ProductContainer *pProduct);

// Create a product - used for AS2 Fixtures
void createProductFromStruct(SA_PARAM_NN_VALID ProductContainer *pProduct);

// Add or replace a localization entry
void productLocalize(U32 uProductID,
					 SA_PARAM_NN_STR const char *pLanguageTag,
					 SA_PARAM_NN_STR const char *pName,
					 SA_PARAM_NN_STR const char *pDescription);

// Remove a localization entry
void productUnlocalize(U32 uProductID, SA_PARAM_NN_STR const char *pLanguageTag);

// Set a product's key-value
void productSetKeyValue(U32 uProductID, SA_PARAM_NN_STR const char *pKey, SA_PARAM_OP_STR const char *pValue);

// Import a single product
void importProduct(SA_PARAM_NN_VALID ProductContainer *pProduct,
				   SA_PARAM_OP_VALID ProductModificationCB pCB,
				   SA_PARAM_OP_VALID void *pUserData);


/************************************************************************/
/* List functions                                                       */
/************************************************************************/

// Flags used for getting a product list
#define PRODUCTS_ASSOCIATIVE		BIT(0)	// Find products that can be associated with an account
#define PRODUCTS_NOT_ASSOCIATIVE	BIT(1)	// Find products that can not be associated with an account
#define PRODUCTS_ALL				(PRODUCTS_ASSOCIATIVE | PRODUCTS_NOT_ASSOCIATIVE) // Find all products

// Get a list of all subscriptions -- DO NOT FREE THE CONTENTS; ONLY eaDestroy IT!
SA_RET_OP_VALID EARRAY_OF(ProductContainer) getProductList(U32 uFlags);

// Get a list of product names as an earray of estrings
SA_RET_OP_VALID STRING_EARRAY getProductNameList(U32 uFlags);


/************************************************************************/
/* Initialization                                                       */
/************************************************************************/

// Initialize all products
void initializeProducts(void);

// Scan all products and do fixups and such
void scanProducts(void);

/************************************************************************/
/* Utility functions                                                    */
/************************************************************************/

// Converts an earray of account permissions into a string
void concatPermissionString(CONST_EARRAY_OF(AccountPermissionValue) values, SA_PARAM_NN_VALID char **estr);

// Convert a string to a tax classification
TaxClassification convertTaxClassificationFromString(SA_PARAM_NN_STR const char *pTaxClassification);

// Convert a tax classification to a string
SA_RET_OP_STR const char *convertTaxClassificationToString(TaxClassification eClassification);

// Get an HTTP enum list of tax classifications in the proper order
SA_RET_OP_STR const char *taxClassificationEnumString(void);

// Determine if a product grants a permission
bool productGrantsPermission(SA_PARAM_NN_VALID const ProductContainer *pProduct, SA_PARAM_NN_STR const char *pPermission);

// Determine if a product has a subscription listed as required
bool productHasSubscriptionListed(SA_PARAM_NN_VALID const ProductContainer *pProduct,
								  SA_PARAM_NN_STR const char *pSubInternalName);

// Get the price of a product in a given currency
SA_RET_OP_VALID const Money *getProductPrice(SA_PARAM_NN_VALID const ProductContainer *pProduct,
										  SA_PARAM_NN_STR const char *pCurrency);


/************************************************************************/
/* Default Permissions                                                  */
/************************************************************************/

// Subscriptions are never required for this; Limited features available for default permissions
AUTO_STRUCT;
typedef struct ProductDefaultPermission
{
	// Only fields used are (matching ProductContainer fields):
	// Product Internal Name
	// Shards
	// Permissions
	// uAccessLevel
	char *pProductName;
	STRING_EARRAY ppShards;
	EARRAY_OF(AccountPermissionValue) ppPermissions;
	U32 uAccessLevel;

	// Optional time fields
	U32 uStartTime; AST(FORMATSTRING(HTML_SECS = 1))
	U32 uEndTime; AST(FORMATSTRING(HTML_SECS = 1))

	STRING_EARRAY eaRequiredSubscriptions;
	bool bSubsMustBeInternal;
} ProductDefaultPermission;

AUTO_STRUCT;
typedef struct ProductDefaultPermissionList
{
	EARRAY_OF(ProductDefaultPermission) ppProductPermissions;
} ProductDefaultPermissionList;

ProductDefaultPermission * const * productGetDefaultPermission(void);

#endif