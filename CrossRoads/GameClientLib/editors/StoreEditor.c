//
// StoreEditor.c
//

#ifndef NO_EDITORS

#include "MultiEditTable.h"
#include "MultiEditWindow.h"
#include "ResourceSearch.h"
#include "storeCommon.h"
#include "StringCache.h"
#include "EString.h"
#include "Expression.h"
#include "GameBranch.h"
#include "rewardCommon.h"
#include "GroupProjectCommon.h"

#include "AutoGen/storeCommon_h_ast.h"
#include "AutoGen/GroupProjectCommon_h_ast.h"

// This magic line is required if you run in Full Debug to avoid assert
AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

#define SE_GROUP_MAIN			"Main"
#define SE_SUBGROUP_INVENTORY	"Inventory"
#define SE_SUBGROUP_RESTOCK		"RestockDef"
#define SE_SUBGROUP_DISCOUNT	"DiscountDef"

//-----------------------------------------------------------------------------------
// Global Data
//-----------------------------------------------------------------------------------

static MEWindow *seWindow = NULL;
static int seInventoryId = 0;
static int seRestockId = 0;
static int seDiscountId = 0;

extern ExprContext *g_pItemContext;

//---------------------------------------------------------------------------------------------------
// Callbacks
//---------------------------------------------------------------------------------------------------


static int se_validateCallback(METable *pTable, StoreDef *pStoreDef, void *pUserData)
{
	S32 i;
	for (i = eaSize(&pStoreDef->eaRestockDefs)-1; i >= 0; i--)
	{
		// Assign a unique name to each persisted store item
		char *estrUniqueRowName = NULL;
		const char *pchRewardTable = REF_STRING_FROM_HANDLE(pStoreDef->eaRestockDefs[i]->hRewardTable);
		estrPrintf(&estrUniqueRowName, "%s_|_%d", pchRewardTable, i);
		pStoreDef->eaRestockDefs[i]->pchName = allocAddString(estrUniqueRowName);
		estrDestroy(&estrUniqueRowName);
	}
	return store_Validate(pStoreDef);
}


static void *se_createInventoryItem(METable *pTable, StoreDef *pStoreDef, StoreItemDef *pItemToClone, StoreItemDef *pBeforeItem, StoreItemDef *pAfterItem)
{
	StoreItemDef *pNewItem;
	Message *pMessage = NULL;
	Message *pMessageFlavor = NULL;
	
	// Allocate the object
	if (pItemToClone) {
		pNewItem = StructClone(parse_StoreItemDef, pItemToClone);
		pMessage = langCreateMessage("", "", "", pItemToClone->cantBuyMessage.pEditorCopy ? pItemToClone->cantBuyMessage.pEditorCopy->pcDefaultString : NULL);
		pMessageFlavor = langCreateMessage("", "", "", pItemToClone->longFlavorDesc.pEditorCopy ? pItemToClone->longFlavorDesc.pEditorCopy->pcDefaultString : NULL);
	} else {
		pNewItem = StructCreate(parse_StoreItemDef);
		pMessage = langCreateMessage("", "", "", NULL);
		pMessageFlavor = langCreateMessage("", "", "", NULL);
	}
	
	assertmsg(pNewItem, "Failed to create store item");
	
	// Create display name message
	pNewItem->cantBuyMessage.pEditorCopy = pMessage;
	pNewItem->longFlavorDesc.pEditorCopy = pMessageFlavor;
	
	return pNewItem;
}

static void *se_createRestockDef(METable *pTable, StoreDef *pStoreDef, StoreRestockDef *pDefToClone, StoreRestockDef *pBeforeDef, StoreRestockDef *pAfterDef)
{
	StoreRestockDef *pNewDef;
	
	// Allocate the object
	if (pDefToClone) {
		pNewDef = StructClone(parse_StoreRestockDef, pDefToClone);
	} else {
		pNewDef = StructCreate(parse_StoreRestockDef);
	}
	
	assertmsg(pNewDef, "Failed to create persisted store restock def");
	
	return pNewDef;
}

static void *se_createDiscountDef(METable *pTable, StoreDef *pStoreDef, StoreDiscountDef *pDefToClone, StoreDiscountDef *pBeforeDef, StoreDiscountDef *pAfterDef)
{
	StoreDiscountDef *pNewDef;
	
	// Allocate the object
	if (pDefToClone) {
		pNewDef = StructClone(parse_StoreDiscountDef, pDefToClone);
	} else {
		pNewDef = StructCreate(parse_StoreDiscountDef);
	}
	
	assertmsg(pNewDef, "Failed to create store discount def");
	
	return pNewDef;
}

