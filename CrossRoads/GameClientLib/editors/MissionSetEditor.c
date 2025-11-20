//
// MissionSetEditor.c
//

#ifndef NO_EDITORS

#include "MultiEditTable.h"
#include "MultiEditWindow.h"
#include "ResourceSearch.h"
#include "missionset_common.h"
#include "StringCache.h"
#include "EString.h"

#include "AutoGen/missionset_common_h_ast.h"

// This magic line is required if you run in Full Debug to avoid assert
AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

#define MSE_GROUP_MAIN        "Main"

//-----------------------------------------------------------------------------------
// Global Data
//-----------------------------------------------------------------------------------

static MEWindow *mseWindow = NULL;
static int mseInventoryId = 0;


//---------------------------------------------------------------------------------------------------
// Callbacks
//---------------------------------------------------------------------------------------------------


static int mse_validateCallback(METable *pTable, MissionSet *pSet, void *pUserData)
{
	return missionset_Validate(pSet);
}


static void mse_fixMessages(MissionSet *pSet)
{
	// no messages yet, but I'll keep this in in case we add some since this is already
	// called in the right places
}

static void mse_postOpenCallback(METable *pTable, MissionSet *pSet, MissionSet *pOrigSet)
{
	mse_fixMessages(pSet);
	if (pOrigSet) {
		mse_fixMessages(pOrigSet);
	}
}

static void mse_preSaveCallback(METable *pTable, MissionSet *pSet)
{
	mse_fixMessages(pSet);
}

static void *mse_createEntry(METable *pTable, MissionSet *pSet, MissionSetEntry *pEntryToClone, MissionSetEntry *pBeforeEntry, MissionSetEntry *pAfterEntry)
{
	MissionSetEntry *pNewEntry;

	// Allocate the object
	if (pEntryToClone) {
		pNewEntry = StructClone(parse_MissionSetEntry, pEntryToClone);
	} else {
		pNewEntry = StructCreate(parse_MissionSetEntry);
	}

	return pNewEntry;
}


static void* mse_createObject(METable *pTable, MissionSet *pObjectToClone, char *pcNewName, bool bCloneKeepsKeys)
{
	MissionSet *pNewSet = NULL;
	char buf[128];
	const char *pcBaseName;

	// Create the object
	if (pObjectToClone) {
		pNewSet = StructClone(parse_MissionSet, pObjectToClone);
		pcBaseName = pObjectToClone->pchName;
	} else {
		pNewSet = StructCreate(parse_MissionSet);

		pcBaseName = "_New_MissionSet";
	}
	// Use provided name if available
	if (pcNewName) {
		pcBaseName = pcNewName;
	}

	assertmsg(pNewSet, "Failed to create MissionSet");

	// Assign a new name
	pNewSet->pchName = (char*)METableMakeNewNameShared(pTable, pcBaseName, true);

	// Assign a file
	sprintf(buf,"%s/%s.%s", MISSIONSET_BASE_DIR, pNewSet->pchName, MISSIONSET_EXTENSION);
	pNewSet->pchFilename = (char*)allocAddString(buf);

	return pNewSet;
}


static void *mse_tableCreateCallback(METable *pTable, MissionSet *pObjectToClone, bool bCloneKeepsKeys)
{
	return mse_createObject(pTable, pObjectToClone, NULL, bCloneKeepsKeys);
}


static void *mse_windowCreateCallback(MEWindow *pWindow, MissionSet *pObjectToClone)
{
	return mse_createObject(pWindow->pTable, pObjectToClone, NULL, false);
}


static void mse_dictChangeCallback(enumResourceEventType eType, const char *pDictName, ConstReferenceData pRefData, Referent pReferent, METable *pTable)
{
	METableDictChanged(pTable, eType, pReferent, pRefData);
}


