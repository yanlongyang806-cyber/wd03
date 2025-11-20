//
// GroupProjectNumericEditor.c
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

#define GPNE_GROUP_MAIN	"Main"

//-----------------------------------------------------------------------------------
// Global Data
//-----------------------------------------------------------------------------------

static MEWindow *gpneWindow = NULL;

//---------------------------------------------------------------------------------------------------
// Callbacks
//---------------------------------------------------------------------------------------------------

static int gpne_validateCallback(METable *pTable, GroupProjectNumericDef *pNumericDef, void *pUserData)
{
	return GroupProjectNumeric_Validate(pNumericDef);
}

static void gpne_fixMessages(GroupProjectNumericDef *pNumericDef)
{
	char *tmpS = NULL;
	char *tmpKeyPrefix = NULL;
	estrStackCreate(&tmpS);
	estrStackCreate(&tmpKeyPrefix);
	
	estrCopy2(&tmpKeyPrefix, "GroupProjectNumericDef.");
	if(g_pcGameBranch)
	{
		estrConcat(&tmpKeyPrefix, g_pcGameBranch, (int)strlen(g_pcGameBranch));
		estrConcatChar(&tmpKeyPrefix, '.');
	}
	
	// Display name
	if (!pNumericDef->displayNameMsg.pEditorCopy) {
		pNumericDef->displayNameMsg.pEditorCopy = StructCreate(parse_Message);
	}
	
	estrPrintf(&tmpS, "%s%s.DisplayName", tmpKeyPrefix, pNumericDef->name);
	if (!pNumericDef->displayNameMsg.pEditorCopy->pcMessageKey || 
		(stricmp(tmpS, pNumericDef->displayNameMsg.pEditorCopy->pcMessageKey) != 0)) {
		pNumericDef->displayNameMsg.pEditorCopy->pcMessageKey = allocAddString(tmpS);
	}
	
	estrPrintf(&tmpS, "Display name message");
	if (!pNumericDef->displayNameMsg.pEditorCopy->pcDescription ||
		(stricmp(tmpS, pNumericDef->displayNameMsg.pEditorCopy->pcDescription) != 0)) {
		StructFreeString(pNumericDef->displayNameMsg.pEditorCopy->pcDescription);
		pNumericDef->displayNameMsg.pEditorCopy->pcDescription = StructAllocString(tmpS);
	}
	
	estrPrintf(&tmpS, "GroupProjectNumericDef");
	if (!pNumericDef->displayNameMsg.pEditorCopy->pcScope ||
		(stricmp(tmpS, pNumericDef->displayNameMsg.pEditorCopy->pcScope) != 0)) {
		pNumericDef->displayNameMsg.pEditorCopy->pcScope = allocAddString(tmpS);
	}

	// Tooltip
	if (!pNumericDef->tooltipMsg.pEditorCopy) {
		pNumericDef->tooltipMsg.pEditorCopy = StructCreate(parse_Message);
	}

	estrPrintf(&tmpS, "%s%s.Tooltip", tmpKeyPrefix, pNumericDef->name);
	if (!pNumericDef->tooltipMsg.pEditorCopy->pcMessageKey || 
		(stricmp(tmpS, pNumericDef->tooltipMsg.pEditorCopy->pcMessageKey) != 0)) {
		pNumericDef->tooltipMsg.pEditorCopy->pcMessageKey = allocAddString(tmpS);
	}
	
	estrPrintf(&tmpS, "Tooltip message");
	if (!pNumericDef->tooltipMsg.pEditorCopy->pcDescription ||
		(stricmp(tmpS, pNumericDef->tooltipMsg.pEditorCopy->pcDescription) != 0)) {
		StructFreeString(pNumericDef->tooltipMsg.pEditorCopy->pcDescription);
		pNumericDef->tooltipMsg.pEditorCopy->pcDescription = StructAllocString(tmpS);
	}
	
	estrPrintf(&tmpS, "GroupProjectNumericDef");
	if (!pNumericDef->tooltipMsg.pEditorCopy->pcScope ||
		(stricmp(tmpS, pNumericDef->tooltipMsg.pEditorCopy->pcScope) != 0)) {
		pNumericDef->tooltipMsg.pEditorCopy->pcScope = allocAddString(tmpS);
	}

	estrDestroy(&tmpS);
	estrDestroy(&tmpKeyPrefix);
}