static void se_fixDiscountMessages(StoreDef *pStoreDef, StoreDiscountDef *pDiscountDef)
{
	char *tmpS = NULL;
	char *tmpKeyPrefix = NULL;
	estrStackCreate(&tmpS);
	estrStackCreate(&tmpKeyPrefix);

	estrCopy2(&tmpKeyPrefix, "StoreDef.");
	if(g_pcGameBranch)
	{
		estrConcat(&tmpKeyPrefix, g_pcGameBranch, (int)strlen(g_pcGameBranch));
		estrConcatChar(&tmpKeyPrefix, '.');
	}

	// Display name
	if (!pDiscountDef->msgDisplayName.pEditorCopy) {
		pDiscountDef->msgDisplayName.pEditorCopy = StructCreate(parse_Message);
	}
	
	estrPrintf(&tmpS, "%s%s.DisplayName.%s", tmpKeyPrefix, pStoreDef->name, pDiscountDef->pchName);
	if (!pDiscountDef->msgDisplayName.pEditorCopy->pcMessageKey || 
		(stricmp(tmpS, pDiscountDef->msgDisplayName.pEditorCopy->pcMessageKey) != 0)) {
		pDiscountDef->msgDisplayName.pEditorCopy->pcMessageKey = allocAddString(tmpS);
	}
	
	estrPrintf(&tmpS, "Display name message");
	if (!pDiscountDef->msgDisplayName.pEditorCopy->pcDescription ||
		(stricmp(tmpS, pDiscountDef->msgDisplayName.pEditorCopy->pcDescription) != 0)) {
		StructFreeString(pDiscountDef->msgDisplayName.pEditorCopy->pcDescription);
		pDiscountDef->msgDisplayName.pEditorCopy->pcDescription = StructAllocString(tmpS);
	}
	
	estrPrintf(&tmpS, "StoreDiscountDef");
	if (!pDiscountDef->msgDisplayName.pEditorCopy->pcScope ||
		(stricmp(tmpS, pDiscountDef->msgDisplayName.pEditorCopy->pcScope) != 0)) {
		pDiscountDef->msgDisplayName.pEditorCopy->pcScope = allocAddString(tmpS);
	}

	// Description
	if (!pDiscountDef->msgDescription.pEditorCopy) {
		pDiscountDef->msgDescription.pEditorCopy = StructCreate(parse_Message);
	}

	estrPrintf(&tmpS, "%s%s.Description.%s", tmpKeyPrefix, pStoreDef->name, pDiscountDef->pchName);
	if (!pDiscountDef->msgDescription.pEditorCopy->pcMessageKey || 
		(stricmp(tmpS, pDiscountDef->msgDescription.pEditorCopy->pcMessageKey) != 0)) {
		pDiscountDef->msgDescription.pEditorCopy->pcMessageKey = allocAddString(tmpS);
	}
	
	estrPrintf(&tmpS, "Description message");
	if (!pDiscountDef->msgDescription.pEditorCopy->pcDescription ||
		(stricmp(tmpS, pDiscountDef->msgDescription.pEditorCopy->pcDescription) != 0)) {
		StructFreeString(pDiscountDef->msgDescription.pEditorCopy->pcDescription);
		pDiscountDef->msgDescription.pEditorCopy->pcDescription = StructAllocString(tmpS);
	}
	
	estrPrintf(&tmpS, "StoreDiscountDef");
	if (!pDiscountDef->msgDescription.pEditorCopy->pcScope ||
		(stricmp(tmpS, pDiscountDef->msgDescription.pEditorCopy->pcScope) != 0)) {
		pDiscountDef->msgDescription.pEditorCopy->pcScope = allocAddString(tmpS);
	}

	estrDestroy(&tmpS);
	estrDestroy(&tmpKeyPrefix);
}

