//
// PowerStoreEditor.c
//

#ifndef NO_EDITORS

#include "MultiEditTable.h"
#include "MultiEditWindow.h"
#include "ResourceSearch.h"
#include "powerStoreCommon.h"
#include "PowerTree.h"
#include "StringCache.h"
#include "EString.h"

#include "AutoGen/PowerTree_h_ast.h"

// This magic line is required if you run in Full Debug to avoid assert
AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

#define SE_GROUP_MAIN        "Main"
#define SE_SUBGROUP_INVENTORY "Inventory"

//-----------------------------------------------------------------------------------
// Global Data
//-----------------------------------------------------------------------------------

static MEWindow *pseWindow = NULL;
static int pseInventoryId = 0;


//---------------------------------------------------------------------------------------------------
// Callbacks
//---------------------------------------------------------------------------------------------------


static int pse_validateCallback(METable *pTable, PowerStoreDef *pStoreDef, void *pUserData)
{
	return powerstore_Validate(pStoreDef);
}


static void *pse_createInventoryPower(METable *pTable, PowerStoreDef *pStoreDef, PowerStorePowerDef *pPowerToClone, PowerStorePowerDef *pBeforePower, PowerStorePowerDef *pAfterPower)
{
	PowerStorePowerDef *pNewPower;
	Message *pMessage = NULL;

	// Allocate the object
	if (pPowerToClone) {
		pNewPower = (PowerStorePowerDef*)StructClone(parse_PowerStorePowerDef, pPowerToClone);
		pMessage = langCreateMessage("", "", "", pPowerToClone->cantBuyMessage.pEditorCopy ? pPowerToClone->cantBuyMessage.pEditorCopy->pcDefaultString : NULL);
	} else {
		pNewPower = (PowerStorePowerDef*)StructCreate(parse_PowerStorePowerDef);
		pMessage = langCreateMessage("", "", "", NULL);
	}

	assertmsg(pNewPower, "Failed to create store item");

	// Create display name message
	pNewPower->cantBuyMessage.pEditorCopy = pMessage;

	return pNewPower;
}

static void pse_fixMessages(PowerStoreDef *pStoreDef, PowerStorePowerDef *pPowerDef)
{
	char *tmpS = NULL;
	estrStackCreate(&tmpS);

	if (!pPowerDef->cantBuyMessage.pEditorCopy) {
		pPowerDef->cantBuyMessage.pEditorCopy = StructCreate(parse_Message);
	}

	// Set up key if not exactly
	estrPrintf(&tmpS, "PowerStoreDef.%s.Power.%s", pStoreDef->pcName, REF_STRING_FROM_HANDLE(pPowerDef->hNode));
	if (!pPowerDef->cantBuyMessage.pEditorCopy->pcMessageKey || 
		(stricmp(tmpS, pPowerDef->cantBuyMessage.pEditorCopy->pcMessageKey) != 0)) {
			pPowerDef->cantBuyMessage.pEditorCopy->pcMessageKey = allocAddString(tmpS);
	}

	estrPrintf(&tmpS, "Message describing why you can't purchase this power");
	if (!pPowerDef->cantBuyMessage.pEditorCopy->pcDescription ||
		(stricmp(tmpS, pPowerDef->cantBuyMessage.pEditorCopy->pcDescription) != 0)) {
			StructFreeString(pPowerDef->cantBuyMessage.pEditorCopy->pcDescription);
			pPowerDef->cantBuyMessage.pEditorCopy->pcDescription = StructAllocString(tmpS);
	}

	estrPrintf(&tmpS, "StorePowerDef");
	if (!pPowerDef->cantBuyMessage.pEditorCopy->pcScope ||
		(stricmp(tmpS, pPowerDef->cantBuyMessage.pEditorCopy->pcScope) != 0)) {
			pPowerDef->cantBuyMessage.pEditorCopy->pcScope = allocAddString(tmpS);
	}

	estrDestroy(&tmpS);
}

