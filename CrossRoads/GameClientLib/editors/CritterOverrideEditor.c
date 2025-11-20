//
// CritterOverrideEditor.c
//

#ifndef NO_EDITORS

#include "entCritter.h"
#include "CritterOverrideEditor.h"
#include "MultiEditTable.h"
#include "MultiEditWindow.h"
#include "ResourceSearch.h"
#include "StringCache.h"

#include "AutoGen/entCritter_h_ast.h"
#include "entEnums_h_ast.h"
#include "Powers.h"

// This magic line is required if you run in Full Debug to avoid assert
AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

static int coOnDeathPowerID = 0;

#define COP_GROUP_MAIN        "Main"
#define COP_GROUP_OPTIONS     "Options"
#define COP_GROUP_ONDEATH	  "OnDeath"

//-----------------------------------------------------------------------------------
// Global Data
//-----------------------------------------------------------------------------------

static MEWindow *copWindow = NULL;


//---------------------------------------------------------------------------------------------------
// Callbacks
//---------------------------------------------------------------------------------------------------


static int cop_compareStrings(const char** left, const char** right)
{
	return stricmp(*left,*right);
}


static int cop_validateCallback(METable *pTable, CritterOverrideDef *pCritterOverrideDef, void *pUserData)
{
	return critterOverride_Validate(pCritterOverrideDef);
}

static void *co_createOnDeathPower(METable *pTable, CritterOverrideDef *pOverrideDef, ref_PowerDef *pDefToClone, ref_PowerDef *pDefBefore, ref_PowerDef *pDefAfter)
{
	ref_PowerDef *pNewDef = NULL;

	if(pDefToClone)
	{
		pNewDef = StructClone(parse_ref_PowerDef,pDefToClone);
	}
	else
	{
		pNewDef = StructCreate(parse_ref_PowerDef);
	}

	return pNewDef;
}


static void *cop_createObject(METable *pTable, CritterOverrideDef *pObjectToClone, char *pcNewName, bool bCloneKeepsKeys)
{
	CritterOverrideDef *pNewDef = NULL;
	char buf[128];
	const char *pcBaseName;
	
	// Create the object
	if (pObjectToClone) {
		pNewDef = StructClone(parse_CritterOverrideDef, pObjectToClone);
		pcBaseName = pObjectToClone->pchName;
	} else {
		pNewDef = StructCreate(parse_CritterOverrideDef);

		pcBaseName = "_New_CritterOverride";
	}
	// Use provided name if available
	if (pcNewName) {
		pcBaseName = pcNewName;
	}

	assertmsg(pNewDef, "Failed to create CritterOverride");

	// Assign a new name
	pNewDef->pchName = (char*)METableMakeNewNameShared(pTable, pcBaseName, true);

	// Assign a file
	sprintf(buf,"defs/CritterOverrides/%s.CritterOverride",pNewDef->pchName);
	pNewDef->pchFileName = (char*)allocAddString(buf);

	return pNewDef;
}

static void *cop_tableCreateCallback(METable *pTable, CritterOverrideDef *pObjectToClone, bool bCloneKeepsKeys)
{
	return cop_createObject(pTable, pObjectToClone, NULL, bCloneKeepsKeys);
}


static void *cop_windowCreateCallback(MEWindow *pWindow, CritterOverrideDef *pObjectToClone)
{
	return cop_createObject(pWindow->pTable, pObjectToClone, NULL, false);
}


static void cop_dictChangeCallback(enumResourceEventType eType, const char *pDictName, ConstReferenceData pRefData, Referent pReferent, METable *pTable)
{
	METableDictChanged(pTable, eType, pReferent, pRefData);
}


static void cop_initCallbacks(MEWindow *pWindow, METable *pTable)
{
	// General Window callbacks
	MEWindowSetCreateCallback(pWindow, cop_windowCreateCallback);

	// General table callbacks
	METableSetValidateCallback(pTable, cop_validateCallback, pTable);
	METableSetCreateCallback(pTable, cop_tableCreateCallback);

	// We need this registered here instead of by METable because the dictionary will 
	// only allow each callback function to be registered once and there may be multiple
	// METable instances.  Our local callback just passes through to the METable.
	resDictRegisterEventCallback(g_hCritterOverrideDict, cop_dictChangeCallback, pTable);
}


