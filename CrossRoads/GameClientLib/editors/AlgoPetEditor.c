/***************************************************************************
*     Copyright (c) 2006-2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#ifndef NO_EDITORS

#include "AlgoPetEditor.h"
#include "AlgoPet.h"
#include "EString.h"
#include "PowerTree.h"
#include "ResourceSearch.h"
#include "StringCache.h"
#include "ItemEnums.h"
#include "itemEnums_h_ast.h"

#include "MultiEditTable.h"
#include "MultiEditWindow.h"

#include "AutoGen/AlgoPet_h_ast.h"
#include "AutoGen/PowerTree_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

static MEWindow *apWindow = NULL;

#define AP_GROUP_MAIN        "Main"

static int apPowerQualityid = 0;
static int apPowerDefid = 0;
static int apPowerChoiceid = 0;
static int apUniformOverlayid = 0;

static void *ap_createObject(METable *pTable, AlgoPetDef *pObjectToClone, char *pcNewName, bool bCloneKeepsKeys)
{
	AlgoPetDef *pNewDef = NULL;
	const char *pcBaseName;
	char *tmpS = NULL;

	estrStackCreate(&tmpS);

	// Create the object
	if (pObjectToClone) {
		pNewDef = StructClone(parse_AlgoPetDef, pObjectToClone);
		pcBaseName = pObjectToClone->pchName;
	} else {
		pNewDef = StructCreate(parse_AlgoPetDef);

		pcBaseName = "_New_AlgoPet";
	}
	// Use provided name if available
	if (pcNewName) {
		pcBaseName = pcNewName;
	}

	assertmsg(pNewDef, "Failed to create Algo Pet");

	// Assign a new name
	pNewDef->pchName = (char*)METableMakeNewNameShared(pTable, pcBaseName, true);

	// Assign a file
	estrPrintf(&tmpS, "defs/items/algopets/%s.algopet",pNewDef->pchName);
	pNewDef->pchFileName = (char*)allocAddString(tmpS);

	estrDestroy(&tmpS);

	return pNewDef;
}

static void *ap_windowCreateCallback(MEWindow *pWindow, AlgoPetDef *pObjectToClone)
{
	return ap_createObject(pWindow->pTable, pObjectToClone, NULL, false);
}

static int ap_validateCallback(METable *pTable, AlgoPetDef *pAlgoDef, void *pUserData)
{
	char buf[1024];

	if (pAlgoDef->pchName[0] == '_') {
		sprintf(buf, "The Algo Pet '%s' cannot have a name starting with an underscore.", pAlgoDef->pchName);
		ui_DialogPopup("Validation Error", buf);
		return 0;
	}

	return algoPetDef_Validate(pAlgoDef);
}

static void ap_postOpenCallback(METable *pTable, AlgoPetDef *pAlgoDef, AlgoPetDef *pOrigAlgoDef)
{
	//Fill in editor fields
	int i,j;

	if(pOrigAlgoDef)
	{
		if(pOrigAlgoDef->ppPowerChoices)
			return;

		for(i=0;i<eaSize(&pAlgoDef->ppPowerQuality);i++)
		{
			for(j=0;j<eaSize(&pAlgoDef->ppPowerQuality[i]->ppChoices);j++)
			{
				AlgoPetEditorPowerChoice *pOrigEditorChoice = StructCreate(parse_AlgoPetEditorPowerChoice);
				AlgoPetEditorPowerChoice *pNewEditorChoice = NULL;

				pOrigEditorChoice->uiRarity = pAlgoDef->ppPowerQuality[i]->uiRarity;
				ea32Copy(&pOrigEditorChoice->puiSharedCategories,&pAlgoDef->ppPowerQuality[i]->puiSharedCategories);
				ea32Copy(&pOrigEditorChoice->puiCategory,&pAlgoDef->ppPowerQuality[i]->ppChoices[j]->puiCategory);

				eaPush(&pOrigAlgoDef->ppPowerChoices,pOrigEditorChoice);

				pNewEditorChoice = StructClone(parse_AlgoPetEditorPowerChoice,pOrigEditorChoice);

				eaPush(&pAlgoDef->ppPowerChoices,pNewEditorChoice);
			}
		}

		if(pOrigAlgoDef->ppPowerChoices)
			METableRegenerateRow(pTable,pAlgoDef);
	}

	METableRefreshRow(pTable,pAlgoDef);
}

static void ap_preSaveCallback(METable *pTable, AlgoPetDef *pAlgoDef)
{
	//Copy out editor fields to actual fields
	int i,j;

	eaDestroyStruct(&pAlgoDef->ppPowerQuality,parse_AlgoPetPowerQuality);

	for(i=0;i<eaSize(&pAlgoDef->ppPowerChoices);i++)
	{
		AlgoPetPowerQuality *pQuality = NULL;
		AlgoPetPowerChoice *pChoice = NULL;

		for(j=0;j<eaSize(&pAlgoDef->ppPowerQuality);j++)
		{
			if(pAlgoDef->ppPowerQuality[j]->uiRarity == pAlgoDef->ppPowerChoices[i]->uiRarity)
			{
				pQuality = pAlgoDef->ppPowerQuality[j];
				break;
			}
		}

		if(!pQuality)
		{
			pQuality = StructCreate(parse_AlgoPetPowerQuality);
			pQuality->uiRarity = pAlgoDef->ppPowerChoices[i]->uiRarity;
			ea32Copy(&pQuality->puiSharedCategories,&pAlgoDef->ppPowerChoices[i]->puiSharedCategories);

			eaPush(&pAlgoDef->ppPowerQuality,pQuality);
		}

		pChoice = StructCreate(parse_AlgoPetPowerChoice);
		ea32Copy(&pChoice->puiCategory,&pAlgoDef->ppPowerChoices[i]->puiCategory);

		eaPush(&pQuality->ppChoices,pChoice);
	}
}

static void *ap_tableCreateCallback(METable *pTable, AlgoPetDef *pObjectToClone, bool bCloneKeepsKeys)
{
	return ap_createObject(pTable, pObjectToClone, NULL, bCloneKeepsKeys);
}

static void ap_dictChangeCallback(enumResourceEventType eType, const char *pDictName, ConstReferenceData pRefData, Referent pReferent, METable *pTable)
{
	METableDictChanged(pTable, eType, pReferent, pRefData);
}

static void ap_rarityFieldChangeCallback(METable *pTable, AlgoPetDef *pAlgoPet, AlgoPetEditorPowerChoice *pRarity, void *pUserData, bool bInitNotify)
{
	ItemQuality eQuality = pRarity->uiRarity;
	int i;

	if(bInitNotify)
		return;

	for(i=0;i<eaSize(&pAlgoPet->ppPowerChoices);i++)
	{
		if(pAlgoPet->ppPowerChoices[i]->uiRarity == eQuality
			&& pAlgoPet->ppPowerChoices[i] != pRarity)
		{
			ea32Copy(&pAlgoPet->ppPowerChoices[i]->puiSharedCategories,&pRarity->puiSharedCategories);
		}
	}
}

static void ap_rarityChangeCallback(METable *pTable, AlgoPetDef *pAlgoPet, AlgoPetEditorPowerChoice *pRarity, void *pUserData, bool bInitNotify)
{
	ItemQuality eQuality = pRarity->uiRarity;
	int i;

	if(bInitNotify)
		return;

	for(i=0;i<eaSize(&pAlgoPet->ppPowerChoices);i++)
	{
		if(pAlgoPet->ppPowerChoices[i]->uiRarity == eQuality
			&& pAlgoPet->ppPowerChoices[i] != pRarity)
		{
			ea32Copy(&pRarity->puiSharedCategories,&pAlgoPet->ppPowerChoices[i]->puiSharedCategories);
			break;
		}
	}
}


//---------------------------------------------------------------------------------------------------
// UI Init
//---------------------------------------------------------------------------------------------------

static void ap_initColumns(METable *pTable)
{
	METableAddSimpleColumn(pTable, "Name", "Name", 150, NULL, kMEFieldType_TextEntry);

	// Lock in name column
	METableSetNumLockedColumns(pTable, 2);

	METableAddScopeColumn(pTable,   "Scope",        "Scope",       160, AP_GROUP_MAIN, kMEFieldType_TextEntry); // Not validated on purpose
	METableAddFileNameColumn(pTable, "File Name",    "fileName",    210, AP_GROUP_MAIN, NULL, "defs/items/algopets", "defs/items/algopets", ".algopet", UIBrowseNewOrExisting);
	METableAddGlobalDictColumn(pTable,"Species", "Species", 160, AP_GROUP_MAIN,kMEFieldType_FlagCombo,"Species","resourceName");
}

static void ap_initCallbacks(MEWindow *pWindow, METable *pTable)
{
	// General Window callbacks
	MEWindowSetCreateCallback(pWindow, ap_windowCreateCallback);

	// General table callbacks
	METableSetValidateCallback(pTable, ap_validateCallback, pTable);
	METableSetPostOpenCallback(pTable, ap_postOpenCallback);
	METableSetPreSaveCallback(pTable, ap_preSaveCallback);
	METableSetCreateCallback(pTable, ap_tableCreateCallback);

	METableSetSubColumnChangeCallback(pTable, apPowerChoiceid, "Shared Categories", ap_rarityFieldChangeCallback, NULL);
	METableSetSubColumnChangeCallback(pTable, apPowerChoiceid, "Rarity", ap_rarityChangeCallback, NULL);


	// We need this registered here instead of by METable because the dictionary will 
	// only allow each callback function to be registered once and there may be multiple
	// METable instances.  Our local callback just passes through to the METable.
	resDictRegisterEventCallback(g_hAlgoPetDict, ap_dictChangeCallback, pTable);
}

static void *ap_createPowerChoiceEditor(METable *pTable, AlgoPetDef *pAlgoDef, AlgoPetEditorPowerChoice *pToClone, AlgoPetEditorPowerChoice *pBefore, AlgoPetEditorPowerChoice *pAfter)
{
	AlgoPetEditorPowerChoice *pNew = NULL;

	if(pToClone) {
		pNew = StructClone(parse_AlgoPetEditorPowerChoice, pToClone);
	} else {
		pNew = StructCreate(parse_AlgoPetEditorPowerChoice);
	}

	if(!pNew)
		return NULL;

	return pNew;
}

static void *ap_createPowerQuality(METable *pTable, AlgoPetDef *pAlgoDef, AlgoPetPowerQuality *pToClone, AlgoPetPowerQuality *pBefore, AlgoPetPowerQuality *pAfter)
{
	AlgoPetPowerQuality *pNewQuality = NULL;

	if(pToClone) {
		pNewQuality = StructClone(parse_AlgoPetPowerQuality, pToClone);
	} else {
		pNewQuality = StructCreate(parse_AlgoPetPowerQuality);
	}

	if(!pNewQuality)
		return NULL;

	return pNewQuality;
}

static void *ap_createUniformOverlay(METable *pTable, AlgoPetDef *pAlgoDef, CostumeRefForAlgoPet *pToClone, CostumeRefForAlgoPet *pBefore, CostumeRefForAlgoPet *pAfter)
{
	CostumeRefForAlgoPet *pNew = NULL;

	if(pToClone)
		pNew = StructClone(parse_CostumeRefForAlgoPet,pToClone);
	else
		pNew = StructCreate(parse_CostumeRefForAlgoPet);

	if(!pNew)
		return NULL;

	return pNew;
}


static void *ap_createPowerDef(METable *pTable, AlgoPetDef *pAlgoDef, AlgoPetPowerDef *pToClone, AlgoPetPowerDef *pBefore, AlgoPetPowerDef *pAfter)
{
	AlgoPetPowerDef *pNewPowerDef = NULL;

	if(pToClone) {
		pNewPowerDef = StructClone(parse_AlgoPetPowerDef, pToClone);
	} else {
		pNewPowerDef = StructCreate(parse_AlgoPetPowerDef);
	}

	if(!pNewPowerDef)
		return NULL;

	return pNewPowerDef;
}

static void ap_initUniformOverlayColumns(METable *pTable)
{
	int id;

	apUniformOverlayid = id = METableCreateSubTable(pTable,"Uniform Overlay","UniformOverlay",parse_CostumeRefForAlgoPet,
													NULL, NULL, NULL, ap_createUniformOverlay);

	METableAddSimpleSubColumn(pTable, id, "Uniform Overlay", NULL, 80, NULL, 0);
	METableSetSubColumnState(pTable, id, "Uniform Overlay", ME_STATE_LABEL);

	METableSetNumLockedSubColumns(pTable, id, 2);

	METableAddGlobalDictSubColumn(pTable,id, "Costume", "hPlayerCostume", 160, "Uniform",kMEFieldType_ValidatedTextEntry,"PlayerCostume","resourceName");
	METableAddSimpleSubColumn(pTable,id,"Weight","Weight",100,"Uniform",kMEFieldType_TextEntry);
}

static void ap_initPowerDefColumns(METable *pTable)
{
	int id;

	apPowerDefid = id = METableCreateSubTable(pTable, "Power", "Power", parse_AlgoPetPowerDef, 
												NULL, NULL, NULL, ap_createPowerDef);

	METableAddSimpleSubColumn(pTable, id, "Power", NULL, 80, NULL, 0);
	METableSetSubColumnState(pTable, id, "Power", ME_STATE_LABEL);

	METableSetNumLockedSubColumns(pTable, id, 2);

	
	//METableAddGlobalDictSubColumn(pTable, id, "Power Node","PowerNode",160,"Power",kMEFieldType_ValidatedTextEntry,"PowerTreeNodeDef","resourceName");
	METableAddDictSubColumn(pTable, id, "Power Node", "PowerNode", 160, "Power", kMEFieldType_ValidatedTextEntry, "PowerTreeNodeDef", parse_PTNodeDef, "NameFull");
	METableAddEnumSubColumn(pTable,id,"Categories","Category",100,"Power",kMEFieldType_FlagCombo,AlgoCategoryEnum);
	METableAddEnumSubColumn(pTable,id,"Exclusive Categories","ExclusiveCategory",100,"Power",kMEFieldType_FlagCombo,AlgoCategoryEnum);
	METableAddSimpleSubColumn(pTable,id,"Weight","Weight",60,"Power",kMEFieldType_TextEntry);
	METableAddExprSubColumn(pTable,id,"Weight Multiplier","ExprWeightMulti",160,"Power",algoPetGetContext());
}

static void ap_initPowerChoiceColumns(METable *pTable)
{
	int id;

	apPowerChoiceid = id = METableCreateSubTable(pTable, "Power Choice", "PowerChoices", parse_AlgoPetEditorPowerChoice,
												NULL, NULL, NULL, ap_createPowerChoiceEditor);

	METableAddSimpleSubColumn(pTable, id, "PowerChoice", NULL, 80, NULL, 0);
	METableSetSubColumnState(pTable, id, "PowerChoice", ME_STATE_LABEL);

	METableSetNumLockedSubColumns(pTable, id, 2);

	METableAddEnumSubColumn(pTable, id, "Rarity", "Rarity", 100, "PowerChoice", kMEFieldType_Combo, ItemQualityEnum);
	METableAddEnumSubColumn(pTable, id, "Shared Categories", "SharedCategory",  160, "PowerChoice", kMEFieldType_TextEntry, AlgoCategoryEnum);
	METableAddEnumSubColumn(pTable, id, "Categories", "Category", 160, "PowerChoice", kMEFieldType_FlagCombo, AlgoCategoryEnum);
	
}

static void ap_init(MultiEditEMDoc *pEditorDoc)
{

	if (!apWindow) {
		// Create the editor window
		apWindow = MEWindowCreate("Algo Pet Editor", "Algo Pet", "Algo Pets", SEARCH_TYPE_ALGOPET, g_hAlgoPetDict, parse_AlgoPetDef, "name", "filename", "scope", pEditorDoc);

		// Add algo-pet specific columns
		ap_initColumns(apWindow->pTable);

		// Add algo-pet specific sub-columns
		ap_initPowerDefColumns(apWindow->pTable);
		ap_initPowerChoiceColumns(apWindow->pTable);
		ap_initUniformOverlayColumns(apWindow->pTable);
		

		METableFinishColumns(apWindow->pTable);

		// Init the menus after adding all the columns
		MEWindowInitTableMenus(apWindow);

		// Set the callbacks
		ap_initCallbacks(apWindow, apWindow->pTable);
	}

	// Show the window
	ui_WindowPresent(apWindow->pUIWindow);
}

//---------------------------------------------------------------------------------------------------
// Public Interface
//---------------------------------------------------------------------------------------------------

MEWindow *algoPetEditor_init(MultiEditEMDoc *pEditorDoc) 
{
	ap_init(pEditorDoc);	
	return apWindow;
}


void algoPetEditor_createAlgoPet(char *pcName)
{
	// Create a new object since it is not in the dictionary
	// Add the object as a new object with no old
	void *pObject = ap_createObject(apWindow->pTable, NULL, pcName, false);
	METableAddRowByObject(apWindow->pTable, pObject, 1, 1);
}

#endif