static void pse_postOpenCallback(METable *pTable, PowerStoreDef *pStoreDef, PowerStoreDef *pOrigStoreDef)
{
	S32 i;

	for (i = eaSize(&pStoreDef->eaInventory)-1; i >= 0; i--){
		pse_fixMessages(pStoreDef, pStoreDef->eaInventory[i]);
	}

	if (pOrigStoreDef) {
		for (i = eaSize(&pOrigStoreDef->eaInventory)-1; i >= 0; i--){
			pse_fixMessages(pOrigStoreDef, pOrigStoreDef->eaInventory[i]);
		}
	}
}

static void pse_preSaveCallback(METable *pTable, PowerStoreDef *pStoreDef)
{
	S32 i;

	for (i = eaSize(&pStoreDef->eaInventory)-1; i >= 0; i--){
		pse_fixMessages(pStoreDef, pStoreDef->eaInventory[i]);
	}
}

static void *pse_createObject(METable *pTable, PowerStoreDef *pObjectToClone, char *pcNewName, bool bCloneKeepsKeys)
{
	PowerStoreDef *pNewDef = NULL;
	char buf[128];
	const char *pcBaseName;

	// Create the object
	if (pObjectToClone) {
		pNewDef = StructClone(parse_PowerStoreDef, pObjectToClone);
		pcBaseName = pObjectToClone->pcName;
	} else {
		pNewDef = StructCreate(parse_PowerStoreDef);

		pcBaseName = "_New_Power_Store";
	}
	// Use provided name if available
	if (pcNewName) {
		pcBaseName = pcNewName;
	}

	assertmsg(pNewDef, "Failed to create power store");

	// Assign a new name
	pNewDef->pcName = (char*)METableMakeNewNameShared(pTable, pcBaseName, true);

	// Assign a file
	sprintf(buf,"defs/powerstores/%s.powerstore",pNewDef->pcName);
	pNewDef->pcFilename = (char*)allocAddString(buf);

	return pNewDef;
}


static void *pse_tableCreateCallback(METable *pTable, PowerStoreDef *pObjectToClone, bool bCloneKeepsKeys)
{
	return pse_createObject(pTable, pObjectToClone, NULL, bCloneKeepsKeys);
}


static void *pse_windowCreateCallback(MEWindow *pWindow, PowerStoreDef *pObjectToClone)
{
	return pse_createObject(pWindow->pTable, pObjectToClone, NULL, false);
}


static char **pse_getTreeNodes(METable *pTable, PowerStorePowerDef *pPowerDef)
{
	PowerTreeDef *pTree;
	PTNodeDef *pNode = NULL;
	ResourceIterator iter;
	char *pcName = NULL;
	char **eaNodeNames = NULL;

	pTree = pPowerDef ? GET_REF(pPowerDef->hTree) : NULL;
	if (pTree) {
		resInitIterator("PowerTreeNodeDef", &iter);
		while (resIteratorGetNext(&iter, &pcName, &pNode)) {
			if (powertree_TreeDefFromNodeDef(pNode) == pTree) {
				eaPush(&eaNodeNames, strdup(pNode->pchNameFull));
			}
		}
		resFreeIterator(&iter);
	}
	return eaNodeNames;
}


static void pse_dictChangeCallback(enumResourceEventType eType, const char *pDictName, ConstReferenceData pRefData, Referent pReferent, METable *pTable)
{
	METableDictChanged(pTable, eType, pReferent, pRefData);
}


static void pse_initCallbacks(MEWindow *pWindow, METable *pTable)
{
	// General Window callbacks
	MEWindowSetCreateCallback(pWindow, pse_windowCreateCallback);

	// General table callbacks
	METableSetValidateCallback(pTable, pse_validateCallback, pTable);
	METableSetCreateCallback(pTable, pse_tableCreateCallback);
	METableSetPostOpenCallback(pTable, pse_postOpenCallback);
	METableSetPreSaveCallback(pTable, pse_preSaveCallback);

	// We need this registered here instead of by METable because the dictionary will 
	// only allow each callback function to be registered once and there may be multiple
	// METable instances.  Our local callback just passes through to the METable.
	resDictRegisterEventCallback(g_PowerStoreDictionary, pse_dictChangeCallback, pTable);
}


//---------------------------------------------------------------------------------------------------
// UI Init
//---------------------------------------------------------------------------------------------------

