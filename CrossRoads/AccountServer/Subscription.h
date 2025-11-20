#ifndef SUBSCRIPTION_H
#define SUBSCRIPTION_H

/************************************************************************/
/* Forward declarations                                                 */
/************************************************************************/

typedef struct PriceContainer PriceContainer; // From net/accountnet.h
typedef struct MoneyContainer MoneyContainer; // From Money.h


/************************************************************************/
/* Types                                                                */
/************************************************************************/

// Subscription flags
#define SUBSCRIPTION_MOCKPRODUCT	BIT(0)

AUTO_ENUM;
typedef enum SubscriptionPeriodType
{
	SPT_Year = 0,
	SPT_Month,
	SPT_Day
} SubscriptionPeriodType;

// Stores the localized information for a subscription (one per language)
AUTO_STRUCT AST_CONTAINER;
typedef struct SubscriptionLocalizedInfo
{
	CONST_STRING_MODIFIABLE pLanguageTag;	AST(PERSIST ESTRING NAME(LanguageTag))	// IETF language tag
	CONST_STRING_MODIFIABLE pName;			AST(PERSIST ESTRING NAME(Name))			// Subscription name (UTF-8)
	CONST_STRING_MODIFIABLE pDescription;	AST(PERSIST ESTRING NAME(Description))	// Subscription description (UTF-8)
} SubscriptionLocalizedInfo;

// Stores information about a subscription.
// WARNING: If you add a member to this structure, you may also want to add it to XMLRPCSubscription in billingHttpRequests.h.
AUTO_STRUCT AST_CONTAINER;
typedef struct SubscriptionContainer
{
	const U32 uID;											AST(PERSIST KEY)
	CONST_STRING_MODIFIABLE pName;							AST(PERSIST ESTRING NAME("Name"))
	CONST_STRING_MODIFIABLE pInternalName;					AST(PERSIST ESTRING NAME("InternalName"))
	CONST_STRING_MODIFIABLE pDescription;					AST(PERSIST ESTRING NAME("Description"))
	CONST_STRING_MODIFIABLE pBillingStatementIdentifier;	AST(PERSIST ESTRING NAME("BillingStatementIdentifier"))
	CONST_STRING_MODIFIABLE pProductName;					AST(PERSIST ESTRING NAME("ProductName")) // Identifies which ProductStruct is
																									 // associated with this when it becomes an AutoBill
	CONST_EARRAY_OF(SubscriptionLocalizedInfo) ppLocalizedInfo;	AST(PERSIST) // Stores localized information for the subscription
	const SubscriptionPeriodType periodType;				AST(PERSIST)
	const int iPeriodAmount;								AST(PERSIST NAME("PeriodAmount"))
	const int iInitialFreeDays;								AST(PERSIST NAME("InitialFreeDays"))
	const U32 gameCard;										AST(PERSIST) // If true, no credit card will be associated
																		 // with the resultant autobill
	CONST_EARRAY_OF(MoneyContainer) ppMoneyPrices;			AST(PERSIST)
	CONST_STRING_EARRAY ppCategories;						AST(ESTRING PERSIST NAME("Categories"))

	const U32 uFlags;										AST(PERSIST NAME(Flags))

	CONST_STRING_MODIFIABLE pBilledProductName;				AST(PERSIST NAME("BilledProductName")) // Product given to an account when this subscription is billed for the first time

	// Obsolete members.
	CONST_EARRAY_OF(PriceContainer) ppPrices;				AST(PERSIST FORCE_CONTAINER NAME("Prices"))  // Deprecated: Use ppMoneyPrices instead.

	AST_COMMAND("Apply Transaction","ServerMonTransactionOnEntity AccountServerSubscription $FIELD(UID) $STRING(Transaction String)")
} SubscriptionContainer;

AUTO_STRUCT;
typedef struct SubscriptionContainerList
{
	EARRAY_OF(SubscriptionContainer) ppList; AST( NAME(Subscription) )
} SubscriptionContainerList;

typedef void (*SubscriptionModificationCB)(SA_PARAM_OP_VALID const SubscriptionContainer *pContainer, bool bSuccess, SA_PARAM_OP_VALID void *pUserData,
										   SA_PARAM_OP_STR const char *pReason);

// Used by ReplaceAccountDatabase XML-RPC call for unit testing
AUTO_STRUCT;
typedef struct SubscriptionCreateData
{
	SubscriptionContainer subscription; AST(EMBEDDED_FLAT)
	char *pPriceString;
	char *pCategoryString;
} SubscriptionCreateData;

void subscriptionAddStruct(SA_PARAM_NN_VALID SubscriptionCreateData *pData);


/************************************************************************/
/* Search functions                                                     */
/************************************************************************/

// Find a subscription by the subscription's name
SA_RET_OP_VALID const SubscriptionContainer *findSubscriptionByName(SA_PARAM_NN_STR const char *pName);

// Find a subscription by the subscription's ID
SA_RET_OP_VALID const SubscriptionContainer *findSubscriptionByID(U32 uID);

// Find all subscription with a category -- eaDestroy NOT eaDestroyStruct
SA_RET_OP_VALID EARRAY_OF(const SubscriptionContainer) findSubscriptionsByCategory(SA_PARAM_NN_STR const char *pCategory);


/************************************************************************/
/* Modifying functions                                                  */
/************************************************************************/

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
					 SA_PARAM_OP_VALID void *pUserData);

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
					  SA_PARAM_OP_VALID void *pUserData);

// Add or replace a localization entry
void subscriptionLocalize(U32 uSubID,
					 SA_PARAM_NN_STR const char *pLanguageTag,
					 SA_PARAM_NN_STR const char *pName,
					 SA_PARAM_NN_STR const char *pDescription);

// Remove a localization entry
void subscriptionUnlocalize(U32 uSubID, SA_PARAM_NN_STR const char *pLanguageTag);


/************************************************************************/
/* List functions                                                       */
/************************************************************************/

// Get a list of all subscriptions -- DO NOT FREE THE CONTENTS; ONLY eaDestroy IT!
SA_RET_OP_VALID EARRAY_OF(SubscriptionContainer) getSubscriptionList(void);


/************************************************************************/
/* Initialization                                                       */
/************************************************************************/

// Initialize the subscriptions
void initializeSubscriptions(void);
void Subscription_DestroyContainers(void);


/************************************************************************/
/* Utility functions                                                    */
/************************************************************************/

// Create a categories ESTRING from a subscription
SA_RET_OP_STR char * getSubscriptionCategoryString(SA_PARAM_NN_VALID const SubscriptionContainer *pSubscription);

// Returns a string representation of the given period type
SA_RET_NN_STR const char *getSubscriptionPeriodName(SubscriptionPeriodType eType);

#include "Subscription_h_ast.h"
#endif