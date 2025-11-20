/***************************************************************************
*     Copyright (c) 2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef NO_EDITORS

#include "ActivityCommon.h"
#include "EString.h"
#include "gameeditorshared.h"
#include "ItemAssignments.h"
#include "MultiEditTable.h"
#include "MultiEditWindow.h"
#include "ResourceSearch.h"
#include "rewardCommon.h"
#include "StringCache.h"

#include "AutoGen/allegiance_h_ast.h"
#include "AutoGen/ItemAssignments_h_ast.h"
#include "AutoGen/itemEnums_h_ast.h"


#define IA_GROUP_MAIN "Main"
#define IA_GROUP_SCALING "Scaling"
#define IA_GROUP_REQUIREMENTS "Requirements"
#define IA_GROUP_FLAGS "Flags"
#define IA_SUBGROUP_OUTCOME	"Outcome"
#define IA_SUBGROUP_SLOT "Slot"
#define IA_SUBGROUP_MODIFIER "Modifier"
#define IA_SUBGROUP_ITEMCOST "ItemCost"

//-----------------------------------------------------------------------------------
// Global Data
//-----------------------------------------------------------------------------------

static MEWindow *s_iaeWindow = NULL;

static int iaeOutcomeId = 0;
static int iaeSlotId = 0;
static int iaeModifierId = 0;
static int iaeItemCostId = 0;

// This magic line is required if you run in Full Debug to avoid assert
AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

static void iae_FixMessages(ItemAssignmentDef* pDef)
{
	char *estrTmp = NULL;
	int i;

	estrStackCreate(&estrTmp);

	// Fixup display name
	if(!pDef->msgDisplayName.pEditorCopy)
	{
		pDef->msgDisplayName.pEditorCopy = StructCreate(parse_Message);
	}

	estrPrintf(&estrTmp, "ItemAssignment.%s", pDef->pchName);
	if(!pDef->msgDisplayName.pEditorCopy->pcMessageKey ||
		(stricmp(estrTmp, pDef->msgDisplayName.pEditorCopy->pcMessageKey) != 0)) {
			pDef->msgDisplayName.pEditorCopy->pcMessageKey = allocAddString(estrTmp);
	}

	estrPrintf(&estrTmp, "Display name for an item assignment definition");
	if(!pDef->msgDisplayName.pEditorCopy->pcDescription ||
		(stricmp(estrTmp, pDef->msgDisplayName.pEditorCopy->pcDescription) != 0)) {
			pDef->msgDisplayName.pEditorCopy->pcDescription = StructAllocString(estrTmp);
	}

	estrPrintf(&estrTmp, "ItemAssignment");
	if(!pDef->msgDisplayName.pEditorCopy->pcScope ||
		(stricmp(estrTmp, pDef->msgDisplayName.pEditorCopy->pcScope) != 0)) {
			pDef->msgDisplayName.pEditorCopy->pcScope = allocAddString(estrTmp);
	}

	// Fixup description
	if(!pDef->msgDescription.pEditorCopy)
	{
		pDef->msgDescription.pEditorCopy = StructCreate(parse_Message);
	}

	estrPrintf(&estrTmp, "ItemAssignment.%s.Description", pDef->pchName);
	if(!pDef->msgDescription.pEditorCopy->pcMessageKey ||
		(stricmp(estrTmp, pDef->msgDescription.pEditorCopy->pcMessageKey) != 0)) {
			pDef->msgDescription.pEditorCopy->pcMessageKey = allocAddString(estrTmp);
	}

	estrPrintf(&estrTmp, "Description for an item assignment");
	if(!pDef->msgDescription.pEditorCopy->pcDescription ||
		(stricmp(estrTmp, pDef->msgDescription.pEditorCopy->pcDescription) != 0)) {
			pDef->msgDescription.pEditorCopy->pcDescription = StructAllocString(estrTmp);
	}

	estrPrintf(&estrTmp, "ItemAssignment");
	if(!pDef->msgDescription.pEditorCopy->pcScope ||
		(stricmp(estrTmp, pDef->msgDescription.pEditorCopy->pcScope) != 0)) {
			pDef->msgDescription.pEditorCopy->pcScope = allocAddString(estrTmp);
	}

	// Fixup assignment chain display name
	if(!pDef->msgAssignmentChainDisplayName.pEditorCopy)
	{
		pDef->msgAssignmentChainDisplayName.pEditorCopy = StructCreate(parse_Message);
	}

	estrPrintf(&estrTmp, "ItemAssignment.%s.ChainDisplayName", pDef->pchName);
	if(!pDef->msgAssignmentChainDisplayName.pEditorCopy->pcMessageKey ||
		(stricmp(estrTmp, pDef->msgAssignmentChainDisplayName.pEditorCopy->pcMessageKey) != 0)) {
			pDef->msgAssignmentChainDisplayName.pEditorCopy->pcMessageKey = allocAddString(estrTmp);
	}

	estrPrintf(&estrTmp, "Display name for an item assignment chain");
	if(!pDef->msgAssignmentChainDisplayName.pEditorCopy->pcDescription ||
		(stricmp(estrTmp, pDef->msgAssignmentChainDisplayName.pEditorCopy->pcDescription) != 0)) {
			pDef->msgAssignmentChainDisplayName.pEditorCopy->pcDescription = StructAllocString(estrTmp);
	}

	estrPrintf(&estrTmp, "ItemAssignment");
	if(!pDef->msgAssignmentChainDisplayName.pEditorCopy->pcScope ||
		(stricmp(estrTmp, pDef->msgAssignmentChainDisplayName.pEditorCopy->pcScope) != 0)) {
			pDef->msgAssignmentChainDisplayName.pEditorCopy->pcScope = allocAddString(estrTmp);
	}

	// Fixup assignment chain description
	if(!pDef->msgAssignmentChainDescription.pEditorCopy)
	{
		pDef->msgAssignmentChainDescription.pEditorCopy = StructCreate(parse_Message);
	}

	estrPrintf(&estrTmp, "ItemAssignment.%s.ChainDescription", pDef->pchName);
	if(!pDef->msgAssignmentChainDescription.pEditorCopy->pcMessageKey ||
		(stricmp(estrTmp, pDef->msgAssignmentChainDescription.pEditorCopy->pcMessageKey) != 0)) {
			pDef->msgAssignmentChainDescription.pEditorCopy->pcMessageKey = allocAddString(estrTmp);
	}

	estrPrintf(&estrTmp, "Description for an item assignment chain");
	if(!pDef->msgAssignmentChainDescription.pEditorCopy->pcDescription ||
		(stricmp(estrTmp, pDef->msgAssignmentChainDescription.pEditorCopy->pcDescription) != 0)) {
			pDef->msgAssignmentChainDescription.pEditorCopy->pcDescription = StructAllocString(estrTmp);
	}

	estrPrintf(&estrTmp, "ItemAssignment");
	if(!pDef->msgAssignmentChainDescription.pEditorCopy->pcScope ||
		(stricmp(estrTmp, pDef->msgAssignmentChainDescription.pEditorCopy->pcScope) != 0)) {
			pDef->msgAssignmentChainDescription.pEditorCopy->pcScope = allocAddString(estrTmp);
	}

	// Fixup outcome messages
	for (i = 0; i < eaSize(&pDef->eaOutcomes); i++)
	{
		ItemAssignmentOutcome* pOutcome = pDef->eaOutcomes[i];

		if(!pOutcome->msgDisplayName.pEditorCopy)
		{
			pOutcome->msgDisplayName.pEditorCopy = StructCreate(parse_Message);
		}
		if(!pOutcome->msgDescription.pEditorCopy)
		{
			pOutcome->msgDescription.pEditorCopy = StructCreate(parse_Message);
		}

		// Display name
		estrPrintf(&estrTmp, "ItemAssignmentOutcome.%s.%s", pDef->pchName, pOutcome->pchName);
		if(!pOutcome->msgDisplayName.pEditorCopy->pcMessageKey ||
			(stricmp(estrTmp, pOutcome->msgDisplayName.pEditorCopy->pcMessageKey) != 0)) {
				pOutcome->msgDisplayName.pEditorCopy->pcMessageKey = allocAddString(estrTmp);
		}

		estrPrintf(&estrTmp, "Display name for an item assignment outcome");
		if(!pOutcome->msgDisplayName.pEditorCopy->pcDescription ||
			(stricmp(estrTmp, pOutcome->msgDisplayName.pEditorCopy->pcDescription) != 0)) {
				pOutcome->msgDisplayName.pEditorCopy->pcDescription = StructAllocString(estrTmp);
		}

		estrPrintf(&estrTmp, "ItemAssignmentOutcome");
		if(!pOutcome->msgDisplayName.pEditorCopy->pcScope ||
			(stricmp(estrTmp, pOutcome->msgDisplayName.pEditorCopy->pcScope) != 0)) {
				pOutcome->msgDisplayName.pEditorCopy->pcScope = allocAddString(estrTmp);
		}

		// Display description
		estrPrintf(&estrTmp, "ItemAssignmentOutcome.%s.%s.Description", pDef->pchName, pOutcome->pchName);
		if(!pOutcome->msgDescription.pEditorCopy->pcMessageKey ||
			(stricmp(estrTmp, pOutcome->msgDescription.pEditorCopy->pcMessageKey) != 0)) {
				pOutcome->msgDescription.pEditorCopy->pcMessageKey = allocAddString(estrTmp);
		}

		estrPrintf(&estrTmp, "Description for an item assignment outcome");
		if(!pOutcome->msgDescription.pEditorCopy->pcDescription ||
			(stricmp(estrTmp, pOutcome->msgDescription.pEditorCopy->pcDescription) != 0)) {
				pOutcome->msgDescription.pEditorCopy->pcDescription = StructAllocString(estrTmp);
		}

		estrPrintf(&estrTmp, "ItemAssignmentOutcome");
		if(!pOutcome->msgDescription.pEditorCopy->pcScope ||
			(stricmp(estrTmp, pOutcome->msgDescription.pEditorCopy->pcScope) != 0)) {
				pOutcome->msgDescription.pEditorCopy->pcScope = allocAddString(estrTmp);
		}

		if (pOutcome->pResults)
		{
			if (!pOutcome->pResults->msgNewAssignmentDescription.pEditorCopy)
			{
				pOutcome->pResults->msgNewAssignmentDescription.pEditorCopy = StructCreate(parse_Message);
			}
			if (!pOutcome->pResults->msgDestroyDescription.pEditorCopy)
			{
				pOutcome->pResults->msgDestroyDescription.pEditorCopy = StructCreate(parse_Message);
			}

			// New assignment description
			estrPrintf(&estrTmp, "ItemAssignmentOutcomeResults.%s.%s.NewAssignment", pDef->pchName, pOutcome->pchName);
			if(!pOutcome->pResults->msgNewAssignmentDescription.pEditorCopy->pcMessageKey ||
				(stricmp(estrTmp, pOutcome->pResults->msgNewAssignmentDescription.pEditorCopy->pcMessageKey) != 0)) {
					pOutcome->pResults->msgNewAssignmentDescription.pEditorCopy->pcMessageKey = allocAddString(estrTmp);
			}

			estrPrintf(&estrTmp, "Description for why an item was placed on a new assignment");
			if(!pOutcome->pResults->msgNewAssignmentDescription.pEditorCopy->pcDescription ||
				(stricmp(estrTmp, pOutcome->pResults->msgNewAssignmentDescription.pEditorCopy->pcDescription) != 0)) {
					pOutcome->pResults->msgNewAssignmentDescription.pEditorCopy->pcDescription = StructAllocString(estrTmp);
			}

			estrPrintf(&estrTmp, "ItemAssignmentOutcomeResults");
			if(!pOutcome->pResults->msgNewAssignmentDescription.pEditorCopy->pcScope ||
				(stricmp(estrTmp, pOutcome->pResults->msgNewAssignmentDescription.pEditorCopy->pcScope) != 0)) {
					pOutcome->pResults->msgNewAssignmentDescription.pEditorCopy->pcScope = allocAddString(estrTmp);
			}

			// Destroy description
			estrPrintf(&estrTmp, "ItemAssignmentOutcomeResults.%s.%s.Destroy", pDef->pchName, pOutcome->pchName);
			if(!pOutcome->pResults->msgDestroyDescription.pEditorCopy->pcMessageKey ||
				(stricmp(estrTmp, pOutcome->pResults->msgDestroyDescription.pEditorCopy->pcMessageKey) != 0)) {
					pOutcome->pResults->msgDestroyDescription.pEditorCopy->pcMessageKey = allocAddString(estrTmp);
			}

			estrPrintf(&estrTmp, "Description for why an item was destroyed");
			if(!pOutcome->pResults->msgDestroyDescription.pEditorCopy->pcDescription ||
				(stricmp(estrTmp, pOutcome->pResults->msgDestroyDescription.pEditorCopy->pcDescription) != 0)) {
					pOutcome->pResults->msgDestroyDescription.pEditorCopy->pcDescription = StructAllocString(estrTmp);
			}

			estrPrintf(&estrTmp, "ItemAssignmentOutcomeResults");
			if(!pOutcome->pResults->msgDestroyDescription.pEditorCopy->pcScope ||
				(stricmp(estrTmp, pOutcome->pResults->msgDestroyDescription.pEditorCopy->pcScope) != 0)) {
					pOutcome->pResults->msgDestroyDescription.pEditorCopy->pcScope = allocAddString(estrTmp);
			}
		}
	}
	estrDestroy(&estrTmp);
}

static void *iae_CreateObject(METable *pTable, ItemAssignmentDef *pObjectToClone, char *pcNewName, bool bCloneKeepsKeys)
{
	ItemAssignmentDef *pNewDef = NULL;
	const char *pcBaseName;
	char *estrTmp = NULL;

	estrStackCreate(&estrTmp);

	// Create the object
	if (pObjectToClone) {
		pNewDef = StructCreate(parse_ItemAssignmentDef);
		
		StructCopyAll(parse_ItemAssignmentDef,pObjectToClone,pNewDef);
		pcBaseName = pObjectToClone->pchName;
	} else {
		pNewDef = StructCreate(parse_ItemAssignmentDef);

		pcBaseName = "_New_ItemAssignmentDef";
	}
	// Use provided name if available
	if (pcNewName) {
		pcBaseName = pcNewName;
	}

	assertmsg(pNewDef, "Failed to create ItemAssignmentDef");

	// Assign a new name
	pNewDef->pchName = (char*)METableMakeNewNameShared(pTable, pcBaseName, true);

	// Assign a file
	estrPrintf(&estrTmp, ITEM_ASSIGNMENT_BASE_DIR"/%s."ITEM_ASSIGNMENT_EXT,pNewDef->pchName);
	pNewDef->pchFileName = (char*)allocAddString(estrTmp);

	estrDestroy(&estrTmp);

	return pNewDef;
}

static void *iae_CreateOutcome(METable *pTable, ItemAssignmentOutcome *pOutcome, ItemAssignmentOutcome *pOutcomeToClone, ItemAssignmentOutcome *pBeforeOutcome, ItemAssignmentOutcome *pAfterOutcome)
{
	ItemAssignmentOutcome *pNewOutcome;

	// Allocate the object
	if (pOutcomeToClone) {
		pNewOutcome = (ItemAssignmentOutcome*)StructClone(parse_ItemAssignmentOutcome, pOutcomeToClone);
	} else {
		pNewOutcome = (ItemAssignmentOutcome*)StructCreate(parse_ItemAssignmentOutcome);
	}

	assertmsg(pNewOutcome, "Failed to create item assignment outcome");
	
	return pNewOutcome;
}

static void *iae_CreateSlot(METable *pTable, ItemAssignmentSlot *pSlot, ItemAssignmentSlot *pSlotToClone, ItemAssignmentSlot *pBeforeSlot, ItemAssignmentSlot *pAfterSlot)
{
	ItemAssignmentSlot *pNewSlot;
	
	// Allocate the object
	if (pSlotToClone) {
		pNewSlot = (ItemAssignmentSlot*)StructClone(parse_ItemAssignmentSlot, pSlotToClone);
	} else {
		pNewSlot = (ItemAssignmentSlot*)StructCreate(parse_ItemAssignmentSlot);
	}
	
	assertmsg(pNewSlot, "Failed to create item assignment slot");
	
	return pNewSlot;
}

static void *iae_CreateOutcomeModifier(METable *pTable, ItemAssignmentOutcomeModifier *pOutcomeModifier, ItemAssignmentOutcomeModifier *pOutcomeModifierToClone, ItemAssignmentOutcomeModifier *pBeforeOutcomeModifier, ItemAssignmentOutcomeModifier *pAfterOutcomeModifier)
{
	ItemAssignmentOutcomeModifier *pNewOutcomeModifier;
	
	// Allocate the object
	if (pOutcomeModifierToClone) {
		pNewOutcomeModifier = (ItemAssignmentOutcomeModifier*)StructClone(parse_ItemAssignmentOutcomeModifier, pOutcomeModifierToClone);
	} else {
		pNewOutcomeModifier = (ItemAssignmentOutcomeModifier*)StructCreate(parse_ItemAssignmentOutcomeModifier);
	}
	
	assertmsg(pNewOutcomeModifier, "Failed to create item assignment outcome modifier");
	
	return pNewOutcomeModifier;
}

static void *iae_CreateItemCost(METable *pTable, ItemAssignmentItemCost *pItemCost, ItemAssignmentItemCost *pItemCostToClone, ItemAssignmentItemCost *pBeforeItemCost, ItemAssignmentItemCost *pAfterItemCost)
{
	ItemAssignmentItemCost *pNewItemCost;
	
	// Allocate the object
	if (pItemCostToClone) {
		pNewItemCost = (ItemAssignmentItemCost*)StructClone(parse_ItemAssignmentItemCost, pItemCostToClone);
	} else {
		pNewItemCost = (ItemAssignmentItemCost*)StructCreate(parse_ItemAssignmentItemCost);
	}
	
	assertmsg(pNewItemCost, "Failed to create item assignment item cost");
	
	return pNewItemCost;
}

static char** iae_GetMaps(METable *pTable, void *pUnused)
{
	char **eaResult = NULL;
	S32 iIdx, iSize = eaSize(&g_GEMapDispNames);
	for(iIdx = 0; iIdx < iSize; iIdx++)
	{
		eaPush(&eaResult, strdup(g_GEMapDispNames[iIdx]));
	}
	return(eaResult);
}

static char** iae_GetOutcomes(METable *pTable, void *pUnused)
{
	char **eaResult = NULL;
	ItemAssignmentDef* pDef = pTable->eaRows[pTable->iEditRow]->pObject;
	if (pDef)
	{
		S32 iIdx, iSize = eaSize(&pDef->eaOutcomes);
		for(iIdx = 0; iIdx < iSize; iIdx++)
		{
			eaPush(&eaResult, strdup(pDef->eaOutcomes[iIdx]->pchName));
		}
	}
	return eaResult;
}

static char** iae_GetModifiers(METable *pTable, void *pUnused)
{
	char **eaResult = NULL;
	ItemAssignmentDef* pDef = pTable->eaRows[pTable->iEditRow]->pObject;
	if (pDef)
	{
		S32 iIdx, iSize = eaSize(&pDef->eaModifiers);
		for(iIdx = 0; iIdx < iSize; iIdx++)
		{
			eaPush(&eaResult, strdup(pDef->eaModifiers[iIdx]->pchName));
		}
	}
	return eaResult;
}

static char** iae_GetActivities(METable *pTable, void *pUnused)
{
	char** eaResult = NULL;
	int i, iSize = eaSize(&g_ActivityDefs.ppDefs);

	eaSetSize(&eaResult, iSize);

	for(i = 0; i < iSize; i++)
	{
		eaResult[i] = strdup(g_ActivityDefs.ppDefs[i]->pchActivityName);
	}
	return eaResult;
}

static void iae_InitColumns(METable *pTable)
{
	METableAddSimpleColumn(pTable, "Name", "name", 150, NULL, kMEFieldType_TextEntry);

	// Lock in name column
	METableSetNumLockedColumns(pTable, 2);

	// Basic ItemAssignment Fields
	METableAddSimpleColumn(pTable, "Display Name", ".DisplayName.EditorCopy", 160, IA_GROUP_MAIN, kMEFieldType_Message);
	METableAddSimpleColumn(pTable, "Description", ".Description.EditorCopy", 160, IA_GROUP_MAIN, kMEFieldType_Message);
	METableAddSimpleColumn(pTable, "Chain Display Name", ".AssignmentChainDisplayName.EditorCopy", 160, IA_GROUP_MAIN, kMEFieldType_Message);
	METableAddSimpleColumn(pTable, "Chain Description", ".AssignmentChainDescription.EditorCopy", 160, IA_GROUP_MAIN, kMEFieldType_Message);
	METableAddScopeColumn(pTable, "Scope", "Scope", 160, IA_GROUP_MAIN, kMEFieldType_TextEntry); // Not validated on purpose
	METableAddFileNameColumn(pTable, "File Name", "FileName", 210, IA_GROUP_MAIN, NULL, ITEM_ASSIGNMENT_BASE_DIR, ITEM_ASSIGNMENT_BASE_DIR, "."ITEM_ASSIGNMENT_EXT, UIBrowseNewOrExisting);
	METableAddSimpleColumn(pTable, "UI Sort Order", "SortOrder", 160, IA_GROUP_MAIN, kMEFieldType_TextEntry);
	METableAddColumn(pTable, "Icon", "Icon", 160, IA_GROUP_MAIN, kMEFieldType_Texture, NULL, NULL, NULL, NULL, "texture_library/ui/Icons", NULL, NULL);
	METableAddColumn(pTable, "Image", "Image", 160, IA_GROUP_MAIN, kMEFieldType_Texture, NULL, NULL, NULL, NULL, "texture_library/ui/Icons", NULL, NULL);
	METableAddColumn(pTable, "Featured Activity", "FeaturedActivity", 180, IA_GROUP_MAIN, kMEFieldType_ValidatedTextEntry, NULL, NULL, NULL, NULL, NULL, NULL, iae_GetActivities);
	METableAddEnumColumn(pTable, "Category", "Category", 120, IA_GROUP_MAIN, kMEFieldType_Combo, ItemAssignmentCategoryEnum);
	METableAddSimpleColumn(pTable, "Completion Experience", "CompletionExperience", 160, IA_GROUP_MAIN, kMEFieldType_TextEntry);
	METableAddSimpleColumn(pTable, "Duration", "Duration", 160, IA_GROUP_MAIN, kMEFieldType_TextEntry);
	METableAddSimpleColumn(pTable, "Cooldown After Completion", "CooldownAfterCompletion", 160, IA_GROUP_MAIN, kMEFieldType_TextEntry);
	METableAddSimpleColumn(pTable, "Minimum Slotted Items", "MinimumSlottedItems", 160, IA_GROUP_MAIN, kMEFieldType_TextEntry);
	METableAddSimpleColumn(pTable, "Assignment Point Cost", "AssignmentPointCost", 160, IA_GROUP_MAIN, kMEFieldType_TextEntry);
	METableAddEnumColumn(pTable, "Weight", "Weight", 120, IA_GROUP_MAIN, kMEFieldType_Combo, ItemAssignmentWeightTypeEnum);
	METableAddSimpleColumn(pTable, "Max Active Unique", "UniqueAssignmentCount", 160, IA_GROUP_REQUIREMENTS, kMEFieldType_TextEntry);

	// Numeric scaling
	METableAddEnumColumn(pTable, "Duration Scale Category", "NumericDurationScaleCategory", 120, IA_GROUP_SCALING, kMEFieldType_Combo, ItemAssignmentDurationScaleCategoryEnum);

	// Requirements
	METableAddSimpleColumn(pTable, "Minimum Level", ".Requirements.MinimumLevel", 160, IA_GROUP_REQUIREMENTS, kMEFieldType_TextEntry);
	METableAddSimpleColumn(pTable, "Maximum Level", ".Requirements.MaximumLevel", 160, IA_GROUP_REQUIREMENTS, kMEFieldType_TextEntry);
	METableAddColumn(pTable, "Required Maps", ".Requirements.RequiredMap", 100, IA_GROUP_REQUIREMENTS, kMEFieldType_ValidatedTextEntry, NULL, NULL, NULL, NULL, NULL, NULL, iae_GetMaps);
	METableAddEnumColumn(pTable, "Required Map Types", ".Requirements.RequiredMapType", 150, IA_GROUP_REQUIREMENTS, kMEFieldType_FlagCombo, ZoneMapTypeEnum);
	METableAddEnumColumn(pTable, "Required Region Types", ".Requirements.RequiredRegionType", 150, IA_GROUP_REQUIREMENTS, kMEFieldType_FlagCombo, WorldRegionTypeEnum);
	METableAddSimpleColumn(pTable, "Required Volumes", ".Requirements.RequiredVolume", 100, IA_GROUP_REQUIREMENTS, kMEFieldType_ValidatedTextEntry);
	METableAddDictColumn(pTable, "Required Allegiance", ".Requirements.RequiredAllegiance", 100, IA_GROUP_REQUIREMENTS, kMEFieldType_ValidatedTextEntry, "Allegiance", parse_AllegianceDef, "Name");
	METableAddGlobalDictColumn(pTable, "Required Numeric Item", ".Requirements.RequiredNumericItem", 100, IA_GROUP_REQUIREMENTS, kMEFieldType_ValidatedTextEntry, "ItemDef", "resourceName");
	METableAddSimpleColumn(pTable, "Required Numeric Value", ".Requirements.RequiredNumericValue", 160, IA_GROUP_REQUIREMENTS, kMEFieldType_TextEntry);
	METableAddGlobalDictColumn(pTable, "Required Mission", ".Requirements.RequiredMission", 100, IA_GROUP_REQUIREMENTS, kMEFieldType_ValidatedTextEntry, "Mission", "resourceName");
	METableAddDictColumn(pTable, "Required Item Assignment", ".Requirements.RequiredItemAssignment", 100, IA_GROUP_REQUIREMENTS, kMEFieldType_ValidatedTextEntry, "ItemAssignmentDef", parse_ItemAssignmentDef, "Name");
	METableAddExprColumn(pTable, "Requires Expression", ".Requirements.RequiresBlock", 160, IA_GROUP_REQUIREMENTS, ItemAssignments_GetContext(NULL));

	// Flags
	METableAddSimpleColumn(pTable, "Is Abortable", "IsAbortable", 120, IA_GROUP_FLAGS, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Disabled", "Disabled", 120, IA_GROUP_FLAGS, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Allow Item Unslotting", "AllowItemUnslotting", 120, IA_GROUP_FLAGS, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Repeatable", "Repeatable", 120, IA_GROUP_FLAGS, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Can Start Remotely", "CanStartRemotely", 120, IA_GROUP_FLAGS, kMEFieldType_BooleanCombo);
}

static void iae_InitOutcomeColumns(METable *pTable)
{
	int id;
	iaeOutcomeId = id = METableCreateSubTable(pTable, "Outcome", "Outcome", parse_ItemAssignmentOutcome, NULL, NULL, NULL, iae_CreateOutcome);
	METableAddSimpleSubColumn(pTable, id, "Outcome", NULL, 80, NULL, 0);
	METableSetSubColumnState(pTable, id, "Outcome", ME_STATE_LABEL);
	
	// Lock in label column
	METableSetNumLockedSubColumns(pTable, id, 2);
	
	// Name
	METableAddSimpleSubColumn(pTable, id, "Name", "Name", 150, NULL, kMEFieldType_TextEntry);
	
	// Display Messages
	METableAddSimpleSubColumn(pTable, id, "Display Name", ".DisplayName.EditorCopy", 100, IA_SUBGROUP_OUTCOME, kMEFieldType_Message);
	METableAddSimpleSubColumn(pTable, id, "Description", ".Description.EditorCopy", 100, IA_SUBGROUP_OUTCOME, kMEFieldType_Message);
	
	// Weights
	METableAddEnumSubColumn(pTable, id, "Base Weight", "BaseWeight", 120, IA_SUBGROUP_OUTCOME, kMEFieldType_Combo, ItemAssignmentWeightTypeEnum);

	// Numeric Scaling
	METableAddExprSubColumn(pTable, id, "Scale All Numerics", "ScaleAllNumericRewards", 160, IA_SUBGROUP_OUTCOME, ItemAssignments_GetContext(NULL));

	// Results
	METableAddGlobalDictSubColumn(pTable, id, "Grant Reward Table", ".Results.GrantRewardTable", 100, IA_SUBGROUP_OUTCOME, kMEFieldType_ValidatedTextEntry, "RewardTable", "resourceName");
	METableAddDictSubColumn(pTable, id, "New Item Assignment", ".Results.NewAssignment", 100, IA_SUBGROUP_OUTCOME, kMEFieldType_ValidatedTextEntry, "ItemAssignmentDef", parse_ItemAssignmentDef, "Name");
	METableAddSimpleSubColumn(pTable, id, "New Assignment Chance", ".Results.NewAssignmentChance", 160, IA_SUBGROUP_OUTCOME, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id, "New Assignment Description", ".Results.NewAssignmentDescription.EditorCopy", 160, IA_SUBGROUP_OUTCOME, kMEFieldType_Message);
	METableAddEnumSubColumn(pTable, id, "Destroy Items Of Quality", ".Results.DestroyItemsOfQuality", 160, IA_SUBGROUP_OUTCOME, kMEFieldType_FlagCombo, ItemQualityEnum);
	METableAddEnumSubColumn(pTable, id, "Destroy Items Of Category", ".Results.DestroyItemsOfCategory", 160, IA_SUBGROUP_OUTCOME, kMEFieldType_FlagCombo, ItemCategoryEnum);
	METableAddSimpleSubColumn(pTable, id, "Destroy Item Chance", ".Results.DestroyChance", 160, IA_SUBGROUP_OUTCOME, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id, "Destroy Description", ".Results.DestroyDescription.EditorCopy", 160, IA_SUBGROUP_OUTCOME, kMEFieldType_Message);
}

static void iae_InitSlotColumns(METable *pTable)
{
	int id;
	iaeSlotId = id = METableCreateSubTable(pTable, "Slot", "Slot", parse_ItemAssignmentSlot, NULL, NULL, NULL, iae_CreateSlot);
	METableAddSimpleSubColumn(pTable, id, "Slot", NULL, 80, NULL, 0);
	METableSetSubColumnState(pTable, id, "Slot", ME_STATE_LABEL);
	
	// Lock in label column
	METableSetNumLockedSubColumns(pTable, id, 2);

	// Slot Fields
	METableAddEnumSubColumn(pTable, id, "Required Item Categories", "RequiredItemCategory", 120, IA_SUBGROUP_SLOT, kMEFieldType_FlagCombo, ItemCategoryEnum);
	METableAddEnumSubColumn(pTable, id, "Restrict Item Categories", "RestrictItemCategory", 120, IA_SUBGROUP_SLOT, kMEFieldType_FlagCombo, ItemCategoryEnum);
	METableAddSubColumn(pTable, id, "Outcome Modifiers", "OutcomeModifier", NULL, 100, IA_SUBGROUP_SLOT, kMEFieldType_ValidatedTextEntry, NULL, NULL, NULL, NULL, NULL, NULL, iae_GetModifiers);
	METableAddSubColumn(pTable, id, "Icon", "Icon", NULL, 160, IA_SUBGROUP_SLOT, kMEFieldType_Texture, NULL, NULL, NULL, NULL, "texture_library/ui/Icons", NULL, NULL);
	METableAddSimpleSubColumn(pTable, id, "Is Optional", "IsOptional", 120, IA_SUBGROUP_SLOT, kMEFieldType_BooleanCombo);
}

static void iae_InitModifierColumns(METable *pTable)
{
	int id;
	iaeModifierId = id = METableCreateSubTable(pTable, "OutcomeModifier", "OutcomeModifier", parse_ItemAssignmentOutcomeModifier, NULL, NULL, NULL, iae_CreateOutcomeModifier);
	METableAddSimpleSubColumn(pTable, id, "OutcomeModifier", NULL, 80, NULL, 0);
	METableSetSubColumnState(pTable, id, "OutcomeModifier", ME_STATE_LABEL);
	
	// Lock in label column
	METableSetNumLockedSubColumns(pTable, id, 2);

	// Name
	METableAddSimpleSubColumn(pTable, id, "Name", "Name", 150, NULL, kMEFieldType_TextEntry);

	// Type
	METableAddEnumSubColumn(pTable, id, "Modifier Type", "ModifierType", 160, IA_SUBGROUP_OUTCOME, kMEFieldType_Combo, ItemAssignmentOutcomeModifierTypeEnum);

	// Outcome Modifier Fields
	METableAddEnumSubColumn(pTable, id, "Affected Item Categories", "ItemCategory", 120, IA_SUBGROUP_MODIFIER, kMEFieldType_FlagCombo, ItemCategoryEnum);
}

static void iae_InitItemCostColumns(METable *pTable)
{
	int id;
	iaeItemCostId = id = METableCreateSubTable(pTable, "ItemCost", ".Requirements.ItemCost", parse_ItemAssignmentItemCost, NULL, NULL, NULL, iae_CreateItemCost);
	METableAddSimpleSubColumn(pTable, id, "ItemCost", NULL, 80, NULL, 0);
	METableSetSubColumnState(pTable, id, "ItemCost", ME_STATE_LABEL);
	
	// Lock in label column
	METableSetNumLockedSubColumns(pTable, id, 2);


	METableAddGlobalDictSubColumn(pTable, id, "Item", "Item", 100, IA_SUBGROUP_ITEMCOST, kMEFieldType_ValidatedTextEntry, "ItemDef", "resourceName");
	METableAddSimpleSubColumn(pTable, id, "Count", "Count", 160, IA_SUBGROUP_ITEMCOST, kMEFieldType_TextEntry);
}

static void *iae_WindowCreateCallback(MEWindow *pWindow, ItemAssignmentDef *pObjectToClone)
{
	return iae_CreateObject(pWindow->pTable, pObjectToClone, NULL, false);
}

static int iae_ValidateCallback(METable *pTable, ItemAssignmentDef *pDef, void *pUserData)
{
	char buf[1024];

	if (pDef->pchName[0] == '_') {
		sprintf(buf, "The ItemAssignmentDef '%s' cannot have a name starting with an underscore.", pDef->pchName);
		ui_DialogPopup("Validation Error", buf);
		return 0;
	}

	return ItemAssignment_Validate(pDef);
}

static void iae_PostOpenCallback(METable *pTable, ItemAssignmentDef *pDef, ItemAssignmentDef *pOrigDef)
{
	iae_FixMessages(pDef);
	if (pOrigDef) {
		iae_FixMessages(pOrigDef);
	}

	METableRefreshRow(pTable, pDef);
}

static void iae_PreSaveCallback(METable *pTable, ItemAssignmentDef *pDef)
{
	iae_FixMessages(pDef);
}

static void *iae_TableCreateCallback(METable *pTable, ItemAssignmentDef *pObjectToClone, bool bCloneKeepsKeys)
{
	return iae_CreateObject(pTable, pObjectToClone, NULL, bCloneKeepsKeys);
}

static void iae_DictChangeCallback(enumResourceEventType eType, const char *pDictName, ConstReferenceData pRefData, Referent pReferent, METable *pTable)
{
	METableDictChanged(pTable, eType, pReferent, pRefData);
}

static void iae_MessageDictChangeCallback(enumResourceEventType eType, const char *pDictName, const char *pcMessageKey, Referent pReferent, METable *pTable)
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

static void iae_InitCallbacks(MEWindow *pWindow, METable *pTable)
{
	// General Window callbacks
	MEWindowSetCreateCallback(pWindow, iae_WindowCreateCallback);

	// General table callbacks
	METableSetValidateCallback(pTable, iae_ValidateCallback, pTable);
	METableSetPostOpenCallback(pTable, iae_PostOpenCallback);
	METableSetPreSaveCallback(pTable, iae_PreSaveCallback);
	METableSetCreateCallback(pTable, iae_TableCreateCallback);

	// We need this registered here instead of by METable because the dictionary will 
	// only allow each callback function to be registered once and there may be multiple
	// METable instances.  Our local callback just passes through to the METable.
	resDictRegisterEventCallback(g_hItemAssignmentDict, iae_DictChangeCallback, pTable);
	resDictRegisterEventCallback(gMessageDict, iae_MessageDictChangeCallback, pTable);
}

static void iae_Init(MultiEditEMDoc *pEditorDoc)
{
	if (!s_iaeWindow) {
		// Create the editor window
		s_iaeWindow = MEWindowCreate("Item Assignment Editor", "Item Assignment", "Item Assignments", SEARCH_TYPE_ITEMASSIGNMENT, g_hItemAssignmentDict, parse_ItemAssignmentDef, "Name", "FileName", "Scope", pEditorDoc);

		// Add ItemAssignment specific columns
		iae_InitColumns(s_iaeWindow->pTable);

		// Add ItemAssignment specific sub-columns
		iae_InitOutcomeColumns(s_iaeWindow->pTable);
		iae_InitSlotColumns(s_iaeWindow->pTable);
		iae_InitModifierColumns(s_iaeWindow->pTable);
		iae_InitItemCostColumns(s_iaeWindow->pTable);

		METableFinishColumns(s_iaeWindow->pTable);

		// Init the menus after adding all the columns
		MEWindowInitTableMenus(s_iaeWindow);

		// Set the callbacks
		iae_InitCallbacks(s_iaeWindow, s_iaeWindow->pTable);

		// Set edit mode
		resSetDictionaryEditMode(g_hRewardTableDict, true);
	}

	// Show the window
	ui_WindowPresent(s_iaeWindow->pUIWindow);
}

//---------------------------------------------------------------------------------------------------
// Public Interface
//---------------------------------------------------------------------------------------------------

MEWindow *itemAssignmentEditor_Init(MultiEditEMDoc *pEditorDoc) 
{
	iae_Init(pEditorDoc);	
	return s_iaeWindow;
}

void itemAssignmentEditor_CreateItemAssignment(char *pcName)
{
	// Create a new object since it is not in the dictionary
	// Add the object as a new object with no old
	void *pObject = iae_CreateObject(s_iaeWindow->pTable, NULL, pcName, false);
	METableAddRowByObject(s_iaeWindow->pTable, pObject, 1, 1);
}

#endif