/***************************************************************************
*     Copyright (c) 2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "contact_common.h"
#include "Entity.h"
#include "ExpressionPrivate.h"
#include "ResourceManager.h"
#include "storeCommon.h"
#include "StringCache.h"
#include "GameBranch.h"
#include "GroupProjectCommon.h"
#include "ActivityCommon.h"
#include "file.h"
#include "ItemCommon.h"
#include "itemCommon_h_ast.h"
#include "ItemEnums.h"
#include "itemEnums_h_ast.h"

#ifdef GAMESERVER
#include "objTransactions.h"
#endif

#include "AutoGen/storeCommon_h_ast.h"
#include "AutoGen/GroupProjectCommon_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

DictionaryHandle g_StoreDictionary = NULL;
extern ExprContext *g_pItemContext;

//Statics for use in the can buy expression
static const char *s_pcVarPlayer = "Player";
static int s_hVarPlayer = 0;

static const char *s_pcVarItem = "Item";
static int s_hVarItem = 0;

static ExprContext* s_pStoreExprContext = NULL;

// Context to track all data-defined Store Categories
DefineContext *g_pDefineStoreCategories = NULL;
DefineContext *g_pDefineStoreHighlightCategories = NULL;

static ExprContext *s_ExpreContextStoreShowItem;

const char *store_GetItemNameContextPtr(void)
{
	return s_pcVarItem;
}

ExprContext *store_GetShowItemContext(void)
{
	return(s_ExpreContextStoreShowItem);
}

ExprFuncTable * store_ShowItemCreateExprFuncTable(void)
{
	static ExprFuncTable * stFuncs = NULL;

	if (!stFuncs) {
		stFuncs = exprContextCreateFunctionTable();
		exprContextAddFuncsToTableByTag(stFuncs, "util");
        exprContextAddFuncsToTableByTag(stFuncs, "gameutil");
		exprContextAddFuncsToTableByTag(stFuncs, "player");
		exprContextAddFuncsToTableByTag(stFuncs, "gameutil");
	}

	return stFuncs;
}

void store_InitContextShowItemIfNull(void)
{
	if(!s_ExpreContextStoreShowItem)
	{
		s_ExpreContextStoreShowItem = exprContextCreate();
		exprContextSetFuncTable(s_ExpreContextStoreShowItem, store_ShowItemCreateExprFuncTable());
		exprContextSetAllowRuntimeSelfPtr(s_ExpreContextStoreShowItem);
		exprContextSetAllowRuntimePartition(s_ExpreContextStoreShowItem);
	}
}

AUTO_RUN;
void store_InitContext(void)
{
	s_pStoreExprContext = exprContextCreate();
	exprContextSetFuncTable(s_pStoreExprContext, store_CreateExprFuncTable());
	exprContextSetAllowRuntimeSelfPtr(s_pStoreExprContext);
	exprContextSetAllowRuntimePartition(s_pStoreExprContext);

	// init the store show item context	
	store_InitContextShowItemIfNull();
}

ExprFuncTable* store_CreateExprFuncTable(void)
{
	static ExprFuncTable* stFuncs = NULL;
	
	if (!stFuncs) {
		stFuncs = exprContextCreateFunctionTable();
		exprContextAddFuncsToTableByTag(stFuncs, "util");
		exprContextAddFuncsToTableByTag(stFuncs, "gameutil");
		exprContextAddFuncsToTableByTag(stFuncs, "player");
	}
	
	return stFuncs;
}

static S32 store_Generate(StoreDef *pStore)
{
	S32 bGood = true;
	S32 i;

	store_BuyContextGenerateSetup();
	store_ShowItemContextGenerateSetup();
	
	for(i=eaSize(&pStore->inventory)-1; i>=0; --i)
	{
		StoreItemDef *pItem = pStore->inventory[i];
		
		if (pItem->pExprCanBuy)
		{
			bGood = exprGenerate(pItem->pExprCanBuy, s_pStoreExprContext) && bGood;
		}
		if (pItem->pExprShowItem)
		{
			bGood = exprGenerate(pItem->pExprShowItem, s_ExpreContextStoreShowItem) && bGood;
		}
	}

	// generate the econ points expression
	if (pStore->pExprEPConversion) {
		if (!exprGenerate(pStore->pExprEPConversion, g_pItemContext)) {
			bGood = false;
		}
	}

	// generate the requires expression for the store
	if (pStore->pExprRequires)
	{
		if (!exprGenerate(pStore->pExprRequires, g_pItemContext)) {
			bGood = false;
		}
	}
	
	for (i = eaSize(&pStore->eaDiscountDefs)-1; i >= 0; i--) {
		StoreDiscountDef* pDiscountDef = pStore->eaDiscountDefs[i];
		if (pDiscountDef->pExprRequires) {
			if (!exprGenerate(pDiscountDef->pExprRequires, g_pItemContext)) {
				bGood = false;
			}
		}
	}

	return(bGood);
}

void store_BuyContextGenerateSetup(void)
{
	exprContextSetPointerVarPooledCached(s_pStoreExprContext, s_pcVarPlayer, NULL, parse_Entity, true, true, &s_hVarPlayer);
	exprContextSetPointerVarPooledCached(s_pStoreExprContext, s_pcVarItem, NULL, parse_ItemDef, true, true, &s_hVarItem);
}

void store_BuyContextSetup(Entity *pPlayerEnt, ItemDef *pItemDef)
{
	exprContextSetSelfPtr(s_pStoreExprContext, pPlayerEnt);
	exprContextSetPartition(s_pStoreExprContext, entGetPartitionIdx(pPlayerEnt));
	exprContextSetPointerVarPooledCached(s_pStoreExprContext, s_pcVarPlayer, pPlayerEnt, parse_Entity, true, true, &s_hVarPlayer);
	exprContextSetPointerVarPooledCached(s_pStoreExprContext, s_pcVarItem, pItemDef, parse_ItemDef, true, true, &s_hVarItem);
}

ExprContext *store_GetBuyContext(void)
{
	return(s_pStoreExprContext);
}

void store_ShowItemContextGenerateSetup(void)
{
	exprContextSetPointerVar(store_GetShowItemContext(), "Player", NULL, parse_Entity, true, false);
}

void store_ShowItemContextSetup(Entity *pEntity)
{
	exprContextSetSelfPtr(store_GetShowItemContext(), pEntity);
	exprContextSetPartition(store_GetShowItemContext(), entGetPartitionIdx(pEntity));
	exprContextSetPointerVar(store_GetShowItemContext(), "Player", pEntity, parse_Entity, true, false);
}

// ----------------------------------------------------------------------------------
// Store Validation
// ----------------------------------------------------------------------------------

bool store_ValidateResearchStore(StoreDef *pStore)
{
	bool bValid = true;
	int i;
	for (i = eaSize(&pStore->inventory)-1; i >= 0; i--)
	{
		StoreItemDef* pItemDef = pStore->inventory[i];
		if (pItemDef->iCount > 1)
		{
			ErrorFilenamef(pStore->filename, "Store %s is flagged as being a research store, but store item %s allows buying in bulk.", 
				pStore->name, REF_STRING_FROM_HANDLE(pItemDef->hItem));
			bValid = false;
		}
	}
	return bValid;
}

bool store_ValidatePuppetStore(StoreDef *pStore)
{
	bool bValid = true;
	int i;
	for (i = eaSize(&pStore->inventory)-1; i >= 0; i--)
	{
		StoreItemDef* pStoreItemDef = pStore->inventory[i];
		ItemDef* pItemDef = GET_REF(pStoreItemDef->hItem);
		PetDef* pPetDef = pItemDef ? GET_REF(pItemDef->hPetDef) : NULL;
		if (!pPetDef || !pPetDef->bCanBePuppet)
		{
			ErrorFilenamef(pStore->filename, "Store '%s' is flagged as a 'PuppetStore', but '%s' is not a puppet item.", 
				pStore->name, REF_STRING_FROM_HANDLE(pStoreItemDef->hItem));
			bValid = false;
		}
	}
	return bValid;
}

bool store_Validate(StoreDef *pDef)
{
	const char *pchTempFileName;
	char *pchPath = NULL;
	ItemDef *pCurrencyDef;
	int i, j;
	bool bResult = true;
	
	if( !resIsValidName(pDef->name) )
	{
		ErrorFilenamef( pDef->filename, "Store name is illegal: '%s'", pDef->name );
		return 0;
	}

	if( !resIsValidScope(pDef->scope) )
	{
		ErrorFilenamef( pDef->filename, "Store scope is illegal: '%s'", pDef->scope );
		return 0;
	}

	pchTempFileName = pDef->filename;
	if (resFixPooledFilename(&pchTempFileName, GameBranch_GetDirectory(&pchPath, STORES_BASE_DIR), pDef->scope, pDef->name, STORES_EXTENSION)) {
		if (IsServer()) {
			ErrorFilenamef( pDef->filename, "Store filename does not match name '%s' scope '%s'", pDef->name, pDef->scope);
			bResult = false;
		}
	}

	if (!GET_REF(pDef->hCurrency) && REF_STRING_FROM_HANDLE(pDef->hCurrency)) {
		ErrorFilenamef(pDef->filename, "Store references non-existent item '%s'", REF_STRING_FROM_HANDLE(pDef->hCurrency));
		bResult = false;
	}

	for(i=eaSize(&pDef->inventory)-1; i>=0; --i)
	{
		StoreItemDef *pItem = pDef->inventory[i];
		ItemDef *pItemDef = GET_REF(pItem->hItem);
		ItemDef *pRequiredNumericDef = NULL;

		if (!GET_REF(pItem->hItem) && REF_STRING_FROM_HANDLE(pItem->hItem))
		{
			ErrorFilenamef(pDef->filename, "Store references non-existent item '%s'", REF_STRING_FROM_HANDLE(pItem->hItem));
			bResult = false;
		}
		else if (!GET_REF(pItem->hItem))
		{
			ErrorFilenamef(pDef->filename, "Store has an inventory row that is empty");
			bResult = false;
		}

		pRequiredNumericDef = GET_REF(pItem->hRequiredNumeric);
		if ( ( pRequiredNumericDef == NULL ) && ( REF_STRING_FROM_HANDLE(pItem->hRequiredNumeric) != NULL ) )
		{
			ErrorFilenamef( pDef->filename, "Store item RequiredNumeric refers to non-existent numeric '%s'", REF_STRING_FROM_HANDLE(pItem->hRequiredNumeric));
			bResult = false;
		}

		if ( ( pRequiredNumericDef != NULL ) && ( pRequiredNumericDef->eType != kItemType_Numeric ) )
		{
			ErrorFilenamef( pDef->filename, "Store item RequiredNumeric refers to a non-numeric item '%s'", pRequiredNumericDef->pchName);
			bResult = false;
		}

		// validate any ValueRecipe override items
		for ( j = 0; j < eaSize(&pItem->ppOverrideValueRecipes); j++ )
		{
			if (!SAFE_GET_REF(pItem->ppOverrideValueRecipes[j], hDef) && REF_STRING_FROM_HANDLE(pItem->ppOverrideValueRecipes[j]->hDef))
			{
				ErrorFilenamef( pDef->filename, "Store item refers to non-existent value recipe '%s'", REF_STRING_FROM_HANDLE(pItem->ppOverrideValueRecipes[j]->hDef));
				bResult = false;
			}
		}
		
		// If this is recipe store, make sure all the items are recipes
		if (pItemDef && pDef->eContents == Store_Recipes && pItemDef->eType != kItemType_ItemRecipe && pItemDef->eType != kItemType_ItemPowerRecipe)
		{
			ErrorFilenamef(pDef->filename, "Store is a recipe store, but has a non-recipe item '%s'", pItemDef->pchName);
			bResult = false;
		}
		// If this is a costume store, make sure all the items have costume unlocks
		if (pItemDef && pDef->eContents == Store_Costumes && (pItemDef->eCostumeMode != kCostumeDisplayMode_Unlock || eaSize(&pItemDef->ppCostumes) == 0))
		{
			ErrorFilenamef(pDef->filename, "Store is a costume store but has item '%s' that has no costume unlock", pItemDef->pchName);
			bResult = false;
		}

		if (!GET_REF(pItem->cantBuyMessage.hMessage) && REF_STRING_FROM_HANDLE(pItem->cantBuyMessage.hMessage)) {
			ErrorFilenamef(pDef->filename, "Store references non-existent message '%s'", REF_STRING_FROM_HANDLE(pItem->cantBuyMessage.hMessage));
			bResult = false;
		}

		// Activity name
		if (pItem->pchActivityName!=NULL && pItem->pchActivityName[0]!=0)
		{
			// Can't use activity def on persisted stuff
			if (pDef->bIsPersisted)
			{
				ErrorFilenamef(pDef->filename, "Store is persisted and item '%s' references an activity '%s'. This is not supported.", pItemDef->pchName,pItem->pchActivityName);
				bResult = false;
			}
			
			// Validate for activity name
			if (ActivityDef_Find(pItem->pchActivityName)==NULL)
			{
				ErrorFilenamef(pDef->filename, "Store item '%s' references an activity, '%s', that doesn't exist.", pItemDef->pchName,pItem->pchActivityName);
				bResult = false;
			}
		}
	}

	// If this store has a specific content type, it can't also have "Sell Enabled"
	if (pDef->eContents != Store_All && pDef->eContents != Store_Sellable_Items && pDef->bSellEnabled){
		ErrorFilenamef( pDef->filename, "Store has restricted contents and has 'Sell Enabled'.  Selling to a restricted-content store is not allowed.");
		bResult = false;
	}

	// If this store has a content type of "Sellable Items" it must have "Sell Enabled"
	if (pDef->eContents == Store_Sellable_Items && !pDef->bSellEnabled)
	{
		ErrorFilenamef( pDef->filename, "Store's contents are Sellable Items, yet selling is not enabled for this store.  Sell Enabled must be set to true.");
		bResult = false;
	}
	
	// Make sure the store has a valid currency
	pCurrencyDef = GET_REF(pDef->hCurrency);
	if (!pCurrencyDef) {
		ErrorFilenamef( pDef->filename, "Store uses invalid currency '%s'.", REF_STRING_FROM_HANDLE(pDef->hCurrency));
		bResult = false;
	} else if (pCurrencyDef->eType != kItemType_Numeric) {
		ErrorFilenamef( pDef->filename, "Store currency needs to be a numeric. Currency '%s' is of type '%s'.",
			REF_STRING_FROM_HANDLE(pDef->hCurrency), StaticDefineIntRevLookup(ItemTypeEnum, pCurrencyDef->eType));
		bResult = false;
	} else {
		if (pCurrencyDef->MinNumericValue < 0) {
			ErrorFilenamef( pDef->filename, "Store currency '%s' has a MinNumericValue below 0. This allows people to spend into the negative.",
				REF_STRING_FROM_HANDLE(pDef->hCurrency));
			bResult = false;
		}
		if (!(pCurrencyDef->flags & kItemDefFlag_TransFailonLowLimit)) {
			ErrorFilenamef( pDef->filename, "Store currency '%s' does not have TransFailOnLowLimit set. This allows people to buy items when they don't have enough currency.",
				REF_STRING_FROM_HANDLE(pDef->hCurrency));
			bResult = false;
		}
	}

	// Special validation for persisted stores
	if (pDef->bIsPersisted)
	{
		for (i = eaSize(&pDef->eaRestockDefs)-1; i >= 0; i--)
		{
			StoreRestockDef* pRestockDef = pDef->eaRestockDefs[i];

			if (!pRestockDef->pchName || !pRestockDef->pchName[0])
			{
				ErrorFilenamef(pDef->filename, 
					"Persisted Store '%s' RestockDef using RewardTable '%s' does not have a name", 
					pDef->name, REF_STRING_FROM_HANDLE(pRestockDef->hRewardTable));
				bResult = false;
				continue;
			}
			else if (!GET_REF(pRestockDef->hRewardTable) && REF_STRING_FROM_HANDLE(pRestockDef->hRewardTable))
			{
				ErrorFilenamef(pDef->filename, 
					"Store '%s' RestockDef '%s' references non-existent reward table '%s'", 
					pDef->name, pRestockDef->pchName, REF_STRING_FROM_HANDLE(pRestockDef->hRewardTable));
				bResult = false;
			}
			else if (pRestockDef->uMaxItemCount && pRestockDef->uMinItemCount > pRestockDef->uMaxItemCount)
			{
				ErrorFilenamef(pDef->filename, 
					"Persisted Store '%s' CategoryDef '%s' has invalid min/max item counts [min %d][max %d]", 
					pDef->name, pRestockDef->pchName, 
					pRestockDef->uMinItemCount, pRestockDef->uMaxItemCount);
				bResult = false;
			}
			else if (pRestockDef->uReplenishTimeMax &&
					 pRestockDef->uReplenishTimeMin > pRestockDef->uReplenishTimeMax)
			{
				ErrorFilenamef(pDef->filename, 
					"Persisted Store '%s' RestockDef '%s' has invalid min/max item replenish times [min %d][max %d]", 
					pDef->name, pRestockDef->pchName, 
					pRestockDef->uReplenishTimeMin, pRestockDef->uReplenishTimeMax);
				bResult = false;
			}
			else if (pRestockDef->uReplenishTimeMin == 0 && pRestockDef->uReplenishTimeMax > 0)
			{
				ErrorFilenamef(pDef->filename, 
					"Persisted Store '%s' RestockDef '%s' has invalid min/max item replenish times [min %d][max %d]. Min item replenish time must be at greater than 0 since the max item replenish time is greater than 0.", 
					pDef->name, pRestockDef->pchName, 
					pRestockDef->uReplenishTimeMin, pRestockDef->uReplenishTimeMax);
				bResult = false;
			}
			else if (pRestockDef->uExpireTimeMax && pRestockDef->uExpireTimeMin > pRestockDef->uExpireTimeMax)
			{
				ErrorFilenamef(pDef->filename, 
					"Persisted Store '%s' RestockDef '%s' has invalid expire times [min %d][max %d]", 
					pDef->name, pRestockDef->pchName, pRestockDef->uExpireTimeMin, pRestockDef->uExpireTimeMax);
				bResult = false;
			}
			else if (pRestockDef->uExpireTimeMin == 0 && pRestockDef->uExpireTimeMax > 0)
			{
				ErrorFilenamef(pDef->filename, 
					"Persisted Store '%s' RestockDef '%s' has invalid expire times [min %d][max %d]. Min item expire time must be greater than 0 since the max item expire time is greater than 0.", 
					pDef->name, pRestockDef->pchName, pRestockDef->uExpireTimeMin, pRestockDef->uExpireTimeMax);
				bResult = false;
			}
			for (j = i-1; j >= 0; j--)
			{
				StoreRestockDef* pCheckDef = pDef->eaRestockDefs[j];
				if (stricmp(pCheckDef->pchName, pRestockDef->pchName)==0)
				{
					ErrorFilenamef(pDef->filename, 
						"Persisted Store '%s' RestockDef '%s' does not have a unique category name", 
						pDef->name, pRestockDef->pchName);
					bResult = false;
				}
			}
		}
	}

	// Validate StoreDiscountDefs
	for (i = eaSize(&pDef->eaDiscountDefs)-1; i >= 0; i--)
	{
		StoreDiscountDef* pDiscountDef = pDef->eaDiscountDefs[i];

		if (!pDiscountDef->pchName || !pDiscountDef->pchName[0])
		{
			ErrorFilenamef(pDef->filename, "Store discount %s has an invalid name.", pDiscountDef->pchName);
			bResult = false;
		}
		if (IS_HANDLE_ACTIVE(pDiscountDef->msgDisplayName.hMessage) && !GET_REF(pDiscountDef->msgDisplayName.hMessage))
		{
			ErrorFilenamef(pDef->filename, "Store discount %s references non-existent display name %s.",
				pDiscountDef->pchName, REF_STRING_FROM_HANDLE(pDiscountDef->msgDisplayName.hMessage));
			bResult = false;
		}
		if (IS_HANDLE_ACTIVE(pDiscountDef->msgDescription.hMessage) && !GET_REF(pDiscountDef->msgDescription.hMessage))
		{
			ErrorFilenamef(pDef->filename, "Store discount %s references non-existent description %s.",
				pDiscountDef->pchName, REF_STRING_FROM_HANDLE(pDiscountDef->msgDescription.hMessage));
			bResult = false;
		}
		if (!IS_HANDLE_ACTIVE(pDiscountDef->hDiscountCostItem))
		{
			ErrorFilenamef(pDef->filename, "Store discount %s does not specify a discount cost item.", pDiscountDef->pchName);
			bResult = false;		
		}
		else if (!GET_REF(pDiscountDef->hDiscountCostItem))
		{
			ErrorFilenamef(pDef->filename, "Store discount %s references non-existent cost item %s.",
				pDiscountDef->pchName, REF_STRING_FROM_HANDLE(pDiscountDef->hDiscountCostItem));
			bResult = false;
		}
		if (pDiscountDef->fDiscountPercent < 0.01f || pDiscountDef->fDiscountPercent > 100.0f)
		{
			ErrorFilenamef(pDef->filename, "Store discount %s has an invalid discount percentage. Valid range is (0-100].", pDiscountDef->pchName);
			bResult = false;
		}
		for (j = i-1; j >= 0; j--)
		{
			StoreDiscountDef* pCheckDiscountDef = pDef->eaDiscountDefs[j];
			if (pCheckDiscountDef->pchName == pDiscountDef->pchName)
			{
				ErrorFilenamef(pDef->filename, "Store discount %s shares a name with another discount on the same store.", pDiscountDef->pchName);
				bResult = false;
			}
		}
	}

	// Validation for research stores
	if (pDef->bIsResearchStore)
	{
		if (!store_ValidateResearchStore(pDef))
		{
			 bResult = false;
		}
	}

	estrDestroy(&pchPath);
	
	return bResult;
}

// ----------------------------------------------------------------------------------
// Store Dictionary
// ----------------------------------------------------------------------------------

// This resource validation callback just does filename fixup
// TODO: add validation to stores
static int storeResValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, StoreDef *pStore, U32 userID)
{
	switch(eType)
	{
		xcase RESVALIDATE_POST_TEXT_READING:	
			store_Generate(pStore);
			store_Validate(pStore);
			return VALIDATE_HANDLED;
		xcase RESVALIDATE_FIX_FILENAME:
		{
			char *pchPath = NULL;
			resFixPooledFilename(&pStore->filename, GameBranch_GetDirectory(&pchPath,STORES_BASE_DIR), pStore->scope, pStore->name, STORES_EXTENSION);
			estrDestroy(&pchPath);
		}
		return VALIDATE_HANDLED;
	}
	return VALIDATE_NOT_HANDLED;
}

AUTO_RUN;
int RegisterStoreDictionary(void)
{
	s_pcVarPlayer = allocAddStaticString(s_pcVarPlayer);
	s_pcVarItem = allocAddStaticString(s_pcVarItem);

	g_StoreDictionary = RefSystem_RegisterSelfDefiningDictionary("Store", false, parse_StoreDef, true, true, NULL);

	if (IsGameServerSpecificallly_NotRelatedTypes())
	{
		resDictManageValidation(g_StoreDictionary, storeResValidateCB);
	}
	if (IsServer())
	{
		resDictProvideMissingResources(g_StoreDictionary);
		if (isDevelopmentMode() || isProductionEditMode()) {
			resDictMaintainInfoIndex(g_StoreDictionary, ".Name", ".Scope", NULL, NULL, NULL);
		}
	} 
	else if (IsClient())
	{
		resDictRequestMissingResources(g_StoreDictionary, 8, false, resClientRequestSendReferentCommand);
	}
	//resDictProvideMissingRequiresEditMode(g_StoreDictionary);
	return 1;
}

AUTO_RUN_LATE;
int PersistedStore_RegisterContainer(void)
{
#ifdef GAMESERVER
	RefSystem_RegisterSelfDefiningDictionary(GlobalTypeToCopyDictionaryName(GLOBALTYPE_PERSISTEDSTORE), false, parse_PersistedStore, false, false, NULL);
	resDictRequestMissingResources(GlobalTypeToCopyDictionaryName(GLOBALTYPE_PERSISTEDSTORE), RES_DICT_KEEP_NONE, false, objCopyDictHandleRequest);
#endif
	objRegisterNativeSchema(GLOBALTYPE_PERSISTEDSTORE, parse_PersistedStore, NULL, NULL, NULL, NULL, NULL);
	return 1;
}

void StoreCategories_Load(void)
{
	g_pDefineStoreCategories = DefineCreate();
	DefineLoadFromFile(g_pDefineStoreCategories, "StoreCategory", "StoreCategories", NULL,  "defs/config/StoreCategories.def", "StoreCategories.bin", kStoreCategory_FIRST_DATA_DEFINED);
}

void StoreHighlightCategories_Load(void)
{
	g_pDefineStoreHighlightCategories = DefineCreate();
	DefineLoadFromFile(g_pDefineStoreHighlightCategories, "StoreHighlightCategory", "StoreHighlightCategories", NULL,  "defs/config/StoreHighlightCategories.def", "StoreHighlightCategories.bin", kStoreHighlightCategory_FIRST_DATA_DEFINED);
}

// Load data-defined Store Categories
AUTO_STARTUP(StoreCategories) ASTRT_DEPS(AS_Messages);
void StoreCategories_Load_Startup(void)
{
	const char *pchMessageFail = NULL;

	StoreCategories_Load();
	StoreHighlightCategories_Load();

	if (pchMessageFail = StaticDefineVerifyMessages(StoreCategoryEnum))
		Errorf("Not all StoreCategory messages were found: %s", pchMessageFail);	
	if (pchMessageFail = StaticDefineVerifyMessages(StoreHighlightCategoryEnum))
		Errorf("Not all StoreHighlightCategory messages were found: %s", pchMessageFail);	
}

void store_Load(void)
{
	char *pchPath = NULL;
	char *pchBinFile = NULL;
	
	resLoadResourcesFromDisk(g_StoreDictionary, 
		GameBranch_GetDirectory(&pchPath, STORES_BASE_DIR), ".store",
		GameBranch_GetFilename(&pchBinFile, STORES_BIN_FILE), 
		PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY);
	
	estrDestroy(&pchPath);
	estrDestroy(&pchBinFile);
}

AUTO_STARTUP(Stores) ASTRT_DEPS(Items, RewardTables, StoreCategories);
void store_LoadDefs(void)
{
	store_Load();
}

StoreDef* store_DefFromName(const char* storeName)
{
	if (storeName)
		return (StoreDef*)RefSystem_ReferentFromString(g_StoreDictionary, storeName);
	return NULL;
}

StoreRestockDef* PersistedStore_FindRestockDefByName(StoreDef* pDef, const char* pchName)
{
	const char* pchPooledName = allocFindString(pchName);
	if (pDef && pDef->bIsPersisted && pchPooledName)
	{
		S32 i;
		for (i = eaSize(&pDef->eaRestockDefs)-1; i >= 0; i--)
		{
			if (pDef->eaRestockDefs[i]->pchName == pchPooledName)
			{
				return pDef->eaRestockDefs[i];
			}
		}
	}
	return NULL;
}

S32 PersistedStore_FindItemByID(PersistedStore* pPersistStore, U32 uID)
{
	if (pPersistStore)
	{
		S32 i;
		for (i = eaSize(&pPersistStore->eaInventory)-1; i >= 0; i--) 
		{
			PersistedStoreItem* pStoreItem = pPersistStore->eaInventory[i];
			if (pStoreItem->uID == uID) 
			{
				return i;
			}
		}
	}
	return -1;
}

StoreDiscountDef* store_FindDiscountDefByName(StoreDef* pDef, const char* pchName)
{
	const char* pchNamePooled = allocFindString(pchName);
	if (pchNamePooled)
	{
		int i;
		for (i = eaSize(&pDef->eaDiscountDefs)-1; i >= 0; i--)
		{
			StoreDiscountDef* pDiscountDef = pDef->eaDiscountDefs[i];
			if (pDiscountDef->pchName == pchNamePooled)
			{
				return pDiscountDef;
			}
		}
	}
	return NULL;
}

#include "AutoGen/storeCommon_h_ast.c"
