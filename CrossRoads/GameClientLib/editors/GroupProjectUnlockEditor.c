//
// GroupProjectEditor.c
//

#ifndef NO_EDITORS

#include "MultiEditTable.h"
#include "MultiEditWindow.h"
#include "ResourceSearch.h"
#include "gameeditorshared.h"
#include "GroupProjectCommon.h"
#include "StringCache.h"
#include "EString.h"
#include "Expression.h"
#include "GameBranch.h"

#include "AutoGen/GroupProjectCommon_h_ast.h"

// This magic line is required if you run in Full Debug to avoid assert
AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

#define GPUE_GROUP_MAIN	"Main"

//-----------------------------------------------------------------------------------
// Global Data
//-----------------------------------------------------------------------------------

static MEWindow *gpueWindow = NULL;

//---------------------------------------------------------------------------------------------------
// Callbacks
//---------------------------------------------------------------------------------------------------

static int gpue_validateCallback(METable *pTable, GroupProjectUnlockDef *pUnlockDef, void *pUserData)
{
	if (pUnlockDef->type != UnlockType_NumericValueEqualOrGreater)
	{
		pUnlockDef->triggerValue = 0;
	}
	return GroupProjectUnlock_Validate(pUnlockDef);
}

static void gpue_fixMessages(GroupProjectUnlockDef *pUnlockDef)
{
	char *tmpS = NULL;
	char *tmpKeyPrefix = NULL;
	estrStackCreate(&tmpS);
	estrStackCreate(&tmpKeyPrefix);
	
	estrCopy2(&tmpKeyPrefix, "GroupProjectUnlockDef.");
	if(g_pcGameBranch)
	{
		estrConcat(&tmpKeyPrefix, g_pcGameBranch, (int)strlen(g_pcGameBranch));
		estrConcatChar(&tmpKeyPrefix, '.');
	}
	
	// Display name
	if (!pUnlockDef->displayNameMsg.pEditorCopy) {
		pUnlockDef->displayNameMsg.pEditorCopy = StructCreate(parse_Message);
	}
	
	estrPrintf(&tmpS, "%s%s.DisplayName", tmpKeyPrefix, pUnlockDef->name);
	if (!pUnlockDef->displayNameMsg.pEditorCopy->pcMessageKey || 
		(stricmp(tmpS, pUnlockDef->displayNameMsg.pEditorCopy->pcMessageKey) != 0)) {
		pUnlockDef->displayNameMsg.pEditorCopy->pcMessageKey = allocAddString(tmpS);
	}
	
	estrPrintf(&tmpS, "Display name message");
	if (!pUnlockDef->displayNameMsg.pEditorCopy->pcDescription ||
		(stricmp(tmpS, pUnlockDef->displayNameMsg.pEditorCopy->pcDescription) != 0)) {
		StructFreeString(pUnlockDef->displayNameMsg.pEditorCopy->pcDescription);
		pUnlockDef->displayNameMsg.pEditorCopy->pcDescription = StructAllocString(tmpS);
	}
	
	estrPrintf(&tmpS, "GroupProjectUnlockDef");
	if (!pUnlockDef->displayNameMsg.pEditorCopy->pcScope ||
		(stricmp(tmpS, pUnlockDef->displayNameMsg.pEditorCopy->pcScope) != 0)) {
		pUnlockDef->displayNameMsg.pEditorCopy->pcScope = allocAddString(tmpS);
	}

	// Description
	if (!pUnlockDef->descriptionMsg.pEditorCopy) {
		pUnlockDef->descriptionMsg.pEditorCopy = StructCreate(parse_Message);
	}

	estrPrintf(&tmpS, "%s%s.Description", tmpKeyPrefix, pUnlockDef->name);
	if (!pUnlockDef->descriptionMsg.pEditorCopy->pcMessageKey || 
		(stricmp(tmpS, pUnlockDef->descriptionMsg.pEditorCopy->pcMessageKey) != 0)) {
		pUnlockDef->descriptionMsg.pEditorCopy->pcMessageKey = allocAddString(tmpS);
	}
	
	estrPrintf(&tmpS, "Description message");
	if (!pUnlockDef->descriptionMsg.pEditorCopy->pcDescription ||
		(stricmp(tmpS, pUnlockDef->descriptionMsg.pEditorCopy->pcDescription) != 0)) {
		StructFreeString(pUnlockDef->descriptionMsg.pEditorCopy->pcDescription);
		pUnlockDef->descriptionMsg.pEditorCopy->pcDescription = StructAllocString(tmpS);
	}
	
	estrPrintf(&tmpS, "GroupProjectUnlockDef");
	if (!pUnlockDef->descriptionMsg.pEditorCopy->pcScope ||
		(stricmp(tmpS, pUnlockDef->descriptionMsg.pEditorCopy->pcScope) != 0)) {
		pUnlockDef->descriptionMsg.pEditorCopy->pcScope = allocAddString(tmpS);
	}

	estrDestroy(&tmpS);
	estrDestroy(&tmpKeyPrefix);
}

