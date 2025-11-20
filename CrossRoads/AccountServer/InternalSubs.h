#ifndef INTERNALSUBS_H
#define INTERNALSUBS_H
#include "InternalSubs_h_ast.h"

typedef struct AccountInfo AccountInfo;

/************************************************************************/
/* Structures                                                           */
/************************************************************************/

// Container for internal subs
AUTO_STRUCT AST_CONTAINER;
typedef struct InternalSubscription
{
	const U32 uID;								AST(KEY PERSIST)	// Container ID
	const U32 uAccountID;						AST(PERSIST)		// Account ID
	CONST_STRING_MODIFIABLE pSubInternalName;	AST(ESTRING PERSIST)// Subscription internal name
	const U32 uExpiration;						AST(PERSIST)		// Time in seconds since 2000 when this will expire (0 for never)
	const U32 uCreated;							AST(PERSIST)		// Time in seconds since 2000 when this was created
	const U32 uProductID;						AST(PERSIST)		// Product ID of product that granted this

	AST_COMMAND("Apply Transaction","ServerMonTransactionOnEntity AccountServerInternalSubscription $FIELD(UID) $STRING(Transaction String)")
} InternalSubscription;


/************************************************************************/
/* Modification functions                                               */
/************************************************************************/

// Create an internal subscription
bool internalSubCreate(SA_PARAM_NN_VALID AccountInfo * pAccount, SA_PARAM_NN_VALID const char * pSubInternalName, U32 uExpiration, U32 uProductID);

// Remove an internal subscription
bool internalSubRemove(U32 uAccountID, SA_PARAM_NN_VALID const char * pSubInternalName);

// Remove all internal subs given by a product
unsigned int internalSubRemoveByProduct(U32 uProductID);

// Destroy all internal subs belonging to the given account ID
void destroyInternalSubsByAccountID(U32 uAccountID);

/************************************************************************/
/* Search functions                                                     */
/************************************************************************/

// Search for an internal sub
SA_RET_OP_VALID const InternalSubscription * findInternalSub(U32 uAccountID, SA_PARAM_NN_VALID const char * pSubInternalName);

// Search for an internal sub by ID
SA_RET_OP_VALID const InternalSubscription * findInternalSubByID(U32 uID);

// Search for internal subs by account ID (eaDestroy NOT eaDestroyStruct the return value!)
SA_RET_OP_VALID EARRAY_OF(const InternalSubscription) findInternalSubsByAccountID(U32 uAccountID);


/************************************************************************/
/* Initialization                                                       */
/************************************************************************/

// Initialize the internal subscriptions
void initializeInternalSubscriptions(void);
void InternalSubs_DestroyContainers(void);

#endif