//---------------------------------------------------------------------------------------------------
// UI Init
//---------------------------------------------------------------------------------------------------

static void cop_initCritterOverrideColumns(METable *pTable)
{
	METableAddSimpleColumn(pTable,   "Name",         "name",        150, NULL, kMEFieldType_TextEntry);

	// Lock in name column
	METableSetNumLockedColumns(pTable, 2);

	METableAddScopeColumn(pTable,    "Scope",        "Scope",       160, COP_GROUP_MAIN, kMEFieldType_TextEntry); // Not validated on purpose
	METableAddFileNameColumn(pTable, "File Name",    "fileName",    210, COP_GROUP_MAIN, NULL, "defs/CritterOverrides", "defs/CritterOverrides", ".CritterOverride", UIBrowseNewOrExisting);
	METableAddSimpleColumn(pTable,   "Notes",		 "comment",     160, COP_GROUP_MAIN, kMEFieldType_MultiText);

	METableAddEnumColumn(pTable,     "Flags",        "flags",       140, COP_GROUP_OPTIONS, kMEFieldType_FlagCombo, kCritterOverrideFlagEnum);
	METableAddSimpleColumn(pTable,   "Mass",         "mass",        100, COP_GROUP_OPTIONS, kMEFieldType_TextEntry);
}

static void cop_initCritterOverrideOnDeathColumns(METable *pTable)
{
	int id;

	// Create the subtable and get the ID
	coOnDeathPowerID = id = METableCreateSubTable(pTable, "On Death Power", "OnDeathPower", parse_ref_PowerDef, NULL,NULL,NULL,co_createOnDeathPower);

	METableAddSimpleSubColumn(pTable, id, "OnDeathPower", NULL, 80, NULL, 0);
	METableSetSubColumnState(pTable, id, "OnDeathPower", ME_STATE_LABEL);

	METableAddGlobalDictSubColumn(pTable,id,"OnDeath Power","OnDeathPower",120,COP_GROUP_ONDEATH,kMEFieldType_ValidatedTextEntry,"PowerDef","resourceName");
	//METableAddDictSubColumn(pTable, id,"Power","OnDeathPower",120, COP_GROUP_ONDEATH, kMEFieldType_ValidatedTextEntry, "PowerDef", parse_PowerDef, "Name");
}


static void cop_init(MultiEditEMDoc *pEditorDoc)
{
	if (!copWindow) {
		// Create the editor window
		copWindow = MEWindowCreate("Critter Override Editor", "Critter Override", "Critter Overrides", SEARCH_TYPE_CRITTER_OVERRIDE, g_hCritterOverrideDict, parse_CritterOverrideDef, "name", "filename", "scope", pEditorDoc);

		// Add item-specific columns
		cop_initCritterOverrideColumns(copWindow->pTable);
		cop_initCritterOverrideOnDeathColumns(copWindow->pTable);
		METableFinishColumns(copWindow->pTable);

		// Init the menus after adding all the columns
		MEWindowInitTableMenus(copWindow);

		// Set the callbacks
		cop_initCallbacks(copWindow, copWindow->pTable);
	}

	// Show the window
	ui_WindowPresent(copWindow->pUIWindow);
}


//---------------------------------------------------------------------------------------------------
// Public Interface
//---------------------------------------------------------------------------------------------------

MEWindow *CritterOverrideEditor_init(MultiEditEMDoc *pEditorDoc) 
{
	cop_init(pEditorDoc);

	return copWindow;
}


void CritterOverrideEditor_createCritterOverride(char *pcName)
{
	// Create a new object since it is not in the dictionary
	// Add the object as a new object with no old
	void *pObject = cop_createObject(copWindow->pTable, NULL, pcName, false);
	METableAddRowByObject(copWindow->pTable, pObject, 1, 1);
}

#endif