static void se_fixMessages(StoreDef *pStoreDef, StoreItemDef *pItemDef)
{
	char *tmpS = NULL;
	char *tmpKeyPrefix = NULL;
	estrStackCreate(&tmpS);
	estrStackCreate(&tmpKeyPrefix);
	
	if (!pItemDef->cantBuyMessage.pEditorCopy) {
		pItemDef->cantBuyMessage.pEditorCopy = StructCreate(parse_Message);
	}

	estrCopy2(&tmpKeyPrefix, "StoreDef.");
	if(g_pcGameBranch)
	{
		estrConcat(&tmpKeyPrefix, g_pcGameBranch, (int)strlen(g_pcGameBranch));
		estrConcatChar(&tmpKeyPrefix, '.');
	}
	
	// Set up key if not exactly
	estrPrintf(&tmpS, "%s%s.Item.%s", tmpKeyPrefix, pStoreDef->name, REF_STRING_FROM_HANDLE(pItemDef->hItem));
	if (!pItemDef->cantBuyMessage.pEditorCopy->pcMessageKey || 
		(stricmp(tmpS, pItemDef->cantBuyMessage.pEditorCopy->pcMessageKey) != 0)) {
			pItemDef->cantBuyMessage.pEditorCopy->pcMessageKey = allocAddString(tmpS);
	}

	estrPrintf(&tmpS, "Message describing why you can't purchase this item");
	if (!pItemDef->cantBuyMessage.pEditorCopy->pcDescription ||
		(stricmp(tmpS, pItemDef->cantBuyMessage.pEditorCopy->pcDescription) != 0)) {
			StructFreeString(pItemDef->cantBuyMessage.pEditorCopy->pcDescription);
			pItemDef->cantBuyMessage.pEditorCopy->pcDescription = StructAllocString(tmpS);
	}

	estrPrintf(&tmpS, "StoreItemDef");
	if (!pItemDef->cantBuyMessage.pEditorCopy->pcScope ||
		(stricmp(tmpS, pItemDef->cantBuyMessage.pEditorCopy->pcScope) != 0)) {
			pItemDef->cantBuyMessage.pEditorCopy->pcScope = allocAddString(tmpS);
	}

	//Flavor message
	if (!pItemDef->longFlavorDesc.pEditorCopy) {
		pItemDef->longFlavorDesc.pEditorCopy = StructCreate(parse_Message);
	}

	estrCopy2(&tmpKeyPrefix, "StoreDef.");
	if(g_pcGameBranch)
	{
		estrConcat(&tmpKeyPrefix, g_pcGameBranch, (int)strlen(g_pcGameBranch));
		estrConcatChar(&tmpKeyPrefix, '.');
	}

	// Set up key if not exactly
	estrPrintf(&tmpS, "%s%s.Item.%s.Flavor", tmpKeyPrefix, pStoreDef->name, REF_STRING_FROM_HANDLE(pItemDef->hItem));
	if (!pItemDef->longFlavorDesc.pEditorCopy->pcMessageKey || 
		(stricmp(tmpS, pItemDef->longFlavorDesc.pEditorCopy->pcMessageKey) != 0)) {
			pItemDef->longFlavorDesc.pEditorCopy->pcMessageKey = allocAddString(tmpS);
	}

	estrPrintf(&tmpS, "Message up-selling the item in detail.");
	if (!pItemDef->longFlavorDesc.pEditorCopy->pcDescription ||
		(stricmp(tmpS, pItemDef->longFlavorDesc.pEditorCopy->pcDescription) != 0)) {
			StructFreeString(pItemDef->longFlavorDesc.pEditorCopy->pcDescription);
			pItemDef->longFlavorDesc.pEditorCopy->pcDescription = StructAllocString(tmpS);
	}

	estrPrintf(&tmpS, "StoreItemDef");
	if (!pItemDef->longFlavorDesc.pEditorCopy->pcScope ||
		(stricmp(tmpS, pItemDef->longFlavorDesc.pEditorCopy->pcScope) != 0)) {
			pItemDef->longFlavorDesc.pEditorCopy->pcScope = allocAddString(tmpS);
	}

	// Custom requirements message to buy this item
	if (!pItemDef->requirementsMessage.pEditorCopy) {
		pItemDef->requirementsMessage.pEditorCopy = StructCreate(parse_Message);
	}

	estrCopy2(&tmpKeyPrefix, "StoreDef.");
	if(g_pcGameBranch)
	{
		estrConcat(&tmpKeyPrefix, g_pcGameBranch, (int)strlen(g_pcGameBranch));
		estrConcatChar(&tmpKeyPrefix, '.');
	}

	// Set up key if not exactly
	estrPrintf(&tmpS, "%s%s.Item.%s.Requirements", tmpKeyPrefix, pStoreDef->name, REF_STRING_FROM_HANDLE(pItemDef->hItem));
	if (!pItemDef->requirementsMessage.pEditorCopy->pcMessageKey || 
		(stricmp(tmpS, pItemDef->requirementsMessage.pEditorCopy->pcMessageKey) != 0)) {
			pItemDef->requirementsMessage.pEditorCopy->pcMessageKey = allocAddString(tmpS);
	}

	estrPrintf(&tmpS, "Message describing additional requirements for this item");
	if (!pItemDef->requirementsMessage.pEditorCopy->pcDescription ||
		(stricmp(tmpS, pItemDef->requirementsMessage.pEditorCopy->pcDescription) != 0)) {
			StructFreeString(pItemDef->requirementsMessage.pEditorCopy->pcDescription);
			pItemDef->requirementsMessage.pEditorCopy->pcDescription = StructAllocString(tmpS);
	}

	estrPrintf(&tmpS, "StoreItemDef");
	if (!pItemDef->requirementsMessage.pEditorCopy->pcScope ||
		(stricmp(tmpS, pItemDef->requirementsMessage.pEditorCopy->pcScope) != 0)) {
			pItemDef->requirementsMessage.pEditorCopy->pcScope = allocAddString(tmpS);
	}

	estrDestroy(&tmpS);
	estrDestroy(&tmpKeyPrefix);
}

