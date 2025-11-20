//
// DonationTaskEditor.c
//

#ifndef NO_EDITORS

#include "MultiEditTable.h"
#include "MultiEditWindow.h"
#include "ResourceSearch.h"
#include "GroupProjectCommon.h"
#include "StringCache.h"
#include "EString.h"
#include "Expression.h"
#include "GameBranch.h"

#include "AutoGen/GroupProjectCommon_h_ast.h"
#include "AutoGen/itemEnums_h_ast.h"

// This magic line is required if you run in Full Debug to avoid assert
AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

#define DTE_GROUP_MAIN			"Main"
#define DTE_SUBGROUP_REQUIRES	"Requirements"
#define DTE_SUBGROUP_STARTREWARDS	"StartRewards"
#define DTE_SUBGROUP_REWARDS	"Rewards"

//-----------------------------------------------------------------------------------
// Global Data
//-----------------------------------------------------------------------------------

static MEWindow *dteWindow = NULL;
static int dteRequirements = 0;
static int dteRewards = 0;
static int dteStartRewards = 0;

extern ExprContext *g_pItemContext;
extern ExprContext *g_DonationTaskExprContext;

//---------------------------------------------------------------------------------------------------
// Callbacks
//---------------------------------------------------------------------------------------------------

static int dte_validateCallback(METable *pTable, DonationTaskDef *pDonationTaskDef, void *pUserData)
{
	int i;

	for (i = eaSize(&pDonationTaskDef->buckets)-1; i >= 0; i--)
	{
		GroupProjectDonationRequirement *pBucket = pDonationTaskDef->buckets[i];
		if (pBucket->specType != DonationSpecType_Item)
		{
			REMOVE_HANDLE(pBucket->requiredItem);
		}
		else if (pBucket->specType != DonationSpecType_Expression)
		{
			//TODO: Cleanup expression
		}
	}
	for (i = eaSize(&pDonationTaskDef->taskRewards)-1; i >= 0; i--)
	{
		DonationTaskReward *pReward = pDonationTaskDef->taskRewards[i];
		if (pReward->rewardType != DonationTaskRewardType_NumericSet && 
			pReward->rewardType != DonationTaskRewardType_NumericAdd)
		{
			REMOVE_HANDLE(pReward->numericDef);
		}
		else if (pReward->rewardType != DonationTaskRewardType_Unlock)
		{
			REMOVE_HANDLE(pReward->unlockDef);
		}
	}
    for (i = eaSize(&pDonationTaskDef->taskStartRewards)-1; i >= 0; i--)
    {
        DonationTaskReward *pReward = pDonationTaskDef->taskStartRewards[i];
        if (pReward->rewardType != DonationTaskRewardType_NumericSet && 
            pReward->rewardType != DonationTaskRewardType_NumericAdd)
        {
            REMOVE_HANDLE(pReward->numericDef);
        }
        else if (pReward->rewardType != DonationTaskRewardType_Unlock)
        {
            REMOVE_HANDLE(pReward->unlockDef);
        }
    }
	return DonationTask_Validate(pDonationTaskDef);
}


static void *dte_createTaskRequirement(METable *pTable, DonationTaskDef *pTaskDef, GroupProjectDonationRequirement *pRequirementToClone, GroupProjectDonationRequirement *pBeforeRequirement, GroupProjectDonationRequirement *pAfterRequirement)
{
	GroupProjectDonationRequirement *pNewRequirement;
	Message *pDisplayMessage;
	Message *pDescriptionMessage;
	Message *pTooltipMessage;
	
	// Allocate the object
	if (pRequirementToClone) {
		pNewRequirement = StructClone(parse_GroupProjectDonationRequirement, pRequirementToClone);
		pDisplayMessage = langCreateMessage("", "", "", pRequirementToClone->displayNameMsg.pEditorCopy ? pRequirementToClone->displayNameMsg.pEditorCopy->pcDefaultString : NULL);
		pDescriptionMessage = langCreateMessage("", "", "", pRequirementToClone->descriptionMsg.pEditorCopy ? pRequirementToClone->descriptionMsg.pEditorCopy->pcDefaultString : NULL);
		pTooltipMessage = langCreateMessage("", "", "", pRequirementToClone->tooltipMsg.pEditorCopy ? pRequirementToClone->tooltipMsg.pEditorCopy->pcDefaultString : NULL);
	} else {
		pNewRequirement = StructCreate(parse_GroupProjectDonationRequirement);
		pDisplayMessage = langCreateMessage("", "", "", NULL);
		pDescriptionMessage = langCreateMessage("", "", "", NULL);
		pTooltipMessage = langCreateMessage("", "", "", NULL);
	}
	
	assertmsg(pNewRequirement, "Failed to create donation requirement");
	
	// Fill in messages
	pNewRequirement->displayNameMsg.pEditorCopy = pDisplayMessage;
	pNewRequirement->descriptionMsg.pEditorCopy = pDescriptionMessage;
	pNewRequirement->tooltipMsg.pEditorCopy = pTooltipMessage;
	
	return pNewRequirement;
}

