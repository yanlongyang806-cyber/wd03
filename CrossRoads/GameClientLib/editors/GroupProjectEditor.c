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
#include "PowerTree.h"

#include "AutoGen/GroupProjectCommon_h_ast.h"
#include "AutoGen/contact_common_h_ast.h"
#include "AutoGen/PowerTree_h_ast.h"

extern ParseTable parse_ItemDef[];

// This magic line is required if you run in Full Debug to avoid assert
AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

#define GPE_GROUP_MAIN			"Main"
#define GPE_SUBGROUP_CONSTANTS	"Constants"
#define GPE_SUBGROUP_CONTACTS	"Contacts"

//-----------------------------------------------------------------------------------
// Global Data
//-----------------------------------------------------------------------------------

static MEWindow *gpeWindow = NULL;
static int gpeConstants = 0;
static int gpeContacts = 0;

//---------------------------------------------------------------------------------------------------
// Callbacks
//---------------------------------------------------------------------------------------------------

static int gpe_validateCallback(METable *pTable, GroupProjectDef *pGroupProjectDef, void *pUserData)
{
	return GroupProject_Validate(pGroupProjectDef);
}


static void *gpe_createConstant(METable *pTable, GroupProjectDef *pGroupProjectDef, GroupProjectConstant *pConstantToClone, GroupProjectConstant *pBeforeConstant, GroupProjectConstant *pAfterConstant)
{
	GroupProjectConstant *pNewConstant;

	// Allocate the object
	if (pConstantToClone) {
		pNewConstant = StructClone(parse_GroupProjectConstant, pConstantToClone);
	} else {
		pNewConstant = StructCreate(parse_GroupProjectConstant);
	}

	assertmsg(pNewConstant, "Failed to create group project constant");

	return pNewConstant;
}

static void *gpe_createContact(METable *pTable, GroupProjectDef *pGroupProjectDef, GroupProjectRemoteContact *pContactToClone, GroupProjectRemoteContact *pBeforeContact, GroupProjectRemoteContact *pAfterContact)
{
	GroupProjectRemoteContact *pNewContact;

	// Allocate the object
	if (pContactToClone) {
		pNewContact = StructClone(parse_GroupProjectRemoteContact, pContactToClone);
	} else {
		pNewContact = StructCreate(parse_GroupProjectRemoteContact);
	}

	assertmsg(pNewContact, "Failed to create group project remote contact");

	return pNewContact;
}

static void gpe_fixMessages(GroupProjectDef *pGroupProjectDef)
{
	char *tmpS = NULL;
	char *tmpKeyPrefix = NULL;
	estrStackCreate(&tmpS);
	estrStackCreate(&tmpKeyPrefix);
	
	estrCopy2(&tmpKeyPrefix, "GroupProjectDef.");
	if(g_pcGameBranch)
	{
		estrConcat(&tmpKeyPrefix, g_pcGameBranch, (int)strlen(g_pcGameBranch));
		estrConcatChar(&tmpKeyPrefix, '.');
	}
	
	// Display name
	if (!pGroupProjectDef->displayNameMsg.pEditorCopy) {
		pGroupProjectDef->displayNameMsg.pEditorCopy = StructCreate(parse_Message);
	}
	
	estrPrintf(&tmpS, "%s%s.DisplayName", tmpKeyPrefix, pGroupProjectDef->name);
	if (!pGroupProjectDef->displayNameMsg.pEditorCopy->pcMessageKey || 
		(stricmp(tmpS, pGroupProjectDef->displayNameMsg.pEditorCopy->pcMessageKey) != 0)) {
		pGroupProjectDef->displayNameMsg.pEditorCopy->pcMessageKey = allocAddString(tmpS);
	}
	
	estrPrintf(&tmpS, "Display name message");
	if (!pGroupProjectDef->displayNameMsg.pEditorCopy->pcDescription ||
		(stricmp(tmpS, pGroupProjectDef->displayNameMsg.pEditorCopy->pcDescription) != 0)) {
		StructFreeString(pGroupProjectDef->displayNameMsg.pEditorCopy->pcDescription);
		pGroupProjectDef->displayNameMsg.pEditorCopy->pcDescription = StructAllocString(tmpS);
	}
	
	estrPrintf(&tmpS, "GroupProjectDef");
	if (!pGroupProjectDef->displayNameMsg.pEditorCopy->pcScope ||
		(stricmp(tmpS, pGroupProjectDef->displayNameMsg.pEditorCopy->pcScope) != 0)) {
		pGroupProjectDef->displayNameMsg.pEditorCopy->pcScope = allocAddString(tmpS);
	}

	// Description
	if (!pGroupProjectDef->descriptionMsg.pEditorCopy) {
		pGroupProjectDef->descriptionMsg.pEditorCopy = StructCreate(parse_Message);
	}

	estrPrintf(&tmpS, "%s%s.Description", tmpKeyPrefix, pGroupProjectDef->name);
	if (!pGroupProjectDef->descriptionMsg.pEditorCopy->pcMessageKey || 
		(stricmp(tmpS, pGroupProjectDef->descriptionMsg.pEditorCopy->pcMessageKey) != 0)) {
		pGroupProjectDef->descriptionMsg.pEditorCopy->pcMessageKey = allocAddString(tmpS);
	}
	
	estrPrintf(&tmpS, "Description message");
	if (!pGroupProjectDef->descriptionMsg.pEditorCopy->pcDescription ||
		(stricmp(tmpS, pGroupProjectDef->descriptionMsg.pEditorCopy->pcDescription) != 0)) {
		StructFreeString(pGroupProjectDef->descriptionMsg.pEditorCopy->pcDescription);
		pGroupProjectDef->descriptionMsg.pEditorCopy->pcDescription = StructAllocString(tmpS);
	}
	
	estrPrintf(&tmpS, "GroupProjectDef");
	if (!pGroupProjectDef->descriptionMsg.pEditorCopy->pcScope ||
		(stricmp(tmpS, pGroupProjectDef->descriptionMsg.pEditorCopy->pcScope) != 0)) {
		pGroupProjectDef->descriptionMsg.pEditorCopy->pcScope = allocAddString(tmpS);
	}

	estrDestroy(&tmpS);
	estrDestroy(&tmpKeyPrefix);
}

