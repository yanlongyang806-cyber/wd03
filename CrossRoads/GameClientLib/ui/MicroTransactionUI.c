
#include "MicroTransactionUI.h"
#include "AccountDataCache.h"
#include "accountnet.h"
#include "earray.h"
#include "Entity.h"
#include "estring.h"
#include "GameAccountDataCommon.h"
#include "GamePermissionsCommon.h"
#include "GlobalTypes.h"
#include "gclAccountProxy.h"
#include "gclEntity.h"
#include "gclMapTransfer.h"
#include "gclMicroTransactions.h"
#include "inventoryCommon.h"
#include "ItemCommon.h"
#include "itemCommon_h_ast.h"
#include "ItemEnums.h"
#include "itemEnums_h_ast.h"
#include "MicroTransactions.h"
#include "MicroTransactions_h_ast.h"
#include "microtransactions_common.h"
#include "Money.h"
#include "Player.h"
#include "Powers.h"
#include "species_common.h"
#include "StringCache.h"
#include "StringUtil.h"
#include "UIGen.h"
#include "MicroTransactions.h"
#include "GameClientLib.h"
#include "cmdparse.h"
#include "gclAccountProxy.h"
#include "GameStringFormat.h"
#include "Expression.h"
#include "gclSteam.h"
#include "SteamCommon.h"
#include "allegiance.h"

#include "MicroTransactionUI_c_ast.h"
#include "MicroTransactions_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

AUTO_STRUCT;
typedef struct TransactionNotifyStruct
{
	char *pchMessage;
} TransactionNotifyStruct;

AUTO_STRUCT;
typedef struct MicroTransactionUIIcon
{
	STRING_POOLED pchIcon;				AST(POOL_STRING)
		//What's my icon?

	int iIndex;
		//What index am I?
} MicroTransactionUIIcon;


AUTO_STRUCT;
typedef struct MicroTransactionUICategory
{
	STRING_POOLED pchName;			AST(POOL_STRING)
		//The name of the category

	STRING_POOLED pchParentName;	AST(POOL_STRING)
		//The name of the parent category

	MTCategoryType eType;
		//Category type

	char *estrDisplayName;			AST(ESTRING)
		// The translated display name

	char *estrDescription;			AST(ESTRING)

	S32 iSortIndex;
		// The sorting index

	bool bHideUnusable;
		// Hide things I can't use in the list

	F32 fMinDiscountPercent;
		// If the price on the def represents the standard price
		// of the product, then this value if > 0 will be the
		// lowest discount percent a product in the category
		// has.

	F32 fMaxDiscountPercent;
		// If the price on the def represents the standard price
		// of the product, then this value if > 0 will be the
		// highest discount percent a product in the category
		// has.

} MicroTransactionUICategory;

// Run a given command when purchasing a specified product
AUTO_STRUCT;
typedef struct MicroTransactionListener
{
	const char* pchProductName; AST(POOL_STRING KEY)
	char* pchCommand; AST(ESTRING)
} MicroTransactionListener;

static MicroUiCoupon** s_eaCouponData = NULL;

typedef struct UICouponInfo
{
	U64 uCouponItem;
	U32 uProductID;
	S32 iIndex;

}UICouponInfo;

static UICouponInfo s_CouponInfo;

static TransactionNotifyStruct **eaTransaction_Notify_Messages = NULL;

static MicroTransactionListener **eaMicroTransactionSuccessListeners = NULL;
static MicroTransactionListener **eaMicroTransactionFailureListeners = NULL;