static void *dte_createTaskReward(METable *pTable, DonationTaskDef *pTaskDef, DonationTaskReward *pDefToClone, DonationTaskReward *pBeforeDef, DonationTaskReward *pAfterDef)
{
	DonationTaskReward *pNewDef;
	
	// Allocate the object
	if (pDefToClone) {
		pNewDef = StructClone(parse_DonationTaskReward, pDefToClone);
	} else {
		pNewDef = StructCreate(parse_DonationTaskReward);
	}
	
	assertmsg(pNewDef, "Failed to create donation task reward");
	
	return pNewDef;
}

static void dte_fixRequirementMessages(DonationTaskDef *pTaskDef, GroupProjectDonationRequirement *pRequirement)
{
	char *tmpS = NULL;
	char *tmpKeyPrefix = NULL;
	estrStackCreate(&tmpS);
	estrStackCreate(&tmpKeyPrefix);

	estrCopy2(&tmpKeyPrefix, "DonationTaskDef.");
	if(g_pcGameBranch)
	{
		estrConcat(&tmpKeyPrefix, g_pcGameBranch, (int)strlen(g_pcGameBranch));
		estrConcatChar(&tmpKeyPrefix, '.');
	}

	// Display name
	if (!pRequirement->displayNameMsg.pEditorCopy) {
		pRequirement->displayNameMsg.pEditorCopy = StructCreate(parse_Message);
	}
	
	estrPrintf(&tmpS, "%s%s.DisplayName.%s", tmpKeyPrefix, pTaskDef->name, pRequirement->name);
	if (!pRequirement->displayNameMsg.pEditorCopy->pcMessageKey || 
		(stricmp(tmpS, pRequirement->displayNameMsg.pEditorCopy->pcMessageKey) != 0)) {
		pRequirement->displayNameMsg.pEditorCopy->pcMessageKey = allocAddString(tmpS);
	}
	
	estrPrintf(&tmpS, "Display name message");
	if (!pRequirement->displayNameMsg.pEditorCopy->pcDescription ||
		(stricmp(tmpS, pRequirement->displayNameMsg.pEditorCopy->pcDescription) != 0)) {
		StructFreeString(pRequirement->displayNameMsg.pEditorCopy->pcDescription);
		pRequirement->displayNameMsg.pEditorCopy->pcDescription = StructAllocString(tmpS);
	}
	
	estrPrintf(&tmpS, "DonationRequirement");
	if (!pRequirement->displayNameMsg.pEditorCopy->pcScope ||
		(stricmp(tmpS, pRequirement->displayNameMsg.pEditorCopy->pcScope) != 0)) {
		pRequirement->displayNameMsg.pEditorCopy->pcScope = allocAddString(tmpS);
	}

	// Description
	if (!pRequirement->descriptionMsg.pEditorCopy) {
		pRequirement->descriptionMsg.pEditorCopy = StructCreate(parse_Message);
	}

	estrPrintf(&tmpS, "%s%s.Description.%s", tmpKeyPrefix, pTaskDef->name, pRequirement->name);
	if (!pRequirement->descriptionMsg.pEditorCopy->pcMessageKey || 
		(stricmp(tmpS, pRequirement->descriptionMsg.pEditorCopy->pcMessageKey) != 0)) {
		pRequirement->descriptionMsg.pEditorCopy->pcMessageKey = allocAddString(tmpS);
	}
	
	estrPrintf(&tmpS, "Description message");
	if (!pRequirement->descriptionMsg.pEditorCopy->pcDescription ||
		(stricmp(tmpS, pRequirement->descriptionMsg.pEditorCopy->pcDescription) != 0)) {
		StructFreeString(pRequirement->descriptionMsg.pEditorCopy->pcDescription);
		pRequirement->descriptionMsg.pEditorCopy->pcDescription = StructAllocString(tmpS);
	}
	
	estrPrintf(&tmpS, "DonationRequirement");
	if (!pRequirement->descriptionMsg.pEditorCopy->pcScope ||
		(stricmp(tmpS, pRequirement->descriptionMsg.pEditorCopy->pcScope) != 0)) {
		pRequirement->descriptionMsg.pEditorCopy->pcScope = allocAddString(tmpS);
	}

	// Tooltip
	if (!pRequirement->tooltipMsg.pEditorCopy) {
		pRequirement->tooltipMsg.pEditorCopy = StructCreate(parse_Message);
	}

	estrPrintf(&tmpS, "%s%s.Tooltip.%s", tmpKeyPrefix, pTaskDef->name, pRequirement->name);
	if (!pRequirement->tooltipMsg.pEditorCopy->pcMessageKey || 
		(stricmp(tmpS, pRequirement->tooltipMsg.pEditorCopy->pcMessageKey) != 0)) {
			pRequirement->tooltipMsg.pEditorCopy->pcMessageKey = allocAddString(tmpS);
	}

	estrPrintf(&tmpS, "Tooltip message");
	if (!pRequirement->tooltipMsg.pEditorCopy->pcDescription ||
		(stricmp(tmpS, pRequirement->tooltipMsg.pEditorCopy->pcDescription) != 0)) {
			StructFreeString(pRequirement->tooltipMsg.pEditorCopy->pcDescription);
			pRequirement->tooltipMsg.pEditorCopy->pcDescription = StructAllocString(tmpS);
	}

	estrPrintf(&tmpS, "DonationRequirement");
	if (!pRequirement->tooltipMsg.pEditorCopy->pcScope ||
		(stricmp(tmpS, pRequirement->tooltipMsg.pEditorCopy->pcScope) != 0)) {
			pRequirement->tooltipMsg.pEditorCopy->pcScope = allocAddString(tmpS);
	}

	estrDestroy(&tmpS);
	estrDestroy(&tmpKeyPrefix);
}