static void se_postOpenCallback(METable *pTable, StoreDef *pStoreDef, StoreDef *pOrigStoreDef)
{
	S32 i;
	
	for (i = eaSize(&pStoreDef->inventory)-1; i >= 0; i--){
		se_fixMessages(pStoreDef, pStoreDef->inventory[i]);
	}
	
	if (pOrigStoreDef) {
		for (i = eaSize(&pOrigStoreDef->inventory)-1; i >= 0; i--){
			se_fixMessages(pOrigStoreDef, pOrigStoreDef->inventory[i]);
		}
	}
	
	for (i = eaSize(&pStoreDef->eaDiscountDefs)-1; i >= 0; i--) {
		se_fixDiscountMessages(pStoreDef, pStoreDef->eaDiscountDefs[i]);
	}

	if (pOrigStoreDef) {
		for (i = eaSize(&pOrigStoreDef->eaDiscountDefs)-1; i >= 0; i--){
			se_fixDiscountMessages(pOrigStoreDef, pOrigStoreDef->eaDiscountDefs[i]);
		}
	}
}

static void se_preSaveCallback(METable *pTable, StoreDef *pStoreDef)
{
	S32 i;
	
	for (i = eaSize(&pStoreDef->inventory)-1; i >= 0; i--){
		se_fixMessages(pStoreDef, pStoreDef->inventory[i]);
	}
	for (i = eaSize(&pStoreDef->eaDiscountDefs)-1; i >= 0; i--){
		se_fixDiscountMessages(pStoreDef, pStoreDef->eaDiscountDefs[i]);
	}
	if (!pStoreDef->bIsPersisted) {
		eaDestroyStruct(&pStoreDef->eaRestockDefs, parse_StoreRestockDef);
	}
}

static void *se_createObject(METable *pTable, StoreDef *pObjectToClone, char *pcNewName, bool bCloneKeepsKeys)
{
	StoreDef *pNewDef = NULL;
	char buf[128];
	const char *pcBaseName;
	char *pchPath = NULL;

	// Create the object
	if (pObjectToClone) {
		pNewDef = StructClone(parse_StoreDef, pObjectToClone);
		pcBaseName = pObjectToClone->name;
	} else {
		pNewDef = StructCreate(parse_StoreDef);

		pcBaseName = "_New_Store";
	}
	// Use provided name if available
	if (pcNewName) {
		pcBaseName = pcNewName;
	}

	assertmsg(pNewDef, "Failed to create store");

	// Assign a new name
	pNewDef->name = (char*)METableMakeNewNameShared(pTable, pcBaseName, true);

	// Assign a file
	sprintf(buf,
		FORMAT_OK(GameBranch_FixupPath(&pchPath, "defs/stores/%s.store", true, false)),
		pNewDef->name);
	pNewDef->filename = (char*)allocAddString(buf);

	estrDestroy(&pchPath);
	
	return pNewDef;
}


static void *se_tableCreateCallback(METable *pTable, StoreDef *pObjectToClone, bool bCloneKeepsKeys)
{
	return se_createObject(pTable, pObjectToClone, NULL, bCloneKeepsKeys);
}


static void *se_windowCreateCallback(MEWindow *pWindow, StoreDef *pObjectToClone)
{
	return se_createObject(pWindow->pTable, pObjectToClone, NULL, false);
}


static void se_dictChangeCallback(enumResourceEventType eType, const char *pDictName, ConstReferenceData pRefData, Referent pReferent, METable *pTable)
{
	store_ShowItemContextGenerateSetup();
	METableDictChanged(pTable, eType, pReferent, pRefData);
}

static void se_IsPersistedChangeCallback(METable *pTable, StoreDef *pStoreDef, void *pUserData, bool bInitNotify)
{
	METableSetFieldNotApplicable(pTable, pStoreDef, "Item Level", !pStoreDef->bIsPersisted);

	if (!pStoreDef->bIsPersisted)
	{
		pStoreDef->iItemLevel = 0;

		//RestockDefs are only valid when a store is persisted
		METableHideSubTable(pTable, pStoreDef, seRestockId, true);
	}
	else
	{
		METableHideSubTable(pTable, pStoreDef, seRestockId, false);
	}
}

