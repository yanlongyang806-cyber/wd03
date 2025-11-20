//
// PlayerStatsEditor.c
//

#ifndef NO_EDITORS

#include "GameEvent.h"
#include "MultiEditTable.h"
#include "MultiEditWindow.h"
#include "ResourceSearch.h"
#include "playerstats_common.h"
#include "StringCache.h"
#include "EString.h"

#include "AutoGen/playerstats_common_h_ast.h"

// This magic line is required if you run in Full Debug to avoid assert
AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

#define PSE_GROUP_MAIN        "Main"
#define PSE_SUBGROUP_EVENT    "Event"

//-----------------------------------------------------------------------------------
// Global Data
//-----------------------------------------------------------------------------------

static MEWindow *pseWindow = NULL;
static int pseInventoryId = 0;


//---------------------------------------------------------------------------------------------------
// Callbacks
//---------------------------------------------------------------------------------------------------


static void pse_fixEvents(PlayerStatDef *pStatDef)
{
	int i;

	// Copy editor data to events
	eaDestroyStruct(&pStatDef->eaEvents, parse_GameEvent);
	for(i = 0; i < eaSize(&pStatDef->eaEditorData); i++)
	{
		eaPush(&pStatDef->eaEvents, StructClone(parse_GameEvent, pStatDef->eaEditorData[i]->pEvent));
	}
	eaDestroyStruct(&pStatDef->eaEditorData, parse_PlayerStatEventsEditorData);
}

static int pse_validateCallback(METable *pTable, PlayerStatDef *pStatDef, void *pUserData)
{
	return playerstatdef_Validate(pStatDef);
}


static void pse_fixMessages(PlayerStatDef *pStatDef)
{
	char *tmpS = NULL;
	estrStackCreate(&tmpS);

	// displayName message
	if (!pStatDef->displayNameMsg.pEditorCopy) {
		pStatDef->displayNameMsg.pEditorCopy = StructCreate(parse_Message);
	}
		
	estrPrintf(&tmpS, "PlayerStatDef.%s.displayname", pStatDef->pchName);
	langFixupMessage(pStatDef->displayNameMsg.pEditorCopy, tmpS, "Display Name of a PlayerStatDef", "PlayerStatDef");
	
	// description message
	if (!pStatDef->descriptionMsg.pEditorCopy) {
		pStatDef->descriptionMsg.pEditorCopy = StructCreate(parse_Message);
	}

	estrPrintf(&tmpS, "PlayerStatDef.%s.description", pStatDef->pchName);
	langFixupMessage(pStatDef->descriptionMsg.pEditorCopy, tmpS, "Description of a PlayerStatDef", "PlayerStatDef");
	
	estrDestroy(&tmpS);
}

static void pse_postOpenCallback(METable *pTable, PlayerStatDef *pStatDef, PlayerStatDef *pOrigPlayerStatDef)
{
	int i;
	pse_fixMessages(pStatDef);
	if (pOrigPlayerStatDef) {
		pse_fixMessages(pOrigPlayerStatDef);
	}


	if(pStatDef)
	{
		// Copy events to editor data
 		for(i = 0; i < eaSize(&pStatDef->eaEvents); i++)
 		{
			while(eaSize(&pStatDef->eaEditorData) <= i)
			{
				eaPush(&pStatDef->eaEditorData, StructCreate(parse_PlayerStatEventsEditorData));
			}

			if(pStatDef->eaEditorData[i]->pEvent)
				StructDestroySafe(parse_GameEvent, &pStatDef->eaEditorData[i]->pEvent);

			pStatDef->eaEditorData[i]->pEvent = StructClone(parse_GameEvent, pStatDef->eaEvents[i]);
		}
		if(pOrigPlayerStatDef && pOrigPlayerStatDef->eaEvents && !pOrigPlayerStatDef->eaEditorData && pStatDef->eaEditorData)
		{
			eaCopyStructs(&pStatDef->eaEditorData, &pOrigPlayerStatDef->eaEditorData, parse_PlayerStatEventsEditorData);
			METableRegenerateRow(pTable, pStatDef);
		}
	}

	METableRefreshRow(pTable,pStatDef);
}

static void pse_preSaveCallback(METable *pTable, PlayerStatDef *pStatDef)
{
	pse_fixEvents(pStatDef);
	pse_fixMessages(pStatDef);
}

static void *pse_createObject(METable *pTable, PlayerStatDef *pObjectToClone, char *pcNewName, bool bCloneKeepsKeys)
{
	PlayerStatDef *pNewDef = NULL;
	char buf[128];
	const char *pcBaseName;

	// Create the object
	if (pObjectToClone) {
		pNewDef = StructClone(parse_PlayerStatDef, pObjectToClone);
		pcBaseName = pObjectToClone->pchName;
	} else {
		pNewDef = StructCreate(parse_PlayerStatDef);

		pcBaseName = "_New_PlayerStatDef";
	}
	// Use provided name if available
	if (pcNewName) {
		pcBaseName = pcNewName;
	}

	assertmsg(pNewDef, "Failed to create playerstatdef");

	// Assign a new name
	pNewDef->pchName = (char*)METableMakeNewNameShared(pTable, pcBaseName, true);

	// Assign a file
	sprintf(buf,"defs/playerstats/%s.playerstat",pNewDef->pchName);
	pNewDef->pchFilename = (char*)allocAddString(buf);

	return pNewDef;
}