static void dte_fixMessages(DonationTaskDef *pTaskDef)
{
	char *tmpS = NULL;
	char *tmpKeyPrefix = NULL;
	estrStackCreate(&tmpS);
	estrStackCreate(&tmpKeyPrefix);
	
	estrCopy2(&tmpKeyPrefix, "DonationTaskDef.");
	if(g_pcGameBranch)
	{
		estrConcat(&tmpKeyPrefix, g_pcGameBranch, (int)strlen(g_pcGameBranch));
		estrConcatChar(&tmpKeyPrefix, '.');
	}
	
	// Display name
	if (!pTaskDef->displayNameMsg.pEditorCopy) {
		pTaskDef->displayNameMsg.pEditorCopy = StructCreate(parse_Message);
	}
	
	estrPrintf(&tmpS, "%s%s.DisplayName", tmpKeyPrefix, pTaskDef->name);
	if (!pTaskDef->displayNameMsg.pEditorCopy->pcMessageKey || 
		(stricmp(tmpS, pTaskDef->displayNameMsg.pEditorCopy->pcMessageKey) != 0)) {
		pTaskDef->displayNameMsg.pEditorCopy->pcMessageKey = allocAddString(tmpS);
	}
	
	estrPrintf(&tmpS, "Display name message");
	if (!pTaskDef->displayNameMsg.pEditorCopy->pcDescription ||
		(stricmp(tmpS, pTaskDef->displayNameMsg.pEditorCopy->pcDescription) != 0)) {
		StructFreeString(pTaskDef->displayNameMsg.pEditorCopy->pcDescription);
		pTaskDef->displayNameMsg.pEditorCopy->pcDescription = StructAllocString(tmpS);
	}
	
	estrPrintf(&tmpS, "DonationTaskDef");
	if (!pTaskDef->displayNameMsg.pEditorCopy->pcScope ||
		(stricmp(tmpS, pTaskDef->displayNameMsg.pEditorCopy->pcScope) != 0)) {
		pTaskDef->displayNameMsg.pEditorCopy->pcScope = allocAddString(tmpS);
	}

	// Description
	if (!pTaskDef->descriptionMsg.pEditorCopy) {
		pTaskDef->descriptionMsg.pEditorCopy = StructCreate(parse_Message);
	}

	estrPrintf(&tmpS, "%s%s.Description", tmpKeyPrefix, pTaskDef->name);
	if (!pTaskDef->descriptionMsg.pEditorCopy->pcMessageKey || 
		(stricmp(tmpS, pTaskDef->descriptionMsg.pEditorCopy->pcMessageKey) != 0)) {
		pTaskDef->descriptionMsg.pEditorCopy->pcMessageKey = allocAddString(tmpS);
	}
	
	estrPrintf(&tmpS, "Description message");
	if (!pTaskDef->descriptionMsg.pEditorCopy->pcDescription ||
		(stricmp(tmpS, pTaskDef->descriptionMsg.pEditorCopy->pcDescription) != 0)) {
		StructFreeString(pTaskDef->descriptionMsg.pEditorCopy->pcDescription);
		pTaskDef->descriptionMsg.pEditorCopy->pcDescription = StructAllocString(tmpS);
	}
	
	estrPrintf(&tmpS, "DonationTaskDef");
	if (!pTaskDef->descriptionMsg.pEditorCopy->pcScope ||
		(stricmp(tmpS, pTaskDef->descriptionMsg.pEditorCopy->pcScope) != 0)) {
		pTaskDef->descriptionMsg.pEditorCopy->pcScope = allocAddString(tmpS);
	}

	estrDestroy(&tmpS);
	estrDestroy(&tmpKeyPrefix);
}