static void se_ContentsTypeChangeCallback(METable *pTable, StoreDef *pStoreDef, void *pUserData, bool bInitNotify)
{
	int i;
	for(i = 0; i < eaSize(&pStoreDef->inventory); i++)
	{
		if(pStoreDef->eContents == Store_Sellable_Items)
		{
			METableSetSubFieldNotApplicable(pTable, pStoreDef, seInventoryId, pStoreDef->inventory[i], "Count", 1);
			METableSetSubFieldNotApplicable(pTable, pStoreDef, seInventoryId, pStoreDef->inventory[i], "Max Available", 1);
			METableSetSubFieldNotApplicable(pTable, pStoreDef, seInventoryId, pStoreDef->inventory[i], "Recharge Rate", 1);
			METableSetSubFieldNotApplicable(pTable, pStoreDef, seInventoryId, pStoreDef->inventory[i], "Use Currency", 1);
			METableSetSubFieldNotApplicable(pTable, pStoreDef, seInventoryId, pStoreDef->inventory[i], "Required Mission", 1);
			METableSetSubFieldNotApplicable(pTable, pStoreDef, seInventoryId, pStoreDef->inventory[i], "Requires Expr", 1);
			METableSetSubFieldNotApplicable(pTable, pStoreDef, seInventoryId, pStoreDef->inventory[i], "Requires Msg", 1);
			METableSetSubFieldNotApplicable(pTable, pStoreDef, seInventoryId, pStoreDef->inventory[i], "Override Value Recipe", 1);
			METableSetSubFieldNotApplicable(pTable, pStoreDef, seInventoryId, pStoreDef->inventory[i], "Force Bind", 1);
			METableSetSubFieldNotApplicable(pTable, pStoreDef, seInventoryId, pStoreDef->inventory[i], "Required Numeric", 1);
			METableSetSubFieldNotApplicable(pTable, pStoreDef, seInventoryId, pStoreDef->inventory[i], "Req. Numeric Value", 1);
			METableSetSubFieldNotApplicable(pTable, pStoreDef, seInventoryId, pStoreDef->inventory[i], "Req. Numeric Incr", 1);
			METableSetSubFieldNotApplicable(pTable, pStoreDef, seInventoryId, pStoreDef->inventory[i], "Purchase Time", 1);
			METableSetSubFieldNotApplicable(pTable, pStoreDef, seInventoryId, pStoreDef->inventory[i], "MicroTransaction", 1);
		} 
		else
		{
			METableSetSubFieldNotApplicable(pTable, pStoreDef, seInventoryId, pStoreDef->inventory[i], "Count", 0);
			METableSetSubFieldNotApplicable(pTable, pStoreDef, seInventoryId, pStoreDef->inventory[i], "Max Available", 0);
			METableSetSubFieldNotApplicable(pTable, pStoreDef, seInventoryId, pStoreDef->inventory[i], "Recharge Rate", 0);
			METableSetSubFieldNotApplicable(pTable, pStoreDef, seInventoryId, pStoreDef->inventory[i], "Use Currency", 0);
			METableSetSubFieldNotApplicable(pTable, pStoreDef, seInventoryId, pStoreDef->inventory[i], "Required Mission", 0);
			METableSetSubFieldNotApplicable(pTable, pStoreDef, seInventoryId, pStoreDef->inventory[i], "Requires Expr", 0);
			METableSetSubFieldNotApplicable(pTable, pStoreDef, seInventoryId, pStoreDef->inventory[i], "Requires Msg", 0);
			METableSetSubFieldNotApplicable(pTable, pStoreDef, seInventoryId, pStoreDef->inventory[i], "Override Value Recipe", 0);
			METableSetSubFieldNotApplicable(pTable, pStoreDef, seInventoryId, pStoreDef->inventory[i], "Force Bind", 0);
			METableSetSubFieldNotApplicable(pTable, pStoreDef, seInventoryId, pStoreDef->inventory[i], "Required Numeric", 0);
			METableSetSubFieldNotApplicable(pTable, pStoreDef, seInventoryId, pStoreDef->inventory[i], "Req. Numeric Value", 0);
			METableSetSubFieldNotApplicable(pTable, pStoreDef, seInventoryId, pStoreDef->inventory[i], "Req. Numeric Incr", 0);
			METableSetSubFieldNotApplicable(pTable, pStoreDef, seInventoryId, pStoreDef->inventory[i], "Purchase Time", 0);
			METableSetSubFieldNotApplicable(pTable, pStoreDef, seInventoryId, pStoreDef->inventory[i], "MicroTransaction", 0);
		}
	}
}

static void se_ProvisionChangeCallback(METable *pTable, StoreDef *pStoreDef, void *pUserData, bool bInitNotify)
{
    METableSetFieldNotApplicable(pTable, pStoreDef, "Group Project Type", pStoreDef->bProvisionFromGroupProject == false);
    METableSetFieldNotApplicable(pTable, pStoreDef, "Provisioning Project", pStoreDef->bProvisionFromGroupProject == false);
    METableSetFieldNotApplicable(pTable, pStoreDef, "Provisioning Numeric", pStoreDef->bProvisionFromGroupProject == false);
}

static void se_initCallbacks(MEWindow *pWindow, METable *pTable)
{
	// General Window callbacks
	MEWindowSetCreateCallback(pWindow, se_windowCreateCallback);

	// General table callbacks
	METableSetValidateCallback(pTable, se_validateCallback, pTable);
	METableSetCreateCallback(pTable, se_tableCreateCallback);
	METableSetPostOpenCallback(pTable, se_postOpenCallback);
	METableSetPreSaveCallback(pTable, se_preSaveCallback);

	METableSetColumnChangeCallback(pTable, "Contents", se_ContentsTypeChangeCallback, NULL);
	METableSetColumnChangeCallback(pTable, "Is Persisted", se_IsPersistedChangeCallback, NULL);
    METableSetColumnChangeCallback(pTable, "Provision From Group Project", se_ProvisionChangeCallback, NULL);

	// We need this registered here instead of by METable because the dictionary will 
	// only allow each callback function to be registered once and there may be multiple
	// METable instances.  Our local callback just passes through to the METable.
	resDictRegisterEventCallback(g_StoreDictionary, se_dictChangeCallback, pTable);
}


