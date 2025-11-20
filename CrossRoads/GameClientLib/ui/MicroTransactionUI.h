/***************************************************************************
*     Copyright (c) 2010, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

#ifndef MICROTRANSACTIONS_UI_H
#define MICROTRANSACTIONS_UI_H
GCC_SYSTEM

#include "MicroTransactions.h"

typedef struct UIGen UIGen;
typedef struct Entity Entity;
typedef struct MicroTransactionProduct MicroTransactionProduct;
typedef struct AccountProxyProduct AccountProxyProduct;
typedef struct InventorySlot InventorySlot;

// Used in the main list view of all the micro transaction products available.
AUTO_STRUCT;
typedef struct MicroTransactionUIProduct
{
	U32 uID;					AST(KEY)
		// The ID of the micro transaction

	char *pName;				AST(ESTRING)
		//The name of the micro transaction
	char *pDescription;			AST(ESTRING)
		// The description of the micro transaction
	char *pLongDescription;		AST(ESTRING)
		// The long description of the micro transaction

	REF_TO(MicroTransactionCategory) hCategory;	AST(NAME(Category))
		//This is the underlying category (The sub category or the main category).  Important when attempting to purchase the product

	REF_TO(AllegianceDef) hAllegiance;	AST(NAME(Allegiance))
		// The allegiance of the product, it is determined by a category that has an allegiance or display allegiance on it.

	S64 iPrice;
		// Price of the micro transaction (In CrypticPoints)

	S64 iDefPrice;
		// Price of the micro transaction based on the def. If the value on the def
		// represents the base price of the item, this may be used to determine if
		// the item is on sale and how much it is discounted.

	const char *pchIcon;			AST(POOL_STRING)
		// The icon to show

	STRING_EARRAY ppIcons;	AST(POOL_STRING)
		// All the icons

	char* estrCannotPurchaseReason;		AST(NAME(CannotPurchaseReason) ESTRING)
		// Display string describing why you cannot purchase this product

	MicroPurchaseErrorType eCannotPurchaseType;
		// The error type describing why you cannot purchase this product

	InventorySlot **eaInventory;		AST(NO_INDEX)
		// The list of items + quantities that this microtransaction may grant

	U32 iUpdate;

	U32 bPrerequisitesMet : 1;
	U32 bCannotPurchaseAgain : 1;
	U32 bOneTimePurchase : 1;
	U32 bUniqueInInv : 1;
	U32 bUpdated : 1;
	U32 bItemRestrictions : 1;
	U32 bNewProduct : 1;
	U32 bFeaturedProduct : 1;
	U32 bOwnedProduct : 1;
		// OwnedProduct: CannotPurchaseAgain, UniqueInInv, AlreadyEntitled or any other CannotPurchaseReason
		// that indicates that the product may be considered "owned".

	U32 bFailsExpression : 1;
		// the required expression has failed

	U32 bNotEnoughCurrency : 1;
		// the account does not have enough in the currency chain to purchase this item. 

	U32 bAlreadyEntitled : 1;
		// True if the product cannot be bought because the player is already entitled to it.
		// For example, things that subscribers have access to that non-subscribers have to buy.

	U32 bPremiumEntitlement : 1;
		// True if the product is free for premium players.

} MicroTransactionUIProduct;

extern ParseTable parse_MicroTransactionUIProduct[];
#define TYPE_parse_MicroTransactionUIProduct MicroTransactionUIProduct

extern S64 microtrans_GetPrice(AccountProxyProduct *pProduct);
extern const char *microtrans_GetPurchaseCategory(MicroTransactionProduct *pProduct);
extern bool microtrans_HasNewCategory(MicroTransactionProduct *pProduct);

// Fill a UIGen with MicroTransactionUIProduct a list of MicroTransactionProduct's.
extern void gclMicroTrans_GetProductList(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt, MicroTransactionProduct **ppProducts);

extern MicroTransactionUIProduct *gclMicroTrans_MakeUIProduct(U32 uID);
extern void gclMicroTrans_UpdateUIProduct(U32 uID, MicroTransactionUIProduct **ppUIProduct);
extern bool gclMicroTrans_SetUIProduct(Entity *pEnt, MicroTransactionUIProduct *pUIProduct, MicroTransactionProduct *pMTProduct, const char *pchUnderlyingCategory);
extern bool gclMicroTrans_expr_ShowProduct(U32 uID);
extern bool gclMicroTrans_expr_ShowProductByName(const char *pchName);
extern U32 gclMicroTrans_expr_GetID(const char *pchName);
extern U64 ui_GenExprGetCurrentCouponItemID(U32 uProductID);


#endif