static void dte_postOpenCallback(METable *pTable, DonationTaskDef *pTaskDef, DonationTaskDef *pOrigTaskDef)
{
	S32 i;
	
	dte_fixMessages(pTaskDef);
	if (pOrigTaskDef) {
		dte_fixMessages(pOrigTaskDef);
	}

	for (i = eaSize(&pTaskDef->buckets)-1; i >= 0; i--) {
		dte_fixRequirementMessages(pTaskDef, pTaskDef->buckets[i]);
	}

	if (pOrigTaskDef) {
		for (i = eaSize(&pOrigTaskDef->buckets)-1; i >= 0; i--){
			dte_fixRequirementMessages(pOrigTaskDef, pOrigTaskDef->buckets[i]);
		}
	}
}

static void dte_preSaveCallback(METable *pTable, DonationTaskDef *pTaskDef)
{
	S32 i;
	
	dte_fixMessages(pTaskDef);
	for (i = eaSize(&pTaskDef->buckets)-1; i >= 0; i--){
		dte_fixRequirementMessages(pTaskDef, pTaskDef->buckets[i]);
	}
}

static void *dte_createObject(METable *pTable, DonationTaskDef *pObjectToClone, char *pcNewName, bool bCloneKeepsKeys)
{
	DonationTaskDef *pNewDef = NULL;
	char buf[128];
	const char *pcBaseName;
	char *pchPath = NULL;
	Message *pDisplayMessage;
	Message *pDescriptionMessage;

	// Create the object
	if (pObjectToClone) {
		pNewDef = StructClone(parse_DonationTaskDef, pObjectToClone);
		pcBaseName = pObjectToClone->name;

		pDisplayMessage = langCreateMessage("", "", "", pObjectToClone->displayNameMsg.pEditorCopy ? pObjectToClone->displayNameMsg.pEditorCopy->pcDefaultString : NULL);
		pDescriptionMessage = langCreateMessage("", "", "", pObjectToClone->descriptionMsg.pEditorCopy ? pObjectToClone->descriptionMsg.pEditorCopy->pcDefaultString : NULL);
	} else {
		pNewDef = StructCreate(parse_DonationTaskDef);

		pcBaseName = "_New_DonationTask";

		pDisplayMessage = langCreateMessage("", "", "", NULL);
		pDescriptionMessage = langCreateMessage("", "", "", NULL);
	}
	// Use provided name if available
	if (pcNewName) {
		pcBaseName = pcNewName;
	}

	assertmsg(pNewDef, "Failed to create donation task");

	// Assign a new name
	pNewDef->name = (char*)METableMakeNewNameShared(pTable, pcBaseName, true);

	// Assign a file
	sprintf(buf,
		FORMAT_OK(GameBranch_FixupPath(&pchPath, DONATION_TASK_BASE_DIR"/%s.%s", true, false)),
		pNewDef->name, DONATION_TASK_EXT);
	pNewDef->filename = (char*)allocAddString(buf);

	// Fill in messages
	pNewDef->displayNameMsg.pEditorCopy = pDisplayMessage;
	pNewDef->descriptionMsg.pEditorCopy = pDescriptionMessage;

	estrDestroy(&pchPath);
	
	return pNewDef;
}


