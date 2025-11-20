//
// ItemPowerEditor.c
//

#ifndef NO_EDITORS

#include "ItemCommon.h"
#include "itemCommon_h_ast.h"
#include "ItemPowerEditor.h"
#include "MultiEditTable.h"
#include "MultiEditWindow.h"
#include "Powers.h"
#include "ResourceSearch.h"
#include "StringCache.h"
#include "tokenstore.h"
#include "estring.h"
#include "GameBranch.h"



// This magic line is required if you run in Full Debug to avoid assert
AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

#define IEP_GROUP_MAIN        "Main"
#define IEP_GROUP_POWER       "Power"
#define IEP_SUBGROUP_RESTRICTION "Restriction"
#define IEP_GROUP_AI		  "AI"

//-----------------------------------------------------------------------------------
// Global Data
//-----------------------------------------------------------------------------------

static MEWindow *iepWindow = NULL;
static int iepPowersId = 0;

extern ExprContext *g_pItemContext;

//---------------------------------------------------------------------------------------------------
// Callbacks
//---------------------------------------------------------------------------------------------------


static int iep_compareStrings(const char** left, const char** right)
{
	return stricmp(*left,*right);
}


static int iep_validateCallback(METable *pTable, ItemPowerDef *pItemPowerDef, void *pUserData)
{
	return itempower_Validate(pItemPowerDef);
}

static void iep_postOpenCallback(METable *pTable, ItemPowerDef *pItemPowerDef, ItemPowerDef *pOrigItemPowerDef)
{
	itempower_FixMessages(pItemPowerDef);
	if (pOrigItemPowerDef) {
		itempower_FixMessages(pOrigItemPowerDef);
	}
}


static void iep_preSaveCallback(METable *pTable, ItemPowerDef *pItemPowerDef)
{
	// Remove empty restriction block
	if (pItemPowerDef->pRestriction && 
		(pItemPowerDef->pRestriction->iMaxLevel == 0) &&
		(pItemPowerDef->pRestriction->iMinLevel == 0) &&
		(pItemPowerDef->pRestriction->eRequiredGemSlotType == kItemGemType_None) &&
		(!pItemPowerDef->pRestriction->pRequires)) {
			int col;
			StructDestroy(parse_UsageRestriction, pItemPowerDef->pRestriction);
			ParserFindColumn(parse_ItemPowerDef, "Restriction", &col);
			TokenStoreSetPointer(parse_ItemPowerDef, col, pItemPowerDef, 0, NULL, NULL);
	}

	// Fix up display name
	itempower_FixMessages(pItemPowerDef);
}


static void *iep_createObject(METable *pTable, ItemPowerDef *pObjectToClone, char *pcNewName, bool bCloneKeepsKeys, bool stripTrailingCharactersFromName)
{
	ItemPowerDef *pNewDef = NULL;
	const char *pcBaseName = NULL;
	const char *pcBaseDisplayName = NULL;
	const char *pcBaseDisplayName2 = NULL;
	const char *pcBaseDescription = NULL;
	char *pcBuffer = NULL;
	Message *pMessage;
	char *tmpS = NULL;

	estrStackCreate(&tmpS);

	// Create the object
	if (pObjectToClone) {
		pNewDef = StructClone(parse_ItemPowerDef, pObjectToClone);
		pcBaseName = pObjectToClone->pchName;
		pcBaseDisplayName = pObjectToClone->displayNameMsg.pEditorCopy ? pObjectToClone->displayNameMsg.pEditorCopy->pcDefaultString : NULL;
		pcBaseDisplayName2 = pObjectToClone->displayNameMsg2.pEditorCopy ? pObjectToClone->displayNameMsg2.pEditorCopy->pcDefaultString : NULL;
		pcBaseDescription = pObjectToClone->descriptionMsg.pEditorCopy ? pObjectToClone->descriptionMsg.pEditorCopy->pcDefaultString : NULL;
	} else {
		pNewDef = StructCreate(parse_ItemPowerDef);

		pcBaseName = "_New_Itempower";
		pcBaseDisplayName = NULL;
		pcBaseDisplayName2 = NULL;
		pcBaseDescription = NULL;
	}
	// Use provided name if available
	if (pcNewName) {
		pcBaseName = pcNewName;
		pcBaseDisplayName = NULL;
		pcBaseDisplayName2 = NULL;
		pcBaseDescription = NULL;
	}

	assertmsg(pNewDef, "Failed to create itempower");

	// Assign a new name
	pNewDef->pchName = (char*)METableMakeNewNameShared(pTable, pcBaseName, stripTrailingCharactersFromName);

	// Create display name message
	pMessage = langCreateMessage("", "", "", pcBaseDisplayName);
	pNewDef->displayNameMsg.pEditorCopy = pMessage;

	// Create display name message2
	pMessage = langCreateMessage("", "", "", pcBaseDisplayName2);
	pNewDef->displayNameMsg2.pEditorCopy = pMessage;

	// Create description message
	pMessage = langCreateMessage("", "", "", pcBaseDescription);
	pNewDef->descriptionMsg.pEditorCopy = pMessage;

	// Assign a file
	estrPrintf(&tmpS,
		FORMAT_OK(GameBranch_FixupPath(&pcBuffer,"defs/itempowers/%s.itempower", true, false)),
		pNewDef->pchName);
	pNewDef->pchFileName = (char*)allocAddString(tmpS);

	estrDestroy(&tmpS);
	estrDestroy(&pcBuffer);

	return pNewDef;
}