static void gpue_postOpenCallback(METable *pTable, GroupProjectUnlockDef *pUnlockDef, GroupProjectUnlockDef *pOrigUnlockDef)
{
	gpue_fixMessages(pUnlockDef);
	if (pOrigUnlockDef) {
		gpue_fixMessages(pOrigUnlockDef);
	}
}

static void gpue_preSaveCallback(METable *pTable, GroupProjectUnlockDef *pUnlockDef)
{
	gpue_fixMessages(pUnlockDef);
}

static void *gpue_createObject(METable *pTable, GroupProjectUnlockDef *pObjectToClone, char *pcNewName, bool bCloneKeepsKeys)
{
	GroupProjectUnlockDef *pNewDef = NULL;
	char buf[128];
	const char *pcBaseName;
	char *pchPath = NULL;
	Message *pDisplayMessage;
	Message *pDescriptionMessage;

	// Create the object
	if (pObjectToClone) {
		pNewDef = StructClone(parse_GroupProjectUnlockDef, pObjectToClone);
		pcBaseName = pObjectToClone->name;

		pDisplayMessage = langCreateMessage("", "", "", pObjectToClone->displayNameMsg.pEditorCopy ? pObjectToClone->displayNameMsg.pEditorCopy->pcDefaultString : NULL);
		pDescriptionMessage = langCreateMessage("", "", "", pObjectToClone->descriptionMsg.pEditorCopy ? pObjectToClone->descriptionMsg.pEditorCopy->pcDefaultString : NULL);
	} else {
		pNewDef = StructCreate(parse_GroupProjectUnlockDef);

		pcBaseName = "_New_GroupProjectUnlock";

		pDisplayMessage = langCreateMessage("", "", "", NULL);
		pDescriptionMessage = langCreateMessage("", "", "", NULL);
	}
	// Use provided name if available
	if (pcNewName) {
		pcBaseName = pcNewName;
	}

	assertmsg(pNewDef, "Failed to create group project unlock");

	// Assign a new name
	pNewDef->name = (char*)METableMakeNewNameShared(pTable, pcBaseName, true);

	// Assign a file
	sprintf(buf,
		FORMAT_OK(GameBranch_FixupPath(&pchPath, GROUP_PROJECT_UNLOCK_BASE_DIR"/%s.%s", true, false)),
		pNewDef->name, GROUP_PROJECT_UNLOCK_EXT);
	pNewDef->filename = (char*)allocAddString(buf);

	// Fill in messages
	pNewDef->displayNameMsg.pEditorCopy = pDisplayMessage;
	pNewDef->descriptionMsg.pEditorCopy = pDescriptionMessage;

	estrDestroy(&pchPath);
	
	return pNewDef;
}


static void *gpue_tableCreateCallback(METable *pTable, GroupProjectUnlockDef *pObjectToClone, bool bCloneKeepsKeys)
{
	return gpue_createObject(pTable, pObjectToClone, NULL, bCloneKeepsKeys);
}


static void *gpue_windowCreateCallback(MEWindow *pWindow, GroupProjectUnlockDef *pObjectToClone)
{
	return gpue_createObject(pWindow->pTable, pObjectToClone, NULL, false);
}

static void gpue_typeChangeCallback(METable *pTable, GroupProjectUnlockDef *pDef, void *pUserData, bool bInitNotify)
{
	METableSetFieldNotApplicable(pTable, pDef, "Trigger Value", (pDef->type != UnlockType_NumericValueEqualOrGreater));
}

static void gpue_dictChangeCallback(enumResourceEventType eType, const char *pDictName, ConstReferenceData pRefData, Referent pReferent, METable *pTable)
{
	METableDictChanged(pTable, eType, pReferent, pRefData);
}

static void gpue_MessageDictChangeCallback(enumResourceEventType eType, const char *pDictName, const char *pcMessageKey, Referent pReferent, METable *pTable)
{
	if ((eType == RESEVENT_RESOURCE_MODIFIED) ||
		(eType == RESEVENT_RESOURCE_REMOVED) ||
		(eType == RESEVENT_RESOURCE_ADDED)) {

		METableMessageChangedRefresh(pTable, pcMessageKey);
	}
}

