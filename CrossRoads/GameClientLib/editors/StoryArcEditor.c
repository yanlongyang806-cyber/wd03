/***************************************************************************
*     Copyright (c) 2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef NO_EDITORS

#include "EString.h"
#include "gameeditorshared.h"
#include "mission_common.h"
#include "progression_common.h"
#include "MultiEditTable.h"
#include "MultiEditWindow.h"
#include "ResourceSearch.h"
#include "StringCache.h"

#include "AutoGen/allegiance_h_ast.h"
#include "AutoGen/progression_common_h_ast.h"

#define SA_GROUP_MAIN "Main"
#define SA_SUBGROUP_MISSION	"Mission"
#define SA_SUBGROUP_CHILDNODE "ChildNode"

//-----------------------------------------------------------------------------------
// Global Data
//-----------------------------------------------------------------------------------

static MEWindow *s_saeWindow = NULL;

static int saeProgressionMissionId = 0;
static int saeProgressionChildrenId = 0;

// This magic line is required if you run in Full Debug to avoid assert
AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

static void sae_FixMessages(GameProgressionNodeDef* pDef)
{
	char *estrTmp = NULL;

	estrStackCreate(&estrTmp);

	// Fixup display name
	if(!pDef->msgDisplayName.pEditorCopy)
	{
		pDef->msgDisplayName.pEditorCopy = StructCreate(parse_Message);
	}

	estrPrintf(&estrTmp, "GameProgressionNodeDef.%s", pDef->pchName);
	if(!pDef->msgDisplayName.pEditorCopy->pcMessageKey ||
		(stricmp(estrTmp, pDef->msgDisplayName.pEditorCopy->pcMessageKey) != 0)) {
			pDef->msgDisplayName.pEditorCopy->pcMessageKey = allocAddString(estrTmp);
	}

	estrPrintf(&estrTmp, "Display name for a game progression node definition");
	if(!pDef->msgDisplayName.pEditorCopy->pcDescription ||
		(stricmp(estrTmp, pDef->msgDisplayName.pEditorCopy->pcDescription) != 0)) {
			StructCopyString(&pDef->msgDisplayName.pEditorCopy->pcDescription, estrTmp);
	}

	estrPrintf(&estrTmp, "GameProgressionNodeDef");
	if(!pDef->msgDisplayName.pEditorCopy->pcScope ||
		(stricmp(estrTmp, pDef->msgDisplayName.pEditorCopy->pcScope) != 0)) {
			pDef->msgDisplayName.pEditorCopy->pcScope = allocAddString(estrTmp);
	}

	// Fixup summary
	if(!pDef->msgSummary.pEditorCopy)
	{
		pDef->msgSummary.pEditorCopy = StructCreate(parse_Message);
	}

	estrPrintf(&estrTmp, "GameProgressionNodeDef.%s.Summary", pDef->pchName);
	if(!pDef->msgSummary.pEditorCopy->pcMessageKey ||
		(stricmp(estrTmp, pDef->msgSummary.pEditorCopy->pcMessageKey) != 0)) {
			pDef->msgSummary.pEditorCopy->pcMessageKey = allocAddString(estrTmp);
	}

	estrPrintf(&estrTmp, "Summary for a game progression node");
	if(!pDef->msgSummary.pEditorCopy->pcDescription ||
		(stricmp(estrTmp, pDef->msgSummary.pEditorCopy->pcDescription) != 0)) {
			StructCopyString(&pDef->msgSummary.pEditorCopy->pcDescription, estrTmp);
	}

	estrPrintf(&estrTmp, "GameProgressionNodeDef");
	if(!pDef->msgSummary.pEditorCopy->pcScope ||
		(stricmp(estrTmp, pDef->msgSummary.pEditorCopy->pcScope) != 0)) {
			pDef->msgSummary.pEditorCopy->pcScope = allocAddString(estrTmp);
	}

	// Fixup teaser
	if(!pDef->msgTeaser.pEditorCopy)
	{
		pDef->msgTeaser.pEditorCopy = StructCreate(parse_Message);
	}

	estrPrintf(&estrTmp, "GameProgressionNodeDef.%s.Teaser", pDef->pchName);
	if(!pDef->msgTeaser.pEditorCopy->pcMessageKey ||
		(stricmp(estrTmp, pDef->msgTeaser.pEditorCopy->pcMessageKey) != 0)) {
			pDef->msgTeaser.pEditorCopy->pcMessageKey = allocAddString(estrTmp);
	}

	estrPrintf(&estrTmp, "Teaser for a game progression node");
	if(!pDef->msgTeaser.pEditorCopy->pcDescription ||
		(stricmp(estrTmp, pDef->msgTeaser.pEditorCopy->pcDescription) != 0)) {
			StructCopyString(&pDef->msgTeaser.pEditorCopy->pcDescription, estrTmp);
	}

	estrPrintf(&estrTmp, "GameProgressionNodeDef");
	if(!pDef->msgTeaser.pEditorCopy->pcScope ||
		(stricmp(estrTmp, pDef->msgTeaser.pEditorCopy->pcScope) != 0)) {
			pDef->msgTeaser.pEditorCopy->pcScope = allocAddString(estrTmp);
	}

	// Fixup mission messages
	if (pDef->pMissionGroupInfo)
	{
		int i;

		// Required missions
		for (i = 0; i < eaSize(&pDef->pMissionGroupInfo->eaMissions); i++)
		{
			GameProgressionMission* pProgMission = pDef->pMissionGroupInfo->eaMissions[i];

			if(!pProgMission->msgDescription.pEditorCopy)
			{
				pProgMission->msgDescription.pEditorCopy = StructCreate(parse_Message);
			}

			// Display description
			estrPrintf(&estrTmp, "GameProgressionMission.%s.%d.Description", pDef->pchName, i);
			if(!pProgMission->msgDescription.pEditorCopy->pcMessageKey ||
				(stricmp(estrTmp, pProgMission->msgDescription.pEditorCopy->pcMessageKey) != 0)) {
					pProgMission->msgDescription.pEditorCopy->pcMessageKey = allocAddString(estrTmp);
			}

			estrPrintf(&estrTmp, "Description for a game progression node mission");
			if(!pProgMission->msgDescription.pEditorCopy->pcDescription ||
				(stricmp(estrTmp, pProgMission->msgDescription.pEditorCopy->pcDescription) != 0)) {
					StructCopyString(&pProgMission->msgDescription.pEditorCopy->pcDescription, estrTmp);
			}

			estrPrintf(&estrTmp, "GameProgressionMission");
			if(!pProgMission->msgDescription.pEditorCopy->pcScope ||
				(stricmp(estrTmp, pProgMission->msgDescription.pEditorCopy->pcScope) != 0)) {
					pProgMission->msgDescription.pEditorCopy->pcScope = allocAddString(estrTmp);
			}
		}
	}
	estrDestroy(&estrTmp);
}

static void *sae_CreateObject(METable *pTable, GameProgressionNodeDef *pObjectToClone, char *pcNewName, bool bCloneKeepsKeys)
{
	GameProgressionNodeDef *pNewDef = NULL;
	const char *pcBaseName;
	char *estrTmp = NULL;

	estrStackCreate(&estrTmp);

	// Create the object
	if (pObjectToClone) {
		pNewDef = StructCreate(parse_GameProgressionNodeDef);
		
		StructCopyAll(parse_GameProgressionNodeDef,pObjectToClone,pNewDef);
		pcBaseName = pObjectToClone->pchName;
	} else {
		pNewDef = StructCreate(parse_GameProgressionNodeDef);

		pcBaseName = "_New_GameProgressionNodeDef";
	}
	// Use provided name if available
	if (pcNewName) {
		pcBaseName = pcNewName;
	}

	assertmsg(pNewDef, "Failed to create GameProgressionNodeDef");

	// Assign a new name
	pNewDef->pchName = (char*)METableMakeNewNameShared(pTable, pcBaseName, true);

	// Assign a file
	estrPrintf(&estrTmp, GAME_PROGRESSION_BASE_DIR"/%s."GAME_PROGRESSION_EXT,pNewDef->pchName);
	pNewDef->pchFilename = (char*)allocAddString(estrTmp);

	estrDestroy(&estrTmp);

	return pNewDef;
}

static void *sae_CreateMission(METable *pTable, GameProgressionMission *pMission, GameProgressionMission *pMissionToClone, GameProgressionMission *pBeforeMission, GameProgressionMission *pAfterMission)
{
	GameProgressionMission *pNewMission;

	// Allocate the object
	if (pMissionToClone) {
		pNewMission = StructClone(parse_GameProgressionMission, pMissionToClone);
	} else {
		pNewMission = StructCreate(parse_GameProgressionMission);
	}

	assertmsg(pNewMission, "Failed to create game progression mission");
	
	return pNewMission;
}

static void *sae_CreateChild(METable *pTable, GameProgressionNodeRef *pChild, GameProgressionNodeRef *pChildToClone, GameProgressionNodeRef *pBeforeChild, GameProgressionNodeRef *pAfterChild)
{
	GameProgressionNodeRef *pNewChild;

	// Allocate the object
	if (pChildToClone) {
		pNewChild = StructClone(parse_GameProgressionNodeRef, pChildToClone);
	} else {
		pNewChild = StructCreate(parse_GameProgressionNodeRef);
	}

	assertmsg(pNewChild, "Failed to create game progression node child");
	
	return pNewChild;
}

static char** sae_GetMaps(METable *pTable, void *pUnused)
{
	char **eaResult = NULL;
	S32 iIdx, iSize = eaSize(&g_GEMapDispNames);
	for(iIdx = 0; iIdx < iSize; iIdx++)
	{
		eaPush(&eaResult, strdup(g_GEMapDispNames[iIdx]));
	}
	return eaResult;
}

static void sae_InitColumns(METable *pTable)
{
	METableAddSimpleColumn(pTable, "Name", "name", 150, NULL, kMEFieldType_TextEntry);

	// Lock in name column
	METableSetNumLockedColumns(pTable, 2);

	// Basic Fields
	METableAddSimpleColumn(pTable, "Display Name", ".DisplayName.EditorCopy", 160, SA_GROUP_MAIN, kMEFieldType_Message);
	METableAddSimpleColumn(pTable, "Summary", ".Summary.EditorCopy", 160, SA_GROUP_MAIN, kMEFieldType_Message);
	METableAddSimpleColumn(pTable, "Teaser", ".Teaser.EditorCopy", 160, SA_GROUP_MAIN, kMEFieldType_Message);
	METableAddScopeColumn(pTable, "Scope", "Scope", 160, SA_GROUP_MAIN, kMEFieldType_TextEntry); // Not validated on purpose
	METableAddFileNameColumn(pTable, "Filename", "Filename", 210, SA_GROUP_MAIN, NULL, GAME_PROGRESSION_BASE_DIR, GAME_PROGRESSION_BASE_DIR, "."GAME_PROGRESSION_EXT, UIBrowseNewOrExisting);
	
	METableAddEnumColumn(pTable, "Functional Type", "NodeFunctionalType", 120, SA_GROUP_MAIN, kMEFieldType_Combo, GameProgressionNodeFunctionalTypeEnum);
	METableAddEnumColumn(pTable, "UI Type", "NodeType", 120, SA_GROUP_MAIN, kMEFieldType_Combo, GameProgressionNodeTypeEnum);	

	METableAddColumn(pTable, "Icon", "Icon", 160, SA_GROUP_MAIN, kMEFieldType_Texture, NULL, NULL, NULL, NULL, "texture_library/ui/Icons", NULL, NULL);
	METableAddColumn(pTable, "Image", "ArtFileName", 160, SA_GROUP_MAIN, kMEFieldType_Texture, NULL, NULL, NULL, NULL, "texture_library/ui/Icons", NULL, NULL);

	METableAddSimpleColumn(pTable, "Sort Order", "SortOrder", 160, SA_GROUP_MAIN, kMEFieldType_ValidatedTextEntry);

	METableAddDictColumn(pTable, "Windback Node", "NodeToWindBack", 100, SA_GROUP_MAIN, kMEFieldType_ValidatedTextEntry, "GameProgressionNodeDef", parse_GameProgressionNodeDef, "Name");
	
	// Requirements
	METableAddDictColumn(pTable, "Required Allegiance", "RequiredAllegiance", 100, SA_GROUP_MAIN, kMEFieldType_ValidatedTextEntry, "Allegiance", parse_AllegianceDef, "Name");
	METableAddDictColumn(pTable, "Required Sub Allegiance", "RequiredSubAllegiance", 100, SA_GROUP_MAIN, kMEFieldType_ValidatedTextEntry, "Allegiance", parse_AllegianceDef, "Name");
	METableAddDictColumn(pTable, "Required Node", "RequiredNode", 100, SA_GROUP_MAIN, kMEFieldType_ValidatedTextEntry, "GameProgressionNodeDef", parse_GameProgressionNodeDef, "Name");

	// MissionGroup data
	METableAddSimpleColumn(pTable, "Est. Completion Time", ".MissionGroupInfo.TimeToComplete", 100, SA_GROUP_MAIN, kMEFieldType_TextEntry);
	METableAddSimpleColumn(pTable, "Required Player Level", ".MissionGroupInfo.RequiredPlayerLevel", 100, SA_GROUP_MAIN, kMEFieldType_ValidatedTextEntry);
	METableAddColumn(pTable, "Map", ".MissionGroupInfo.MapName", 100, SA_GROUP_MAIN, kMEFieldType_ValidatedTextEntry, NULL, NULL, NULL, NULL, NULL, NULL, sae_GetMaps);
	METableAddSimpleColumn(pTable, "Spawn", ".MissionGroupInfo.SpawnPoint", 100, SA_GROUP_MAIN, kMEFieldType_TextEntry);
	METableAddColumn(pTable, "Allowed Mission Maps", ".MissionGroupInfo.AllowedMissionMap", 100, SA_GROUP_MAIN, kMEFieldType_ValidatedTextEntry, NULL, NULL, NULL, NULL, NULL, NULL, sae_GetMaps);
	METableAddSimpleColumn(pTable, "Do Not Auto Grant First Mission On Set Progression", ".MissionGroupInfo.DontAutoGrantMissionOnSetProgression", 120, SA_GROUP_MAIN, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Show Rewards In UI", ".MissionGroupInfo.ShowRewardsInUI", 120, SA_GROUP_MAIN, kMEFieldType_BooleanCombo);

	// Flags
	METableAddSimpleColumn(pTable, "Branch Story", "BranchStory", 120, SA_GROUP_MAIN, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "All Mission Groups Are Unlocked", "MissionGroupsAreUnlockedByDefault", 150, SA_GROUP_MAIN, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Do Not Advance Current Progress Automatically", "DontAdvanceStoryAutomatically", 150, SA_GROUP_MAIN, kMEFieldType_BooleanCombo);	
	
	METableAddSimpleColumn(pTable, "Set Progression On Mission Accept", "SetProgressionOnMissionAccept", 150, SA_GROUP_MAIN, kMEFieldType_BooleanCombo);	
	METableAddSimpleColumn(pTable, "Major Mission Group", ".MissionGroupInfo.Major", 120, SA_GROUP_MAIN, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Debug", "Debug", 120, SA_GROUP_MAIN, kMEFieldType_BooleanCombo);
}

static void sae_InitMissionColumns(METable *pTable)
{
	int id;
	saeProgressionMissionId = id = METableCreateSubTable(pTable, "Mission", ".MissionGroupInfo.Mission", parse_GameProgressionMission, NULL, NULL, NULL, sae_CreateMission);
	METableAddSimpleSubColumn(pTable, id, "Mission", NULL, 80, NULL, 0);
	METableSetSubColumnState(pTable, id, "Mission", ME_STATE_LABEL);
	
	// Lock in label column
	METableSetNumLockedSubColumns(pTable, id, 2);
	
	// Basic Fields
	METableAddSimpleSubColumn(pTable, id, "Description", ".Description.EditorCopy", 100, SA_SUBGROUP_MISSION, kMEFieldType_Message);
	METableAddSubColumn(pTable, id, "Image", "Image", NULL, 160, SA_SUBGROUP_MISSION, kMEFieldType_Texture, NULL, NULL, NULL, NULL, "texture_library/ui/Icons", NULL, NULL);

	// Mission
	METableAddGlobalDictSubColumn(pTable, id, "Mission", "MissionName", 100, SA_SUBGROUP_MISSION, kMEFieldType_ValidatedTextEntry, "Mission", "resourceName");

	// Visible Expression
	METableAddExprSubColumn(pTable, id, "Visible Expression", "ExprBlockVisible", 160, SA_SUBGROUP_MISSION, progression_GetContext(NULL));

	// Flags
	METableAddSimpleSubColumn(pTable, id, "Optional", "Optional", 120, SA_SUBGROUP_MISSION, kMEFieldType_BooleanCombo);
	METableAddSimpleSubColumn(pTable, id, "Skippable", "Skippable", 120, SA_SUBGROUP_MISSION, kMEFieldType_BooleanCombo);
}

static void sae_InitChildrenColumns(METable *pTable)
{
	int id;
	saeProgressionChildrenId = id = METableCreateSubTable(pTable, "ChildNode", "ChildNode", parse_GameProgressionNodeRef, NULL, NULL, NULL, sae_CreateChild);
	METableAddSimpleSubColumn(pTable, id, "ChildNode", NULL, 80, NULL, 0);
	METableSetSubColumnState(pTable, id, "ChildNode", ME_STATE_LABEL);
	
	// Lock in label column
	METableSetNumLockedSubColumns(pTable, id, 2);

	METableAddDictSubColumn(pTable, id, "Node", "NodeDef", 100, SA_SUBGROUP_CHILDNODE, kMEFieldType_ValidatedTextEntry, "GameProgressionNodeDef", parse_GameProgressionNodeDef, "Name");
}

static void *sae_WindowCreateCallback(MEWindow *pWindow, GameProgressionNodeDef *pObjectToClone)
{
	return sae_CreateObject(pWindow->pTable, pObjectToClone, NULL, false);
}

static int sae_ValidateCallback(METable *pTable, GameProgressionNodeDef *pDef, void *pUserData)
{
	char buf[1024];

	if (pDef->pchName[0] == '_') {
		sprintf(buf, "The GameProgressionNodeDef '%s' cannot have a name starting with an underscore.", pDef->pchName);
		ui_DialogPopup("Validation Error", buf);
		return 0;
	}

	if (pDef->eFunctionalType == GameProgressionNodeFunctionalType_MissionGroup)
	{
		REMOVE_HANDLE(pDef->hRequiredAllegiance);
		REMOVE_HANDLE(pDef->hRequiredSubAllegiance);
	}
	if (pDef->eFunctionalType != GameProgressionNodeFunctionalType_MissionGroup)
	{
		REMOVE_HANDLE(pDef->hRequiredNode);
		StructDestroySafe(parse_GameProgressionNodeMissionGroupInfo, &pDef->pMissionGroupInfo);
	}
	return progression_Validate(pDef);
}

static void sae_PostOpenCallback(METable *pTable, GameProgressionNodeDef *pDef, GameProgressionNodeDef *pOrigDef)
{
	sae_FixMessages(pDef);
	if (pOrigDef) {
		sae_FixMessages(pOrigDef);
	}

	METableRefreshRow(pTable, pDef);
}

static void sae_PreSaveCallback(METable *pTable, GameProgressionNodeDef *pDef)
{
	sae_FixMessages(pDef);
}

static void sae_typeChangeCallback(METable *pTable, GameProgressionNodeDef* pNodeDef, void *pUserData, bool bInitNotify)
{
	METableHideSubTable(pTable, pNodeDef, saeProgressionMissionId, (pNodeDef->eFunctionalType!=GameProgressionNodeFunctionalType_MissionGroup));
	METableHideSubTable(pTable, pNodeDef, saeProgressionChildrenId, (pNodeDef->eFunctionalType==GameProgressionNodeFunctionalType_MissionGroup));

	// Hide for non-root nodes
	METableSetFieldNotApplicable(pTable, pNodeDef, "Required Allegiance", (pNodeDef->eFunctionalType != GameProgressionNodeFunctionalType_StoryRoot));
	METableSetFieldNotApplicable(pTable, pNodeDef, "Required Sub Allegiance", (pNodeDef->eFunctionalType != GameProgressionNodeFunctionalType_StoryRoot));
	METableSetFieldNotApplicable(pTable, pNodeDef, "Sort Order", (pNodeDef->eFunctionalType != GameProgressionNodeFunctionalType_StoryRoot));
	METableSetFieldNotApplicable(pTable, pNodeDef, "Branch Story", (pNodeDef->eFunctionalType != GameProgressionNodeFunctionalType_StoryRoot));
	METableSetFieldNotApplicable(pTable, pNodeDef, "All Mission Groups Are Unlocked", (pNodeDef->eFunctionalType != GameProgressionNodeFunctionalType_StoryRoot));
	METableSetFieldNotApplicable(pTable, pNodeDef, "Do Not Advance Current Progress Automatically", (pNodeDef->eFunctionalType != GameProgressionNodeFunctionalType_StoryRoot));
	METableSetFieldNotApplicable(pTable, pNodeDef, "Set Progression On Mission Accept", (pNodeDef->eFunctionalType != GameProgressionNodeFunctionalType_StoryRoot));
			
	// Hide for non-missiongroup nodes
	METableSetFieldNotApplicable(pTable, pNodeDef, "Required Node", (pNodeDef->eFunctionalType!=GameProgressionNodeFunctionalType_MissionGroup));
	METableSetFieldNotApplicable(pTable, pNodeDef, "Est. Completion Time", (pNodeDef->eFunctionalType!=GameProgressionNodeFunctionalType_MissionGroup));
	METableSetFieldNotApplicable(pTable, pNodeDef, "Map", (pNodeDef->eFunctionalType!=GameProgressionNodeFunctionalType_MissionGroup));
	METableSetFieldNotApplicable(pTable, pNodeDef, "Spawn", (pNodeDef->eFunctionalType!=GameProgressionNodeFunctionalType_MissionGroup));	
	METableSetFieldNotApplicable(pTable, pNodeDef, "Allowed Mission Maps", (pNodeDef->eFunctionalType!=GameProgressionNodeFunctionalType_MissionGroup));
	METableSetFieldNotApplicable(pTable, pNodeDef, "Major Mission Group", (pNodeDef->eFunctionalType!=GameProgressionNodeFunctionalType_MissionGroup));
	METableSetFieldNotApplicable(pTable, pNodeDef, "Windback Node", (pNodeDef->eFunctionalType!=GameProgressionNodeFunctionalType_MissionGroup));
	METableSetFieldNotApplicable(pTable, pNodeDef, "Do Not Auto Grant First Mission On Set Progression", (pNodeDef->eFunctionalType!=GameProgressionNodeFunctionalType_MissionGroup));
	METableSetFieldNotApplicable(pTable, pNodeDef, "Show Rewards In UI", (pNodeDef->eFunctionalType!=GameProgressionNodeFunctionalType_MissionGroup));	
}

static void sae_optionalChangedCallback(METable *pTable, GameProgressionNodeDef* pNodeDef, GameProgressionMission* pProgMission, void *pUserData, bool bInitNotify)
{
	METableSetSubFieldNotApplicable(pTable, pNodeDef, saeProgressionMissionId, pProgMission, "Skippable", !!pProgMission->bOptional);

	if (pProgMission->bOptional)
	{
		pProgMission->bSkippable = false;
	}
}

static void *sae_TableCreateCallback(METable *pTable, GameProgressionNodeDef *pObjectToClone, bool bCloneKeepsKeys)
{
	return sae_CreateObject(pTable, pObjectToClone, NULL, bCloneKeepsKeys);
}

static void sae_DictChangeCallback(enumResourceEventType eType, const char *pDictName, ConstReferenceData pRefData, Referent pReferent, METable *pTable)
{
	METableDictChanged(pTable, eType, pReferent, pRefData);
}

static void sae_MessageDictChangeCallback(enumResourceEventType eType, const char *pDictName, const char *pcMessageKey, Referent pReferent, METable *pTable)
{
	if ((eType == RESEVENT_RESOURCE_MODIFIED) ||
		(eType == RESEVENT_RESOURCE_REMOVED) ||
		(eType == RESEVENT_RESOURCE_ADDED)) {

		METableMessageChangedRefresh(pTable, pcMessageKey);
	}
}

//---------------------------------------------------------------------------------------------------
// UI Init
//---------------------------------------------------------------------------------------------------

static void sae_InitCallbacks(MEWindow *pWindow, METable *pTable)
{
	// General Window callbacks
	MEWindowSetCreateCallback(pWindow, sae_WindowCreateCallback);

	// General table callbacks
	METableSetValidateCallback(pTable, sae_ValidateCallback, pTable);
	METableSetPostOpenCallback(pTable, sae_PostOpenCallback);
	METableSetPreSaveCallback(pTable, sae_PreSaveCallback);
	METableSetCreateCallback(pTable, sae_TableCreateCallback);

	METableSetColumnChangeCallback(pTable, "Functional Type", sae_typeChangeCallback, NULL);
	METableSetSubColumnChangeCallback(pTable, saeProgressionMissionId, "Optional", sae_optionalChangedCallback, NULL);

	// We need this registered here instead of by METable because the dictionary will 
	// only allow each callback function to be registered once and there may be multiple
	// METable instances.  Our local callback just passes through to the METable.
	resDictRegisterEventCallback(g_hGameProgressionNodeDictionary, sae_DictChangeCallback, pTable);
	resDictRegisterEventCallback(gMessageDict, sae_MessageDictChangeCallback, pTable);
}

static void sae_Init(MultiEditEMDoc *pEditorDoc)
{
	if (!s_saeWindow) {
		// Create the editor window
		s_saeWindow = MEWindowCreate("Game Progression Editor", "Game Progression Node", "Game Progression Nodes", SEARCH_TYPE_PROGRESSION, g_hGameProgressionNodeDictionary, parse_GameProgressionNodeDef, "Name", "Filename", "Scope", pEditorDoc);

		// Add game progression node columns
		sae_InitColumns(s_saeWindow->pTable);

		// Add game progression mission sub-columns
		sae_InitMissionColumns(s_saeWindow->pTable);

		// Add game progression children sub-columns
		sae_InitChildrenColumns(s_saeWindow->pTable);

		METableFinishColumns(s_saeWindow->pTable);

		// Init the menus after adding all the columns
		MEWindowInitTableMenus(s_saeWindow);

		// Set the callbacks
		sae_InitCallbacks(s_saeWindow, s_saeWindow->pTable);

		// Set edit mode
		resSetDictionaryEditMode(g_MissionDictionary, true);
	}

	// Show the window
	ui_WindowPresent(s_saeWindow->pUIWindow);
}

//---------------------------------------------------------------------------------------------------
// Public Interface
//---------------------------------------------------------------------------------------------------

MEWindow *storyArcEditor_Init(MultiEditEMDoc *pEditorDoc) 
{
	sae_Init(pEditorDoc);	
	return s_saeWindow;
}

void storyArcEditor_CreateStoryArc(char *pcName)
{
	// Create a new object since it is not in the dictionary
	// Add the object as a new object with no old
	void *pObject = sae_CreateObject(s_saeWindow->pTable, NULL, pcName, false);
	METableAddRowByObject(s_saeWindow->pTable, pObject, 1, 1);
}

#endif