static void *iep_tableCreateCallback(METable *pTable, ItemPowerDef *pObjectToClone, bool bCloneKeepsKeys)
{
	return iep_createObject(pTable, pObjectToClone, NULL, bCloneKeepsKeys, true);
}


static void *iep_windowCreateCallback(MEWindow *pWindow, ItemPowerDef *pObjectToClone)
{
	return iep_createObject(pWindow->pTable, pObjectToClone, NULL, false, true);
}


static void iep_dictChangeCallback(enumResourceEventType eType, const char *pDictName, ConstReferenceData pRefData, Referent pReferent, METable *pTable)
{
	METableDictChanged(pTable, eType, pReferent, pRefData);
}


static void iep_messageDictChangeCallback(enumResourceEventType eType, const char *pDictName, const char *pcMessageKey, Referent pReferent, METable *pTable)
{
	if ((eType == RESEVENT_RESOURCE_MODIFIED) ||
		(eType == RESEVENT_RESOURCE_REMOVED) ||
		(eType == RESEVENT_RESOURCE_ADDED)) {

		METableMessageChangedRefresh(pTable, pcMessageKey);
	}
}

static void rte_FieldChangedCallback(METable *pTable, ItemPowerDef *pItemPowerDef, void *pUserData, bool bInitNotify)
{
	// hide all the fields that are only on sometimes
	int iHide = 1;
	if (itempower_IsRealPower(pItemPowerDef))
	{
		iHide = 0;
	}

	METableSetFieldNotApplicable(pTable, pItemPowerDef, "Display Name (noun)", iHide);
	METableSetFieldNotApplicable(pTable, pItemPowerDef, "Display Name (adj)", iHide);
	METableSetFieldNotApplicable(pTable, pItemPowerDef, "Description", iHide);

	METableSetFieldNotApplicable(pTable, pItemPowerDef, "Power", iHide);
	METableSetFieldNotApplicable(pTable, pItemPowerDef, "PowerReplace", iHide);

	METableSetFieldNotApplicable(pTable, pItemPowerDef, "AI Requires",        iHide);
	METableSetFieldNotApplicable(pTable, pItemPowerDef, "AI End Condition",   iHide);
	METableSetFieldNotApplicable(pTable, pItemPowerDef, "AI Min Range",       iHide);
	METableSetFieldNotApplicable(pTable, pItemPowerDef, "AI Max Range",       iHide);
	METableSetFieldNotApplicable(pTable, pItemPowerDef, "AI Weight",          iHide);
	METableSetFieldNotApplicable(pTable, pItemPowerDef, "AI Weight Modifier", iHide);
	METableSetFieldNotApplicable(pTable, pItemPowerDef, "AI Chain Target",    iHide);
	METableSetFieldNotApplicable(pTable, pItemPowerDef, "AI Chain Time",      iHide);
	METableSetFieldNotApplicable(pTable, pItemPowerDef, "AI Chain Requires",  iHide);
	METableSetFieldNotApplicable(pTable, pItemPowerDef, "AI TargetOverride",  iHide);

	iHide = 1-iHide;

	METableSetFieldNotApplicable(pTable, pItemPowerDef, "Factor Value", iHide);

}