static void *se_createEvent(METable *pTable, PlayerStatDef *pPlayerStatDef, PlayerStatEventsEditorData *pEventToClone, PlayerStatEventsEditorData *pBeforeEvent, PlayerStatEventsEditorData *pAfterEvent)
{
	PlayerStatEventsEditorData *pNewEvent;

	// Allocate the object
	if (pEventToClone) {
		pNewEvent = (PlayerStatEventsEditorData*)StructClone(parse_PlayerStatEventsEditorData, pEventToClone);
	} else {
		pNewEvent = (PlayerStatEventsEditorData*)StructCreate(parse_PlayerStatEventsEditorData);
	}

	assertmsg(pNewEvent, "Failed to create game event");

	return pNewEvent;
}



static void *pse_tableCreateCallback(METable *pTable, PlayerStatDef *pObjectToClone, bool bCloneKeepsKeys)
{
	return pse_createObject(pTable, pObjectToClone, NULL, bCloneKeepsKeys);
}


static void *pse_windowCreateCallback(MEWindow *pWindow, PlayerStatDef *pObjectToClone)
{
	return pse_createObject(pWindow->pTable, pObjectToClone, NULL, false);
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
	resDictRegisterEventCallback(g_PlayerStatDictionary, pse_dictChangeCallback, pTable);
}


//---------------------------------------------------------------------------------------------------
// UI Init
//---------------------------------------------------------------------------------------------------

static void pse_initPlayerStatColumns(METable *pTable)
{
	METableAddSimpleColumn(pTable, "Name", "name", 150, NULL, kMEFieldType_TextEntry);
	
	// Lock in name column
	METableSetNumLockedColumns(pTable, 2);
	
	METableAddScopeColumn(pTable,      "Scope",           "Scope",          160, PSE_GROUP_MAIN, kMEFieldType_TextEntry); // Not validated on purpose
	METableAddFileNameColumn(pTable,   "File Name",       "filename",       210, PSE_GROUP_MAIN, NULL, "defs/playerstats", "defs/playerstats", ".playerstat", UIBrowseNewOrExisting);
	METableAddSimpleColumn(pTable,     "Notes",           "notes",          150, PSE_GROUP_MAIN, kMEFieldType_MultiText);
	METableAddSimpleColumn(pTable,     "Display Name",    ".displayNameMsg.EditorCopy", 160, PSE_GROUP_MAIN, kMEFieldType_Message);
	METableAddSimpleColumn(pTable,     "Description",	  ".descriptionMsg.EditorCopy", 160, PSE_GROUP_MAIN, kMEFieldType_Message);
	METableAddEnumColumn(pTable,       "Category",        "Category",       150, PSE_GROUP_MAIN, kMEFieldType_Combo, PlayerStatCategoryEnum);
	METableAddEnumColumn(pTable,	   "Tags",			  "PlayerStatTag",	150, PSE_GROUP_MAIN, kMEFieldType_FlagCombo, PlayerStatTagEnum);
	METableAddEnumColumn(pTable,       "Update Type",     "UpdateType",     150, PSE_GROUP_MAIN, kMEFieldType_Combo, PlayerStatUpdateTypeEnum);
	METableAddSimpleColumn(pTable,     "Rank",			  "rank",           150, PSE_GROUP_MAIN, kMEFieldType_TextEntry);
	METableAddSimpleColumn(pTable,     "Icon Name",		  "IconName",       150, PSE_GROUP_MAIN, kMEFieldType_Texture);
	METableAddSimpleColumn(pTable,     "Track Per-Match", "PlayerPerMatchStat", 175, PSE_GROUP_MAIN, kMEFieldType_Check);
	METableAddSimpleColumn(pTable,     "Notify Player On Change", "NotifyPlayerOnChange", 175, PSE_GROUP_MAIN, kMEFieldType_Check);
}

static void pse_initEventColumns(METable *pTable)
{
	int id;

	// Create the subtable and get the ID
	pseInventoryId = id = METableCreateSubTable(pTable, "Event", "EditorData", parse_PlayerStatEventsEditorData, NULL, NULL, NULL, se_createEvent);

	METableAddGameEventSubColumn(pTable,  id, "Event", "Event", 500, PSE_SUBGROUP_EVENT, NULL );
}


static void pse_init(MultiEditEMDoc *pEditorDoc)
{
	if (!pseWindow) {
		// Create the editor window
		pseWindow = MEWindowCreate("PlayerStat Editor", "PlayerStatDef", "PlayerStatDefs", SEARCH_TYPE_PLAYERSTAT, g_PlayerStatDictionary, parse_PlayerStatDef, "name", "filename", "scope", pEditorDoc);

		// Add columns
		pse_initPlayerStatColumns(pseWindow->pTable);
		pse_initEventColumns(pseWindow->pTable);
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

MEWindow *playerStatsEditor_init(MultiEditEMDoc *pEditorDoc) 
{
	pse_init(pEditorDoc);

	return pseWindow;
}


void playerStatsEditor_createPlayerStat(char *pcName)
{
	// Create a new object since it is not in the dictionary
	// Add the object as a new object with no old
	void *pObject = pse_createObject(pseWindow->pTable, NULL, pcName, false);
	METableAddRowByObject(pseWindow->pTable, pObject, 1, 1);
}

#endif