static void pse_initStoreColumns(METable *pTable)
{
	METableAddSimpleColumn(pTable, "Name", "name", 150, NULL, kMEFieldType_TextEntry);

	// Lock in name column
	METableSetNumLockedColumns(pTable, 2);

	METableAddScopeColumn(pTable,      "Scope",           "Scope",          160, SE_GROUP_MAIN, kMEFieldType_TextEntry); // Not validated on purpose
	METableAddFileNameColumn(pTable,   "File Name",       "filename",       210, SE_GROUP_MAIN, NULL, "defs/powerstores", "defs/powerstores", ".powerstore", UIBrowseNewOrExisting);
	METableAddSimpleColumn(pTable,     "Notes",           "notes",          150, SE_GROUP_MAIN, kMEFieldType_MultiText);
	METableAddGlobalDictColumn(pTable, "Currency",        "hCurrency",      240, SE_GROUP_MAIN, kMEFieldType_ValidatedTextEntry, "ItemDef", "resourceName");
	METableAddSimpleColumn(pTable,     "IsTrainer",       "IsTrainer",		100, SE_GROUP_MAIN, kMEFieldType_BooleanCombo);
}


static void pse_initInventoryColumns(METable *pTable)
{
	int id;

	// Create the subtable and get the ID
	pseInventoryId = id = METableCreateSubTable(pTable, "Store Power", ".inventory", parse_PowerStorePowerDef, NULL, NULL, NULL, pse_createInventoryPower);

	// Lock in label column
	METableSetNumLockedSubColumns(pTable, id, 2);

	METableAddGlobalDictSubColumn(pTable, id, "Power Tree",       "PowerTree", 240, NULL, kMEFieldType_ValidatedTextEntry, "PowerTreeDef", "resourceName");
	METableAddSubColumn(pTable, id, "Power Tree Node",  "PowerTreeNode", NULL, 240, NULL, kMEFieldType_ValidatedTextEntry, NULL, NULL, NULL, NULL, NULL, NULL, pse_getTreeNodes);
	METableAddSimpleSubColumn(pTable,	  id, "Node Rank",		  "Rank", 100, SE_SUBGROUP_INVENTORY, kMEFieldType_TextEntry);

	METableAddSimpleSubColumn(pTable,	  id, "Cost",		      "value", 100, SE_SUBGROUP_INVENTORY, kMEFieldType_TextEntry);

	METableAddExprSubColumn(pTable,		  id, "Requires Expr",    "ExprCanBuy", 100, SE_SUBGROUP_INVENTORY, g_pPowerStoreContext);
	METableAddSimpleSubColumn(pTable,	  id, "Requires Msg",     ".cantBuyMessage.EditorCopy", 160, SE_SUBGROUP_INVENTORY, kMEFieldType_Message);
}


static void pse_init(MultiEditEMDoc *pEditorDoc)
{
	if (!pseWindow) {
		// Create the editor window
		pseWindow = MEWindowCreate("Power Store Editor", "PowerStore", "Power Stores", SEARCH_TYPE_POWER_STORE, g_PowerStoreDictionary, parse_PowerStoreDef, "name", "filename", "scope", pEditorDoc);

		// Add store-specific columns
		pse_initStoreColumns(pseWindow->pTable);
		pse_initInventoryColumns(pseWindow->pTable);
		METableFinishColumns(pseWindow->pTable);

		// Init the menus after adding all the columns
		MEWindowInitTableMenus(pseWindow);

		// Set the callbacks
		pse_initCallbacks(pseWindow, pseWindow->pTable);
	}

	// Show the window
	ui_WindowPresent(pseWindow->pUIWindow);
}


//---------------------------------------------------------------------------------------------------
// Public Interface
//---------------------------------------------------------------------------------------------------

MEWindow *powerStoreEditor_init(MultiEditEMDoc *pEditorDoc) 
{
	pse_init(pEditorDoc);

	resRequestAllResourcesInDictionary("PowerTreeDef");

	return pseWindow;
}


void powerStoreEditor_createPowerStore(char *pcName)
{
	// Create a new object since it is not in the dictionary
	// Add the object as a new object with no old
	void *pObject = pse_createObject(pseWindow->pTable, NULL, pcName, false);
	METableAddRowByObject(pseWindow->pTable, pObject, 1, 1);
}

#endif