static void iep_initCallbacks(MEWindow *pWindow, METable *pTable)
{
	// General Window callbacks
	MEWindowSetCreateCallback(pWindow, iep_windowCreateCallback);

	// General table callbacks
	METableSetValidateCallback(pTable, iep_validateCallback, pTable);
	METableSetCreateCallback(pTable, iep_tableCreateCallback);
	METableSetPostOpenCallback(pTable, iep_postOpenCallback);
	METableSetPreSaveCallback(pTable, iep_preSaveCallback);

	METableSetColumnChangeCallback(pTable, "ItemPower Category", rte_FieldChangedCallback, NULL);

	// We need this registered here instead of by METable because the dictionary will 
	// only allow each callback function to be registered once and there may be multiple
	// METable instances.  Our local callback just passes through to the METable.
	resDictRegisterEventCallback(g_hItemPowerDict, iep_dictChangeCallback, pTable);
	resDictRegisterEventCallback(gMessageDict, iep_messageDictChangeCallback, pTable);
}


//---------------------------------------------------------------------------------------------------
// UI Init
//---------------------------------------------------------------------------------------------------

static void iep_initItemPowerColumns(METable *pTable)
{
	char *pcPath = NULL;
	GameBranch_GetDirectory(&pcPath, ITEMPOWERS_BASE_DIR);
	METableAddSimpleColumn(pTable, "Name", "name", 150, NULL, kMEFieldType_TextEntry);

	// Lock in name column
	METableSetNumLockedColumns(pTable, 2);
	
	METableAddSimpleColumn(pTable,   "Display Name (noun)", ".displayNameMsg.EditorCopy",     160, IEP_GROUP_MAIN, kMEFieldType_Message);
	METableAddSimpleColumn(pTable,   "Display Name (adj)",  ".displayNameMsg2.EditorCopy",     160, IEP_GROUP_MAIN, kMEFieldType_Message);
	METableAddSimpleColumn(pTable,   "Description",  ".descriptionMsg.EditorCopy",     160, IEP_GROUP_MAIN, kMEFieldType_Message);
	
	METableAddScopeColumn(pTable,    "Scope",           "Scope",                       160, IEP_GROUP_MAIN, kMEFieldType_TextEntry); // Not validated on purpose
	METableAddFileNameColumn(pTable, "File Name",       "fileName",                    210, IEP_GROUP_MAIN, NULL, pcPath, pcPath, ".itempower", UIBrowseNewOrExisting);
	METableAddSimpleColumn(pTable,   "Notes",           "notes",                       150, IEP_GROUP_MAIN, kMEFieldType_MultiText);
	
	METableAddSimpleColumn(pTable,   "Icon",         "icon",						   180, IEP_GROUP_MAIN, kMEFieldType_Texture);
	
	METableAddEnumColumn(pTable,       "Flags",           "flags",                     140, IEP_GROUP_POWER, kMEFieldType_FlagCombo, ItemPowerFlagEnum);
	METableAddGlobalDictColumn(pTable, "Power",			  "power",                     140, IEP_GROUP_POWER, kMEFieldType_ValidatedTextEntry, "PowerDef", "resourceName");
	METableAddGlobalDictColumn(pTable, "PowerReplace",    "PowerReplace",              140, IEP_GROUP_POWER, kMEFieldType_ValidatedTextEntry, "PowerReplaceDef", "resourceName");
	METableAddEnumColumn(pTable,       "ItemPower Category",  "ItemPowerCategory",     140, IEP_GROUP_POWER, kMEFieldType_FlagCombo, ItemPowerCategoryEnum);
	METableAddEnumColumn(pTable,       "ItemPower Art Category",  "ArtCategory",     140, IEP_GROUP_POWER, kMEFieldType_Combo, ItemPowerArtCategoryEnum);

	METableAddSimpleColumn(pTable, "Point-Buy Cost",    "PointBuyCost",              140, IEP_GROUP_MAIN, kMEFieldType_TextEntry);
	METableAddSimpleColumn(pTable, "Factor Value",    "FactorValue",              140, IEP_GROUP_MAIN, kMEFieldType_TextEntry);

	METableAddExprColumn(pTable,       "Economy Points",  "ExprEconomyPoints",         140, IEP_GROUP_MAIN, g_pItemContext);
	METableAddGlobalDictColumn(pTable, "Craft Recipe",    "CraftRecipe",               140, IEP_GROUP_MAIN, kMEFieldType_ValidatedTextEntry, "ItemDef", "resourceName");
	METableAddGlobalDictColumn(pTable, "Value Recipe",    "ValueRecipe",               140, IEP_GROUP_MAIN, kMEFieldType_ValidatedTextEntry, "ItemDef", "resourceName");
	
	METableAddSimpleColumn(pTable,   "Min Level",       ".Restriction.MinLevel",       100, IEP_SUBGROUP_RESTRICTION, kMEFieldType_TextEntry);
	METableAddSimpleColumn(pTable,   "Max Level",       ".Restriction.MaxLevel",       100, IEP_SUBGROUP_RESTRICTION, kMEFieldType_TextEntry);
	METableAddExprColumn(pTable,     "Requires Expr",   ".Restriction.pRequiresBlock", 120, IEP_SUBGROUP_RESTRICTION, NULL);
	METableAddEnumColumn(pTable,	 "Slotted Gem Type Required", ".Restriction.RequiredGemSlotType", 120, IEP_SUBGROUP_RESTRICTION, kMEFieldType_Combo, ItemGemTypeEnum);
	
	METableAddExprColumn(pTable,    "AI Requires",       ".PowerConfig.AIRequires",          120, IEP_GROUP_AI, NULL);
	METableAddExprColumn(pTable,    "AI End Condition",  ".PowerConfig.AIEndCondition",      120, IEP_GROUP_AI, NULL);
	METableAddSimpleColumn(pTable,  "AI Min Range",      ".PowerConfig.AIPreferredMinRange", 100, IEP_GROUP_AI, kMEFieldType_TextEntry);
	METableAddSimpleColumn(pTable,  "AI Max Range",      ".PowerConfig.AIPreferredMaxRange", 100, IEP_GROUP_AI, kMEFieldType_TextEntry);
	METableAddSimpleColumn(pTable,  "AI Weight",         ".PowerConfig.AIWeight",            100, IEP_GROUP_AI, kMEFieldType_TextEntry);
	METableAddExprColumn(pTable,    "AI Weight Modifier",".PowerConfig.AIWeightModifier",    100, IEP_GROUP_AI, NULL);
	METableAddSimpleColumn(pTable,  "AI Chain Target",   ".PowerConfig.AIChainTarget",       100, IEP_GROUP_AI, kMEFieldType_TextEntry);
	METableAddSimpleColumn(pTable,  "AI Chain Time",     ".PowerConfig.AIChainTime",         100, IEP_GROUP_AI, kMEFieldType_TextEntry);
	METableAddExprColumn(pTable,    "AI Chain Requires", ".PowerConfig.AIChainRequires",     120, IEP_GROUP_AI, NULL);
	METableAddExprColumn(pTable,    "AI TargetOverride", ".PowerConfig.AITargetOverride",    120, IEP_GROUP_AI, NULL);
	estrDestroy(&pcPath);
}