static void gpe_postOpenCallback(METable *pTable, GroupProjectDef *pGroupProjectDef, GroupProjectDef *pOrigGroupProjectDef)
{
	gpe_fixMessages(pGroupProjectDef);
	if (pOrigGroupProjectDef) {
		gpe_fixMessages(pOrigGroupProjectDef);
	}
}

static void gpe_preSaveCallback(METable *pTable, GroupProjectDef *pGroupProjectDef)
{
	gpe_fixMessages(pGroupProjectDef);
}

static void *gpe_createObject(METable *pTable, GroupProjectDef *pObjectToClone, char *pcNewName, bool bCloneKeepsKeys)
{
	GroupProjectDef *pNewDef = NULL;
	char buf[128];
	const char *pcBaseName;
	char *pchPath = NULL;
	Message *pDisplayMessage;
	Message *pDescriptionMessage;

	// Create the object
	if (pObjectToClone) {
		pNewDef = StructClone(parse_GroupProjectDef, pObjectToClone);
		pcBaseName = pObjectToClone->name;

		pDisplayMessage = langCreateMessage("", "", "", pObjectToClone->displayNameMsg.pEditorCopy ? pObjectToClone->displayNameMsg.pEditorCopy->pcDefaultString : NULL);
		pDescriptionMessage = langCreateMessage("", "", "", pObjectToClone->descriptionMsg.pEditorCopy ? pObjectToClone->descriptionMsg.pEditorCopy->pcDefaultString : NULL);
	} else {
		pNewDef = StructCreate(parse_GroupProjectDef);

		pcBaseName = "_New_GroupProject";

		pDisplayMessage = langCreateMessage("", "", "", NULL);
		pDescriptionMessage = langCreateMessage("", "", "", NULL);
	}
	// Use provided name if available
	if (pcNewName) {
		pcBaseName = pcNewName;
	}

	assertmsg(pNewDef, "Failed to create group project");

	// Assign a new name
	pNewDef->name = (char*)METableMakeNewNameShared(pTable, pcBaseName, true);

	// Assign a file
	sprintf(buf,
		FORMAT_OK(GameBranch_FixupPath(&pchPath, GROUP_PROJECT_BASE_DIR"/%s.%s", true, false)),
		pNewDef->name, GROUP_PROJECT_EXT);
	pNewDef->filename = (char*)allocAddString(buf);

	// Fill in messages
	pNewDef->displayNameMsg.pEditorCopy = pDisplayMessage;
	pNewDef->descriptionMsg.pEditorCopy = pDescriptionMessage;

	estrDestroy(&pchPath);
	
	return pNewDef;
}


static void *gpe_tableCreateCallback(METable *pTable, GroupProjectDef *pObjectToClone, bool bCloneKeepsKeys)
{
	return gpe_createObject(pTable, pObjectToClone, NULL, bCloneKeepsKeys);
}