//---------------------------------------------------------------------------------------------------
// UI Init
//---------------------------------------------------------------------------------------------------

static void se_initStoreColumns(METable *pTable)
{
	char *pchPath = NULL;

	GameBranch_GetDirectory(&pchPath, STORES_BASE_DIR);

	METableAddSimpleColumn(pTable, "Name", "name", 150, NULL, kMEFieldType_TextEntry);
	
	// Lock in name column
	METableSetNumLockedColumns(pTable, 2);
	
	METableAddScopeColumn(pTable,      "Scope",           "Scope",            160, SE_GROUP_MAIN, kMEFieldType_TextEntry); // Not validated on purpose
	METableAddFileNameColumn(pTable,   "File Name",       "filename",         210, SE_GROUP_MAIN, NULL, pchPath, pchPath, ".store", UIBrowseNewOrExisting);
	METableAddSimpleColumn(pTable,     "Notes",           "notes",            150, SE_GROUP_MAIN, kMEFieldType_MultiText);
	METableAddExprColumn(pTable,	   "Requires Expr",   "ExprRequires",     100, SE_GROUP_MAIN, store_GetBuyContext());
	METableAddGlobalDictColumn(pTable, "Currency",        "hCurrency",        240, SE_GROUP_MAIN, kMEFieldType_ValidatedTextEntry, "ItemDef", "resourceName");
	METableAddSimpleColumn(pTable,     "Buy Multiplier",  "BuyMultiplier",    100, SE_GROUP_MAIN, kMEFieldType_TextEntry);
	METableAddSimpleColumn(pTable,     "Sell Multiplier", "SellMultiplier",   100, SE_GROUP_MAIN, kMEFieldType_TextEntry);
	METableAddExprColumn(pTable,       "EP Conversion",   "ExprEPConversion", 100, SE_GROUP_MAIN, g_pItemContext);
	METableAddSimpleColumn(pTable,     "Sell Enabled",    "SellEnabled",      100, SE_GROUP_MAIN, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable,     "Is Persisted",    "IsPersisted",      100, SE_GROUP_MAIN, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable,     "Research Store",  "IsResearchStore",  100, SE_GROUP_MAIN, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable,"Display C-Store Points","DisplayStoreCPoints",200, SE_GROUP_MAIN, kMEFieldType_BooleanCombo);
	METableAddEnumColumn(pTable,       "Contents",        "Contents",         150, SE_GROUP_MAIN, kMEFieldType_Combo, StoreContentsEnum);
	METableAddEnumColumn(pTable,       "Region",		  "Region",           150, SE_GROUP_MAIN, kMEFieldType_Combo, WorldRegionTypeEnum);
	METableAddSimpleColumn(pTable,     "Item Level",	  "ItemLevel",       100, SE_GROUP_MAIN, kMEFieldType_TextEntry);
    METableAddSimpleColumn(pTable,     "Guild Map Owner Members Only",  "GuildMapOwnerMembersOnly",  100, SE_GROUP_MAIN, kMEFieldType_BooleanCombo);
    METableAddSimpleColumn(pTable,     "Provision From Group Project",  "ProvisionFromGroupProject",  100, SE_GROUP_MAIN, kMEFieldType_BooleanCombo);
    METableAddEnumColumn(pTable,       "Group Project Type", "GroupProjectType", 150, SE_GROUP_MAIN, kMEFieldType_Combo, GroupProjectTypeEnum);
    METableAddGlobalDictColumn(pTable, "Provisioning Project", "provisioningProjectDef", 200, SE_GROUP_MAIN, kMEFieldType_ValidatedTextEntry, "GroupProjectDef", "resourceName");
    METableAddGlobalDictColumn(pTable, "Provisioning Numeric", "provisioningNumericDef", 200, SE_GROUP_MAIN, kMEFieldType_ValidatedTextEntry, "GroupProjectNumericDef", "resourceName");

	estrDestroy(&pchPath);
}