static void iep_init(MultiEditEMDoc *pEditorDoc)
{
	if (!iepWindow) {
		// Create the editor window
		iepWindow = MEWindowCreate("Item Power Editor", "Item Power", "Item Powers", SEARCH_TYPE_ITEMPOWER, g_hItemPowerDict, parse_ItemPowerDef, "name", "filename", "scope", pEditorDoc);

		// Add item-specific columns
		iep_initItemPowerColumns(iepWindow->pTable);
		METableFinishColumns(iepWindow->pTable);

		// Init the menus after adding all the columns
		MEWindowInitTableMenus(iepWindow);

		// Set the callbacks
		iep_initCallbacks(iepWindow, iepWindow->pTable);
	}

	// Show the window
	ui_WindowPresent(iepWindow->pUIWindow);
}


//---------------------------------------------------------------------------------------------------
// Public Interface
//---------------------------------------------------------------------------------------------------

MEWindow *itemPowerEditor_init(MultiEditEMDoc *pEditorDoc) 
{
	iep_init(pEditorDoc);

	return iepWindow;
}


void itemPowerEditor_createItemPower(char *pcName)
{
	// Create a new object since it is not in the dictionary
	// Add the object as a new object with no old
	void *pObject = iep_createObject(iepWindow->pTable, NULL, pcName, false, true);
	METableAddRowByObject(iepWindow->pTable, pObject, 1, 1);
}

void itemPowerEditor_createItemPowerFromPowerDef(char *pcName, PowerDef* pDef, const char* pScope)
{
	// Create a new object since it is not in the dictionary
	// Add the object as a new object with no old
	void *pObject = iep_createObject(iepWindow->pTable, NULL, pcName, false, false);
	SET_HANDLE_FROM_STRING(g_hPowerDefDict, pDef->pchName, ((ItemPowerDef*)pObject)->hPower);
	((ItemPowerDef*)pObject)->pchScope = (char*)allocAddString(pScope);
	
	METableAddRowByObject(iepWindow->pTable, pObject, 1, 1);
}

#endif