static void *gpe_windowCreateCallback(MEWindow *pWindow, GroupProjectDef *pObjectToClone)
{
	return gpe_createObject(pWindow->pTable, pObjectToClone, NULL, false);
}


static void gpe_dictChangeCallback(enumResourceEventType eType, const char *pDictName, ConstReferenceData pRefData, Referent pReferent, METable *pTable)
{
	METableDictChanged(pTable, eType, pReferent, pRefData);
}

static void gpe_MessageDictChangeCallback(enumResourceEventType eType, const char *pDictName, const char *pcMessageKey, Referent pReferent, METable *pTable)
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
	MEWindowSetCreateCallback(pWindow, gpe_windowCreateCallback);

	// General table callbacks
	METableSetValidateCallback(pTable, gpe_validateCallback, pTable);
	METableSetCreateCallback(pTable, gpe_tableCreateCallback);
	METableSetPostOpenCallback(pTable, gpe_postOpenCallback);
	METableSetPreSaveCallback(pTable, gpe_preSaveCallback);

	// We need this registered here instead of by METable because the dictionary will 
	// only allow each callback function to be registered once and there may be multiple
	// METable instances.  Our local callback just passes through to the METable.
	resDictRegisterEventCallback(g_GroupProjectDict, gpe_dictChangeCallback, pTable);
	resDictRegisterEventCallback(gMessageDict, gpe_MessageDictChangeCallback, pTable);
}


//---------------------------------------------------------------------------------------------------
// UI Init
//---------------------------------------------------------------------------------------------------

static char** gpe_GetMaps(METable *pTable, void *pUnused)
{
	char **eaResult = NULL;
	S32 iIdx, iSize = eaSize(&g_GEMapDispNames);
	for(iIdx = 0; iIdx < iSize; iIdx++)
	{
		eaPush(&eaResult, strdup(g_GEMapDispNames[iIdx]));
	}
	return(eaResult);
}

static void gpe_initGroupProjectColumns(METable *pTable)
{
	char *pchPath = NULL;

	GameBranch_GetDirectory(&pchPath, GROUP_PROJECT_BASE_DIR);

	METableAddSimpleColumn(pTable, "Name", "name", 150, NULL, kMEFieldType_TextEntry);
	
	// Lock in name column
	METableSetNumLockedColumns(pTable, 2);
	
	METableAddScopeColumn(pTable, "Scope", "Scope", 160, GPE_GROUP_MAIN, kMEFieldType_TextEntry); // Not validated on purpose
	METableAddFileNameColumn(pTable, "File Name", "filename", 210, GPE_GROUP_MAIN, NULL, pchPath, pchPath, "."GROUP_PROJECT_EXT, UIBrowseNewOrExisting);
	METableAddSimpleColumn(pTable, "Display Name", ".displayNameMsg.EditorCopy", 100, GPE_GROUP_MAIN, kMEFieldType_Message);
	METableAddSimpleColumn(pTable, "Description", ".descriptionMsg.EditorCopy", 100, GPE_GROUP_MAIN, kMEFieldType_Message);
	METableAddEnumColumn(pTable, "Type", "type", 240, GPE_GROUP_MAIN, kMEFieldType_Combo, GroupProjectTypeEnum);
	METableAddDictColumn(pTable, "Unlocks", "Unlock", 240, GPE_GROUP_MAIN, kMEFieldType_ValidatedTextEntry, "GroupProjectUnlockDef", parse_GroupProjectUnlockDef, "name");
	METableAddDictColumn(pTable, "Tasks", "Task", 240, GPE_GROUP_MAIN, kMEFieldType_ValidatedTextEntry, "DonationTaskDef", parse_DonationTaskDef, "name");
	METableAddDictColumn(pTable, "Valid Numerics", "ValidNumeric", 240, GPE_GROUP_MAIN, kMEFieldType_ValidatedTextEntry, "GroupProjectNumericDef", parse_GroupProjectNumericDef, "name");
	METableAddEnumColumn(pTable, "Slot Types", "SlotType", 240, GPE_GROUP_MAIN, kMEFieldType_FlagCombo, GroupProjectTaskSlotTypeEnum);
	METableAddColumn(pTable, "Donation Maps", "DonationMap", 100, GPE_GROUP_MAIN, kMEFieldType_ValidatedTextEntry, NULL, NULL, NULL, NULL, NULL, NULL, gpe_GetMaps);
    METableAddDictColumn(pTable, "Contribution Numeric", "contributionNumeric", 240, GPE_GROUP_MAIN, kMEFieldType_ValidatedTextEntry, "ItemDef", parse_ItemDef, "name");
    METableAddDictColumn(pTable, "Lifetime Contribution Numeric", "lifetimeContributionNumeric", 240, GPE_GROUP_MAIN, kMEFieldType_ValidatedTextEntry, "ItemDef", parse_ItemDef, "name");
	METableAddColumn(pTable, "Icon", "Icon", 180, GPE_GROUP_MAIN, kMEFieldType_Texture, NULL, NULL, NULL, NULL, "texture_library/ui/Icons", NULL, NULL);
	METableAddDictColumn(pTable, "Power Trees", "PowerTree", 240, GPE_GROUP_MAIN, kMEFieldType_ValidatedTextEntry, "PowerTreeDef", parse_PowerTreeDef, "Name");

	estrDestroy(&pchPath);
}


