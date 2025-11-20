/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef NO_EDITORS

#include "aiJobs.h"
#include "aiStructCommon.h"
#include "ChoiceTable_common.h"
#include "EditorPrefs.h"
#include "encounter_common.h"
#include "EncounterTemplateEditor.h"
#include "entCritter.h"
#include "Entity.h"
#include "Expression.h"
#include "GameEditorShared.h"
#include "InteractionEditor.h"
#include "MultiEditField.h"
#include "StateMachine.h"
#include "StringCache.h"
#include "UIGimmeButton.h"
#include "UIStyle.h"
#include "WorldGrid.h"
#include "GameActionEditor.h"

#include "AutoGen/EncounterTemplateEditor_h_ast.h"


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););


//-----------------------------------------------------------------------------------
// Global Data
//-----------------------------------------------------------------------------------

static bool gInitializedEditor = false;
static bool gInitializedEditorData = false;
static bool gIndexChanged = false;
static bool gDataChanged = false;

static char **geaScopes = NULL;

extern EMEditor s_EncounterTemplateEditor;

static UISkin *gBoldExpanderSkin;

static EncounterLevelProperties *gDefaultLevelProperties;
static EncounterSpawnProperties *gDefaultSpawnProperties;
static EncounterActorSharedProperties *gDefaultActorSharedProperties;
static EncounterAIProperties *gDefaultAIProperties;
static EncounterWaveProperties *gDefaultWaveProperties;
static EncounterDifficultyProperties *gDefaultDifficultyProperties;

static UIButton *s_pButton1Col;
static UIButton *s_pButton2Col;


//---------------------------------------------------------------------------------------------------
// Function Prototypes and type definitions
//---------------------------------------------------------------------------------------------------

#define X_OFFSET_BASE    15
#define X_OFFSET_INDENT  30
#define X_OFFSET_CONTROL 125
#define X_OFFSET_DIFFICULTY_TEAM 100
#define X_OFFSET_DIFFICULTY_VALUE 90
#define X_OFFSET_DIFFICULTY_CELL 40

#define X_PERCENT_SPLIT	0.55

#define STANDARD_ROW_HEIGHT	28
#define LABEL_ROW_HEIGHT	20
#define SEPARATOR_HEIGHT	11

#define ET_PREF_EDITOR_NAME	"Encounter Template Editor"
#define ET_PREF_CAT_UI		"UI"

#define ETCreateLabel(pcText,pcTooltip,x,y,pExpander)  ETRefreshLabel(NULL, (pcText), (pcTooltip), (x), 0, (y), (pExpander))

static void ETFieldChangedCB(MEField *pField, bool bFinished, EncounterTemplateEditDoc *pDoc);
static bool ETFieldPreChangeCB(MEField *pField, bool bFinished, EncounterTemplateEditDoc *pDoc);
static void ETTemplateChanged(EncounterTemplateEditDoc *pDoc, bool bUndoable);
static void ETTemplatePreSaveFixup(EncounterTemplate *pTemplate);
static void ETUpdateDisplay(EncounterTemplateEditDoc *pDoc);
static void ETCritterTypeChangedCB(MEField *pField, bool bFinished, EncounterTemplateEditDoc *pDoc);
static void ETFillEmptyStructs(EncounterTemplate *pTemplate);


//---------------------------------------------------------------------------------------------------
// Data Manipulation
//---------------------------------------------------------------------------------------------------

static int ETStringCompare(const char** left, const char** right)
{
	return stricmp(*left,*right);
}


static void ETTemplateUndoCB(EncounterTemplateEditDoc *pDoc, ETUndoData *pData)
{
	// Put the undo template into the editor
	StructDestroy(parse_EncounterTemplate, pDoc->pTemplate);
	pDoc->pTemplate = StructClone(parse_EncounterTemplate, pData->pPreTemplate);
	if (pDoc->pNextUndoTemplate) {
		StructDestroy(parse_EncounterTemplate, pDoc->pNextUndoTemplate);
	}
	pDoc->pNextUndoTemplate = StructClone(parse_EncounterTemplate, pDoc->pTemplate);

	// Update the UI
	ETTemplateChanged(pDoc, false);
}


static void ETTemplateRedoCB(EncounterTemplateEditDoc *pDoc, ETUndoData *pData)
{
	// Put the undo template into the editor
	StructDestroy(parse_EncounterTemplate, pDoc->pTemplate);
	pDoc->pTemplate = StructClone(parse_EncounterTemplate, pData->pPostTemplate);
	if (pDoc->pNextUndoTemplate) {
		StructDestroy(parse_EncounterTemplate, pDoc->pNextUndoTemplate);
	}
	pDoc->pNextUndoTemplate = StructClone(parse_EncounterTemplate, pDoc->pTemplate);

	// Update the UI
	ETTemplateChanged(pDoc, false);
}


static void ETTemplateUndoFreeCB(EncounterTemplateEditDoc *pDoc, ETUndoData *pData)
{
	// Free the memory
	StructDestroy(parse_EncounterTemplate, pData->pPreTemplate);
	StructDestroy(parse_EncounterTemplate, pData->pPostTemplate);
	free(pData);
}


static void ETIndexChangedCB(void *unused)
{
	if (gIndexChanged) {
		gIndexChanged = false;
		resGetUniqueScopes(g_hEncounterTemplateDict, &geaScopes);
	}
}


static void ETDataChangedCB(void *unused) 
{
	if (gDataChanged) {
		int i;

		gDataChanged = false;
		for(i=eaSize(&s_EncounterTemplateEditor.open_docs)-1; i>=0; --i) {
			ETUpdateDisplay((EncounterTemplateEditDoc*)s_EncounterTemplateEditor.open_docs[i]);
		}
	}
}


static void ETContentDictChanged(enumResourceEventType eType, const char *pDictName, const char *pcName, Referent pReferent, void *pUserData)
{
	if ((eType == RESEVENT_INDEX_MODIFIED) && !gIndexChanged) {
		gIndexChanged = true;
		emQueueFunctionCall(ETIndexChangedCB, NULL);
	}
}


static void ETDataDictChanged(enumResourceEventType eType, const char *pDictName, const char *pcName, Referent pReferent, void *pUserData)
{
	if (!gDataChanged && ((eType == RESEVENT_RESOURCE_ADDED) || (eType == RESEVENT_RESOURCE_MODIFIED))) {
		gDataChanged = true;
		emQueueFunctionCall(ETDataChangedCB, NULL);
	}
}


static void ETFreeOverrideVarGroup(ETOverrideVarGroup *pGroup)
{
	ui_WidgetQueueFreeAndNull(&pGroup->pOverrideButton);
	ui_WidgetQueueFreeAndNull(&pGroup->pInitFromLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pInitFromValueLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pInheritedLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pVarLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pVarValueLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pVarLabel2);
	ui_WidgetQueueFreeAndNull(&pGroup->pVarValueLabel2);

	MEFieldSafeDestroy(&pGroup->pInitFromField);
	MEFieldSafeDestroy(&pGroup->pIntField);
	MEFieldSafeDestroy(&pGroup->pFloatField);
	MEFieldSafeDestroy(&pGroup->pStringField);
	MEFieldSafeDestroy(&pGroup->pLocStringField);
	MEFieldSafeDestroy(&pGroup->pAnimField);
	MEFieldSafeDestroy(&pGroup->pCritterDefField);
	MEFieldSafeDestroy(&pGroup->pCritterGroupField);
	MEFieldSafeDestroy(&pGroup->pMessageField);
	MEFieldSafeDestroy(&pGroup->pZoneMapField);
	MEFieldSafeDestroy(&pGroup->pSpawnPointField);
	MEFieldSafeDestroy(&pGroup->pItemDefField);
	MEFieldSafeDestroy(&pGroup->pChoiceTableField);
	MEFieldSafeDestroy(&pGroup->pChoiceNameField);
	MEFieldSafeDestroy(&pGroup->pMapVarField);

	eaDestroyEx(&pGroup->eaChoiceTableNames, NULL);
	eaDestroyEx(&pGroup->eaSrcMapVariables, NULL);
	free(pGroup);
}


static void ETFreeFSMVarGroup(ETFSMVarGroup *pGroup)
{
	if (!pGroup)
		return;

	ui_WidgetQueueFreeAndNull(&pGroup->pFSMLabel)
	ui_WidgetQueueFreeAndNull(&pGroup->pTemplateLabel)
	ui_WidgetQueueFreeAndNull(&pGroup->pCritterLabel)
	ui_WidgetQueueFreeAndNull(&pGroup->pExtraLabel)
	ui_WidgetQueueFreeAndNull(&pGroup->pAddVarButton)

	eaDestroyStruct(&pGroup->eaCachedFSMVars, parse_WorldVariableDef);
	eaDestroyStruct(&pGroup->eaCachedOrigFSMVars, parse_WorldVariableDef);
	eaDestroyStruct(&pGroup->eaCachedCritterVars, parse_WorldVariableDef);
	eaDestroyStruct(&pGroup->eaCachedOrigCritterVars, parse_WorldVariableDef);

	eaDestroyEx(&pGroup->eaFSMOverrideVarGroups, ETFreeOverrideVarGroup);
	eaDestroyEx(&pGroup->eaTemplateOverrideVarGroups, ETFreeOverrideVarGroup);
	eaDestroyEx(&pGroup->eaCritterOverrideVarGroups, ETFreeOverrideVarGroup);
	eaDestroyEx(&pGroup->eaExtraVarDefGroups, GEFreeVariableDefGroup);
}


static void ETFreeLevelGroup(ETLevelGroup *pGroup)
{
	MEFieldSafeDestroy(&pGroup->pLevelTypeField);
	MEFieldSafeDestroy(&pGroup->pLevelMinField);
	MEFieldSafeDestroy(&pGroup->pLevelMaxField);
	MEFieldSafeDestroy(&pGroup->pLevelOffsetMinField);
	MEFieldSafeDestroy(&pGroup->pLevelOffsetMaxField);
	MEFieldSafeDestroy(&pGroup->pMapVarField);

	MEFieldSafeDestroy(&pGroup->pClampTypeField);
	MEFieldSafeDestroy(&pGroup->pMinLevelField);
	MEFieldSafeDestroy(&pGroup->pMaxLevelField);
	MEFieldSafeDestroy(&pGroup->pClampOffsetMinField);
	MEFieldSafeDestroy(&pGroup->pClampOffsetMaxField);
	MEFieldSafeDestroy(&pGroup->pClampMapVarField);
	ui_WidgetQueueFreeAndNull(&pGroup->pOverrideParentCheckbox);

	free(pGroup);
}

static void ETFreeDifficultyGroup(ETDifficultyGroup *pGroup)
{
	if(!pGroup)
		return;

	ui_WidgetQueueFreeAndNull(&pGroup->pDifficultyTypeLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pDifficultyLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pMapVarLabel);

	MEFieldSafeDestroy(&pGroup->pDifficultyTypeField);
	MEFieldSafeDestroy(&pGroup->pDifficultyField);
	MEFieldSafeDestroy(&pGroup->pMapVarField);

	free(pGroup);
}

static void ETFreeSpawnGroup(ETSpawnGroup *pGroup)
{
	MEFieldSafeDestroy(&pGroup->pSpawnAnimTypeField);
	MEFieldSafeDestroy(&pGroup->pSpawnAnimField);
	MEFieldSafeDestroy(&pGroup->pSpawnAnimTimeField);
	MEFieldSafeDestroy(&pGroup->pIsAmbushField);
	ui_WidgetQueueFreeAndNull(&pGroup->pOverrideParentCheckbox);

	free(pGroup);
}


static void ETFreeActorSharedGroup(ETActorSharedGroup *pGroup)
{
	MEFieldSafeDestroy(&pGroup->pCritterGroupTypeField);
	MEFieldSafeDestroy(&pGroup->pCritterGroupField);
	MEFieldSafeDestroy(&pGroup->pCritterGroupMapVarField);
	MEFieldSafeDestroy(&pGroup->pFactionTypeField);
	MEFieldSafeDestroy(&pGroup->pFactionField);
	MEFieldSafeDestroy(&pGroup->pGangField);
	MEFieldSafeDestroy(&pGroup->pOverrideSendDistanceField);
	ui_WidgetQueueFreeAndNull(&pGroup->pOverrideParentCheckbox);

	free(pGroup);
}


static void ETFreeAIGroup(ETAIGroup *pGroup)
{
	MEFieldSafeDestroy(&pGroup->pCombatRoleField);
	MEFieldSafeDestroy(&pGroup->pFSMTypeField);
	MEFieldSafeDestroy(&pGroup->pFSMField);
	ui_WidgetQueueFreeAndNull(&pGroup->pOverrideParentCheckbox);

	ETFreeFSMVarGroup(pGroup->pFSMVarGroup);

	free(pGroup);
}


static void ETFreeJobSubGroup(ETJobSubGroup *pGroup)
{
	ui_WidgetQueueFreeAndNull(&pGroup->pRemoveButton);

	ui_WidgetQueueFreeAndNull(&pGroup->pJobNameLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pJobFSMLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pJobRequiresLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pJobRatingLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pJobPriorityLabel);

	MEFieldSafeDestroy(&pGroup->pJobNameField);
	MEFieldSafeDestroy(&pGroup->pJobFSMField);
	MEFieldSafeDestroy(&pGroup->pJobRequiresField);
	MEFieldSafeDestroy(&pGroup->pJobRatingField);
	MEFieldSafeDestroy(&pGroup->pJobPriorityField);

	free(pGroup);
}


static void ETFreeJobGroup(ETJobGroup *pGroup)
{
	int i;

	for(i=eaSize(&pGroup->eaSubGroups)-1; i>=0; --i) {
		ETFreeJobSubGroup(pGroup->eaSubGroups[i]);
	}

	free(pGroup);
}


static void ETFreeWaveGroup(ETWaveGroup *pGroup)
{
	MEFieldSafeDestroy(&pGroup->pWaveCondField);
	MEFieldSafeDestroy(&pGroup->pWaveIntervalTypeField);
	MEFieldSafeDestroy(&pGroup->pWaveIntervalField);
	MEFieldSafeDestroy(&pGroup->pWaveDelayTypeField);
	MEFieldSafeDestroy(&pGroup->pWaveDelayMinField);
	MEFieldSafeDestroy(&pGroup->pWaveDelayMaxField);
	ui_WidgetQueueFreeAndNull(&pGroup->pOverrideParentCheckbox);

	free(pGroup);
}

static void ETFreeRewardsGroup(ETRewardsGroup *pGroup)
{
	MEFieldSafeDestroy(&pGroup->pRewardTypeField);
	MEFieldSafeDestroy(&pGroup->pRewardTableField);
	MEFieldSafeDestroy(&pGroup->pRewardLevelTypeField);

	ui_WidgetQueueFreeAndNull(&pGroup->pRewardTypeLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pRewardTableLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pRewardLevelTypeLabel);

	free(pGroup);
}


static void ETFreeInteractGroup(ETInteractGroup *pGroup)
{
	ui_WidgetQueueFreeAndNull(&pGroup->pNameLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pRemoveButton);

	FreeInteractionPropertiesGroup(pGroup->pInteractPropsGroup);

	free(pGroup);
}


static void ETFreeActorSpawnGroup(ETActorSpawnGroup *pGroup)
{
	ui_WidgetQueueFreeAndNull(&pGroup->pDifficultyLabel);

	MEFieldSafeDestroy(&pGroup->pTeamSizeField);

	free(pGroup);
}

static void ETFreeActorGroup(ETActorGroup *pGroup)
{
	ui_WidgetQueueFreeAndNull(&pGroup->pNameLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pDispNameTypeLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pDispNameLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pDispSubNameTypeLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pDispSubNameLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pCritterTypeLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pCritterLabel1);
	ui_WidgetQueueFreeAndNull(&pGroup->pCritterRankLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pCritterSubRankLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pUseTeamSizeLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pBossTeamSizeLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pFactionTypeLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pFactionLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pGangLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pSpawnAnimTypeLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pSpawnAnimLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pSpawnAnimTimeLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pNonCombatLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pFSMTypeLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pFSMLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pInteractRootLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pInteractRangeLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pLevelOffsetLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pPetContactListLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pCombatRoleLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pCommentsLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pOverrideCritterTypeCheckbox);
	ui_WidgetQueueFreeAndNull(&pGroup->pOverrideDisplayNameCheckbox);
	ui_WidgetQueueFreeAndNull(&pGroup->pOverrideFactionCheckbox);
	ui_WidgetQueueFreeAndNull(&pGroup->pOverrideMiscCheckbox);
	ui_WidgetQueueFreeAndNull(&pGroup->pOverrideActorFSMCheckbox);
	ui_WidgetQueueFreeAndNull(&pGroup->pOverrideSpawnAnimCheckbox);
	ui_WidgetQueueFreeAndNull(&pGroup->pOverrideSpawnConditionsCheckbox);
	ui_WidgetQueueFreeAndNull(&pGroup->pNemesisLeaderLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pNemesisTeamIndexLabel);

	MEFieldSafeDestroy(&pGroup->pNameField);
	MEFieldSafeDestroy(&pGroup->pCommentsField);
	MEFieldSafeDestroy(&pGroup->pDispNameTypeField);
	MEFieldSafeDestroy(&pGroup->pDispNameField);
	MEFieldSafeDestroy(&pGroup->pDispSubNameTypeField);
	MEFieldSafeDestroy(&pGroup->pDispSubNameField);
	MEFieldSafeDestroy(&pGroup->pCritterTypeField);
	MEFieldSafeDestroy(&pGroup->pCritterDefField);
	MEFieldSafeDestroy(&pGroup->pCritterGroupField);
	MEFieldSafeDestroy(&pGroup->pCritterRankField);
	MEFieldSafeDestroy(&pGroup->pCritterSubRankField);
	MEFieldSafeDestroy(&pGroup->pCritterMapVarField);
	MEFieldSafeDestroy(&pGroup->pFactionTypeField);
	MEFieldSafeDestroy(&pGroup->pFactionField);
	MEFieldSafeDestroy(&pGroup->pGangField);
	MEFieldSafeDestroy(&pGroup->pSpawnAnimTypeField);
	MEFieldSafeDestroy(&pGroup->pSpawnAnimField);
	MEFieldSafeDestroy(&pGroup->pSpawnAnimTimeField);
	MEFieldSafeDestroy(&pGroup->pNonCombatField);
	MEFieldSafeDestroy(&pGroup->pFSMTypeField);
	MEFieldSafeDestroy(&pGroup->pFSMField);
	MEFieldSafeDestroy(&pGroup->pInteractRangeField);
	MEFieldSafeDestroy(&pGroup->pLevelOffsetField);
	MEFieldSafeDestroy(&pGroup->pPetContactListField);
	MEFieldSafeDestroy(&pGroup->pCombatRoleField);
	MEFieldSafeDestroy(&pGroup->pNemesisTeamIndexField);
	MEFieldSafeDestroy(&pGroup->pNemesisLeaderField);

	ui_ExpanderGroupRemoveExpander(pGroup->pDoc->pActorExpanderGroup, pGroup->pExpander);

	eaDestroyEx(&pGroup->eaInteractGroups, ETFreeInteractGroup);
	eaDestroyEx(&pGroup->eaSpawnGroups, ETFreeActorSpawnGroup);
	eaDestroyEx(&pGroup->eaBossSpawnGroups, ETFreeActorSpawnGroup);
	
	eaDestroy(&pGroup->eaVarDefs);
	ETFreeFSMVarGroup(pGroup->pFSMVarGroup);

	free(pGroup);
}

static void ETFreeStrengthCell(ETStrengthCell *pGroup)
{
	ui_WidgetQueueFreeAndNull(&pGroup->pTeamSizeButton);
	ui_WidgetQueueFreeAndNull(&pGroup->pTeamSizeLabel);

	free(pGroup);
}

static void ETFreeStrengthRow(ETStrengthRow *pGroup)
{
	int i;
	
	ui_WidgetQueueFreeAndNull(&pGroup->pDifficultyLabel);

	for(i = eaSize(&pGroup->eaCells)-1; i >= 0; --i)
	{
		ETFreeStrengthCell(eaRemove(&pGroup->eaCells, i));
	}
	eaDestroy(&pGroup->eaCells);

	free(pGroup);
}

static void ETFreeStrengthGroup(ETStrengthGroup *pGroup)
{
	int i;

	ui_WidgetQueueFreeAndNull(&pGroup->pTeamSizeHeader);
	ui_WidgetQueueFreeAndNull(&pGroup->pStrengthHeader);
	ui_WidgetQueueFreeAndNull(&pGroup->pNumActorsHeader);
	ui_WidgetQueueFreeAndNull(&pGroup->pNumActors1);
	ui_WidgetQueueFreeAndNull(&pGroup->pNumActors2);
	ui_WidgetQueueFreeAndNull(&pGroup->pNumActors3);
	ui_WidgetQueueFreeAndNull(&pGroup->pNumActors4);
	ui_WidgetQueueFreeAndNull(&pGroup->pNumActors5);

	for(i = eaSize(&pGroup->eaRows)-1; i >= 0; --i)
	{
		ETFreeStrengthRow(eaRemove(&pGroup->eaRows, i));
	}
	eaDestroy(&pGroup->eaRows);

	free(pGroup);
}

static void ETAddFSMVarDef(UIButton *pButton, ETFSMVarGroup *pGroup)
{
	WorldVariableDef *pNewVarDef;
	WorldVariableDef*** peaVarList = NULL;
	WorldVariableDef** eaAssembledVarList = NULL;
	char buf[128];
	int i = 0;
	int iCount = 0;

	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}
	pNewVarDef = StructCreate(parse_WorldVariableDef);
	if(pGroup && pGroup->iActorIndex >= 0) {
		peaVarList = &pGroup->pDoc->pTemplate->eaActors[pGroup->iActorIndex]->eaVariableDefs;
		encounterTemplate_GetActorFSMVarDefs(pGroup->pDoc->pTemplate, pGroup->pDoc->pTemplate->eaActors[pGroup->iActorIndex], &eaAssembledVarList, NULL);
	} else {
		peaVarList = &pGroup->pDoc->pTemplate->pSharedAIProperties->eaVariableDefs;
		encounterTemplate_GetEncounterFSMVarDefs(pGroup->pDoc->pTemplate, &eaAssembledVarList, NULL);
	}

	//give this a unique name
	while(true) {
		sprintf(buf, "FSMVar_%d", iCount);
		for(i=eaSize(&eaAssembledVarList)-1; i>=0; --i) {
			if (eaAssembledVarList[i]->pcName && (stricmp(eaAssembledVarList[i]->pcName, buf) == 0)) {
				break;
			}
		}
		if (i < 0) {
			break;
		}
		++iCount;
	}
	pNewVarDef->pcName = allocAddString(buf);
	eaPush(peaVarList, pNewVarDef);

	eaDestroy(&eaAssembledVarList);
	// Notify of change
	ETFieldChangedCB(NULL, true, pGroup->pDoc);
}

static void ETAddFSMVariableDef(UIButton *pButton, ETAIGroup *pGroup)
{
	WorldVariableDef *pNewVarDef;

	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	pNewVarDef = StructCreate(parse_WorldVariableDef);
	eaPush(&pGroup->pDoc->pTemplate->pSharedAIProperties->eaVariableDefs, pNewVarDef);

	// Notify of change
	ETFieldChangedCB(NULL, true, pGroup->pDoc);
}


static void ETAddActorFSMVariableDef(UIButton *pButton, ETActorGroup *pGroup)
{
	WorldVariableDef *pNewVar;

	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	pNewVar = StructCreate(parse_WorldVariableDef);
	eaPush(&pGroup->pDoc->pTemplate->eaActors[pGroup->iIndex]->eaVariableDefs, pNewVar);

	// Notify of change
	ETFieldChangedCB(NULL, true, pGroup->pDoc);
}


static void ETAddJob(UIButton *pButton, ETJobGroup *pGroup)
{
	AIJobDesc *pNewJob;

	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	pNewJob = StructCreate(parse_AIJobDesc);
	eaPush(&pGroup->pDoc->pTemplate->eaJobs, pNewJob);

	// Notify of change
	ETFieldChangedCB(NULL, true, pGroup->pDoc);
}


static void ETRemoveJob(UIButton *pButton, ETJobSubGroup *pGroup)
{
	// Make sure the resource is checked out of Gimme
	AIJobDesc** eaJobs = NULL;
	int iStartOfEditableJobs = 0;
	if (!emDocIsEditable(&pGroup->pGroup->pDoc->emDoc, true)) {
		return;
	}

	if (IS_HANDLE_ACTIVE(pGroup->pGroup->pDoc->pTemplate->hParent))
	{
		encounterTemplate_FillAIJobEArray(GET_REF(pGroup->pGroup->pDoc->pTemplate->hParent), &eaJobs);
		iStartOfEditableJobs = eaSize(&eaJobs);
	}

	if ((pGroup->iIndex - iStartOfEditableJobs) < eaSize(&pGroup->pGroup->pDoc->pJobGroup->eaSubGroups)) {
		StructDestroy(parse_AIJobDesc, pGroup->pGroup->pDoc->pTemplate->eaJobs[pGroup->iIndex - iStartOfEditableJobs]);
		eaRemove(&pGroup->pGroup->pDoc->pTemplate->eaJobs, pGroup->iIndex - iStartOfEditableJobs);
	}

	// Notify of change
	ETFieldChangedCB(NULL, true, pGroup->pGroup->pDoc);


}


static void ETAddInteraction(UIButton *pButton, ETActorGroup *pGroup)
{
	EncounterActorProperties *pActor = NULL;
	WorldInteractionPropertyEntry *pNewEntry;

	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	if (pGroup->pDoc->pTemplate->eaActors && (pGroup->iIndex < eaSize(&pGroup->pDoc->pTemplate->eaActors))) {
		pActor = pGroup->pDoc->pTemplate->eaActors[pGroup->iIndex];
	}
	if (pActor) {
		if (!pActor->pInteractionProperties) {
			pActor->pInteractionProperties = StructCreate(parse_WorldInteractionProperties);
		}
		pNewEntry = StructCreate(parse_WorldInteractionPropertyEntry);
		pNewEntry->pcInteractionClass = pcPooled_Contact;
		pNewEntry->pContactProperties = StructCreate(parse_WorldContactInteractionProperties);
		pNewEntry->bUseExclusionFlag = 1;
		eaPush(&pActor->pInteractionProperties->eaEntries, pNewEntry);

		// Notify of change
		ETFieldChangedCB(NULL, true, pGroup->pDoc);
	}
}


static void ETRemoveInteraction(UIButton *pButton, ETInteractGroup *pGroup)
{
	EncounterActorProperties *pActor = NULL;
	int iStartOfEditableInteractions = 0;
	WorldInteractionPropertyEntry** eaParentInteractions = NULL;
	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pGroup->pActorGroup->pDoc->emDoc, true)) {
		return;
	}
	if (pGroup->pActorGroup->pDoc->pTemplate->eaActors && (pGroup->pActorGroup->iIndex < eaSize(&pGroup->pActorGroup->pDoc->pTemplate->eaActors))) {
		pActor = pGroup->pActorGroup->pDoc->pTemplate->eaActors[pGroup->pActorGroup->iIndex];
	}
	if (IS_HANDLE_ACTIVE(pGroup->pActorGroup->pDoc->pTemplate->hParent))
	{
		encounterTemplate_FillActorInteractionEarray(GET_REF(pGroup->pActorGroup->pDoc->pTemplate->hParent), pActor, &eaParentInteractions);
		iStartOfEditableInteractions = eaSize(&eaParentInteractions);
		eaDestroy(&eaParentInteractions);
	}
	
	if (pActor && pActor->pInteractionProperties && ((pGroup->iIndex - iStartOfEditableInteractions) < eaSize(&pActor->pInteractionProperties->eaEntries))) {
		StructDestroy(parse_WorldInteractionPropertyEntry, pActor->pInteractionProperties->eaEntries[pGroup->iIndex - iStartOfEditableInteractions]);
		eaRemove(&pActor->pInteractionProperties->eaEntries, pGroup->iIndex - iStartOfEditableInteractions);

		// If remove last properties, then clean up
		if (!eaSize(&pActor->pInteractionProperties->eaEntries)) {
			StructDestroy(parse_WorldInteractionProperties, pActor->pInteractionProperties);
			pActor->pInteractionProperties = NULL;
		}
	}

	// Notify of change
	ETFieldChangedCB(NULL, true, pGroup->pActorGroup->pDoc);
}


static void ETAddActor(UIButton *pButton, EncounterTemplateEditDoc *pDoc)
{
	EncounterActorProperties *pNewActor;
	EncounterActorProperties** eaActorList = NULL;
	char buf[128];
	int iCount = 1;
	int i;

	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pDoc->emDoc, true)) {
		return;
	}

	pNewActor = StructCreate(parse_EncounterActorProperties);
	eaPush(&pDoc->pTemplate->eaActors, pNewActor);

	encounterTemplate_FillActorEarray(pDoc->pTemplate, &eaActorList);
	// Give it a unique name
	while(true) {
		sprintf(buf, "Actor_%d", iCount);
		for(i=eaSize(&eaActorList)-1; i>=0; --i) {
			if (eaActorList[i]->pcName && (stricmp(eaActorList[i]->pcName, buf) == 0)) {
				break;
			}
		}
		if (i < 0) {
			break;
		}
		++iCount;
	}
	pNewActor->pcName = allocAddString(buf);
	pNewActor->bOverrideCritterSpawnInfo = true;
	pNewActor->bOverrideCritterType = true;
	pNewActor->bOverrideDisplayName = true;
	pNewActor->bOverrideDisplaySubName = true;
	pNewActor->bOverrideFaction = true;
	pNewActor->bOverrideFSMInfo = true;
	pNewActor->bOverrideMisc = true;
	pNewActor->bOverrideSpawnConditions = true;

	// Notify of change
	ETCritterTypeChangedCB(NULL, true, pDoc);
}


static void ETRemoveActor(UIButton *pButton, ETActorGroup *pGroup)
{
	EncounterActorProperties *pActor;
	char* pcName = NULL;
	int iActor = -1;
	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	estrStackCreate(&pcName);
	MEFieldGetString(pGroup->pNameField, &pcName);
	pActor = encounterTemplate_GetActorByName(pGroup->pDoc->pTemplate, pcName);
	estrDestroy(&pcName);

	iActor = eaFind(&pGroup->pDoc->pTemplate->eaActors, pActor);
	if (iActor > -1)
	{
		eaRemove(&pGroup->pDoc->pTemplate->eaActors, iActor);
		StructDestroy(parse_EncounterActorProperties, pActor);
	}

	// Notify of change
	ETFieldChangedCB(NULL, true, pGroup->pDoc);
}

static void ETCloneActor(UIButton *pButton, ETActorGroup *pGroup)
{
	EncounterActorProperties *pNewActor;
	EncounterActorProperties *pOldActor;
	EncounterActorProperties** eaActorList = NULL;
	char* pcName = NULL;
	char buf[128];
	int iCount = 1;
	int i;
	WorldInteractionPropertyEntry** eaEntries = NULL;
	WorldVariableDef** eaVars = NULL;
	EncounterActorSpawnProperties** eaSpawnProps = NULL;
	EncounterActorSpawnProperties** eaBossSpawnProps = NULL;

	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	encounterTemplate_FillActorEarray(pGroup->pDoc->pTemplate, &eaActorList);

	estrStackCreate(&pcName);
	MEFieldGetString(pGroup->pNameField, &pcName);
	pOldActor = encounterTemplate_GetActorByName(pGroup->pDoc->pTemplate, pcName);
	estrDestroy(&pcName);

	pNewActor = StructClone(parse_EncounterActorProperties, pOldActor);

	if(!pOldActor || !pNewActor)
		return;

	eaPush(&pGroup->pDoc->pTemplate->eaActors, pNewActor);

	// Give it a unique name, respecting inherited actors.
	while(true) {
		sprintf(buf, "Actor_%d", iCount);
		for(i=eaSize(&eaActorList)-1; i>=0; --i) {
			if (eaActorList[i]->pcName && (stricmp(eaActorList[i]->pcName, buf) == 0)) {
				break;
			}
		}
		if (i < 0) {
			break;
		}
		++iCount;
	}
	pNewActor->pcName = allocAddString(buf);
	pNewActor->bOverrideCritterSpawnInfo = true;
	pNewActor->bOverrideCritterType = true;
	pNewActor->bOverrideDisplayName = true;
	pNewActor->bOverrideDisplaySubName = true;
	pNewActor->bOverrideFaction = true;
	pNewActor->bOverrideFSMInfo = true;
	pNewActor->bOverrideMisc = true;
	pNewActor->bOverrideSpawnConditions = true;

	//we need to copy inherited data.
	if (!pOldActor->bOverrideDisplayName)
		StructCopyAll(parse_EncounterActorNameProperties, encounterTemplate_GetActorNameProperties(pGroup->pDoc->pTemplate, pOldActor), &pNewActor->nameProps);
	if (!pOldActor->bOverrideCritterType)
		StructCopyAll(parse_EncounterActorCritterProperties, encounterTemplate_GetActorCritterProperties(pGroup->pDoc->pTemplate, pOldActor), &pNewActor->critterProps);
	if (!pOldActor->bOverrideFaction)
		StructCopyAll(parse_EncounterActorFactionProperties, encounterTemplate_GetActorFactionProperties(pGroup->pDoc->pTemplate, pOldActor), &pNewActor->factionProps);
	if (!pOldActor->bOverrideMisc)
		StructCopyAll(parse_EncounterActorMiscProperties, encounterTemplate_GetActorMiscProperties(pGroup->pDoc->pTemplate, pOldActor), &pNewActor->miscProps);

	if (!pOldActor->bOverrideFSMInfo)
		StructCopyAll(parse_EncounterActorFSMProperties, encounterTemplate_GetActorFSMProperties(pGroup->pDoc->pTemplate, pOldActor), &pNewActor->fsmProps);
	if (!pOldActor->bOverrideCritterSpawnInfo)
		StructCopyAll(parse_EncounterActorSpawnInfoProperties, encounterTemplate_GetActorSpawnInfoProperties(pGroup->pDoc->pTemplate, pOldActor), &pNewActor->spawnInfoProps);
	if (!pOldActor->bOverrideSpawnConditions)
	{
		eaSpawnProps = encounterTemplate_GetActorSpawnProps(pGroup->pDoc->pTemplate, pOldActor);
		eaClearStruct(&pNewActor->eaSpawnProperties, parse_EncounterActorSpawnProperties);
		eaCopyStructs(&eaSpawnProps, &pNewActor->eaSpawnProperties, parse_EncounterActorSpawnProperties);

		eaBossSpawnProps = encounterTemplate_GetActorBossProps(pGroup->pDoc->pTemplate, pOldActor);
		eaClearStruct(&pNewActor->eaBossSpawnProperties, parse_EncounterActorSpawnProperties);
		eaCopyStructs(&eaBossSpawnProps, &pNewActor->eaBossSpawnProperties, parse_EncounterActorSpawnProperties);
	}

	encounterTemplate_GetActorFSMVarDefs(pGroup->pDoc->pTemplate, pOldActor, &eaVars, NULL);
	eaClearStruct(&pNewActor->eaVariableDefs, parse_WorldVariableDef);
	eaCopyStructs(&eaVars, &pNewActor->eaVariableDefs, parse_WorldVariableDef);
	if (pNewActor->pInteractionProperties)
	{
		encounterTemplate_FillActorInteractionEarray(pGroup->pDoc->pTemplate, pOldActor, &eaEntries);
		eaClearStruct(&pNewActor->pInteractionProperties->eaEntries, parse_WorldInteractionPropertyEntry);
		eaCopyStructs(&eaEntries, &pNewActor->pInteractionProperties->eaEntries, parse_WorldInteractionPropertyEntry);
		eaDestroy(&eaEntries);
	}

	eaDestroy(&eaVars);

	// Notify of change
	ETCritterTypeChangedCB(NULL, true, pGroup->pDoc);
}

static void ETMoveActorUp(UIButton *pButton, ETActorGroup *pGroup)
{
	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	eaSwap(&pGroup->pDoc->pTemplate->eaActors, pGroup->iIndex, pGroup->iIndex - 1);

	// Notify of change
	ETFieldChangedCB(NULL, true, pGroup->pDoc);
}


static void ETMoveActorDown(UIButton *pButton, ETActorGroup *pGroup)
{
	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	eaSwap(&pGroup->pDoc->pTemplate->eaActors, pGroup->iIndex, pGroup->iIndex + 1);

	// Notify of change
	ETFieldChangedCB(NULL, true, pGroup->pDoc);
}


static void ETSetSizeAll(UIButton *pButton, EncounterTemplateEditDoc *pDoc)
{
	int i;

	if(!pDoc)
		return;

	for(i=eaSize(&pDoc->eaActorGroups)-1; i>=0; --i) {
		ui_ExpanderSetOpened(pDoc->eaActorGroups[i]->pExpander, true);
	}

	ETUpdateDisplay(pDoc);
}




//---------------------------------------------------------------------------------------------------
// UI Logic
//---------------------------------------------------------------------------------------------------

static void ETAddFieldToParent(MEField *pField, UIWidget *pParent, F32 x, F32 y, F32 xPercent, F32 w, UIUnitType wUnit, F32 padRight, EncounterTemplateEditDoc *pDoc)
{
	MEExpanderAddFieldToParent(pField, pParent, x, y, xPercent, w, wUnit, padRight, ETFieldChangedCB, ETFieldPreChangeCB, pDoc);
}


static MEField *ETRefreshSimpleField(MEField *pField, void *pOrigData, void *pData, ParseTable *pParseTable, char *pcFieldName, MEFieldType eType,
								     UIExpander *pExpander, F32 x, F32 y, F32 xPercent, F32 w, UIUnitType wUnit, F32 padRight, EncounterTemplateEditDoc *pDoc)
{
	if (!pField) {
		pField = MEFieldCreateSimple(eType, pOrigData, pData, pParseTable, pcFieldName);
		assertmsgf(pField, "Failed to create field named %s", pcFieldName);
		ETAddFieldToParent(pField, UI_WIDGET(pExpander), x, y, xPercent, w, wUnit, padRight, pDoc);
	} else {
		ui_WidgetSetPositionEx(pField->pUIWidget, x, y, xPercent, 0, UITopLeft);
		MEFieldSetAndRefreshFromData(pField, pOrigData, pData);
	}
	return pField;
}
static bool ETRefreshOverrideCheckBox(UICheckButton** ppCheckBox, UIExpander** ppExpander, char *pcFieldName, F32 x, F32 y, bool bChecked, EncounterTemplateEditDoc* pDoc, UIActivationFunc cbFunc, void* cbData, bool bShow)
{
	if (IS_HANDLE_ACTIVE(pDoc->pTemplate->hParent) && bShow)
	{
		if (!(*ppCheckBox))
		{
			*ppCheckBox = ui_CheckButtonCreate(x, y, pcFieldName, bChecked);
			assertmsgf(*ppCheckBox, "Failed to create check box named %s", pcFieldName);
			ui_CheckButtonSetToggledCallback(*ppCheckBox, cbFunc, cbData);
			ui_ExpanderAddChild(*ppExpander, *ppCheckBox);
		}
		else 
		{
			ui_WidgetSetPositionEx(&(*ppCheckBox)->widget, x, y, 0, 0, UITopLeft);
		}
		ui_CheckButtonSetState(*ppCheckBox, bChecked);
		return true;
	}
	else if (*ppCheckBox)
	{
		ui_WidgetQueueFreeAndNull(ppCheckBox);
		return false;
	}
	return false;
}


static MEField *ETRefreshEnumField(MEField *pField, void *pOrigData, void *pData, ParseTable *pParseTable, char *pcFieldName, StaticDefineInt pEnum[],
								   UIExpander *pExpander, F32 x, F32 y, F32 xPercent, F32 w, UIUnitType wUnit, F32 padRight, EncounterTemplateEditDoc *pDoc)
{
   if (!pField) {
	   pField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pOrigData, pData, pParseTable, pcFieldName, pEnum);
	   assertmsgf(pField, "Failed to create field named %s", pcFieldName);
	   ETAddFieldToParent(pField, UI_WIDGET(pExpander), x, y, xPercent, w, wUnit, padRight, pDoc);
   } else {
	   ui_WidgetSetPositionEx(pField->pUIWidget, x, y, xPercent, 0, UITopLeft);
	   MEFieldSetAndRefreshFromData(pField, pOrigData, pData);
   }
   return pField;
}


static MEField *ETRefreshFlagEnumField(MEField *pField, void *pOrigData, void *pData, ParseTable *pParseTable, char *pcFieldName, StaticDefineInt pEnum[],
									   UIExpander *pExpander, F32 x, F32 y, F32 xPercent, F32 w, UIUnitType wUnit, F32 padRight, EncounterTemplateEditDoc *pDoc)
{
	if (!pField) {
		pField = MEFieldCreateSimpleEnum(kMEFieldType_FlagCombo, pOrigData, pData, pParseTable, pcFieldName, pEnum);
		assertmsgf(pField, "Failed to create field named %s", pcFieldName);
		ETAddFieldToParent(pField, UI_WIDGET(pExpander), x, y, xPercent, w, wUnit, padRight, pDoc);
	} else {
		ui_WidgetSetPositionEx(pField->pUIWidget, x, y, xPercent, 0, UITopLeft);
		MEFieldSetAndRefreshFromData(pField, pOrigData, pData);
	}
	return pField;
}


static MEField *ETRefreshDictionaryField(MEField *pField, void *pOrigData, void *pData, ParseTable *pParseTable, char *pcFieldName, char *pcDict,
										 UIExpander *pExpander, F32 x, F32 y, F32 xPercent, F32 w, UIUnitType wUnit, F32 padRight, EncounterTemplateEditDoc *pDoc)
{
	if (!pField) {
		pField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_ValidatedTextEntry, pOrigData, pData, pParseTable, pcFieldName, pcDict, "ResourceName");
		assertmsgf(pField, "Failed to create field named %s", pcFieldName);
		ETAddFieldToParent(pField, UI_WIDGET(pExpander), x, y, xPercent, w, wUnit, padRight, pDoc);
	} else {
		ui_WidgetSetPositionEx(pField->pUIWidget, x, y, xPercent, 0, UITopLeft);
		MEFieldSetAndRefreshFromData(pField, pOrigData, pData);
	}
	return pField;
}


static MEField *ETRefreshDataField(MEField *pField, void *pOrigData, void *pData, ParseTable *pParseTable, char *pcFieldName, const void ***peaModel, bool bValidated,
								   UIExpander *pExpander, F32 x, F32 y, F32 xPercent, F32 w, UIUnitType wUnit, F32 padRight, EncounterTemplateEditDoc *pDoc)
{
	if (!pField) {
		pField = MEFieldCreateSimpleDataProvided(bValidated ? kMEFieldType_ValidatedTextEntry : kMEFieldType_TextEntry, pOrigData, pData, pParseTable, pcFieldName, NULL, peaModel, NULL);
		assertmsgf(pField, "Failed to create field named %s", pcFieldName);
		ETAddFieldToParent(pField, UI_WIDGET(pExpander), x, y, xPercent, w, wUnit, padRight, pDoc);
	} else {
		ui_WidgetSetPositionEx(pField->pUIWidget, x, y, xPercent, 0, UITopLeft);
		MEFieldSetAndRefreshFromData(pField, pOrigData, pData);
	}
	return pField;
}


static UILabel *ETRefreshLabel(UILabel *pLabel, const char *pcText, const char *pcTooltip, F32 x, F32 xPercent, F32 y, UIExpander *pExpander)
{
	if (!pLabel) {
		pLabel = ui_LabelCreate(pcText, x, y);
		assertmsgf(pLabel, "Failed to create label with text \"%s\"", pcText);
		ui_WidgetSetPositionEx(UI_WIDGET(pLabel), x, y, xPercent, 0, UITopLeft);
		ui_WidgetSetTooltipString(UI_WIDGET(pLabel), pcTooltip);
		ui_LabelEnableTooltips(pLabel);
		ui_ExpanderAddChild(pExpander, pLabel);
	} else {
		ui_LabelSetText(pLabel, pcText);
		ui_WidgetSetTooltipString(UI_WIDGET(pLabel), pcTooltip);
		ui_WidgetSetPositionEx(UI_WIDGET(pLabel), x, y, xPercent, 0, UITopLeft);
	}
	return pLabel;
}

static UIExpander *ETCreateExpander(UIExpanderGroup *pExGroup, const char *pcName, int index)
{
	UIExpander *pExpander = ui_ExpanderCreate(pcName, 0);
	assertmsgf(pExpander, "Failed to create field named %s", pcName);
	ui_WidgetSkin(UI_WIDGET(pExpander), gBoldExpanderSkin);
	ui_ExpanderGroupInsertExpander(pExGroup, pExpander, index);
	ui_ExpanderSetOpened(pExpander, 1);

	return pExpander;
}

void ETFillDefaultActorProperties(EncounterActorProperties* pActor, EncounterTemplate *pTemplate)
{
	bool bIsStandaloneActor = true;
	int j = 0;
	int iLastDifficulty = encounter_GetEncounterDifficultiesCount();
	EncounterActorProperties* pParentActor = NULL;
	EncounterTemplate* pParentTemplate = GET_REF(pTemplate->hParent);
	if(iLastDifficulty < 1)
		iLastDifficulty = 1;

	if (IS_HANDLE_ACTIVE(pTemplate->hParent))
	{
		//don't fill default anything for critters which are inheriting from a parent.
		pParentActor = encounterTemplate_GetActorByName(GET_REF(pTemplate->hParent), pActor->pcName);
		if (pParentActor)
		{
			bIsStandaloneActor = false;
		}
	}
	// Fill default ranks
	if (bIsStandaloneActor)
	{
		if ((pActor->critterProps.eCritterType == ActorCritterType_CritterGroup) || 
			(pActor->critterProps.eCritterType == ActorCritterType_MapVariableGroup) || 
			(pActor->critterProps.eCritterType == ActorCritterType_FromTemplate) ||
			pActor->critterProps.eCritterType == ActorCritterType_NemesisMinion ||
			pActor->critterProps.eCritterType == ActorCritterType_NemesisMinionNormal ||
			pActor->critterProps.eCritterType == ActorCritterType_NemesisMinionTeam ||
			pActor->critterProps.eCritterType == ActorCritterType_NemesisMinionForLeader )
		{
				if (!pActor->critterProps.pcRank) {
					pActor->critterProps.pcRank = g_pcCritterDefaultRank;
				}
				if (!pActor->critterProps.pcSubRank) {
					pActor->critterProps.pcSubRank = g_pcCritterDefaultSubRank;
				}
		} else if(	(pActor->critterProps.eCritterType == ActorCritterType_CritterDef) ||
			(pActor->critterProps.eCritterType == ActorCritterType_MapVariableDef)) {
				pActor->critterProps.pcRank = NULL;
				if (!pActor->critterProps.pcSubRank) {
					pActor->critterProps.pcSubRank = g_pcCritterDefaultSubRank;
				}
		} else {
			pActor->critterProps.pcRank = NULL;
			pActor->critterProps.pcSubRank = NULL;
		}
	}
	else if (pActor->bOverrideCritterType)
	{
		//if we don't have either, we probably just clicked an override checkbox and should take parent's data.
		if (!pActor->critterProps.pcRank && !pActor->critterProps.pcSubRank)
			pActor->critterProps.eCritterType = encounterTemplate_GetActorCritterType(pParentTemplate, pParentActor);
		if (!pActor->critterProps.pcRank)
			pActor->critterProps.pcRank = encounterTemplate_GetActorRank(pParentTemplate, pParentActor);
		if (!pActor->critterProps.pcSubRank)
			pActor->critterProps.pcSubRank = encounterTemplate_GetActorSubRank(pParentTemplate, pParentActor);
	}

	if (bIsStandaloneActor)
	{
		// Setup default spawn properties
		for(j = 0; eaSize(&pActor->eaSpawnProperties) <  iLastDifficulty && j < iLastDifficulty; j++)
		{
			EncounterActorSpawnProperties *pSpawnProps = StructCreate(parse_EncounterActorSpawnProperties);
			pSpawnProps->eSpawnAtDifficulty = j;
			pSpawnProps->eSpawnAtTeamSize = 31;			// All team size flags set
			eaIndexedAdd(&pActor->eaSpawnProperties, pSpawnProps);
		}

		// Setup default boss spawn properties
		for(j = 0; eaSize(&pActor->eaBossSpawnProperties) <  iLastDifficulty && j < iLastDifficulty; j++)
		{
			EncounterActorSpawnProperties *pSpawnProps = StructCreate(parse_EncounterActorSpawnProperties);
			pSpawnProps->eSpawnAtDifficulty = j;
			pSpawnProps->eSpawnAtTeamSize = 0;
			eaIndexedAdd(&pActor->eaBossSpawnProperties, pSpawnProps);
		}
	}
	else if (pActor->bOverrideSpawnConditions)
	{
		//copy parent's data if we can.
		if (!eaSize(&pActor->eaSpawnProperties))
		{
			EncounterActorSpawnProperties** pProps = encounterTemplate_GetActorSpawnProps(pParentTemplate, pParentActor);
			eaIndexedEnable(&pActor->eaSpawnProperties, parse_EncounterActorSpawnProperties);
			eaCopyStructs(&pProps, &pActor->eaSpawnProperties, parse_EncounterActorSpawnProperties);
		}

		if (!eaSize(&pActor->eaBossSpawnProperties))
		{
			EncounterActorSpawnProperties** pProps = encounterTemplate_GetActorBossProps(pParentTemplate, pParentActor);
			eaIndexedEnable(&pActor->eaBossSpawnProperties, parse_EncounterActorSpawnProperties);
			eaCopyStructs(&pProps, &pActor->eaBossSpawnProperties, parse_EncounterActorSpawnProperties);
		}

	}
}
static void ETFillAllDefaultActorPropertiesInTemplate(EncounterTemplate *pTemplate)
{
	int i;

	for(i=eaSize(&pTemplate->eaActors)-1; i>=0; --i) {
		ETFillDefaultActorProperties(pTemplate->eaActors[i], pTemplate);
	}
}


// This is called whenever any template data changes to do cleanup
static void ETTemplateChanged(EncounterTemplateEditDoc *pDoc, bool bUndoable)
{
	if (!pDoc->bIgnoreFieldChanges) {
		ETUpdateDisplay(pDoc);

		if (bUndoable) {
			ETUndoData *pData = calloc(1, sizeof(ETUndoData));
			pData->pPreTemplate = pDoc->pNextUndoTemplate;
			pData->pPostTemplate = StructClone(parse_EncounterTemplate, pDoc->pTemplate);
			EditCreateUndoCustom(pDoc->emDoc.edit_undo_stack, ETTemplateUndoCB, ETTemplateRedoCB, ETTemplateUndoFreeCB, pData);
			pDoc->pNextUndoTemplate = StructClone(parse_EncounterTemplate, pDoc->pTemplate);
		}
	}
}


// This is called by MEField prior to allowing an edit
static bool ETFieldPreChangeCB(MEField *pField, bool bFinished, EncounterTemplateEditDoc *pDoc)
{
	// Make sure the resource is checked out of Gimme
	return emDocIsEditable(&pDoc->emDoc, true);
}


// This is called when an MEField is changed
static void ETFieldChangedCB(MEField *pField, bool bFinished, EncounterTemplateEditDoc *pDoc)
{
	ETTemplateChanged(pDoc, bFinished);
}

// This is called when an MEField is changed
static void ETActorNameChangedCB(MEField *pField, bool bFinished, EncounterTemplateEditDoc *pDoc)
{
	if (bFinished)
	{
		int i;
		int iCount = 0;
		bool bExistsOnParent = false;
	//	EncounterActorProperties** eaActors = NULL;
		char* pcName = NULL;
		//see if this name exists on a parent
	//	encounterTemplate_FillActorEarray(GET_REF(pDoc->pTemplate->hParent), &eaActors);
		estrStackCreate(&pcName);
		MEFieldGetString(pField, &pcName);
		//check the parent stack and our own earray

		//if we have more than one actor with this name, no good.
		for (i = 0; i < eaSize(&pDoc->pTemplate->eaActors); i++)
		{
			if (stricmp(pcName, pDoc->pTemplate->eaActors[i]->pcName) == 0)
			{
				iCount++;
			}
		}
		if (iCount > 1)
		{
			Errorf("More than one actor named %s!", pcName);
			StructDestroy(parse_EncounterTemplate, pDoc->pTemplate);
			pDoc->pTemplate = StructClone(parse_EncounterTemplate, pDoc->pNextUndoTemplate);
		}
		ETTemplateChanged(pDoc, bFinished);

		estrDestroy(&pcName);
	}
}

static void ETSetScopeCB(MEField *pField, bool bFinished, EncounterTemplateEditDoc *pDoc)
{
	if (!pDoc->bIgnoreFilenameChanges) {
		// Update the filename appropriately
		resFixFilename(g_hEncounterTemplateDict, pDoc->pTemplate->pcName, pDoc->pTemplate);
	}

	// Call on to do regular updates
	ETFieldChangedCB(pField, bFinished, pDoc);
}

static void ETCopyInheritedActorData(EncounterTemplate *pDstTemplate, EncounterActorProperties* pSrcActor, EncounterActorProperties* pMatchingActor)
{
	//We need to go through all the actor's properties and set them to the data from the highest overridden inheritance level.
	EncounterActorSpawnProperties** ppSpawnProps = NULL;
	WorldVariableDef** ppTempDefs = NULL;
				
	//This means we need to pretend just for now that we still have the same parent set.
	pMatchingActor->miscProps.bIsNonCombatant = !encounterTemplate_GetActorIsCombatant(pDstTemplate, pSrcActor);
				
	pMatchingActor->critterProps.iLevelOffset = encounterTemplate_GetActorLevelOffset(pDstTemplate, pSrcActor);
				
	eaClearStruct(&pMatchingActor->eaBossSpawnProperties, parse_EncounterActorSpawnProperties);
	ppSpawnProps = encounterTemplate_GetActorBossProps(pDstTemplate, pSrcActor);
	eaPushStructs(&pMatchingActor->eaBossSpawnProperties, &ppSpawnProps, parse_EncounterActorSpawnProperties);
				
	eaClearStruct(&pMatchingActor->eaSpawnProperties, parse_EncounterActorSpawnProperties);
	ppSpawnProps = encounterTemplate_GetActorSpawnProps(pDstTemplate, pSrcActor);
	eaPushStructs(&pMatchingActor->eaSpawnProperties, &ppSpawnProps, parse_EncounterActorSpawnProperties);

	eaClearStruct(&pMatchingActor->eaVariableDefs, parse_WorldVariableDef);
	encounterTemplate_GetActorFSMVarDefs(pDstTemplate, pSrcActor, &ppTempDefs, NULL);
	eaPushStructs(&pMatchingActor->eaVariableDefs, &ppTempDefs, parse_WorldVariableDef);

	pMatchingActor->critterProps.eCritterType = encounterTemplate_GetActorCritterType(pDstTemplate, pSrcActor);
	pMatchingActor->nameProps.eDisplayNameType = encounterTemplate_GetActorDisplayMessageSource(pDstTemplate, pSrcActor);
	pMatchingActor->nameProps.eDisplayNameType = encounterTemplate_GetActorDisplayMessageSource(pDstTemplate, pSrcActor);
	pMatchingActor->nameProps.eDisplaySubNameType = encounterTemplate_GetActorDisplaySubNameMessageSource(pDstTemplate, pSrcActor);
	pMatchingActor->factionProps.eFactionType = encounterTemplate_GetActorFactionSource(pDstTemplate, pSrcActor);
	pMatchingActor->fsmProps.eFSMType = encounterTemplate_GetActorFSMSource(pDstTemplate, pSrcActor);
	pMatchingActor->spawnInfoProps.eSpawnAnimType = encounterTemplate_GetActorSpawnAnimSource(pDstTemplate, pSrcActor);

	if (pMatchingActor->nameProps.eDisplayNameType == EncounterCritterOverrideType_Specified)
		SET_HANDLE_FROM_REFERENT(gMessageDict, encounterTemplate_GetActorDisplayMessage(pDstTemplate, pSrcActor, 0), pMatchingActor->nameProps.displayNameMsg.hMessage);
	if (pMatchingActor->nameProps.eDisplaySubNameType == EncounterCritterOverrideType_Specified)
		SET_HANDLE_FROM_REFERENT(gMessageDict, encounterTemplate_GetActorDisplaySubNameMessage(pDstTemplate, pSrcActor, 0), pMatchingActor->nameProps.displaySubNameMsg.hMessage);
	if (pMatchingActor->critterProps.eCritterType == ActorCritterType_CritterDef)
		SET_HANDLE_FROM_REFERENT(g_hCritterDefDict, encounterTemplate_GetActorCritterDef(pDstTemplate, pSrcActor, 0), pMatchingActor->critterProps.hCritterDef);
	else if (pMatchingActor->critterProps.eCritterType == ActorCritterType_CritterGroup)
		SET_HANDLE_FROM_REFERENT(g_hCritterGroupDict, encounterTemplate_GetActorCritterGroup(pDstTemplate, pSrcActor, 0, pMatchingActor->critterProps.eCritterType), pMatchingActor->critterProps.hCritterGroup);
	if (pMatchingActor->factionProps.eFactionType == EncounterTemplateOverrideType_Specified)
	{
		SET_HANDLE_FROM_REFERENT(g_hCritterFactionDict, encounterTemplate_GetActorFaction(pDstTemplate, pSrcActor, 0), pMatchingActor->factionProps.hFaction);
		pMatchingActor->factionProps.iGangID = encounterTemplate_GetActorGangID(pDstTemplate, pSrcActor, 0);
	}
	if (pMatchingActor->fsmProps.eFSMType == EncounterTemplateOverrideType_Specified)
		SET_HANDLE_FROM_REFERENT(gFSMDict, encounterTemplate_GetActorFSM(pDstTemplate, pSrcActor, 0), pMatchingActor->fsmProps.hFSM);

	pMatchingActor->miscProps.pcCombatRole = encounterTemplate_GetActorCombatRole(pDstTemplate, pSrcActor);
	if (pMatchingActor->critterProps.pcCritterMapVariable)
		free(pMatchingActor->critterProps.pcCritterMapVariable);
	pMatchingActor->critterProps.pcCritterMapVariable = StructAllocString(encounterTemplate_GetActorCritterTypeMapVarName(pDstTemplate, pSrcActor));
	pMatchingActor->critterProps.pcRank = encounterTemplate_GetActorRank(pDstTemplate, pSrcActor);
	pMatchingActor->critterProps.pcSubRank = encounterTemplate_GetActorSubRank(pDstTemplate, pSrcActor);
	if (pMatchingActor->spawnInfoProps.eSpawnAnimType == EncounterTemplateOverrideType_Specified)
	{
		if (pMatchingActor->spawnInfoProps.pcSpawnAnim)
			free(pMatchingActor->spawnInfoProps.pcSpawnAnim);
		pMatchingActor->spawnInfoProps.pcSpawnAnim = StructAllocString(encounterTemplate_GetActorSpawnAnim(pDstTemplate, pSrcActor, 0, &pMatchingActor->spawnInfoProps.fSpawnLockdownTime));
	}
	pMatchingActor->bOverrideCritterSpawnInfo = true;
	pMatchingActor->bOverrideCritterType = true;
	pMatchingActor->bOverrideDisplayName = true;
	pMatchingActor->bOverrideFaction = true;
	pMatchingActor->bOverrideFSMInfo = true;
	pMatchingActor->bOverrideMisc = true;
	pMatchingActor->bOverrideSpawnConditions = true;
}
// This copies the inheritable fields from pSrcTemplate to the doc's current template
// If pSrcTemplate is NULL, default fields will be used.
// This is used when un-inheriting a template to fill in all the blank stuff.
static void ETCopyInheritableFields(EncounterTemplate *pDstTemplate, EncounterTemplate* pSrcTemplate)
{
	EncounterLevelProperties *pSrcLevelProperties = NULL;
	EncounterSpawnProperties *pSrcSpawnProperties = NULL;
	EncounterWaveProperties *pSrcWaveProperties = NULL;
	EncounterAIProperties *pSrcSharedAIProperties = NULL;
	EncounterRewardProperties *pSrcRewardProprties = NULL;
	AIJobDesc **eaSrcJobs = NULL;
	EncounterActorProperties **eaSrcActors = NULL;
	EncounterActorProperties **eaSrcActorDuplicates = NULL;
	int i = 0;
	int j = 0;

	// We need to pretend to still have a parent so all the functions that get called will retrieve the
	// correct data. It gets cleared later.
	SET_HANDLE_FROM_REFERENT(g_hEncounterTemplateDict, pSrcTemplate, pDstTemplate->hParent);

	pSrcLevelProperties = encounterTemplate_GetLevelProperties(pSrcTemplate);
	pSrcSpawnProperties = encounterTemplate_GetSpawnProperties(pSrcTemplate);
	pSrcWaveProperties = encounterTemplate_GetWaveProperties(pSrcTemplate);
	pSrcSharedAIProperties = encounterTemplate_GetAIProperties(pSrcTemplate);
	pSrcRewardProprties = encounterTemplate_GetRewardProperties(pSrcTemplate);

	if(!pDstTemplate)
		return;
	encounterTemplate_FillAIJobEArray(pSrcTemplate, &eaSrcJobs);
	encounterTemplate_FillActorEarray(pSrcTemplate, &eaSrcActors);
	eaPushStructs(&eaSrcActorDuplicates, &eaSrcActors, parse_EncounterActorProperties);

	// Inheritable property structs
	if(pDstTemplate->pLevelProperties && !pDstTemplate->pLevelProperties->bOverrideParentValues)
	{
		StructDestroy(parse_EncounterLevelProperties, pDstTemplate->pLevelProperties);
		pDstTemplate->pLevelProperties = pSrcLevelProperties ? StructClone(parse_EncounterLevelProperties, pSrcLevelProperties) : NULL;
	}

	if(pDstTemplate->pSpawnProperties && !pDstTemplate->pSpawnProperties->bOverrideParentValues)
	{
		StructDestroy(parse_EncounterSpawnProperties, pDstTemplate->pSpawnProperties);
		pDstTemplate->pSpawnProperties = pSrcSpawnProperties ? StructClone(parse_EncounterSpawnProperties, pSrcSpawnProperties) : NULL;
	}

	if(pDstTemplate->pWaveProperties && !pDstTemplate->pWaveProperties->bOverrideParentValues)
	{
		StructDestroy(parse_EncounterWaveProperties, pDstTemplate->pWaveProperties);
		pDstTemplate->pWaveProperties = pSrcWaveProperties ? StructClone(parse_EncounterWaveProperties, pSrcWaveProperties) : NULL;
	}

	if(pDstTemplate->pSharedAIProperties && !pDstTemplate->pSharedAIProperties->bOverrideParentValues)
	{
		StructDestroy(parse_EncounterAIProperties, pDstTemplate->pSharedAIProperties);
		pDstTemplate->pSharedAIProperties = pSrcSharedAIProperties ? StructClone(parse_EncounterAIProperties, pSrcSharedAIProperties) : NULL;
	}

	if(pDstTemplate->pRewardProperties && !pDstTemplate->pRewardProperties->bOverrideParentValues)
	{
		StructDestroy(parse_EncounterRewardProperties, pDstTemplate->pRewardProperties);
		pDstTemplate->pRewardProperties = pSrcRewardProprties ? StructClone(parse_EncounterRewardProperties, pSrcRewardProprties) : NULL;
	}

	// Inheritable EArrays

	//append parent jobs to this template's so nothing gets lost. Order will change, but that doesn't matter.
	eaPushStructs(&pDstTemplate->eaJobs, &eaSrcJobs, parse_AIJobDesc);

	if(pDstTemplate->eaActors)
	{
		//if things were overridden, we need to figure out what it was and overlay it on the src data.
		for (i = 0; i < eaSize(&pDstTemplate->eaActors); i++)
		{

			EncounterActorProperties* pMatchingActor = NULL;

			for(j=eaSize(&eaSrcActorDuplicates)-1; j>=0; --j) 
			{
				pMatchingActor = eaSrcActorDuplicates[j];
				if (pMatchingActor->pcName && (stricmp(pMatchingActor->pcName, pDstTemplate->eaActors[i]->pcName) == 0)) {
					break;
				}
				pMatchingActor = NULL;
			}

			if (!pMatchingActor)
			{
				//this is an all-new actor, save it for the new list.
				eaPush(&eaSrcActorDuplicates, StructClone(parse_EncounterActorProperties, pDstTemplate->eaActors[i]));
			}
			else
			{
				ETCopyInheritedActorData(pDstTemplate, pDstTemplate->eaActors[j], pMatchingActor);
			}
		}
		eaDestroyStruct(&pDstTemplate->eaActors, parse_EncounterActorProperties);
	}
	pDstTemplate->eaActors = eaSrcActorDuplicates;
	eaSrcActorDuplicates = NULL;

	eaDestroy(&eaSrcJobs);
	eaDestroy(&eaSrcActors);


	// Individual inheritable fields
	if(!pDstTemplate->pActorSharedProperties) {
		pDstTemplate->pActorSharedProperties = StructClone(parse_EncounterActorSharedProperties, gDefaultActorSharedProperties);
	} else if(IS_HANDLE_ACTIVE(pDstTemplate->pActorSharedProperties->hFaction)){
		REMOVE_HANDLE(pDstTemplate->pActorSharedProperties->hFaction);
	}
	
	if(pDstTemplate->pActorSharedProperties && !pDstTemplate->pActorSharedProperties->bOverrideParentValues) 
	{
		pDstTemplate->pActorSharedProperties->eFactionType = pSrcTemplate && pSrcTemplate->pActorSharedProperties ? pSrcTemplate->pActorSharedProperties->eFactionType : gDefaultActorSharedProperties->eFactionType;
		if(pSrcTemplate && pSrcTemplate->pActorSharedProperties && IS_HANDLE_ACTIVE(pSrcTemplate->pActorSharedProperties->hFaction))
		{
			COPY_HANDLE(pDstTemplate->pActorSharedProperties->hFaction, pSrcTemplate->pActorSharedProperties->hFaction);
		} else {
			COPY_HANDLE(pDstTemplate->pActorSharedProperties->hFaction, gDefaultActorSharedProperties->hFaction);
		}
		pDstTemplate->pActorSharedProperties->iGangID = pSrcTemplate && pSrcTemplate->pActorSharedProperties ? pSrcTemplate->pActorSharedProperties->iGangID : gDefaultActorSharedProperties->iGangID;
		pDstTemplate->pActorSharedProperties->fOverrideSendDistance = pSrcTemplate && pSrcTemplate->pActorSharedProperties ?  pSrcTemplate->pActorSharedProperties->fOverrideSendDistance : gDefaultActorSharedProperties->fOverrideSendDistance;
	}

	SET_HANDLE_FROM_REFERENT(g_hEncounterTemplateDict, NULL, pDstTemplate->hParent);

	// Fill in the empty property structs
	ETFillEmptyStructs(pDstTemplate);
}



static void ETSetNameCB(MEField *pField, bool bFinished, EncounterTemplateEditDoc *pDoc)
{
	// When the name changes, change the title of the window
	ui_WindowSetTitle(pDoc->pMainWindow, pDoc->pTemplate->pcName);

	// Make sure the browser picks up the new template name if the name changed
	sprintf(pDoc->emDoc.doc_name, "%s", pDoc->pTemplate->pcName);
	sprintf(pDoc->emDoc.doc_display_name, "%s", pDoc->pTemplate->pcName);
	pDoc->emDoc.name_changed = 1;

	// Call the scope function to avoid duplicating logic
	ETSetScopeCB(pField, bFinished, pDoc);
}


static void ETCritterTypeChangedCB(MEField *pField, bool bFinished, EncounterTemplateEditDoc *pDoc)
{
	// Make sure ranks are right after a change
	ETFillAllDefaultActorPropertiesInTemplate(pDoc->pTemplate);

	ETFieldChangedCB(pField, bFinished, pDoc);
}



static void ETSetWidgetStatesFromInheritance(UICheckButton *pButton, EncounterTemplateEditDoc *pDoc)
{

	if (IS_HANDLE_ACTIVE(pDoc->pTemplate->hParent))
	{
		int i = 0;
		int j = 0;
		char* pcName = NULL;
		EncounterTemplate* pParent = GET_REF(pDoc->pTemplate->hParent);
		AIJobDesc** eaParentJobs = NULL;
		WorldVariableDef** eaParentVars = NULL;
		WorldVariableDef** eaActorVars = NULL;
		WorldInteractionPropertyEntry** eaActorInteractions = NULL;
		estrStackCreate(&pcName);
		//encounter group
		ui_SetActive(&pDoc->pEncounterExpanderGroup->widget, true);
		ui_SetActive(&pDoc->pActorSharedGroup->pExpander->widget, true);
		ui_SetActive(&pDoc->pAIGroup->pExpander->widget, true);
		ui_SetActive(&pDoc->pJobGroup->pExpander->widget, true);
		ui_SetActive(&pDoc->pSpawnGroup->pExpander->widget, true);
		ui_SetActive(&pDoc->pWaveGroup->pExpander->widget, true);
		ui_SetActive(&pDoc->pRewardsGroup->pExpander->widget, true);

		//sadly, there is no easy way to do this, so I have to disable them all individually.

		//encounter group level props
		if (pDoc->pLevelGroup->pOverrideParentCheckbox && !ui_CheckButtonGetState(pDoc->pLevelGroup->pOverrideParentCheckbox))
		{
			ui_SetActive(SAFE_MEMBER2(pDoc->pLevelGroup, pLevelTypeField, pUIWidget), false);
			ui_SetActive(SAFE_MEMBER2(pDoc->pLevelGroup, pLevelMinField, pUIWidget), false);
			ui_SetActive(SAFE_MEMBER2(pDoc->pLevelGroup, pLevelMaxField, pUIWidget), false);
			ui_SetActive(SAFE_MEMBER2(pDoc->pLevelGroup, pLevelOffsetMinField, pUIWidget), false);
			ui_SetActive(SAFE_MEMBER2(pDoc->pLevelGroup, pLevelOffsetMaxField, pUIWidget), false);
			ui_SetActive(SAFE_MEMBER2(pDoc->pLevelGroup, pMapVarField, pUIWidget), false);
			ui_SetActive(SAFE_MEMBER2(pDoc->pLevelGroup, pClampTypeField, pUIWidget), false);
			ui_SetActive(SAFE_MEMBER2(pDoc->pLevelGroup, pMinLevelField, pUIWidget), false);
			ui_SetActive(SAFE_MEMBER2(pDoc->pLevelGroup, pMaxLevelField, pUIWidget), false);
			ui_SetActive(SAFE_MEMBER2(pDoc->pLevelGroup, pClampOffsetMinField, pUIWidget), false);
			ui_SetActive(SAFE_MEMBER2(pDoc->pLevelGroup, pClampOffsetMaxField, pUIWidget), false);
			ui_SetActive(SAFE_MEMBER2(pDoc->pLevelGroup, pClampMapVarField, pUIWidget), false);
		}

		if (pDoc->pRewardsGroup->pOverrideParentCheckbox && !ui_CheckButtonGetState(pDoc->pRewardsGroup->pOverrideParentCheckbox))
		{
			ui_SetActive(SAFE_MEMBER2(pDoc->pRewardsGroup, pRewardTypeField, pUIWidget), false);
			ui_SetActive(SAFE_MEMBER2(pDoc->pRewardsGroup, pRewardTableField, pUIWidget), false);
			ui_SetActive(SAFE_MEMBER2(pDoc->pRewardsGroup, pRewardLevelTypeField, pUIWidget), false);
			ui_SetActive(SAFE_MEMBER2(pDoc->pRewardsGroup, pRewardLevelField, pUIWidget), false);
		}

		if (pDoc->pActorSharedGroup->pOverrideParentCheckbox && !ui_CheckButtonGetState(pDoc->pActorSharedGroup->pOverrideParentCheckbox))
		{
			ui_SetActive(SAFE_MEMBER2(pDoc->pActorSharedGroup, pFactionTypeField, pUIWidget), false);
			ui_SetActive(SAFE_MEMBER2(pDoc->pActorSharedGroup, pFactionField, pUIWidget), false);
			ui_SetActive(SAFE_MEMBER2(pDoc->pActorSharedGroup, pGangField, pUIWidget), false);
			ui_SetActive(SAFE_MEMBER2(pDoc->pActorSharedGroup, pOverrideSendDistanceField, pUIWidget), false);
		}
		if (pDoc->pAIGroup->pOverrideParentCheckbox && !ui_CheckButtonGetState(pDoc->pAIGroup->pOverrideParentCheckbox))
		{
			ui_SetActive(SAFE_MEMBER2(pDoc->pAIGroup, pCombatRoleField, pUIWidget), false);
			ui_SetActive(SAFE_MEMBER2(pDoc->pAIGroup, pFSMTypeField, pUIWidget), false);
			ui_SetActive(SAFE_MEMBER2(pDoc->pAIGroup, pFSMField, pUIWidget), false);
			//ui_SetActive(&pDoc->pAIGroup->pFSMVarGroup->pExpander->widget, false);
		}
		encounterTemplate_GetEncounterFSMVarDefs(pDoc->pTemplate, &eaParentVars, NULL);
		for (i = 0; i < eaSize(&pDoc->pAIGroup->pFSMVarGroup->eaExtraVarDefGroups); i++)
		{
			bool bEditable = false;
			if (eaFind(&pDoc->pTemplate->pSharedAIProperties->eaVariableDefs, eaParentVars[i]) != -1)
			{
				bEditable = true;
			}
			ui_SetActive(SAFE_MEMBER2(pDoc->pAIGroup->pFSMVarGroup->eaExtraVarDefGroups[i], pChoiceIndexField, pUIWidget), bEditable);
			ui_SetActive(SAFE_MEMBER2(pDoc->pAIGroup->pFSMVarGroup->eaExtraVarDefGroups[i], pChoiceNameField, pUIWidget), bEditable);
			ui_SetActive(SAFE_MEMBER2(pDoc->pAIGroup->pFSMVarGroup->eaExtraVarDefGroups[i], pChoiceTableField, pUIWidget), bEditable);
			ui_SetActive(SAFE_MEMBER2(pDoc->pAIGroup->pFSMVarGroup->eaExtraVarDefGroups[i], pExpressionField, pUIWidget), bEditable);
			ui_SetActive(SAFE_MEMBER2(pDoc->pAIGroup->pFSMVarGroup->eaExtraVarDefGroups[i], pInitFromField, pUIWidget), bEditable);
			ui_SetActive(SAFE_MEMBER2(pDoc->pAIGroup->pFSMVarGroup->eaExtraVarDefGroups[i], pMapVariableField, pUIWidget), bEditable);
			ui_SetActive(SAFE_MEMBER2(pDoc->pAIGroup->pFSMVarGroup->eaExtraVarDefGroups[i], pMissionField, pUIWidget), bEditable);
			ui_SetActive(SAFE_MEMBER2(pDoc->pAIGroup->pFSMVarGroup->eaExtraVarDefGroups[i], pMissionVariableField, pUIWidget), bEditable);
			ui_SetActive(SAFE_MEMBER2(pDoc->pAIGroup->pFSMVarGroup->eaExtraVarDefGroups[i], pNameField, pUIWidget), bEditable);
			ui_SetActive(SAFE_MEMBER2(pDoc->pAIGroup->pFSMVarGroup->eaExtraVarDefGroups[i], pInitFromField, pUIWidget), bEditable);
			ui_SetActive(SAFE_MEMBER2(pDoc->pAIGroup->pFSMVarGroup->eaExtraVarDefGroups[i], pTypeField, pUIWidget), bEditable);
			ui_SetActive(SAFE_MEMBER2(pDoc->pAIGroup->pFSMVarGroup->eaExtraVarDefGroups[i], pValueField, pUIWidget), bEditable);
			ui_SetActive(SAFE_MEMBER2(pDoc->pAIGroup->pFSMVarGroup->eaExtraVarDefGroups[i], pValueField2, pUIWidget), bEditable);
			if (pDoc->pAIGroup->pFSMVarGroup->eaExtraVarDefGroups[i]->pRemoveButton)
				ui_SetActive(&pDoc->pAIGroup->pFSMVarGroup->eaExtraVarDefGroups[i]->pRemoveButton->widget, bEditable);
		}
		encounterTemplate_FillAIJobEArray(pDoc->pTemplate, &eaParentJobs);
		for (i = 0; i < eaSize(&pDoc->pJobGroup->eaSubGroups); i++)
		{
			bool bEditable = false;
			if (eaFind(&pDoc->pTemplate->eaJobs, eaParentJobs[i]) != -1)
			{
				bEditable = true;
			}
			ui_SetActive(SAFE_MEMBER2(pDoc->pJobGroup->eaSubGroups[i], pJobFSMField, pUIWidget), bEditable);
			ui_SetActive(SAFE_MEMBER2(pDoc->pJobGroup->eaSubGroups[i], pJobNameField, pUIWidget), bEditable);
			ui_SetActive(SAFE_MEMBER2(pDoc->pJobGroup->eaSubGroups[i], pJobPriorityField, pUIWidget), bEditable);
			ui_SetActive(SAFE_MEMBER2(pDoc->pJobGroup->eaSubGroups[i], pJobRatingField, pUIWidget), bEditable);
			ui_SetActive(SAFE_MEMBER2(pDoc->pJobGroup->eaSubGroups[i], pJobRequiresField, pUIWidget), bEditable);
			if (pDoc->pJobGroup->eaSubGroups[i]->pRemoveButton)
				ui_SetActive(&pDoc->pJobGroup->eaSubGroups[i]->pRemoveButton->widget, bEditable);
		}
		if (pDoc->pSpawnGroup->pOverrideParentCheckbox && !ui_CheckButtonGetState(pDoc->pSpawnGroup->pOverrideParentCheckbox))
		{
			ui_SetActive(SAFE_MEMBER2(pDoc->pSpawnGroup, pSpawnAnimTypeField, pUIWidget), false);
			ui_SetActive(SAFE_MEMBER2(pDoc->pSpawnGroup, pSpawnAnimField, pUIWidget), false);
			ui_SetActive(SAFE_MEMBER2(pDoc->pSpawnGroup, pSpawnAnimTimeField, pUIWidget), false);
			ui_SetActive(SAFE_MEMBER2(pDoc->pSpawnGroup, pIsAmbushField, pUIWidget), false);
		}
		if (pDoc->pWaveGroup->pOverrideParentCheckbox && !ui_CheckButtonGetState(pDoc->pWaveGroup->pOverrideParentCheckbox))
		{
			ui_SetActive(SAFE_MEMBER2(pDoc->pWaveGroup, pWaveCondField, pUIWidget), false);
			ui_SetActive(SAFE_MEMBER2(pDoc->pWaveGroup, pWaveIntervalTypeField, pUIWidget), false);
			ui_SetActive(SAFE_MEMBER2(pDoc->pWaveGroup, pWaveIntervalField, pUIWidget), false);
			ui_SetActive(SAFE_MEMBER2(pDoc->pWaveGroup, pWaveDelayTypeField, pUIWidget), false);
			ui_SetActive(SAFE_MEMBER2(pDoc->pWaveGroup, pWaveDelayMinField, pUIWidget), false);
			ui_SetActive(SAFE_MEMBER2(pDoc->pWaveGroup, pWaveDelayMaxField, pUIWidget), false);
		}

		ui_SetActive(&pDoc->pActorExpanderGroup->widget, true);
		//actors
		for (i = 0; i < eaSize(&pDoc->eaActorGroups); i++)
		{
			EncounterActorProperties* pTheActor = NULL;
			if (pDoc->eaActorGroups[i]->pUpButton)
				ui_SetActive(&pDoc->eaActorGroups[i]->pUpButton->widget, false);
			if (pDoc->eaActorGroups[i]->pDownButton)
				ui_SetActive(&pDoc->eaActorGroups[i]->pDownButton->widget, false);

			MEFieldGetString(pDoc->eaActorGroups[i]->pNameField, &pcName);
			pTheActor = encounterTemplate_GetActorByName(pParent, pcName);
			//if we don't exist on the parent, everything is enabled.
			if (!encounterTemplate_GetActorByName(pParent, pcName))
			{
				continue;
			}
			ui_SetActive(SAFE_MEMBER(pDoc->eaActorGroups[i]->pNameField, pUIWidget), false);
			if (pDoc->eaActorGroups[i]->pOverrideDisplayNameCheckbox && !ui_CheckButtonGetState(pDoc->eaActorGroups[i]->pOverrideDisplayNameCheckbox))
			{
				ui_SetActive(SAFE_MEMBER(pDoc->eaActorGroups[i]->pDispNameField, pUIWidget), false);
				ui_SetActive(SAFE_MEMBER(pDoc->eaActorGroups[i]->pDispNameTypeField, pUIWidget), false);
				ui_SetActive(SAFE_MEMBER(pDoc->eaActorGroups[i]->pCommentsField, pUIWidget), false);
			}
			ui_SetActive(SAFE_MEMBER(pDoc->eaActorGroups[i]->pSubNameField, pUIWidget), false);
			if (pDoc->eaActorGroups[i]->pOverrideDisplaySubNameCheckbox && !ui_CheckButtonGetState(pDoc->eaActorGroups[i]->pOverrideDisplaySubNameCheckbox))
			{
				ui_SetActive(SAFE_MEMBER(pDoc->eaActorGroups[i]->pDispSubNameField, pUIWidget), false);
				ui_SetActive(SAFE_MEMBER(pDoc->eaActorGroups[i]->pDispSubNameTypeField, pUIWidget), false);
			}
			if (pDoc->eaActorGroups[i]->pOverrideCritterTypeCheckbox && !ui_CheckButtonGetState(pDoc->eaActorGroups[i]->pOverrideCritterTypeCheckbox))
			{
				ui_SetActive(SAFE_MEMBER(pDoc->eaActorGroups[i]->pCritterGroupField, pUIWidget), false);
				ui_SetActive(SAFE_MEMBER(pDoc->eaActorGroups[i]->pCritterDefField, pUIWidget), false);
				ui_SetActive(SAFE_MEMBER(pDoc->eaActorGroups[i]->pCritterRankField, pUIWidget), false);
				ui_SetActive(SAFE_MEMBER(pDoc->eaActorGroups[i]->pCritterSubRankField, pUIWidget), false);
				ui_SetActive(SAFE_MEMBER(pDoc->eaActorGroups[i]->pCritterTypeField, pUIWidget), false);
				ui_SetActive(SAFE_MEMBER(pDoc->eaActorGroups[i]->pLevelOffsetField, pUIWidget), false);
			}
			if (pDoc->eaActorGroups[i]->pOverrideFactionCheckbox && !ui_CheckButtonGetState(pDoc->eaActorGroups[i]->pOverrideFactionCheckbox))
			{
				ui_SetActive(SAFE_MEMBER(pDoc->eaActorGroups[i]->pFactionTypeField, pUIWidget), false);
				ui_SetActive(SAFE_MEMBER(pDoc->eaActorGroups[i]->pFactionField, pUIWidget), false);
				ui_SetActive(SAFE_MEMBER(pDoc->eaActorGroups[i]->pGangField, pUIWidget), false);
			}
			if (pDoc->eaActorGroups[i]->pOverrideSpawnConditionsCheckbox && !ui_CheckButtonGetState(pDoc->eaActorGroups[i]->pOverrideSpawnConditionsCheckbox))
			{
				for (j = 0; j < eaSize(&pDoc->eaActorGroups[i]->eaSpawnGroups); j++)
					ui_SetActive(SAFE_MEMBER(pDoc->eaActorGroups[i]->eaSpawnGroups[j]->pTeamSizeField, pUIWidget), false);

				for (j = 0; j < eaSize(&pDoc->eaActorGroups[i]->eaBossSpawnGroups); j++)
					ui_SetActive(SAFE_MEMBER(pDoc->eaActorGroups[i]->eaBossSpawnGroups[j]->pTeamSizeField, pUIWidget), false);
			}
			if (pDoc->eaActorGroups[i]->pOverrideSpawnAnimCheckbox && !ui_CheckButtonGetState(pDoc->eaActorGroups[i]->pOverrideSpawnAnimCheckbox))
			{
				ui_SetActive(SAFE_MEMBER(pDoc->eaActorGroups[i]->pSpawnAnimField, pUIWidget), false);
				ui_SetActive(SAFE_MEMBER(pDoc->eaActorGroups[i]->pSpawnAnimTypeField, pUIWidget), false);
				ui_SetActive(SAFE_MEMBER(pDoc->eaActorGroups[i]->pSpawnAnimTimeField, pUIWidget), false);
			}
			if (pDoc->eaActorGroups[i]->pOverrideMiscCheckbox && !ui_CheckButtonGetState(pDoc->eaActorGroups[i]->pOverrideMiscCheckbox))
			{
				ui_SetActive(SAFE_MEMBER(pDoc->eaActorGroups[i]->pNonCombatField, pUIWidget), false);
				ui_SetActive(SAFE_MEMBER(pDoc->eaActorGroups[i]->pCombatRoleField, pUIWidget), false);
			}
			if (pDoc->eaActorGroups[i]->pOverrideActorFSMCheckbox && !ui_CheckButtonGetState(pDoc->eaActorGroups[i]->pOverrideActorFSMCheckbox))
			{
				ui_SetActive(SAFE_MEMBER(pDoc->eaActorGroups[i]->pFSMTypeField, pUIWidget), false);
				ui_SetActive(SAFE_MEMBER(pDoc->eaActorGroups[i]->pFSMField, pUIWidget), false);
			}
			eaClear(&eaActorVars);
			encounterTemplate_GetActorFSMVarDefs(pDoc->pTemplate, pDoc->pTemplate->eaActors[i], &eaActorVars, NULL);
			for (j = 0; j < eaSize(&pDoc->eaActorGroups[i]->pFSMVarGroup->eaExtraVarDefGroups); j++)
			{
				bool bEditable = false;
				if (eaSize(&eaActorVars))
				{
					if (eaFind(&pDoc->pTemplate->eaActors[i]->eaVariableDefs, eaActorVars[j]) != -1)
					{
						bEditable = true;
					}
				}
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->pFSMVarGroup->eaExtraVarDefGroups[j], pChoiceIndexField, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->pFSMVarGroup->eaExtraVarDefGroups[j], pChoiceNameField, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->pFSMVarGroup->eaExtraVarDefGroups[j], pChoiceTableField, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->pFSMVarGroup->eaExtraVarDefGroups[j], pExpressionField, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->pFSMVarGroup->eaExtraVarDefGroups[j], pInitFromField, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->pFSMVarGroup->eaExtraVarDefGroups[j], pMapVariableField, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->pFSMVarGroup->eaExtraVarDefGroups[j], pMissionField, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->pFSMVarGroup->eaExtraVarDefGroups[j], pMissionVariableField, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->pFSMVarGroup->eaExtraVarDefGroups[j], pNameField, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->pFSMVarGroup->eaExtraVarDefGroups[j], pInitFromField, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->pFSMVarGroup->eaExtraVarDefGroups[j], pTypeField, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->pFSMVarGroup->eaExtraVarDefGroups[j], pValueField, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->pFSMVarGroup->eaExtraVarDefGroups[j], pValueField2, pUIWidget), bEditable);
				if (pDoc->eaActorGroups[i]->pFSMVarGroup->eaExtraVarDefGroups[j]->pRemoveButton)
					ui_SetActive(&pDoc->eaActorGroups[i]->pFSMVarGroup->eaExtraVarDefGroups[j]->pRemoveButton->widget, bEditable);
			}
			eaClear(&eaActorInteractions);
			encounterTemplate_FillActorInteractionEarray(pDoc->pTemplate, pTheActor, &eaActorInteractions);
			for (j = 0; j < eaSize(&pDoc->eaActorGroups[i]->eaInteractGroups); j++)
			{
				bool bEditable = false;
				WorldInteractionProperties* pProps = encounterTemplate_GetActorInteractionProps(pDoc->pTemplate, pDoc->pTemplate->eaActors[i]);
				if (pDoc->pTemplate->eaActors[i]->pInteractionProperties && eaSize(&pDoc->pTemplate->eaActors[i]->pInteractionProperties->eaEntries) && eaSize(&eaActorInteractions) && pProps->eaEntries && eaSize(&pProps->eaEntries))
				{
					if (eaFind(&pDoc->pTemplate->eaActors[i]->pInteractionProperties->eaEntries, eaActorInteractions[j]) != -1)
					{
						bEditable = true;
					}
				}
				ui_SetActive(SAFE_MEMBER_ADDR2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pInteractButton, widget), bEditable);
				ui_SetActive(SAFE_MEMBER_ADDR2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pInteractButton, widget), bEditable);
				ui_SetActive(SAFE_MEMBER_ADDR2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pVisibleButton, widget), bEditable);
				ui_SetActive(SAFE_MEMBER_ADDR2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pCategoryButton, widget), bEditable);
				ui_SetActive(SAFE_MEMBER_ADDR2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pExclusiveButton, widget), bEditable);
				ui_SetActive(SAFE_MEMBER_ADDR2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pTimeButton, widget), bEditable);
				ui_SetActive(SAFE_MEMBER_ADDR2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pTextButton, widget), bEditable);
				ui_SetActive(SAFE_MEMBER_ADDR2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pRewardButton, widget), bEditable);
				ui_SetActive(SAFE_MEMBER_ADDR2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pAnimationButton, widget), bEditable);
				ui_SetActive(SAFE_MEMBER_ADDR2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pSoundButton, widget), bEditable);
				ui_SetActive(SAFE_MEMBER_ADDR2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pActionButton, widget), bEditable);
				ui_SetActive(SAFE_MEMBER_ADDR2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pSuccessGameActionsButton, widget), bEditable);
				ui_SetActive(SAFE_MEMBER_ADDR2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pFailureGameActionsButton, widget), bEditable);
				ui_SetActive(SAFE_MEMBER_ADDR2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pContactButton, widget), bEditable);
				ui_SetActive(SAFE_MEMBER_ADDR2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pVarAddButton, widget), bEditable);
				ui_SetActive(SAFE_MEMBER_ADDR2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pShowDefValues, widget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pClassField, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pInteractCondField, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pSuccessCondField, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pVisibleExprField, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pCategoryField, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pPriorityField, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pInteractionCategoryField, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pAutoExecField, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pUseTimeField, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pActiveTimeField, pUIWidget), bEditable);

				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pCooldownTimeField, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pCustomCooldownField, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pDynamicCooldownField, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pTeamUsableWhenActiveField, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pHideDuringCooldownField, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pInterruptOnPowerField, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pInterruptOnDamageField, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pInterruptOnMoveField, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pNoRespawnField, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pUsabilityTextField, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pInteractTextField, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pDetailTextField, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pDetailTextureField, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pSuccessTextField, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pFailureTextField, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pAnimField, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pAttemptSoundField, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pSuccessSoundField, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pFailureSoundField, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pInterruptSoundField, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pAttemptActionField, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pSuccessActionField, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pFailureActionField, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pInterruptActionField, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pNoLongerActiveActionField, pUIWidget), bEditable);

				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pCooldownActionField, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pRewardTableField, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pRewardLevelTypeField, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pRewardLevelField, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pRewardLevelMapVarField, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pContactField, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pContactDialogField, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pCraftSkillField, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pCraftMaxSkillField, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pCraftReward1Field, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pCraftReward2Field, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pCraftReward3Field, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pCritterField, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pCritterOverrideField, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pCritterLevelField, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pDisplayNameField, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pEntityNameField, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pRespawnTimeField, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pDeathPowerField, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pPerPlayerDoorField, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pSinglePlayerDoorField, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pCollectDestStatusField, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pAllowJoinTeamAtDoorField, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pDoorTypeField, pUIWidget), bEditable);

				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pDoorIDField, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pDoorQueueField, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pDoorTransitionField, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pDoorKeyField, pUIWidget), bEditable);
				ui_SetActive(SAFE_MEMBER2(pDoc->eaActorGroups[i]->eaInteractGroups[j]->pInteractPropsGroup, pInteractDefField, pUIWidget), bEditable);

				if (pDoc->eaActorGroups[i]->eaInteractGroups[j]->pRemoveButton)
					ui_SetActive(&pDoc->eaActorGroups[i]->eaInteractGroups[j]->pRemoveButton->widget, bEditable);
			}
			if (pDoc->eaActorGroups[i]->pRemoveButton)
				ui_SetActive(&pDoc->eaActorGroups[i]->pRemoveButton->widget, false);
		}
		estrDestroy(&pcName);
		eaDestroy(&eaActorVars);
		eaDestroy(&eaParentVars);
		eaDestroy(&eaActorInteractions);
	}
	else
	{
		ui_SetActive(&pDoc->pActorExpanderGroup->widget, true);
		ui_SetActive(&pDoc->pEncounterExpanderGroup->widget, true);
	}

}

static EncounterActorProperties* ETGetOrCreateActorFromParent(EncounterTemplateEditDoc *pDoc, const char* pName)
{
	// Make sure the resource is checked out of Gimme
	int i;
	EncounterActorProperties* pActor = NULL;
	for (i = 0; i < eaSize(&pDoc->pTemplate->eaActors); i++)
	{
		if (stricmp(pDoc->pTemplate->eaActors[i]->pcName, pName) == 0)
		{
			pActor = pDoc->pTemplate->eaActors[i];
		}
	}
	if (!pActor)
	{
		pActor = StructCreate(parse_EncounterActorProperties);
		pActor->pcName = allocAddString(pName);
		eaPush(&pDoc->pTemplate->eaActors, pActor);
	}
	return pActor;
}

static void ETOverrideEncounterLevelPropsToggled(UICheckButton *pButton, EncounterTemplateEditDoc *pDoc)
{
	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pDoc->emDoc, true) || !pButton || !pDoc) {
		return;
	}
	pDoc->pTemplate->pLevelProperties->bOverrideParentValues = ui_CheckButtonGetState(pDoc->pLevelGroup->pOverrideParentCheckbox);

	ETFieldChangedCB(NULL, true, pDoc);
	ETSetWidgetStatesFromInheritance(NULL, pDoc);
}
static void ETOverrideEncounterActorSharedPropsToggled(UICheckButton *pButton, EncounterTemplateEditDoc *pDoc)
{
	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pDoc->emDoc, true) || !pButton || !pDoc) {
		return;
	}
	pDoc->pTemplate->pActorSharedProperties->bOverrideParentValues = ui_CheckButtonGetState(pDoc->pActorSharedGroup->pOverrideParentCheckbox);
	ETFieldChangedCB(NULL, true, pDoc);
	ETSetWidgetStatesFromInheritance(NULL, pDoc);
}
static void ETOverrideEncounterActorAIPropsToggled(UICheckButton *pButton, EncounterTemplateEditDoc *pDoc)
{
	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pDoc->emDoc, true) || !pButton || !pDoc) {
		return;
	}
	pDoc->pTemplate->pSharedAIProperties->bOverrideParentValues = ui_CheckButtonGetState(pDoc->pAIGroup->pOverrideParentCheckbox);
	ETFieldChangedCB(NULL, true, pDoc);
	ETSetWidgetStatesFromInheritance(NULL, pDoc);
}
static void ETOverrideEncounterSpawnPropsToggled(UICheckButton *pButton, EncounterTemplateEditDoc *pDoc)
{
	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pDoc->emDoc, true) || !pButton || !pDoc) {
		return;
	}
	pDoc->pTemplate->pSpawnProperties->bOverrideParentValues = ui_CheckButtonGetState(pDoc->pSpawnGroup->pOverrideParentCheckbox);
	ETFieldChangedCB(NULL, true, pDoc);
	ETSetWidgetStatesFromInheritance(NULL, pDoc);
}
static void ETOverrideEncounterRewardPropsToggled(UICheckButton *pButton, EncounterTemplateEditDoc *pDoc)
{
	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pDoc->emDoc, true) || !pButton || !pDoc) {
		return;
	}
	pDoc->pTemplate->pRewardProperties->bOverrideParentValues = ui_CheckButtonGetState(pDoc->pRewardsGroup->pOverrideParentCheckbox);
	ETFieldChangedCB(NULL, true, pDoc);
	ETSetWidgetStatesFromInheritance(NULL, pDoc);
}

static void ETOverrideEncounterWavePropsToggled(UICheckButton *pButton, EncounterTemplateEditDoc *pDoc)
{
	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pDoc->emDoc, true) || !pButton || !pDoc) {
		return;
	}
	pDoc->pTemplate->pWaveProperties->bOverrideParentValues = ui_CheckButtonGetState(pDoc->pWaveGroup->pOverrideParentCheckbox);
	ETFieldChangedCB(NULL, true, pDoc);
	ETSetWidgetStatesFromInheritance(NULL, pDoc);
}

static void ETOverrideActorCritterTypeToggled(UICheckButton *pButton, ETActorGroup *pGroup)
{
	EncounterActorProperties* pActor = NULL;
	char* pName = NULL;
	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pGroup->pDoc->emDoc, true) || !pButton || !pGroup->pDoc) {
		return;
	}

	estrStackCreate(&pName);
	MEFieldGetString(pGroup->pNameField, &pName);
	pActor = ETGetOrCreateActorFromParent(pGroup->pDoc, pName);
	estrDestroy(&pName);

	pActor->bOverrideCritterType = ui_CheckButtonGetState(pGroup->pOverrideCritterTypeCheckbox);
	ETFillDefaultActorProperties(pActor, pGroup->pDoc->pTemplate);
	if (GET_REF(pGroup->pDoc->pTemplate->hParent) && pActor->bOverrideCritterType)
	{
		pActor->bOverrideCritterType = false;//turn off temporarily to allow lookup to succeed
		
		pActor->critterProps.eCritterType = encounterTemplate_GetActorCritterType(pGroup->pDoc->pTemplate, pActor);
		pActor->critterProps.pcRank = encounterTemplate_GetActorRank(pGroup->pDoc->pTemplate, pActor);
		pActor->critterProps.pcSubRank = encounterTemplate_GetActorSubRank(pGroup->pDoc->pTemplate, pActor);
		pActor->critterProps.iLevelOffset = encounterTemplate_GetActorLevelOffset(pGroup->pDoc->pTemplate, pActor);
		if (encounterTemplate_GetActorCritterTypeMapVarName(pGroup->pDoc->pTemplate, pActor))
			pActor->critterProps.pcCritterMapVariable = strdup(encounterTemplate_GetActorCritterTypeMapVarName(pGroup->pDoc->pTemplate, pActor));
		SET_HANDLE_FROM_REFERENT(g_hCritterDefDict, encounterTemplate_GetActorCritterDef(pGroup->pDoc->pTemplate, pActor, 0), pActor->critterProps.hCritterDef);
		SET_HANDLE_FROM_REFERENT(g_hCritterGroupDict, encounterTemplate_GetActorCritterGroup(pGroup->pDoc->pTemplate, pActor, 0, pActor->critterProps.eCritterType), pActor->critterProps.hCritterDef);
		
		pActor->bOverrideCritterType = true;
	}

	ETFieldChangedCB(NULL, true, pGroup->pDoc);
	ETSetWidgetStatesFromInheritance(NULL, pGroup->pDoc);
}

static void ETOverrideActorDisplayNameToggled(UICheckButton *pButton, ETActorGroup *pGroup)
{
	EncounterActorProperties* pActor = NULL;
	char* pName = NULL;
	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pGroup->pDoc->emDoc, true) || !pButton || !pGroup->pDoc) {
		return;
	}

	estrStackCreate(&pName);
	MEFieldGetString(pGroup->pNameField, &pName);
	pActor = ETGetOrCreateActorFromParent(pGroup->pDoc, pName);
	estrDestroy(&pName);

	pActor->bOverrideDisplayName = ui_CheckButtonGetState(pGroup->pOverrideDisplayNameCheckbox);
	ETFillDefaultActorProperties(pActor, pGroup->pDoc->pTemplate);
	ETFieldChangedCB(NULL, true, pGroup->pDoc);
	ETSetWidgetStatesFromInheritance(NULL, pGroup->pDoc);
}

static void ETOverrideActorDisplaySubNameToggled(UICheckButton *pButton, ETActorGroup *pGroup)
{
	EncounterActorProperties* pActor = NULL;
	char* pName = NULL;
	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pGroup->pDoc->emDoc, true) || !pButton || !pGroup->pDoc) {
		return;
	}

	estrStackCreate(&pName);
	MEFieldGetString(pGroup->pSubNameField, &pName);
	pActor = ETGetOrCreateActorFromParent(pGroup->pDoc, pName);
	estrDestroy(&pName);

	pActor->bOverrideDisplaySubName = ui_CheckButtonGetState(pGroup->pOverrideDisplaySubNameCheckbox);
	ETFillDefaultActorProperties(pActor, pGroup->pDoc->pTemplate);
	ETFieldChangedCB(NULL, true, pGroup->pDoc);
	ETSetWidgetStatesFromInheritance(NULL, pGroup->pDoc);
}

static void ETOverrideActorFactionToggled(UICheckButton *pButton, ETActorGroup *pGroup)
{
	EncounterActorProperties* pActor = NULL;
	char* pName = NULL;
	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pGroup->pDoc->emDoc, true) || !pButton || !pGroup->pDoc) {
		return;
	}

	estrStackCreate(&pName);
	MEFieldGetString(pGroup->pNameField, &pName);
	pActor = ETGetOrCreateActorFromParent(pGroup->pDoc, pName);
	estrDestroy(&pName);

	pActor->bOverrideFaction = ui_CheckButtonGetState(pGroup->pOverrideFactionCheckbox);
	ETFillDefaultActorProperties(pActor, pGroup->pDoc->pTemplate);
	ETFieldChangedCB(NULL, true, pGroup->pDoc);
	ETSetWidgetStatesFromInheritance(NULL, pGroup->pDoc);
}

static void ETOverrideActorSpawnConditionsToggled(UICheckButton *pButton, ETActorGroup *pGroup)
{
	EncounterActorProperties* pActor = NULL;
	char* pName = NULL;
	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pGroup->pDoc->emDoc, true) || !pButton || !pGroup->pDoc) {
		return;
	}

	estrStackCreate(&pName);
	MEFieldGetString(pGroup->pNameField, &pName);
	pActor = ETGetOrCreateActorFromParent(pGroup->pDoc, pName);
	estrDestroy(&pName);

	pActor->bOverrideSpawnConditions = ui_CheckButtonGetState(pGroup->pOverrideSpawnConditionsCheckbox);
	ETFillDefaultActorProperties(pActor, pGroup->pDoc->pTemplate);
	ETFieldChangedCB(NULL, true, pGroup->pDoc);
	ETSetWidgetStatesFromInheritance(NULL, pGroup->pDoc);
}

static void ETOverrideActorSpawnAnimToggled(UICheckButton *pButton, ETActorGroup *pGroup)
{
	EncounterActorProperties* pActor = NULL;
	char* pName = NULL;
	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pGroup->pDoc->emDoc, true) || !pButton || !pGroup->pDoc) {
		return;
	}

	estrStackCreate(&pName);
	MEFieldGetString(pGroup->pNameField, &pName);
	pActor = ETGetOrCreateActorFromParent(pGroup->pDoc, pName);
	estrDestroy(&pName);

	pActor->bOverrideCritterSpawnInfo = ui_CheckButtonGetState(pGroup->pOverrideSpawnAnimCheckbox);
	ETFillDefaultActorProperties(pActor, pGroup->pDoc->pTemplate);
	ETFieldChangedCB(NULL, true, pGroup->pDoc);
	ETSetWidgetStatesFromInheritance(NULL, pGroup->pDoc);
}

static void ETOverrideActorFSMToggled(UICheckButton *pButton, ETActorGroup *pGroup)
{
	EncounterActorProperties* pActor = NULL;
	char* pName = NULL;
	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pGroup->pDoc->emDoc, true) || !pButton || !pGroup->pDoc) {
		return;
	}

	estrStackCreate(&pName);
	MEFieldGetString(pGroup->pNameField, &pName);
	pActor = ETGetOrCreateActorFromParent(pGroup->pDoc, pName);
	estrDestroy(&pName);

	pActor->bOverrideFSMInfo = ui_CheckButtonGetState(pGroup->pOverrideActorFSMCheckbox);
	ETFillDefaultActorProperties(pActor, pGroup->pDoc->pTemplate);
	ETFieldChangedCB(NULL, true, pGroup->pDoc);
	ETSetWidgetStatesFromInheritance(NULL, pGroup->pDoc);
}

static void ETOverrideActorMiscToggled(UICheckButton *pButton, ETActorGroup *pGroup)
{
	EncounterActorProperties* pActor = NULL;
	char* pName = NULL;
	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pGroup->pDoc->emDoc, true) || !pButton || !pGroup->pDoc) {
		return;
	}

	estrStackCreate(&pName);
	MEFieldGetString(pGroup->pNameField, &pName);
	pActor = ETGetOrCreateActorFromParent(pGroup->pDoc, pName);
	estrDestroy(&pName);

	pActor->bOverrideMisc = ui_CheckButtonGetState(pGroup->pOverrideMiscCheckbox);
	ETFillDefaultActorProperties(pActor, pGroup->pDoc->pTemplate);
	ETFieldChangedCB(NULL, true, pGroup->pDoc);
	ETSetWidgetStatesFromInheritance(NULL, pGroup->pDoc);
}

static void ETSetParentCB(MEField *pField, bool bFinished, EncounterTemplateEditDoc *pDoc)
{
	char* estrData = NULL;

	if (!pDoc || !pField || pDoc->bIgnoreFieldChanges) 
	{
		return;
	}
	estrStackCreate(&estrData);
	MEFieldGetString(pField, &estrData);
	ETFillEmptyStructs(pDoc->pTemplate);
	if(bFinished)
	{
		EncounterTemplate *pOrigTemplate = pDoc->pOrigTemplate;
		EncounterTemplate *pOrigParentTemplate = pOrigTemplate ? GET_REF(pOrigTemplate->hParent) : NULL;
		int i;
		EncounterTemplate* pParent = GET_REF(pDoc->pTemplate->hParent);
		if(pOrigParentTemplate && !pParent)
		{
			ETCopyInheritableFields(pDoc->pTemplate, pOrigParentTemplate);
		}
		else if (pParent)
		{
			for (i = 0; i < eaSize(&pDoc->pTemplate->eaActors); i++)
			{
				if (!encounterTemplate_GetActorByName(pParent, pDoc->pTemplate->eaActors[i]->pcName))
				{
					EncounterActorProperties* pOrigActorProps = encounterTemplate_GetActorByName(pDoc->pOrigTemplate, pDoc->pTemplate->eaActors[i]->pcName);
					ETCopyInheritedActorData(pDoc->pOrigTemplate, pOrigActorProps, pDoc->pTemplate->eaActors[i]);
				}
			}
		}
		ETFieldChangedCB(pField, bFinished, pDoc);
		ETSetWidgetStatesFromInheritance(NULL, pDoc);
	}
	estrDestroy(&estrData);
}


static void ETRefreshButtonSet(UIExpander *pExpander, F32 x, F32 y, bool bUp, bool bDown, const char *pcRemoveText, UIButton **ppRemoveButton, UIActivationFunc pRemoveFunc, const char* pcCloneText, UIButton **ppCloneButton, UIActivationFunc pCloneFunc, UIButton **ppUpButton, UIActivationFunc pUpFunc, UIButton **ppDownButton, UIActivationFunc pDownFunc, void *pGroup)
{
	// Update remove button
	if (!*ppRemoveButton) {
		*ppRemoveButton = ui_ButtonCreate(pcRemoveText, X_OFFSET_BASE+x, y, pRemoveFunc, pGroup);
		ui_WidgetSetWidth(UI_WIDGET(*ppRemoveButton), 100);
		ui_ExpanderAddChild(pExpander, *ppRemoveButton);
	} else {
		ui_WidgetSetPosition(UI_WIDGET(*ppRemoveButton), X_OFFSET_BASE+x, y);
	}

	// Update clone button
	if (!*ppCloneButton) {
		*ppCloneButton = ui_ButtonCreate(pcCloneText, X_OFFSET_BASE+110+x, y, pCloneFunc, pGroup);
		ui_WidgetSetWidth(UI_WIDGET(*ppCloneButton), 100);
		ui_ExpanderAddChild(pExpander, *ppCloneButton);
	} else {
		ui_WidgetSetPosition(UI_WIDGET(*ppCloneButton), X_OFFSET_BASE+110+x, y);
	}

	// Update up button
	if (!*ppUpButton) {
		*ppUpButton = ui_ButtonCreate("Up", X_OFFSET_BASE+220+x, y, pUpFunc, pGroup);
		ui_WidgetSetWidth(UI_WIDGET(*ppUpButton), 60);
		ui_ExpanderAddChild(pExpander, *ppUpButton);
	} else {
		ui_WidgetSetPosition(UI_WIDGET(*ppUpButton), X_OFFSET_BASE+220+x, y);
	}
	ui_SetActive(UI_WIDGET(*ppUpButton), bUp);

	// Update down button
	if (!*ppDownButton) {
		*ppDownButton = ui_ButtonCreate("Down", X_OFFSET_BASE+390+x, y, pDownFunc, pGroup);
		ui_WidgetSetWidth(UI_WIDGET(*ppDownButton), 60);
		ui_ExpanderAddChild(pExpander, *ppDownButton);
	} else {
		ui_WidgetSetPosition(UI_WIDGET(*ppDownButton), X_OFFSET_BASE+390+x, y);
	}
	ui_SetActive(UI_WIDGET(*ppDownButton), bDown);

}

static void ETRefreshLevel(EncounterTemplateEditDoc *pDoc)
{
	EncounterLevelProperties *pLevel, *pOrigLevel = NULL;
	ETLevelGroup *pGroup;
	F32 y = 0;

	if (!pDoc->pLevelGroup) {
		pDoc->pLevelGroup = calloc(1,sizeof(ETLevelGroup));
		pDoc->pLevelGroup->pExpander = ETCreateExpander(pDoc->pEncounterExpanderGroup, "Encounter Level", 0);

	}


	pGroup = pDoc->pLevelGroup;

	if (ETRefreshOverrideCheckBox(&pGroup->pOverrideParentCheckbox, &pGroup->pExpander, "Override Parent", 16, y, pDoc->pTemplate->pLevelProperties->bOverrideParentValues, pDoc, ETOverrideEncounterLevelPropsToggled, pDoc, true))
		y += STANDARD_ROW_HEIGHT;

	pLevel = encounterTemplate_GetLevelProperties(pDoc->pTemplate);
	if (pDoc->pOrigTemplate) {
		pOrigLevel = encounterTemplate_GetLevelProperties(pDoc->pOrigTemplate);
	}

	if(!pLevel)
		return;


	pGroup->pLevelTypeLabel = ETRefreshLabel(pGroup->pLevelTypeLabel, "Encounter Level", "Specifies how the encounter level will be chosen.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
	pGroup->pLevelTypeField = ETRefreshEnumField(pGroup->pLevelTypeField, pOrigLevel, pLevel, parse_EncounterLevelProperties, "LevelType", EncounterLevelTypeEnum,
									pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 140, UIUnitFixed, 5, pDoc);
	y += STANDARD_ROW_HEIGHT;

	if (pLevel->eLevelType == EncounterLevelType_Specified) {
		pGroup->pLevelMinLabel = ETRefreshLabel(pGroup->pLevelMinLabel, "Level Value: Min", "Specifies the min and max level.  A value of zero in either position means no min or max (respectively).", X_OFFSET_BASE, 0, y, pGroup->pExpander);
		pGroup->pLevelMinField = ETRefreshSimpleField(pGroup->pLevelMinField, pOrigLevel, pLevel, parse_EncounterLevelProperties, "SpecifiedLevelMin", kMEFieldType_TextEntry,
			pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 60, UIUnitFixed, 5, pDoc);

		pGroup->pLevelMaxLabel = ETRefreshLabel(pGroup->pLevelMaxLabel, "Max", "Specifies the min and max level.  A value of zero in either position means no min or max (respectively).", X_OFFSET_CONTROL+65, 0, y, pGroup->pExpander);
		pGroup->pLevelMaxField = ETRefreshSimpleField(pGroup->pLevelMaxField, pOrigLevel, pLevel, parse_EncounterLevelProperties, "SpecifiedLevelMax", kMEFieldType_TextEntry,
			pGroup->pExpander, X_OFFSET_CONTROL+95, y, 0, 60, UIUnitFixed, 5, pDoc);
		y += STANDARD_ROW_HEIGHT;
	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pLevelMinLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pLevelMaxLabel);
		MEFieldSafeDestroy(&pGroup->pLevelMinField);
		MEFieldSafeDestroy(&pGroup->pLevelMaxField);
	}

	if (pLevel->eLevelType == EncounterLevelType_MapVariable) {
		pGroup->pMapVarLabel = ETRefreshLabel(pGroup->pMapVarLabel, "Map Variable", "The map variable to be used to determine the encounter level.", X_OFFSET_BASE+20, 0, y, pGroup->pExpander);
		pGroup->pMapVarField = ETRefreshDataField(pGroup->pMapVarField, pOrigLevel, pLevel, parse_EncounterLevelProperties, "MapVariable", &pDoc->eaVarNames, false,
								pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
		y += STANDARD_ROW_HEIGHT;
	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pMapVarLabel);
		MEFieldSafeDestroy(&pGroup->pMapVarField);
	}

	if (pLevel->eLevelType != EncounterLevelType_Specified) {
		pGroup->pLevelOffsetMinLabel = ETRefreshLabel(pGroup->pLevelOffsetMinLabel, "Level Offset: Min", "Specified the min and max level offset.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
		pGroup->pLevelOffsetMinField = ETRefreshSimpleField(pGroup->pLevelOffsetMinField, pOrigLevel, pLevel, parse_EncounterLevelProperties, "LevelOffsetMin", kMEFieldType_TextEntry,
			pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 60, UIUnitFixed, 5, pDoc);

		pGroup->pLevelOffsetMaxLabel = ETRefreshLabel(pGroup->pLevelOffsetMaxLabel, "Max", "Specified the min and max level offset.", X_OFFSET_CONTROL+65, 0, y, pGroup->pExpander);
		pGroup->pLevelOffsetMaxField = ETRefreshSimpleField(pGroup->pLevelOffsetMaxField, pOrigLevel, pLevel, parse_EncounterLevelProperties, "LevelOffsetMax", kMEFieldType_TextEntry,
			pGroup->pExpander, X_OFFSET_CONTROL+95, y, 0, 60, UIUnitFixed, 5, pDoc);
		y += STANDARD_ROW_HEIGHT;

	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pLevelOffsetMinLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pLevelOffsetMaxLabel);
		MEFieldSafeDestroy(&pGroup->pLevelOffsetMinField);
		MEFieldSafeDestroy(&pGroup->pLevelOffsetMaxField);
	}

	if (pLevel->eLevelType != EncounterLevelType_Specified) {
		pGroup->pClampTypeLabel = ETRefreshLabel(pGroup->pClampTypeLabel, "Clamp Level", "Specifies how the encounter level will be clamped.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
		pGroup->pClampTypeField = ETRefreshEnumField(pGroup->pClampTypeField, pOrigLevel, pLevel, parse_EncounterLevelProperties, "ClampType", EncounterLevelClampTypeEnum,
										pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 140, UIUnitFixed, 5, pDoc);
		y += STANDARD_ROW_HEIGHT;

		if (pLevel->eClampType == EncounterLevelClampType_Specified) {
			pGroup->pMinLevelLabel = ETRefreshLabel(pGroup->pMinLevelLabel, "Clamp Level: Min", "Specified the min and max level.  A value of zero in either position means no min or max (respectively).", X_OFFSET_BASE, 0, y, pGroup->pExpander);
			pGroup->pMinLevelField = ETRefreshSimpleField(pGroup->pMinLevelField, pOrigLevel, pLevel, parse_EncounterLevelProperties, "MinLevel", kMEFieldType_TextEntry,
										pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 60, UIUnitFixed, 5, pDoc);

			pGroup->pMaxLevelLabel = ETRefreshLabel(pGroup->pMaxLevelLabel, "Max", "Specified the min and max level.  A value of zero in either position means no min or max (respectively).", X_OFFSET_CONTROL+65, 0, y, pGroup->pExpander);
			pGroup->pMaxLevelField = ETRefreshSimpleField(pGroup->pMaxLevelField, pOrigLevel, pLevel, parse_EncounterLevelProperties, "MaxLevel", kMEFieldType_TextEntry,
										pGroup->pExpander, X_OFFSET_CONTROL+95, y, 0, 60, UIUnitFixed, 5, pDoc);
			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pMinLevelLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pMaxLevelLabel);
			MEFieldSafeDestroy(&pGroup->pMinLevelField);
			MEFieldSafeDestroy(&pGroup->pMaxLevelField);
		}

		if (pLevel->eClampType == EncounterLevelClampType_MapVariable) {
			pGroup->pClampMapVarLabel = ETRefreshLabel(pGroup->pClampMapVarLabel, "Clamp Variable", "The map variable to be used to determine the encounter level clamping.", X_OFFSET_BASE+20, 0, y, pGroup->pExpander);
			pGroup->pClampMapVarField = ETRefreshDataField(pGroup->pClampMapVarField, pOrigLevel, pLevel, parse_EncounterLevelProperties, "ClampMapVariable", &pDoc->eaVarNames, false,
									pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pClampMapVarLabel);
			MEFieldSafeDestroy(&pGroup->pClampMapVarField);
		}

		if (pLevel->eClampType != EncounterLevelClampType_Specified) {
			pGroup->pClampOffsetMinLabel = ETRefreshLabel(pGroup->pClampOffsetMinLabel, "Clamp Offset: Min", "Specified the min and max level offset for clamping.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
			pGroup->pClampOffsetMinField = ETRefreshSimpleField(pGroup->pClampOffsetMinField, pOrigLevel, pLevel, parse_EncounterLevelProperties, "ClampOffsetMin", kMEFieldType_TextEntry,
				pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 60, UIUnitFixed, 5, pDoc);

			pGroup->pClampOffsetMaxLabel = ETRefreshLabel(pGroup->pClampOffsetMaxLabel, "Max", "Specified the min and max level offset for clamping.", X_OFFSET_CONTROL+65, 0, y, pGroup->pExpander);
			pGroup->pClampOffsetMaxField = ETRefreshSimpleField(pGroup->pClampOffsetMaxField, pOrigLevel, pLevel, parse_EncounterLevelProperties, "ClampOffsetMax", kMEFieldType_TextEntry,
				pGroup->pExpander, X_OFFSET_CONTROL+95, y, 0, 60, UIUnitFixed, 5, pDoc);
			y += STANDARD_ROW_HEIGHT;

		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pClampOffsetMinLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pClampOffsetMaxLabel);
			MEFieldSafeDestroy(&pGroup->pClampOffsetMinField);
			MEFieldSafeDestroy(&pGroup->pClampOffsetMaxField);
		}

	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pClampTypeLabel);
		MEFieldSafeDestroy(&pGroup->pClampTypeField);
	}

	ui_ExpanderSetHeight(pGroup->pExpander, y);
}

static U32 ETRefreshDifficulty(EncounterTemplateEditDoc *pDoc)
{
	EncounterDifficultyProperties *pDifficulty, *pOrigDifficulty = NULL;
	ETDifficultyGroup *pGroup;
	F32 y = 0;
	U32 iExpanderIdx = 1;

	if(encounter_GetEncounterDifficultiesCount() <= 1)
	{
		if(pDoc->pDifficultyGroup)
			ETFreeDifficultyGroup(pDoc->pDifficultyGroup);
		return iExpanderIdx;
	}

	if (!pDoc->pDifficultyGroup) {
		pDoc->pDifficultyGroup = calloc(1,sizeof(ETDifficultyGroup));
		pDoc->pDifficultyGroup->pExpander = ETCreateExpander(pDoc->pEncounterExpanderGroup, "Encounter Difficulty", 1);
	}
	iExpanderIdx++;
	pGroup = pDoc->pDifficultyGroup;
	pDifficulty = encounterTemplate_GetDifficultyProperties(pDoc->pTemplate);
	if (pDoc->pOrigTemplate) {
		pOrigDifficulty = encounterTemplate_GetDifficultyProperties(pDoc->pOrigTemplate);
	}

	if(!pDifficulty)
		return iExpanderIdx;

	pGroup->pDifficultyTypeLabel = ETRefreshLabel(pGroup->pDifficultyTypeLabel, "Enc. Difficulty", "Specifies how the encounter Difficulty will be chosen.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
	pGroup->pDifficultyTypeField = ETRefreshEnumField(pGroup->pDifficultyTypeField, pOrigDifficulty, pDifficulty, parse_EncounterDifficultyProperties, "DifficultyType", EncounterDifficultyTypeEnum,
		pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 140, UIUnitFixed, 5, pDoc);
	if(pGroup->pDifficultyTypeField && pGroup->pDifficultyTypeField->pUICombo)
		pGroup->pDifficultyTypeField->pUICombo->bDontSortList = true;
	y += STANDARD_ROW_HEIGHT;

	if (pDifficulty->eDifficultyType == EncounterDifficultyType_Specified) {
		pGroup->pDifficultyLabel = ETRefreshLabel(pGroup->pDifficultyLabel, "Value", "Specifies the Difficulty.", X_OFFSET_BASE+20, 0, y, pGroup->pExpander);
		pGroup->pDifficultyField = ETRefreshEnumField(pGroup->pDifficultyField, pOrigDifficulty, pDifficulty, parse_EncounterDifficultyProperties, "SpecifiedDifficulty", EncounterDifficultyEnum,
			pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 60, UIUnitFixed, 5, pDoc);

		y += STANDARD_ROW_HEIGHT;
	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pDifficultyLabel);
		MEFieldSafeDestroy(&pGroup->pDifficultyField);
	}

	if (pDifficulty->eDifficultyType == EncounterDifficultyType_MapVariable) {
		pGroup->pMapVarLabel = ETRefreshLabel(pGroup->pMapVarLabel, "Map Variable", "The map variable to be used to determine the encounter Difficulty.", X_OFFSET_BASE+20, 0, y, pGroup->pExpander);
		pGroup->pMapVarField = ETRefreshDataField(pGroup->pMapVarField, pOrigDifficulty, pDifficulty, parse_EncounterDifficultyProperties, "MapVariable", &pDoc->eaVarNames, false,
			pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
		y += STANDARD_ROW_HEIGHT;
	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pMapVarLabel);
		MEFieldSafeDestroy(&pGroup->pMapVarField);
	}

	ui_ExpanderSetHeight(pGroup->pExpander, y);
	return iExpanderIdx;
}


static void ETRefreshActorShared(EncounterTemplateEditDoc *pDoc, U32 iExpanderIdx)
{
	EncounterActorSharedProperties *pProps, *pOrigProps = NULL;
	EncounterTemplate* pActualTemplate = pDoc->pTemplate;
	EncounterTemplate* pOrigActualTemplate = pDoc->pOrigTemplate;
	ETActorSharedGroup *pGroup;
	F32 y = 0;

	if (!pDoc->pActorSharedGroup) {
		pDoc->pActorSharedGroup = calloc(1,sizeof(ETActorSharedGroup));
		pDoc->pActorSharedGroup->pExpander = ETCreateExpander(pDoc->pEncounterExpanderGroup, "Actor Defaults", iExpanderIdx);
	}
	pGroup = pDoc->pActorSharedGroup;
		pProps = pDoc->pTemplate->pActorSharedProperties;
		if (pDoc->pOrigTemplate) {
			pOrigProps = pDoc->pOrigTemplate->pActorSharedProperties;
		}

	// Overridable fields

	pGroup->pCritterGroupTypeLabel = ETRefreshLabel(pGroup->pCritterGroupTypeLabel, "Critter Group", "Specifies how the critter group for critters will be chosen.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
	pGroup->pCritterGroupTypeField = ETRefreshEnumField(pGroup->pCritterGroupTypeField, pOrigProps, pProps, parse_EncounterActorSharedProperties, "CritterGroupType", EncounterSharedCritterGroupSourceEnum,
		pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 140, UIUnitFixed, 5, pDoc);
	y += STANDARD_ROW_HEIGHT;

	if (pProps->eCritterGroupType == EncounterSharedCritterGroupSource_Specified) {
		pGroup->pCritterGroupLabel = ETRefreshLabel(pGroup->pCritterGroupLabel, "Group", "If set, the critter group to apply to all actors (unless overriden on the actor).", X_OFFSET_BASE+20, 0, y, pGroup->pExpander);
		pGroup->pCritterGroupField = ETRefreshDictionaryField(pGroup->pCritterGroupField, pOrigProps, pProps, parse_EncounterActorSharedProperties, "CritterGroup", "CritterGroup",
			pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
		y += STANDARD_ROW_HEIGHT;
	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pCritterGroupLabel);
		MEFieldSafeDestroy(&pGroup->pCritterGroupField);
	}

	if (pProps->eCritterGroupType == EncounterSharedCritterGroupSource_MapVariable) {
		pGroup->pCritterGroupMapVarLabel = ETRefreshLabel(pGroup->pCritterGroupMapVarLabel, "Map Variable", "If set, the critter group to apply to all actors (unless overriden on the actor).", X_OFFSET_BASE+20, 0, y, pGroup->pExpander);
		pGroup->pCritterGroupMapVarField = ETRefreshDataField(pGroup->pCritterGroupMapVarField, pOrigProps, pProps, parse_EncounterActorSharedProperties, "CritterGroupMapVar", &pDoc->eaVarNames, false,
			pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
		y += STANDARD_ROW_HEIGHT;
	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pCritterGroupMapVarLabel);
		MEFieldSafeDestroy(&pGroup->pCritterGroupMapVarField);
	}


	pProps = encounterTemplate_GetActorSharedProperties(pDoc->pTemplate);
	if (pDoc->pOrigTemplate) {
		pOrigProps = encounterTemplate_GetActorSharedProperties(pDoc->pOrigTemplate);
	}

	if (!pProps)
		return;

	if (ETRefreshOverrideCheckBox(&pGroup->pOverrideParentCheckbox, &pGroup->pExpander, "Override Parent Faction", 16, y, pDoc->pTemplate->pActorSharedProperties->bOverrideParentValues, pDoc, ETOverrideEncounterActorSharedPropsToggled, pDoc, true))
		y += STANDARD_ROW_HEIGHT;

	pGroup->pFactionTypeLabel = ETRefreshLabel(pGroup->pFactionTypeLabel, "Critter Faction", "Specifies how the critter faction for critters will be chosen.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
	pGroup->pFactionTypeField = ETRefreshEnumField(pGroup->pFactionTypeField, pOrigProps, pProps, parse_EncounterActorSharedProperties, "CritterFactionType", EncounterCritterOverrideTypeEnum,
		pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 140, UIUnitFixed, 5, pDoc);
	y += STANDARD_ROW_HEIGHT;

	if (pProps->eFactionType == EncounterCritterOverrideType_Specified) {
		pGroup->pFactionLabel = ETRefreshLabel(pGroup->pFactionLabel, "Faction", "If set, the critter faction to apply to all actors (unless overriden on the actor).", X_OFFSET_BASE+20, 0, y, pGroup->pExpander);
		pGroup->pFactionField = ETRefreshDictionaryField(pGroup->pFactionField, pOrigProps, pProps, parse_EncounterActorSharedProperties, "CritterFaction", "CritterFaction",
			pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
		y += STANDARD_ROW_HEIGHT;

		pGroup->pGangLabel = ETRefreshLabel(pGroup->pGangLabel, "Gang ID", "If set, the gang ID to apply to all actors (unless overriden on the actor).", X_OFFSET_BASE+20, 0, y, pGroup->pExpander);
		pGroup->pGangField = ETRefreshSimpleField(pGroup->pGangField, pOrigProps, pProps, parse_EncounterActorSharedProperties, "Gang", kMEFieldType_TextEntry,
			pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 60, UIUnitFixed, 5, pDoc);
		y += STANDARD_ROW_HEIGHT;
	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pFactionLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pGangLabel);
		MEFieldSafeDestroy(&pGroup->pFactionField);
		MEFieldSafeDestroy(&pGroup->pGangField);
	}

	pGroup->pOverrideSendDistanceLabel = ETRefreshLabel(pGroup->pOverrideSendDistanceLabel, "Override Send Distance", "Override the default entity send distance for all critters in this encounter.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
	pGroup->pOverrideSendDistanceField = ETRefreshSimpleField(pGroup->pOverrideSendDistanceField, pOrigProps, pProps, parse_EncounterActorSharedProperties, "OverrideSendDistance", kMEFieldType_TextEntry,
		pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 140, UIUnitFixed, 5, pDoc);
	y += STANDARD_ROW_HEIGHT;

	ui_ExpanderSetHeight(pGroup->pExpander, y);
}

static void ETOverrideVarToggled(UICheckButton *pButton, ETOverrideVarGroup *pGroup)
{
	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pGroup->pDoc->emDoc, true) || !pButton || !pGroup || !pGroup->pVarDef) {
		return;
	}

	// Create the new variable using the displayed one as a base
	if(ui_CheckButtonGetState(pButton)) {
		WorldVariableDef *pNewVar;
		EncounterTemplate *pTemplate = pGroup->pDoc->pTemplate;

		pNewVar = StructClone(parse_WorldVariableDef, pGroup->pVarDef);
		if(pNewVar) {
			if(pTemplate && pGroup->iActorIndex > -1 && pGroup->iActorIndex < eaSize(&pTemplate->eaActors)) {
				EncounterActorProperties *pActor = pTemplate->eaActors[pGroup->iActorIndex];
				if(pActor) {
					pGroup->index = eaPush(&pActor->eaVariableDefs, pNewVar);
				}
			} else if(pTemplate && pTemplate->pSharedAIProperties) {
				pGroup->index = eaPush(&pTemplate->pSharedAIProperties->eaVariableDefs, pNewVar);
			}
		}
	} else if(pGroup->index >= 0) {
		// Delete the variable
		EncounterTemplate *pTemplate = pGroup->pDoc->pTemplate;

		if(pTemplate && pGroup->iActorIndex > -1 && pGroup->iActorIndex < eaSize(&pTemplate->eaActors)) {
			EncounterActorProperties *pActor = pTemplate->eaActors[pGroup->iActorIndex];
			if(pActor) {
				if(pGroup->index < eaSize(&pActor->eaVariableDefs)) {
					StructDestroySafe(parse_WorldVariableDef, &pActor->eaVariableDefs[pGroup->index]);
					eaRemove(&pActor->eaVariableDefs, pGroup->index);
				}
			}
		} else if(pTemplate && pTemplate->pSharedAIProperties) {
			if(pGroup->index < eaSize(&pTemplate->pSharedAIProperties->eaVariableDefs)) {
				StructDestroySafe(parse_WorldVariableDef, &pTemplate->pSharedAIProperties->eaVariableDefs[pGroup->index]);
				eaRemove(&pTemplate->pSharedAIProperties->eaVariableDefs, pGroup->index);
			}
		}

		pGroup->index = -1;
	}

	// Notify of change
	ETFieldChangedCB(NULL, true, pGroup->pDoc);
}

// Displays a world variable def with a check box to override.
static F32 ETRefreshOverrideVar(ETOverrideVarGroup *pGroup, WorldVariableDef* pOrigVar, WorldVariableDef*** peaFoundVars, const char* pchInheritedText, F32 y)
{
	bool bFound = false;
	int i;
	WorldVariableDef *pFoundVar = NULL;
	WorldVariableDef *pVar = pGroup ? pGroup->pVarDef : NULL;
	WorldVariableDef ***peaSpecifiedVarDefs = NULL;
	EncounterTemplate *pTemplate = pGroup && pGroup->pDoc ? pGroup->pDoc->pTemplate : NULL;
	EncounterActorProperties **eaActors = NULL;
	EncounterAIProperties *pAIProperties = encounterTemplate_GetAIProperties(pTemplate);
	char** srcMapVariables = zmapInfoGetVariableNames(NULL);
	char** partNames;
	char* estrName = NULL;
	if(!pVar || !pGroup)
	{
		return y;
	}

	encounterTemplate_FillActorEarray(pTemplate, &eaActors);

	// Get the specified variables from the actor/template
	if(pTemplate && pGroup->iActorIndex > -1 && pGroup->iActorIndex < eaSize(&eaActors)) {
		EncounterActorProperties *pActor = eaActors[pGroup->iActorIndex];
		if(pActor) {
			peaSpecifiedVarDefs = &pActor->eaVariableDefs;
		}
	} else if(pAIProperties) {
		peaSpecifiedVarDefs = &pAIProperties->eaVariableDefs;
	}

	eaDestroy(&eaActors);

	// Check vars which have already been overridden
	if(peaSpecifiedVarDefs) {
		for(i = 0; i < eaSize(peaSpecifiedVarDefs) && !bFound; i++) {
			if(stricmp((*peaSpecifiedVarDefs)[i]->pcName, pVar->pcName) == 0) {
				bFound = true;
				pFoundVar = (*peaSpecifiedVarDefs)[i];
				pGroup->index = i;
			}
		}
	}

	// Keep track of the variables used
	if(bFound)
		eaPush(peaFoundVars, pFoundVar);
	else
		eaPush(peaFoundVars, pGroup->pVarDef);

	// Update list of map variables and choice table names
	GECheckNameList( &pGroup->eaSrcMapVariables, &srcMapVariables );
	if(bFound)
		partNames = choice_ListNames( REF_STRING_FROM_HANDLE( pFoundVar->choice_table ));
	else
		partNames = choice_ListNames( REF_STRING_FROM_HANDLE( pVar->choice_table ));
	GECheckNameList( &pGroup->eaChoiceTableNames, &partNames );

	// Build the name string
	estrCreate(&estrName);
	if(!bFound && pchInheritedText && (pVar->eDefaultType != WVARDEF_SPECIFY_DEFAULT || pVar->pSpecificValue)) {
		estrPrintf(&estrName, "%s  [%s]  %s", pVar->pcName, worldVariableTypeToString(pVar->eType), pchInheritedText);
	} else {
		estrPrintf(&estrName, "%s  [%s]", pVar->pcName, worldVariableTypeToString(pVar->eType));
	}

	// Display the Check Box, name, type, and inheritance
	if(!pGroup->pOverrideButton) {
		pGroup->pOverrideButton = ui_CheckButtonCreate(X_OFFSET_INDENT, y, estrName, bFound);
		ui_CheckButtonSetToggledCallback(pGroup->pOverrideButton, ETOverrideVarToggled, pGroup);
		ui_WidgetSetTooltipString(UI_WIDGET(pGroup->pOverrideButton), "Click here to specify a value for this variable.");
		ui_ExpanderAddChild(pGroup->pExpander, pGroup->pOverrideButton);
	} else {
		ui_CheckButtonSetText(pGroup->pOverrideButton, estrName);
		ui_CheckButtonSetState(pGroup->pOverrideButton, bFound);
		ui_WidgetSetPosition(UI_WIDGET(pGroup->pOverrideButton), X_OFFSET_INDENT, y);
	}
	estrDestroy(&estrName);

	y += STANDARD_ROW_HEIGHT;

 	// InitFrom
	if(bFound || pVar->eDefaultType != WVARDEF_SPECIFY_DEFAULT || pVar->pSpecificValue) {
		pGroup->pInitFromLabel = ETRefreshLabel(pGroup->pInitFromLabel, "Init From:", "How this variable is specified.", X_OFFSET_INDENT+15, 0, y, pGroup->pExpander);

		if(bFound) { 
			ui_WidgetQueueFreeAndNull(&pGroup->pInitFromValueLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pInheritedLabel);
			pGroup->pInitFromField = ETRefreshEnumField(pGroup->pInitFromField, pOrigVar, pFoundVar, parse_WorldVariableDef, "DefaultType", WorldVariableDefaultValueTypeEnum,
														pGroup->pExpander, X_OFFSET_CONTROL, y, 0, X_PERCENT_SPLIT, UIUnitPercentage, 0, pGroup->pDoc);
		} else { 
			MEFieldSafeDestroy(&pGroup->pInitFromField);
			pGroup->pInitFromValueLabel = ETRefreshLabel(pGroup->pInitFromValueLabel, StaticDefineIntRevLookup(WorldVariableDefaultValueTypeEnum, pVar->eDefaultType), NULL,
				X_OFFSET_CONTROL, 0, y, pGroup->pExpander);
		}
		y += STANDARD_ROW_HEIGHT;

	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pInitFromLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pInitFromValueLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pInheritedLabel);
		MEFieldSafeDestroy(&pGroup->pInitFromField);
	}


	// Values: Fields
	if(bFound) {
		// Specify default
		if(pFoundVar->eDefaultType == WVARDEF_SPECIFY_DEFAULT)
		{
			if(!pFoundVar->pSpecificValue) {
				pFoundVar->pSpecificValue = StructCreate(parse_WorldVariable);
				pFoundVar->pSpecificValue->pcName = allocAddString(pFoundVar->pcName);
				pFoundVar->pSpecificValue->eType = pFoundVar->eType;
			}

			if (pFoundVar->eType == WVAR_INT) {
				pGroup->pVarLabel = ETRefreshLabel(pGroup->pVarLabel, "Int Value", "Specified integer value to use for the FSM Variable", X_OFFSET_INDENT + 15, 0, y, pGroup->pExpander);
				pGroup->pIntField = ETRefreshSimpleField(pGroup->pIntField, pOrigVar?pOrigVar->pSpecificValue:NULL, pFoundVar->pSpecificValue, parse_WorldVariable, "IntVal",
					kMEFieldType_TextEntry, pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pGroup->pDoc);
			} else {
				MEFieldSafeDestroy(&pGroup->pIntField);
			}
			
			if (pFoundVar->eType == WVAR_FLOAT) {
				pGroup->pVarLabel = ETRefreshLabel(pGroup->pVarLabel, "Float Value", "Specified floating point value to use for the FSM Variable", X_OFFSET_INDENT + 15, 0, y, pGroup->pExpander);
				pGroup->pFloatField = ETRefreshSimpleField(pGroup->pFloatField, pOrigVar?pOrigVar->pSpecificValue:NULL, pFoundVar->pSpecificValue, parse_WorldVariable, "FloatVal",
					kMEFieldType_TextEntry, pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pGroup->pDoc);
			} else {
				MEFieldSafeDestroy(&pGroup->pFloatField);
			}

			if (pFoundVar->eType == WVAR_STRING) {
				pGroup->pVarLabel = ETRefreshLabel(pGroup->pVarLabel, "String Value", "Specified string value to use for the FSM Variable", X_OFFSET_INDENT + 15, 0, y, pGroup->pExpander);
				pGroup->pStringField = ETRefreshSimpleField(pGroup->pStringField, pOrigVar?pOrigVar->pSpecificValue:NULL, pFoundVar->pSpecificValue, parse_WorldVariable, "StringVal",
					kMEFieldType_TextEntry, pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pGroup->pDoc);
			} else {
				MEFieldSafeDestroy(&pGroup->pStringField);
			}

			if (pFoundVar->eType == WVAR_LOCATION_STRING) {
				pGroup->pVarLabel = ETRefreshLabel(pGroup->pVarLabel, "Loc. String", "Specified location string value to use for the FSM Variable", X_OFFSET_INDENT + 15, 0, y, pGroup->pExpander);
				pGroup->pLocStringField = ETRefreshSimpleField(pGroup->pLocStringField, pOrigVar?pOrigVar->pSpecificValue:NULL, pFoundVar->pSpecificValue, parse_WorldVariable, "StringVal",
					kMEFieldType_TextEntry, pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pGroup->pDoc);
			} else {
				MEFieldSafeDestroy(&pGroup->pLocStringField);
			}

			if (pFoundVar->eType == WVAR_ANIMATION) {
				pGroup->pVarLabel = ETRefreshLabel(pGroup->pVarLabel, "Animation", "Specified animation to use for the FSM Variable", X_OFFSET_INDENT + 15, 0, y, pGroup->pExpander);
				if (!pGroup->pAnimField) {
					pGroup->pAnimField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_TextEntry, pOrigVar?pOrigVar->pSpecificValue:NULL, pFoundVar->pSpecificValue, parse_WorldVariable, "StringVal", "AIAnimList", "ResourceName");
					ETAddFieldToParent(pGroup->pAnimField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1, UIUnitPercentage, 5,  pGroup->pDoc);
				} else {
					ui_WidgetSetPositionEx(pGroup->pAnimField->pUIWidget, X_OFFSET_CONTROL, y, 0, 0, UITopLeft);
					MEFieldSetAndRefreshFromData(pGroup->pAnimField, pOrigVar?pOrigVar->pSpecificValue:NULL, pFoundVar->pSpecificValue);
				}
			} else {
				MEFieldSafeDestroy(&pGroup->pAnimField);
			}

			if (pFoundVar->eType == WVAR_CRITTER_DEF) {
				pGroup->pVarLabel = ETRefreshLabel(pGroup->pVarLabel, "Critter Def", "Specified Critter Def to use for the FSM Variable", X_OFFSET_INDENT + 15, 0, y, pGroup->pExpander);
				pGroup->pCritterDefField = ETRefreshDictionaryField(pGroup->pCritterDefField, pOrigVar?pOrigVar->pSpecificValue:NULL, pFoundVar->pSpecificValue, parse_WorldVariable, "CritterDef",
					"CritterDef", pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pGroup->pDoc);
			} else {
				MEFieldSafeDestroy(&pGroup->pCritterDefField);
			}

			if (pFoundVar->eType == WVAR_CRITTER_GROUP) {
				pGroup->pVarLabel = ETRefreshLabel(pGroup->pVarLabel, "Critter Group", "Specified Critter Group to use for the FSM Variable", X_OFFSET_INDENT + 15, 0, y, pGroup->pExpander);
				pGroup->pCritterGroupField = ETRefreshDictionaryField(pGroup->pCritterGroupField, pOrigVar?pOrigVar->pSpecificValue:NULL, pFoundVar->pSpecificValue, parse_WorldVariable, "CritterGroup",
					"CritterGroup", pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pGroup->pDoc);
			} else {
				MEFieldSafeDestroy(&pGroup->pCritterGroupField);
			}

			if (pFoundVar->eType == WVAR_MESSAGE) {
				pGroup->pVarLabel = ETRefreshLabel(pGroup->pVarLabel, "Message", "Specified Message to use for the FSM Variable", X_OFFSET_INDENT + 15, 0, y, pGroup->pExpander);
				pGroup->pMessageField = ETRefreshSimpleField(pGroup->pMessageField, pOrigVar&&pOrigVar->pSpecificValue?&pOrigVar->pSpecificValue->messageVal:NULL, &pFoundVar->pSpecificValue->messageVal, parse_DisplayMessage, "editorCopy",
					kMEFieldType_Message, pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pGroup->pDoc);
			} else {
				MEFieldSafeDestroy(&pGroup->pMessageField);
			}

			if (pFoundVar->eType == WVAR_MAP_POINT) {
				pGroup->pVarLabel = ETRefreshLabel(pGroup->pVarLabel, "Zone Map", "The target zonemap.  If left empty, the current map.", X_OFFSET_INDENT + 15, 0, y, pGroup->pExpander);
				pGroup->pZoneMapField = ETRefreshDictionaryField(pGroup->pZoneMapField, pOrigVar?pOrigVar->pSpecificValue:NULL, pFoundVar->pSpecificValue, parse_WorldVariable, "ZoneMap",
					"ZoneMap", pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pGroup->pDoc);
				y+=STANDARD_ROW_HEIGHT;
				pGroup->pVarLabel2 = ETRefreshLabel(pGroup->pVarLabel2, "Spawn Point", "The target spawn point on the zonemap.  Can also be MissionReturn to go back to how this map was entered.", X_OFFSET_INDENT + 15, 0, y, pGroup->pExpander);
				pGroup->pSpawnPointField = ETRefreshSimpleField(pGroup->pSpawnPointField, pOrigVar?pOrigVar->pSpecificValue:NULL, pFoundVar->pSpecificValue, parse_WorldVariable, "StringVal",
					kMEFieldType_TextEntry, pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pGroup->pDoc);
			} else {
				ui_WidgetQueueFreeAndNull(&pGroup->pVarLabel2);
				MEFieldSafeDestroy(&pGroup->pZoneMapField);
				MEFieldSafeDestroy(&pGroup->pSpawnPointField);
			}

			if (pFoundVar->eType == WVAR_ITEM_DEF) {
				pGroup->pVarLabel = ETRefreshLabel(pGroup->pVarLabel, "Item Def", "Specified Item Def to use for the FSM Variable", X_OFFSET_INDENT + 15, 0, y, pGroup->pExpander);
				pGroup->pItemDefField = ETRefreshSimpleField(pGroup->pItemDefField, pOrigVar?pOrigVar->pSpecificValue:NULL, pFoundVar->pSpecificValue, parse_WorldVariable, "StringVal",
					kMEFieldType_TextEntry, pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pGroup->pDoc);
			} else {
				MEFieldSafeDestroy(&pGroup->pItemDefField);
			}

			y += STANDARD_ROW_HEIGHT;
		} else {
			MEFieldSafeDestroy(&pGroup->pIntField);
			MEFieldSafeDestroy(&pGroup->pFloatField);
			MEFieldSafeDestroy(&pGroup->pStringField);
			MEFieldSafeDestroy(&pGroup->pLocStringField);
			MEFieldSafeDestroy(&pGroup->pAnimField);
			MEFieldSafeDestroy(&pGroup->pCritterDefField);
			MEFieldSafeDestroy(&pGroup->pCritterGroupField);
			MEFieldSafeDestroy(&pGroup->pMessageField);
			MEFieldSafeDestroy(&pGroup->pZoneMapField);
			MEFieldSafeDestroy(&pGroup->pSpawnPointField);
			MEFieldSafeDestroy(&pGroup->pItemDefField);
		}
		
		// Choice Table
		if(pFoundVar->eDefaultType == WVARDEF_CHOICE_TABLE)
		{
			pGroup->pVarLabel = ETRefreshLabel(pGroup->pVarLabel, "Choice Table", "Value comes from this choice table.", X_OFFSET_INDENT + 15, 0, y, pGroup->pExpander);
			pGroup->pChoiceTableField = ETRefreshDictionaryField(pGroup->pChoiceTableField, pOrigVar, pFoundVar, parse_WorldVariableDef, "ChoiceTable",
				"ChoiceTable", pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pGroup->pDoc);
			y+=STANDARD_ROW_HEIGHT;
			pGroup->pVarLabel2 = ETRefreshLabel(pGroup->pVarLabel2, "Choice Value", "Value comes from this value in the choice table use.  If two places specify the same choice table, the values will come from the same row.", X_OFFSET_INDENT + 15, 0, y, pGroup->pExpander);
			pGroup->pChoiceNameField = ETRefreshDataField(pGroup->pChoiceNameField, pOrigVar, pFoundVar, parse_WorldVariableDef, "ChoiceName",
				&pGroup->eaChoiceTableNames, false, pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pGroup->pDoc);
			y+=STANDARD_ROW_HEIGHT;
		} else {
			MEFieldSafeDestroy(&pGroup->pChoiceTableField);
			MEFieldSafeDestroy(&pGroup->pChoiceNameField);
		}

		// Map Variable
		if(pFoundVar->eDefaultType ==  WVARDEF_MAP_VARIABLE)
		{
			ui_WidgetQueueFreeAndNull(&pGroup->pVarLabel2);

			pGroup->pVarLabel = ETRefreshLabel(pGroup->pVarLabel, "Map Variable", "Value comes from this map variable.", X_OFFSET_INDENT + 15, 0, y, pGroup->pExpander);
			pGroup->pMapVarField = ETRefreshDataField(pGroup->pMapVarField, pOrigVar, pFoundVar, parse_WorldVariableDef, "MapVariable",
				&pGroup->eaSrcMapVariables, false, pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pGroup->pDoc);
			y+=STANDARD_ROW_HEIGHT;
		} else {
			MEFieldSafeDestroy(&pGroup->pMapVarField);
		}
		ui_WidgetQueueFreeAndNull(&pGroup->pVarValueLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pVarValueLabel2);
	} else {
		// Values: Labels
		switch(pVar->eDefaultType) {
			xcase WVARDEF_SPECIFY_DEFAULT:
			if(pVar->pSpecificValue && pVar->eType != WVAR_NONE) {
				char* estrValue = NULL;
				estrCreate(&estrValue);
				switch (pVar->eType) {
					xcase WVAR_INT:
						pGroup->pVarLabel = ETRefreshLabel(pGroup->pVarLabel, "Int Value", "Specified integer value to use for the FSM Variable", X_OFFSET_INDENT + 15, 0, y, pGroup->pExpander);
						estrPrintf(&estrValue, "%d", pVar->pSpecificValue->iIntVal);
					xcase WVAR_FLOAT:
						pGroup->pVarLabel = ETRefreshLabel(pGroup->pVarLabel, "Float Value", "Specified floating point value to use for the FSM Variable", X_OFFSET_INDENT + 15, 0, y, pGroup->pExpander);
						estrPrintf(&estrValue, "%.5f", pVar->pSpecificValue->fFloatVal);
					xcase WVAR_STRING:
						pGroup->pVarLabel = ETRefreshLabel(pGroup->pVarLabel, "String Value", "Specified string value to use for the FSM Variable", X_OFFSET_INDENT + 15, 0, y, pGroup->pExpander);
						estrPrintf(&estrValue, "%s", NULL_TO_EMPTY(pVar->pSpecificValue->pcStringVal));
					xcase WVAR_LOCATION_STRING:
						pGroup->pVarLabel = ETRefreshLabel(pGroup->pVarLabel, "Loc. String", "Specified location string value to use for the FSM Variable", X_OFFSET_INDENT + 15, 0, y, pGroup->pExpander);
						estrPrintf(&estrValue, "%s", NULL_TO_EMPTY(pVar->pSpecificValue->pcStringVal));
					xcase WVAR_ANIMATION:
						pGroup->pVarLabel = ETRefreshLabel(pGroup->pVarLabel, "Animation", "Specified animation to use for the FSM Variable", X_OFFSET_INDENT + 15, 0, y, pGroup->pExpander);
						estrPrintf(&estrValue, "%s", NULL_TO_EMPTY(pVar->pSpecificValue->pcStringVal));
					xcase WVAR_CRITTER_DEF:
						pGroup->pVarLabel = ETRefreshLabel(pGroup->pVarLabel, "Critter Def", "Specified Critter Def to use for the FSM Variable", X_OFFSET_INDENT + 15, 0, y, pGroup->pExpander);
						estrPrintf(&estrValue, "%s", NULL_TO_EMPTY(REF_STRING_FROM_HANDLE(pVar->pSpecificValue->hCritterDef)));
					xcase WVAR_CRITTER_GROUP:
						pGroup->pVarLabel = ETRefreshLabel(pGroup->pVarLabel, "Critter Group", "Specified Critter Group to use for the FSM Variable", X_OFFSET_INDENT + 15, 0, y, pGroup->pExpander);
						estrPrintf(&estrValue, "%s", NULL_TO_EMPTY(REF_STRING_FROM_HANDLE(pVar->pSpecificValue->hCritterGroup)));
					xcase WVAR_MESSAGE:
						pGroup->pVarLabel = ETRefreshLabel(pGroup->pVarLabel, "Message", "Specified Message to use for the FSM Variable", X_OFFSET_INDENT + 15, 0, y, pGroup->pExpander);
						estrPrintf(&estrValue, "%s", NULL_TO_EMPTY(TranslateDisplayMessage(pVar->pSpecificValue->messageVal)));
						if(!EMPTY_TO_NULL(estrValue))
							estrPrintf(&estrValue, "%s", NULL_TO_EMPTY(langTranslateMessage(locGetLanguage(getCurrentLocale()), pVar->pSpecificValue->messageVal.pEditorCopy)));
					xcase WVAR_MAP_POINT:
						pGroup->pVarLabel = ETRefreshLabel(pGroup->pVarLabel, "Zone Map", "The target zonemap.  If left empty, the current map.", X_OFFSET_INDENT + 15, 0, y, pGroup->pExpander);
						pGroup->pVarValueLabel = ETRefreshLabel(pGroup->pVarValueLabel, EMPTY_TO_NULL(pVar->pSpecificValue->pcZoneMap) ? pVar->pSpecificValue->pcZoneMap : "NOT SET", NULL, X_OFFSET_INDENT + 15, 0, y, pGroup->pExpander);
						y+=STANDARD_ROW_HEIGHT;
						pGroup->pVarLabel2 = ETRefreshLabel(pGroup->pVarLabel2, "Spawn Point", "The target spawn point on the zonemap.  Can also be MissionReturn to go back to how this map was entered.", X_OFFSET_INDENT + 15, 0, y, pGroup->pExpander);
						pGroup->pVarValueLabel2 = ETRefreshLabel(pGroup->pVarValueLabel2, EMPTY_TO_NULL(pVar->pSpecificValue->pcStringVal) ? pVar->pSpecificValue->pcStringVal : "NOT SET", NULL, X_OFFSET_INDENT + 15, 0, y, pGroup->pExpander);
					xcase WVAR_ITEM_DEF:
						pGroup->pVarLabel = ETRefreshLabel(pGroup->pVarLabel, "Item Def", "Specified Item Def to use for the FSM Variable", X_OFFSET_INDENT + 15, 0, y, pGroup->pExpander);
						estrPrintf(&estrValue, "%s", NULL_TO_EMPTY(pVar->pSpecificValue->pcStringVal));
				}

				if(pVar->eType != WVAR_MAP_POINT) {
					ui_WidgetQueueFreeAndNull(&pGroup->pVarLabel2);
					ui_WidgetQueueFreeAndNull(&pGroup->pVarValueLabel2);
					pGroup->pVarValueLabel = ETRefreshLabel(pGroup->pVarValueLabel, EMPTY_TO_NULL(estrValue) ? estrValue : "NOT SET", NULL, X_OFFSET_CONTROL, 0, y, pGroup->pExpander);
				}
				y+=STANDARD_ROW_HEIGHT;
			} else {
				ui_WidgetQueueFreeAndNull(&pGroup->pVarLabel);
				ui_WidgetQueueFreeAndNull(&pGroup->pVarValueLabel);
				ui_WidgetQueueFreeAndNull(&pGroup->pVarLabel2);
				ui_WidgetQueueFreeAndNull(&pGroup->pVarValueLabel2);
			}
			xcase WVARDEF_CHOICE_TABLE:
			{
				pGroup->pVarLabel = ETRefreshLabel(pGroup->pVarLabel, "Choice Table", "Value comes from this choice table.", X_OFFSET_INDENT + 15, 0, y, pGroup->pExpander);
				pGroup->pVarValueLabel = ETRefreshLabel(pGroup->pVarValueLabel, EMPTY_TO_NULL(REF_STRING_FROM_HANDLE(pVar->choice_table)) ? REF_STRING_FROM_HANDLE(pVar->choice_table) : "NOT SET", NULL, X_OFFSET_CONTROL, 0, y, pGroup->pExpander);
				y+=STANDARD_ROW_HEIGHT;
				pGroup->pVarLabel2 = ETRefreshLabel(pGroup->pVarLabel2, "Choice Value", "Value comes from this value in the choice table use.  If two places specify the same choice table, the values will come from the same row.", X_OFFSET_INDENT + 15, 0, y, pGroup->pExpander);
				pGroup->pVarValueLabel2 = ETRefreshLabel(pGroup->pVarValueLabel2, EMPTY_TO_NULL(pVar->choice_name) ? pVar->choice_name : "NOT SET", NULL, X_OFFSET_CONTROL, 0, y, pGroup->pExpander);
				y+=STANDARD_ROW_HEIGHT;
			}
			xcase WVARDEF_MAP_VARIABLE:
			{
				ui_WidgetQueueFreeAndNull(&pGroup->pVarLabel2);
				ui_WidgetQueueFreeAndNull(&pGroup->pVarValueLabel2);

				pGroup->pVarLabel = ETRefreshLabel(pGroup->pVarLabel, "Map Variable", "Value comes from this map variable.", X_OFFSET_INDENT + 15, 0, y, pGroup->pExpander);
				pGroup->pVarValueLabel = ETRefreshLabel(pGroup->pVarValueLabel, EMPTY_TO_NULL(pVar->map_variable) ? pVar->map_variable : "NOT SET", NULL, X_OFFSET_CONTROL, 0, y, pGroup->pExpander);
				y+=STANDARD_ROW_HEIGHT;
				break;
			}
		}
		MEFieldSafeDestroy(&pGroup->pIntField);
		MEFieldSafeDestroy(&pGroup->pFloatField);
		MEFieldSafeDestroy(&pGroup->pStringField);
		MEFieldSafeDestroy(&pGroup->pLocStringField);
		MEFieldSafeDestroy(&pGroup->pAnimField);
		MEFieldSafeDestroy(&pGroup->pCritterDefField);
		MEFieldSafeDestroy(&pGroup->pCritterGroupField);
		MEFieldSafeDestroy(&pGroup->pMessageField);
		MEFieldSafeDestroy(&pGroup->pZoneMapField);
		MEFieldSafeDestroy(&pGroup->pSpawnPointField);
		MEFieldSafeDestroy(&pGroup->pItemDefField);
		MEFieldSafeDestroy(&pGroup->pChoiceTableField);
		MEFieldSafeDestroy(&pGroup->pChoiceNameField);
		MEFieldSafeDestroy(&pGroup->pMapVarField);
	}

	return y;
}

// Gets all the variables from an FSM, converts them to world variable defs, and sets values based on inherited
// variables.
static void ETBuildVarList(FSM *pFSM, WorldVariableDef **eaInheritedVarDefs, WorldVariableDef ***peaVarList)
{
	FSMExternVar **eaFSMVarDefs = NULL;
	int i, j;

	// Collect the FSM var defs
	if (pFSM) {
		fsmGetExternVarNamesRecursive( pFSM, &eaFSMVarDefs, "Encounter" );
	}
	for(i=0; i<eaSize(&eaFSMVarDefs); ++i) {
		FSMExternVar *pVar = eaFSMVarDefs[i];
		WorldVariableDef *pDef;
		WorldVariableDef *pInheritedDef = NULL;

		// See if this variable has been inherited
		if(eaInheritedVarDefs) {
			for(j=eaSize(&eaInheritedVarDefs)-1; j>=0 && !pInheritedDef; j--) {
				if(stricmp(pVar->name, eaInheritedVarDefs[j]->pcName) == 0) {
					pInheritedDef = eaInheritedVarDefs[j];
				}
			}
		}

		// Setup the new WorldVariableDef
		if (i < eaSize(peaVarList)) {
			pDef = (*peaVarList)[i];
		} else {
			pDef = StructCreate(parse_WorldVariableDef);
			eaPush(peaVarList, pDef);
		}

		if(pInheritedDef) {
			StructCopyAll(parse_WorldVariableDef, pInheritedDef, pDef);
		} else {
			StructReset(parse_WorldVariableDef, pDef);
			pDef->pcName = allocAddString(pVar->name);
			pDef->eType = worldVariableTypeFromFSMExternVar(pVar);
		}
	}
	// Cleanup extra variables on peaVarList
	if (peaVarList) {
		for(i=eaSize(peaVarList)-1; i>= eaSize(&eaFSMVarDefs); --i) {
			if (*peaVarList) {
				StructDestroy(parse_WorldVariableDef, (*peaVarList)[i]);
			}
			eaRemove(peaVarList, i);
		}
		eaDestroy(&eaFSMVarDefs);
		eaQSort((*peaVarList), worldVariableDefNameCmp);
	}
}

// Displays a set of FSM vars which use a check box to allow overriding
static F32 ETRefreshOverrideVars(ETOverrideVarGroup*** peaOverrideVarGroups, EncounterTemplateEditDoc *pDoc, UIExpander *pExpander, WorldVariable** eaInheritedVars, WorldVariable** eaOrigInheritedVars, WorldVariableDef ***peaBaseVarDefs, WorldVariableDef ***peaOrigBaseVarDefs, WorldVariableDef ***peaSpecifiedVars, WorldVariableDef ***peaFoundVars, const char* pchInheritedDefText, const char* pchInheritedVarText, int iActorIndex, F32 y)
{
	int i, j;
	for(i=0; i<eaSize(peaBaseVarDefs); i++)
	{
		ETOverrideVarGroup *pOverrideGroup = NULL;
		WorldVariableDef *pOrigVar = NULL;
		const char* pchInheritedText = pchInheritedDefText;
		bool bFound = false;

		// Setup the override group
		if(i < eaSize(peaOverrideVarGroups)) {
			pOverrideGroup = (*peaOverrideVarGroups)[i];
		} else {
			pOverrideGroup = calloc(1, sizeof(ETOverrideVarGroup));
			pOverrideGroup->pDoc = pDoc;
			pOverrideGroup->pExpander = pExpander;
			pOverrideGroup->index = -1;
			eaPush(peaOverrideVarGroups, pOverrideGroup);
		}
		pOverrideGroup->pVarDef = (*peaBaseVarDefs)[i];

		// Set the OrigVar
		if(pOverrideGroup->index > -1) {
			if(peaOrigBaseVarDefs && pOverrideGroup->index < eaSize(peaOrigBaseVarDefs))
				pOrigVar = (*peaOrigBaseVarDefs)[pOverrideGroup->index];
		} else if(peaOrigBaseVarDefs && i < eaSize(peaOrigBaseVarDefs)){
			pOrigVar = (*peaOrigBaseVarDefs)[i];
		}
		pOverrideGroup->pVarDef = (*peaBaseVarDefs)[i];
		pOverrideGroup->iActorIndex = iActorIndex;

		// If inherited from a passed in worldVariable set the specified value and 
		// set the inherited text to pchInheritedVarText (instead of pchInheritedVarDefText)
		if(eaInheritedVars && pOverrideGroup->pVarDef->eDefaultType == WVARDEF_SPECIFY_DEFAULT && !pOverrideGroup->pVarDef->pSpecificValue) {
			for(j=eaSize(&eaInheritedVars)-1; j>=0 && !bFound; j--) {
				if(stricmp(pOverrideGroup->pVarDef->pcName, eaInheritedVars[j]->pcName) == 0 ) {
					bFound = true;
					pOverrideGroup->pVarDef->pSpecificValue = StructClone(parse_WorldVariable, eaInheritedVars[j]);
					pchInheritedText = pchInheritedVarText;
				}
			}
		}
		if(eaOrigInheritedVars && pOrigVar && pOrigVar->eDefaultType == WVARDEF_SPECIFY_DEFAULT && !pOrigVar->pSpecificValue) {
			bFound = false;
			for(j=eaSize(&eaOrigInheritedVars)-1; j>=0 && !bFound; j--) {
				if(stricmp(pOrigVar->pcName, eaOrigInheritedVars[j]->pcName) == 0 ) {
					bFound = true;
					pOrigVar->pSpecificValue = StructClone(parse_WorldVariable, eaOrigInheritedVars[j]);
				}
			}
		}
		// Display the variable
		y = ETRefreshOverrideVar(pOverrideGroup, pOrigVar, peaFoundVars, pchInheritedText, y);
	}
	// Cleanup extra groups
	if (peaOverrideVarGroups && (*peaOverrideVarGroups)) {
		for(i=eaSize(peaOverrideVarGroups)-1; i>= eaSize(peaBaseVarDefs); --i) {
			ETFreeOverrideVarGroup((*peaOverrideVarGroups)[i]);
			eaRemove(peaOverrideVarGroups, i);
		}
	}
	return y;
}

// Displays collection of FSM Vars stored on either an actor or on an encounter template
static F32 ETRefreshFSMVarGroup(ETFSMVarGroup *pGroup, int iActorIndex, F32 y)
{
	FSMExternVar **eaFSMVarDefs = NULL;
	WorldVariableDef **eaFoundVars = NULL;
	WorldVariableDef** eaInheritedVarDefs = NULL;
	WorldVariableDef** eaOrigInheritedVarDefs = NULL;
	WorldVariableDef*** peaAssembledFSMVarDefs = NULL;//this list can't go out of scope or things break
	WorldVariableDef*** peaModifiableFSMVarDefs = NULL;//the list of variables that can be safely modified without violating inheritance
	WorldVariableDef** eaOrigAssembledFSMVarDefs = NULL;
	EncounterTemplateEditDoc *pDoc = pGroup ? pGroup->pDoc : NULL;
	EncounterTemplate *pTemplate = pDoc ? pDoc->pTemplate : NULL;
	EncounterTemplate *pOrigTemplate = pDoc ? pDoc->pOrigTemplate : NULL;
	EncounterActorProperties *pActor = NULL;
	ETActorGroup* pActorGroup = NULL;
	EncounterActorProperties *pOrigActor = NULL;
	EncounterActorProperties **eaActors = NULL;
	EncounterActorProperties **eaOrigActors = NULL;
	EncounterAIProperties *pAIProperties = encounterTemplate_GetAIProperties(pTemplate);
	EncounterAIProperties *pOrigAIProperties = encounterTemplate_GetAIProperties(pOrigTemplate);
	FSM *pFSM = NULL;
	FSM *pOrigFSM = NULL;
	int iPartitionIdx = PARTITION_CLIENT;

	int i, varCount;

	if(!pTemplate || !pGroup) {
		return y;
	}
	
	pGroup->iActorIndex = iActorIndex;

	encounterTemplate_FillActorEarray(pTemplate, &eaActors);
	encounterTemplate_FillActorEarray(pOrigTemplate, &eaOrigActors);

	// Get FSM/OrigFSM
	if(eaActors && iActorIndex >= 0 && iActorIndex < eaSize(&eaActors)) {
		pActor = eaActors[iActorIndex];
		pActorGroup = pDoc->eaActorGroups[iActorIndex];
		pFSM = encounterTemplate_GetActorFSM(pTemplate, pActor, iPartitionIdx);
	} else if(pTemplate) {
		pFSM = encounterTemplate_GetEncounterFSM(pTemplate);
	}

	eaDestroy(&eaActors);

	if(eaOrigActors && iActorIndex >= 0 && iActorIndex < eaSize(&eaOrigActors)) {
		pOrigActor = eaOrigActors[iActorIndex];
		pOrigFSM = encounterTemplate_GetActorFSM(pOrigTemplate, pOrigActor, iPartitionIdx);
	} else if(pOrigTemplate) {
		pOrigFSM = encounterTemplate_GetEncounterFSM(pOrigTemplate);
	}

	eaDestroy(&eaOrigActors);

	if (pActor)
	{
		peaAssembledFSMVarDefs = &pActorGroup->eaVarDefs;
		peaModifiableFSMVarDefs = &pActor->eaVariableDefs;
		eaClear(peaAssembledFSMVarDefs);
		encounterTemplate_GetActorFSMVarDefs(pTemplate, pActor, peaAssembledFSMVarDefs, NULL);
		encounterTemplate_GetActorFSMVarDefs(pOrigTemplate, pActor, &eaOrigAssembledFSMVarDefs, NULL);
	}
	else
	{
		peaAssembledFSMVarDefs = &pDoc->eaVarDefs;
		peaModifiableFSMVarDefs = &pTemplate->pSharedAIProperties->eaVariableDefs;
		eaClear(peaAssembledFSMVarDefs);
		encounterTemplate_GetEncounterFSMVarDefs(pTemplate, peaAssembledFSMVarDefs, NULL);
		encounterTemplate_GetEncounterFSMVarDefs(pOrigTemplate, &eaOrigAssembledFSMVarDefs, NULL);
	}
	// Build FSM Var List
	if(pActor && pAIProperties) {
		eaCopy(&eaInheritedVarDefs, &pAIProperties->eaVariableDefs);
	} 
	ETBuildVarList(pFSM, eaInheritedVarDefs, &pGroup->eaCachedFSMVars);

	if(pOrigActor && pOrigAIProperties) {
		eaCopy(&eaOrigInheritedVarDefs, &pOrigAIProperties->eaVariableDefs);
	}
	ETBuildVarList(pOrigFSM, eaOrigInheritedVarDefs, &pGroup->eaCachedOrigFSMVars);

	// Add Label
	if(pGroup->eaCachedFSMVars && eaSize(&pGroup->eaCachedFSMVars)) {
		pGroup->pFSMLabel = ETRefreshLabel(pGroup->pFSMLabel, "FSM Variables", "Variables which apply to the current FSM", X_OFFSET_BASE, 0, y, pGroup->pExpander);
		y+=STANDARD_ROW_HEIGHT;
	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pFSMLabel);
	}

	if(pActor) {
		WorldVariable** eaOrigCritterVars = NULL;
		WorldVariable** eaCritterVars = NULL;
		// Lookup inherited critter vars
 		encounterTemplate_GetActorCritterFSMVars(pTemplate, pActor, iPartitionIdx, &eaInheritedVarDefs, &eaCritterVars);
 		if(pOrigActor)
 			encounterTemplate_GetActorCritterFSMVars(pTemplate, pOrigActor, iPartitionIdx, &eaOrigInheritedVarDefs, &eaOrigCritterVars);
		// Refresh FSM vars pulling values from inherited template and critter vars
		y = ETRefreshOverrideVars(&pGroup->eaFSMOverrideVarGroups, pGroup->pDoc, pGroup->pExpander, eaCritterVars, eaOrigCritterVars, &pGroup->eaCachedFSMVars, &eaOrigAssembledFSMVarDefs, peaAssembledFSMVarDefs, &eaFoundVars, "(* From Template *)", "(* From Critter *)", iActorIndex, y);
		eaDestroy(&eaCritterVars);
		eaDestroy(&eaOrigCritterVars);
	} else {
		// Refresh template level fsm vars
		y = ETRefreshOverrideVars(&pGroup->eaFSMOverrideVarGroups, pGroup->pDoc, pGroup->pExpander, NULL, NULL, &pGroup->eaCachedFSMVars, &eaOrigAssembledFSMVarDefs, peaAssembledFSMVarDefs, &eaFoundVars, "(* Inherited *)", NULL, -1, y);
	}

	// Build additional template var list
	if(pActor && pTemplate && pAIProperties) {
		// Gather template vars which haven't been used yet
		eaClear(&eaInheritedVarDefs);
		eaCopy(&eaInheritedVarDefs, &eaFoundVars);
		encounterTemplate_GetEncounterFSMVarDefs(pTemplate, &eaInheritedVarDefs, NULL);
		eaRemoveRange(&eaInheritedVarDefs, 0, eaSize(&eaFoundVars));
		if(pOrigTemplate) {
			eaClear(&eaOrigInheritedVarDefs);
			eaCopy(&eaOrigInheritedVarDefs, &eaFoundVars);
			encounterTemplate_GetEncounterFSMVarDefs(pTemplate, &eaOrigInheritedVarDefs, NULL);
			eaRemoveRange(&eaOrigInheritedVarDefs, 0, eaSize(&eaFoundVars));
		}
		// If any vars exist, display them
		if(eaInheritedVarDefs && eaSize(&eaInheritedVarDefs)) {
			// Label
			pGroup->pTemplateLabel = ETRefreshLabel(pGroup->pTemplateLabel, "Additional Template Vars", "Variables defined by the template which do not apply to the current FSM", X_OFFSET_BASE, 0, y, pGroup->pExpander);
			y+=STANDARD_ROW_HEIGHT;

			// Sort
			eaQSort(eaInheritedVarDefs, worldVariableDefNameCmp);
			if(eaOrigInheritedVarDefs)
				eaQSort(eaOrigInheritedVarDefs, worldVariableDefNameCmp);
	
			// Display
			y = ETRefreshOverrideVars(&pGroup->eaTemplateOverrideVarGroups, pDoc, pGroup->pExpander, NULL, NULL, &eaInheritedVarDefs, &eaOrigAssembledFSMVarDefs, peaAssembledFSMVarDefs, &eaFoundVars, "(* From Template *)", NULL, iActorIndex, y );
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pTemplateLabel);
		}
	} else {
		if(pGroup->eaTemplateOverrideVarGroups){
			eaDestroyEx(&pGroup->eaTemplateOverrideVarGroups, ETFreeOverrideVarGroup);
		}
		ui_WidgetQueueFreeAndNull(&pGroup->pTemplateLabel);
	}

	// Build Critter Var List
	if(pActor) {
		WorldVariable** eaCritterVars = NULL;
		WorldVariable** eaOrigCritterVars = NULL;

		// Gather critter vars which haven't been used yet
		encounterTemplate_GetActorCritterFSMVars(pTemplate, pActor, iPartitionIdx, &eaFoundVars, &eaCritterVars);
		if(pOrigActor) {
			encounterTemplate_GetActorCritterFSMVars(pTemplate, pOrigActor, iPartitionIdx, &eaFoundVars, &eaOrigCritterVars);
		}
		// If any vars exist, convert them to var defs
		if(eaCritterVars && eaSize(&eaCritterVars)) {
			// Label
			pGroup->pCritterLabel = ETRefreshLabel(pGroup->pCritterLabel, "Additional Critter Vars", "Variables defined by the Critter Def which do not apply to the current FSM", X_OFFSET_BASE, 0, y, pGroup->pExpander);
			y+=STANDARD_ROW_HEIGHT;

			// Sort
			eaQSort(eaCritterVars, worldVariableNameCmp);

			// Convert to var def
			for(i=0; i<eaSize(&eaCritterVars); i++) {
				WorldVariableDef *pDef = NULL;
				if (i < eaSize(&pGroup->eaCachedCritterVars)) {
					pDef = pGroup->eaCachedCritterVars[i];
				} else {
					pDef = StructCreate(parse_WorldVariableDef);
					eaPush(&pGroup->eaCachedCritterVars, pDef);
				}
				pDef->pcName = allocAddString(eaCritterVars[i]->pcName);
				pDef->eType = eaCritterVars[i]->eType;
				pDef->eDefaultType = WVARDEF_SPECIFY_DEFAULT;
				pDef->pSpecificValue = StructClone(parse_WorldVariable, eaCritterVars[i]);
			}
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pCritterLabel);
		}
		if (pGroup->eaCachedCritterVars) {
			for(i=eaSize(&pGroup->eaCachedCritterVars)-1; i>= eaSize(&eaCritterVars); --i) {
				eaRemove(&pGroup->eaCachedCritterVars, i);
			}
		}

		// Convert OrigCritterVars to var defs
		if(eaOrigCritterVars) {
			eaQSort(eaOrigCritterVars, worldVariableNameCmp);
			for(i=0; i<eaSize(&eaOrigCritterVars); i++) {
				WorldVariableDef *pDef = NULL;
				if (i < eaSize(&pGroup->eaCachedOrigCritterVars)) {
					pDef = pGroup->eaCachedOrigCritterVars[i];
				} else {
					pDef = StructCreate(parse_WorldVariableDef);
					eaPush(&pGroup->eaCachedOrigCritterVars, pDef);
				}
				pDef->pcName = allocAddString(eaOrigCritterVars[i]->pcName);
				pDef->eType = eaOrigCritterVars[i]->eType;
				pDef->eDefaultType = WVARDEF_SPECIFY_DEFAULT;
				pDef->pSpecificValue = StructClone(parse_WorldVariable, eaOrigCritterVars[i]);
			}
		}
		if (pGroup->eaCachedOrigCritterVars) {
			for(i=eaSize(&pGroup->eaCachedOrigCritterVars)-1; i>= eaSize(&eaOrigCritterVars); --i) {
				eaRemove(&pGroup->eaCachedOrigCritterVars, i);
			}
		}
		// Display the critter vars
		y = ETRefreshOverrideVars(&pGroup->eaCritterOverrideVarGroups, pDoc, pGroup->pExpander, NULL, NULL, &pGroup->eaCachedCritterVars, &eaOrigAssembledFSMVarDefs, peaAssembledFSMVarDefs, &eaFoundVars, "(* From Critter *)", NULL, iActorIndex, y);
		eaDestroy(&eaCritterVars);
		eaDestroy(&eaOrigCritterVars);
	} else {
		eaDestroyStruct(&pGroup->eaCachedCritterVars, parse_WorldVariableDef);
		eaDestroyStruct(&pGroup->eaCachedOrigCritterVars, parse_WorldVariableDef);
		eaDestroyEx(&pGroup->eaCritterOverrideVarGroups, ETFreeOverrideVarGroup);
		ui_WidgetQueueFreeAndNull(&pGroup->pCritterLabel);
	}
	
	// Build additional specified var list
	// Gather vars which haven't been used yet
	eaClear(&eaInheritedVarDefs);
	eaCopy(&eaInheritedVarDefs, &eaFoundVars);
	if(pActor) {
		//encounterTemplate_GetActorFSMVarDefs(pTemplate, pActor, &eaInheritedVarDefs, NULL);
		encounterTemplate_GetActorFSMVarDefs(pTemplate, pActor, &eaInheritedVarDefs, NULL);
	} else {
		//we need to gather var defs from all inheritance levels, not just the root.
		encounterTemplate_GetEncounterFSMVarDefs(pTemplate, &eaInheritedVarDefs, NULL);
		/*
		if (pAIProperties) 
		{
			for(i=0; i<eaSize(&pAIProperties->eaVariableDefs); ++i) 
			{
				encounterTemplate_AddVarDefIfNotPresent(&eaInheritedVarDefs, NULL, pAIProperties->eaVariableDefs[i]);
			}
		}
		*/
	}
	eaRemoveRange(&eaInheritedVarDefs, 0, eaSize(&eaFoundVars));

	if(pOrigTemplate) {
		eaClear(&eaOrigInheritedVarDefs);
		eaCopy(&eaOrigInheritedVarDefs, &eaFoundVars);
		if(pActor) {
			if(pOrigActor) {
				//encounterTemplate_GetActorFSMVarDefs(pOrigTemplate, pOrigActor, &eaOrigInheritedVarDefs, NULL);
				encounterTemplate_GetActorFSMVarDefs(pOrigTemplate, pOrigActor, &eaOrigInheritedVarDefs, NULL);
			}
		} else {
			encounterTemplate_GetEncounterFSMVarDefs(pOrigTemplate, &eaOrigInheritedVarDefs, NULL);
			/*
			if (pOrigAIProperties) {
				for(i=0; i<eaSize(&pOrigAIProperties->eaVariableDefs); ++i) {
					encounterTemplate_AddVarDefIfNotPresent(&eaOrigInheritedVarDefs, NULL, pOrigAIProperties->eaVariableDefs[i]);
				}
			}
			*/
		}
		eaRemoveRange(&eaOrigInheritedVarDefs, 0, eaSize(&eaFoundVars));
	}

	// Display the vars as GEVariableDef groups
	varCount = 0;
	if(eaInheritedVarDefs && eaSize(&eaInheritedVarDefs)) {
		pGroup->pExtraLabel = ETRefreshLabel(pGroup->pExtraLabel, "Additional Vars", "Additional specified variables which do not apply to the current FSM", X_OFFSET_BASE, 0, y, pGroup->pExpander);
		y+=STANDARD_ROW_HEIGHT;

		for(i=0; i<eaSize(&eaInheritedVarDefs); ++i) {
			GEVariableDefGroup *pVarDefGroup;
			WorldVariableDef *pVarDef, *pOrigVarDef;
			int index;

			if (i<eaSize(&pGroup->eaExtraVarDefGroups)) {
				pVarDefGroup = pGroup->eaExtraVarDefGroups[i];
			} else {
				pVarDefGroup = calloc(1,sizeof(GEVariableDefGroup));
				pVarDefGroup->pData = pGroup;
				eaPush(&pGroup->eaExtraVarDefGroups, pVarDefGroup);
			}

			pVarDef = eaInheritedVarDefs[i];
			if (eaOrigInheritedVarDefs && (i<eaSize(&eaOrigInheritedVarDefs))) {
				pOrigVarDef = eaOrigInheritedVarDefs[i];
			} else {
				pOrigVarDef = NULL;
			}

			index = eaFind(peaAssembledFSMVarDefs, eaInheritedVarDefs[i]);
			if(index < 0)
				continue;
			pVarDefGroup->index = index;

			y = GEUpdateVariableDefGroupFromNames(pVarDefGroup, UI_WIDGET(pGroup->pExpander), NULL, peaAssembledFSMVarDefs, peaModifiableFSMVarDefs, pVarDef, pOrigVarDef, zmapInfoGetPublicName(NULL), index, X_OFFSET_INDENT, X_OFFSET_CONTROL, y, ETFieldChangedCB, ETFieldPreChangeCB, pDoc);
			varCount++;
		}
	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pExtraLabel);
	}
	// Free unused variable groups
	if (pGroup->eaExtraVarDefGroups) {
		for(i=eaSize(&pGroup->eaExtraVarDefGroups)-1; i>=varCount; --i) {
			GEFreeVariableDefGroup(pGroup->eaExtraVarDefGroups[i]);
			eaRemove(&pGroup->eaExtraVarDefGroups, i);
		}
	}

	// Add variable button
	if (!pGroup->pAddVarButton) {
		pGroup->pAddVarButton = ui_ButtonCreate("Add Variable", X_OFFSET_BASE, y, ETAddFSMVarDef, pGroup);
		ui_WidgetSetWidth(UI_WIDGET(pGroup->pAddVarButton), 120);
		ui_ExpanderAddChild(pGroup->pExpander, pGroup->pAddVarButton);
	} 
	ui_WidgetSetPosition(UI_WIDGET(pGroup->pAddVarButton), X_OFFSET_BASE, y);
	y += STANDARD_ROW_HEIGHT;


	// Free vars
	if(eaFSMVarDefs)
		eaDestroy(&eaFSMVarDefs);
 	if(eaFoundVars)
 		eaDestroy(&eaFoundVars);
	if(eaOrigAssembledFSMVarDefs)
		eaDestroy(&eaOrigAssembledFSMVarDefs);
	if(eaInheritedVarDefs)
		eaDestroy(&eaInheritedVarDefs);
	if(eaOrigInheritedVarDefs)
		eaDestroy(&eaOrigInheritedVarDefs);

	return y;
}

// Create an FSMVarGroup
ETFSMVarGroup *ETInitFSMVarGroup(EncounterTemplateEditDoc *pDoc, UIExpander* pExpander, bool bActor) 
{
	ETFSMVarGroup *pGroup = calloc(1, sizeof(ETFSMVarGroup));
	pGroup->pDoc = pDoc;
	pGroup->pExpander = pExpander;
	return pGroup;
}

static void ETRefreshAI(EncounterTemplateEditDoc *pDoc, U32 iExpanderIdx)
{
	EncounterAIProperties *pProps, *pOrigProps = NULL;
	ETAIGroup *pGroup;
	F32 y = 0;
	FSM *pFSM = NULL;
	FSM *pOrigFSM = NULL;

	if (!pDoc->pAIGroup) {
		pDoc->pAIGroup = calloc(1,sizeof(ETAIGroup));
		pDoc->pAIGroup->pExpander = ETCreateExpander(pDoc->pEncounterExpanderGroup, "Actor AI", iExpanderIdx);
	}
	pGroup = pDoc->pAIGroup;

	if (ETRefreshOverrideCheckBox(&pGroup->pOverrideParentCheckbox, &pGroup->pExpander, "Override Parent", 16, y, pDoc->pTemplate->pSharedAIProperties->bOverrideParentValues, pDoc, ETOverrideEncounterActorAIPropsToggled, pDoc, true))
		y += STANDARD_ROW_HEIGHT;

	pGroup->pDoc = pDoc;
	pProps = encounterTemplate_GetAIProperties(pDoc->pTemplate);
	if (pDoc->pOrigTemplate) {
		pOrigProps = encounterTemplate_GetAIProperties(pDoc->pOrigTemplate);
	}

	if(!pProps)
	{
		pProps = pDoc->pTemplate->pSharedAIProperties;
		if (!pProps)
			return;
	}

	pGroup->pCombatRoleLabel = ETRefreshLabel(pGroup->pCombatRoleLabel, "Combat Role Def", "If set, the combat role definition that will be used for this encounter.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
	pGroup->pCombatRoleField = ETRefreshDictionaryField(pGroup->pCombatRoleField, pOrigProps, pProps,  
						parse_EncounterAIProperties, "CombatRoles", "AICombatRolesDef", pGroup->pExpander, 
						X_OFFSET_CONTROL, y, 0, 1.f, UIUnitPercentage, 5, pDoc);
	y += STANDARD_ROW_HEIGHT;


	pGroup->pFSMTypeLabel = ETRefreshLabel(pGroup->pFSMTypeLabel, "FSM Source", "Specifies how the FSM for critters will be chosen.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
	pGroup->pFSMTypeField = ETRefreshEnumField(pGroup->pFSMTypeField, pOrigProps, pProps, parse_EncounterAIProperties, "FSMType", EncounterCritterOverrideTypeEnum,
		pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 140, UIUnitFixed, 5, pDoc);
	y += STANDARD_ROW_HEIGHT;

	if (pProps->eFSMType == EncounterCritterOverrideType_Specified) {
		pGroup->pFSMLabel = ETRefreshLabel(pGroup->pFSMLabel, "FSM", "If set, the FSM to apply to all actors (unless overriden on the actor).", X_OFFSET_BASE+20, 0, y, pGroup->pExpander);
		pGroup->pFSMField = ETRefreshDictionaryField(pGroup->pFSMField, pOrigProps, pProps, parse_EncounterAIProperties, "FSM", "FSM",
			pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
		y += STANDARD_ROW_HEIGHT;
	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pFSMLabel);
		MEFieldSafeDestroy(&pGroup->pFSMField);
	}

	// Display the FSM vars
	if (pProps->eFSMType == EncounterCritterOverrideType_Specified) {
		pFSM = GET_REF(pProps->hFSM);
	}
	if (pOrigProps && pOrigProps->eFSMType == EncounterCritterOverrideType_Specified) {
		pOrigFSM = GET_REF(pOrigProps->hFSM);
	}
	
	if(!pGroup->pFSMVarGroup) {
		pGroup->pFSMVarGroup = ETInitFSMVarGroup(pGroup->pDoc, pGroup->pExpander, false);
	}
	y = ETRefreshFSMVarGroup(pGroup->pFSMVarGroup, -1, y);
	//y = ETRefreshFSMVarDefs(pDoc, NULL, &pGroup->eaOverrideVarGroups, &pGroup->eaVarDefGroups, &pGroup->eaVarLabels, pGroup->pExpander, pOrigFSM, pFSM, pOrigProps ? &pOrigProps->eaVariableDefs : NULL, &pProps->eaVariableDefs, &pGroup->eaVarDefs, &pGroup->eaVarNames, &pGroup->pAddVarButton, y, ETAddFSMVariableDef, pGroup);

	ui_ExpanderSetHeight(pGroup->pExpander, y);
}


static void ETRefreshJobs(EncounterTemplateEditDoc *pDoc, U32 iExpanderIdx)
{
	AIJobDesc *pProps, *pOrigProps = NULL;
	AIJobDesc **eaJobs = NULL;
	AIJobDesc **eaOrigJobs = NULL;
	ETJobGroup *pGroup;
	ETJobSubGroup *pSubGroup;
	F32 y = 0;
	int i;

	encounterTemplate_FillAIJobEArray(pDoc->pTemplate, &eaJobs);
	encounterTemplate_FillAIJobEArray(pDoc->pTemplate, &eaOrigJobs);

	if (!pDoc->pJobGroup) {
		pDoc->pJobGroup = calloc(1,sizeof(ETJobGroup));
		pDoc->pJobGroup->pExpander = ETCreateExpander(pDoc->pEncounterExpanderGroup, "Actor Jobs", iExpanderIdx);
	}
	pGroup = pDoc->pJobGroup;
	pGroup->pDoc = pDoc;

	for(i=0; i<eaSize(&eaJobs); ++i) {
		char buf[128];

		if (i < eaSize(&pGroup->eaSubGroups)) {
			pSubGroup = pGroup->eaSubGroups[i];
		} else {
			pSubGroup = calloc(1,sizeof(ETJobSubGroup));
			pSubGroup->pGroup = pGroup;
			pSubGroup->iIndex = i;
			eaPush(&pGroup->eaSubGroups, pSubGroup);
		}

		pProps = eaJobs[i];
		if (pDoc->pOrigTemplate && (i < eaSize(&eaOrigJobs))) {
			pOrigProps = eaOrigJobs[i];
		} else {
			pOrigProps = NULL;
		}

		sprintf(buf, "Job Name #%d", i+1);
		pSubGroup->pJobNameLabel = ETRefreshLabel(pSubGroup->pJobNameLabel, buf, "The name of the job.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
		pSubGroup->pJobNameField = ETRefreshSimpleField(pSubGroup->pJobNameField, pOrigProps, pProps, parse_AIJobDesc, "jobname", kMEFieldType_TextEntry,
			pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 85, pDoc);

		if (!pSubGroup->pRemoveButton) {
			pSubGroup->pRemoveButton = ui_ButtonCreate("Remove", 5, y, ETRemoveJob, pSubGroup);
			ui_WidgetSetWidth(UI_WIDGET(pSubGroup->pRemoveButton), 70);
			ui_ExpanderAddChild(pGroup->pExpander, pSubGroup->pRemoveButton);
		} 
		ui_WidgetSetPositionEx(UI_WIDGET(pSubGroup->pRemoveButton), 5, y, 0, 0, UITopRight);
		y += STANDARD_ROW_HEIGHT;

		pSubGroup->pJobFSMLabel = ETRefreshLabel(pSubGroup->pJobFSMLabel, "FSM", "The FSM to apply for this job.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
		pSubGroup->pJobFSMField = ETRefreshDictionaryField(pSubGroup->pJobFSMField, pOrigProps, pProps, parse_AIJobDesc, "fsmname", "FSM",
			pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
		y += STANDARD_ROW_HEIGHT;

		pSubGroup->pJobRequiresLabel = ETRefreshLabel(pSubGroup->pJobRequiresLabel, "Requires Expr", "An actor can do a job when this is true.  If blank, all actors can do the job.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
		pSubGroup->pJobRequiresField = ETRefreshSimpleField(pSubGroup->pJobRequiresField, pOrigProps, pProps, parse_AIJobDesc, "JobRequiresBlock", kMEFieldTypeEx_Expression,
			pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
		y += STANDARD_ROW_HEIGHT;

		pSubGroup->pJobRatingLabel = ETRefreshLabel(pSubGroup->pJobRatingLabel, "Rating Expr", "The actor with the highest rating will do the job.  It will pick randomly if there is a tie.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
		pSubGroup->pJobRatingField = ETRefreshSimpleField(pSubGroup->pJobRatingField, pOrigProps, pProps, parse_AIJobDesc, "JobRatingBlock", kMEFieldTypeEx_Expression,
			pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
		y += STANDARD_ROW_HEIGHT;

		pSubGroup->pJobPriorityLabel = ETRefreshLabel(pSubGroup->pJobPriorityLabel, "Priority", "The priority of this job compared to other jobs.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
		pSubGroup->pJobPriorityField = ETRefreshSimpleField(pSubGroup->pJobPriorityField, pOrigProps, pProps, parse_AIJobDesc, "priority", kMEFieldType_TextEntry,
			pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 60, UIUnitFixed, 5, pDoc);
		y += STANDARD_ROW_HEIGHT;
	}

	// Free unused job groups
	if (pGroup->eaSubGroups) {
		for(i=eaSize(&pGroup->eaSubGroups)-1; i>=eaSize(&eaJobs); --i) {
			ETFreeJobSubGroup(pGroup->eaSubGroups[i]);
			eaRemove(&pGroup->eaSubGroups, i);
		}
	}

	if (!pGroup->pAddButton) {
		pGroup->pAddButton = ui_ButtonCreate("Add Job", X_OFFSET_BASE, y, ETAddJob, pGroup);
		ui_WidgetSetWidth(UI_WIDGET(pGroup->pAddButton), 100);
		ui_ExpanderAddChild(pGroup->pExpander, pGroup->pAddButton);
	} 
	ui_WidgetSetPosition(UI_WIDGET(pGroup->pAddButton), X_OFFSET_BASE, y);
	y += STANDARD_ROW_HEIGHT;

	ui_ExpanderSetHeight(pGroup->pExpander, y);

	eaDestroy(&eaJobs);
	eaDestroy(&eaOrigJobs);
}


static void ETRefreshSpawn(EncounterTemplateEditDoc *pDoc, U32 iExpanderIdx)
{
	EncounterSpawnProperties *pProps, *pOrigProps = NULL;
	ETSpawnGroup *pGroup;
	F32 y = 0;

	if (!pDoc->pSpawnGroup) {
		pDoc->pSpawnGroup = calloc(1,sizeof(ETSpawnGroup));
		pDoc->pSpawnGroup->pExpander = ETCreateExpander(pDoc->pEncounterExpanderGroup, "Spawn Rules", iExpanderIdx);
	}
	pGroup = pDoc->pSpawnGroup;

	if (ETRefreshOverrideCheckBox(&pGroup->pOverrideParentCheckbox, &pGroup->pExpander, "Override Parent", 16, y, pDoc->pTemplate->pSpawnProperties->bOverrideParentValues, pDoc, ETOverrideEncounterSpawnPropsToggled, pDoc, true))
		y += STANDARD_ROW_HEIGHT;

	pProps = encounterTemplate_GetSpawnProperties(pDoc->pTemplate);
	if (pDoc->pOrigTemplate) {
		pOrigProps = encounterTemplate_GetSpawnProperties(pDoc->pOrigTemplate);
	}

	if(!pProps)
	{
		pProps = pDoc->pTemplate->pSpawnProperties;
		if (!pProps)
			return;
	}

	pGroup->pSpawnAnimTypeLabel = ETRefreshLabel(pGroup->pSpawnAnimTypeLabel, "Spawn Animation", "Specifies how the spawn animation will be chosen.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
	pGroup->pSpawnAnimTypeField = ETRefreshEnumField(pGroup->pSpawnAnimTypeField, pOrigProps, pProps, parse_EncounterSpawnProperties, "SpawnAnimType", EncounterSpawnAnimTypeEnum,
		pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 140, UIUnitFixed, 5, pDoc);
	y += STANDARD_ROW_HEIGHT;

	if (pProps->eSpawnAnimType == EncounterSpawnAnimType_Specified) {
		pGroup->pSpawnAnimLabel = ETRefreshLabel(pGroup->pSpawnAnimLabel, "Animation", "The spawn animation to use for the encounter.", X_OFFSET_BASE+20, 0, y, pGroup->pExpander);
		pGroup->pSpawnAnimField = ETRefreshDictionaryField(pGroup->pSpawnAnimField, pOrigProps, pProps, parse_EncounterSpawnProperties, "SpawnAnim", "AIAnimList",
			pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
		y += STANDARD_ROW_HEIGHT;

		pGroup->pSpawnAnimTimeLabel = ETRefreshLabel(pGroup->pSpawnAnimTimeLabel, "Anim Time", "The time the spawn animation takes (in seconds), during which the critter is not able to move", X_OFFSET_BASE+20, 0, y, pGroup->pExpander);
		pGroup->pSpawnAnimTimeField = ETRefreshSimpleField(pGroup->pSpawnAnimTimeField, pOrigProps, pProps, parse_EncounterSpawnProperties, "SpawnLockdownTime", kMEFieldType_TextEntry,
			pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 80, UIUnitFixed, 5, pDoc);
		y += STANDARD_ROW_HEIGHT;
	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pSpawnAnimLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pSpawnAnimTimeLabel);
		MEFieldSafeDestroy(&pGroup->pSpawnAnimField);
		MEFieldSafeDestroy(&pGroup->pSpawnAnimTimeField);
	}

	pGroup->pIsAmbushLabel = ETRefreshLabel(pGroup->pIsAmbushLabel, "Is Ambush", "This is used by the Champions Nemesis ambush system.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
	pGroup->pIsAmbushField = ETRefreshSimpleField(pGroup->pIsAmbushField, pOrigProps, pProps, parse_EncounterSpawnProperties, "IsAmbush", kMEFieldType_BooleanCombo,
		pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 80, UIUnitFixed, 5, pDoc);
	y += STANDARD_ROW_HEIGHT;

	ui_ExpanderSetHeight(pGroup->pExpander, y);
}


static void ETRefreshWave(EncounterTemplateEditDoc *pDoc, U32 iExpanderIdx)
{
	EncounterWaveProperties *pProps, *pOrigProps = NULL;
	ETWaveGroup *pGroup;
	F32 y = 0;

	if (!pDoc->pWaveGroup) {
		pDoc->pWaveGroup = calloc(1,sizeof(ETWaveGroup));
		pDoc->pWaveGroup->pExpander = ETCreateExpander(pDoc->pEncounterExpanderGroup, "Wave Definition", iExpanderIdx);
	}
	pGroup = pDoc->pWaveGroup;

	if (ETRefreshOverrideCheckBox(&pGroup->pOverrideParentCheckbox, &pGroup->pExpander, "Override Parent", 16, y, pDoc->pTemplate->pWaveProperties->bOverrideParentValues, pDoc, ETOverrideEncounterWavePropsToggled, pDoc, true))
		y += STANDARD_ROW_HEIGHT;

	pProps = encounterTemplate_GetWaveProperties(pDoc->pTemplate);
	if (pDoc->pOrigTemplate) {
		pOrigProps = encounterTemplate_GetWaveProperties(pDoc->pOrigTemplate);
	}

	if(!pProps)
	{
		pProps = pDoc->pTemplate->pWaveProperties;
		if (!pProps)
			return;
	}

	pGroup->pWaveCondLabel = ETRefreshLabel(pGroup->pWaveCondLabel, "Wave Condition", "When this is true, the wave will continue.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
	pGroup->pWaveCondField = ETRefreshSimpleField(pGroup->pWaveCondField, pOrigProps, pProps, parse_EncounterWaveProperties, "WaveCondition", kMEFieldTypeEx_Expression,
		pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
	y += STANDARD_ROW_HEIGHT;

	pGroup->pWaveIntervalTypeLabel = ETRefreshLabel(pGroup->pWaveIntervalTypeLabel, "Wave Interval", "The is the time between waves.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
	pGroup->pWaveIntervalTypeField = ETRefreshEnumField(pGroup->pWaveIntervalTypeField, pOrigProps, pProps, parse_EncounterWaveProperties, "WaveIntervalType", WorldEncounterWaveTimerTypeEnum,
		pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 120, UIUnitFixed, 5, pDoc);
	y += STANDARD_ROW_HEIGHT;

	if (pProps->eWaveIntervalType != WorldEncounterWaveTimerType_Custom) {
		char buf[256];
		F32 fTime = encounter_GetWaveTimerValue(pProps->eWaveIntervalType, 0, zerovec3);
		sprintf(buf, "%g sec", fTime);
		pGroup->pWaveIntervalTimeLabel = ETRefreshLabel(pGroup->pWaveIntervalTimeLabel, buf, NULL, X_OFFSET_CONTROL+125, 0, y - STANDARD_ROW_HEIGHT, pGroup->pExpander);
	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pWaveIntervalTimeLabel);
	}

	if (pProps->eWaveIntervalType == WorldEncounterWaveTimerType_Custom) {
		pGroup->pWaveIntervalLabel = ETRefreshLabel(pGroup->pWaveIntervalLabel, "Time", "This is the time in seconds.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
		pGroup->pWaveIntervalField = ETRefreshSimpleField(pGroup->pWaveIntervalField, pOrigProps, pProps, parse_EncounterWaveProperties, "WaveInterval", kMEFieldType_TextEntry,
			pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 60, UIUnitFixed, 5, pDoc);
		y += STANDARD_ROW_HEIGHT;
	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pWaveIntervalLabel);
		MEFieldSafeDestroy(&pGroup->pWaveIntervalField);
	}

	pGroup->pWaveDelayTypeLabel = ETRefreshLabel(pGroup->pWaveDelayTypeLabel, "Wave Delay", "This is the delay between waves.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
	pGroup->pWaveDelayTypeField = ETRefreshEnumField(pGroup->pWaveDelayTypeField, pOrigProps, pProps, parse_EncounterWaveProperties, "WaveDelayType", WorldEncounterWaveDelayTimerTypeEnum,
		pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 120, UIUnitFixed, 5, pDoc);
	y += STANDARD_ROW_HEIGHT;

	if (pProps->eWaveDelayType != WorldEncounterWaveDelayTimerType_Custom) {
		char buf[256];
		F32 fMinTime, fMaxTime;
		encounter_GetWaveDelayTimerValue(pProps->eWaveDelayType, 0, 0, zerovec3, &fMinTime, &fMaxTime);
		if(fMinTime == fMaxTime)
			sprintf(buf, "%g sec", fMinTime);
		else 
			sprintf(buf, "%g sec - %g sec", fMinTime, fMaxTime);
		pGroup->pWaveDelayTimeLabel = ETRefreshLabel(pGroup->pWaveDelayTimeLabel, buf, NULL, X_OFFSET_CONTROL+125, 0, y - STANDARD_ROW_HEIGHT, pGroup->pExpander);
	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pWaveDelayTimeLabel);
	}

	if (pProps->eWaveDelayType == WorldEncounterWaveDelayTimerType_Custom) {
		pGroup->pWaveDelayMinLabel = ETRefreshLabel(pGroup->pWaveDelayMinLabel, "Time:     Min", "This is the time in seconds.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
		pGroup->pWaveDelayMinField = ETRefreshSimpleField(pGroup->pWaveDelayMinField, pOrigProps, pProps, parse_EncounterWaveProperties, "WaveDelayMin", kMEFieldType_TextEntry,
			pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 60, UIUnitFixed, 5, pDoc);

		pGroup->pWaveDelayMaxLabel = ETRefreshLabel(pGroup->pWaveDelayMaxLabel, "Max", "This is the time in seconds.", X_OFFSET_CONTROL+65, 0, y, pGroup->pExpander);
		pGroup->pWaveDelayMaxField = ETRefreshSimpleField(pGroup->pWaveDelayMaxField, pOrigProps, pProps, parse_EncounterWaveProperties, "WaveDelayMax", kMEFieldType_TextEntry,
			pGroup->pExpander, X_OFFSET_CONTROL+95, y, 0, 60, UIUnitFixed, 5, pDoc);
		y += STANDARD_ROW_HEIGHT;
	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pWaveDelayMinLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pWaveDelayMaxLabel);
		MEFieldSafeDestroy(&pGroup->pWaveDelayMinField);
		MEFieldSafeDestroy(&pGroup->pWaveDelayMaxField);
	}

	ui_ExpanderSetHeight(pGroup->pExpander, y);
}

static void ETRefreshRewards(EncounterTemplateEditDoc *pDoc, U32 iExpanderIdx)
{
	EncounterRewardProperties *pProps, *pOrigProps = NULL;
	ETRewardsGroup *pGroup;
	F32 y = 0;

	if (!pDoc->pRewardsGroup) {
		pDoc->pRewardsGroup = calloc(1,sizeof(ETRewardsGroup));
		pDoc->pRewardsGroup->pExpander = ETCreateExpander(pDoc->pEncounterExpanderGroup, "Rewards", iExpanderIdx);
	}
	pGroup = pDoc->pRewardsGroup;

	if (ETRefreshOverrideCheckBox(&pGroup->pOverrideParentCheckbox, &pGroup->pExpander, "Override Parent", 16, y, pDoc->pTemplate->pRewardProperties->bOverrideParentValues, pDoc, ETOverrideEncounterRewardPropsToggled, pDoc, true))
		y += STANDARD_ROW_HEIGHT;

	pProps = encounterTemplate_GetRewardProperties(pDoc->pTemplate);
	if (pDoc->pOrigTemplate) {
		pOrigProps = encounterTemplate_GetRewardProperties(pDoc->pOrigTemplate);
	}

	if(!pProps)
	{
		pProps = pDoc->pTemplate->pRewardProperties;
		if (!pProps)
			return;
	}

	pGroup->pRewardTypeLabel = ETRefreshLabel(pGroup->pRewardTypeLabel, "Reward Type", "How to handle granting rewards from this template.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
	pGroup->pRewardTypeField = ETRefreshEnumField(pGroup->pRewardTypeField, pOrigProps, pProps, parse_EncounterRewardProperties, "RewardType", WorldEncounterRewardTypeEnum,
		pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
	y += STANDARD_ROW_HEIGHT;

	if (pProps->eRewardType != kWorldEncounterRewardType_DefaultRewards)
	{
		pGroup->pRewardTableLabel = ETRefreshLabel(pGroup->pRewardTableLabel, "Reward Table", "The reward table to use.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
		pGroup->pRewardTableField = ETRefreshDictionaryField(pGroup->pRewardTableField, pOrigProps, pProps, parse_EncounterRewardProperties, "RewardTable", "RewardTable",
			pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 120, UIUnitFixed, 5, pDoc);
		y += STANDARD_ROW_HEIGHT;
	}
	else
	{
		ui_WidgetQueueFreeAndNull(&pGroup->pRewardTableLabel);
		MEFieldSafeDestroy(&pGroup->pRewardTableField);
		REMOVE_HANDLE(pProps->hRewardTable);
	}

	pGroup->pRewardLevelTypeLabel = ETRefreshLabel(pGroup->pRewardLevelTypeLabel, "Reward Level Type", "Decides what level to grant rewards.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
	pGroup->pRewardLevelTypeField = ETRefreshEnumField(pGroup->pRewardLevelTypeField, pOrigProps, pProps, parse_EncounterRewardProperties, "RewardLevelType", WorldEncounterRewardLevelTypeEnum,
		pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
	y += STANDARD_ROW_HEIGHT;

	if (pProps->eRewardLevelType == kWorldEncounterRewardLevelType_SpecificLevel)
	{
		pGroup->pRewardLevelLabel = ETRefreshLabel(pGroup->pRewardLevelLabel, "Reward Level", "The reward table to use.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
		pGroup->pRewardLevelField = ETRefreshSimpleField(pGroup->pRewardLevelField, pOrigProps, pProps, parse_EncounterRewardProperties, "RewardLevel", kMEFieldType_TextEntry,
			pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 120, UIUnitFixed, 5, pDoc);
		y += STANDARD_ROW_HEIGHT;
	}
	else
	{
		ui_WidgetQueueFreeAndNull(&pGroup->pRewardLevelLabel);
		MEFieldSafeDestroy(&pGroup->pRewardLevelField);
		pProps->iRewardLevel = 0;
	}

	ui_ExpanderSetHeight(pGroup->pExpander, y);
}


static F32 ETRefreshInteractions(ETActorGroup *pGroup, EncounterActorProperties *pOrigActor, EncounterActorProperties *pActor, F32 y)
{
	int iNumProps = 0;
	int i;
	WorldInteractionPropertyEntry** eaInteractionProps = NULL;
	WorldInteractionPropertyEntry** eaOrigInteractionProps = NULL;
	WorldInteractionProperties* pProps = NULL;
	WorldInteractionProperties* pOrigProps = NULL;

	if (pActor) {
		pProps = encounterTemplate_GetActorInteractionProps(pGroup->pDoc->pTemplate, pActor);
		pOrigProps = encounterTemplate_GetActorInteractionProps(pGroup->pDoc->pOrigTemplate, pOrigActor);
		encounterTemplate_FillActorInteractionEarray(pGroup->pDoc->pTemplate, pActor, &eaInteractionProps);
		encounterTemplate_FillActorInteractionEarray(pGroup->pDoc->pOrigTemplate, pOrigActor, &eaOrigInteractionProps);
		iNumProps = eaSize(&eaInteractionProps);
		for(i=0; i<iNumProps; ++i) {
			WorldInteractionPropertyEntry *pEntry = eaInteractionProps[i];
			WorldInteractionPropertyEntry *pOrigEntry = NULL;
			ETInteractGroup *pInteractGroup;
			char buf[128];

			// Get the orig entry
			if (pOrigActor && pOrigProps && (i < eaSize(&eaOrigInteractionProps))) {
				pOrigEntry = eaOrigInteractionProps[i];
			}

			// Get the interact group
			if (i >= eaSize(&pGroup->eaInteractGroups)) {
				pInteractGroup = calloc(1,sizeof(ETInteractGroup));
				pInteractGroup->iIndex = i;
				pInteractGroup->pActorGroup = pGroup;

				pInteractGroup->pInteractPropsGroup = calloc(1, sizeof(InteractionPropertiesGroup));
				pInteractGroup->pInteractPropsGroup->cbChange = ETFieldChangedCB;
				pInteractGroup->pInteractPropsGroup->cbPreChange = ETFieldPreChangeCB;
				pInteractGroup->pInteractPropsGroup->pParentData = pGroup->pDoc;
				pInteractGroup->pInteractPropsGroup->pExpander = pGroup->pExpander;
				pInteractGroup->pInteractPropsGroup->fOffsetX = X_OFFSET_BASE+20;
				pInteractGroup->pInteractPropsGroup->eType = InteractionDefType_Entity;

				eaPush(&pGroup->eaInteractGroups, pInteractGroup);
			} else {
				pInteractGroup = pGroup->eaInteractGroups[i];
			}

			// Refresh the label and remove button
			sprintf(buf, "Interaction #%d", i+1);
			pInteractGroup->pNameLabel = ETRefreshLabel(pInteractGroup->pNameLabel, buf, NULL, X_OFFSET_BASE, 0, y, pGroup->pExpander);

			if (!pInteractGroup->pRemoveButton) {
				pInteractGroup->pRemoveButton = ui_ButtonCreate("Remove Interaction", 0, y, ETRemoveInteraction, pInteractGroup);
				ui_ExpanderAddChild(pGroup->pExpander, pInteractGroup->pRemoveButton);
			}
			ui_WidgetSetPositionEx(UI_WIDGET(pInteractGroup->pRemoveButton), 0, y, 0, 0, UITopRight);

			y += STANDARD_ROW_HEIGHT;

			// Refresh entry properties
			y = UpdateInteractionPropertiesGroup(pInteractGroup->pInteractPropsGroup, pEntry, pOrigEntry, NULL, y);

			// Allow setting of interact range
			if (i == 0 && eaSize(&pProps->eaEntries)) {
				pGroup->pInteractRootLabel = ETRefreshLabel(pGroup->pInteractRootLabel, "Root Properties", NULL, X_OFFSET_BASE+20, 0, y, pGroup->pExpander);
				y += STANDARD_ROW_HEIGHT;

				pGroup->pInteractRangeLabel = ETRefreshLabel(pGroup->pInteractRangeLabel, "Interact Range", "This is the distance at which the actor can be interacted.  If this is zero, the default range for entities will be used.", X_OFFSET_BASE+35, 0, y, pGroup->pExpander);
				pGroup->pInteractRangeField = ETRefreshSimpleField(pGroup->pInteractRangeField, pOrigActor ? pOrigProps : NULL, pProps, parse_WorldInteractionProperties, "InteractDist", kMEFieldType_TextEntry,
												pGroup->pExpander, X_OFFSET_CONTROL+35, y, 0, 80, UIUnitFixed, 0, pGroup->pDoc);
				y += STANDARD_ROW_HEIGHT;
			}
		}
		eaDestroy(&eaInteractionProps);
		eaDestroy(&eaOrigInteractionProps);
	}

	// Remove unused groups
	for(i=eaSize(&pGroup->eaInteractGroups)-1; i>=iNumProps; --i) {
		ETFreeInteractGroup(pGroup->eaInteractGroups[i]);
		eaRemove(&pGroup->eaInteractGroups, i);
	}
	if (iNumProps == 0) {
		ui_WidgetQueueFreeAndNull(&pGroup->pInteractRootLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pInteractRangeLabel);
		MEFieldSafeDestroy(&pGroup->pInteractRangeField);
	}

	// Put in the ADD button
	if (!pGroup->pAddInteractButton) {
		pGroup->pAddInteractButton = ui_ButtonCreate("Add Interaction", 0, y, ETAddInteraction, pGroup);
		ui_ExpanderAddChild(pGroup->pExpander, pGroup->pAddInteractButton);
	}
	ui_WidgetSetPosition(UI_WIDGET(pGroup->pAddInteractButton), X_OFFSET_BASE, y);
	y += STANDARD_ROW_HEIGHT;

	return y;
}

static F32 ETRefreshActorSpawnGroup(ETActorSpawnGroup *pGroup, EncounterActorSpawnProperties* pOrigProps, EncounterActorSpawnProperties *pProps, F32 y)
{
	const char* pchDifficulty = pProps ? StaticDefineIntRevLookupNonNull(EncounterDifficultyEnum, pProps->eSpawnAtDifficulty) : "";
	bool bMultipleDifficulties = (encounter_GetEncounterDifficultiesCount() > 1);

	if(!pGroup || !pProps)
		return y;

	if(bMultipleDifficulties)
		pGroup->pDifficultyLabel = ETRefreshLabel(pGroup->pDifficultyLabel, pchDifficulty, "Select the team sizes which you would like this actor to spawn at when the difficulty is set to this value", X_OFFSET_CONTROL, 0, y, pGroup->pExpander);
	else if(pGroup->pDifficultyLabel)
		ui_WidgetQueueFreeAndNull(&pGroup->pDifficultyLabel);

	pGroup->pTeamSizeField = ETRefreshFlagEnumField(pGroup->pTeamSizeField, pOrigProps, pProps, parse_EncounterActorSpawnProperties, "SpawnAtTeamSize", TeamSizeFlagsEnum,
		pGroup->pExpander, X_OFFSET_CONTROL, y, bMultipleDifficulties?0.25:0, 120, UIUnitFixed, 5, pGroup->pDoc);
	y += STANDARD_ROW_HEIGHT;

	return y;

}


static void ETRefreshActor(EncounterTemplateEditDoc* pDoc, ETActorGroup *pGroup, EncounterActorProperties *pOrigActor, EncounterActorProperties *pActor, int iIndex)
{
	char buf[1024];
	F32 y = 0;
	FSM *pFSM = NULL;
	FSM *pOrigFSM = NULL;
	EncounterActorProperties **eaActors = NULL;
	EncounterActorSpawnProperties **ppSpawnProps = NULL;
	EncounterActorSpawnProperties **ppOrigSpawnProps = NULL;
	EncounterActorProperties* pParentActor = NULL;
	
	EncounterActorNameProperties* pNameProperties = encounterTemplate_GetActorNameProperties(pDoc->pTemplate, pActor);
	EncounterActorCritterProperties* pCritterProperties = encounterTemplate_GetActorCritterProperties(pDoc->pTemplate, pActor);
	EncounterActorFactionProperties* pFactionProperties = encounterTemplate_GetActorFactionProperties(pDoc->pTemplate, pActor);
	EncounterActorSpawnInfoProperties* pSpawnInfoProperties = encounterTemplate_GetActorSpawnInfoProperties(pDoc->pTemplate, pActor);
	EncounterActorMiscProperties* pMiscProperties = encounterTemplate_GetActorMiscProperties(pDoc->pTemplate, pActor);
	EncounterActorFSMProperties* pFsmProperties = encounterTemplate_GetActorFSMProperties(pDoc->pTemplate, pActor);
	
	EncounterActorNameProperties* pOrigNameProperties = encounterTemplate_GetActorNameProperties(pDoc->pTemplate, pOrigActor);
	EncounterActorCritterProperties* pOrigCritterProperties = encounterTemplate_GetActorCritterProperties(pDoc->pTemplate, pOrigActor);
	EncounterActorFactionProperties* pOrigFactionProperties = encounterTemplate_GetActorFactionProperties(pDoc->pTemplate, pOrigActor);
	EncounterActorSpawnInfoProperties* pOrigSpawnInfoProperties = encounterTemplate_GetActorSpawnInfoProperties(pDoc->pTemplate, pOrigActor);
	EncounterActorMiscProperties* pOrigMiscProperties = encounterTemplate_GetActorMiscProperties(pDoc->pTemplate, pOrigActor);
	EncounterActorFSMProperties* pOrigFsmProperties = encounterTemplate_GetActorFSMProperties(pDoc->pTemplate, pOrigActor);
	
	int i;
	int iNumDifficulties = MAX(encounter_GetEncounterDifficultiesCount(),1);
	bool bMultipleDifficulties = (iNumDifficulties > 1);
	int iPartitionIdx = PARTITION_CLIENT;
	bool bIsStandaloneActor = true;

	if (IS_HANDLE_ACTIVE(pDoc->pTemplate->hParent))
	{
		//don't fill default anything for critters which are inheriting from a parent.
		pParentActor = encounterTemplate_GetActorByName(GET_REF(pDoc->pTemplate->hParent), pActor->pcName);
		if (pParentActor)
		{
			bIsStandaloneActor = false;
		}
	}

	if (!pGroup->pExpander) {
		pGroup->pExpander = ETCreateExpander(pGroup->pDoc->pActorExpanderGroup, "Actor", iIndex);
	}
	sprintf(buf, "Actor: %s", pActor->pcName ? pActor->pcName : "Unnamed");
	ui_WidgetSetTextString(UI_WIDGET(pGroup->pExpander), buf);

	pGroup->pNameLabel = ETRefreshLabel(pGroup->pNameLabel, "Actor Name", "This is the identity of the actor.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
	pGroup->pNameField = ETRefreshSimpleField(pGroup->pNameField, pOrigActor, pActor, parse_EncounterActorProperties, "Name", kMEFieldType_TextEntry,
		pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pGroup->pDoc);
	MEFieldSetChangeCallback(pGroup->pNameField, ETActorNameChangedCB, pGroup->pDoc);
	y += STANDARD_ROW_HEIGHT;

	if (ETRefreshOverrideCheckBox(&pGroup->pOverrideDisplayNameCheckbox, &pGroup->pExpander, "Override Display Name", 16, y, pActor->bOverrideDisplayName, pDoc, ETOverrideActorDisplayNameToggled, pGroup, !bIsStandaloneActor))
		y += STANDARD_ROW_HEIGHT;

	pGroup->pDispNameTypeLabel = ETRefreshLabel(pGroup->pDispNameTypeLabel, "Display Name", "This is how the display name is determined.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
	pGroup->pDispNameTypeField = ETRefreshEnumField(pGroup->pDispNameTypeField, pOrigNameProperties, pNameProperties, parse_EncounterActorNameProperties, "DisplayNameType", EncounterCritterOverrideTypeEnum,
		pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 120, UIUnitFixed, 5, pGroup->pDoc);
	y += STANDARD_ROW_HEIGHT;

	if (pNameProperties->eDisplayNameType == EncounterCritterOverrideType_Specified) {
		pGroup->pDispNameLabel = ETRefreshLabel(pGroup->pDispNameLabel, "Name", "The display name to use for the critter", X_OFFSET_BASE+20, 0, y, pGroup->pExpander);
		pGroup->pDispNameField = ETRefreshSimpleField(pGroup->pDispNameField, pOrigNameProperties ? &pOrigNameProperties->displayNameMsg : NULL, &pNameProperties->displayNameMsg, parse_DisplayMessage, "EditorCopy", kMEFieldType_Message,
			pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pGroup->pDoc);
		y += STANDARD_ROW_HEIGHT;
	} else {
		CritterDef *pCritter = encounterTemplate_GetActorCritterDef(pGroup->pDoc->pTemplate, pActor, iPartitionIdx);
		if (pCritter) {
			Message *pMsg = GET_REF(pCritter->displayNameMsg.hMessage);
			if (pMsg && pMsg->pcDefaultString) {
				sprintf(buf, "(%s)", pMsg->pcDefaultString);
			} else {
				sprintf(buf, "(* None *)");
			}
		} else {
			sprintf(buf, "(* Depends on Critter Chosen *)");
		}
		pGroup->pDispNameLabel = ETRefreshLabel(pGroup->pDispNameLabel, buf, NULL, X_OFFSET_CONTROL+125, 0, y - STANDARD_ROW_HEIGHT, pGroup->pExpander);

		MEFieldSafeDestroy(&pGroup->pDispNameField);
	}

	if (ETRefreshOverrideCheckBox(&pGroup->pOverrideDisplaySubNameCheckbox, &pGroup->pExpander, "Override Display Sub Name", 16, y, pActor->bOverrideDisplaySubName, pDoc, ETOverrideActorDisplaySubNameToggled, pGroup, !bIsStandaloneActor))
		y += STANDARD_ROW_HEIGHT;

	pGroup->pDispSubNameTypeLabel = ETRefreshLabel(pGroup->pDispSubNameTypeLabel, "Display Sub Name", "This is how the display sub name is determined.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
	pGroup->pDispSubNameTypeField = ETRefreshEnumField(pGroup->pDispSubNameTypeField, pOrigActor, pActor, parse_EncounterActorProperties, "DisplaySubNameType", EncounterCritterOverrideTypeEnum,
		pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 120, UIUnitFixed, 5, pGroup->pDoc);
	y += STANDARD_ROW_HEIGHT;

	if (pNameProperties->eDisplaySubNameType == EncounterCritterOverrideType_Specified) {
		pGroup->pDispSubNameLabel = ETRefreshLabel(pGroup->pDispSubNameLabel, "Sub Name", "The display subname to use for the critter", X_OFFSET_BASE+20, 0, y, pGroup->pExpander);
		pGroup->pDispSubNameField = ETRefreshSimpleField(pGroup->pDispSubNameField, pOrigNameProperties ? &pOrigNameProperties->displaySubNameMsg : NULL, &pNameProperties->displaySubNameMsg, parse_DisplayMessage, "EditorCopy", kMEFieldType_Message,
			pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pGroup->pDoc);
		y += STANDARD_ROW_HEIGHT;
	} else {
		CritterDef *pCritter = encounterTemplate_GetActorCritterDef(pGroup->pDoc->pTemplate, pActor, iPartitionIdx);
		if (pCritter) {
			Message *pMsg = GET_REF(pCritter->displaySubNameMsg.hMessage);
			if (pMsg && pMsg->pcDefaultString) {
				sprintf(buf, "(%s)", pMsg->pcDefaultString);
			} else {
				sprintf(buf, "(* None *)");
			}
		} else {
			sprintf(buf, "(* Depends on Critter Chosen *)");
		}
		pGroup->pDispSubNameLabel = ETRefreshLabel(pGroup->pDispSubNameLabel, buf, NULL, X_OFFSET_CONTROL+125, 0, y - STANDARD_ROW_HEIGHT, pGroup->pExpander);

		MEFieldSafeDestroy(&pGroup->pDispSubNameField);
	}

	pGroup->pCommentsLabel = ETRefreshLabel(pGroup->pCommentsLabel, "Comments", "Designer notes about this actor.  Never shown in game.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
	pGroup->pCommentsField = ETRefreshSimpleField(pGroup->pCommentsField, pOrigNameProperties, pNameProperties, parse_EncounterActorNameProperties, "Comments", kMEFieldType_MultiText,
		pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pGroup->pDoc);
	y += STANDARD_ROW_HEIGHT;

	if (ETRefreshOverrideCheckBox(&pGroup->pOverrideCritterTypeCheckbox, &pGroup->pExpander, "Override Type Info", 16, y, pActor->bOverrideCritterType, pDoc, ETOverrideActorCritterTypeToggled, pGroup, !bIsStandaloneActor))
		y += STANDARD_ROW_HEIGHT;

	pGroup->pCritterTypeLabel = ETRefreshLabel(pGroup->pCritterTypeLabel, "Critter Type", "This is how the critter's type is determined.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
	pGroup->pCritterTypeField = ETRefreshEnumField(pGroup->pCritterTypeField, pOrigCritterProperties, pCritterProperties, parse_EncounterActorCritterProperties, "CritterType", ActorCritterTypeEnum,
		pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 140, UIUnitFixed, 5, pGroup->pDoc);
	MEFieldSetChangeCallback(pGroup->pCritterTypeField, ETCritterTypeChangedCB, pGroup->pDoc);
	y += STANDARD_ROW_HEIGHT;

	if (pCritterProperties->eCritterType == ActorCritterType_FromTemplate) {
		CritterGroup* pTemplateCritterGroup = encounterTemplate_GetCritterGroup(pGroup->pDoc->pTemplate, iPartitionIdx);
		if (pTemplateCritterGroup) {
			sprintf(buf, "(%s)", pTemplateCritterGroup->pchName);
		} else {
			sprintf(buf, "(* Not Set *)");
		}
		pGroup->pCritterLabel1 = ETRefreshLabel(pGroup->pCritterLabel1, buf, NULL, X_OFFSET_CONTROL + 145, 0, y - STANDARD_ROW_HEIGHT, pGroup->pExpander);
	}

	if (pCritterProperties->eCritterType == ActorCritterType_CritterDef) {
		pGroup->pCritterLabel1 = ETRefreshLabel(pGroup->pCritterLabel1, "Critter Def", "The def to use for the critter", X_OFFSET_BASE+20, 0, y, pGroup->pExpander);
		pGroup->pCritterDefField = ETRefreshDictionaryField(pGroup->pCritterDefField, pOrigCritterProperties, pCritterProperties, parse_EncounterActorCritterProperties, "CritterDef", "CritterDef",
			pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pGroup->pDoc);
		y += STANDARD_ROW_HEIGHT;
	} else {
		MEFieldSafeDestroy(&pGroup->pCritterDefField);
	}

	if (pCritterProperties->eCritterType == ActorCritterType_CritterGroup) {
		pGroup->pCritterLabel1 = ETRefreshLabel(pGroup->pCritterLabel1, "Critter Group", "The group to use for the critter", X_OFFSET_BASE+20, 0, y, pGroup->pExpander);
		pGroup->pCritterGroupField = ETRefreshDictionaryField(pGroup->pCritterGroupField, pOrigCritterProperties, pCritterProperties, parse_EncounterActorCritterProperties, "CritterGroup", "CritterGroup",
			pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 120, UIUnitFixed, 5, pGroup->pDoc);
		y += STANDARD_ROW_HEIGHT;
	} else {
		MEFieldSafeDestroy(&pGroup->pCritterGroupField);
	}

	if ((pCritterProperties->eCritterType == ActorCritterType_MapVariableDef) || 
		(pCritterProperties->eCritterType == ActorCritterType_MapVariableGroup)) {
		pGroup->pCritterLabel1 = ETRefreshLabel(pGroup->pCritterLabel1, "Map Variable", "The map variable to use for the critter def", X_OFFSET_BASE+20, 0, y, pGroup->pExpander);
		pGroup->pCritterMapVarField = ETRefreshDataField(pGroup->pCritterMapVarField, pOrigCritterProperties, pCritterProperties, parse_EncounterActorCritterProperties, "CritterMapVariable", &pGroup->pDoc->eaVarNames, false, 
			pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pGroup->pDoc);
		y += STANDARD_ROW_HEIGHT;
	} else {
		MEFieldSafeDestroy(&pGroup->pCritterMapVarField);
	}

	if (pCritterProperties->eCritterType == ActorCritterType_CritterGroup || 
		pCritterProperties->eCritterType == ActorCritterType_MapVariableGroup ||
		pCritterProperties->eCritterType == ActorCritterType_FromTemplate ||
		pCritterProperties->eCritterType == ActorCritterType_NemesisMinion ||
		pCritterProperties->eCritterType == ActorCritterType_NemesisMinionNormal || 
		pCritterProperties->eCritterType == ActorCritterType_NemesisMinionTeam ||
		pCritterProperties->eCritterType == ActorCritterType_NemesisMinionForLeader ) {
		pGroup->pCritterRankLabel = ETRefreshLabel(pGroup->pCritterRankLabel, "Rank", "The rank to use for the critter", X_OFFSET_BASE+20, 0, y, pGroup->pExpander);
		pGroup->pCritterRankField = ETRefreshDataField(pGroup->pCritterRankField, pOrigCritterProperties, pCritterProperties, parse_EncounterActorCritterProperties, "CritterRank", &g_eaCritterRankNames, true,
			pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 120, UIUnitFixed, 5, pGroup->pDoc);
		y += STANDARD_ROW_HEIGHT;
	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pCritterRankLabel);
		MEFieldSafeDestroy(&pGroup->pCritterRankField);
	}

	if(pCritterProperties->eCritterType == ActorCritterType_CritterGroup || 
		pCritterProperties->eCritterType == ActorCritterType_MapVariableGroup ||
		pCritterProperties->eCritterType == ActorCritterType_FromTemplate ||
		pCritterProperties->eCritterType == ActorCritterType_CritterDef ||
		pCritterProperties->eCritterType == ActorCritterType_MapVariableDef ||
		pCritterProperties->eCritterType == ActorCritterType_NemesisMinion ||
		pCritterProperties->eCritterType == ActorCritterType_NemesisMinionNormal || 
		pCritterProperties->eCritterType == ActorCritterType_NemesisMinionTeam ||
		pCritterProperties->eCritterType == ActorCritterType_NemesisMinionForLeader ) {
		pGroup->pCritterSubRankLabel = ETRefreshLabel(pGroup->pCritterSubRankLabel, "Sub-Rank", "The sub-Rank to use for the critter", X_OFFSET_BASE+20, 0, y, pGroup->pExpander);
		pGroup->pCritterSubRankField = ETRefreshDataField(pGroup->pCritterSubRankField, pOrigCritterProperties, pCritterProperties, parse_EncounterActorCritterProperties, "CritterSubRank", &g_eaCritterSubRankNames, true,
			pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 120, UIUnitFixed, 5, pGroup->pDoc);
		y += STANDARD_ROW_HEIGHT;
	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pCritterSubRankLabel);
		MEFieldSafeDestroy(&pGroup->pCritterSubRankField);
	}

	if(pCritterProperties->eCritterType == ActorCritterType_NemesisTeam || pCritterProperties->eCritterType == ActorCritterType_NemesisMinionTeam)
	{
		pGroup->pNemesisTeamIndexLabel = ETRefreshLabel(pGroup->pNemesisTeamIndexLabel, "Team Index", "The index into team to use (0-4).", X_OFFSET_BASE, 0, y, pGroup->pExpander);
		pGroup->pNemesisTeamIndexField = ETRefreshSimpleField(pGroup->pNemesisTeamIndexField, pOrigCritterProperties, pCritterProperties, parse_EncounterActorCritterProperties, "NemesisTeamIndex", kMEFieldType_TextEntry,
			pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 80, UIUnitFixed, 5, pGroup->pDoc);
		y += STANDARD_ROW_HEIGHT;
	}
	else
	{
		ui_WidgetQueueFreeAndNull(&pGroup->pNemesisTeamIndexLabel);
		MEFieldSafeDestroy(&pGroup->pNemesisTeamIndexField);
	}

	if(pCritterProperties->eCritterType == ActorCritterType_NemesisForLeader || pCritterProperties->eCritterType == ActorCritterType_NemesisMinionForLeader)
	{
		pGroup->pNemesisLeaderLabel = ETRefreshLabel(pGroup->pNemesisLeaderLabel, "Entire team", "If the leader doesn't have a nemesis find one in team.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
		pGroup->pNemesisLeaderField = ETRefreshSimpleField(pGroup->pNemesisLeaderField, pOrigCritterProperties, pCritterProperties, parse_EncounterActorCritterProperties, "NemesisLeaderTeam", kMEFieldType_BooleanCombo,
			pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 80, UIUnitFixed, 5, pGroup->pDoc);
		y += STANDARD_ROW_HEIGHT;
	}
	else
	{
		ui_WidgetQueueFreeAndNull(&pGroup->pNemesisLeaderLabel);
		MEFieldSafeDestroy(&pGroup->pNemesisLeaderField);
	}

	pGroup->pLevelOffsetLabel = ETRefreshLabel(pGroup->pLevelOffsetLabel, "Level Offset", "A level offset is added to the level of the encounter to change an individual actor's level. 0 means use encounter level.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
	pGroup->pLevelOffsetField = ETRefreshSimpleField(pGroup->pLevelOffsetField, pOrigCritterProperties, pCritterProperties, parse_EncounterActorCritterProperties, "LevelOffset", kMEFieldType_TextEntry,
		pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 80, UIUnitFixed, 5, pGroup->pDoc);
	y += STANDARD_ROW_HEIGHT;

	if (pCritterProperties->eCritterType == ActorCritterType_PetContactList) {
		pGroup->pPetContactListLabel = ETRefreshLabel(pGroup->pPetContactListLabel, "Pet Contact", "The Pet Contact List to use for this critter.", X_OFFSET_BASE+20, 0, y, pGroup->pExpander);
		pGroup->pPetContactListField = ETRefreshDictionaryField(pGroup->pPetContactListField, pOrigCritterProperties, pCritterProperties, parse_EncounterActorCritterProperties, "PetContactList", "PetContactList", pGroup->pExpander,
			X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pGroup->pDoc);
		y+=STANDARD_ROW_HEIGHT;
	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pPetContactListLabel);
		MEFieldSafeDestroy(&pGroup->pPetContactListField);
	}

	if (ETRefreshOverrideCheckBox(&pGroup->pOverrideFactionCheckbox, &pGroup->pExpander, "Override Faction Info", 16, y, pActor->bOverrideFaction, pDoc, ETOverrideActorFactionToggled, pGroup, !bIsStandaloneActor))
		y += STANDARD_ROW_HEIGHT;

	pGroup->pFactionTypeLabel = ETRefreshLabel(pGroup->pFactionTypeLabel, "Critter Faction", "This is how the critter's faction is determined.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
	pGroup->pFactionTypeField = ETRefreshEnumField(pGroup->pFactionTypeField, pOrigFactionProperties, pFactionProperties, parse_EncounterActorFactionProperties, "CritterFactionType", EncounterTemplateOverrideTypeEnum,
		pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 120, UIUnitFixed, 5, pGroup->pDoc);
	y += STANDARD_ROW_HEIGHT;

	if (pFactionProperties->eFactionType == EncounterTemplateOverrideType_Specified) {
		pGroup->pFactionLabel = ETRefreshLabel(pGroup->pFactionLabel, "Faction", "The faction and gang to use for the critter", X_OFFSET_BASE+20, 0, y, pGroup->pExpander);
		pGroup->pFactionField = ETRefreshDictionaryField(pGroup->pFactionField, pOrigFactionProperties, pFactionProperties, parse_EncounterActorFactionProperties, "CritterFaction", "CritterFaction",
			pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pGroup->pDoc);
		y += STANDARD_ROW_HEIGHT;

		pGroup->pGangLabel = ETRefreshLabel(pGroup->pGangLabel, "Gang ID", "The gang ID to use for the critter", X_OFFSET_BASE+20, 0, y, pGroup->pExpander);
		pGroup->pGangField = ETRefreshSimpleField(pGroup->pGangField, pOrigFactionProperties, pFactionProperties, parse_EncounterActorFactionProperties, "CritterGang", kMEFieldType_TextEntry,
			pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 60, UIUnitFixed, 5, pGroup->pDoc);
		y += STANDARD_ROW_HEIGHT;
	} else {
		char *estrName = NULL;
		if (encounterTemplate_GetActorFactionName(pGroup->pDoc->pTemplate, pActor, iPartitionIdx, &estrName)) {
			int iGangID = encounterTemplate_GetActorGangID(pGroup->pDoc->pTemplate, pActor, iPartitionIdx);
			sprintf(buf, "(%s, gang %d)", estrName, iGangID);
			estrDestroy(&estrName);
		} else {
			sprintf(buf, "(* Depends on Critter Chosen *)");
		}
		pGroup->pFactionLabel = ETRefreshLabel(pGroup->pFactionLabel, buf, NULL, X_OFFSET_CONTROL+125, 0, y - STANDARD_ROW_HEIGHT, pGroup->pExpander);

		ui_WidgetQueueFreeAndNull(&pGroup->pGangLabel);
		MEFieldSafeDestroy(&pGroup->pFactionField);
		MEFieldSafeDestroy(&pGroup->pGangField);
	}

	if (ETRefreshOverrideCheckBox(&pGroup->pOverrideSpawnConditionsCheckbox, &pGroup->pExpander, "Override Spawn Conditions", 16, y, pActor->bOverrideSpawnConditions, pDoc, ETOverrideActorSpawnConditionsToggled, pGroup, !bIsStandaloneActor))
		y += STANDARD_ROW_HEIGHT;

	// Spawn at team size
	if(bMultipleDifficulties)
	{
		pGroup->pUseTeamSizeLabel = ETRefreshLabel(pGroup->pUseTeamSizeLabel, "Spawn at:", "This specified which difficulty and team sizes the actor will spawn for.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
		pGroup->pSpawnAtDifficultyLabel = ETRefreshLabel(pGroup->pSpawnAtDifficultyLabel, "Difficulty", "The difficulties at which to spawn the actor", X_OFFSET_CONTROL, 0, y, pGroup->pExpander);
		pGroup->pSpawnAtTeamSizeLabel = ETRefreshLabel(pGroup->pSpawnAtTeamSizeLabel, "Team Size", "The team sizes at which to spawn the actor.", X_OFFSET_CONTROL, 0.25, y, pGroup->pExpander);
		y += STANDARD_ROW_HEIGHT;
	}
	else
	{
		pGroup->pUseTeamSizeLabel = ETRefreshLabel(pGroup->pUseTeamSizeLabel, "Spawn at Tm Size", "This specified which team sizes the actor will spawn for.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
	}

	ppSpawnProps = encounterTemplate_GetActorSpawnProps(pDoc->pTemplate, pActor);
	ppOrigSpawnProps = encounterTemplate_GetActorSpawnProps(pDoc->pTemplate, pOrigActor);
	for(i = 0; i < iNumDifficulties && i < eaSize(&ppSpawnProps); i++)
	{
		EncounterActorSpawnProperties *pProps = ppSpawnProps[i];
		EncounterActorSpawnProperties *pOrigProps = NULL;

		if(pOrigActor && ppOrigSpawnProps && eaSize(&ppOrigSpawnProps) > 0 && i < eaSize(&ppOrigSpawnProps))
			pOrigProps = ppOrigSpawnProps[i];

		while(eaSize(&pGroup->eaSpawnGroups) <= i)
		{
			ETActorSpawnGroup *pSpawnGroup = calloc(1,sizeof(ETActorSpawnGroup));
			pSpawnGroup->pDoc = pGroup->pDoc;
			pSpawnGroup->pExpander = pGroup->pExpander;
			eaPush(&pGroup->eaSpawnGroups, pSpawnGroup);
		}
		y = ETRefreshActorSpawnGroup(pGroup->eaSpawnGroups[i], pOrigProps, pProps, y);
	}

	while(eaSize(&pGroup->eaSpawnGroups) > i)
	{
		ETFreeActorSpawnGroup(eaPop(&pGroup->eaSpawnGroups));
	}

	// Boss at team size
	if(bMultipleDifficulties)
	{
		pGroup->pBossTeamSizeLabel = ETRefreshLabel(pGroup->pBossTeamSizeLabel, "Boss at:", "This specified which difficulty and team sizes the actor will get a boss bar.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
		pGroup->pBossAtDifficultyLabel = ETRefreshLabel(pGroup->pBossAtDifficultyLabel, "Difficulty", "The difficulties at which to spawn the actor", X_OFFSET_CONTROL, 0, y, pGroup->pExpander);
		pGroup->pBossAtTeamSizeLabel = ETRefreshLabel(pGroup->pBossAtTeamSizeLabel, "Team Size", "The team sizes at which to spawn the actor.", X_OFFSET_CONTROL, 0.25, y, pGroup->pExpander);
		y += STANDARD_ROW_HEIGHT;
	}
	else
	{
		pGroup->pBossTeamSizeLabel = ETRefreshLabel(pGroup->pBossTeamSizeLabel, "Boss at Tm Size", "This specified which team sizes the actor will get a boss bar.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
	}

	ppSpawnProps = encounterTemplate_GetActorBossProps(pDoc->pTemplate, pActor);
	ppOrigSpawnProps = encounterTemplate_GetActorBossProps(pDoc->pTemplate, pOrigActor);
	for(i = 0; i < iNumDifficulties && i < eaSize(&ppSpawnProps); i++)
	{
		EncounterActorSpawnProperties *pProps = ppSpawnProps[i];
		EncounterActorSpawnProperties *pOrigProps = NULL;

		if(pOrigActor && ppOrigSpawnProps && eaSize(&ppOrigSpawnProps) > 0 && i < eaSize(&ppOrigSpawnProps))
			pOrigProps = ppOrigSpawnProps[i];

		while(eaSize(&pGroup->eaBossSpawnGroups) <= i)
		{
			ETActorSpawnGroup *pSpawnGroup = calloc(1,sizeof(ETActorSpawnGroup));
			pSpawnGroup->pDoc = pGroup->pDoc;
			pSpawnGroup->pExpander = pGroup->pExpander;
			eaPush(&pGroup->eaBossSpawnGroups, pSpawnGroup);
		}
		y = ETRefreshActorSpawnGroup(pGroup->eaBossSpawnGroups[i], pOrigProps, pProps, y);
	}

	while(eaSize(&pGroup->eaBossSpawnGroups) > i)
	{
		ETFreeActorSpawnGroup(eaPop(&pGroup->eaBossSpawnGroups));
	}

	if (ETRefreshOverrideCheckBox(&pGroup->pOverrideSpawnAnimCheckbox, &pGroup->pExpander, "Override Spawn Anims", 16, y, pActor->bOverrideCritterSpawnInfo, pDoc, ETOverrideActorSpawnAnimToggled, pGroup, !bIsStandaloneActor))
		y += STANDARD_ROW_HEIGHT;

	pGroup->pSpawnAnimTypeLabel = ETRefreshLabel(pGroup->pSpawnAnimTypeLabel, "Spawn Animation", "This is how the critter's spawn animation is determined.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
	pGroup->pSpawnAnimTypeField = ETRefreshEnumField(pGroup->pSpawnAnimTypeField, pOrigSpawnInfoProperties, pSpawnInfoProperties, parse_EncounterActorSpawnInfoProperties, "SpawnAnimType", EncounterTemplateOverrideTypeEnum,
		pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 120, UIUnitFixed, 5, pGroup->pDoc);
	y += STANDARD_ROW_HEIGHT;

	if (pSpawnInfoProperties->eSpawnAnimType == EncounterTemplateOverrideType_Specified) {
		pGroup->pSpawnAnimLabel = ETRefreshLabel(pGroup->pSpawnAnimLabel, "Animation", "The spawn animation for the critter", X_OFFSET_BASE+20, 0, y, pGroup->pExpander);
		pGroup->pSpawnAnimField = ETRefreshDictionaryField(pGroup->pSpawnAnimField, pOrigSpawnInfoProperties, pSpawnInfoProperties, parse_EncounterActorSpawnInfoProperties, "SpawnAnim", "AIAnimList",
			pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pGroup->pDoc);
		y += STANDARD_ROW_HEIGHT;

		pGroup->pSpawnAnimTimeLabel = ETRefreshLabel(pGroup->pSpawnAnimTimeLabel, "Anim Time", "The time of the spawn animation for the critter (in seconds), during which it can't move", X_OFFSET_BASE+20, 0, y, pGroup->pExpander);
		pGroup->pSpawnAnimTimeField = ETRefreshSimpleField(pGroup->pSpawnAnimTimeField, pOrigSpawnInfoProperties, pSpawnInfoProperties, parse_EncounterActorSpawnInfoProperties, "SpawnLockdownTime", kMEFieldType_TextEntry,
			pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 80, UIUnitFixed, 5, pGroup->pDoc);
		y += STANDARD_ROW_HEIGHT;
	} else {
		F32 fLockoutTime = 0;
		const char *pcSpawnAnim = encounterTemplate_GetActorSpawnAnim(pGroup->pDoc->pTemplate, pActor, iPartitionIdx, &fLockoutTime);
		if (pcSpawnAnim) {
			sprintf(buf, "(%s, %f sec)", pcSpawnAnim, fLockoutTime);
		} else if (encounterTemplate_IsActorSpawnAnimKnown(pGroup->pDoc->pTemplate, pActor)) {
			sprintf(buf, "(* None *)");
		} else {
			sprintf(buf, "(* Depends on Critter Chosen *)");
		}
		pGroup->pSpawnAnimLabel = ETRefreshLabel(pGroup->pSpawnAnimLabel, buf, NULL, X_OFFSET_CONTROL+125, 0, y - STANDARD_ROW_HEIGHT, pGroup->pExpander);

		ui_WidgetQueueFreeAndNull(&pGroup->pSpawnAnimTimeLabel);
		MEFieldSafeDestroy(&pGroup->pSpawnAnimField);
		MEFieldSafeDestroy(&pGroup->pSpawnAnimTimeField);
	}

	if (ETRefreshOverrideCheckBox(&pGroup->pOverrideMiscCheckbox, &pGroup->pExpander, "Override Misc Info", 16, y, pActor->bOverrideMisc, pDoc, ETOverrideActorMiscToggled, pGroup, !bIsStandaloneActor))
		y += STANDARD_ROW_HEIGHT;

	pGroup->pNonCombatLabel = ETRefreshLabel(pGroup->pNonCombatLabel, "Is Non-Combatant", "An actor that is 'non-combatant' does not need to be killed to consider the encounter successful.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
	pGroup->pNonCombatField = ETRefreshSimpleField(pGroup->pNonCombatField, pOrigMiscProperties, pMiscProperties, parse_EncounterActorMiscProperties, "IsNonCombatant", kMEFieldType_BooleanCombo,
		pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 80, UIUnitFixed, 5, pGroup->pDoc);
	y += STANDARD_ROW_HEIGHT;

	pGroup->pCombatRoleLabel = ETRefreshLabel(pGroup->pCombatRoleLabel, "Combat Role", "Name of the combat role defined in the encounter template's Combat Role Def.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
	pGroup->pCombatRoleField = ETRefreshSimpleField(pGroup->pCombatRoleField, pOrigMiscProperties, pMiscProperties, parse_EncounterActorMiscProperties, "CombatRole", kMEFieldType_TextEntry,
		pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 80, UIUnitFixed, 5, pGroup->pDoc);
	y += STANDARD_ROW_HEIGHT;

	if (ETRefreshOverrideCheckBox(&pGroup->pOverrideActorFSMCheckbox, &pGroup->pExpander, "Override FSM Data", 16, y, pActor->bOverrideFSMInfo, pDoc, ETOverrideActorFSMToggled, pGroup, !bIsStandaloneActor))
		y += STANDARD_ROW_HEIGHT;


	pGroup->pFSMTypeLabel = ETRefreshLabel(pGroup->pFSMTypeLabel, "FSM Source", "This is how the critter's FSM is determined.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
	pGroup->pFSMTypeField = ETRefreshEnumField(pGroup->pFSMTypeField, pOrigFsmProperties, pFsmProperties, parse_EncounterActorFSMProperties, "FSMType", EncounterTemplateOverrideTypeEnum,
		pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 120, UIUnitFixed, 5, pGroup->pDoc);
	y += STANDARD_ROW_HEIGHT;

	if (pFsmProperties->eFSMType == EncounterTemplateOverrideType_Specified) {
		pGroup->pFSMLabel = ETRefreshLabel(pGroup->pFSMLabel, "FSM", "The FSM for the critter", X_OFFSET_BASE+20, 0, y, pGroup->pExpander);
		pGroup->pFSMField = ETRefreshDictionaryField(pGroup->pFSMField, pOrigFsmProperties, pFsmProperties, parse_EncounterActorFSMProperties, "FSM", "FSM",
			pGroup->pExpander, X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pGroup->pDoc);
		y += STANDARD_ROW_HEIGHT;
	} else {
		char *estrName = NULL;
		if (encounterTemplate_GetActorFSMName(pGroup->pDoc->pTemplate, pActor, iPartitionIdx, &estrName)) {
			sprintf(buf, "(%s)", estrName);
			estrDestroy(&estrName);
		} else {
			sprintf(buf, "(* Depends on Critter Chosen *)");
		}
		pGroup->pFSMLabel = ETRefreshLabel(pGroup->pFSMLabel, buf, NULL, X_OFFSET_CONTROL+125, 0, y - STANDARD_ROW_HEIGHT, pGroup->pExpander);

		MEFieldSafeDestroy(&pGroup->pFSMField);
	}

	// Get the FSM from wherever it comes from
	pFSM = encounterTemplate_GetActorFSM(pGroup->pDoc->pTemplate, pActor, iPartitionIdx);
	pOrigFSM = encounterTemplate_GetActorFSM(pGroup->pDoc->pOrigTemplate, pOrigActor, iPartitionIdx);
 
	if(!pGroup->pFSMVarGroup) {
		pGroup->pFSMVarGroup = ETInitFSMVarGroup(pGroup->pDoc, pGroup->pExpander, true);
	}
	y = ETRefreshFSMVarGroup(pGroup->pFSMVarGroup, iIndex, y);

	y = ETRefreshInteractions(pGroup, pOrigActor, pActor, y);

	encounterTemplate_FillActorEarray(pGroup->pDoc->pTemplate, &eaActors);

	ETRefreshButtonSet(pGroup->pExpander, 0, y, (iIndex > 0), (iIndex < eaSize(&eaActors)-1), "Delete Actor", &pGroup->pRemoveButton, ETRemoveActor, "Clone Actor", &pGroup->pCloneButton, ETCloneActor, &pGroup->pUpButton, ETMoveActorUp, &pGroup->pDownButton, ETMoveActorDown, pGroup);
	y += STANDARD_ROW_HEIGHT;

	ui_ExpanderSetHeight(pGroup->pExpander, y);

	eaDestroy(&eaActors);
}

static void ETStrengthCellClickedCB(UIButton *pButton, ETStrengthCell *pCell)
{
	int i;
	EncounterActorProperties** eaActors = NULL;
	
	if(!pCell || !pCell->pDoc)
		return;

	encounterTemplate_FillActorEarray(pCell->pDoc->pTemplate, &eaActors);

	for(i=eaSize(&pCell->pDoc->eaActorGroups)-1; i>=0; --i) {
		bool bOpened = (i < eaSize(&eaActors) && (encounterTemplate_GetActorEnabled(pCell->pDoc->pTemplate, eaActors[i], pCell->iTeamSize, pCell->eDifficulty)) );
		ui_ExpanderSetOpened(pCell->pDoc->eaActorGroups[i]->pExpander, bOpened);
	}
	eaDestroy(&eaActors);
}

static void ETRefreshStrengthCell(ETStrengthCell *pCell, UIExpander* pExpander, bool bMultipleDifficulties, F32 y)
{
	char* buf = NULL;
	F32 fXOffset = 0;
	if(!pCell || !pExpander || !pCell->pDoc)
		return;

	estrCreate(&buf);
	estrPrintf(&buf, "%.2f", encounterTemplate_GetEncounterValue(pCell->pDoc->pTemplate, PARTITION_CLIENT, pCell->iTeamSize, pCell->eDifficulty));

	fXOffset = X_OFFSET_DIFFICULTY_TEAM + ((pCell->iTeamSize-1)*X_OFFSET_DIFFICULTY_CELL);

	if(bMultipleDifficulties)
	{
		if(pCell->pTeamSizeLabel)
			ui_WidgetQueueFreeAndNull(&pCell->pTeamSizeLabel);

		if(!pCell->pTeamSizeButton)
		{
			ui_ButtonCreateAndAddToExpander(pCell->pTeamSizeButton, pExpander, buf, fXOffset, y, ETStrengthCellClickedCB, pCell);
		} else {
			ui_ButtonSetText(pCell->pTeamSizeButton, buf);
			ui_ButtonSetCallback(pCell->pTeamSizeButton, ETStrengthCellClickedCB, pCell);
			ui_WidgetSetPosition(UI_WIDGET(pCell->pTeamSizeButton), fXOffset, y);
		}

	} else {
		if(pCell->pTeamSizeButton)
			ui_WidgetQueueFreeAndNull(&pCell->pTeamSizeButton);

		pCell->pTeamSizeLabel = ETRefreshLabel(pCell->pTeamSizeLabel, buf, NULL, fXOffset, 0, y, pExpander);
	}
}

static F32 ETRefreshStrengthRow(ETStrengthRow *pRow, int iDifficulty, bool bMultipleDifficulties, EncounterTemplateEditDoc *pDoc, F32 y)
{
	int i;

	if(!pRow)
		return y;

	if(bMultipleDifficulties)
	{
		pRow->pDifficultyLabel = ETRefreshLabel(pRow->pDifficultyLabel, StaticDefineIntRevLookupNonNull(EncounterDifficultyEnum, iDifficulty), NULL, X_OFFSET_DIFFICULTY_CELL+X_OFFSET_INDENT, 0, y, pRow->pExpander);
	}
	else
	{
		ui_WidgetQueueFreeAndNull(&pRow->pDifficultyLabel);
	}

	for(i = 0; i < TEAM_MAX_SIZE; i++)
	{
		while(eaSize(&pRow->eaCells) <= i)
		{
			ETStrengthCell *pNewCell = calloc(1,sizeof(ETStrengthCell));
			pNewCell->eDifficulty = iDifficulty;
			pNewCell->pDoc = pDoc;
			pNewCell->iTeamSize = i+1;
			eaPush(&pRow->eaCells, pNewCell);
		}

		ETRefreshStrengthCell(pRow->eaCells[i], pRow->pExpander, bMultipleDifficulties, y);
	}

	y += STANDARD_ROW_HEIGHT;

	while(eaSize(&pRow->eaCells) > TEAM_MAX_SIZE)
	{
		ETFreeStrengthCell(eaPop(&pRow->eaCells));
	}

	return y;
}

static void ETRefreshStrength(EncounterTemplateEditDoc *pDoc)
{
	ETStrengthGroup *pGroup = pDoc->pStrengthGroup;
	UIExpander *pExpander = pGroup ? pGroup->pExpander : NULL;
	EncounterActorProperties **eaActors = NULL;
	int y = 0;
	char* buf = NULL;
	int i;
	int actors1 = 0;
	int actors2 = 0;
	int actors3 = 0;
	int actors4 = 0;
	int actors5 = 0;
	int iNumDifficulties = encounter_GetEncounterDifficultiesCount();
	bool bMultipleDifficulties = (iNumDifficulties > 1);
	
	if(iNumDifficulties == 0)
		iNumDifficulties = 1;
	
	if(!pExpander)
		return;

 	if(!pGroup->pTeamSizeHeader) {
		UILabel *pLabel = NULL;
 
		// Team Size Header
		pGroup->pTeamSizeHeader = ETRefreshLabel(pGroup->pTeamSizeHeader, "Team Size:", "The player's team size.", 0, 0, y, pExpander);
		ETCreateLabel("1", NULL, 12+X_OFFSET_DIFFICULTY_TEAM, y, pExpander);
		ETCreateLabel("2", NULL, 12+X_OFFSET_DIFFICULTY_TEAM + X_OFFSET_DIFFICULTY_CELL, y, pExpander);
		ETCreateLabel("3", NULL, 12+X_OFFSET_DIFFICULTY_TEAM + 2*X_OFFSET_DIFFICULTY_CELL, y, pExpander);
		ETCreateLabel("4", NULL, 12+X_OFFSET_DIFFICULTY_TEAM + 3*X_OFFSET_DIFFICULTY_CELL, y, pExpander);
		ETCreateLabel("5", NULL, 12+X_OFFSET_DIFFICULTY_TEAM + 4*X_OFFSET_DIFFICULTY_CELL, y, pExpander);

		y += STANDARD_ROW_HEIGHT;

	} else {
		y += STANDARD_ROW_HEIGHT;
	}

	if(bMultipleDifficulties)
	{
		ui_WidgetQueueFreeAndNull(&pGroup->pNumActorsHeader);
		ui_WidgetQueueFreeAndNull(&pGroup->pNumActors1);
		ui_WidgetQueueFreeAndNull(&pGroup->pNumActors2);
		ui_WidgetQueueFreeAndNull(&pGroup->pNumActors3);
		ui_WidgetQueueFreeAndNull(&pGroup->pNumActors4);
		ui_WidgetQueueFreeAndNull(&pGroup->pNumActors5);

		pGroup->pStrengthHeader = ETRefreshLabel(pGroup->pStrengthHeader, "Difficulty:", "The detected difficulty.", 0, 0, y, pExpander);
	} else {

		pGroup->pNumActorsHeader = ETRefreshLabel(pGroup->pNumActorsHeader, "Num Actors:", "The number of actors present in the encounter at a given team size.", 0, 0, y, pExpander);

		encounterTemplate_FillActorEarray(pDoc->pTemplate, &eaActors);

		// Number of actors at each team size
		for(i = 0; i < eaSize(&eaActors); i++)
		{
			if(encounterTemplate_GetActorEnabled(pDoc->pTemplate, eaActors[i], 1, 0))
				actors1++;
			if(encounterTemplate_GetActorEnabled(pDoc->pTemplate, eaActors[i], 2, 0))
				actors2++;
			if(encounterTemplate_GetActorEnabled(pDoc->pTemplate, eaActors[i], 3, 0))
				actors3++;
			if(encounterTemplate_GetActorEnabled(pDoc->pTemplate, eaActors[i], 4, 0))
				actors4++;
			if(encounterTemplate_GetActorEnabled(pDoc->pTemplate, eaActors[i], 5, 0))
				actors5++;
		}

		eaDestroy(&eaActors);

		estrCreate(&buf);
		estrPrintf(&buf, "%d", actors1);
		pGroup->pNumActors1 = ETRefreshLabel(pGroup->pNumActors1, strdup(buf), NULL, 12+X_OFFSET_DIFFICULTY_TEAM, 0, y, pExpander);
		estrPrintf(&buf, "%d", actors2);
		pGroup->pNumActors2 = ETRefreshLabel(pGroup->pNumActors2, strdup(buf), NULL, 12+X_OFFSET_DIFFICULTY_TEAM + X_OFFSET_DIFFICULTY_CELL, 0, y, pExpander);
		estrPrintf(&buf, "%d", actors3);
		pGroup->pNumActors3 = ETRefreshLabel(pGroup->pNumActors3, strdup(buf), NULL, 12+X_OFFSET_DIFFICULTY_TEAM + 2*X_OFFSET_DIFFICULTY_CELL, 0, y, pExpander);
		estrPrintf(&buf, "%d", actors4);
		pGroup->pNumActors4 = ETRefreshLabel(pGroup->pNumActors4, strdup(buf), NULL, 12+X_OFFSET_DIFFICULTY_TEAM + 3*X_OFFSET_DIFFICULTY_CELL, 0, y, pExpander);
		estrPrintf(&buf, "%d", actors5);
		pGroup->pNumActors5 = ETRefreshLabel(pGroup->pNumActors5, strdup(buf), NULL, 12+X_OFFSET_DIFFICULTY_TEAM + 4*X_OFFSET_DIFFICULTY_CELL, 0, y, pExpander);

		y += STANDARD_ROW_HEIGHT;

		pGroup->pStrengthHeader = ETRefreshLabel(pGroup->pStrengthHeader, "Strength:", "The strength rating of the encounter given a player's team size.", 0, 0, y, pExpander);
	}

	i = 0;

	for(i=0; i < iNumDifficulties; i++)
	{
		while(eaSize(&pGroup->eaRows) <= i)
		{
			ETStrengthRow *pNewRow = calloc(1,sizeof(ETStrengthRow));
			pNewRow->pExpander = pGroup->pExpander;
			eaPush(&pGroup->eaRows, pNewRow);
		}

		y = ETRefreshStrengthRow(pGroup->eaRows[i], i, bMultipleDifficulties, pDoc, y);
	}

	while(eaSize(&pGroup->eaRows) > iNumDifficulties)
	{
		ETFreeStrengthRow(eaPop(&pGroup->eaRows));
	}

 	if(buf)
		estrDestroy(&buf);
	ui_ExpanderSetHeight(pExpander, y);
	
}

static void ETFreeViewActorsButton(ETViewActorsButton *pButton)
{
	if(pButton)
	{
		if(pButton->pDoc)
			ui_WindowRemoveChild(pButton->pDoc->pMainWindow, pButton->pButton);

		ui_WidgetQueueFreeAndNull(&pButton->pButton);
		free(pButton);
	}
}

static void ETViewActorsTypeChangedCB(UIComboBox *pCombo, int iValue, EncounterTemplateEditDoc *pDoc)
{
	if(pCombo)
		EditorPrefStoreInt(ET_PREF_EDITOR_NAME, ET_PREF_CAT_UI, "ViewActorsType", iValue);
	if(pDoc)
		ETUpdateDisplay(pDoc);
}

static void ETViewActorsButtonClickedCB(UIButton *pButton, ETViewActorsButton *pButtonData)
{
	if(pButtonData && pButtonData->pDoc)
	{
		int i,j;
		EncounterActorProperties** eaActors = NULL;
		bool bOpened = false;
		int iMax = pButtonData->eType == ETViewActorsType_Difficulty ? TEAM_MAX_SIZE : MAX(encounter_GetEncounterDifficultiesCount(),1);

		encounterTemplate_FillActorEarray(pButtonData->pDoc->pTemplate, &eaActors);

		for(i=eaSize(&pButtonData->pDoc->eaActorGroups)-1; i>=0; --i) 
		{
			bOpened = false;
			if(i < eaSize(&eaActors))
			{
				if(pButtonData->eType == ETViewActorsType_TeamSize)
				{
					for(j = 0; j < iMax && !bOpened; j++)
					{
						bOpened = bOpened || encounterTemplate_GetActorEnabled(pButtonData->pDoc->pTemplate, eaActors[i], pButtonData->iIndex+1, j);
					}
				}
				else
				{
					for(j = 0; j < iMax && !bOpened; j++)
					{
						bOpened = bOpened || encounterTemplate_GetActorEnabled(pButtonData->pDoc->pTemplate, eaActors[i], j+1, pButtonData->iIndex);
					}
				}
				ui_ExpanderSetOpened(pButtonData->pDoc->eaActorGroups[i]->pExpander, bOpened);
			}
		}

		eaDestroy(&eaActors);

		ETUpdateDisplay(pButtonData->pDoc);
	}
}


static void ETRefreshActorButtons(EncounterTemplateEditDoc *pDoc, bool bSingleColumnView)
{
	int i;
	ETViewActorsType eType = EditorPrefGetInt(ET_PREF_EDITOR_NAME, ET_PREF_CAT_UI, "ViewActorsType", ETViewActorsType_TeamSize);
	int iNumDifficulties = encounter_GetEncounterDifficultiesCount();
	int iMax = 0;
	F32 fScale = pDoc && pDoc->emDoc.editor ? emGetEditorScale(pDoc->emDoc.editor) : 1;
	F32 xOffset = 0;
	char buf[256];

	if(!pDoc)
		return;

	// Create Fields
	// Show all actors who spawn at 
	// Difficulty/Team Size
	if(iNumDifficulties > 1)
	{
		if(!pDoc->pViewActorsType) {
			pDoc->pViewActorsType = ui_ComboBoxCreateWithEnum(0, 0, 80, ETViewActorsTypeEnum, ETViewActorsTypeChangedCB, pDoc);
			ui_ComboBoxSetSelectedEnum(pDoc->pViewActorsType, eType);
			ui_WindowAddChild(pDoc->pMainWindow, pDoc->pViewActorsType);
		}
	} else {
		if(pDoc->pViewActorsType) {
			ui_WindowRemoveChild(pDoc->pMainWindow, pDoc->pViewActorsType);
			ui_WidgetQueueFreeAndNull(&pDoc->pViewActorsType);
		}
		eType = ETViewActorsType_TeamSize;
	}
	// Value
	if(eType == ETViewActorsType_TeamSize)
	{
		iMax = TEAM_MAX_SIZE;
	}
	else
	{
		iMax = iNumDifficulties;
	}
	while(eaSize(&pDoc->eaViewActorsButtons) < iMax)
	{
		ETViewActorsButton *pNewButton = calloc(1, sizeof(ETViewActorsButton));
		pNewButton->eType = eType;
		pNewButton->iIndex = eaSize(&pDoc->eaViewActorsButtons);
		pNewButton->pDoc = pDoc;
		if(eType == ETViewActorsType_TeamSize) {
			sprintf(buf, "%d", (pNewButton->iIndex+1));
		} else {
			sprintf(buf, "%s", StaticDefineIntRevLookupNonNull(EncounterDifficultyEnum, pNewButton->iIndex));
		}
		pNewButton->pButton = ui_ButtonCreate(buf, 0, 0, ETViewActorsButtonClickedCB, pNewButton);
		ui_WidgetSetWidth(UI_WIDGET(pNewButton->pButton), 20);
		ui_WindowAddChild(pDoc->pMainWindow, pNewButton->pButton);
		eaPush(&pDoc->eaViewActorsButtons, pNewButton);
	}
	while(eaSize(&pDoc->eaViewActorsButtons) > iMax)
	{
		ETFreeViewActorsButton(eaPop(&pDoc->eaViewActorsButtons));
	}
	if(!pDoc->pButtonAll)
	{
		pDoc->pButtonAll = ui_ButtonCreate("All", 130, 0, ETSetSizeAll, pDoc);
		ui_WidgetSetWidth(UI_WIDGET(pDoc->pButtonAll), 30);
		ui_WindowAddChild(pDoc->pMainWindow, pDoc->pButtonAll);
	}


	xOffset = 0;
	// Refresh positions
	if(bSingleColumnView)
	{
		if(pDoc->pViewActorsType)
		{	
			ui_WidgetSetPositionEx(UI_WIDGET(pDoc->pViewActorsType), xOffset, 0, 0, 0, UIBottomLeft);
			xOffset += 90;
		}

		for(i=0; i < eaSize(&pDoc->eaViewActorsButtons); i++)
		{
			pDoc->eaViewActorsButtons[i]->eType = eType;
			ui_WidgetSetPositionEx(UI_WIDGET(pDoc->eaViewActorsButtons[i]->pButton), xOffset, 0, 0, 0, UIBottomLeft);

			// Set text and width
			if(eType == ETViewActorsType_TeamSize) {
				sprintf(buf, "%d", (pDoc->eaViewActorsButtons[i]->iIndex+1));
			} else {
				sprintf(buf, "%s", StaticDefineIntRevLookupNonNull(EncounterDifficultyEnum, pDoc->eaViewActorsButtons[i]->iIndex));
			}
			ui_ButtonSetText(pDoc->eaViewActorsButtons[i]->pButton, buf);
			ui_WidgetSetWidth(UI_WIDGET(pDoc->eaViewActorsButtons[i]->pButton), (15 + ui_StyleFontWidth(ui_WidgetGetFont(UI_WIDGET(pDoc->eaViewActorsButtons[i]->pButton)), fScale, ui_ButtonGetText(pDoc->eaViewActorsButtons[i]->pButton))));

			xOffset += 10 + ui_WidgetGetWidth(UI_WIDGET(pDoc->eaViewActorsButtons[i]->pButton));
		}
		xOffset += 5;
		ui_WidgetSetPositionEx(UI_WIDGET(pDoc->pButtonAll), xOffset, 0, 0, 0, UIBottomLeft);
		xOffset += 60;
		ui_WidgetSetPositionEx(UI_WIDGET(pDoc->pButtonAdd), 0, 0, 0, 0, UIBottomRight);
	}
	else
	{
		ui_WidgetSetPositionEx(UI_WIDGET(pDoc->pButtonAdd), 0, 0, 0, 0, UIBottomRight);
		xOffset += 150;
		ui_WidgetSetPositionEx(UI_WIDGET(pDoc->pButtonAll), xOffset, 0, 0, 0, UIBottomRight);
		xOffset += 45;
		for(i=eaSize(&pDoc->eaViewActorsButtons)-1; i >= 0 ; --i)
		{
			pDoc->eaViewActorsButtons[i]->eType = eType;
			ui_WidgetSetPositionEx(UI_WIDGET(pDoc->eaViewActorsButtons[i]->pButton), xOffset, 0, 0, 0, UIBottomRight);

			// Set text and width
			if(eType == ETViewActorsType_TeamSize) {
				sprintf(buf, "%d", (pDoc->eaViewActorsButtons[i]->iIndex+1));
			} else {
				sprintf(buf, "%s", StaticDefineIntRevLookupNonNull(EncounterDifficultyEnum, pDoc->eaViewActorsButtons[i]->iIndex));
			}
			ui_ButtonSetText(pDoc->eaViewActorsButtons[i]->pButton, buf);
			ui_WidgetSetWidth(UI_WIDGET(pDoc->eaViewActorsButtons[i]->pButton), (15 + ui_StyleFontWidth(ui_WidgetGetFont(UI_WIDGET(pDoc->eaViewActorsButtons[i]->pButton)), fScale, ui_ButtonGetText(pDoc->eaViewActorsButtons[i]->pButton))));

			xOffset += 10 + ui_WidgetGetWidth(UI_WIDGET(pDoc->eaViewActorsButtons[i]->pButton));
		}
		if(pDoc->pViewActorsType)
		{
			ui_WidgetSetPositionEx(UI_WIDGET(pDoc->pViewActorsType), xOffset, 0, 0, 0, UIBottomRight);
			xOffset += 85;
		}
	}
}

static void ETResizeScrollArea(EncounterTemplateEditDoc *pDoc)
{
	F32 ySize = 0;
	int i;

	if(!pDoc || !pDoc->pScrollArea)
		return;

	// Get height of two expander groups
	if(pDoc->pActorExpanderGroup) {
		for(i = 0; i < eaSize(&pDoc->pActorExpanderGroup->childrenInOrder); i++)
		{
			UIWidget *expand = pDoc->pActorExpanderGroup->childrenInOrder[i];
			ySize += expand->height;
			ySize += pDoc->pActorExpanderGroup->spacing;
		}
	}
	if(pDoc->pEncounterExpanderGroup) {
		for(i = 0; i < eaSize(&pDoc->pEncounterExpanderGroup->childrenInOrder); i++)
		{
			UIWidget *expand = pDoc->pEncounterExpanderGroup->childrenInOrder[i];
			ySize += expand->height;
			ySize += pDoc->pEncounterExpanderGroup->spacing;
		}
	}

	// Add Label Height
	if(pDoc->pActorExpanderLabel)
		ySize += LABEL_ROW_HEIGHT;
	if(pDoc->pEncounterExpanderLabel)
		ySize += LABEL_ROW_HEIGHT;

	ui_ScrollAreaSetSize(pDoc->pScrollArea, 0, ySize);
}

static void ETExpanderGroupReflowCB(UIExpanderGroup *pExpander, EncounterTemplateEditDoc *pDoc)
{
	if(pDoc && !pDoc->bIgnoreFieldChanges)
		ETUpdateDisplay(pDoc);
}

static void ETStrengthExpanderCB(UIExpander *pExpander, EncounterTemplateEditDoc *pDoc)
{
	if(pDoc && !pDoc->bIgnoreFieldChanges)
		ETUpdateDisplay(pDoc);
}

static void ETRefreshColumnLayout(EncounterTemplateEditDoc *pDoc, int iColumns, F32 y)
{
	UIWindow *pWin = pDoc ? pDoc->pMainWindow:NULL;
	F32 fBottomY = 0;
	static int iLastColCount = 0;
	int i;

	if(!pWin || !pDoc)
		return;

	if(iColumns != iLastColCount)
	{
		// Set window dimensions
		if(iColumns == 1)
			ui_WindowSetDimensions(pWin, 500, UI_WIDGET(pWin)->height, 500, 400);
		else
			ui_WindowSetDimensions(pWin, 950, UI_WIDGET(pWin)->height, 600, 400);

		iLastColCount = iColumns;
	}

	// Resize doc level fields
	if(pDoc->eaDocFields) {
		for(i = 0; i < eaSize(&pDoc->eaDocFields); i++)
		{
			UIWidget* pWidget = pDoc->eaDocFields[i]->pUIWidget;
			if(iColumns == 1)
				ui_WidgetSetDimensionsEx(pWidget, 1.0, ui_WidgetGetHeight(pWidget), UIUnitPercentage, UIUnitFixed);
			else
				ui_WidgetSetDimensionsEx(pWidget, 0.5, ui_WidgetGetHeight(pWidget), UIUnitPercentage, UIUnitFixed);
		}
	}

	// Strength Area
	if(pDoc->pStrengthGroup && pDoc->pStrengthGroup->pExpander)
	{
		if(iColumns == 1)
		{
			// Left Justified
			ui_WidgetSetPositionEx(UI_WIDGET(pDoc->pStrengthGroup->pExpander),  0-(X_OFFSET_DIFFICULTY_TEAM + 5*X_OFFSET_DIFFICULTY_CELL), y, 1, 0, UITopRight);
			y+=ui_WidgetGetHeight(UI_WIDGET(pDoc->pStrengthGroup->pExpander));
		}
		else
		{
			// Right Justified
			ui_WidgetSetPositionEx(UI_WIDGET(pDoc->pStrengthGroup->pExpander), 0-(X_OFFSET_DIFFICULTY_TEAM + 5*X_OFFSET_DIFFICULTY_CELL), 0, 1, 0, UITopLeft);
			y = MAXF(ui_WidgetGetHeight(UI_WIDGET(pDoc->pStrengthGroup->pExpander)), y);
		}
	}

	// Scroll Area
	if(pDoc->pScrollArea)
	{
		// Determine scroll height
		if(iColumns == 1) {
			ETResizeScrollArea(pDoc);
			
		}
		
		ui_WidgetSetPosition(UI_WIDGET(pDoc->pScrollArea), 0, y);
		if(pDoc->pScrollArea->widget.sb)
		{
			pDoc->pScrollArea->widget.sb->scrollY = iColumns == 1;
		}
	}

	y = 0;

	// Encounter Area
	if(pDoc->pEncounterExpanderLabel) {
		ui_WidgetSetPosition(UI_WIDGET(pDoc->pEncounterExpanderLabel), 0, y);
		y+=LABEL_ROW_HEIGHT;
	}
	if(pDoc->pEncounterExpanderGroup)
	{
		ui_WidgetSetPosition(UI_WIDGET(pDoc->pEncounterExpanderGroup), 0, y);
		if(iColumns == 1)
		{
			ui_WidgetSetDimensionsEx(UI_WIDGET(pDoc->pEncounterExpanderGroup), 1.0, 1.0, UIUnitPercentage, UIUnitPercentage);
			ui_ExpanderGroupSetGrow(pDoc->pEncounterExpanderGroup, true);
			y+=ui_WidgetGetHeight(UI_WIDGET(pDoc->pEncounterExpanderGroup));
		}
		else
		{
			ui_WidgetSetPaddingEx(UI_WIDGET(pDoc->pEncounterExpanderGroup), 0, 0, 0, fBottomY);
			ui_ExpanderGroupSetGrow(pDoc->pEncounterExpanderGroup, false);
			ui_WidgetSetDimensionsEx(UI_WIDGET(pDoc->pEncounterExpanderGroup), 0.45, 1.0, UIUnitPercentage, UIUnitPercentage);
		}
	}

	// Actor Area
	if(pDoc->pActorExpanderLabel) {
		if(iColumns == 1) 
		{
			ui_WidgetSetPosition(UI_WIDGET(pDoc->pActorExpanderLabel), 0, y);
			y+=LABEL_ROW_HEIGHT;
		}
		else
		{
			ui_WidgetSetPositionEx(UI_WIDGET(pDoc->pActorExpanderLabel), 10, 0, 0.5, 0, UITopLeft);
			if(!pDoc->pEncounterExpanderLabel) {
				y+=LABEL_ROW_HEIGHT;
			}
		}
	}
	if(pDoc->pActorExpanderGroup)
	{
		if(iColumns == 1)
		{
			ui_WidgetSetDimensionsEx(UI_WIDGET(pDoc->pActorExpanderGroup),1.0, 1.0, UIUnitPercentage, UIUnitPercentage);
			ui_ExpanderGroupSetGrow(pDoc->pActorExpanderGroup, true);
			ui_WidgetSetPosition(UI_WIDGET(pDoc->pActorExpanderGroup), 0, y);
		}
		else
		{
			ui_ExpanderGroupSetGrow(pDoc->pActorExpanderGroup, false);
			ui_WidgetSetPositionEx(UI_WIDGET(pDoc->pActorExpanderGroup), 10, y, 0.45, 0, UITopLeft);
			ui_WidgetSetDimensionsEx(UI_WIDGET(pDoc->pActorExpanderGroup), 0.55, 1.0, UIUnitPercentage, UIUnitPercentage);
		}
	}

	// Setup buttons
	ETRefreshActorButtons(pDoc, (iColumns == 1));
}


static void ETUpdateDisplay(EncounterTemplateEditDoc *pDoc)
{
	int i;
	EncounterActorProperties **eaActors = NULL;
	EncounterActorProperties **eaOrigActors = NULL;
	U32 iExpanderIdx;

	// Ignore changes while UI refreshes
	pDoc->bIgnoreFieldChanges = true;

	// Refresh Var names
	eaClear(&pDoc->eaVarNames);
	for(i=0; i<zmapInfoGetVariableCount(NULL); ++i) {
		WorldVariableDef *pDef = zmapInfoGetVariableDef(NULL, i);
		if (pDef) {
			eaPush(&pDoc->eaVarNames, (char*)pDef->pcName);
		}
	}

	// Refresh name and one-off label
	if(pDoc->pTemplate->bOneOff)
	{
		if(!pDoc->pOneOffLabel)
		{
			ui_LabelCreateAndAdd(pDoc->pOneOffLabel, pDoc->pMainWindow, "(One-Off)", 0, 0);
			ui_WidgetSetTooltipString(UI_WIDGET(pDoc->pOneOffLabel), "This template was created by cloning an existing template");
			ui_LabelEnableTooltips(pDoc->pOneOffLabel);
		}
		ui_WidgetSetPositionEx(UI_WIDGET(pDoc->pOneOffLabel), -100, 0, 0.5, 0, UITopLeft);		

		ui_WidgetSetPaddingEx(pDoc->pNameField->pUIWidget, 0, 121, 0, 0);
	}
	else
	{
		if(pDoc->pOneOffLabel)
			ui_WidgetQueueFreeAndNull(&pDoc->pOneOffLabel);

		ui_WidgetSetPaddingEx(pDoc->pNameField->pUIWidget, 0, 21, 0, 0);
	}

	// Refresh strength labels
	ETRefreshStrength(pDoc);

	// Refresh the dynamic expanders
	ETRefreshLevel(pDoc);
	iExpanderIdx = ETRefreshDifficulty(pDoc);
	ETRefreshActorShared(pDoc, iExpanderIdx);
	iExpanderIdx++;
	ETRefreshAI(pDoc, iExpanderIdx);
	iExpanderIdx++;
	ETRefreshJobs(pDoc, iExpanderIdx);
	iExpanderIdx++;
	ETRefreshSpawn(pDoc, iExpanderIdx);
	iExpanderIdx++;
	ETRefreshWave(pDoc, iExpanderIdx);
	iExpanderIdx++;
	ETRefreshRewards(pDoc, iExpanderIdx);

	encounterTemplate_FillActorEarray(pDoc->pTemplate, &eaActors);
	encounterTemplate_FillActorEarray(pDoc->pOrigTemplate, &eaOrigActors);

	// Refresh actor expanders
	for(i=0; i<eaSize(&eaActors); ++i) {
		ETActorGroup *pGroup;
		EncounterActorProperties *pActor, *pOrigActor;

		if (i < eaSize(&pDoc->eaActorGroups)) {
			pGroup = pDoc->eaActorGroups[i];
		} else {
			pGroup = calloc(1,sizeof(ETActorGroup));
			pGroup->pDoc = pDoc;
			pGroup->iIndex = i;
			eaPush(&pDoc->eaActorGroups, pGroup);
		}

		pActor = eaActors[i];
		if (eaOrigActors && (i < eaSize(&eaOrigActors))) {
			pOrigActor = eaOrigActors[i];
		} else {
			pOrigActor = NULL;
		}

		ETRefreshActor(pDoc, pGroup, pOrigActor, pActor, i);
	}
	if (pDoc->eaActorGroups) {
		for(i=eaSize(&pDoc->eaActorGroups)-1; i>=eaSize(&eaActors); --i) {
			ETFreeActorGroup(pDoc->eaActorGroups[i]);
			eaRemove(&pDoc->eaActorGroups, i);
		}
	}

	eaDestroy(&eaActors);
	eaDestroy(&eaOrigActors);

	// Refresh Column View
	ETRefreshColumnLayout(pDoc, EditorPrefGetInt(ET_PREF_EDITOR_NAME, ET_PREF_CAT_UI, "Columns", 2), STANDARD_ROW_HEIGHT*4);


	// Refresh doc-level fields
	// Moved to bottom for inheritance UI reasons
	for(i=eaSize(&pDoc->eaDocFields)-1; i>=0; --i) {
		MEFieldSetAndRefreshFromData(pDoc->eaDocFields[i], pDoc->pOrigTemplate, pDoc->pTemplate);
	}

	// Update non-field UI components
	ui_GimmeButtonSetName(pDoc->pFileButton, pDoc->pTemplate->pcName);
	ui_GimmeButtonSetReferent(pDoc->pFileButton, pDoc->pTemplate);
	ui_LabelSetText(pDoc->pFilenameLabel, pDoc->pTemplate->pcFilename);

	// Update saved flag
	pDoc->emDoc.saved = pDoc->pOrigTemplate && (StructCompare(parse_EncounterTemplate, pDoc->pOrigTemplate, pDoc->pTemplate, 0, 0, 0) == 0);

	// Start paying attention to changes again
	pDoc->bIgnoreFieldChanges = false;
}

//---------------------------------------------------------------------------------------------------
// Toolbars
//---------------------------------------------------------------------------------------------------

static void ETFileToolbar_NewCB(UIButton *button, EMEditor *pEditor)
{
	if(pEditor)
		emNewDoc(pEditor->default_type, NULL);
}

static void ETFileToolbar_OpenCB(UIButton *button, EMEditor *pEditor)
{
	if(pEditor && pEditor->pickers && eaSize(&pEditor->pickers) > 0) {
		emPickerShow(pEditor->pickers[0], NULL, true, NULL, NULL);
	}
}

static void ETFileToolbar_SaveCB(UIButton *button, UserData unused)
{
	EMEditorDoc *doc = emGetActiveEditorDoc();
	if (!doc)
		return;
	emQueueFunctionCallStatus(NULL, NULL, emSaveDoc, doc, -1);
}

static EMToolbar* ETFileToolbar_Create(EMEditor *pEditor)
{
	EMToolbar *toolbar;
	UIButton *button;
	F32 x = 0;

	toolbar = emToolbarCreate(10);

	// New
	button = ui_ButtonCreate("New", x, 0, ETFileToolbar_NewCB, pEditor);
	button->widget.height = emToolbarGetHeight(toolbar);
	x += button->widget.width + 5;
	emToolbarAddChild(toolbar, button, true);

	// Open
	button = ui_ButtonCreate("Open", x, 0, ETFileToolbar_OpenCB, pEditor);
	button->widget.height = emToolbarGetHeight(toolbar);
	x += button->widget.width + 5;
	emToolbarAddChild(toolbar, button, true);

	// Save
	button = ui_ButtonCreate("Save", x, 0, ETFileToolbar_SaveCB, NULL);
	button->widget.height = emToolbarGetHeight(toolbar);
	x += button->widget.width + 5;
	emToolbarAddChild(toolbar, button, true);

	emToolbarSetWidth(toolbar,x);

	return toolbar;
}

static void ETViewToolbar_1ColCB(UIButton *button, EncounterTemplateEditDoc *pDoc)
{
	EditorPrefStoreInt(ET_PREF_EDITOR_NAME, ET_PREF_CAT_UI, "Columns", 1);
	if(pDoc)
		ETUpdateDisplay(pDoc);
}

static void ETViewToolbar_2ColCB(UIButton *button, EncounterTemplateEditDoc *pDoc)
{
	EditorPrefStoreInt(ET_PREF_EDITOR_NAME, ET_PREF_CAT_UI, "Columns", 2);
	if(pDoc)
		ETUpdateDisplay(pDoc);
}

static void ETViewToolbar_SetCallbacks(EncounterTemplateEditDoc *pDoc)
{
	if(s_pButton1Col)
	{
		ui_ButtonSetCallback(s_pButton1Col, ETViewToolbar_1ColCB, pDoc);
	}
	if(s_pButton2Col)
	{
		ui_ButtonSetCallback(s_pButton2Col, ETViewToolbar_2ColCB, pDoc);
	}
}

static EMToolbar* ETViewToolbar_Create(void)
{
	EMToolbar* pToolbar;
	F32 x = 0;

	pToolbar = emToolbarCreate(10);

	// Single Column
	s_pButton1Col = ui_ButtonCreate("1-Column View", x, 0, ETViewToolbar_1ColCB, NULL);
	s_pButton1Col->widget.height = emToolbarGetHeight(pToolbar);
	x += s_pButton1Col->widget.width + 5;
	emToolbarAddChild(pToolbar, s_pButton1Col, true);

	// Double Column
	s_pButton2Col = ui_ButtonCreate("2-Column View", x, 0, ETViewToolbar_2ColCB, NULL);
	s_pButton2Col->widget.height = emToolbarGetHeight(pToolbar);
	x += s_pButton2Col->widget.width + 5;
	emToolbarAddChild(pToolbar, s_pButton2Col, true);

	emToolbarSetWidth(pToolbar,x);

	return pToolbar;
}

//---------------------------------------------------------------------------------------------------
// UI Initialization
//---------------------------------------------------------------------------------------------------


static UIWindow *ETInitMainWindow(EncounterTemplateEditDoc *pDoc)
{
	UIWindow *pWin;
	UILabel *pLabel;
	MEField *pField;
	F32 y = 0;
	F32 fBottomY = 0;
	F32 fTopY = 0;
	bool bSingleColumn = EditorPrefGetInt(ET_PREF_EDITOR_NAME, ET_PREF_CAT_UI, "Columns", 2) == 1;

	// Create the window
	pWin = ui_WindowCreate(pDoc->pTemplate->pcName, 15, 50, bSingleColumn?500:950, 600);
	pWin->minW = bSingleColumn?500:600;
	pWin->minH = 400;
	EditorPrefGetWindowPosition(ENCOUNTER_TEMPLATE_EDITOR, "Window Position", "Main", pWin);

	// Strength Area
	pDoc->pStrengthGroup = calloc(1, sizeof(ETStrengthGroup));
	pDoc->pStrengthGroup->pExpander = ui_ExpanderCreate("Encounter Strength", 0);
	ui_WidgetSkin(UI_WIDGET(pDoc->pStrengthGroup->pExpander), gBoldExpanderSkin);
	ui_WidgetSetPositionEx(UI_WIDGET(pDoc->pStrengthGroup->pExpander), 0-(X_OFFSET_DIFFICULTY_TEAM + 5*X_OFFSET_DIFFICULTY_CELL), y, 1, 0, UITopLeft);
	ui_WidgetSetPaddingEx(UI_WIDGET(pDoc->pStrengthGroup->pExpander), 0, 0, 0, fBottomY);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pDoc->pStrengthGroup->pExpander), X_OFFSET_DIFFICULTY_TEAM + 5*X_OFFSET_DIFFICULTY_CELL, 4*STANDARD_ROW_HEIGHT, UIUnitFixed, UIUnitFixed);
	ui_WindowAddChild(pWin, UI_WIDGET(pDoc->pStrengthGroup->pExpander));
	ui_ExpanderSetOpened(pDoc->pStrengthGroup->pExpander, 1);
	ui_ExpanderSetExpandCallback(pDoc->pStrengthGroup->pExpander, ETStrengthExpanderCB, pDoc);

	// Name
	pLabel = ui_LabelCreate("Name", 0, y);
	ui_WindowAddChild(pWin, pLabel);
	pDoc->pNameField = MEFieldCreateSimple(kMEFieldType_TextEntry, pDoc->pOrigTemplate, pDoc->pTemplate, parse_EncounterTemplate, "Name");
	if(pDoc->pTemplate && pDoc->pTemplate->bOneOff)
	{
		ETAddFieldToParent(pDoc->pNameField, UI_WIDGET(pWin), X_OFFSET_CONTROL, y, 0, 0.5, UIUnitPercentage, 121, pDoc);
		ui_LabelCreateAndAdd(pDoc->pOneOffLabel, pWin, "(One-Off)", 0, y);
		ui_WidgetSetPositionEx(UI_WIDGET(pDoc->pOneOffLabel), -100, y, 0.5, 0, UITopLeft);
		ui_WidgetSetTooltipString(UI_WIDGET(pDoc->pOneOffLabel), "This template was created by cloning an existing template");
		ui_LabelEnableTooltips(pDoc->pOneOffLabel);
	}
	else
	{
		ETAddFieldToParent(pDoc->pNameField, UI_WIDGET(pWin), X_OFFSET_CONTROL, y, 0, 0.5, UIUnitPercentage, 21, pDoc);
	}
	MEFieldSetChangeCallback(pDoc->pNameField, ETSetNameCB, pDoc);
	eaPush(&pDoc->eaDocFields, pDoc->pNameField);

	y += STANDARD_ROW_HEIGHT;

	// Scope
	pLabel = ui_LabelCreate("Scope", 0, y);
	ui_WindowAddChild(pWin, pLabel);
	pField = MEFieldCreateSimpleDataProvided(kMEFieldType_TextEntry, pDoc->pOrigTemplate, pDoc->pTemplate, parse_EncounterTemplate, "Scope", NULL, &geaScopes, NULL);
	ETAddFieldToParent(pField, UI_WIDGET(pWin), X_OFFSET_CONTROL, y, 0, 0.5, UIUnitPercentage, 21, pDoc);
	MEFieldSetChangeCallback(pField, ETSetScopeCB, pDoc);
	eaPush(&pDoc->eaDocFields, pField);

	y += STANDARD_ROW_HEIGHT;

	// Parent
	pLabel = ui_LabelCreate("Parent", 0, y);
	ui_WindowAddChild(pWin, pLabel);
	pField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_ValidatedTextEntry, pDoc->pOrigTemplate, pDoc->pTemplate, parse_EncounterTemplate, "ParentEncounter", "EncounterTemplate", "ResourceName");
	ETAddFieldToParent(pField, UI_WIDGET(pWin), X_OFFSET_CONTROL, y, 0, 0.5, UIUnitPercentage, 21, pDoc);
	MEFieldSetChangeCallback(pField, ETSetParentCB, pDoc);
	eaPush(&pDoc->eaDocFields, pField);

	y += STANDARD_ROW_HEIGHT;

	// File Name
	pLabel = ui_LabelCreate("File Name", 0, y);
	ui_WindowAddChild(pWin, pLabel);
	pDoc->pFileButton = ui_GimmeButtonCreate(X_OFFSET_CONTROL, y, "EncounterTemplate", pDoc->pTemplate->pcName, pDoc->pTemplate);
	ui_WindowAddChild(pWin, pDoc->pFileButton);
	pLabel = ui_LabelCreate(pDoc->pTemplate->pcFilename, X_OFFSET_CONTROL+20, y);
	ui_WindowAddChild(pWin, pLabel);
	ui_WidgetSetWidthEx(UI_WIDGET(pLabel), 0.4, UIUnitPercentage);
	ui_WidgetSetPaddingEx(UI_WIDGET(pLabel), 0, 21, 0, 0);
	pDoc->pFilenameLabel = pLabel;

	y += STANDARD_ROW_HEIGHT;

	// Scroll Area
	pDoc->pScrollArea = ui_ScrollAreaCreate(0, y, 0, 0, 0, 0, false, bSingleColumn);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pDoc->pScrollArea), 1.0, 1.0, UIUnitPercentage, UIUnitPercentage);
	ui_WidgetSetPaddingEx(UI_WIDGET(pDoc->pScrollArea), 0, 0, 0, fBottomY + STANDARD_ROW_HEIGHT);
	ui_WindowAddChild(pWin, pDoc->pScrollArea);

	y=0;

	pDoc->pEncounterExpanderLabel = ui_LabelCreate("Encounter Definition", 0, y);
	ui_LabelSetFont(pDoc->pEncounterExpanderLabel, RefSystem_ReferentFromString(g_ui_FontDict, "Default_Bold"));
	ui_ScrollAreaAddChild(pDoc->pScrollArea, pDoc->pEncounterExpanderLabel);

	pDoc->pActorExpanderLabel = ui_LabelCreate("Actor Definitions", 0, y);
	ui_LabelSetFont(pDoc->pActorExpanderLabel, RefSystem_ReferentFromString(g_ui_FontDict, "Default_Bold"));
	ui_WidgetSetPositionEx(UI_WIDGET(pDoc->pActorExpanderLabel), 10, y, 0.4, 0, UITopLeft);
	ui_ScrollAreaAddChild(pDoc->pScrollArea, pDoc->pActorExpanderLabel);

	y += LABEL_ROW_HEIGHT;

	fTopY = y;

	// Encounter Area
	pDoc->pEncounterExpanderGroup = ui_ExpanderGroupCreate();
	ui_WidgetSetPosition(UI_WIDGET(pDoc->pEncounterExpanderGroup), 0, y);
	ui_WidgetSetPaddingEx(UI_WIDGET(pDoc->pEncounterExpanderGroup), 0, 0, 0, fBottomY);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pDoc->pEncounterExpanderGroup), 0.45, 1.0, UIUnitPercentage, UIUnitPercentage);
	UI_SET_STYLE_BORDER_NAME(pDoc->pEncounterExpanderGroup->hBorder, "Default_MiniFrame_Empty");
	ui_ScrollAreaAddChild(pDoc->pScrollArea, pDoc->pEncounterExpanderGroup);

	y = fTopY;

	// Actor Area
	pDoc->pActorExpanderGroup = ui_ExpanderGroupCreate();
	ui_WidgetSetPositionEx(UI_WIDGET(pDoc->pActorExpanderGroup), 10, y, 0.45, 0, UITopLeft);
	ui_WidgetSetPaddingEx(UI_WIDGET(pDoc->pActorExpanderGroup), 0, 0, 0, fBottomY);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pDoc->pActorExpanderGroup), 0.55, 1.0, UIUnitPercentage, UIUnitPercentage);
	UI_SET_STYLE_BORDER_NAME(pDoc->pActorExpanderGroup->hBorder, "Default_MiniFrame_Empty");
	ui_ScrollAreaAddChild(pDoc->pScrollArea, pDoc->pActorExpanderGroup);

	pDoc->pButtonAdd = ui_ButtonCreate("Add Actor", 0, 0, ETAddActor, pDoc);
	ui_WidgetSetPositionEx(UI_WIDGET(pDoc->pButtonAdd), 0, 0, 0, 0, UIBottomRight);
	ui_WidgetSetWidth(UI_WIDGET(pDoc->pButtonAdd), 100);
	ui_WindowAddChild(pWin, pDoc->pButtonAdd);


	ui_ExpanderGroupSetReflowCallback(pDoc->pEncounterExpanderGroup, ETExpanderGroupReflowCB, pDoc);
	ui_ExpanderGroupSetReflowCallback(pDoc->pActorExpanderGroup, ETExpanderGroupReflowCB, pDoc);

	return pWin;
}


static void ETInitDisplay(EMEditor *pEditor, EncounterTemplateEditDoc *pDoc)
{
	// Create the window (ignore field change callbacks during init)
	pDoc->bIgnoreFieldChanges = true;
	pDoc->bIgnoreFilenameChanges = true;
	pDoc->pMainWindow = ETInitMainWindow(pDoc);
	pDoc->bIgnoreFieldChanges = false;
	pDoc->bIgnoreFilenameChanges = false;

	// Show the window
	ui_WindowPresent(pDoc->pMainWindow);

	// Editor Manager needs to be told about the windows used
	pDoc->emDoc.primary_ui_window = pDoc->pMainWindow;
	eaPush(&pDoc->emDoc.ui_windows, pDoc->pMainWindow);

	// Update the rest of the UI
	ETUpdateDisplay(pDoc);
}

static void ETInitToolbarsAndMenus(EMEditor *pEditor)
{
	EMToolbar *pToolbar;

	// File Toolbar
	pToolbar = ETFileToolbar_Create(pEditor);
	eaPush(&pEditor->toolbars, pToolbar);

	// View Toolbar
	pToolbar = ETViewToolbar_Create();
	eaPush(&pEditor->toolbars, pToolbar);

	// File menu
	emMenuItemCreate(pEditor, "et_reverttemplate", "Revert", NULL, NULL, "ET_RevertTemplate");
	emMenuRegister(pEditor, emMenuCreate(pEditor, "File", "et_reverttemplate", NULL));
}


//---------------------------------------------------------------------------------------------------
// Public Interface
//---------------------------------------------------------------------------------------------------

static void ETGotFocusCB(EMEditorDoc* pDoc)
{
	ETViewToolbar_SetCallbacks((EncounterTemplateEditDoc*)pDoc);
}

void ETInitData(EMEditor *pEditor)
{
	if (pEditor && !gInitializedEditor) {
		ETInitToolbarsAndMenus(pEditor);
		pEditor->got_focus_func = ETGotFocusCB;

		// Have Editor Manager handle a lot of change tracking
		emAutoHandleDictionaryStateChange(pEditor, "EncounterTemplate", true, NULL, NULL, NULL, NULL, NULL);

		resGetUniqueScopes(g_hEncounterTemplateDict, &geaScopes);

		aiRequestEditingData();

		gInitializedEditor = true;
	}

	if (!gInitializedEditorData) {
		gBoldExpanderSkin = ui_SkinCreate(NULL);
		SET_HANDLE_FROM_STRING(g_ui_FontDict, "Default_Bold", gBoldExpanderSkin->hNormal);

		// Make sure lists refresh if dictionary changes
		resDictRegisterEventCallback(g_hEncounterTemplateDict, ETContentDictChanged, NULL);
		resDictRegisterEventCallback("FSM", ETDataDictChanged, NULL);
		resDictRegisterEventCallback("CritterDef", ETDataDictChanged, NULL);

		gDefaultLevelProperties = StructCreate(parse_EncounterLevelProperties);

		gDefaultSpawnProperties = StructCreate(parse_EncounterSpawnProperties);

		gDefaultActorSharedProperties = StructCreate(parse_EncounterActorSharedProperties);

		gDefaultAIProperties = StructCreate(parse_EncounterAIProperties);

		gDefaultWaveProperties = StructCreate(parse_EncounterWaveProperties);

		gDefaultDifficultyProperties = StructCreate(parse_EncounterDifficultyProperties);

		gInitializedEditorData = true;
	}
}


static void ETFillEmptyStructs(EncounterTemplate *pTemplate)
{
	EncounterTemplate* pTemplateIter = pTemplate;

	if (!pTemplate->pLevelProperties) {
		pTemplate->pLevelProperties = StructClone(parse_EncounterLevelProperties, gDefaultLevelProperties);
	}

	if (!pTemplate->pSpawnProperties) {
		pTemplate->pSpawnProperties = StructClone(parse_EncounterSpawnProperties, gDefaultSpawnProperties);
	}

	if (!pTemplate->pActorSharedProperties) {
		pTemplate->pActorSharedProperties = StructClone(parse_EncounterActorSharedProperties, gDefaultActorSharedProperties);
	}

	if (!pTemplate->pSharedAIProperties) {
		pTemplate->pSharedAIProperties = StructClone(parse_EncounterAIProperties, gDefaultAIProperties);
	}

	if (!pTemplate->pWaveProperties) {
		pTemplate->pWaveProperties = StructClone(parse_EncounterWaveProperties, gDefaultWaveProperties);
	}

	if (!pTemplate->pDifficultyProperties) {
		pTemplate->pDifficultyProperties = StructClone(parse_EncounterDifficultyProperties, gDefaultDifficultyProperties);
	}

	if (!pTemplate->pRewardProperties) {
		pTemplate->pRewardProperties = StructCreate(parse_EncounterRewardProperties);
	}

	//create skeletons for inherited actors so we know what names are taken and so forth
	//also merge inherited actors into their proper place in the earray
	if (IS_HANDLE_ACTIVE(pTemplate->hParent))
	{
		int i = 0;
		int j = 0;
		EncounterActorProperties** eaAllActors = NULL;
		EncounterActorProperties** eaMerged = NULL;
		bool bCreateSkeleton = true;
		encounterTemplate_FillActorEarray(pTemplate, &eaAllActors);
		for (i = 0; i < eaSize(&eaAllActors); i++)
		{
			bCreateSkeleton = true;
			for (j = 0; j < eaSize(&pTemplate->eaActors); j++)
			{
				if (stricmp(eaAllActors[i]->pcName, pTemplate->eaActors[j]->pcName) == 0)
				{
					bCreateSkeleton = false;
					eaPush(&eaMerged, pTemplate->eaActors[j]);
					eaRemoveFast(&pTemplate->eaActors, j);
					break;
				}
			}
			if (bCreateSkeleton)
			{
				EncounterActorProperties* pActor = StructCreate(parse_EncounterActorProperties);
				pActor->pcName = eaAllActors[i]->pcName;
				eaPush(&eaMerged, pActor);
			}
		}
		eaDestroy(&pTemplate->eaActors);
		pTemplate->eaActors = eaMerged;
	}

	if (!pTemplate->pDifficultyProperties) {
		pTemplate->pDifficultyProperties = StructClone(parse_EncounterDifficultyProperties, gDefaultDifficultyProperties);
	}
}


static void ETTemplatePostOpenFixup(EncounterTemplate *pTemplate)
{
	// Fill in data
	ETFillEmptyStructs(pTemplate);

	// Make editor copy
	langMakeEditorCopy(parse_EncounterTemplate, pTemplate, true);

	// Fix up editor copies of messages
	encounterTemplate_FixupMessages(pTemplate);

	// Clean expressions
	encounterTemplate_Clean(pTemplate);

}


static void ETRemoveUnusedStructs(EncounterTemplate *pTemplate)
{
	int i;

	if (pTemplate->pLevelProperties) {
		if (pTemplate->pLevelProperties->eLevelType != EncounterLevelType_MapVariable) {
			StructFreeStringSafe(&pTemplate->pLevelProperties->pcMapVariable);
		}
		if (pTemplate->pLevelProperties->eLevelType != EncounterLevelType_Specified) { 
			pTemplate->pLevelProperties->iSpecifiedMin = 0;
			pTemplate->pLevelProperties->iSpecifiedMax = 0;
		} else {
			pTemplate->pLevelProperties->iLevelOffsetMin = 0;
			pTemplate->pLevelProperties->iLevelOffsetMax = 0;
		}
		if (pTemplate->pLevelProperties->eClampType != EncounterLevelClampType_MapVariable) {
			StructFreeStringSafe(&pTemplate->pLevelProperties->pcClampMapVariable);
		}
		if (pTemplate->pLevelProperties->eClampType != EncounterLevelClampType_Specified) { 
			pTemplate->pLevelProperties->iClampSpecifiedMin = 0;
			pTemplate->pLevelProperties->iClampSpecifiedMax = 0;
		} else {
			pTemplate->pLevelProperties->iClampOffsetMin = 0;
			pTemplate->pLevelProperties->iClampOffsetMax = 0;
		}
	}
	if (StructCompare(parse_EncounterLevelProperties, pTemplate->pLevelProperties, gDefaultLevelProperties, 0,0,0) == 0) {
		StructDestroySafe(parse_EncounterLevelProperties, &pTemplate->pLevelProperties);
	}

	if (pTemplate->pActorSharedProperties) {
		if (pTemplate->pActorSharedProperties->eCritterGroupType != EncounterSharedCritterGroupSource_Specified) {
			REMOVE_HANDLE(pTemplate->pActorSharedProperties->hCritterGroup);
		}
		if (pTemplate->pActorSharedProperties->eCritterGroupType != EncounterSharedCritterGroupSource_MapVariable) {
			StructFreeStringSafe(&pTemplate->pActorSharedProperties->pcCritterGroupMapVar);
		}

		if (pTemplate->pActorSharedProperties->eFactionType != EncounterCritterOverrideType_Specified) {
			REMOVE_HANDLE(pTemplate->pActorSharedProperties->hFaction);
			pTemplate->pActorSharedProperties->iGangID = 0;
		}
	}
	if (StructCompare(parse_EncounterActorSharedProperties, pTemplate->pActorSharedProperties, gDefaultActorSharedProperties, 0,0,0) == 0) {
		StructDestroySafe(parse_EncounterActorSharedProperties, &pTemplate->pActorSharedProperties);
	} 

	if (pTemplate->pSharedAIProperties) {
		if (pTemplate->pSharedAIProperties->eFSMType != EncounterCritterOverrideType_Specified) {
			REMOVE_HANDLE(pTemplate->pSharedAIProperties->hFSM);
		}
	}
	if (StructCompare(parse_EncounterAIProperties, pTemplate->pSharedAIProperties, gDefaultAIProperties, 0,0,0) == 0) {
		StructDestroySafe(parse_EncounterAIProperties, &pTemplate->pSharedAIProperties);
	}

	if (pTemplate->pSpawnProperties) {
		if (pTemplate->pSpawnProperties->eSpawnAnimType != EncounterSpawnAnimType_Specified) {
			StructFreeStringSafe(&pTemplate->pSpawnProperties->pcSpawnAnim);
			pTemplate->pSpawnProperties->fSpawnLockdownTime = 0;
		}
	}
	if (StructCompare(parse_EncounterSpawnProperties, pTemplate->pSpawnProperties, gDefaultSpawnProperties, 0,0,0) == 0) {
		StructDestroySafe(parse_EncounterSpawnProperties, &pTemplate->pSpawnProperties);
	}

	if (StructCompare(parse_EncounterWaveProperties, pTemplate->pWaveProperties, gDefaultWaveProperties, 0,0,0) == 0) {
		StructDestroySafe(parse_EncounterWaveProperties, &pTemplate->pWaveProperties);
	}

	if (StructCompare(parse_EncounterDifficultyProperties, pTemplate->pDifficultyProperties, gDefaultDifficultyProperties, 0,0,0) == 0) {
		StructDestroySafe(parse_EncounterDifficultyProperties, &pTemplate->pDifficultyProperties);
	}

	if (pTemplate->pRewardProperties &&
		pTemplate->pRewardProperties->eRewardType == kWorldEncounterRewardType_DefaultRewards &&
		pTemplate->pRewardProperties->eRewardLevelType == kWorldEncounterRewardLevelType_DefaultLevel) {
		StructDestroySafe(parse_EncounterRewardProperties, &pTemplate->pRewardProperties);
	}

	for(i=eaSize(&pTemplate->eaActors)-1; i>=0; --i) {
		EncounterActorProperties *pActor = pTemplate->eaActors[i];
		if (pActor->critterProps.eCritterType != ActorCritterType_CritterDef) {
			REMOVE_HANDLE(pActor->critterProps.hCritterDef);
		}
		if (pActor->critterProps.eCritterType != ActorCritterType_CritterGroup) {
			REMOVE_HANDLE(pActor->critterProps.hCritterGroup);
		}
		if ((pActor->critterProps.eCritterType != ActorCritterType_MapVariableDef) && (pActor->critterProps.eCritterType != ActorCritterType_MapVariableGroup)) {
			StructFreeStringSafe(&pActor->critterProps.pcCritterMapVariable);
		}
		if ((pActor->critterProps.eCritterType != ActorCritterType_FromTemplate) && (pActor->critterProps.eCritterType != ActorCritterType_CritterGroup) && (pActor->critterProps.eCritterType != ActorCritterType_MapVariableGroup) &&
			pActor->critterProps.eCritterType != ActorCritterType_NemesisMinion &&
			pActor->critterProps.eCritterType != ActorCritterType_NemesisMinionNormal &&
			pActor->critterProps.eCritterType != ActorCritterType_NemesisMinionTeam &&
			pActor->critterProps.eCritterType != ActorCritterType_NemesisMinionForLeader )
		{
			pActor->critterProps.pcRank = NULL;
			if( (pActor->critterProps.eCritterType != ActorCritterType_CritterDef) &&
				(pActor->critterProps.eCritterType != ActorCritterType_MapVariableDef) &&
				pActor->critterProps.eCritterType != ActorCritterType_NemesisMinion &&
				pActor->critterProps.eCritterType != ActorCritterType_NemesisMinionNormal &&
				pActor->critterProps.eCritterType != ActorCritterType_NemesisMinionTeam &&
				pActor->critterProps.eCritterType != ActorCritterType_NemesisMinionForLeader )
			{
				pActor->critterProps.pcSubRank = NULL;
			}
		}
		if (pActor->critterProps.eCritterType != ActorCritterType_PetContactList) {
			REMOVE_HANDLE(pActor->miscProps.hPetContactList);
		}

		if (pActor->nameProps.eDisplayNameType != EncounterCritterOverrideType_Specified) {
			StructDestroySafe(parse_Message, &pActor->nameProps.displayNameMsg.pEditorCopy);
		}
		if (pActor->nameProps.eDisplaySubNameType != EncounterCritterOverrideType_Specified) {
			StructDestroySafe(parse_Message, &pActor->nameProps.displaySubNameMsg.pEditorCopy);
		}

		if (pActor->factionProps.eFactionType != EncounterTemplateOverrideType_Specified) {
			REMOVE_HANDLE(pActor->factionProps.hFaction);
			pActor->factionProps.iGangID = 0;
		}

		if (pActor->spawnInfoProps.eSpawnAnimType != EncounterTemplateOverrideType_Specified) {
			StructFreeStringSafe(&pActor->spawnInfoProps.pcSpawnAnim);
			pActor->spawnInfoProps.fSpawnLockdownTime = 0;
		}

		if (pActor->fsmProps.eFSMType != EncounterTemplateOverrideType_Specified) {
			REMOVE_HANDLE(pActor->fsmProps.hFSM);
		}
		pActor->bOverrideInteractionInfo = false;//no longer used
		//if this actor doesn't change anything from the parent version, it's useless
		if (IS_HANDLE_ACTIVE(pTemplate->hParent) && !(pActor->bOverrideCritterSpawnInfo ||
			pActor->bOverrideCritterType ||
			pActor->bOverrideDisplayName ||
			pActor->bOverrideDisplaySubName ||
			pActor->bOverrideFaction ||
			pActor->bOverrideFSMInfo ||
			pActor->bOverrideSpawnConditions ||
			pActor->bOverrideMisc) &&
			eaSize(&pActor->eaVariableDefs) <= 0)
		{
			StructDestroySafe(parse_EncounterActorProperties, &pActor);
			eaRemove(&pTemplate->eaActors, i);
		}
	}
}


static void ETTemplatePreSaveFixup(EncounterTemplate *pTemplate)
{
	// Clean up unused data
	ETRemoveUnusedStructs(pTemplate);

	// Clean up messages
	encounterTemplate_FixupMessages(pTemplate);

	// Clean Expressions
	encounterTemplate_Clean(pTemplate);
}


static EncounterTemplateEditDoc *ETInitDoc(EncounterTemplate *pTemplate, bool bCreated)
{
	EncounterTemplateEditDoc *pDoc;
	char nameBuf[260];

	// Initialize the structure
	pDoc = (EncounterTemplateEditDoc*)calloc(1,sizeof(EncounterTemplateEditDoc));

	// Fill in the map description data
	if (bCreated) {
		pDoc->pTemplate = StructCreate(parse_EncounterTemplate);
		assert(pDoc->pTemplate);
		emMakeUniqueDocName(&pDoc->emDoc, "New_Encounter", "EncounterTemplate", "EncounterTemplate");
		pDoc->pTemplate->pcName = allocAddString(pDoc->emDoc.doc_name);
		sprintf(nameBuf, "defs/encounters2/%s.encounter2", pDoc->pTemplate->pcName);
		pDoc->pTemplate->pcFilename = allocAddString(nameBuf);
		ETTemplatePostOpenFixup(pDoc->pTemplate);
	} else {
		pDoc->pTemplate = StructClone(parse_EncounterTemplate, pTemplate);
		assert(pDoc->pTemplate);
		ETTemplatePostOpenFixup(pDoc->pTemplate);
		pDoc->pOrigTemplate = StructClone(parse_EncounterTemplate, pDoc->pTemplate);
	}

	// Set view toolbar callbacks
	ETViewToolbar_SetCallbacks(pDoc);

	// Set up the undo stack
	pDoc->emDoc.edit_undo_stack = EditUndoStackCreate();
	EditUndoSetContext(pDoc->emDoc.edit_undo_stack, pDoc);
	pDoc->pNextUndoTemplate = StructClone(parse_EncounterTemplate, pDoc->pTemplate);

	return pDoc;
}


EncounterTemplateEditDoc *ETOpenTemplate(EMEditor *pEditor, char *pcName)
{
	EncounterTemplateEditDoc *pDoc = NULL;
	EncounterTemplate *pTemplate = NULL;
	bool bCreated = false;

	if (pcName && resIsEditingVersionAvailable(g_hEncounterTemplateDict, pcName)) {
		// Simply open the object since it is in the dictionary
		pTemplate = RefSystem_ReferentFromString(g_hEncounterTemplateDict, pcName);
	} else if (pcName) {
		// Wait for object to show up so we can open it
		resSetDictionaryEditMode(g_hEncounterTemplateDict, true);
		resSetDictionaryEditMode("PetContactList", true);
		emSetResourceState(pEditor, pcName, EMRES_STATE_OPENING);
		resRequestOpenResource(g_hEncounterTemplateDict, pcName);
	} else {
		// Create a new object since it is not in the dictionary
		bCreated = true;
	}

	if (pTemplate || bCreated) {
		pDoc = ETInitDoc(pTemplate, bCreated);
		ETInitDisplay(pEditor, pDoc);
		ETSetWidgetStatesFromInheritance(NULL, pDoc);
		resFixFilename(g_hEncounterTemplateDict, pDoc->pTemplate->pcName, pDoc->pTemplate);
	}

	return pDoc;
}


void ETRevertTemplate(EncounterTemplateEditDoc *pDoc)
{
	EncounterTemplate *pTemplate;

	if (!pDoc->emDoc.orig_doc_name[0]) {
		// Cannot revert if no original
		return;
	}

	pTemplate = RefSystem_ReferentFromString(g_hEncounterTemplateDict, pDoc->emDoc.orig_doc_name);
	if (pTemplate) {
		// Revert the map description
		StructDestroy(parse_EncounterTemplate, pDoc->pTemplate);
		StructDestroy(parse_EncounterTemplate, pDoc->pOrigTemplate);
		pDoc->pTemplate = StructClone(parse_EncounterTemplate, pTemplate);
		ETTemplatePostOpenFixup(pDoc->pTemplate);
		pDoc->pOrigTemplate = StructClone(parse_EncounterTemplate, pDoc->pTemplate);

		// Clear the undo stack on revert
		EditUndoStackClear(pDoc->emDoc.edit_undo_stack);
		StructDestroy(parse_EncounterTemplate, pDoc->pNextUndoTemplate);
		pDoc->pNextUndoTemplate = StructClone(parse_EncounterTemplate, pDoc->pTemplate);

		// Refresh the UI
		pDoc->bIgnoreFieldChanges = true;
		pDoc->bIgnoreFilenameChanges = true;
		ETUpdateDisplay(pDoc);
		pDoc->bIgnoreFieldChanges = false;
		pDoc->bIgnoreFilenameChanges = false;
	} 
}


void ETCloseTemplate(EncounterTemplateEditDoc *pDoc)
{
	int i;

	pDoc->bIgnoreFieldChanges = true;
	pDoc->bIgnoreFilenameChanges = true;

	

	// Free doc fields
	eaDestroyEx(&pDoc->eaDocFields, MEFieldDestroy);

	// Free name and one-off labels/fields
	if(pDoc->pOneOffLabel)
		ui_WidgetQueueFreeAndNull(&pDoc->pOneOffLabel)

	// Free groups
	ETFreeStrengthGroup(pDoc->pStrengthGroup);
	ETFreeLevelGroup(pDoc->pLevelGroup);
	ETFreeDifficultyGroup(pDoc->pDifficultyGroup);
	ETFreeSpawnGroup(pDoc->pSpawnGroup);
	ETFreeActorSharedGroup(pDoc->pActorSharedGroup);
	ETFreeAIGroup(pDoc->pAIGroup);
	ETFreeJobGroup(pDoc->pJobGroup);
	ETFreeWaveGroup(pDoc->pWaveGroup);
	ETFreeRewardsGroup(pDoc->pRewardsGroup);
	for(i=eaSize(&pDoc->eaActorGroups)-1; i>=0; --i) {
		ETFreeActorGroup(pDoc->eaActorGroups[i]);
	}
	eaDestroy(&pDoc->eaActorGroups);

	// Free the objects
	StructDestroy(parse_EncounterTemplate, pDoc->pTemplate);
	if (pDoc->pOrigTemplate) {
		StructDestroy(parse_EncounterTemplate, pDoc->pOrigTemplate);
	}
	StructDestroy(parse_EncounterTemplate, pDoc->pNextUndoTemplate);

	// Free data
	eaDestroy(&pDoc->eaVarNames);

	// Free buttons
	ui_WidgetQueueFree(UI_WIDGET(pDoc->pButtonAdd));
	ui_WidgetQueueFree(UI_WIDGET(pDoc->pButtonAll));
	for(i=eaSize(&pDoc->eaViewActorsButtons)-1; i >=0; --i)
	{
		ETFreeViewActorsButton(eaRemove(&pDoc->eaViewActorsButtons, i));
	}
	ui_WidgetQueueFree(UI_WIDGET(pDoc->pViewActorsType));

	// Free Labels
	ui_WidgetQueueFree(UI_WIDGET(pDoc->pFilenameLabel));
	ui_WidgetQueueFree(UI_WIDGET(pDoc->pActorExpanderLabel));
	ui_WidgetQueueFree(UI_WIDGET(pDoc->pEncounterExpanderLabel));

	// Free Expander Groups
	ui_WidgetQueueFree(UI_WIDGET(pDoc->pEncounterExpanderGroup));
	ui_WidgetQueueFree(UI_WIDGET(pDoc->pActorExpanderGroup));

	// Close the window
	ui_WindowHide(pDoc->pMainWindow);
	ui_WidgetQueueFree(UI_WIDGET(pDoc->pMainWindow));

	pDoc->bIgnoreFieldChanges = false;
	pDoc->bIgnoreFilenameChanges = false;

}


EMTaskStatus ETSaveTemplate(EncounterTemplateEditDoc* pDoc, bool bSaveAsNew)
{
	EMTaskStatus status;
	const char *pcName;
	EncounterTemplate *pTemplateCopy;

	// Deal with state changes
	pcName = pDoc->pTemplate->pcName;
	if (emHandleSaveResourceState(pDoc->emDoc.editor, pcName, &status)) {
		return status;
	}

	// Do cleanup before validation
	pTemplateCopy = StructClone(parse_EncounterTemplate, pDoc->pTemplate);
	ETTemplatePreSaveFixup(pTemplateCopy);
	ETTemplateChanged(pDoc, false);

	// Perform validation

	// Do the save (which will free the copy)
	status = emSmartSaveDoc(&pDoc->emDoc, pTemplateCopy, pDoc->pOrigTemplate, bSaveAsNew);

	return status;
}

#include "AutoGen/EncounterTemplateEditor_h_ast.c"

#endif