static void *dte_tableCreateCallback(METable *pTable, DonationTaskDef *pObjectToClone, bool bCloneKeepsKeys)
{
	return dte_createObject(pTable, pObjectToClone, NULL, bCloneKeepsKeys);
}


static void *dte_windowCreateCallback(MEWindow *pWindow, DonationTaskDef *pObjectToClone)
{
	return dte_createObject(pWindow->pTable, pObjectToClone, NULL, false);
}

static void dte_requirementTypeChangeCallback(METable *pTable, DonationTaskDef *pDef, GroupProjectDonationRequirement *pRequirement, void *pUserData, bool bInitNotify)
{
	METableSetSubFieldNotApplicable(pTable, pDef, dteRequirements, pRequirement, "Required Item", (pRequirement->specType != DonationSpecType_Item));
	METableSetSubFieldNotApplicable(pTable, pDef, dteRequirements, pRequirement, "Allowed Item Expression", (pRequirement->specType != DonationSpecType_Expression));
    METableSetSubFieldNotApplicable(pTable, pDef, dteRequirements, pRequirement, "Required Item Categories", (pRequirement->specType != DonationSpecType_Expression));
    METableSetSubFieldNotApplicable(pTable, pDef, dteRequirements, pRequirement, "Restrict Item Categories", (pRequirement->specType != DonationSpecType_Expression));
}

static void dte_rewardTypeChangeCallback(METable *pTable, DonationTaskDef *pDef, DonationTaskReward *pReward, void *pUserData, bool bInitNotify)
{
	METableSetSubFieldNotApplicable(pTable, pDef, dteRewards, pReward, "Project Numeric", (pReward->rewardType != DonationTaskRewardType_NumericAdd) && (pReward->rewardType != DonationTaskRewardType_NumericSet));
    METableSetSubFieldNotApplicable(pTable, pDef, dteRewards, pReward, "Reward Constant", (pReward->rewardType != DonationTaskRewardType_NumericAdd) && (pReward->rewardType != DonationTaskRewardType_NumericSet));
    METableSetSubFieldNotApplicable(pTable, pDef, dteRewards, pReward, "Unlock", (pReward->rewardType != DonationTaskRewardType_Unlock));
}

static void dte_startRewardTypeChangeCallback(METable *pTable, DonationTaskDef *pDef, DonationTaskReward *pReward, void *pUserData, bool bInitNotify)
{
    METableSetSubFieldNotApplicable(pTable, pDef, dteStartRewards, pReward, "Project Numeric", (pReward->rewardType != DonationTaskRewardType_NumericAdd) && (pReward->rewardType != DonationTaskRewardType_NumericSet));
    METableSetSubFieldNotApplicable(pTable, pDef, dteStartRewards, pReward, "Reward Constant", (pReward->rewardType != DonationTaskRewardType_NumericAdd) && (pReward->rewardType != DonationTaskRewardType_NumericSet));
    METableSetSubFieldNotApplicable(pTable, pDef, dteStartRewards, pReward, "Unlock", (pReward->rewardType != DonationTaskRewardType_Unlock));
}

