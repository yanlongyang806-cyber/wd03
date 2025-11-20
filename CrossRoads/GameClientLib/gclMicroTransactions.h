/***************************************************************************
*     Copyright (c) 2010, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

#ifndef GCLMICROTRANSACTIONS_H
#define GCLMICROTRANSACTIONS_H
GCC_SYSTEM

#include "MicroTransactions.h"

typedef struct AccountProxyProductList AccountProxyProductList;
typedef struct CachedPaymentMethod CachedPaymentMethod;
typedef struct Entity Entity;
typedef struct ItemDef ItemDef;
typedef struct PCCostumeSet PCCostumeSet;
typedef struct PaymentMethodsResponse PaymentMethodsResponse;
typedef struct PlayerCostume PlayerCostume;

// A source Product, Item, or CostumeSet
AUTO_STRUCT;
typedef struct MicroTransactionCostumeSource
{
	U32 uID;							AST(KEY)
		// pProduct->uID, just for quickly determining if the source has already been added.

	MicroTransactionProduct *pProduct;	AST(UNOWNED)
		// Reference to the MicroTransactionProduct

	REF_TO(ItemDef) hItem;				AST(REFDICT(ItemDef))
		// The ItemDef that grants the costume, if it is granted via an ItemDef

	REF_TO(PCCostumeSet) hCostumeSet;	AST(REFDICT(CostumeSet))
		// The PCCostumeSet that grants the costume, if it is granted via an CostumeSet

	bool bHidden;
		// If the costume is hidden from the player

	bool bOwned;
		// If the costume is owned by the player

	bool bNew;
		// If the product is new
} MicroTransactionCostumeSource;

AUTO_STRUCT;
typedef struct MicroTransactionCostume
{
	REF_TO(PlayerCostume) hCostume;		AST(KEY REFDICT(PlayerCostume))
		// Reference to the costume

	MicroTransactionCostumeSource **eaSources;
		// The list of sources for this costume

	bool bHidden;
		// If the costume is hidden from the player, this is the result of all Sources and'd together.

	bool bOwned;
		// If the costume is owned by the player, this is the result of all Sources or'd together.

	bool bNew;
		// If the product is new, this is the result of all Sources and'd together.
} MicroTransactionCostume;

AUTO_STRUCT;
typedef struct MicroTransactionCostumeList
{
	MicroTransactionCostume **ppCostumes;
} MicroTransactionCostumeList;

typedef void (*MicroTrans_CostumeListChanged)(void *pUserData);

extern MicroTransactionProductList *g_pMTList;
extern MicroTransactionCostumeList *g_pMTCostumes;

extern AccountProxyProductList *g_pPointBuyList;
extern CachedPaymentMethod **g_eaPaymentMethods;

bool gclMicroTrans_expr_IsPWAccount(void);

bool gclMicroTrans_PrerequisitesMet(MicroTransactionProduct *pMTProduct);

void gclMicroTrans_RecvProductList(SA_PARAM_OP_VALID const MicroTransactionProductList *pList);
void gclMicroTrans_SetMOTD(SA_PARAM_OP_VALID AccountProxyProduct *pProduct);

void gclMicroTrans_RecvPointBuyProducts(SA_PARAM_OP_VALID const AccountProxyProductList *pList);
void gclMicroTrans_RecvPaymentMethods(SA_PARAM_OP_VALID PaymentMethodsResponse *pResponse);

void gclMicroTrans_UpdateCostumeList(void);

void gclMicroTrans_PurchaseMicroTransactionList(SA_PARAM_NN_VALID Entity *pEntity, MicroTransactionProduct **eaProducts, SA_PARAM_OP_STR const char *pchCurrency);

void gclMicroTrans_AddCostumeListChangedHandler(MicroTrans_CostumeListChanged pHandler, void *pUserData);
void gclMicroTrans_RemoveCostumeListChangedHandler(MicroTrans_CostumeListChanged pHandler);

bool gclMicroTrans_HasNewCategory(MicroTransactionProduct *pProduct);

S32 gclMicroTrans_FindProductsForPermissionExpr(MicroTransactionProduct ***pppProducts, Expression *pExpr);
MicroTransactionProduct *gclMicroTrans_FindProductForPermission(const char *pchPermission);
MicroTransactionProduct *gclMicroTrans_FindProductForKey(const char *pchAccountKey);
S32 gclMicroTrans_FindProductsForPermission(MicroTransactionProduct ***pppProducts, const char *pchPermission);
S32 gclMicroTrans_FindProductsForKey(MicroTransactionProduct ***pppProducts, const char *pchAccountKey);

bool gclMicroTrans_IsProductHidden(MicroTransactionProduct *pMTProduct);

void gclMicroTrans_ClearLists();

bool gclMicroTrans_PointBuy_CanAttemptPurchase(bool bTest);
	// Implements a client-side cooldown on purchasing points

void gclMicroTrans_PointBuySuccess(const char *pchName);
	// The purchase of points succeeded
void gclMicroTrans_PointBuyFailed(const char *pchName);
	// The purchase of points failed

void microtrans_DEPR_GetLocalizedInfo(SA_PARAM_NN_VALID const AccountProxyProduct *pProduct, SA_PRE_NN_VALID SA_POST_NN_RELEMS(1) char **pNameOut, SA_PRE_NN_VALID SA_POST_NN_RELEMS(1) char **pDescriptionOut);

#endif //GCLMICROTRANSACTIONS_H