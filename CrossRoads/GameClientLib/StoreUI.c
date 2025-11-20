/***************************************************************************
*     Copyright (c) 2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "contact_common.h"
#include "Entity.h"
#include "Character.h"
#include "estring.h"
#include "expression.h"
#include "GameAccountDataCommon.h"
#include "mission_common.h"
#include "storeCommon.h"
#include "powerStoreCommon.h"
#include "UIGen.h"
#include "Player.h"
#include "Powers.h"
#include "PowerTree.h"
#include "PowerTreeHelpers.h"
#include "StringCache.h"
#include "GameStringFormat.h"
#include "EntitySavedData.h"
#include "CharacterClass.h"
#include "gclEntity.h"
#include "gclMicroTransactions.h"
#include "gclUIGen.h"
#include "gclAccountProxy.h"
#include "contact_common.h"
#include "ItemCommon.h"
#include "itemCommon_h_ast.h"
#include "ItemEnums.h"
#include "itemEnums_h_ast.h"

#include "storeCommon_h_ast.h"
#include "StoreUI_c_ast.h"
#include "Entity_h_ast.h"
#include "Character_h_ast.h"
#include "powerStoreCommon_h_ast.h"
#include "PowerTree_h_ast.h"
#include "contact_common_h_ast.h"
#include "UIGen_h_ast.h"

#include "Autogen/GameServerLib_autogen_ServerCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

extern ParseTable parse_PowerDef[];
#define TYPE_parse_PowerDef PowerDef

AUTO_STRUCT;
typedef struct StoreTagInfo
{
	ItemTag eTag;
	const char *pchDisplayName;				AST(UNOWNED)
	U32 iCount;
} StoreTagInfo;

AUTO_STRUCT;
typedef struct UsageRestrictionCategoryInfo
{
	UsageRestrictionCategory eUsageCategory;
	const char *pchDisplayName;				AST(UNOWNED)
	S32 iCount;
} UsageRestrictionCategoryInfo;

static StoreItemInfo s_SelectedItem;

AUTO_RUN;
void StoreUI_Init(void)
{
	ui_GenInitStaticDefineVars(StoreCanBuyErrorEnum, "StoreCanBuyError_");
	ui_GenInitStaticDefineVars(PowerStoreTrainerTypeEnum, "PowerStoreTrainerType_");
}

static int SortStoreItemInfosByCategory(const StoreItemInfo **ppInfoA, const StoreItemInfo **ppInfoB, const void *pContext)
{
	const StoreItemInfo *pInfoA = *ppInfoA;
	const StoreItemInfo *pInfoB = *ppInfoB;

	if (!pInfoA && pInfoB)
	{
		return -1;
	}
	else if (!pInfoB)
	{
		return 1;
	}

	// Using stable sort will maintain the original order in the list
	return pInfoA->eStoreCategory - pInfoB->eStoreCategory;
}

static void GetBuyItemCategoryList(UsageRestrictionCategoryInfo ***peaCategoryInfo)
{
	Entity *pEnt = entActivePlayerPtr();
	ContactDialog *pDialog = SAFE_MEMBER3(pEnt, pPlayer, pInteractInfo, pContactDialog);
	S32 i, j, iCount = 0;

	if (pDialog && eaSize(&pDialog->eaStoreItems) > 0)
	{
		for (i = 0; i < eaSize(&pDialog->eaStoreItems); i++)
		{
			StoreItemInfo *pItemInfo = pDialog->eaStoreItems[i];
			ItemDef *pItemDef = pItemInfo->pOwnedItem ? GET_REF(pItemInfo->pOwnedItem->hItem) : NULL;
			UsageRestrictionCategory eUICategory;
			UsageRestrictionCategoryInfo *pInfo;

			if (!pItemDef || !pItemDef->pRestriction || pItemDef->pRestriction->eUICategory == UsageRestrictionCategory_None) {
				continue;
			}

			eUICategory = pItemDef->pRestriction->eUICategory;

			for (j = iCount - 1; j >= 0; j--) {
				if ((*peaCategoryInfo)[j]->eUsageCategory == eUICategory) {
					break;
				}
			}

			if (j < 0) {
				// initialize new struct
				Message *pMessage = StaticDefineGetMessage(UsageRestrictionCategoryEnum, eUICategory);
				pInfo = eaGetStruct(peaCategoryInfo, parse_UsageRestrictionCategoryInfo, iCount++);
				pInfo->eUsageCategory = eUICategory;
				pInfo->pchDisplayName = pMessage ? TranslateMessagePtr(pMessage) : StaticDefineIntRevLookupNonNull(UsageRestrictionCategoryEnum, eUICategory);
				pInfo->iCount = 1;
			} else {
				// increase count of existing struct
				pInfo = (*peaCategoryInfo)[j];
				pInfo->iCount++;
			}
		}
	}

	eaSetSizeStruct(peaCategoryInfo, parse_UsageRestrictionCategoryInfo, iCount);
}

static void GetBuyItemList(StoreItemInfo ***peaStoreItemInfo,
							bool bSortCategories,
							bool bInsertCategoryHeaders,
							StoreCategory eFilterCategory,
							UsageRestrictionCategory eItemUsageCategory,
							CharClassCategory eItemPetClassCategory,
							U32 bfItemPetClassCategories)
{
	static StoreItemInfo **s_eaHeaders = NULL;
	Entity *pEnt = entActivePlayerPtr();
	ContactDialog *pDialog = SAFE_MEMBER3(pEnt, pPlayer, pInteractInfo, pContactDialog);
	S32 iCount = 0, iHeaders = 0;
	S32 i, j;
	StoreCategory eLastCategory = kStoreCategory_None;

	eaClearFast(peaStoreItemInfo);
	if (!pEnt || !pDialog)
	{
		eaDestroyStruct(&s_eaHeaders, parse_StoreItemInfo);
		return;
	}

	eaCopy(peaStoreItemInfo, &pDialog->eaStoreItems);

	// Sort the items into their categories. By using stable sort, items in the same
	// category will stay in the same relative order as they are in the original list.
	if (bSortCategories) {
		eaStableSort(*peaStoreItemInfo, NULL, SortStoreItemInfosByCategory);
	}

	// Preprocessing step
	for (i = 0; i < eaSize(peaStoreItemInfo); i++) {
		StoreItemInfo *pItemInfo = (*peaStoreItemInfo)[i];
		ItemDef *pItemDef = pItemInfo->pOwnedItem ? GET_REF(pItemInfo->pOwnedItem->hItem) : NULL;
		PetDef *pPetDef = pItemDef ? GET_REF(pItemDef->hPetDef) : NULL;
		CharacterClass *pPetClass = pPetDef ? GET_REF(pPetDef->hClass) : NULL;
		bool bRemove = false;

		// Apply filters
		if (eFilterCategory != kStoreCategory_None && eFilterCategory != pItemInfo->eStoreCategory) {
			bRemove = true;
		}
		if (eItemUsageCategory != UsageRestrictionCategory_None && (!pItemDef || pItemDef->pRestriction && pItemDef->pRestriction->eUICategory != eItemUsageCategory)) {
			bRemove = true;
		}
		if (eItemPetClassCategory != CharClassCategory_None && (!pPetClass || pPetClass->eCategory != eItemPetClassCategory)) {
			bRemove = true;
		}
		if (bfItemPetClassCategories != 0 && (!pPetClass || ((1 << pPetClass->eCategory) & bfItemPetClassCategories) == 0)) {
			bRemove = true;
		}
		if (bRemove) {
			eaRemove(peaStoreItemInfo, i--);
			continue;
		}

		// All the UI references pItem, but the StoreItemInfo that comes from the
		// server only has pOwnedItem filled out
		pItemInfo->pItem = pItemInfo->pOwnedItem;

		// Handle microtransacted items
		if (g_pMTList) {
			if (IS_HANDLE_ACTIVE(pItemInfo->hMicroTransaction) && !pItemInfo->uMicroTransactionID) {
				// Find the micro transaction in the list of products
				for (j = 0; j < eaSize(&g_pMTList->ppProducts); j++) {
					MicroTransactionProduct *pProduct = g_pMTList->ppProducts[j];
					if (REF_COMPARE_HANDLES(pProduct->hDef, pItemInfo->hMicroTransaction) && gclMicroTrans_PrerequisitesMet(pProduct)) {
						pItemInfo->uMicroTransactionID = pProduct->uID;
						break;
					}
				}
			}
			if (IS_HANDLE_ACTIVE(pItemInfo->hRequiredMicroTransaction) && !pItemInfo->uRequiredMicroTransactionID) {
				// Find the micro transaction in the list of products
				for (j = 0; j < eaSize(&g_pMTList->ppProducts); j++) {
					MicroTransactionProduct *pProduct = g_pMTList->ppProducts[j];
					if (REF_COMPARE_HANDLES(pProduct->hDef, pItemInfo->hRequiredMicroTransaction) && gclMicroTrans_PrerequisitesMet(pProduct)) {
						pItemInfo->uRequiredMicroTransactionID = pProduct->uID;
						break;
					}
				}
			}
		}

		// Insert the header if necessary
		if (bInsertCategoryHeaders && eLastCategory != pItemInfo->eStoreCategory) {
			StoreItemInfo *pHeader = eaGetStruct(&s_eaHeaders, parse_StoreItemInfo, iHeaders++);
			eLastCategory = pItemInfo->eStoreCategory;
			pHeader->bIsHeader = true;
			pHeader->eStoreCategory = pItemInfo->eStoreCategory;
			pHeader->pchRequiredNumericName = pItemInfo->pchRequiredNumericName;
			eaInsert(peaStoreItemInfo, pHeader, i++);
			iCount++;
		}

		iCount++;
	}

	eaSetSize(peaStoreItemInfo, iCount);
	eaSetSizeStruct(&s_eaHeaders, parse_StoreItemInfo, iHeaders);
}

static int storeui_GetPlayerItemCount(Entity* pEnt, const char* pchItemDefName)
{
	static InvBagIDs* s_peExcludeBags = NULL;
	if (!s_peExcludeBags)
	{
		eaiPush(&s_peExcludeBags, InvBagIDs_Buyback);
	}
	return inv_ent_AllBagsCountItemsEx(pEnt, pchItemDefName, s_peExcludeBags);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowerStoreSetData");
bool exprPowerStoreSetData(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID PowerStorePowerInfo *pPowerInfo)
{
	if ( pPowerInfo )
	{
		PTNodeDef* pNodeDef =  GET_REF(pPowerInfo->hNode);
		
		if ( pNodeDef && eaSize(&pNodeDef->ppRanks) > 0 )
		{
			PowerDef* pPowerDef = GET_REF(pNodeDef->ppRanks[0]->hPowerDef);
			
			if ( pPowerDef )
			{
				ui_GenSetPointer(pGen, pPowerDef, parse_PowerDef);

				return true;
			}
		}
	}
	ui_GenSetPointer(pGen, NULL, parse_PowerDef);

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowerStoreGetInfoFromNode");
SA_RET_OP_VALID PowerStorePowerInfo* exprPowerStorePowerGetInfoFromNode(SA_PARAM_OP_VALID Entity* pEntity, const char* pchNodeName)
{
	S32 i;
	ContactDialog *pDialog = SAFE_MEMBER3(pEntity, pPlayer, pInteractInfo, pContactDialog);
	pchNodeName = allocFindString(pchNodeName);

	if (!pDialog || !pchNodeName)
		return false;

	for (i = 0; i < eaSize(&pDialog->eaStorePowers); i++)
	{
		PowerStorePowerInfo* pInfo = pDialog->eaStorePowers[i];
		PTNodeDef* pNodeInfoDef = GET_REF(pInfo->hNode);

		if (pNodeInfoDef && pNodeInfoDef->pchNameFull == pchNodeName)
		{
			return pInfo;
		}
	}

	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowerStoreGetPowerValueInCurrency");
S32 exprPowerStoreGetPowerValueInCurrency(	SA_PARAM_OP_VALID PowerStorePowerInfo* pInfo, 
											const char* pchCurrency)
{
	pchCurrency = allocFindString(pchCurrency);
	if (pInfo && pchCurrency) 
	{
		int i;
		for (i = 0; i < eaSize(&pInfo->eaCostInfo); i++)
		{
			ItemDef *pItemDef = GET_REF(pInfo->eaCostInfo[i]->hCurrency);
			if (pItemDef && pItemDef->pchName == pchCurrency) 
				return pInfo->eaCostInfo[i]->iCount;
		}
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowerStorePowerGetRequirementsText");
const char* exprPowerStorePowerGetRequirementsText(SA_PARAM_OP_VALID Entity* pEntity, const char* pchNodeName)
{
	PowerStorePowerInfo* pInfo = exprPowerStorePowerGetInfoFromNode(pEntity,pchNodeName);

	return pInfo ? pInfo->pcRequirementsText : "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowerStorePowerIsEnabled");
bool exprPowerStorePowerIsEnabled(SA_PARAM_OP_VALID Entity* pEntity, const char* pchNodeName, bool bCheckCost)
{
	S32 i;
	PowerStorePowerInfo* pInfo = exprPowerStorePowerGetInfoFromNode(pEntity,pchNodeName);

	if ( pInfo )
	{
		if ( pInfo->bFailsRequirements )
			return false;

		if ( bCheckCost )
		{
			for (i = 0; i < eaSize(&pInfo->eaCostInfo); i++)
			{
				if (pInfo->eaCostInfo[i]->bTooExpensive)
					return false;
			}

			return true;
		}
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowerStoreGetPowerValueFromNode");
S32 exprPowerStoreGetPowerValueFromNode(const char* pchNodeName, const char* pchCurrency)
{
	S32 i;
	Entity* pEnt = entActivePlayerPtr();
	ContactDialog* pDialog = SAFE_MEMBER3(pEnt, pPlayer, pInteractInfo, pContactDialog);
	PTNodeDef* pNodeDef = powertreenodedef_Find(pchNodeName);

	if ( pDialog && pNodeDef )
	{
		for ( i = eaSize( &pDialog->eaStorePowers )-1; i >= 0; i-- )
		{
			PowerStorePowerInfo* pInfo = pDialog->eaStorePowers[i];
			if ( pNodeDef == GET_REF(pInfo->hNode) )
			{
				return exprPowerStoreGetPowerValueInCurrency(pInfo, pchCurrency);
			}
		}
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowerStoreCreateStoreEntity");
SA_RET_OP_VALID Entity* exprPowerStoreCreateStoreEntity(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt)
{
	S32 i;
	ContactDialog *pDialog = SAFE_MEMBER3(pEnt, pPlayer, pInteractInfo, pContactDialog);
	
	static NOCONST(Entity)* pStoreEnt = NULL;

	if ( pDialog==NULL )
		return NULL;

	//create the fake entity
	pStoreEnt = StructCreateWithComment(parse_Entity, "Store entity returned by exprPowerStoreCreateStoreEntity");

	pStoreEnt->pChar = StructCreateNoConst(parse_Character);

	pStoreEnt->pChar->pEntParent = (Entity*)pStoreEnt;

	//create the power trees
	for ( i = 0; i < eaSize( &pDialog->eaStorePowers ); i++ )
	{
		PowerTreeDef* pTreeDef = GET_REF( pDialog->eaStorePowers[i]->hTree );

		// This seems crazy - The trees aren't checked for legality and they may not match the node if the
		//  data isn't set up correctly...
		entity_PowerTreeAddHelper(pStoreEnt,pTreeDef);
	}

	for ( i = 0; i < eaSize( &pDialog->eaStorePowers ); i++ )
	{
		PowerTreeDef* pTreeDef = GET_REF( pDialog->eaStorePowers[i]->hTree );
		PTNodeDef* pNodeDef = GET_REF( pDialog->eaStorePowers[i]->hNode );

		if ( pTreeDef==NULL || pNodeDef==NULL )
			continue;

		entity_PowerTreeNodeEscrowHelper( PARTITION_CLIENT, pStoreEnt, NULL, pTreeDef->pchName, pNodeDef->pchNameFull, NULL );
	}

	//set the gen pointer to this entity
	ui_GenSetPointer(pGen, pStoreEnt, parse_Entity);

	return (Entity*)pStoreEnt;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowerStoreDestroyStoreEntity");
void exprPowerStoreDestroyStoreEntity(SA_PARAM_NN_VALID UIGen *pGen)
{
	Entity* pEntity = (Entity*)ui_GenGetPointer( pGen, parse_Entity, NULL );

	StructDestroySafe( parse_Entity, &pEntity );
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowerStoreGetStoreEntity");
SA_RET_OP_VALID Entity* exprPowerStoreGetStoreEntity(SA_PARAM_NN_VALID UIGen *pGen)
{
	return (Entity*)ui_GenGetPointer( pGen, parse_Entity, NULL );
}



AUTO_EXPR_FUNC(UIGen) ACMD_NAME(PowerStoreTrainPet);
void exprPowerStoreBuyPowerForPet(SA_PARAM_OP_VALID Entity *pEnt, 
								  SA_PARAM_OP_VALID PowerStorePowerInfo *pPowerInfo, 
								  SA_PARAM_OP_VALID Entity* pPetEnt, const char* pchReplaceNode)
{
	if (!pEnt || !pPowerInfo || !pPetEnt || entGetType(pPetEnt) != GLOBALTYPE_ENTITYSAVEDPET)
		return;

	ServerCmd_powerstore_TrainPet(entGetContainerID(pPetEnt), pchReplaceNode, pPowerInfo->iIndex);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("StoreGetBuyItemList");
void exprStoreGetBuyItemList(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt)
{
	StoreItemInfo *** peaStoreItemInfo;
	if (ui_GenIsListManagingStructures(pGen)) {
		ui_GenSetList(pGen, NULL, NULL);
	}
	peaStoreItemInfo = ui_GenGetManagedListSafe(pGen, StoreItemInfo);
	GetBuyItemList(peaStoreItemInfo, false, false, kStoreCategory_None, UsageRestrictionCategory_None, CharClassCategory_None, 0);
	ui_GenSetManagedListSafe(pGen, peaStoreItemInfo, StoreItemInfo, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(StoreGetFilteredBuyItemList);
void exprStoreGetFilteredPetItemList(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt, S32 eItemUsageCategory, S32 eClassCategories)
{
	StoreItemInfo *** peaStoreItemInfo;
	if (ui_GenIsListManagingStructures(pGen)) {
		ui_GenSetList(pGen, NULL, NULL);
	}
	peaStoreItemInfo = ui_GenGetManagedListSafe(pGen, StoreItemInfo);
	GetBuyItemList(peaStoreItemInfo, false, false, kStoreCategory_None, eItemUsageCategory, CharClassCategory_None, eClassCategories);
	ui_GenSetManagedListSafe(pGen, peaStoreItemInfo, StoreItemInfo, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(StoreGetBuyItemUsageList);
void exprStoreGetBuyItemUsageList(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt)
{
	UsageRestrictionCategoryInfo ***peaInfo = ui_GenGetManagedListSafe(pGen, UsageRestrictionCategoryInfo);
	GetBuyItemCategoryList(peaInfo);
	ui_GenSetManagedListSafe(pGen, peaInfo, UsageRestrictionCategoryInfo, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("StoreSetSelectedItem");
void exprStoreSelectItem(SA_PARAM_OP_VALID StoreItemInfo *pInfo)
{
	if (pInfo)
	{
		Item *pTemp = pInfo->pItem;
		pInfo->pItem = NULL;
		StructCopyAll(parse_StoreItemInfo, pInfo, &s_SelectedItem);
		pInfo->pItem = pTemp;

		if (pInfo->pItem && !pInfo->pOwnedItem)
			s_SelectedItem.pOwnedItem = StructClone(parse_Item, pInfo->pItem);

		if (s_SelectedItem.pOwnedItem)
			s_SelectedItem.pItem = s_SelectedItem.pOwnedItem;
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("StoreGetSelectedItem");
SA_RET_NN_VALID StoreItemInfo *exprStoreGetSelectedItem(void)
{
	return &s_SelectedItem;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenStoreGetSelectedItem");
SA_RET_NN_VALID StoreItemInfo *exprGenStoreGetSelectedItem(SA_PARAM_NN_VALID UIGen *pGen)
{
	ui_GenSetPointer(pGen, &s_SelectedItem, parse_StoreItemInfo);
	return &s_SelectedItem;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("StoreGetBuyItemListSize");
int exprStoreGetBuyItemListSize(SA_PARAM_OP_VALID Entity *pEnt)
{
	ContactDialog *pDialog = SAFE_MEMBER3(pEnt, pPlayer, pInteractInfo, pContactDialog);

	if(pDialog) {
		return eaSize(&pDialog->eaStoreItems);
	} else {
		return 0;
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("StoreGetCategoryName");
const char * exprStoreGetCategoryName(int eStoreCategory)
{
	return StaticDefineGetTranslatedMessage(StoreCategoryEnum, eStoreCategory);
}

AUTO_STRUCT;
typedef struct StoreCategoryInfo 
{
	StoreCategory eCategory;
	const char *pchCategoryName;	AST(UNOWNED)
	STRING_POOLED requiredNumericName; AST(POOL_STRING)
} StoreCategoryInfo;

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("StoreGetCategories");
void exprStoreGetCategories(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt, bool includeShowAll)
{
	static S32 *s_StoreCategories = NULL;
	ContactDialog *pDialog = SAFE_MEMBER3(pEnt, pPlayer, pInteractInfo, pContactDialog);
	StoreCategoryInfo*** peaData = ui_GenGetManagedListSafe(pGen, StoreCategoryInfo);

	if ( pDialog )
	{
		int i;
		int n;
		int j;
	
		// collect the store categories for this store
		ea32ClearFast(&s_StoreCategories);
		n = eaSize(&pDialog->eaStoreItems);
		for ( i = 0; i < n; i++ )
		{
			ea32PushUnique(&s_StoreCategories, pDialog->eaStoreItems[i]->eStoreCategory);
		}

		// if requested and the list is longer than one, include "show all"
		if ( includeShowAll && ( ea32Size(&s_StoreCategories) > 1 ) )
		{
			ea32PushUnique(&s_StoreCategories, 0);
		}

		n = ea32Size(&s_StoreCategories);

		eaSetSizeStruct(peaData, parse_StoreCategoryInfo, n);

		for ( i = 0; i < n; i++ )
		{
			StoreCategoryInfo* pInfo = (*peaData)[i];
			pInfo->eCategory = s_StoreCategories[i];
			if ( ( s_StoreCategories[i] == 0 ) && includeShowAll )
			{
				pInfo->pchCategoryName = langTranslateMessageKey(locGetLanguage(getCurrentLocale()), "Staticdefine_Storecategory_ShowAll"); 
			}
			else
			{
				pInfo->pchCategoryName = exprStoreGetCategoryName(s_StoreCategories[i]);
				for ( j = 0; j < eaSize(&pDialog->eaStoreItems); j++ )
				{
					if ( pDialog->eaStoreItems[j]->eStoreCategory == s_StoreCategories[i] )
					{
						pInfo->requiredNumericName = pDialog->eaStoreItems[j]->pchRequiredNumericName;
						break;
					}
				}
			}
		}
	}
	else
	{
		eaClearStruct(peaData, parse_StoreCategoryInfo);
	}
	ui_GenSetManagedListSafe(pGen, peaData, StoreCategoryInfo, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("StoreGetBuyItemListWithCategories");
void exprStoreGetBuyItemListWithCategories(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt, U32 filterCategory)
{
	StoreItemInfo ***peaStoreItemInfo = NULL;
	if (ui_GenIsListManagingStructures(pGen)) {
		ui_GenSetList(pGen, NULL, NULL);
	}
	peaStoreItemInfo = ui_GenGetManagedListSafe(pGen, StoreItemInfo);
	GetBuyItemList(peaStoreItemInfo, true, true, filterCategory, UsageRestrictionCategory_None, CharClassCategory_None, 0);
	ui_GenSetManagedListSafe(pGen, peaStoreItemInfo, StoreItemInfo, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("StoreGetBuyItemListWithCategory");
void exprStoreGetBuyItemListWithCategory(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt, const char *pchFilterCategory)
{
	static StoreItemInfo **s_eaStoreItemInfo = NULL;
	S32 category = pchFilterCategory ? StaticDefineIntGetInt(StoreCategoryEnum,pchFilterCategory) : kStoreCategory_None;
	GetBuyItemList(&s_eaStoreItemInfo, true, false, category, UsageRestrictionCategory_None, CharClassCategory_None, 0);
	ui_GenSetListSafe(pGen, &s_eaStoreItemInfo, StoreItemInfo);
}

// Get a list of StoreSellableItemInfo from the player's contact dialog
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("StoreGetSellItemList");
void exprStoreGetSellItemList(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt)
{
	StoreItemInfo*** peaInfo = NULL;
	ContactDialog *pDialog = SAFE_MEMBER3(pEnt, pPlayer, pInteractInfo, pContactDialog);
	S32 iCount = 0;

	if (!ui_GenIsListManagingStructures(pGen)) {
		ui_GenSetList(pGen, NULL, NULL);
	}
	peaInfo = ui_GenGetManagedListSafe(pGen, StoreItemInfo);

	if (pDialog && pDialog->bSellEnabled)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		S32 i, iSize = eaSize(&pDialog->eaSellableItemInfo);
		for (i = 0; i < iSize; i++)
		{
			StoreItemInfo* pItemInfo;
			StoreItemCostInfo* pCostInfo;
			StoreSellableItemInfo* pSellableItemInfo = pDialog->eaSellableItemInfo[i];
			Item* pItem = inv_GetItemFromBag(pEnt, pSellableItemInfo->iBagID, pSellableItemInfo->iSlot, pExtract);
			
			if (!pItem || pItem->id != pSellableItemInfo->uItemID)
			{
				pItem = inv_GetItemByID(pEnt, pSellableItemInfo->uItemID);
			}
			if (!pItem)
			{
				continue;
			}

			// Get or create the item info, and fill it with the sell info
			pItemInfo = eaGetStruct(peaInfo, parse_StoreItemInfo, iCount++);
			pItemInfo->pItem = pItem;
			pItemInfo->iBagID = pSellableItemInfo->iBagID;
			pItemInfo->iSlot = pSellableItemInfo->iSlot;
			pItemInfo->iCount = pSellableItemInfo->iCount;
			
			// Get or create the cost info for this item info, and fill it with the sell info
			pCostInfo = eaGetStruct(&pItemInfo->eaCostInfo, parse_StoreItemCostInfo, 0);
			COPY_HANDLE(pCostInfo->hItemDef, pDialog->hStoreCurrency);
			pCostInfo->bTooExpensive = false;
			pCostInfo->iCount = pSellableItemInfo->iCost;
		}
	}

	eaSetSizeStruct(peaInfo, parse_StoreItemInfo, iCount);
	ui_GenSetManagedListSafe(pGen, peaInfo, StoreItemInfo, true);
}

// Get a list of all items in the player's bags
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("StoreGetBuybackItemList");
void exprStoreGetBuybackItemList(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt)
{
	StoreItemInfo*** peaInfo = NULL;
	InteractInfo *pInteractInfo = SAFE_MEMBER2(pEnt, pPlayer, pInteractInfo);
	ContactDialog *pDialog = SAFE_MEMBER3(pEnt, pPlayer, pInteractInfo, pContactDialog);
	S32 i, iCount = 0;

	if (!ui_GenIsListManagingStructures(pGen)) {
		ui_GenSetList(pGen, NULL, NULL);
	}
	peaInfo = ui_GenGetManagedListSafe(pGen, StoreItemInfo);

	if (pInteractInfo && eaSize(&pInteractInfo->eaBuyBackList) && pDialog && pDialog->bSellEnabled)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		S32 iListSize = eaSize(&pInteractInfo->eaBuyBackList);

		// Setup item pointers
		for(i = 0; i < iListSize; i++)	
		{
			StoreItemInfo* pItemInfo = pInteractInfo->eaBuyBackList[i];
			StoreItemInfo* pUIItemInfo = NULL;
			Item *pItem = NULL;

			if(!pItem && pItemInfo->pBuyBackInfo)
			{
				ItemBuyBack *pBuyBack = eaIndexedGetUsingInt(&pEnt->pPlayer->eaItemBuyBackList, pItemInfo->pBuyBackInfo->uBuyBackItemId);
				if(pBuyBack)
				{
					pItem = pBuyBack->pItem;
				}
			}
			if(!pItem) {
				continue;
			}
			pUIItemInfo = eaGetStruct(peaInfo, parse_StoreItemInfo, iCount++);
			pUIItemInfo->pItem = pItem;
			pUIItemInfo->iCount = pItemInfo->iCount;
			pUIItemInfo->iBagID = pItemInfo->iBagID;
			pUIItemInfo->iSlot = pItemInfo->iSlot;
			pUIItemInfo->storeItemPetID = pItemInfo->storeItemPetID;

			if(pItemInfo->eaCostInfo && pItemInfo->eaCostInfo[0])
			{
				if (!eaSize(&pUIItemInfo->eaCostInfo)){
					eaPush(&pUIItemInfo->eaCostInfo, StructCreate(parse_StoreItemCostInfo));
				}
				while(eaSize(&pUIItemInfo->eaCostInfo) > 1){
					StructDestroy(parse_StoreItemCostInfo, eaPop(&pUIItemInfo->eaCostInfo));
				}
				COPY_HANDLE(pUIItemInfo->eaCostInfo[0]->hItemDef, pItemInfo->eaCostInfo[0]->hItemDef);
				pUIItemInfo->eaCostInfo[0]->bTooExpensive = pItemInfo->eaCostInfo[0]->bTooExpensive;
				pUIItemInfo->eaCostInfo[0]->iCount = pItemInfo->eaCostInfo[0]->iCount;
			}

			if(!pUIItemInfo->pBuyBackInfo){
				pUIItemInfo->pBuyBackInfo = StructCreate(parse_BuyBackItemInfo);
			}
			StructCopyAll(parse_BuyBackItemInfo, pItemInfo->pBuyBackInfo, pUIItemInfo->pBuyBackInfo);

			// Here we check to see if the player can afford the item that they just sold back

			//  It's possible this is not a good place for this, since we aren't checking for the other possible errors,
			//	but presumably if the player sold an item to a vendor, they meet the requirements to have them in their inventory.
			//  If this is an incorrect assumption, this will need to be revisited ~DHOGBERG 1/20/2012
			if(pItemInfo && pUIItemInfo){
				int iPlayerMoney = 0;
				StoreItemCostInfo *pCostInfo = pItemInfo->eaCostInfo[0];
				ItemDef *pCurrencyDef = NULL;

				pCurrencyDef = pCostInfo?GET_REF(pCostInfo->hItemDef):NULL;
				if (pCurrencyDef){
					if (pCurrencyDef->eType == kItemType_Numeric) {
						iPlayerMoney = inv_GetNumericItemValue(pEnt, pCurrencyDef->pchName);
					} else {
						iPlayerMoney = storeui_GetPlayerItemCount(pEnt, pCurrencyDef->pchName);
					}
				}

				// TODO: Right now this calculated the cost for the entire stack of items
				//       If/when we make it so you can buy individual items from a group of identical items that a player sold to a vendor, this may need to change
				if(pItemInfo->pBuyBackInfo && pItemInfo->pBuyBackInfo->iCost * pItemInfo->pBuyBackInfo->iCount > iPlayerMoney)
				{
					pUIItemInfo->eCanBuyError = kStoreCanBuyError_CostRequirement;
				}
				else 
				{
					pUIItemInfo->eCanBuyError = pItemInfo->eCanBuyError;
				}
			}
			
		}
	}
	eaSetSizeStruct(peaInfo, parse_StoreItemInfo, iCount);
	ui_GenSetManagedListSafe(pGen, peaInfo, StoreItemInfo, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("StoreUsesOnlyResources");
bool exprStoreUsesOnlyResources(SA_PARAM_OP_VALID Entity *pEnt)
{
	ContactDialog *pDialog = SAFE_MEMBER3(pEnt, pPlayer, pInteractInfo, pContactDialog);
	S32 i, j;
	const char *pcResources = allocAddString("resources");
	
	if (!pDialog || !pDialog->eaStoreItems) {
		return true;
	}
	
	for (i = 0; i < eaSize(&pDialog->eaStoreItems); i++) {
		if (!pDialog->eaStoreItems[i]) {
			continue;
		}
		
		for (j = 0; j < eaSize(&pDialog->eaStoreItems[i]->eaCostInfo); j++) {
			ItemDef *pItemDef = GET_REF(pDialog->eaStoreItems[i]->eaCostInfo[j]->hItemDef);
			if (pItemDef && pItemDef->eType == kItemType_Numeric && pItemDef->pchName != pcResources) {
				return false;
			}
		}
	}
	
	return true;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("StoreGetCurrencyList");
void exprStoreGetCurrencyList(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt)
{
	static ItemDef **s_eaCurrencyList = NULL;
	ContactDialog *pDialog = SAFE_MEMBER3(pEnt, pPlayer, pInteractInfo, pContactDialog);
	S32 i, j, k;
	
	eaClear(&s_eaCurrencyList);
	
	if (pDialog && pDialog->eaStoreItems) {
		for (i = 0; i < eaSize(&pDialog->eaStoreItems); i++) {
			if (!pDialog->eaStoreItems[i]) {
				continue;
			}
			
			for (j = 0; j < eaSize(&pDialog->eaStoreItems[i]->eaCostInfo); j++) {
				ItemDef *pItemDef = GET_REF(pDialog->eaStoreItems[i]->eaCostInfo[j]->hItemDef);
				if (pItemDef) {
					bool bFound = false;
					for (k = 0; k < eaSize(&s_eaCurrencyList); k++) {
						if (pItemDef->pchName == s_eaCurrencyList[k]->pchName) {
							bFound = true;
							break;
						}
					}
					if (!bFound) {
						eaPush(&s_eaCurrencyList, pItemDef);
					}
				}
			}
		}
	}
	
	ui_GenSetList(pGen, &s_eaCurrencyList, parse_ItemDef);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("StoreUsesCurrency");
bool exprStoreUsesCurrency(SA_PARAM_OP_VALID Entity *pEnt, const char *pchCurrency)
{
	ContactDialog *pDialog = SAFE_MEMBER3(pEnt, pPlayer, pInteractInfo, pContactDialog);
	S32 i, j;

	pchCurrency = pchCurrency && *pchCurrency ? allocFindString(pchCurrency) : NULL;
	if (!pchCurrency) {
		return false;
	}

	if (pDialog && pDialog->eaStoreItems) {
		for (i = 0; i < eaSize(&pDialog->eaStoreItems); i++) {
			if (!pDialog->eaStoreItems[i]) {
				continue;
			}

			for (j = 0; j < eaSize(&pDialog->eaStoreItems[i]->eaCostInfo); j++) {
				ItemDef *pItemDef = GET_REF(pDialog->eaStoreItems[i]->eaCostInfo[j]->hItemDef);
				if (pItemDef && pItemDef->eType == kItemType_Numeric && pItemDef->pchName == pchCurrency) {
					return true;
				}
			}
		}
	}

	return false;
}

static int StoreUI_SortDiscounts(const StoreDiscountInfo **ppInfoA, const StoreDiscountInfo **ppInfoB)
{
	const StoreDiscountInfo* pInfoA = (*ppInfoA);
	const StoreDiscountInfo* pInfoB = (*ppInfoB);
	const char* pchDisplayNameA;
	const char* pchDisplayNameB;

	if (pInfoA->fDiscountPercent > pInfoB->fDiscountPercent)
		return -1;

	if (pInfoB->fDiscountPercent > pInfoA->fDiscountPercent)
		return 1;

	pchDisplayNameA = TranslateMessageRef(pInfoA->hDisplayName);
	pchDisplayNameB = TranslateMessageRef(pInfoB->hDisplayName);
	return stricmp(pchDisplayNameA, pchDisplayNameB);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("StoreGetDiscountListForItem");
void exprStoreGetDiscountListForItem(SA_PARAM_NN_VALID UIGen* pGen, SA_PARAM_OP_VALID StoreItemInfo* pItemInfo)
{
	StoreDiscountInfo*** peaData = ui_GenGetManagedListSafe(pGen, StoreDiscountInfo);
	Entity* pEnt = entActivePlayerPtr();
	ContactDialog* pDialog = SAFE_MEMBER3(pEnt, pPlayer, pInteractInfo, pContactDialog);
	int i, j, iCount = 0;
	static const char** s_ppchDiscounts = NULL;

	eaClearFast(&s_ppchDiscounts);

	if (pDialog && pItemInfo)
	{
		for (i = eaSize(&pItemInfo->eaCostInfo)-1; i >= 0; i--)
		{
			StoreItemCostInfo* pCostInfo = pItemInfo->eaCostInfo[i];
			for (j = eaSize(&pCostInfo->ppchDiscounts)-1; j >= 0; j--)
			{
				eaPushUnique(&s_ppchDiscounts, pCostInfo->ppchDiscounts[j]);
			}
		}
		for (i = eaSize(&s_ppchDiscounts)-1; i >= 0; i--)
		{
			StoreDiscountInfo* pDiscountInfo = eaIndexedGetUsingString(&pDialog->eaStoreDiscounts, s_ppchDiscounts[i]);
			if (pDiscountInfo)
			{
				StoreDiscountInfo* pData = eaGetStruct(peaData, parse_StoreDiscountInfo, iCount++);
				StructCopyAll(parse_StoreDiscountInfo, pDiscountInfo, pData);
			}
		}
	}

	eaSetSizeStruct(peaData, parse_StoreDiscountInfo, iCount);
	eaQSort(*peaData, StoreUI_SortDiscounts);
	ui_GenSetManagedListSafe(pGen, peaData, StoreDiscountInfo, true);
}

static bool storeui_RecipeMatchesFilter(ItemDef *pRecipeItemDef, ItemType eRecipeType, ItemType eItemType, SkillType eSkillType, InvBagIDs eItemBagID, SlotType eSlotType, int iGroup, ItemTag eTag)
{
	if (!pRecipeItemDef)
		return false;

	// Check for correct recipe type
	if ( pRecipeItemDef->eType != eRecipeType )
		return false;

	// Check for correct skill type on the recipe
	if ( (eSkillType!=kSkillType_None ) && (pRecipeItemDef->kSkillType!=eSkillType) )
		return false;

	if (eRecipeType == kItemType_ItemRecipe) 
	{
		ItemDef *pBaseItemDef = pRecipeItemDef ? GET_REF(pRecipeItemDef->pCraft->hItemResult) : NULL;

		if (!pBaseItemDef)
			return false;

		// check for the correct tag
		if (eTag != ItemTag_Any && eTag != pBaseItemDef->eTag)
			return false;

		// check for the correct item type on the result item
		if ((eItemType != kItemType_None) && (eItemType != pBaseItemDef->eType))
			return false;

		// check the BagID on the result item
		if ((eItemBagID != InvBagIDs_None) && (eaiFind(&pBaseItemDef->peRestrictBagIDs, eItemBagID) < 0))
			return false;

		// check for the correct upgrade type on the result item
		if ((eSlotType != kSlotType_Any) && (eSlotType != pBaseItemDef->eRestrictSlotType))
			return false;
	}
	else if (eRecipeType == kItemType_ItemPowerRecipe) 
	{
		// Check the BagID on the ItemPower
		if ((eItemBagID!=InvBagIDs_None) &&
				!(eaiSize(&pRecipeItemDef->peRestrictBagIDs)==0 ||
				  eaiFind(&pRecipeItemDef->peRestrictBagIDs, eItemBagID) >= 0))
			return false;

		// This item power recipe isn't associated with this group
		if (iGroup>=0 && (pRecipeItemDef->Group & (1 << iGroup)) == 0) {
			return false;
		}
	}
	else
	{
		//invalid recipe type
		return false;
	}
	return true;
}

static bool storeui_RecipeAlreadyKnown(Entity *pEnt, ItemDef *pItemDef, GameAccountDataExtract *pExtract)
{
	if (pItemDef)
		return (inv_ent_CountItems(pEnt, InvBagIDs_Recipe, pItemDef->pchName, pExtract) > 0);
	return false;
}

static int storeui_GetItemSkillLevel(const StoreItemInfo *pItemInfo)
{
	if (pItemInfo && pItemInfo->pItem){
		ItemDef *pDef = GET_REF(pItemInfo->pItem->hItem);
		if (pDef && pDef->pRestriction){
			return pDef->pRestriction->iSkillLevel;
		}
	}
	return 0;
}

static int storeui_RecipeCmp(const StoreItemInfo **ppItemInfoA, const StoreItemInfo **ppItemInfoB)
{
	if (ppItemInfoA && *ppItemInfoA && ppItemInfoB && *ppItemInfoB)
		return storeui_GetItemSkillLevel(*ppItemInfoA) - storeui_GetItemSkillLevel(*ppItemInfoB);
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("StoreGetFilteredRecipeList");
void exprStoreGetFilteredRecipeList(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt, int eRecipeType, int eItemType, int eSkillType, int eItemBagID, int eItemUpgradeType, int eTag, bool bKnown, bool bAvailable, bool bUnavailable)
{
	ContactDialog *pDialog = SAFE_MEMBER3(pEnt, pPlayer, pInteractInfo, pContactDialog);
	static StoreItemInfo **s_eaUnownedItemInfo = NULL;
	int i;
	
	eaClear(&s_eaUnownedItemInfo);

	if (pDialog)
	{
		StoreItemInfo **peaKnownItems = NULL;
		StoreItemInfo **peaUnavailableItems = NULL;
		StoreItemInfo **peaAvailableItems = NULL;
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);

		for (i = 0; i < eaSize(&pDialog->eaStoreItems); i++)
		{
			ItemDef *pItemDef = pDialog->eaStoreItems[i]->pOwnedItem?GET_REF(pDialog->eaStoreItems[i]->pOwnedItem->hItem):NULL;
			if (pItemDef)
			{
				// all of the UI references pItem, but the StoreItemInfo that comes from the
				// server only has pOwnedItem filled out
				pDialog->eaStoreItems[i]->pItem = pDialog->eaStoreItems[i]->pOwnedItem;

				// filter out non-matching recipes
				if (!storeui_RecipeMatchesFilter(pItemDef, eRecipeType, eItemType, eSkillType, eItemBagID, eItemUpgradeType, -1, eTag))
					continue;

				// known recipes
				if (storeui_RecipeAlreadyKnown(pEnt, pItemDef, pExtract))
				{
					if (bKnown)
						eaPush(&peaKnownItems, pDialog->eaStoreItems[i]);
				}
				// unavailable recipes; "unavailable" means buying the item is disabled, but you don't own it already
				else if (pDialog->eaStoreItems[i]->eCanBuyError != kStoreCanBuyError_None)
				{
					if (bUnavailable)
						eaPush(&peaUnavailableItems, pDialog->eaStoreItems[i]);
				}
				// available recipes
				else
				{
					if (bAvailable)
						eaPush(&peaAvailableItems, pDialog->eaStoreItems[i]);
				}
			}
		}

		eaQSort(peaKnownItems, storeui_RecipeCmp);
		eaQSort(peaUnavailableItems, storeui_RecipeCmp);
		eaQSort(peaAvailableItems, storeui_RecipeCmp);
		eaPushEArray(&s_eaUnownedItemInfo, &peaAvailableItems);
		eaPushEArray(&s_eaUnownedItemInfo, &peaUnavailableItems);
		eaPushEArray(&s_eaUnownedItemInfo, &peaKnownItems);
		eaDestroy(&peaAvailableItems);
		eaDestroy(&peaUnavailableItems);
		eaDestroy(&peaKnownItems);

		ui_GenSetList(pGen, &s_eaUnownedItemInfo, parse_StoreItemInfo);
	}
	else
		ui_GenSetList(pGen, NULL, NULL);
}

static int storeui_StoreTagInfoCmp(const StoreTagInfo **ppTagInfo1, const StoreTagInfo **ppTagInfo2)
{
	return (*ppTagInfo1)->iCount - (*ppTagInfo2)->iCount;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("RecipeStoreGetTagsFiltered");
void exprRecipeStoreGetTagsFiltered(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_STR const char *pcDummyTagMsgKey, int bShowUnspecialized)
{
	ContactDialog *pDialog = SAFE_MEMBER3(pEnt, pPlayer, pInteractInfo, pContactDialog);
	StoreTagInfo **peaStoreTagList = NULL;
	StoreTagInfo *pNewTag;
	int i, j;

	if (pDialog)
	{
		// compile all of the tags on all recipes' item results in the store
		for (i = 0; i < eaSize(&pDialog->eaStoreItems); i++)
		{
			Item *pRecipeItem = pDialog->eaStoreItems[i]->pItem ? pDialog->eaStoreItems[i]->pItem : pDialog->eaStoreItems[i]->pOwnedItem;
			ItemDef *pRecipeItemDef = pRecipeItem ? GET_REF(pRecipeItem->hItem) : NULL;
			ItemDef *pResultItemDef = pRecipeItemDef && pRecipeItemDef->eType == kItemType_ItemRecipe && pRecipeItemDef->pCraft ? GET_REF(pRecipeItemDef->pCraft->hItemResult) : NULL;
			ItemTag eTag = pResultItemDef ? pResultItemDef->eTag : ItemTag_None;
			bool bFoundTag = false;

			// ignore item power recipes or recipes that don't make anything
			if (!pResultItemDef)
				continue;

			// Check to see if this tag is one that the player specializes in,
			// doesn't need to be specialized in, or has disabled the specialization filter.
			if(entity_CraftingCheckTag(pEnt, eTag) || bShowUnspecialized)
			{
				// if the item's tag is already in the list, increment the count
				for (j = 0; j < eaSize(&peaStoreTagList) && !bFoundTag; j++)
				{
					if (peaStoreTagList[j]->eTag == eTag)
					{
						peaStoreTagList[j]->iCount++;
						bFoundTag = true;
						break;
					}
				}

				// if this is a new tag, add it to the list
				if (!bFoundTag)
				{
					pNewTag = StructCreate(parse_StoreTagInfo);
					pNewTag->iCount = 1;
					pNewTag->eTag = eTag;
					pNewTag->pchDisplayName = StaticDefineGetTranslatedMessage(ItemTagEnum, eTag);
					eaPush(&peaStoreTagList, pNewTag);
				}
			}
		}
	}

	// sort the list from greatest to least in terms of recipe count
	eaQSort(peaStoreTagList, storeui_StoreTagInfoCmp);

	// add any requested dummy tags
	if (pcDummyTagMsgKey)
	{
		const char *pcTransMsg = langTranslateMessageKey(locGetLanguage(getCurrentLocale()), pcDummyTagMsgKey);
		if (pcTransMsg && pcTransMsg[0])
		{
			pNewTag = StructCreate(parse_StoreTagInfo);
			pNewTag->iCount = 1;
			pNewTag->eTag = -1;
			pNewTag->pchDisplayName = pcTransMsg;
			eaPush(&peaStoreTagList, pNewTag);
		}
	}

	// set the list onto the gen
	ui_GenSetManagedListSafe(pGen, &peaStoreTagList, StoreTagInfo, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("RecipeStoreGetTags");
void exprRecipeStoreGetTags(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_STR const char *pcDummyTagMsgKey)
{
	exprRecipeStoreGetTagsFiltered(pGen, pEnt, pcDummyTagMsgKey, 0);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("StoreHasItemRecipes");
bool exprStoreHasItemRecipes(SA_PARAM_OP_VALID Entity *pEnt)
{
	ContactDialog *pDialog = SAFE_MEMBER3(pEnt, pPlayer, pInteractInfo, pContactDialog);
	int i;
	
	if(pDialog) {
		for (i = 0; i < eaSize(&pDialog->eaStoreItems); i++){
			ItemDef *pItemDef = pDialog->eaStoreItems[i]->pOwnedItem?GET_REF(pDialog->eaStoreItems[i]->pOwnedItem->hItem):NULL;
			if (pItemDef && pItemDef->eType == kItemType_ItemRecipe){
				return true;
			}
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("StoreHasItemPowerRecipes");
bool exprStoreHasItemPowerRecipes(SA_PARAM_OP_VALID Entity *pEnt)
{
	ContactDialog *pDialog = SAFE_MEMBER3(pEnt, pPlayer, pInteractInfo, pContactDialog);
	int i;
	
	if(pDialog) {
		for (i = 0; i < eaSize(&pDialog->eaStoreItems); i++){
			ItemDef *pItemDef = pDialog->eaStoreItems[i]->pOwnedItem?GET_REF(pDialog->eaStoreItems[i]->pOwnedItem->hItem):NULL;
			if (pItemDef && pItemDef->eType == kItemType_ItemPowerRecipe){
				return true;
			}
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("StoreGetItemValueInCurrency");
S32 exprStoreGetItemValueInCurrency(SA_PARAM_OP_VALID StoreItemInfo *pItemInfo, const char *pcCurrency)
{
	if (pItemInfo) {
		int i;
		
		for (i = 0; i < eaSize(&pItemInfo->eaCostInfo); i++){
			ItemDef *pItemDef = GET_REF(pItemInfo->eaCostInfo[i]->hItemDef);
			
			if (pItemDef && !strcmp(pItemDef->pchName, pcCurrency)) {
				return pItemInfo->eaCostInfo[i]->iCount;
			}
		}
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("StoreGetItemValueTable");
char *exprStoreGetItemValueTable(SA_PARAM_OP_VALID StoreItemInfo *pItemInfo, const char *pchFormat, const char *pchEndFormat, const char *pchTooExpensiveFormat, const char *pchTooExpensiveEndFormat, const char *pchExclude)
{
	static char *estrResult = NULL;
	static char *estrRows = NULL;
	estrClear(&estrResult);
	estrClear(&estrRows);
	
	if (pItemInfo) {
		int i;
		for (i = 0; i < eaSize(&pItemInfo->eaCostInfo); i++) {
			ItemDef *pItemDef = GET_REF(pItemInfo->eaCostInfo[i]->hItemDef);
			
			if (pItemDef && strcmp(pItemDef->pchName, pchExclude)) {
				if (pItemInfo->eaCostInfo[i]->bTooExpensive) {
					FormatGameMessageKey(&estrRows, "Store_ValueTable_Row_TooExpensive",
						STRFMT_STRING("ItemName", TranslateDisplayMessage(pItemDef->displayNameMsg)),
						STRFMT_INT("Amount", pItemInfo->eaCostInfo[i]->iCount),
						STRFMT_INT("OriginalAmount", pItemInfo->eaCostInfo[i]->iOriginalCount),
						STRFMT_INT("AvailableAmount", pItemInfo->eaCostInfo[i]->iAvailableCount),
						STRFMT_END);
				} else {
					FormatGameMessageKey(&estrRows, "Store_ValueTable_Row",
						STRFMT_STRING("ItemName", TranslateDisplayMessage(pItemDef->displayNameMsg)),
						STRFMT_INT("Amount", pItemInfo->eaCostInfo[i]->iCount),
						STRFMT_INT("OriginalAmount", pItemInfo->eaCostInfo[i]->iOriginalCount),
						STRFMT_INT("AvailableAmount", pItemInfo->eaCostInfo[i]->iAvailableCount),
						STRFMT_END);
				}
			}
		}
		
		FormatGameMessageKey(&estrResult, "Store_ValueTable", STRFMT_STRING("Rows", estrRows), STRFMT_END);
	}
	return estrResult;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("StoreGetItemValueTableWithIcons");
char *exprStoreGetItemValueTableWithIcons(SA_PARAM_OP_VALID StoreItemInfo *pItemInfo, const char *pchFormat, const char *pchEndFormat, const char *pchTooExpensiveFormat, const char *pchTooExpensiveEndFormat, const char *pchExclude)
{
	static char *estrResult = NULL;
	static char *estrRows = NULL;
	estrClear(&estrResult);
	estrClear(&estrRows);

	if (pItemInfo) {
		int i;
		for (i = 0; i < eaSize(&pItemInfo->eaCostInfo); i++) {
			ItemDef *pItemDef = GET_REF(pItemInfo->eaCostInfo[i]->hItemDef);

			if (pItemDef && strcmp(pItemDef->pchName, pchExclude)) {
				const char* pchIconName = item_GetIconName(NULL, pItemDef);
				
				if (pItemDef->eType == kItemType_Numeric) {
					pchIconName = gclGetBestIconName(pchIconName, "default_item_icon");
				}
				if (pItemInfo->eaCostInfo[i]->bTooExpensive) {
					FormatGameMessageKey(&estrRows, "Store_ValueTable_Row_TooExpensive_WithIcons",
						STRFMT_STRING("ItemTexture", pchIconName),
						STRFMT_STRING("ItemName", TranslateDisplayMessage(pItemDef->displayNameMsg)),
						STRFMT_INT("Amount", pItemInfo->eaCostInfo[i]->iCount),
						STRFMT_INT("OriginalAmount", pItemInfo->eaCostInfo[i]->iOriginalCount),
						STRFMT_INT("AvailableAmount", pItemInfo->eaCostInfo[i]->iAvailableCount),
						STRFMT_END);
				} else {
					FormatGameMessageKey(&estrRows, "Store_ValueTable_Row_WithIcons",
						STRFMT_STRING("ItemTexture", pchIconName),
						STRFMT_STRING("ItemName", TranslateDisplayMessage(pItemDef->displayNameMsg)),
						STRFMT_INT("Amount", pItemInfo->eaCostInfo[i]->iCount),
						STRFMT_INT("OriginalAmount", pItemInfo->eaCostInfo[i]->iOriginalCount),
						STRFMT_INT("AvailableAmount", pItemInfo->eaCostInfo[i]->iAvailableCount),
						STRFMT_END);
				}
			}
		}

		FormatGameMessageKey(&estrResult, "Store_ValueTable_WithIcons", STRFMT_STRING("Rows", estrRows), STRFMT_END);
	}
	return estrResult;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("StoreGetItemValueTableList");
void exprStoreGetItemValueTableList(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID StoreItemInfo *pItemInfo)
{
	if (pItemInfo)
		ui_GenSetListSafe(pGen, &pItemInfo->eaCostInfo, StoreItemCostInfo);
	else
		ui_GenSetListSafe(pGen, NULL, StoreItemCostInfo);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("StoreGetItemProvisioningList");
void exprStoreGetItemProvisioningList(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID StoreItemInfo *pItemInfo)
{
	ContactDialogStoreProvisioning ***peaProvisioning = ui_GenGetManagedListSafe(pGen, ContactDialogStoreProvisioning);
	Entity *pPlayer = entActivePlayerPtr();
	ContactDialog *pDialog = SAFE_MEMBER3(pPlayer, pPlayer, pInteractInfo, pContactDialog);
	S32 i;

	eaClearFast(peaProvisioning);

	if (pItemInfo && pDialog)
	{
		for (i = eaSize(&pDialog->eaProvisioning) - 1; i >= 0; i--)
		{
			if (eaFind(&pDialog->eaProvisioning[i]->eapchStores, pItemInfo->pchStoreName) >= 0)
				eaPush(peaProvisioning, pDialog->eaProvisioning[i]);
		}
	}

	ui_GenSetManagedListSafe(pGen, peaProvisioning, ContactDialogStoreProvisioning, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("StoreItemIsEnabled");
bool exprStoreItemIsEnabled(SA_PARAM_OP_VALID StoreItemInfo *pItemInfo)
{
	int i;
	if (!pItemInfo || pItemInfo->eCanBuyError != kStoreCanBuyError_None)
		return false;

	for (i = 0; i < eaSize(&pItemInfo->eaCostInfo); i++){
		if (pItemInfo->eaCostInfo[i]->bTooExpensive){
			return false;
		}
	}	
	return true;
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME(StoreSellItem);
void exprStoreSellItem(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID StoreItemInfo *pStoreItemInfo, S32 iCount)
{
	if (!pEnt || !pStoreItemInfo || iCount < 1) {
		return;
	}
	
	ServerCmd_store_SellItem(pStoreItemInfo->iBagID, pStoreItemInfo->iSlot, iCount, pStoreItemInfo->storeItemPetID?GLOBALTYPE_ENTITYSAVEDPET:GLOBALTYPE_ENTITYPLAYER, pStoreItemInfo->storeItemPetID?pStoreItemInfo->storeItemPetID:entGetContainerID(pEnt));
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(StoreBuybackItem);
void exprStoreBuybackItem(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID StoreItemInfo *pStoreItemInfo, S32 iCount)
{
	if (!pEnt || !pStoreItemInfo || iCount < 1) {
		return;
	}

	ServerCmd_store_BuybackItem(pStoreItemInfo->storeItemPetID?GLOBALTYPE_ENTITYSAVEDPET:GLOBALTYPE_ENTITYPLAYER, pStoreItemInfo->storeItemPetID?pStoreItemInfo->storeItemPetID:entGetContainerID(pEnt), pStoreItemInfo);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(StoreSellItemBySlot);
void exprStoreSellItemBySlot(S32 iBag, S32 iSlot, S32 iCount)
{
	ServerCmd_store_SellItem(iBag, iSlot, iCount, GLOBALTYPE_ENTITYPLAYER, 0);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(StoreSellItemBySlotToContact);
void exprStoreSellItemBySlotToContact(S32 iBag, S32 iSlot, S32 iCount, const char* pchContactDef)
{
	ServerCmd_store_SellItemNoDialog(iBag, iSlot, iCount, GLOBALTYPE_ENTITYPLAYER, 0, pchContactDef);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(StoreSellItemBySlotAndEntIDToContact);
void exprStoreSellItemBySlotAndEntIDToContact(S32 iBag, S32 iSlot, S32 iContainerID, S32 iType, S32 iCount, const char* pchContactDef)
{
	ServerCmd_store_SellItemNoDialog(iBag, iSlot, iCount, iType, iContainerID, pchContactDef);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(StoreBuyItem);
void exprStoreBuyItem(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID StoreItemInfo *pStoreItemInfo, S32 iCount)
{
	if (!pEnt || !pStoreItemInfo || !pStoreItemInfo->pchStoreName ||iCount < 1) {
		return;
	}
	
	ServerCmd_store_BuyItem(pStoreItemInfo->pchStoreName, pStoreItemInfo->index, iCount);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(StoreBuyAndRemoveItem);
void exprStoreBuyAndRemoveItem(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID Entity* pTargetEnt, SA_PARAM_OP_VALID StoreItemInfo *pStoreItemInfo, S32 iCount)
{
	if (!pEnt || !pTargetEnt || !pStoreItemInfo || !pStoreItemInfo->pchStoreName ||iCount < 1) {
		return;
	}

	ServerCmd_store_BuyAndRemoveItem(entGetType(pTargetEnt), entGetContainerID(pTargetEnt), pStoreItemInfo->pchStoreName, pStoreItemInfo->index, iCount);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(InjuryStoreSetTarget);
void exprInjuryStoreSetTarget(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID Entity* pTargetEnt)
{
	if (!pEnt || !pTargetEnt) {
		return;
	}

	ServerCmd_injuryStore_SetTarget(entGetType(pTargetEnt), entGetContainerID(pTargetEnt));
}

// if iMaxCount < 0, then it indicates that there is no desired max
static S32 gclStoreGetMaxBuyCount(SA_PARAM_NN_VALID Entity *pEnt, SA_PARAM_NN_VALID StoreItemInfo *pStoreItemInfo, S32 iMaxCount)
{
	if (iMaxCount != 0)
	{
		int i;

		//Limit max count to what the player can afford
		for (i = 0; i < eaSize(&pStoreItemInfo->eaCostInfo); i++)
		{
			StoreItemCostInfo *pCostInfo = pStoreItemInfo->eaCostInfo[i];
			ItemDef *pCurrencyDef = pCostInfo?GET_REF(pCostInfo->hItemDef):NULL;
			int iHave = 0;

			if (pCurrencyDef){
				if (pCurrencyDef->eType == kItemType_Numeric) {
					iHave = inv_GetNumericItemValue(pEnt, pCurrencyDef->pchName);
				} else {
					iHave = inv_ent_AllBagsCountItems(pEnt, pCurrencyDef->pchName);
				}
			}

			if (pCostInfo){
				if (pCostInfo->iCount > 0 && (iMaxCount < 0 || (pCostInfo->iCount * iMaxCount) > iHave))
				{
					iMaxCount = iHave / pCostInfo->iCount;
					if (iMaxCount <= 1) break;
				}
			}
		}
	}
	return MAX(iMaxCount, 0);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(StoreMaxBuyCount);
S32 exprStoreMaxBuyCount(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID StoreItemInfo *pStoreItemInfo)
{
	if (!pEnt || !pStoreItemInfo) {
		return 0;
	}
	return gclStoreGetMaxBuyCount(pEnt, pStoreItemInfo, -1);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(StoreMaxBulkBuyCount);
S32 exprStoreMaxBulkBuyCount(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID StoreItemInfo *pStoreItemInfo)
{
	S32 iMaxCount;

	if (!pEnt || !pStoreItemInfo) {
		return 0;
	}

	iMaxCount = pStoreItemInfo->iMayBuyInBulk;
	if (iMaxCount < 0) iMaxCount = 1;
	
	return gclStoreGetMaxBuyCount(pEnt, pStoreItemInfo, iMaxCount);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("StoreGetSellEnabled");
bool exprStoreGetSellEnabled(SA_PARAM_OP_VALID Entity *pEnt)
{
	ContactDialog *pDialog = SAFE_MEMBER3(pEnt, pPlayer, pInteractInfo, pContactDialog);
	if (pDialog)
	{
		return pDialog->bSellEnabled;
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(StoreDisplayCPoints);
bool exprStoreDisplayCPoints(SA_PARAM_OP_VALID Entity *pEnt)
{
	ContactDialog *pDialog = SAFE_MEMBER3(pEnt, pPlayer, pInteractInfo, pContactDialog);
	if (pDialog)
	{
		return pDialog->bDisplayStoreCPoints;
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenIsItemSellable);
bool exprIsItemSellable(SA_PARAM_OP_VALID ItemDef* pItemDef)
{
	if (pItemDef && 
		!((pItemDef->flags & kItemDefFlag_CantSell) ||
		pItemDef->eType == kItemType_Mission ||
		pItemDef->eType == kItemType_MissionGrant))
	{
		return true;
	}

	return false;
}

static U32 s_uResearchTimeExpireMs = 0;

AUTO_COMMAND ACMD_PRIVATE ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0);
void gclStore_SetResearchTime(U32 uResearchTime)
{
	s_uResearchTimeExpireMs = g_ui_State.totalTimeInMs + (uResearchTime * 1000);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(StoreGetResearchTimeRemaining);
F32 exprStoreGetResearchTimeRemaining(void)
{
	Entity* pEnt = entActivePlayerPtr();
	ContactDialog *pDialog = SAFE_MEMBER3(pEnt, pPlayer, pInteractInfo, pContactDialog);
	if (pDialog && pDialog->bIsResearching)
	{
		U32 uCurrentTimeMs = g_ui_State.totalTimeInMs;
		if (s_uResearchTimeExpireMs >= uCurrentTimeMs)
		{
			return (s_uResearchTimeExpireMs - uCurrentTimeMs) / 1000.0f;
		}
		else if (s_uResearchTimeExpireMs)
		{
			s_uResearchTimeExpireMs = 0;
			ServerCmd_store_FinishResearch();
		}
	}
	else
	{
		s_uResearchTimeExpireMs = 0;
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(StoreCancelResearch) ACMD_PRODUCTS(StarTrek);
void exprStoreCancelResearch(void)
{
	Entity* pEnt = entActivePlayerPtr();
	ContactDialog *pDialog = SAFE_MEMBER3(pEnt, pPlayer, pInteractInfo, pContactDialog);
	if (pDialog && pDialog->bIsResearching)
	{
		s_uResearchTimeExpireMs = 0;
		ServerCmd_store_CancelResearch();
	}
}

static int sortStoreHighlightCategory(const UIGenVarTypeGlob **ppLeft, const UIGenVarTypeGlob **ppRight)
{
	return (*ppLeft)->iInt - (*ppRight)->iInt;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("StoreItemGetHighlightCategories");
void exprStoreItemGetHighlightCategories(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID StoreItemInfo *pInfo, U32 uOptions)
{
	UIGenVarTypeGlob ***peaGlob = ui_GenGetManagedListSafe(pGen, UIGenVarTypeGlob);

	if (pInfo)
	{
		S32 i;
		eaSetSizeStruct(peaGlob, parse_UIGenVarTypeGlob, eaiSize(&pInfo->peHighlightCategory));
		for (i = 0; i < eaiSize(&pInfo->peHighlightCategory); i++)
		{
			StoreHighlightCategory eCategory = pInfo->peHighlightCategory[i];
			UIGenVarTypeGlob *pGlob = eaGet(peaGlob, i);
			Message *pMessage = StaticDefineGetMessage(StoreHighlightCategoryEnum, eCategory);
			pGlob->iInt = eCategory;
			pGlob->fFloat = eCategory;
			if (pMessage)
				estrPrintf(&pGlob->pchString, "%s", TranslateMessagePtr(pMessage));
			else
				estrClear(&pGlob->pchString);
		}
		// Default sort by enum value
		eaQSort(*peaGlob, sortStoreHighlightCategory);
	}
	else
	{
		eaClearStruct(peaGlob, parse_UIGenVarTypeGlob);
	}

	ui_GenSetManagedListSafe(pGen, peaGlob, UIGenVarTypeGlob, true);
}

#include "StoreUI_c_ast.c"