static void gpne_postOpenCallback(METable *pTable, GroupProjectNumericDef *pNumericDef, GroupProjectNumericDef *pOrigNumericDef)
{
	gpne_fixMessages(pNumericDef);
	if (pOrigNumericDef) {
		gpne_fixMessages(pOrigNumericDef);
	}
}

static void gpne_preSaveCallback(METable *pTable, GroupProjectNumericDef *pNumericDef)
{
	gpne_fixMessages(pNumericDef);
}

static void *gpne_createObject(METable *pTable, GroupProjectNumericDef *pObjectToClone, char *pcNewName, bool bCloneKeepsKeys)
{
	GroupProjectNumericDef *pNewDef = NULL;
	char buf[128];
	const char *pcBaseName;
	char *pchPath = NULL;
	Message *pDisplayMessage;
	Message *pTooltipMessage;

	// Create the object
	if (pObjectToClone) {
		pNewDef = StructClone(parse_GroupProjectNumericDef, pObjectToClone);
		pcBaseName = pObjectToClone->name;

		pDisplayMessage = langCreateMessage("", "", "", pObjectToClone->displayNameMsg.pEditorCopy ? pObjectToClone->displayNameMsg.pEditorCopy->pcDefaultString : NULL);
		pTooltipMessage = langCreateMessage("", "", "", pObjectToClone->tooltipMsg.pEditorCopy ? pObjectToClone->tooltipMsg.pEditorCopy->pcDefaultString : NULL);
	} else {
		pNewDef = StructCreate(parse_GroupProjectNumericDef);

		pcBaseName = "_New_GroupProjectNumeric";

		pDisplayMessage = langCreateMessage("", "", "", NULL);
		pTooltipMessage = langCreateMessage("", "", "", NULL);
	}
	// Use provided name if available
	if (pcNewName) {
		pcBaseName = pcNewName;
	}

	assertmsg(pNewDef, "Failed to create group project numeric");

	// Assign a new name
	pNewDef->name = (char*)METableMakeNewNameShared(pTable, pcBaseName, true);

	// Assign a file
	sprintf(buf,
		FORMAT_OK(GameBranch_FixupPath(&pchPath, GROUP_PROJECT_NUMERIC_BASE_DIR"/%s.%s", true, false)),
		pNewDef->name, GROUP_PROJECT_NUMERIC_EXT);
	pNewDef->filename = (char*)allocAddString(buf);

	// Fill in messages
	pNewDef->displayNameMsg.pEditorCopy = pDisplayMessage;
	pNewDef->tooltipMsg.pEditorCopy = pTooltipMessage;

	estrDestroy(&pchPath);
	
	return pNewDef;
}


static void *gpne_tableCreateCallback(METable *pTable, GroupProjectNumericDef *pObjectToClone, bool bCloneKeepsKeys)
{
	return gpne_createObject(pTable, pObjectToClone, NULL, bCloneKeepsKeys);
}


static void *gpne_windowCreateCallback(MEWindow *pWindow, GroupProjectNumericDef *pObjectToClone)
{
	return gpne_createObject(pWindow->pTable, pObjectToClone, NULL, false);
}


static void gpne_dictChangeCallback(enumResourceEventType eType, const char *pDictName, ConstReferenceData pRefData, Referent pReferent, METable *pTable)
{
	METableDictChanged(pTable, eType, pReferent, pRefData);
}

static void gpne_MessageDictChangeCallback(enumResourceEventType eType, const char *pDictName, const char *pcMessageKey, Referent pReferent, METable *pTable)
{
	if ((eType == RESEVENT_RESOURCE_MODIFIED) ||
		(eType == RESEVENT_RESOURCE_REMOVED) ||
		(eType == RESEVENT_RESOURCE_ADDED)) {

		METableMessageChangedRefresh(pTable, pcMessageKey);
	}
}