static void gpe_initCallbacks(MEWindow *pWindow, METable *pTable)
{
	// General Window callbacks
	MEWindowSetCreateCallback(pWindow, gpue_windowCreateCallback);

	// General table callbacks
	METableSetValidateCallback(pTable, gpue_validateCallback, pTable);
	METableSetCreateCallback(pTable, gpue_tableCreateCallback);
	METableSetPostOpenCallback(pTable, gpue_postOpenCallback);
	METableSetPreSaveCallback(pTable, gpue_preSaveCallback);

	METableSetColumnChangeCallback(pTable, "Type", gpue_typeChangeCallback, NULL);

	// We need this registered here instead of by METable because the dictionary will 
	// only allow each callback function to be registered once and there may be multiple
	// METable instances.  Our local callback just passes through to the METable.
	resDictRegisterEventCallback(g_GroupProjectUnlockDict, gpue_dictChangeCallback, pTable);
	resDictRegisterEventCallback(gMessageDict, gpue_MessageDictChangeCallback, pTable);
}


//---------------------------------------------------------------------------------------------------
// UI Init
//---------------------------------------------------------------------------------------------------

static void gpue_initGroupProjectUnlockColumns(METable *pTable)
{
	char *pchPath = NULL;

	GameBranch_GetDirectory(&pchPath, GROUP_PROJECT_UNLOCK_BASE_DIR);

	METableAddSimpleColumn(pTable, "Name", "name", 150, NULL, kMEFieldType_TextEntry);
	
	// Lock in name column
	METableSetNumLockedColumns(pTable, 2);
	
	METableAddScopeColumn(pTable, "Scope", "Scope", 160, GPUE_GROUP_MAIN, kMEFieldType_TextEntry); // Not validated on purpose
	METableAddFileNameColumn(pTable, "File Name", "filename", 210, GPUE_GROUP_MAIN, NULL, pchPath, pchPath, "."GROUP_PROJECT_UNLOCK_EXT, UIBrowseNewOrExisting);
	METableAddSimpleColumn(pTable, "Display Name", ".displayNameMsg.EditorCopy", 100, GPUE_GROUP_MAIN, kMEFieldType_Message);
	METableAddSimpleColumn(pTable, "Description", ".descriptionMsg.EditorCopy", 100, GPUE_GROUP_MAIN, kMEFieldType_Message);
	METableAddSimpleColumn(pTable, "Tooltip", ".tooltipMsg.EditorCopy", 100, GPUE_GROUP_MAIN, kMEFieldType_Message);
	METableAddColumn(pTable, "Icon", "Icon", 180, GPUE_GROUP_MAIN, kMEFieldType_Texture, NULL, NULL, NULL, NULL, "texture_library/ui/Icons", NULL, NULL);
	METableAddEnumColumn(pTable, "Type", "type", 240, GPUE_GROUP_MAIN, kMEFieldType_Combo, GroupProjectUnlockTypeEnum);
	METableAddDictColumn(pTable, "Numeric", "Numeric", 240, GPUE_GROUP_MAIN, kMEFieldType_ValidatedTextEntry, "GroupProjectNumericDef", parse_GroupProjectNumericDef, "name");
	METableAddSimpleColumn(pTable, "Trigger Value", "triggerValue", 100, GPUE_GROUP_MAIN, kMEFieldType_TextEntry);
	estrDestroy(&pchPath);
}

static void gpue_init(MultiEditEMDoc *pEditorDoc)
{
	if (!gpueWindow) {
		// Create the editor window
		gpueWindow = MEWindowCreate("Group Project Unlock Editor", "GroupProjectUnlock", "GroupProjectUnlocks", SEARCH_TYPE_GP_UNLOCK, g_GroupProjectUnlockDict, parse_GroupProjectUnlockDef, "name", "filename", "scope", pEditorDoc);

		// Add unlock-specific columns
		gpue_initGroupProjectUnlockColumns(gpueWindow->pTable);
		METableFinishColumns(gpueWindow->pTable);

		// Init the menus after adding all the columns
		MEWindowInitTableMenus(gpueWindow);

		// Set the callbacks
		gpe_initCallbacks(gpueWindow, gpueWindow->pTable);

		// Request resources
		resRequestAllResourcesInDictionary(g_GroupProjectNumericDict);
	}

	// Show the window
	ui_WindowPresent(gpueWindow->pUIWindow);
}


//---------------------------------------------------------------------------------------------------
// Public Interface
//---------------------------------------------------------------------------------------------------

MEWindow *groupProjectUnlockEditor_init(MultiEditEMDoc *pEditorDoc) 
{
	gpue_init(pEditorDoc);

	return gpueWindow;
}

void groupProjectUnlockEditor_createGroupProjectUnlock(char *pcName)
{
	// Create a new object since it is not in the dictionary
	// Add the object as a new object with no old
	void *pObject = gpue_createObject(gpueWindow->pTable, NULL, pcName, false);
	METableAddRowByObject(gpueWindow->pTable, pObject, 1, 1);
}

#endif