static void se_initInventoryColumns(METable *pTable)
{
	static ExprContext* pStoreExprContext = NULL;
	int id;
	const char **eaKeys = NULL;
	
	if (!pStoreExprContext) {
		pStoreExprContext = exprContextCreate();
		exprContextSetFuncTable(pStoreExprContext, store_CreateExprFuncTable());
	}
	
	// Create the subtable and get the ID
	seInventoryId = id = METableCreateSubTable(pTable, "Store Item", ".inventory", parse_StoreItemDef, NULL, NULL, NULL, se_createInventoryItem);
	
	// Lock in label column
	METableSetNumLockedSubColumns(pTable, id, 2);
	
	METableAddGlobalDictSubColumn(pTable, id, "Item Name",        "item", 240, NULL, kMEFieldType_ValidatedTextEntry, "ItemDef", "resourceName");
	METableAddSimpleSubColumn(pTable,	  id, "Count",            "Count", 100, SE_SUBGROUP_INVENTORY, kMEFieldType_TextEntry);
	
	METableAddSimpleSubColumn(pTable,	  id, "Use Currency",     "ForceUseCurrency", 100, SE_SUBGROUP_INVENTORY, kMEFieldType_BooleanCombo);
	
	METableAddGlobalDictSubColumn(pTable, id, "Required Mission", "ReqMission", 240, SE_SUBGROUP_INVENTORY, kMEFieldType_ValidatedTextEntry, "Mission", "resourceName");
	METableAddExprSubColumn(pTable,		  id, "Requires Expr",    "ExprCanBuy", 100, SE_SUBGROUP_INVENTORY, pStoreExprContext);
	METableAddSimpleSubColumn(pTable,	  id, "Requires Msg",     ".cantBuyMessage.EditorCopy", 160, SE_SUBGROUP_INVENTORY, kMEFieldType_Message);
	METableAddSimpleSubColumn(pTable,	  id, "Custom Reqs Msg",     ".requirementsMessage.EditorCopy", 160, SE_SUBGROUP_INVENTORY, kMEFieldType_Message);
	METableAddGlobalDictSubColumn(pTable, id, "Override Value Recipe",   "OverrideValueRecipe",       140, SE_SUBGROUP_INVENTORY, kMEFieldType_ValidatedTextEntry, "ItemDef", "resourceName");
	METableAddSimpleSubColumn(pTable,	  id, "Force Bind",     "ForceBind", 100, SE_SUBGROUP_INVENTORY, kMEFieldType_BooleanCombo);
	METableAddEnumSubColumn(pTable,		  id, "Category",		"Category",  100, SE_SUBGROUP_INVENTORY, kMEFieldType_Combo, StoreCategoryEnum);

	DefineFillAllKeysAndValues(StoreHighlightCategoryEnum, &eaKeys, NULL);
	if (eaSize(&eaKeys) > 0)
		METableAddEnumSubColumn(pTable,	  id, "Highlight Category",		"HighlightCategory",  100, SE_SUBGROUP_INVENTORY, kMEFieldType_FlagCombo, StoreHighlightCategoryEnum);
	eaDestroy(&eaKeys);

	METableAddSimpleSubColumn(pTable,	  id, "Long Flavor Desc",     ".longFlavorDesc.EditorCopy", 160, SE_SUBGROUP_INVENTORY, kMEFieldType_Message);
	METableAddSubColumn(pTable,			  id, "Display Texture",			"DisplayTex",	NULL,	180, SE_SUBGROUP_INVENTORY, kMEFieldType_Texture, NULL, NULL, NULL, NULL, "texture_library/ui/Icons", NULL, NULL);

	METableAddGlobalDictSubColumn(pTable, id, "Required Numeric", "RequiredNumeric", 140, SE_SUBGROUP_INVENTORY, kMEFieldType_ValidatedTextEntry, "ItemDef", "resourceName");
	METableAddSimpleSubColumn(pTable,	  id, "Req. Numeric Value", "RequiredNumericValue", 140, SE_SUBGROUP_INVENTORY, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable,	  id, "Req. Numeric Incr", "RequiredNumericIncr", 140, SE_SUBGROUP_INVENTORY, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable,	  id, "Research Time", "ResearchTime", 140, SE_SUBGROUP_INVENTORY, kMEFieldType_TextEntry);

	METableAddGlobalDictSubColumn(pTable, id, "MicroTransaction", "ReqMicroTransaction", 240, SE_SUBGROUP_INVENTORY, kMEFieldType_ValidatedTextEntry, "MicroTransactionDef", "resourceName");
	METableAddExprSubColumn(pTable,		  id, "Show Item Expr",  "ExprShowItem",     100, SE_SUBGROUP_INVENTORY, store_GetShowItemContext());

	METableAddSimpleSubColumn(pTable,	  id, "Required Activity", "ActivityName",   180, SE_SUBGROUP_INVENTORY, kMEFieldType_TextEntry);
}