static void dte_dictChangeCallback(enumResourceEventType eType, const char *pDictName, ConstReferenceData pRefData, Referent pReferent, METable *pTable)
{
	METableDictChanged(pTable, eType, pReferent, pRefData);
}

static void dte_MessageDictChangeCallback(enumResourceEventType eType, const char *pDictName, const char *pcMessageKey, Referent pReferent, METable *pTable)
{
	if ((eType == RESEVENT_RESOURCE_MODIFIED) ||
		(eType == RESEVENT_RESOURCE_REMOVED) ||
		(eType == RESEVENT_RESOURCE_ADDED)) {

		METableMessageChangedRefresh(pTable, pcMessageKey);
	}
}

static void dte_initCallbacks(MEWindow *pWindow, METable *pTable)
{
	// General Window callbacks
	MEWindowSetCreateCallback(pWindow, dte_windowCreateCallback);

	// General table callbacks
	METableSetValidateCallback(pTable, dte_validateCallback, pTable);
	METableSetCreateCallback(pTable, dte_tableCreateCallback);
	METableSetPostOpenCallback(pTable, dte_postOpenCallback);
	METableSetPreSaveCallback(pTable, dte_preSaveCallback);

	METableSetSubColumnChangeCallback(pTable, dteRequirements, "Type", dte_requirementTypeChangeCallback, NULL);
	METableSetSubColumnChangeCallback(pTable, dteRewards, "Type", dte_rewardTypeChangeCallback, NULL);
    METableSetSubColumnChangeCallback(pTable, dteStartRewards, "Type", dte_startRewardTypeChangeCallback, NULL);

	// We need this registered here instead of by METable because the dictionary will 
	// only allow each callback function to be registered once and there may be multiple
	// METable instances.  Our local callback just passes through to the METable.
	resDictRegisterEventCallback(g_DonationTaskDict, dte_dictChangeCallback, pTable);
	resDictRegisterEventCallback(gMessageDict, dte_MessageDictChangeCallback, pTable);
}


//---------------------------------------------------------------------------------------------------
// UI Init
//---------------------------------------------------------------------------------------------------