AUTO_EXPR_FUNC(UIGen) ACMD_NAME("TransactionsNotify_GetTextList");
void exprFuncTransactionsNotifyText(UIGen *pGen)
{
	ui_GenSetManagedListSafe(pGen, &eaTransaction_Notify_Messages, TransactionNotifyStruct, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("TransactionsNotify_HasText");
bool exprFuncTransactionsNotifyHasText(void)
{
	return(eaSize(&eaTransaction_Notify_Messages));
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("TransactionsNotify_ClearText");
void exprFuncTransactionsNotifyClearText(void)
{
	eaClearStruct(&eaTransaction_Notify_Messages, parse_TransactionNotifyStruct);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_NAME("TransactionsNotify");
void gclTransactionsNotify(ACMD_SENTENCE pchMessage)
{
	TransactionNotifyStruct *pNotify = StructCreate(parse_TransactionNotifyStruct);
	pNotify->pchMessage = StructAllocString(pchMessage);
	eaPush(&eaTransaction_Notify_Messages, pNotify);
}

void TransactionsRegisterListener(MicroTransactionListener*** peaMicroTransactionListeners, const char *pchProduct, const char *pchCommand)
{
	MicroTransactionListener *pListener = NULL;
	U32 uProductID = -1;

	// Convert to a product ID if the name is strictly digits
	if (pchProduct && *pchProduct && strlen(pchProduct) == strspn(pchProduct, "0123456789"))
	{
		uProductID = atoi(pchProduct);
		pchProduct = NULL;
	}

	if (uProductID)
	{
		MicroTransactionProduct *pMTProduct = g_pMTList ? eaIndexedGetUsingInt(&g_pMTList->ppProducts, uProductID) : NULL;
		if (pMTProduct)
			pchProduct = REF_STRING_FROM_HANDLE(pMTProduct->hDef);
	}

	if (!pchProduct || !*pchProduct)
		return;

	if (!eaIndexedGetTable(peaMicroTransactionListeners))
		eaIndexedEnable(peaMicroTransactionListeners, parse_MicroTransactionListener);

	pchProduct = allocAddString(pchProduct);
	pListener = eaIndexedGetUsingString(peaMicroTransactionListeners, pchProduct);

	if (pListener)
	{
		estrCopy2(&pListener->pchCommand, pchCommand);
	}
	else
	{
		pListener = StructCreate(parse_MicroTransactionListener);
		pListener->pchProductName = pchProduct;
		estrCopy2(&pListener->pchCommand, pchCommand);
		eaPush(peaMicroTransactionListeners, pListener);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MicroTrans_RegisterListener");
void exprTransactionsRegisterSuccessListenerByName(const char *pchProduct, const char *pchCommand);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MicroTrans_RegisterSuccessListenerByName");
void exprTransactionsRegisterSuccessListenerByName(const char *pchProduct, const char *pchCommand)
{
	TransactionsRegisterListener(&eaMicroTransactionSuccessListeners, pchProduct, pchCommand);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MicroTrans_RegisterFailureListenerByName");
void exprTransactionsRegisterFailureListenerByName(const char *pchProduct, const char *pchCommand)
{
	TransactionsRegisterListener(&eaMicroTransactionFailureListeners, pchProduct, pchCommand);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MicroTrans_RegisterListenersByName");
void exprTransactionsRegisterListenersByName(const char *pchProduct, const char *pchSuccessCommand, const char *pchFailureCommand)
{
	TransactionsRegisterListener(&eaMicroTransactionSuccessListeners, pchProduct, pchSuccessCommand);
	TransactionsRegisterListener(&eaMicroTransactionFailureListeners, pchProduct, pchFailureCommand);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MicroTrans_RegisterSuccessListener");
void exprTransactionsRegisterSuccessListener(U32 uID, const char *pchCommand)
{
	char pchProduct[32];
	sprintf(pchProduct, "%d", uID);
	TransactionsRegisterListener(&eaMicroTransactionSuccessListeners, pchProduct, pchCommand);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MicroTrans_RegisterFailureListener");
void exprTransactionsRegisterFailureListener(U32 uID, const char *pchCommand)
{
	char pchProduct[32];
	sprintf(pchProduct, "%d", uID);
	TransactionsRegisterListener(&eaMicroTransactionFailureListeners, pchProduct, pchCommand);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MicroTrans_RegisterListeners");
void exprTransactionsRegisterListeners(U32 uID, const char *pchSuccessCommand, const char *pchFailureCommand)
{
	char pchProduct[32];
	sprintf(pchProduct, "%d", uID);
	TransactionsRegisterListener(&eaMicroTransactionSuccessListeners, pchProduct, pchSuccessCommand);
	TransactionsRegisterListener(&eaMicroTransactionFailureListeners, pchProduct, pchFailureCommand);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_NAME("TransactionsNotifySuccess");
void gclTransactionsNotifySuccess(const char *pchType, ACMD_SENTENCE pchName)
{
	if(stricmp(pchType, "MicroTransaction")==0)
	{
		if(pchName && stricmp(pchName, "none"))
		{
			MicroTransactionDef *pDef;
			MicroTransactionListener *pListener;

			// Run listener
			pListener = eaIndexedGetUsingString(&eaMicroTransactionSuccessListeners, pchName);
			if (pListener && pListener->pchCommand && pListener->pchCommand[0])
			{
				globCmdParseAndReturn(pListener->pchCommand, NULL, 0, -1, CMD_CONTEXT_HOWCALLED_UNSPECIFIED, NULL);
				return;
			}

			pDef = RefSystem_ReferentFromString(g_hMicroTransDefDict, pchName);
			if(pDef)
			{
				gclMapMoveConfirm_Microtransaction_Notify(pDef, true);
			}
		}
	}
	else if(stricmp(pchType, "PointBuy")==0)
	{
		gclMicroTrans_PointBuySuccess(pchName);
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_NAME("TransactionsNotifyFailure");
void gclTransactionsNotifyFailure(const char *pchType, ACMD_SENTENCE pchName)
{
	if(stricmp(pchType, "MicroTransaction")==0)
	{
		if(pchName && stricmp(pchName, "none"))
		{
			MicroTransactionDef *pDef;
			MicroTransactionListener *pListener;

			pListener = eaIndexedGetUsingString(&eaMicroTransactionFailureListeners, pchName);
			if (pListener && pListener->pchCommand && pListener->pchCommand[0])
			{
				globCmdParseAndReturn(pListener->pchCommand, NULL, 0, -1, CMD_CONTEXT_HOWCALLED_UNSPECIFIED, NULL);
				return;
			}
			
			pDef = RefSystem_ReferentFromString(g_hMicroTransDefDict, pchName);
			if(pDef)
			{
				gclMapMoveConfirm_Microtransaction_Notify(pDef, false);
			}
		}
	}
	else if(stricmp(pchType, "PointBuy")==0)
	{
		gclMicroTrans_PointBuyFailed(pchName);
	}
}

static int SortCategories(const MicroTransactionUICategory **pA, const MicroTransactionUICategory **pB)
{
	// First sort by featured
	if(((*pA)->eType == kMTCategory_Featured && (*pB)->eType == kMTCategory_Featured)
		|| ((*pA)->eType != kMTCategory_Featured && (*pB)->eType != kMTCategory_Featured))
	{
	if((*pA)->iSortIndex != (*pB)->iSortIndex)
	{
		return (*pA)->iSortIndex - (*pB)->iSortIndex;
	}

	return(stricmp((*pA)->estrDisplayName, (*pB)->estrDisplayName));
	}

	if((*pA)->eType == kMTCategory_Featured )
		return -1;

	// B must be featured.

	return 1;
}

//Gets the Sub-categories  of a specific category available
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MicroTrans_GetSubCategories);
void microtrans_ExprGenGetSubCategories(SA_PARAM_NN_VALID UIGen *pGen, const char *pchCategory)
{
	static MicroTransactionCategory **eaAllCategories = NULL;
	static MicroTransactionCategory **s_eaDiscountCategories = NULL;
	static F32 *s_eafDiscounts = NULL;
	F32 fOverallMinDiscount = 100;
	F32 fOverallMaxDiscount = -100;
	MicroTransactionUICategory ***peaMTCategories = ui_GenGetManagedListSafe(pGen, MicroTransactionUICategory);

	MicroTransactionCategory *pCurrentCategory = RefSystem_ReferentFromString(g_hMicroTransCategoryDict, pchCategory);

	eaClearFast(&eaAllCategories);
	eaClearStruct(peaMTCategories, parse_MicroTransactionUICategory);
	eaIndexedEnable(&eaAllCategories, parse_MicroTransactionCategory);
	eaClearFast(&s_eaDiscountCategories);
	eafClearFast(&s_eafDiscounts);

	if(g_pMTList && pCurrentCategory)
	{
		S32 iIdx, iSize = eaSize(&g_pMTList->ppProducts);
		S32 iCatIdx, iDiscIdx;
		for(iIdx=0; iIdx <iSize; iIdx++)
		{
			MicroTransactionProduct *pMTProduct = eaGet(&g_pMTList->ppProducts,iIdx);
			MicroTransactionDef *pMTDef = GET_REF(pMTProduct->hDef);
			S64 iPrice = microtrans_GetPrice(pMTProduct->pProduct);
			S64 iFullPrice = microtrans_GetFullPrice(pMTProduct->pProduct);
			S64 iDefPrice = iFullPrice > 0 ? iFullPrice : iPrice;
			F32 fDiscount = iDefPrice > 0 ? (F32)iPrice / (F32)iDefPrice : 0;
			for(iCatIdx = eaSize(&pMTProduct->ppchCategories)-1; iCatIdx>=0; iCatIdx--)
			{
				MicroTransactionCategory *pCategory = RefSystem_ReferentFromString(g_hMicroTransCategoryDict, pMTProduct->ppchCategories[iCatIdx]);
				if( pCategory && 
					pCategory->pchParentCategory == pCurrentCategory->pchName &&
					pCategory->eType != kMTCategory_Hidden )
				{
					eaPush(&eaAllCategories, pCategory);
					if (iPrice > 0 && iPrice < iDefPrice)
					{
						eafPush(&s_eafDiscounts, fDiscount);
						eaPush(&s_eaDiscountCategories, pCategory);
						MIN1(fOverallMinDiscount, fDiscount);
						MAX1(fOverallMaxDiscount, fDiscount);
					}
				}
			}
		}

		if(pCurrentCategory->eType != kMTCategory_Featured)
		{
			MicroTransactionUICategory *pUICat = StructCreate(parse_MicroTransactionUICategory);
			char *estrDescKey = NULL;
			estrStackCreate(&estrDescKey);
			pUICat->pchName = NULL;
			pUICat->bHideUnusable = false;
			pUICat->pchParentName = allocAddString(pCurrentCategory->pchName);
			estrPrintf(&pUICat->estrDisplayName, "%s", TranslateMessageKey("Transactions.SubCategory.All"));
			estrPrintf(&estrDescKey, "MicroTransCatDesc_%s_All", pCurrentCategory->pchName);
			estrPrintf(&pUICat->estrDescription, "%s", NULL_TO_EMPTY(TranslateMessageKey(estrDescKey)));
			estrDestroy(&estrDescKey);
			if (fOverallMinDiscount <= fOverallMaxDiscount)
			{
				pUICat->fMinDiscountPercent = fOverallMinDiscount;
				pUICat->fMaxDiscountPercent = fOverallMaxDiscount;
			}

			eaPush(peaMTCategories, pUICat);
		}

		for(iCatIdx = eaSize(&eaAllCategories)-1; iCatIdx>=0; iCatIdx--)
		{
			MicroTransactionCategory *pMTCat = eaAllCategories[iCatIdx];
			MicroTransactionUICategory *pUICat = StructCreate(parse_MicroTransactionUICategory);
			F32 fMinDiscount = 100, fMaxDiscount = -100;
			char *estrDescKey = NULL;
			estrStackCreate(&estrDescKey);
			pUICat->pchName = allocAddString(pMTCat->pchName);
			pUICat->pchParentName = allocAddString(pCurrentCategory->pchName);
			pUICat->eType = pMTCat->eType;
			pUICat->bHideUnusable = pMTCat->bHideUnusable;
			pUICat->iSortIndex = pMTCat->iSortIndex;
			estrPrintf(&pUICat->estrDisplayName, "%s", TranslateDisplayMessage(pMTCat->displayNameMesg));
			estrPrintf(&estrDescKey, "MicroTransCatDesc_%s", pMTCat->pchName);
			estrPrintf(&pUICat->estrDescription, "%s", NULL_TO_EMPTY(TranslateMessageKey(estrDescKey)));
			estrDestroy(&estrDescKey);

			for (iDiscIdx = eafSize(&s_eafDiscounts)-1; iDiscIdx>=0; iDiscIdx--)
			{
				if (eaGet(&s_eaDiscountCategories, iDiscIdx) == pMTCat)
				{
					MIN1(fMinDiscount, s_eafDiscounts[iDiscIdx]);
					MAX1(fMaxDiscount, s_eafDiscounts[iDiscIdx]);
				}
			}

			if (fMinDiscount <= fMaxDiscount)
			{
				pUICat->fMinDiscountPercent = fMinDiscount;
				pUICat->fMaxDiscountPercent = fMaxDiscount;
			}

			eaPush(peaMTCategories, pUICat);
		}

		eaQSort(*peaMTCategories, SortCategories);
	}

	ui_GenSetManagedListSafe(pGen, peaMTCategories, MicroTransactionUICategory, true);
}

//Gets the categories that are available.  Returns the index of the bonus category.  -1 of non-existent
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MicroTrans_GetCategories);
S32 microtrans_ExprGenGetCategories(SA_PARAM_NN_VALID UIGen *pGen)
{
	static MicroTransactionCategory **eaAllCategories = NULL;
	static MicroTransactionCategory **s_eaDiscountCategories = NULL;
	static F32 *s_eafDiscounts = NULL;
	MicroTransactionUICategory ***peaMTCategories = ui_GenGetManagedListSafe(pGen, MicroTransactionUICategory);
	S32 iBonusCategoryIndex = -1;

	eaClearFast(&eaAllCategories);
	eaClearStruct(peaMTCategories, parse_MicroTransactionUICategory);
	eaIndexedEnable(&eaAllCategories, parse_MicroTransactionCategory);
	eaClearFast(&s_eaDiscountCategories);
	eafClearFast(&s_eafDiscounts);

	if(g_pMTList)
	{
		S32 iIdx, iSize = eaSize(&g_pMTList->ppProducts);
		S32 iCatIdx, iDiscIdx;
		for(iIdx=0; iIdx <iSize; iIdx++)
		{
			MicroTransactionProduct *pMTProduct = eaGet(&g_pMTList->ppProducts,iIdx);
			MicroTransactionDef *pMTDef = GET_REF(pMTProduct->hDef);
			S64 iPrice = microtrans_GetPrice(pMTProduct->pProduct);
			S64 iFullPrice = microtrans_GetFullPrice(pMTProduct->pProduct);
			S64 iDefPrice = iFullPrice > 0 ? iFullPrice : iPrice;
			F32 fDiscount = iDefPrice > 0 ? (F32)iPrice / (F32)iDefPrice : 0;
			for(iCatIdx = eaSize(&pMTProduct->ppchCategories)-1; iCatIdx>=0; iCatIdx--)
			{
				MicroTransactionCategory *pCategory = RefSystem_ReferentFromString(g_hMicroTransCategoryDict, pMTProduct->ppchCategories[iCatIdx]);
				if(pCategory && pCategory->eType != kMTCategory_Main && pCategory->eType != kMTCategory_Hidden)
				{
					if(pCategory->pchParentCategory)
					{
						MicroTransactionCategory *pParentCategory = RefSystem_ReferentFromString(g_hMicroTransCategoryDict, pCategory->pchParentCategory);
						if(pParentCategory)
						{
							eaPush(&eaAllCategories, pParentCategory);
							if (iPrice > 0 && iPrice < iDefPrice)
							{
								eafPush(&s_eafDiscounts, fDiscount);
								eaPush(&s_eaDiscountCategories, pParentCategory);
							}
						}
					}
					else
					{
						eaPush(&eaAllCategories, pCategory);
						if (iPrice > 0 && iPrice < iDefPrice)
						{
							eafPush(&s_eafDiscounts, fDiscount);
							eaPush(&s_eaDiscountCategories, pCategory);
						}
					}
				}
			}
		}

		for(iCatIdx = eaSize(&eaAllCategories)-1; iCatIdx>=0; iCatIdx--)
		{
			MicroTransactionCategory *pMTCat = eaAllCategories[iCatIdx];
			MicroTransactionUICategory *pUICat = StructCreate(parse_MicroTransactionUICategory);
			F32 fMinDiscount = 100, fMaxDiscount = -100;
			char *estrDescKey = NULL;
			estrStackCreate(&estrDescKey);
			pUICat->pchName = allocAddString(pMTCat->pchName);
			pUICat->eType = pMTCat->eType;
			pUICat->bHideUnusable = pMTCat->bHideUnusable;
			pUICat->iSortIndex = pMTCat->iSortIndex;
			estrPrintf(&pUICat->estrDisplayName, "%s", TranslateDisplayMessage(pMTCat->displayNameMesg));
			estrPrintf(&estrDescKey, "MicroTransCatDesc_%s", pMTCat->pchName);
			estrPrintf(&pUICat->estrDescription, "%s", NULL_TO_EMPTY(TranslateMessageKey(estrDescKey)));
			estrDestroy(&estrDescKey);

			for (iDiscIdx = eafSize(&s_eafDiscounts)-1; iDiscIdx>=0; iDiscIdx--)
			{
				if (eaGet(&s_eaDiscountCategories, iDiscIdx) == pMTCat)
				{
					MIN1(fMinDiscount, s_eafDiscounts[iDiscIdx]);
					MAX1(fMaxDiscount, s_eafDiscounts[iDiscIdx]);
				}
			}

			if (fMinDiscount <= fMaxDiscount)
			{
				pUICat->fMinDiscountPercent = fMinDiscount;
				pUICat->fMaxDiscountPercent = fMaxDiscount;
			}

			eaPush(peaMTCategories, pUICat);
		}

		eaQSort(*peaMTCategories, SortCategories);
	}

	//Find the bonus category
	if(peaMTCategories)
	{
		for(iBonusCategoryIndex = eaSize(peaMTCategories)-1; iBonusCategoryIndex >= 0; iBonusCategoryIndex--)
		{
			if((*peaMTCategories)[iBonusCategoryIndex]->eType == kMTCategory_Bonus)
				break;
		}
	}

	ui_GenSetManagedListSafe(pGen, peaMTCategories, MicroTransactionUICategory, true);
	return iBonusCategoryIndex;
}

bool microtrans_HasNewCategory(MicroTransactionProduct *pProduct)
{
	S32 iCatIdx;
	for(iCatIdx = eaSize(&pProduct->ppchCategories)-1; iCatIdx >= 0; iCatIdx--)
	{
		MicroTransactionCategory *pCategory = RefSystem_ReferentFromString(g_hMicroTransCategoryDict, pProduct->ppchCategories[iCatIdx]);
		if(pCategory && pCategory->eType == kMTCategory_New)
			return true;
	}
	return false;
}

bool microtrans_HasFeaturedCategory(MicroTransactionProduct *pProduct)
{
	S32 iCatIdx;
	for(iCatIdx = eaSize(&pProduct->ppchCategories)-1; iCatIdx >= 0; iCatIdx--)
	{
		MicroTransactionCategory *pCategory = RefSystem_ReferentFromString(g_hMicroTransCategoryDict, pProduct->ppchCategories[iCatIdx]);
		if(pCategory && pCategory->eType == kMTCategory_Featured)
			return true;
	}
	return false;
}

const char *microtrans_GetPurchaseCategory(MicroTransactionProduct *pProduct)
{
	S32 iCatIdx;
	for(iCatIdx = 0; iCatIdx < eaSize(&pProduct->ppchCategories); iCatIdx++)
	{
		MicroTransactionCategory *pCategory = RefSystem_ReferentFromString(g_hMicroTransCategoryDict, pProduct->ppchCategories[iCatIdx]);
		if(pCategory && (pCategory->eType == kMTCategory_Normal || pCategory->eType == kMTCategory_Hidden))
			return pCategory->pchName;
	}
	return NULL;
}

AllegianceDef *microtrans_GetDisplayAllegiance(MicroTransactionProduct *pProduct)
{
	S32 iCatIdx;
	AllegianceDef *pCommonAllegiance = NULL;

	for(iCatIdx = 0; iCatIdx < eaSize(&pProduct->ppchCategories); iCatIdx++)
	{
		MicroTransactionCategory *pCategory = RefSystem_ReferentFromString(g_hMicroTransCategoryDict, pProduct->ppchCategories[iCatIdx]);
		if(pCategory && (IS_HANDLE_ACTIVE(pCategory->hDisplayAllegiance) || IS_HANDLE_ACTIVE(pCategory->hAllegiance)))
		{
			AllegianceDef *pCategoryAllegiance = GET_REF(pCategory->hDisplayAllegiance) ? GET_REF(pCategory->hDisplayAllegiance) : GET_REF(pCategory->hAllegiance);
			// Conflicting allegiances. Assume no allegiance?
			if (pCommonAllegiance && pCommonAllegiance != pCategoryAllegiance)
				return NULL;
			else if (!pCommonAllegiance)
				pCommonAllegiance = pCategoryAllegiance;
		}
	}

	return pCommonAllegiance;
}

static const char* microtrans_GetUnderlyingCategory(MicroTransactionProduct *pProduct, MicroTransactionCategory *pCategory)
{
	if( !pCategory )
		return NULL;
	if( eaFindString(&pProduct->ppchCategories, pCategory->pchName) >= 0)
		return  pCategory->pchName;
	else
	{
		MicroTransactionCategory *pCurrentCategory = NULL;
		RefDictIterator iter;
		RefSystem_InitRefDictIterator(g_hMicroTransCategoryDict, &iter);
		
		while(pCurrentCategory = (MicroTransactionCategory*) RefSystem_GetNextReferentFromIterator(&iter))
		{
			if(  pCurrentCategory->pchParentCategory == pCategory->pchName && 
				 eaFindString(&pProduct->ppchCategories, pCurrentCategory->pchName) >= 0)
				break;
		}

		return (pCurrentCategory ? pCurrentCategory->pchName : NULL);
	}
}

static int sortProducts(const MicroTransactionUIProduct **pA, const MicroTransactionUIProduct **pB)
{
	Entity *pPlayer = entActivePlayerPtr();
	int iCmp;
	AllegianceDef *pAllegiance = pPlayer ? GET_REF(pPlayer->hAllegiance) : NULL;
	AllegianceDef *pSubAllegiance = pPlayer ? GET_REF(pPlayer->hSubAllegiance) : NULL;

	// Owned items last
	iCmp = (*pA)->bOwnedProduct - (*pB)->bOwnedProduct;

	// Featured and new products first
	if (iCmp == 0)
		iCmp = ((*pB)->bFeaturedProduct || (*pB)->bNewProduct) - ((*pA)->bFeaturedProduct || (*pA)->bNewProduct);

	if (iCmp == 0 && !((*pA)->bFeaturedProduct || (*pA)->bNewProduct))
	{
		// Products with no purchase errors are second
		if (iCmp == 0)
			iCmp = ((*pB)->eCannotPurchaseType == kMicroPurchaseErrorType_None) - ((*pA)->eCannotPurchaseType == kMicroPurchaseErrorType_None);

		if (iCmp == 0 && ((*pA)->eCannotPurchaseType != kMicroPurchaseErrorType_None))
		{
			// Those with purchase errors, sort same/no allegiance to the top
			AllegianceDef *pAllegA = GET_REF((*pA)->hAllegiance);
			AllegianceDef *pAllegB = GET_REF((*pB)->hAllegiance);

			iCmp = (!pAllegB || pAllegB == pAllegiance || pAllegB == pSubAllegiance) - (!pAllegA || pAllegA == pAllegiance || pAllegA == pSubAllegiance);
		}
	}

	if (iCmp == 0)
		iCmp = stricmp((*pA)->pName, (*pB)->pName);

	if (iCmp == 0)
		iCmp = (int)(*pA)->uID - (int)(*pB)->uID;

	return iCmp;
}

static MicroTransactionUIProduct *microtrans_FindUIProd(MicroTransactionUIProduct ***peaList, U32 uID)
{
	S32 iIdx;
	MicroTransactionUIProduct *pProduct = NULL;
	for(iIdx = eaSize(peaList)-1; iIdx >= 0; iIdx--)
	{
		pProduct = (*peaList)[iIdx];
		if(pProduct && pProduct->uID == uID)
			break;
	}
	
	return iIdx >= 0 ? pProduct : NULL;
}

//TODO: remove the following function eventually (gens are still relying on setting these bits)
void MicroTransUI_SetProductFlags(MicroTransactionUIProduct *pUIProduct)
{
	pUIProduct->bUniqueInInv = pUIProduct->eCannotPurchaseType == kMicroPurchaseErrorType_Unique;
	pUIProduct->bItemRestrictions = pUIProduct->eCannotPurchaseType == kMicroPurchaseErrorType_UsageRestrictions
								||	pUIProduct->eCannotPurchaseType == kMicroPurchaseErrorType_Allegiance;
	pUIProduct->bCannotPurchaseAgain = pUIProduct->eCannotPurchaseType == kMicroPurchaseErrorType_CannotPurchaseAgain
								|| pUIProduct->eCannotPurchaseType == kMicroPurchaseErrorType_AlreadyEntitled;
	pUIProduct->bAlreadyEntitled = pUIProduct->eCannotPurchaseType == kMicroPurchaseErrorType_AlreadyEntitled;

	pUIProduct->bFailsExpression = pUIProduct->eCannotPurchaseType == kMicroPurchaseErrorType_FailsExpressionRequirement;
	pUIProduct->bNotEnoughCurrency = pUIProduct->eCannotPurchaseType == kMicroPurchaseErrorType_NotEnoughCurrency;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetProductList);
void microtrans_ExprGenGetProductList(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_NN_STR const char *pCategory)
{
	MicroTransactionUIProduct ***peaMTList = ui_GenGetManagedListSafe(pGen, MicroTransactionUIProduct);
	MicroTransactionCategory *pMTCategory = microtrans_CategoryFromStr(pCategory);
	S32 i;

	for(i = eaSize(peaMTList)-1; i >= 0; i--)
	{
		MicroTransactionUIProduct *pMTProd = eaGet(peaMTList, i);
		if(pMTProd)
		{
			pMTProd->bUpdated = false;
		}
	}

	if(pMTCategory && g_pMTList)
	{
		S32 iIdx, iSize = eaSize(&g_pMTList->ppProducts);
		S64 iPrice;
		for(iIdx = 0; iIdx < iSize; iIdx++)
		{
			MicroTransactionProduct *pMTProduct = g_pMTList->ppProducts[iIdx];
			MicroTransactionDef *pDef = GET_REF(pMTProduct->hDef);
			bool bMeetsRequirements = true;

			MicroTransactionUIProduct *pUIProduct = NULL;
			const char *pchUnderlyingCategory = microtrans_GetUnderlyingCategory(pMTProduct, pMTCategory);

			if(!pMTProduct->pProduct || !pchUnderlyingCategory || !pDef)
				continue;

			//Find out if I meet the requirements
			bMeetsRequirements = gclMicroTrans_PrerequisitesMet(pMTProduct);

			//If I don't want to see things I can't purchase (pre-order bonuses etc)
			if(pMTCategory->bHideUnusable && !bMeetsRequirements)
				continue;

			//If the microtransaction has a required purchase and it hasn't been purchased, then hide it
			//TODO(BH): Add the ability to see them even if you don't meet the purchase requirement?
			if(IS_HANDLE_ACTIVE(pDef->hRequiredPurchase) && !microtrans_HasPurchased(entity_GetGameAccount(pEnt), GET_REF(pDef->hRequiredPurchase)))
				continue;

			//Get the price
			iPrice = microtrans_GetPrice(pMTProduct->pProduct);

			//Not a good price
			if(iPrice < 0)
				continue;

			pUIProduct = microtrans_FindUIProd(peaMTList, pMTProduct->pProduct->uID);
			if(!pUIProduct)
			{
				pUIProduct = StructCreate(parse_MicroTransactionUIProduct);
				pUIProduct->uID = pMTProduct->pProduct->uID;
				
				eaPush(peaMTList, pUIProduct);
			}

			if(gclMicroTrans_SetUIProduct(pEnt, pUIProduct, pMTProduct, pchUnderlyingCategory))
				pUIProduct->bUpdated = true;
		}
	}

	for(i=eaSize(peaMTList)-1; i>=0; i--)
	{
		MicroTransactionUIProduct *pUIProduct = (*peaMTList)[i];
		if(!pUIProduct->bUpdated)
		{
			StructDestroy(parse_MicroTransactionUIProduct, eaRemove(peaMTList, i));
		}
	}

	eaQSort(*peaMTList, sortProducts);

	ui_GenSetManagedListSafe(pGen, peaMTList, MicroTransactionUIProduct, true);
}

static void microtrans_HasEnoughCurrency(Entity *pEnt, MicroTransactionProduct *pMTProduct, MicroTransactionDef *pDef, MicroPurchaseErrorType *pError, char** pestrError)
{
	if(pError && *pError == kMicroPurchaseErrorType_None && pDef && pMTProduct)
	{
		S64 iCurrency;
		S64 iPrice;

		iCurrency = gclAPExprAccountGetKeyValueInt(microtrans_GetShardCurrency());
		iPrice = microtrans_GetPrice(pMTProduct->pProduct);
		if(iPrice < 0 || iCurrency < iPrice)
		{
			*pError = kMicroPurchaseErrorType_NotEnoughCurrency;
			microtrans_GetCanPurchaseErrorString( *pError, pDef, locGetLanguage(getCurrentLocale()), pestrError);
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenProduct_GetAllIcons);
void gclMicroTrans_expr_GenProduct_GetAllIcons(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID MicroTransactionUIProduct *pProduct)
{
	MicroTransactionUIIcon ***peaIconList = ui_GenGetManagedListSafe(pGen, MicroTransactionUIIcon);
	eaClearStruct(peaIconList, parse_MicroTransactionUIIcon);

	if(pProduct)
	{
		int i, n = eaSize(&pProduct->ppIcons);
		for(i = 0; i < n; i++)
		{
			MicroTransactionUIIcon *pIcon = StructCreate(parse_MicroTransactionUIIcon);
			pIcon->pchIcon = allocAddString(pProduct->ppIcons[i]);
			pIcon->iIndex = i;
			eaPush(peaIconList, pIcon);
		}
	}

	ui_GenSetManagedListSafe(pGen, peaIconList, MicroTransactionUIIcon, true);
}

// if too slow then cach item discount %
static S64 microtrans_PriceWithCoupon(Entity *pEntity, S64 iPrice, U32 uProductID, S64 iFullPrice)
{
	S64 iDiscount = 0;

	Item *pItem = inv_GetItemByID(pEntity, s_CouponInfo.uCouponItem);
	if(pEntity && pItem)
	{
		if(MicroTrans_ValidDiscountItem(pEntity, uProductID, pItem, &g_pMTList->ppProducts))
		{
			iDiscount = MicroTrans_GetItemDiscount(pEntity, pItem);
			if(iPrice < iFullPrice)
			{
				// coupons always use full price for calculating final price
				iPrice = iFullPrice;
			}
		}
	}
	else
	{
		// no coupon
		return iPrice;
	}

	iPrice *= (100 * MT_DISCOUNT_PER_PERCENT) - iDiscount;
	iPrice /= 100 * MT_DISCOUNT_PER_PERCENT;

	return iPrice;
}

bool gclMicroTrans_SetUIProduct(Entity *pEnt, MicroTransactionUIProduct *pUIProduct, MicroTransactionProduct *pMTProduct, const char *pchUnderlyingCategory)
{
	if (pUIProduct && pMTProduct && pMTProduct->pProduct)
	{
		MicroTransactionDef *pDef = GET_REF(pMTProduct->hDef);
		S64 iFullPrice;
		S64 iPrice;
		AllegianceDef *pAllegiance;
		const char *pchPurchaseCategory = pchUnderlyingCategory;

		//Get the price
		iPrice = microtrans_GetPrice(pMTProduct->pProduct);

		//Not a good price
		if(iPrice < 0)
		{
			if (pUIProduct->pName)
				StructReset(parse_MicroTransactionUIProduct, pUIProduct);
			return false;
		}

		estrClear(&pUIProduct->estrCannotPurchaseReason);
		pUIProduct->eCannotPurchaseType = microtrans_GetCanPurchaseError(entGetPartitionIdx_NoAssert(pEnt), pEnt, entity_GetGameAccount(pEnt), pDef,
			locGetLanguage(getCurrentLocale()),
			&pUIProduct->estrCannotPurchaseReason);

		if(pUIProduct->eCannotPurchaseType == kMicroPurchaseErrorType_FailsExpressionRequirement && pDef->bUseBuyExprForVisibility)
			return false;

		iFullPrice = microtrans_GetFullPrice(pMTProduct->pProduct);

		iPrice = microtrans_PriceWithCoupon(pEnt, iPrice, pMTProduct->uID, iFullPrice);

		pUIProduct->uID = pMTProduct->uID;
		pUIProduct->iPrice = iPrice;
		pUIProduct->iDefPrice = iFullPrice > 0 ? iFullPrice : iPrice;

		//Find out if I meet the requirements
		pUIProduct->bPrerequisitesMet = gclMicroTrans_PrerequisitesMet(pMTProduct);

		if (!pchPurchaseCategory)
			pchPurchaseCategory = microtrans_GetPurchaseCategory(pMTProduct);

		if (pchPurchaseCategory)
			SET_HANDLE_FROM_STRING(g_hMicroTransCategoryDict, pchPurchaseCategory, pUIProduct->hCategory);
		else if (IS_HANDLE_ACTIVE(pUIProduct->hCategory))
			REMOVE_HANDLE(pUIProduct->hCategory);

		pAllegiance = microtrans_GetDisplayAllegiance(pMTProduct);
		if (pAllegiance)
			SET_HANDLE_FROM_REFERENT(g_hAllegianceDict, pAllegiance, pUIProduct->hAllegiance);
		else if (IS_HANDLE_ACTIVE(pUIProduct->hAllegiance))
			REMOVE_HANDLE(pUIProduct->hAllegiance);

		pUIProduct->bNewProduct = microtrans_HasNewCategory(pMTProduct);
		pUIProduct->bFeaturedProduct = microtrans_HasFeaturedCategory(pMTProduct);
		pUIProduct->bPremiumEntitlement = microtrans_IsPremiumEntitlement(pDef);

		if(pDef)
		{
			int i, n = eaSize(&pDef->eaParts);
			int iInventorySize = 0;
			estrPrintf(&pUIProduct->pName, "%s", TranslateDisplayMessage(pDef->displayNameMesg));
			estrPrintf(&pUIProduct->pDescription, "%s", TranslateDisplayMessage(pDef->descriptionShortMesg));
			estrPrintf(&pUIProduct->pLongDescription, "%s", TranslateDisplayMessage(pDef->descriptionLongMesg));

			pUIProduct->pchIcon = allocAddString(pDef->pchIconSmall);

			eaClearFast(&pUIProduct->ppIcons);

			if(pDef->pchIconLarge)
				eaPush(&pUIProduct->ppIcons, pDef->pchIconLarge);
			else if(pDef->pchIconSmall)
				eaPush(&pUIProduct->ppIcons, pDef->pchIconSmall);

			if(pDef->pchIconLargeSecond)
				eaPush(&pUIProduct->ppIcons, pDef->pchIconLargeSecond);
			if(pDef->pchIconLargeThird)
				eaPush(&pUIProduct->ppIcons, pDef->pchIconLargeThird);

			for(i = 0; i < n; i++)
			{
				MicroTransactionPart *pPart = eaGet(&pDef->eaParts, i);
				if (!pPart)
					continue;

				if (pPart->pchIconPart)
					eaPush(&pUIProduct->ppIcons, pPart->pchIconPart);

				if (pDef->eaParts[i]->ePartType == kMicroPart_Item && GET_REF(pDef->eaParts[i]->hItemDef))
				{
					InventorySlot *pConstSlot = eaGetStruct(&pUIProduct->eaInventory, parse_InventorySlot, iInventorySize++);
					NOCONST(InventorySlot) *pSlot = CONTAINER_NOCONST(InventorySlot, pConstSlot);
					ItemDef *pItemDef = GET_REF(pDef->eaParts[i]->hItemDef);
					if (!pSlot->pItem || GET_REF(pSlot->pItem->hItem) != pItemDef)
					{
						// If the item is randomized, ensure that it doesn't change every time this item is randomized for the same item def
						U32 uSeed = (U32)pUIProduct + iInventorySize;
						StructDestroyNoConstSafe(parse_Item, &pSlot->pItem);
						pSlot->pItem = inv_ItemInstanceFromDefName(pItemDef->pchName, entity_GetSavedExpLevel(pEnt), 0, NULL, NULL, NULL, false, &uSeed);
					}
					if (pSlot->pItem)
						pSlot->pItem->count = pDef->eaParts[i]->iCount;
				}
			}

			eaSetSizeStruct(&pUIProduct->eaInventory, parse_InventorySlot, iInventorySize);
		}
		else
		{
			eaClear(&pUIProduct->ppIcons);
			estrClear(&pUIProduct->pName);
			estrClear(&pUIProduct->pDescription);
			eaDestroyStruct(&pUIProduct->eaInventory, parse_InventorySlot);
		}

		// client side only check for enough currency
		microtrans_HasEnoughCurrency(pEnt, pMTProduct, pDef, &pUIProduct->eCannotPurchaseType, &pUIProduct->estrCannotPurchaseReason);
		pUIProduct->bOwnedProduct = microtrans_AlreadyPurchased(pEnt, entity_GetGameAccount(pEnt), pDef);
		MicroTransUI_SetProductFlags(pUIProduct);
		pUIProduct->iUpdate = gGCLState.totalElapsedTimeMs;
		return true;
	}
	else if (pUIProduct->pName)
	{
		StructReset(parse_MicroTransactionUIProduct, pUIProduct);
	}
	return false;
}

MicroTransactionUIProduct *gclMicroTrans_MakeUIProduct(U32 uID)
{
	static MicroTransactionUIProduct *s_pBuffer = NULL;
	Entity *pEnt = entActivePlayerPtr();

	if (g_pMTList)
	{
		S32 iIdx, iSize = eaSize(&g_pMTList->ppProducts);
		for (iIdx = 0; iIdx < iSize; iIdx++)
		{
			if (g_pMTList->ppProducts[iIdx]->uID == uID)
			{
				MicroTransactionUIProduct *pUIProduct = s_pBuffer ? s_pBuffer : StructCreate(parse_MicroTransactionUIProduct);
				s_pBuffer = NULL;

				if (!gclMicroTrans_SetUIProduct(pEnt, pUIProduct, g_pMTList->ppProducts[iIdx], NULL))
				{
					// Save the unused struct
					s_pBuffer = pUIProduct;
					return NULL;
				}

				return pUIProduct;
			}
		}
	}

	return NULL;
}

void gclMicroTrans_UpdateUIProduct(U32 uID, MicroTransactionUIProduct **ppUIProduct)
{
	static MicroTransactionUIProduct *s_pBuffer = NULL;
	Entity *pEnt = entActivePlayerPtr();

	if (g_pMTList && ppUIProduct)
	{
		S32 iIdx, iSize = eaSize(&g_pMTList->ppProducts);
		for (iIdx = 0; iIdx < iSize; iIdx++)
		{
			if (g_pMTList->ppProducts[iIdx]->uID == uID)
			{
				MicroTransactionUIProduct *pUIProduct = NULL;
				if (*ppUIProduct)
				{
					pUIProduct = *ppUIProduct;
					*ppUIProduct = NULL;
				}
				else if (s_pBuffer)
				{
					pUIProduct = s_pBuffer;
					s_pBuffer = NULL;
				}
				else
				{
					pUIProduct = StructCreate(parse_MicroTransactionUIProduct);
				}

				if (!gclMicroTrans_SetUIProduct(pEnt, pUIProduct, g_pMTList->ppProducts[iIdx], NULL))
				{
					// Save the unused struct
					if (s_pBuffer)
						StructDestroy(parse_MicroTransactionUIProduct, s_pBuffer);
					s_pBuffer = pUIProduct;
					pUIProduct = NULL;
				}

				*ppUIProduct = pUIProduct;
			}
		}
	}
}

SA_RET_OP_VALID static MicroTransactionUIProduct *gclMicroTrans_GetProduct(ExprContext *pContext, SA_PARAM_OP_VALID UIGen *pGen, U32 uID)
{
	static MicroTransactionUIProduct s_UIProductStatic = {0};
	static U32 s_iFrame;
	Entity *pEnt = entActivePlayerPtr();
	MicroTransactionUIProduct *pUIProduct = &s_UIProductStatic;

	if (pGen)
		pUIProduct = ui_GenGetManagedPointer(pGen, parse_MicroTransactionUIProduct);

	if (pUIProduct->uID == uID && pUIProduct->iUpdate == gGCLState.totalElapsedTimeMs)
	{
		if (pGen)
			ui_GenSetManagedPointer(pGen, pUIProduct, parse_MicroTransactionUIProduct, true);
		return pUIProduct;
	}

	if (g_pMTList)
	{
		S32 iIdx, iSize = eaSize(&g_pMTList->ppProducts);
		for (iIdx = 0; iIdx < iSize; iIdx++)
		{
			if (g_pMTList->ppProducts[iIdx]->uID == uID)
			{
				MicroTransactionProduct *pMTProduct = g_pMTList->ppProducts[iIdx];

				if (gclMicroTrans_IsProductHidden(pMTProduct) || !gclMicroTrans_SetUIProduct(pEnt, pUIProduct, pMTProduct, NULL))
				{
					if (pGen)
						ui_GenSetPointer(pGen, NULL, parse_MicroTransactionUIProduct);
					return NULL;
				}

				if (pGen)
					ui_GenSetManagedPointer(pGen, pUIProduct, parse_MicroTransactionUIProduct, true);
				return pUIProduct;
			}
		}
	}

	if (pUIProduct->pName)
		StructReset(parse_MicroTransactionUIProduct, pUIProduct);

	if (pGen)
		ui_GenSetPointer(pGen, NULL, parse_MicroTransactionUIProduct);
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MicroTransProduct);
SA_RET_OP_VALID MicroTransactionUIProduct *gclMicroTrans_expr_GetProduct(ExprContext *pContext, U32 uID)
{
	return gclMicroTrans_GetProduct(pContext, NULL, uID);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSetMicroTransProduct);
SA_RET_OP_VALID MicroTransactionUIProduct *gclMicroTrans_expr_GenGetProduct(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, U32 uID)
{
	return gclMicroTrans_GetProduct(pContext, pGen, uID);
}

void gclMicroTrans_GetProductList(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt, MicroTransactionProduct **ppProducts)
{
	MicroTransactionUIProduct ***peaMTList = ui_GenGetManagedListSafe(pGen, MicroTransactionUIProduct);
	S32 i;

	for(i = eaSize(peaMTList)-1; i >= 0; i--)
	{
		MicroTransactionUIProduct *pMTProd = eaGet(peaMTList, i);
		if(pMTProd)
		{
			pMTProd->bUpdated = false;
		}
	}

	if (g_pMTList)
	{
		S32 iIdx, iSize = eaSize(&ppProducts);
		S64 iPrice;
		for (iIdx = 0; iIdx < iSize; iIdx++)
		{
			MicroTransactionProduct *pMTProduct = ppProducts[iIdx];
			MicroTransactionDef *pDef = GET_REF(pMTProduct->hDef);
			bool bMeetsRequirements = true;

			MicroTransactionUIProduct *pUIProduct = NULL;

			//Find out if I meet the requirements
			bMeetsRequirements = gclMicroTrans_PrerequisitesMet(pMTProduct);

			//Get the price
			iPrice = microtrans_GetPrice(pMTProduct->pProduct);

			//Not a good price
			if(iPrice < 0)
				continue;

			pUIProduct = microtrans_FindUIProd(peaMTList, pMTProduct->pProduct->uID);
			if(!pUIProduct)
			{
				pUIProduct = StructCreate(parse_MicroTransactionUIProduct);
				pUIProduct->uID = pMTProduct->pProduct->uID;

				eaPush(peaMTList, pUIProduct);
			}

			gclMicroTrans_SetUIProduct(pEnt, pUIProduct, pMTProduct, NULL);

			pUIProduct->bUpdated = true;
		}
	}

	for(i=eaSize(peaMTList)-1; i>=0; i--)
	{
		MicroTransactionUIProduct *pUIProduct = (*peaMTList)[i];
		if(!pUIProduct->bUpdated)
		{
			StructDestroy(parse_MicroTransactionUIProduct, eaRemove(peaMTList, i));
		}
	}

	eaQSort(*peaMTList, sortProducts);

	ui_GenSetManagedListSafe(pGen, peaMTList, MicroTransactionUIProduct, true);
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MicroTrans_GetMapMoveProduct);
S32 gclMicroTrans_expr_GetMapMoveProduct(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt)
{
	const MicroTransactionRef **ppRef = gclMapMoveConfirm_Microtransactions();
	static MicroTransactionUIProduct s_UIProduct = {0};
	MicroTransactionUIProduct *pUIProduct = NULL;

	if(eaSize(&ppRef) && g_pMTList)
	{
		int iIdx;
		S64 iBestPrice = LLONG_MAX;
		
		FOR_EACH_IN_EARRAY(ppRef, const MicroTransactionRef, pRef)
		{
			MicroTransactionDef *pDef = GET_REF(pRef->hMTDef);
			if(!pDef)
				continue;

			for(iIdx = eaSize(&g_pMTList->ppProducts)-1; iIdx>=0; iIdx--)
			{
				if(GET_REF(g_pMTList->ppProducts[iIdx]->hDef) == pDef)
				{
					if (gclMicroTrans_SetUIProduct(pEnt, &s_UIProduct, g_pMTList->ppProducts[iIdx], NULL))
					{
						if(s_UIProduct.eCannotPurchaseType != kMicroPurchaseErrorType_None || !s_UIProduct.bPrerequisitesMet)
							continue;
						
						if(!pUIProduct || s_UIProduct.iPrice < pUIProduct->iPrice)
						{
							StructDestroySafe(parse_MicroTransactionUIProduct, &pUIProduct);
						
							pUIProduct = StructClone(parse_MicroTransactionUIProduct, &s_UIProduct);
						}
					}
				}
			}
		} FOR_EACH_END;
	}

	ui_GenSetManagedPointer(pGen, pUIProduct, parse_MicroTransactionUIProduct, true);

	return (pUIProduct != NULL) ? 1 : 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MicroTrans_ShowProduct);
bool gclMicroTrans_expr_ShowProduct(U32 uID)
{
	MicroTransactionProduct *pProduct = NULL;

	if (g_pMTList)
	{
		S32 i, n = eaSize(&g_pMTList->ppProducts);
		for (i = 0; i < n; i++)
		{
			if (g_pMTList->ppProducts[i]->uID == uID)
			{
				pProduct = g_pMTList->ppProducts[i];
				break;
			}
		}
	}

	if (pProduct)
	{
		globCmdParsef("MicroTrans_ShowProduct %d", pProduct->uID);
		return true;
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MicroTrans_GetID);
U32 gclMicroTrans_expr_GetID(const char *pchName)
{
	if (g_pMTList)
	{
		S32 i, n = eaSize(&g_pMTList->ppProducts);
		for (i = 0; i < n; i++)
		{
			const char *pchOtherName = REF_STRING_FROM_HANDLE(g_pMTList->ppProducts[i]->hDef);
			if (pchOtherName && stricmp(pchName, pchOtherName) == 0)
			{
				return g_pMTList->ppProducts[i]->uID;
			}
		}
	}
	return 0;
}

AUTO_COMMAND ACMD_NAME(MicroTrans_ShowProductByName) ACMD_ACCESSLEVEL(0) ACMD_HIDE;
bool gclMicroTrans_expr_ShowProductByName(const char *pchName);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MicroTrans_ShowProductByName);
bool gclMicroTrans_expr_ShowProductByName(const char *pchName)
{
	U32 uID = gclMicroTrans_expr_GetID(pchName);
	return gclMicroTrans_expr_ShowProduct(uID);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MicroTransProductByName);
SA_RET_OP_VALID MicroTransactionUIProduct *gclMicroTrans_expr_GetProductByName(ExprContext *pContext, const char *pchName)
{
	U32 uID = gclMicroTrans_expr_GetID(pchName);
	return gclMicroTrans_GetProduct(pContext, NULL, uID);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSetMicroTransProductByName);
SA_RET_OP_VALID MicroTransactionUIProduct *gclMicroTrans_expr_GenGetProductByName(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchName)
{
	U32 uID = gclMicroTrans_expr_GetID(pchName);
	return gclMicroTrans_GetProduct(pContext, pGen, uID);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MessageFormatMicroTransProduct);
const char *gclMicroTrans_expr_MessageFormatProduct(ExprContext *pContext, ACMD_EXPR_DICT(Message) const char *pchMessageKey, U32 uID)
{
	static char *s_pch = NULL;
	MicroTransactionProduct *pProduct = g_pMTList ? eaIndexedGetUsingInt(&g_pMTList->ppProducts, uID) : NULL;

	estrClear(&s_pch);
	if (pProduct)
	{
		FormatGameMessageKey(&s_pch, pchMessageKey, STRFMT_MICROTRANSACTION_KEY("Value", pProduct), STRFMT_END);
	}

	return s_pch;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(StringFormatMicroTransProduct);
const char *gclMicroTrans_expr_StringFormatProduct(ExprContext *pContext, const char *pchFormat, U32 uID)
{
	static char *s_pch = NULL;
	MicroTransactionProduct *pProduct = g_pMTList ? eaIndexedGetUsingInt(&g_pMTList->ppProducts, uID) : NULL;

	estrClear(&s_pch);
	if (pProduct)
	{
		FormatGameString(&s_pch, pchFormat, STRFMT_MICROTRANSACTION_KEY("Value", pProduct), STRFMT_END);
	}

	return s_pch;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MessageFormatMicroTransProductByName);
const char *gclMicroTrans_expr_MessageFormatProductByName(ExprContext *pContext, ACMD_EXPR_DICT(Message) const char *pchMessageKey, const char *pchName)
{
	U32 uID = gclMicroTrans_expr_GetID(pchName);
	return gclMicroTrans_expr_MessageFormatProduct(pContext, pchMessageKey, uID);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(StringFormatMicroTransProductByName);
const char *gclMicroTrans_expr_StringFormatProductByName(ExprContext *pContext, const char *pchFormat, const char *pchName)
{
	U32 uID = gclMicroTrans_expr_GetID(pchName);
	return gclMicroTrans_expr_StringFormatProduct(pContext, pchFormat, uID);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MicroTrans_HasCostumeUnlock);
bool gclMicroTrans_expr_HasCostumeUnlock(U32 uID)
{
	if (g_pMTCostumes)
	{
		S32 iIdx, iNum = eaSize(&g_pMTCostumes->ppCostumes);
		for (iIdx = 0; iIdx < iNum; iIdx++)
		{
			if (eaIndexedFindUsingInt(&g_pMTCostumes->ppCostumes[iIdx]->eaSources, uID) >= 0)
			{
				return true;
			}
		}
	}

	return false;
}

AUTO_STRUCT;
typedef struct MicroTrans_PaymentMethodUI
{
	char *pchID;							// Vindicia's payment ID
	char *pchDescription;					// Customer entered description of the account
	char *pchType;							// Payment type (visa, MC etc)
	char *pchNameOnAccount;					// Customer's name
	char *pchCurrency;						// Currency used
	
	char *pchCCPrefix;						// First 6 digits of the card number
	char *pchCCSuffix;	 					// Last 4 or 5 digits of the card number.
	char *pchCCExpDate;						// Credit card expiration date
	bool bSteamWallet;						// Flag indicating this is a Steam Wallet method
} MicroTrans_PaymentMethodUI;

static void GetPaymentMethods(MicroTrans_PaymentMethodUI ***peaMethods)
{
	int i;

	eaClearStruct(peaMethods, parse_MicroTrans_PaymentMethodUI);

	for(i=0; i< eaSize(&g_eaPaymentMethods); i++)
	{
		CachedPaymentMethod *pMethod = g_eaPaymentMethods[i]; 
		MicroTrans_PaymentMethodUI *pUIMethod = StructCreate(parse_MicroTrans_PaymentMethodUI);

		pUIMethod->pchID = StructAllocString(pMethod->VID);
		pUIMethod->pchDescription = StructAllocString(pMethod->description);
		pUIMethod->pchNameOnAccount = StructAllocString(pMethod->accountName);
		pUIMethod->pchType = StructAllocString(pMethod->type);
		pUIMethod->pchCurrency = StructAllocString(pMethod->currency);

		if(pMethod->creditCard)
		{
			pUIMethod->pchCCPrefix = StructAllocString(pMethod->creditCard->bin);
			pUIMethod->pchCCSuffix = StructAllocString(pMethod->creditCard->lastDigits);
			pUIMethod->pchCCExpDate = StructAllocString(pMethod->creditCard->expireDate);
		}
		else if(pMethod->methodProvider == TPROVIDER_Steam)
		{
			pUIMethod->bSteamWallet = true;
		}
		eaPush(peaMethods, pUIMethod);
	}
}
	

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetMicroTransPaymentMethods);
void gclMicrotrans_expr_GenGetPaymentMethods(SA_PARAM_NN_VALID UIGen *pGen)
{
	static MicroTrans_PaymentMethodUI **s_eaMethods = NULL;
	
	GetPaymentMethods(&s_eaMethods);

	ui_GenSetManagedListSafe(pGen, &s_eaMethods, MicroTrans_PaymentMethodUI, false);
}

AUTO_STRUCT;
typedef struct MicroTrans_PointBuyProduct
{
	U32 uID;				AST(KEY)
		// The ID of the micro transaction

	char *pName;			AST(ESTRING)
		//The name of the micro transaction
	char *pDescription;		AST(ESTRING)
		// The description of the micro transaction
	char *pchCategory;		AST(ESTRING)
		//This is the underlying category (The sub category or the main category).  Important when attempting to purchase the product

	char *pchCurrency;		AST(ESTRING)
		//The currency of this product

	char *estrCost;			AST(ESTRING)
		//The cost of this product (in the currency specified)

	S64 sCost;				NO_AST
		//The internal cost of this product (for sorting)

	bool bUpdated;
} MicroTrans_PointBuyProduct;

static void MicroTrans_GetPointBuyPrice(AccountProxyProduct *pProduct, const char *pchCurrency, char **estrOutput, S64 *sCost)
{
	const Money *pPrice;

	if (!devassert(pProduct)) return;

	pPrice = findMoneyFromCurrency(pProduct->ppMoneyPrices, pchCurrency);

	if (pPrice)
	{
		*sCost = pPrice->Internal._internal_SubdividedAmount;
		estrFromMoney(estrOutput, pPrice);
	}
}

static int SortByCost(const MicroTrans_PointBuyProduct **pA, const MicroTrans_PointBuyProduct **pB)
{
	return (*pA)->sCost - (*pB)->sCost;
}

static bool MicroTrans_CheckProductCategory(SA_PARAM_NN_VALID AccountProxyProduct *pProduct, SA_PARAM_NN_STR const char *pchCategory)
{
	EARRAY_FOREACH_BEGIN(pProduct->ppCategories, i);
	{
		if (!stricmp_safe(pProduct->ppCategories[i], pchCategory))
			return true;
	}
	EARRAY_FOREACH_END;

	return false;
}

// Gets the point buy products in the currency specified
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetPointBuyProducts);
void gclMicrotrans_expr_GenGetPointBuyProducts(SA_PARAM_NN_VALID UIGen *pGen, const char *pchCurrency, S32 bSteamPurchase)
{
	MicroTrans_PointBuyProduct ***peaProducts = ui_GenGetManagedListSafe(pGen, MicroTrans_PointBuyProduct);
	int i;

	for (i = eaSize(peaProducts) - 1; i >= 0; i--)
	{
		MicroTrans_PointBuyProduct *pProduct = eaGet(peaProducts, i);
		if (pProduct)
			pProduct->bUpdated = false;
	}

	eaIndexedEnable(peaProducts, parse_MicroTrans_PointBuyProduct);

	if (g_pPointBuyList)
	{
		for (i=0; i < eaSize(&g_pPointBuyList->ppList); i++)
		{
			AccountProxyProduct *pProduct = g_pPointBuyList->ppList[i];
			MicroTrans_PointBuyProduct *pUIProduct = eaIndexedGetUsingInt(peaProducts, pProduct->uID);
			char *estrCost = NULL;
			S64 sCost = 0;

			MicroTrans_GetPointBuyPrice(pProduct, pchCurrency, &estrCost, &sCost);

			if (!estrCost ||
				!eaSize(&pProduct->ppCategories) ||
				!MicroTrans_CheckProductCategory(pProduct, bSteamPurchase ? microtrans_GetShardPointBuySteamCategory() : microtrans_GetShardPointBuyCategory()))
			{
				estrDestroy(&estrCost);
				continue;
			}

			if (!pUIProduct)
			{
				pUIProduct = StructCreate(parse_MicroTrans_PointBuyProduct);
				pUIProduct->uID = pProduct->uID;
				microtrans_DEPR_GetLocalizedInfo(pProduct, &pUIProduct->pName, &pUIProduct->pDescription);

				pUIProduct->pchCategory = estrCreateFromStr(pProduct->ppCategories[0]);
				
				eaPush(peaProducts, pUIProduct);
			}

			estrCopy(&pUIProduct->estrCost,&estrCost);
			estrDestroy(&estrCost);

			pUIProduct->sCost = sCost;
			pUIProduct->bUpdated = true;
		}
	}

	for (i=eaSize(peaProducts)-1; i>=0; i--)
	{
		MicroTrans_PointBuyProduct *pProduct = eaGet(peaProducts, i);
		if (pProduct && pProduct->bUpdated)
			continue;

		StructDestroy(parse_MicroTrans_PointBuyProduct, eaRemove(peaProducts, i));
	}
	
	eaIndexedDisable(peaProducts);
	eaQSort(*peaProducts, SortByCost);

	ui_GenSetManagedListSafe(pGen, peaProducts, MicroTrans_PointBuyProduct, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenCanPurchasePoints);
bool gclMicrotrans_expr_GenCanPurchasePoints()
{
	if (!g_pPointBuyList || !eaSize(&g_pPointBuyList->ppList) || !eaSize(&g_eaPaymentMethods))
		return false;
	if (!microtrans_SteamWalletEnabled() || !gclSteamIsSubscribedApp(STEAM_CURRENT_APP))
		return false;
	return true;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MicroTrans_GetPaymentMethodCount);
int gclMicrotrans_expr_GetPaymentMethodCount()
{
	return eaSize(&g_eaPaymentMethods);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetPaymentMethod);
int gclMicrotrans_expr_GetPaymentMethod(SA_PARAM_NN_VALID UIGen *pGen, int index)
{
	static MicroTrans_PaymentMethodUI **s_eaMethods = NULL;
	MicroTrans_PaymentMethodUI *pMethod = NULL;
	
	GetPaymentMethods(&s_eaMethods);

	pMethod = eaGet(&s_eaMethods, index);

	if(pMethod)
	{
		ui_GenSetPointer(pGen, pMethod, parse_MicroTrans_PaymentMethodUI);
		return 1;
	}
	else
	{

		ui_GenSetPointer(pGen, NULL, parse_MicroTrans_PaymentMethodUI);
		return 0;
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MicroTransIsOwned);
bool gclMicroTrans_expr_MicroTransIsOwned(U32 uID)
{
	MicroTransactionProduct *pProduct = NULL;
	MicroTransactionDef *pDef;
	Entity *pEnt = entActivePlayerPtr();

	if (!g_pMTList)
	{
		return false;
	}

	pProduct = eaIndexedGetUsingInt(&g_pMTList->ppProducts, uID);
	if (!pProduct)
	{
		return false;
	}

	pDef = GET_REF(pProduct->hDef);
	if (!pDef)
	{
		return false;
	}

	return microtrans_AlreadyPurchased(pEnt, entity_GetGameAccount(pEnt), pDef);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MicroTransIsOwnedByName);
bool gclMicroTrans_expr_MicroTransIsOwnedByName(const char *pchName)
{
	U32 uID = gclMicroTrans_expr_GetID(pchName);
	return gclMicroTrans_expr_MicroTransIsOwned(uID);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MicroTransProductIsOwned);
bool gclMicroTrans_expr_MicroTransactionUIProductIsOwned(SA_PARAM_OP_VALID MicroTransactionUIProduct *pProduct)
{
	return pProduct->bOwnedProduct;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MicroTransProductGetAllegiance);
const char* gclMicroTrans_expr_MicroTransactionUIProductGetAllegiance(SA_PARAM_OP_VALID MicroTransactionUIProduct *pProduct)
{
	return pProduct ? REF_STRING_FROM_HANDLE(pProduct->hAllegiance) : "";
}

// Returns the first item on the microtransaction.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MicroTransProductGetItem);
const char* gclMicroTrans_expr_MicroTransProductGetItem(SA_PARAM_OP_VALID MicroTransactionUIProduct *pProduct)
{
	if (g_pMTList && pProduct)
	{
		MicroTransactionProduct *pMTProduct = eaIndexedGetUsingInt(&g_pMTList->ppProducts, pProduct->uID);
		MicroTransactionDef *pDef = pMTProduct ? GET_REF(pMTProduct->hDef) : NULL;
		if (pDef)
		{
			S32 i;
			for (i = 0; i < eaSize(&pDef->eaParts); i++)
			{
				if (pDef->eaParts[i]->ePartType == kMicroPart_Item)
				{
					return REF_STRING_FROM_HANDLE(pDef->eaParts[i]->hItemDef);
				}
			}
		}
	}
	return "";
}

// Returns the quality of the first item on the microtransaction.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MicroTransProductGetItemQuality);
S32 gclMicroTrans_expr_MicroTransProductGetItemQuality(SA_PARAM_OP_VALID MicroTransactionUIProduct *pProduct)
{
	if (g_pMTList && pProduct)
	{
		MicroTransactionProduct *pMTProduct = eaIndexedGetUsingInt(&g_pMTList->ppProducts, pProduct->uID);
		MicroTransactionDef *pDef = pMTProduct ? GET_REF(pMTProduct->hDef) : NULL;
		if (pDef)
		{
			S32 i;
			for (i = 0; i < eaSize(&pDef->eaParts); i++)
			{
				if (pDef->eaParts[i]->ePartType == kMicroPart_Item)
				{
					ItemDef *pItemDef = GET_REF(pDef->eaParts[i]->hItemDef);

					if (pItemDef)
					{
						return (S32)pItemDef->Quality;
					}
					else
					{
						return (S32)kItemQuality_None;
					}
				}
			}
		}
	}
	return (S32)kItemQuality_None;
}

// Returns how many of an item def is granted from a microtransaction.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MicroTransProductGetItemCount);
S32 gclMicroTrans_expr_MicroTransProductGetItemCount(SA_PARAM_OP_VALID MicroTransactionUIProduct *pProduct, const char *pchItemDef)
{
	S32 iCount = 0;
	if (g_pMTList && pProduct)
	{
		MicroTransactionProduct *pMTProduct = eaIndexedGetUsingInt(&g_pMTList->ppProducts, pProduct->uID);
		MicroTransactionDef *pDef = pMTProduct ? GET_REF(pMTProduct->hDef) : NULL;
		if (pDef)
		{
			S32 i;
			pchItemDef = allocAddString(pchItemDef);
			for (i = 0; i < eaSize(&pDef->eaParts); i++)
			{
 				if (pDef->eaParts[i]->ePartType == kMicroPart_Item && REF_STRING_FROM_HANDLE(pDef->eaParts[i]->hItemDef) == pchItemDef)
					iCount += pDef->eaParts[i]->iCount;
			}
		}
	}
	return iCount;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MicroTransProductGetPartCount);
S32 gclMicroTrans_expr_MicroTransProductGetPartCount(SA_PARAM_OP_VALID MicroTransactionUIProduct *pProduct, int iPart)
{
	if (g_pMTList && pProduct)
	{
		MicroTransactionProduct *pMTProduct = eaIndexedGetUsingInt(&g_pMTList->ppProducts, pProduct->uID);
		MicroTransactionDef *pDef = pMTProduct ? GET_REF(pMTProduct->hDef) : NULL;
		if (pDef)
		{
			if (iPart < eaSize(&pDef->eaParts))
			{
				return pDef->eaParts[iPart]->iCount;
			}
		}
	}
	return 0;
}

//Returns the number of icon textures
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MicroTransProductGetImageCount);
S32 gclMicroTrans_expr_MicroTransProductGetImageCount(SA_PARAM_OP_VALID MicroTransactionUIProduct *pProduct)
{
	if (pProduct)
	{
		return eaSize(&pProduct->ppIcons);
	}
	return 0;
}

// Returns the "inventory" of a microtransaction product.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MicroTransactionProductGetInventory);
void gclMicroTrans_expr_MicroTransProductGetInventory(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID MicroTransactionUIProduct *pProduct)
{
	if (pProduct)
		ui_GenSetManagedListSafe(pGen, &pProduct->eaInventory, InventorySlot, false);
	else
		ui_GenSetManagedListSafe(pGen, NULL, InventorySlot, false);
}

AUTO_RUN;
void gclMicroTrans_InitVars(void)
{
	ui_GenInitStaticDefineVars(MTCategoryTypeEnum, "MTCategoryType");
	ui_GenInitStaticDefineVars(MicroPurchaseErrorTypeEnum, "MTPurchaseErrorType_");
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetCouponCount);
int ui_GenExprGetCouponCount()
{
	return eaSize(&s_eaCouponData);
}

// The coupons for the current micro-transaction item
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetCouponList);
void ui_GenExprGetCouponList(SA_PARAM_NN_VALID UIGen* pGen, U32 uProductID)
{
	static U32 s_uLastProductID = 0;
	static U32 s_uLastTM = 0;
	U32 tm = timeSecondsSince2000();
	Entity *pEntity = entActivePlayerPtr();
	if(pEntity)
	{
		if(s_CouponInfo.uProductID != uProductID || tm >= s_uLastTM)
		{
			S32 i;
			U64 oldCouponItem = s_CouponInfo.uCouponItem;

			eaClearStruct(&s_eaCouponData, parse_MicroUiCoupon);
			s_uLastTM = tm + 1;		// about every 
			if(s_CouponInfo.uProductID != uProductID)
			{
				s_CouponInfo.uCouponItem = 0;	// no coupon
				s_CouponInfo.uProductID = uProductID;
				s_CouponInfo.iIndex = 0;
			}

			{
				// Add an empty item to first part of list
				MicroUiCoupon *pCoupon = StructCreate(parse_MicroUiCoupon);
				pCoupon->uCouponItemID = 0;
				eaPush(&s_eaCouponData, pCoupon);
			}

			if(uProductID >0 )
			{
				// create the list
				MicroTrans_GetBestCouponItemID(pEntity, uProductID, &g_pMTList->ppProducts, &s_eaCouponData);
			}

			// make sure all items have the correct index, keep coupon the same and make sure its index is updated
			for(i = 0; i < eaSize(&s_eaCouponData); ++i)
			{
				s_eaCouponData[i]->iIndex = i;
				if(oldCouponItem > 0 && oldCouponItem == s_eaCouponData[i]->uCouponItemID)
				{
					// This coupon is still valid, set it for the new product
					s_CouponInfo.uCouponItem = s_eaCouponData[i]->uCouponItemID;
					s_CouponInfo.iIndex = i;
				}
			}
		}
	}

	ui_GenSetManagedListSafe(pGen, &s_eaCouponData, MicroUiCoupon, false);
}

// The coupons for the current micro-transaction item
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSetCouponIndex);
void ui_GenExprSetCouponIndex(S32 iIndex)
{
	if(iIndex >= 0 && iIndex < eaSize(&s_eaCouponData))
	{
		s_CouponInfo.uCouponItem = s_eaCouponData[iIndex]->uCouponItemID;
		s_CouponInfo.iIndex = iIndex;
	}
}

// The coupon nmae for this index
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetCouponName);
const char * ui_GenExprGetCouponName(S32 iIndex)
{
	Entity *pEntity = entActivePlayerPtr();
	if(pEntity && iIndex >= 0 && iIndex < eaSize(&s_eaCouponData) && s_eaCouponData[iIndex]->uCouponItemID != 0)
	{
		Item *pItem = inv_GetItemByID(pEntity, s_eaCouponData[iIndex]->uCouponItemID);
		if(pItem)
		{
			ItemDef *pItemDef = GET_REF(pItem->hItem);
			if(pItemDef)
			{
				if(s_eaCouponData[iIndex]->eaName)
				{
					return s_eaCouponData[iIndex]->eaName;
				}

				// create the name nn% Item name
				if(pItemDef->bCouponUsesItemLevel)
				{
					estrPrintf(&s_eaCouponData[iIndex]->eaName, "%d%% %s",
						MicroTrans_GetItemDiscount(pEntity, pItem) / MT_DISCOUNT_PER_PERCENT,
						item_GetDefLocalName(pItemDef, locGetLanguage(getCurrentLocale()))
					);
				}
				else
				{
					// assume coupon % is in name!
					estrPrintf(&s_eaCouponData[iIndex]->eaName, "%s",
						item_GetDefLocalName(pItemDef, locGetLanguage(getCurrentLocale()))
					);
				}

				return s_eaCouponData[iIndex]->eaName;
			}
		}
	}

	return langTranslateMessageKeyDefault(locGetLanguage(getCurrentLocale()), "item_No_Coupon", "No Coupon");
}

// The coupon index
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetCurrentCouponIndex);
S32 ui_GenExprGetCurrentCouponIndex(void)
{
	return s_CouponInfo.iIndex;
}

// The coupon item id if it is valid, productid is an override if > 0
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetCurrentCouponItemID);
U64 ui_GenExprGetCurrentCouponItemID(U32 uProductID)
{
	Entity *pEntity = entActivePlayerPtr();
	Item *pItem = inv_GetItemByID(pEntity, s_CouponInfo.uCouponItem);
	if(uProductID == 0)
	{
		uProductID = s_CouponInfo.uProductID;
	}

	if(pEntity && pItem && MicroTrans_ValidDiscountItem(pEntity, uProductID, pItem, &g_pMTList->ppProducts))
	{
		return s_CouponInfo.uCouponItem;
	}

	return 0;
}

#include "MicroTransactionUI_h_ast.c"
#include "MicroTransactionUI_c_ast.c"