static void se_initRestockColumns(METable *pTable)
{
	int id;

	// Create the subtable and get the ID
	seRestockId = id = METableCreateSubTable(pTable, "Persisted Store Restock Defs", ".RestockDef", parse_StoreRestockDef, NULL, NULL, NULL, se_createRestockDef);
	
	// Lock in label column
	METableSetNumLockedSubColumns(pTable, id, 2);

	//The reward table used to replenish items
	METableAddGlobalDictSubColumn(pTable, id, "Reward Table", "RewardTable", 100, SE_SUBGROUP_RESTOCK, kMEFieldType_ValidatedTextEntry, "RewardTable", "resourceName");

	//The level to pass into the reward table
	METableAddSimpleSubColumn(pTable, id, "Item Level Override", "ItemLevel", 100, SE_SUBGROUP_RESTOCK, kMEFieldType_TextEntry);
	
	//Optional UI Category
	METableAddEnumSubColumn(pTable,	id, "Category", "Category",  100, SE_SUBGROUP_RESTOCK, kMEFieldType_Combo, StoreCategoryEnum);

	//Replenish times
	METableAddSimpleSubColumn(pTable, id, "Min Replenish Time", "MinReplenishTime", 120, SE_SUBGROUP_RESTOCK, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id, "Max Replenish Time", "MaxReplenishTime", 120, SE_SUBGROUP_RESTOCK, kMEFieldType_TextEntry);

	//Expire times
	METableAddSimpleSubColumn(pTable, id, "Min Expire Time", "MinExpireTime", 120, SE_SUBGROUP_RESTOCK, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id, "Max Expire Time", "MaxExpireTime", 120, SE_SUBGROUP_RESTOCK, kMEFieldType_TextEntry);

	//Item counts
	METableAddSimpleSubColumn(pTable, id, "Min Item Count", "MinItemCount", 120, SE_SUBGROUP_RESTOCK, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id, "Max Item Count", "MaxItemCount", 120, SE_SUBGROUP_RESTOCK, kMEFieldType_TextEntry);
}

static void se_initDiscountColumns(METable *pTable)
{
	int id;

	// Create the subtable and get the ID
	seDiscountId = id = METableCreateSubTable(pTable, "DiscountDef", ".DiscountDef", parse_StoreDiscountDef, NULL, NULL, NULL, se_createDiscountDef);
	
	METableAddSimpleSubColumn(pTable, id, "Name", "name", 150, NULL, kMEFieldType_TextEntry);

	// Lock in label column
	METableSetNumLockedSubColumns(pTable, id, 2);

	// Display name message
	METableAddSimpleSubColumn(pTable, id, "Display Name", ".DisplayName.EditorCopy", 100, SE_SUBGROUP_DISCOUNT, kMEFieldType_Message);

	// Description message
	METableAddSimpleSubColumn(pTable, id, "Description", ".Description.EditorCopy", 100, SE_SUBGROUP_DISCOUNT, kMEFieldType_Message);

	// The reward table used to replenish items
	METableAddGlobalDictSubColumn(pTable, id, "Item Cost", "DiscountCostItem", 240, NULL, kMEFieldType_ValidatedTextEntry, "ItemDef", "resourceName");
	
	// The level to pass into the reward table
	METableAddSimpleSubColumn(pTable, id, "Discount Percent", "DiscountPercent", 100, SE_SUBGROUP_DISCOUNT, kMEFieldType_TextEntry);
	
	// The items to apply the discount to
	METableAddEnumSubColumn(pTable,	id, "Apply To Item Category", "ApplyToItemCategory",  100, SE_SUBGROUP_DISCOUNT, kMEFieldType_Combo, ItemCategoryEnum);

	// Requires expression
	METableAddExprSubColumn(pTable, id, "Requires Expr", "ExprRequires", 100, SE_SUBGROUP_DISCOUNT, store_GetBuyContext());
}

static void se_init(MultiEditEMDoc *pEditorDoc)
{
	if (!seWindow) {
		// Create the editor window
		seWindow = MEWindowCreate("Store Editor", "Store", "Stores", SEARCH_TYPE_STORE, g_StoreDictionary, parse_StoreDef, "name", "filename", "scope", pEditorDoc);

		// Add store-specific columns
		se_initStoreColumns(seWindow->pTable);
		se_initInventoryColumns(seWindow->pTable);
		se_initRestockColumns(seWindow->pTable);
		se_initDiscountColumns(seWindow->pTable);
		METableFinishColumns(seWindow->pTable);

		// Init the menus after adding all the columns
		MEWindowInitTableMenus(seWindow);

		// Set the callbacks
		se_initCallbacks(seWindow, seWindow->pTable);

		// Set edit mode
		resSetDictionaryEditMode(g_hRewardTableDict, true);
	}

	// Show the window
	ui_WindowPresent(seWindow->pUIWindow);
}


//---------------------------------------------------------------------------------------------------
// Public Interface
//---------------------------------------------------------------------------------------------------

MEWindow *storeEditor_init(MultiEditEMDoc *pEditorDoc) 
{
	se_init(pEditorDoc);

	return seWindow;
}


void storeEditor_createStore(char *pcName)
{
	// Create a new object since it is not in the dictionary
	// Add the object as a new object with no old
	void *pObject = se_createObject(seWindow->pTable, NULL, pcName, false);
	METableAddRowByObject(seWindow->pTable, pObject, 1, 1);
}

#endif