static void gpe_initConstantColumns(METable *pTable)
{
	int id;

	// Create the subtable and get the ID
	gpeConstants = id = METableCreateSubTable(pTable, "Constant", ".constant", parse_GroupProjectConstant, NULL, NULL, NULL, gpe_createConstant);

	// Lock in label column
	METableSetNumLockedSubColumns(pTable, id, 2);

	METableAddSimpleSubColumn(pTable, id, "Key", "key", 240, GPE_SUBGROUP_CONSTANTS, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id, "Value", "value", 240, GPE_SUBGROUP_CONSTANTS, kMEFieldType_TextEntry);
}

static void gpe_initContactColumns(METable *pTable)
{
	int id;

	// Create the subtable and get the ID
	gpeContacts = id = METableCreateSubTable(pTable, "Remote Contact", ".RemoteContact", parse_GroupProjectRemoteContact, NULL, NULL, NULL, gpe_createContact);

	// Lock in label column
	METableSetNumLockedSubColumns(pTable, id, 2);

	METableAddSimpleSubColumn(pTable, id, "Key", "key", 240, GPE_SUBGROUP_CONTACTS, kMEFieldType_TextEntry);
	METableAddDictSubColumn(pTable, id, "Contact", "contactDef", 240, GPE_SUBGROUP_CONTACTS, kMEFieldType_TextEntry, "ContactDef", parse_ContactDef, "name");
	METableAddDictSubColumn(pTable, id, "Required Unlocks", "RequiredUnlock", 240, GPE_SUBGROUP_CONTACTS, kMEFieldType_TextEntry, "GroupProjectUnlockDef", parse_GroupProjectUnlockDef, "name");
}

static void gpe_init(MultiEditEMDoc *pEditorDoc)
{
	if (!gpeWindow) {
		// Create the editor window
		gpeWindow = MEWindowCreate("Group Project Editor", "GroupProject", "GroupProjects", SEARCH_TYPE_GROUPPROJECT, g_GroupProjectDict, parse_GroupProjectDef, "name", "filename", "scope", pEditorDoc);

		// Add task-specific columns
		gpe_initGroupProjectColumns(gpeWindow->pTable);
		gpe_initConstantColumns(gpeWindow->pTable);
		gpe_initContactColumns(gpeWindow->pTable);
		METableFinishColumns(gpeWindow->pTable);

		// Init the menus after adding all the columns
		MEWindowInitTableMenus(gpeWindow);

		// Set the callbacks
		gpe_initCallbacks(gpeWindow, gpeWindow->pTable);

		// Request resources
		resRequestAllResourcesInDictionary(g_DonationTaskDict);
		resRequestAllResourcesInDictionary(g_GroupProjectUnlockDict);
		resRequestAllResourcesInDictionary(g_GroupProjectNumericDict);
		resRequestAllResourcesInDictionary("ContactDef");
		resRequestAllResourcesInDictionary("PowerTreeDef");
	}

	// Show the window
	ui_WindowPresent(gpeWindow->pUIWindow);
}


//---------------------------------------------------------------------------------------------------
// Public Interface
//---------------------------------------------------------------------------------------------------

MEWindow *groupProjectEditor_init(MultiEditEMDoc *pEditorDoc) 
{
	gpe_init(pEditorDoc);

	return gpeWindow;
}

void groupProjectEditor_createGroupProject(char *pcName)
{
	// Create a new object since it is not in the dictionary
	// Add the object as a new object with no old
	void *pObject = gpe_createObject(gpeWindow->pTable, NULL, pcName, false);
	METableAddRowByObject(gpeWindow->pTable, pObject, 1, 1);
}

#endif