static void gpne_initCallbacks(MEWindow *pWindow, METable *pTable)
{
	// General Window callbacks
	MEWindowSetCreateCallback(pWindow, gpne_windowCreateCallback);

	// General table callbacks
	METableSetValidateCallback(pTable, gpne_validateCallback, pTable);
	METableSetCreateCallback(pTable, gpne_tableCreateCallback);
	METableSetPostOpenCallback(pTable, gpne_postOpenCallback);
	METableSetPreSaveCallback(pTable, gpne_preSaveCallback);

	// We need this registered here instead of by METable because the dictionary will 
	// only allow each callback function to be registered once and there may be multiple
	// METable instances.  Our local callback just passes through to the METable.
	resDictRegisterEventCallback(g_GroupProjectNumericDict, gpne_dictChangeCallback, pTable);
	resDictRegisterEventCallback(gMessageDict, gpne_MessageDictChangeCallback, pTable);
}


//---------------------------------------------------------------------------------------------------
// UI Init
//---------------------------------------------------------------------------------------------------

static void gpne_initGroupProjectNumericColumns(METable *pTable)
{
	char *pchPath = NULL;

	GameBranch_GetDirectory(&pchPath, GROUP_PROJECT_NUMERIC_BASE_DIR);

	METableAddSimpleColumn(pTable, "Name", "name", 150, NULL, kMEFieldType_TextEntry);
	
	// Lock in name column
	METableSetNumLockedColumns(pTable, 2);
	
	METableAddScopeColumn(pTable, "Scope", "Scope", 160, GPNE_GROUP_MAIN, kMEFieldType_TextEntry); // Not validated on purpose
	METableAddFileNameColumn(pTable, "File Name", "filename", 210, GPNE_GROUP_MAIN, NULL, pchPath, pchPath, "."GROUP_PROJECT_NUMERIC_EXT, UIBrowseNewOrExisting);
	METableAddSimpleColumn(pTable, "Display Name", ".displayNameMsg.EditorCopy", 100, GPNE_GROUP_MAIN, kMEFieldType_Message);
	METableAddSimpleColumn(pTable, "Tooltip", ".tooltipMsg.EditorCopy", 100, GPNE_GROUP_MAIN, kMEFieldType_Message);
	METableAddColumn(pTable, "Icon", "Icon", 180, GPNE_GROUP_MAIN, kMEFieldType_Texture, NULL, NULL, NULL, NULL, "texture_library/ui/Icons", NULL, NULL);
	METableAddSimpleColumn(pTable, "Max Value", "MaxValue", 100, GPNE_GROUP_MAIN, kMEFieldType_TextEntry);
	estrDestroy(&pchPath);
}

static void gpe_init(MultiEditEMDoc *pEditorDoc)
{
	if (!gpneWindow) {
		// Create the editor window
		gpneWindow = MEWindowCreate("Group Project Numeric Editor", "GroupProjectNumeric", "GroupProjectNumerics", SEARCH_TYPE_GP_NUMERIC, g_GroupProjectNumericDict, parse_GroupProjectNumericDef, "name", "filename", "scope", pEditorDoc);

		// Add numeric-specific columns
		gpne_initGroupProjectNumericColumns(gpneWindow->pTable);
		METableFinishColumns(gpneWindow->pTable);

		// Init the menus after adding all the columns
		MEWindowInitTableMenus(gpneWindow);

		// Set the callbacks
		gpne_initCallbacks(gpneWindow, gpneWindow->pTable);
	}

	// Show the window
	ui_WindowPresent(gpneWindow->pUIWindow);
}


//---------------------------------------------------------------------------------------------------
// Public Interface
//---------------------------------------------------------------------------------------------------

MEWindow *groupProjectNumericEditor_init(MultiEditEMDoc *pEditorDoc) 
{
	gpe_init(pEditorDoc);

	return gpneWindow;
}

void groupProjectNumericEditor_createGroupProjectNumeric(char *pcName)
{
	// Create a new object since it is not in the dictionary
	// Add the object as a new object with no old
	void *pObject = gpne_createObject(gpneWindow->pTable, NULL, pcName, false);
	METableAddRowByObject(gpneWindow->pTable, pObject, 1, 1);
}

#endif