static void mse_initCallbacks(MEWindow *pWindow, METable *pTable)
{
	// General Window callbacks
	MEWindowSetCreateCallback(pWindow, mse_windowCreateCallback);

	// General table callbacks
	METableSetValidateCallback(pTable, mse_validateCallback, pTable);
	METableSetCreateCallback(pTable, mse_tableCreateCallback);
	METableSetPostOpenCallback(pTable, mse_postOpenCallback);
	METableSetPreSaveCallback(pTable, mse_preSaveCallback);

	// We need this registered here instead of by METable because the dictionary will 
	// only allow each callback function to be registered once and there may be multiple
	// METable instances.  Our local callback just passes through to the METable.
	resDictRegisterEventCallback(g_MissionSetDictionary, mse_dictChangeCallback, pTable);
}


//---------------------------------------------------------------------------------------------------
// UI Init
//---------------------------------------------------------------------------------------------------

static void mse_initMissionSetColumns(METable *pTable)
{
	int id;

	METableAddSimpleColumn(pTable, "Name", "name", 150, NULL, kMEFieldType_TextEntry);
	
	// Lock in name column
	METableSetNumLockedColumns(pTable, 2);
	
	METableAddScopeColumn(pTable,      "Scope",           "Scope",          160, MSE_GROUP_MAIN, kMEFieldType_TextEntry); // Not validated on purpose
	METableAddFileNameColumn(pTable,   "File Name",       "filename",       210, MSE_GROUP_MAIN, NULL, MISSIONSET_BASE_DIR, MISSIONSET_BASE_DIR, MISSIONSET_DOTEXTENSION, UIBrowseNewOrExisting);
	METableAddSimpleColumn(pTable,     "Notes",           "notes",          150, MSE_GROUP_MAIN, kMEFieldType_MultiText);

	// Create the subtable and get the ID
	id = METableCreateSubTable(pTable, "Mission", "Entry", parse_MissionSetEntry, NULL, NULL, NULL, mse_createEntry);

	METableAddSimpleSubColumn(pTable, id, "Entry", NULL, 80, NULL, 0);
	METableSetSubColumnState(pTable, id, "Entry", ME_STATE_LABEL);
	METableAddGlobalDictSubColumn(pTable, id, "MissionDef", "MissionDef", 210, NULL, kMEFieldType_ValidatedTextEntry, "MissionDef", "resourceName");
	METableAddSimpleSubColumn(pTable, id, "Min Level", "MinLevel", 50, NULL, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id, "Max Level", "MaxLevel", 50, NULL, kMEFieldType_TextEntry);

	// Lock the label column in
	METableSetNumLockedSubColumns(pTable, id, 1);
}


static void mse_init(MultiEditEMDoc *pEditorDoc)
{
	if (!mseWindow) {
		// Create the editor window
		mseWindow = MEWindowCreate("MissionSet Editor", "MissionSet", "MissionSets", SEARCH_TYPE_MISSIONSET, g_MissionSetDictionary, parse_MissionSet, "name", "filename", "scope", pEditorDoc);

		// Add columns
		mse_initMissionSetColumns(mseWindow->pTable);
		METableFinishColumns(mseWindow->pTable);

		// Init the menus after adding all the columns
		MEWindowInitTableMenus(mseWindow);

		// Set the callbacks
		mse_initCallbacks(mseWindow, mseWindow->pTable);
	}

	// Show the window
	ui_WindowPresent(mseWindow->pUIWindow);
}


//---------------------------------------------------------------------------------------------------
// Public Interface
//---------------------------------------------------------------------------------------------------

MEWindow *missionSetEditor_init(MultiEditEMDoc *pEditorDoc) 
{
	mse_init(pEditorDoc);

	return mseWindow;
}


void missionSetEditor_createMissionSet(char *pcName)
{
	// Create a new object since it is not in the dictionary
	// Add the object as a new object with no old
	void *pObject = mse_createObject(mseWindow->pTable, NULL, pcName, false);
	METableAddRowByObject(mseWindow->pTable, pObject, 1, 1);
}

#endif