static void dte_initDonationTaskColumns(METable *pTable)
{
	char *pchPath = NULL;

	GameBranch_GetDirectory(&pchPath, DONATION_TASK_BASE_DIR);

	METableAddSimpleColumn(pTable, "Name", "name", 150, NULL, kMEFieldType_TextEntry);
	
	// Lock in name column
	METableSetNumLockedColumns(pTable, 2);
	
	METableAddScopeColumn(pTable, "Scope", "Scope", 160, DTE_GROUP_MAIN, kMEFieldType_TextEntry); // Not validated on purpose
	METableAddFileNameColumn(pTable, "File Name", "filename", 210, DTE_GROUP_MAIN, NULL, pchPath, pchPath, "."DONATION_TASK_EXT, UIBrowseNewOrExisting);
	METableAddSimpleColumn(pTable, "Display Name", ".displayNameMsg.EditorCopy", 100, DTE_GROUP_MAIN, kMEFieldType_Message);
	METableAddSimpleColumn(pTable, "Description", ".descriptionMsg.EditorCopy", 100, DTE_GROUP_MAIN, kMEFieldType_Message);
	METableAddSimpleColumn(pTable, "Tooltip", ".tooltipMsg.EditorCopy", 100, DTE_GROUP_MAIN, kMEFieldType_Message);
	METableAddEnumColumn(pTable, "Slot Type", "SlotType", 120, DTE_GROUP_MAIN, kMEFieldType_Combo, GroupProjectTaskSlotTypeEnum);
	METableAddColumn(pTable, "Icon", "Icon", 180, DTE_GROUP_MAIN, kMEFieldType_Texture, NULL, NULL, NULL, NULL, "texture_library/ui/Icons", NULL, NULL);
	METableAddSimpleColumn(pTable, "Seconds To Complete", "secondsToComplete", 120, DTE_GROUP_MAIN, kMEFieldType_TextEntry);
	METableAddSimpleColumn(pTable, "Repeatable", "repeatable", 100, DTE_GROUP_MAIN, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Cancelable", "cancelable", 100, DTE_GROUP_MAIN, kMEFieldType_BooleanCombo);
    METableAddSimpleColumn(pTable, "Task Available for New Projects", "taskAvailableForNewProject", 180, DTE_GROUP_MAIN, kMEFieldType_BooleanCombo);
	METableAddExprColumn(pTable, "Task Available Expression", "taskAvailableExpr", 240, DTE_GROUP_MAIN, g_DonationTaskExprContext);
	METableAddEnumColumn(pTable, "Category", "category", 120, DTE_GROUP_MAIN, kMEFieldType_Combo, DonationTaskCategoryTypeEnum);
    METableAddGlobalDictColumn(pTable, "Player Reward Table", "completionRewardTable", 240, DTE_GROUP_MAIN, kMEFieldType_ValidatedTextEntry, "RewardTable", "resourceName");

	estrDestroy(&pchPath);
}


static void dte_initRequirementColumns(METable *pTable)
{
	int id;
	
	// Create the subtable and get the ID
	dteRequirements = id = METableCreateSubTable(pTable, "Task Requirement", ".bucket", parse_GroupProjectDonationRequirement, NULL, NULL, NULL, dte_createTaskRequirement);
	
	// Lock in label column
	METableSetNumLockedSubColumns(pTable, id, 2);
	
	METableAddSimpleSubColumn(pTable, id, "Name", "name", 240, DTE_SUBGROUP_REQUIRES, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id, "Display Name", ".displayNameMsg.EditorCopy", 240, DTE_SUBGROUP_REQUIRES, kMEFieldType_Message);
	METableAddSimpleSubColumn(pTable, id, "Description", ".descriptionMsg.EditorCopy", 240, DTE_SUBGROUP_REQUIRES, kMEFieldType_Message);
	METableAddSimpleSubColumn(pTable, id, "Tooltip", ".tooltipMsg.EditorCopy", 240, DTE_SUBGROUP_REQUIRES, kMEFieldType_Message);
	METableAddSubColumn(pTable, id, "Icon", "Icon", NULL, 180, DTE_SUBGROUP_REQUIRES, kMEFieldType_Texture, NULL, NULL, NULL, NULL, "texture_library/ui/Icons", NULL, NULL);
	METableAddEnumSubColumn(pTable, id, "Type", "Type", 240, DTE_SUBGROUP_REQUIRES, kMEFieldType_Combo, GroupProjectDonationSpecTypeEnum);
	METableAddSimpleSubColumn(pTable, id, "Count", "count", 240, DTE_SUBGROUP_REQUIRES, kMEFieldType_TextEntry);
    METableAddSimpleSubColumn(pTable, id, "Donation Increment", "donationIncrement", 240, DTE_SUBGROUP_REQUIRES, kMEFieldType_TextEntry);
	METableAddGlobalDictSubColumn(pTable, id, "Required Item", "RequiredItem", 240, DTE_SUBGROUP_REQUIRES, kMEFieldType_ValidatedTextEntry, "ItemDef", "resourceName");
	METableAddExprSubColumn(pTable, id, "Allowed Item Expression", "allowedItemExpr", 240, DTE_SUBGROUP_REQUIRES, g_pItemContext);
    METableAddEnumSubColumn(pTable, id, "Required Item Categories", "RequiredItemCategory", 120, DTE_SUBGROUP_REQUIRES, kMEFieldType_FlagCombo, ItemCategoryEnum);
    METableAddEnumSubColumn(pTable, id, "Restrict Item Categories", "RestrictItemCategory", 120, DTE_SUBGROUP_REQUIRES, kMEFieldType_FlagCombo, ItemCategoryEnum);
	METableAddSimpleSubColumn(pTable, id, "Contribution Constant", "contributionConstant", 240, DTE_SUBGROUP_REQUIRES, kMEFieldType_TextEntry);
	
}

static void dte_initRewardColumns(METable *pTable)
{
	int id;

	// Create the subtable and get the ID
	dteRewards = id = METableCreateSubTable(pTable, "Task Rewards", ".TaskReward", parse_DonationTaskReward, NULL, NULL, NULL, dte_createTaskReward);
	
	// Lock in label column
	METableSetNumLockedSubColumns(pTable, id, 2);

	METableAddEnumSubColumn(pTable, id, "Type", "Type", 240, DTE_SUBGROUP_REWARDS, kMEFieldType_Combo, DonationTaskRewardTypeEnum);
	METableAddDictSubColumn(pTable, id, "Unlock", "Unlock", 240, DTE_SUBGROUP_REWARDS, kMEFieldType_ValidatedTextEntry, "GroupProjectUnlockDef", parse_GroupProjectUnlockDef, "name");
	METableAddDictSubColumn(pTable, id, "Project Numeric", "Numeric", 240, DTE_SUBGROUP_REWARDS, kMEFieldType_ValidatedTextEntry, "GroupProjectNumericDef", parse_GroupProjectNumericDef, "name");
	METableAddSimpleSubColumn(pTable, id, "Reward Constant", "rewardConstant", 240, DTE_SUBGROUP_REWARDS, kMEFieldType_TextEntry);
}

static void dte_initStartRewardColumns(METable *pTable)
{
    int id;

    // Create the subtable and get the ID
    dteStartRewards = id = METableCreateSubTable(pTable, "Task Start Rewards", ".TaskStartReward", parse_DonationTaskReward, NULL, NULL, NULL, dte_createTaskReward);

    // Lock in label column
    METableSetNumLockedSubColumns(pTable, id, 2);

    METableAddEnumSubColumn(pTable, id, "Type", "Type", 240, DTE_SUBGROUP_STARTREWARDS, kMEFieldType_Combo, DonationTaskRewardTypeEnum);
    METableAddDictSubColumn(pTable, id, "Unlock", "Unlock", 240, DTE_SUBGROUP_STARTREWARDS, kMEFieldType_ValidatedTextEntry, "GroupProjectUnlockDef", parse_GroupProjectUnlockDef, "name");
    METableAddDictSubColumn(pTable, id, "Project Numeric", "Numeric", 240, DTE_SUBGROUP_STARTREWARDS, kMEFieldType_ValidatedTextEntry, "GroupProjectNumericDef", parse_GroupProjectNumericDef, "name");
    METableAddSimpleSubColumn(pTable, id, "Reward Constant", "rewardConstant", 240, DTE_SUBGROUP_STARTREWARDS, kMEFieldType_TextEntry);
}

static void dte_init(MultiEditEMDoc *pEditorDoc)
{
	if (!dteWindow) {
		// Create the editor window
		dteWindow = MEWindowCreate("Donation Task Editor", "DonationTask", "DonationTasks", SEARCH_TYPE_DONATIONTASK, g_DonationTaskDict, parse_DonationTaskDef, "name", "filename", "scope", pEditorDoc);

		// Add task-specific columns
		dte_initDonationTaskColumns(dteWindow->pTable);
		dte_initRequirementColumns(dteWindow->pTable);
		dte_initRewardColumns(dteWindow->pTable);
        dte_initStartRewardColumns(dteWindow->pTable);
		METableFinishColumns(dteWindow->pTable);

		// Init the menus after adding all the columns
		MEWindowInitTableMenus(dteWindow);

		// Set the callbacks
		dte_initCallbacks(dteWindow, dteWindow->pTable);

		// Set edit mode
		resRequestAllResourcesInDictionary(g_GroupProjectUnlockDict);
		resRequestAllResourcesInDictionary(g_GroupProjectNumericDict);
	}

	// Show the window
	ui_WindowPresent(dteWindow->pUIWindow);
}


//---------------------------------------------------------------------------------------------------
// Public Interface
//---------------------------------------------------------------------------------------------------

MEWindow *donationTaskEditor_init(MultiEditEMDoc *pEditorDoc) 
{
	dte_init(pEditorDoc);

	return dteWindow;
}

void donationTaskEditor_createDonationTask(char *pcName)
{
	// Create a new object since it is not in the dictionary
	// Add the object as a new object with no old
	void *pObject = dte_createObject(dteWindow->pTable, NULL, pcName, false);
	METableAddRowByObject(dteWindow->pTable, pObject, 1, 1);
}

#endif