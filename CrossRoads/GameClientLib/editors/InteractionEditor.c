/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef NO_EDITORS

#include "ChoiceTable_common.h"
#include "EditorPrefs.h"
#include "Expression.h"
#include "gameaction_common.h"
#include "GameActionEditor.h"
#include "GameEditorShared.h"
#include "interaction_common.h"
#include "InteractionEditor.h"
#include "MultiEditField.h"
#include "StringCache.h"
#include "UIGimmeButton.h"
#include "UISeparator.h"
#include "UISkin.h"
#include "soundLib.h"
#include "wlEncounter.h"
#include "wlGroupPropertyStructs.h"
#include "WorldGrid.h"
#include "WorldEditorAttributesHelpers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););


//-----------------------------------------------------------------------------------
// Global Tooltip strings
//-----------------------------------------------------------------------------------

#define INTERACT_INTERACTCOND_TOOLTIP "When true, the node can be interacted with.  Otherwise it won't allow interaction."
#define INTERACT_ATTEMPTABLECOND_TOOLTIP "When true, the interaction is attemptable as usual and the Success Expr will determine actual success of the attempt.  If false, a different interact FX will be shown, the mouseover tip will be different and interacting will short-circuit (no anim, no timer bar) and fail."
#define INTERACT_SUCCESSCOND_TOOLTIP "When true, the interaction is successful.  Otherwise it will fail."

//-----------------------------------------------------------------------------------
// Global Data
//-----------------------------------------------------------------------------------

static bool gInitializedEditor = false;
static bool gInitializedEditorData = false;
static bool gIndexChanged = false;

static char **geaScopes = NULL;
static char **s_eaClassNames = NULL;
static char **s_eaClassNames2 = NULL;
static char **s_eaCategoryNames = NULL;

extern EMEditor *s_InteractionEditor;

static InteractionEditDoc *gEmbeddedDoc = NULL;

static UISkin *gBoldExpanderSkin;


//---------------------------------------------------------------------------------------------------
// Function Prototypes and type definitions
//---------------------------------------------------------------------------------------------------

#define X_OFFSET_BASE    15
#define X_OFFSET_CONTROL 125

#define X_OFFSET_INTERACT_BASE2    20
#define X_OFFSET_INTERACT_CONTROL  135
#define X_OFFSET_INTERACT_CONTROL2 155
#define X_OFFSET_INTERACT_CONTROL3 175

#define STANDARD_ROW_HEIGHT	26
#define LABEL_ROW_HEIGHT	20
#define SEPARATOR_HEIGHT	11

#define IECreateLabel(pcText,pcTooltip,x,y,pExpander)  IERefreshLabel(NULL, (pcText), (pcTooltip), (x), 0, (y), (pExpander))

static void IEFieldChangedCB(MEField *pField, bool bFinished, InteractionEditDoc *pDoc);
static bool IEFieldPreChangeCB(MEField *pField, bool bFinished, InteractionEditDoc *pDoc);
static void IEInteractionDefChanged(InteractionEditDoc *pDoc, bool bUndoable);
static void IEDefPreSaveFixup(InteractionDef *pDef);
static void IEUpdateDisplay(InteractionEditDoc *pDoc);


//---------------------------------------------------------------------------------------------------
// Data Manipulation
//---------------------------------------------------------------------------------------------------

static int IEStringCompare(const char** left, const char** right)
{
	return stricmp(*left,*right);
}


static void IEStructFreeString(char *pcStructString)
{
	StructFreeString(pcStructString);
}


static void IEInteractionDefUndoCB(InteractionEditDoc *pDoc, IEUndoData *pData)
{
	// Put the undo def into the editor
	StructDestroy(parse_InteractionDef, pDoc->pDef);
	pDoc->pDef = StructClone(parse_InteractionDef, pData->pPreDef);
	if (pDoc->pNextUndoDef) {
		StructDestroy(parse_InteractionDef, pDoc->pNextUndoDef);
	}
	pDoc->pNextUndoDef = StructClone(parse_InteractionDef, pDoc->pDef);

	// Update the UI
	IEInteractionDefChanged(pDoc, false);
}


static void IEInteractionDefRedoCB(InteractionEditDoc *pDoc, IEUndoData *pData)
{
	// Put the undo def into the editor
	StructDestroy(parse_InteractionDef, pDoc->pDef);
	pDoc->pDef = StructClone(parse_InteractionDef, pData->pPostDef);
	if (pDoc->pNextUndoDef) {
		StructDestroy(parse_InteractionDef, pDoc->pNextUndoDef);
	}
	pDoc->pNextUndoDef= StructClone(parse_InteractionDef, pDoc->pDef);

	// Update the UI
	IEInteractionDefChanged(pDoc, false);
}


static void IEInteractionDefUndoFreeCB(InteractionEditDoc *pDoc, IEUndoData *pData)
{
	// Free the memory
	StructDestroy(parse_InteractionDef, pData->pPreDef);
	StructDestroy(parse_InteractionDef, pData->pPostDef);
	free(pData);
}


static void IEIndexChangedCB(void *unused)
{
	if (gIndexChanged) {
		gIndexChanged = false;
		resGetUniqueScopes(g_InteractionDefDictionary, &geaScopes);
	}
}


static void IEContentDictChanged(enumResourceEventType eType, const char *pDictName, const char *pcName, Referent pReferent, void *pUserData)
{
	if ((eType == RESEVENT_INDEX_MODIFIED) && !gIndexChanged) {
		gIndexChanged = true;
		emQueueFunctionCall(IEIndexChangedCB, NULL);
	}
}


//---------------------------------------------------------------------------------------------------
// Interaction Property Editing UI
//---------------------------------------------------------------------------------------------------

void FreeInteractionPropertiesGroup(InteractionPropertiesGroup *pGroup)
{
	int i;

	// Base
	ui_WidgetQueueFree((UIWidget*)pGroup->pGeneralSectionLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pClassLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pDefClassLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pDefClassValueLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pInteractButton);
	ui_WidgetQueueFree((UIWidget*)pGroup->pInteractCondLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pInteractCondValueLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pSuccessCondLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pSuccessCondValueLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pAttemptableCondLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pAttemptableCondValueLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pVisibleButton);
	ui_WidgetQueueFree((UIWidget*)pGroup->pVisibleExprLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pVisibleExprValueLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pCategoryButton);
	ui_WidgetQueueFree((UIWidget*)pGroup->pCategoryLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pCategoryValueLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pPriorityLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pPriorityValueLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pAutoExecLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pAutoExecValueLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pDisablePowersInterruptLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pDisablePowersInterruptValueLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pAllowDuringCombatLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pAllowDuringCombatValueLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pExclusiveLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pExclusiveButton);
	ui_WidgetQueueFree((UIWidget*)pGroup->pExclusiveValueLabel);
	// Time
	ui_WidgetQueueFree((UIWidget*)pGroup->pTimeSectionLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pTimeButton);
	ui_WidgetQueueFree((UIWidget*)pGroup->pUseTimeLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pUseTimeValueLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pActiveTimeLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pActiveTimeValueLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pNoRespawnLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pNoRespawnValueLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pCooldownTimeLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pCooldownTimeValueLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pCustomCooldownLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pCustomCooldownValueLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pDynamicCooldownLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pDynamicCooldownValueLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pTeamUsableWhenActiveLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pTeamUsableWhenActiveValueLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pHideDuringCooldownLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pHideDuringCooldownValueLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pInterruptOnPowerLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pInterruptOnDamageLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pInterruptOnMoveLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pInterruptLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pInterruptValueLabel);
	// Text
	ui_WidgetQueueFree((UIWidget*)pGroup->pTextButton);
	ui_WidgetQueueFree((UIWidget*)pGroup->pUsabilityTextLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pUsabilityTextValueLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pInteractTextLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pInteractTextValueLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pDetailTextLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pDetailTextValueLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pDetailTextureLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pDetailTextureValueSprite);
	ui_WidgetQueueFree((UIWidget*)pGroup->pSuccessTextLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pSuccessTextValueLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pFailureTextLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pFailureTextValueLabel);
	// Reward
	ui_WidgetQueueFree((UIWidget*)pGroup->pRewardSectionLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pRewardButton);
	ui_WidgetQueueFree((UIWidget*)pGroup->pRewardTableLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pRewardTableValueLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pRewardLevelTypeLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pRewardLevelTypeValueLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pRewardLevelLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pRewardLevelValueLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pRewardLevelMapVarLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pRewardLevelMapVarValueLabel);
	// Animation
	ui_WidgetQueueFree((UIWidget*)pGroup->pAnimationButton);
	ui_WidgetQueueFree((UIWidget*)pGroup->pAnimLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pAnimValueLabel);
	// Action
	ui_WidgetQueueFree((UIWidget*)pGroup->pActionSectionLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pActionButton);
	ui_WidgetQueueFree((UIWidget*)pGroup->pAttemptActionLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pAttemptActionValueLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pSuccessActionLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pSuccessActionValueLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pFailureActionLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pFailureActionValueLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pInterruptActionLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pInterruptActionValueLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pNoLongerActiveActionLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pNoLongerActiveActionValueLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pCooldownActionLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pCooldownActionValueLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pSuccessGameActionsLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pSuccessGameActionsValueLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pSuccessGameActionsButton);
	ui_WidgetQueueFree((UIWidget*)pGroup->pFailureGameActionsLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pFailureGameActionsValueLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pFailureGameActionsButton);
	// Contact
	ui_WidgetQueueFree((UIWidget*)pGroup->pContactSectionLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pContactButton);
	ui_WidgetQueueFree((UIWidget*)pGroup->pContactLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pContactValueLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pContactDialogLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pContactDialogValueLabel);
	// Crafting
	ui_WidgetQueueFree((UIWidget*)pGroup->pCraftingSectionLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pCraftSkillLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pCraftSkillValueLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pCraftMaxSkillLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pCraftMaxSkillValueLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pCraftReward1Label);
	ui_WidgetQueueFree((UIWidget*)pGroup->pCraftReward1ValueLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pCraftReward2Label);
	ui_WidgetQueueFree((UIWidget*)pGroup->pCraftReward2ValueLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pCraftReward3Label);
	ui_WidgetQueueFree((UIWidget*)pGroup->pCraftReward3ValueLabel);
	// Destructible
	ui_WidgetQueueFree((UIWidget*)pGroup->pDestructibeSectionLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pCritterLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pCritterOverrideLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pCritterLevelLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pDisplayNameLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pEntityNameLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pRespawnTimeLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pDeathPowerLabel);
	// Door
	ui_WidgetQueueFree((UIWidget*)pGroup->pDoorSectionLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pDoorTypeLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pDoorTypeValueLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pDoorIDLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pDoorIDValueLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pDoorQueueLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pDoorQueueValueLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pPerPlayerDoorLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pPerPlayerDoorValueLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pSinglePlayerDoorLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pSinglePlayerDoorValueLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pAllowJoinTeamAtDoorLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pAllowJoinTeamAtDoorValueLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pCollectDestStatusLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pCollectDestStatusValueLabel);
    ui_WidgetQueueFree((UIWidget*)pGroup->pDestinationSameOwnerLabel);
    ui_WidgetQueueFree((UIWidget*)pGroup->pDestinationSameOwnerValueLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pDoorTransitionLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pDoorTransitionValueLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pDefDoor1Label);
	ui_WidgetQueueFree((UIWidget*)pGroup->pDefDoor1ValueLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pDefDoor2Label);
	ui_WidgetQueueFree((UIWidget*)pGroup->pDefDoor2ValueLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pVarAddButton);
	// FromDefinition
	ui_WidgetQueueFree((UIWidget*)pGroup->pFromDefSectionLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pInteractDefLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pShowDefValues);
	// Sound
	ui_WidgetQueueFreeAndNull(&pGroup->pSoundSectionLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pAttemptSoundLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pAttemptSoundValueLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pSuccessSoundLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pSuccessSoundValueLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pFailureSoundLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pFailureSoundValueLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pInterruptSoundLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pInterruptSoundValueLabel);
	
	// Base
	MEFieldSafeDestroy(&pGroup->pClassField);
	MEFieldSafeDestroy(&pGroup->pInteractCondField);
	MEFieldSafeDestroy(&pGroup->pSuccessCondField);
	MEFieldSafeDestroy(&pGroup->pAttemptableCondField);
	MEFieldSafeDestroy(&pGroup->pVisibleExprField);
	MEFieldSafeDestroy(&pGroup->pCategoryField);
	MEFieldSafeDestroy(&pGroup->pPriorityField);
	MEFieldSafeDestroy(&pGroup->pInteractionCategoryField);
	MEFieldSafeDestroy(&pGroup->pAutoExecField);
	MEFieldSafeDestroy(&pGroup->pDisablePowersInterruptField);
	// Time
	MEFieldSafeDestroy(&pGroup->pUseTimeField);
	MEFieldSafeDestroy(&pGroup->pActiveTimeField);
	MEFieldSafeDestroy(&pGroup->pCooldownTimeField);
	MEFieldSafeDestroy(&pGroup->pCustomCooldownField);
	MEFieldSafeDestroy(&pGroup->pDynamicCooldownField);
	MEFieldSafeDestroy(&pGroup->pTeamUsableWhenActiveField);
	MEFieldSafeDestroy(&pGroup->pHideDuringCooldownField);
	MEFieldSafeDestroy(&pGroup->pInterruptOnPowerField);
	MEFieldSafeDestroy(&pGroup->pInterruptOnDamageField);
	MEFieldSafeDestroy(&pGroup->pInterruptOnMoveField);
	MEFieldSafeDestroy(&pGroup->pNoRespawnField);
	// Text
	MEFieldSafeDestroy(&pGroup->pUsabilityTextField);
	MEFieldSafeDestroy(&pGroup->pInteractTextField);
	MEFieldSafeDestroy(&pGroup->pDetailTextField);
	MEFieldSafeDestroy(&pGroup->pDetailTextureField);
	MEFieldSafeDestroy(&pGroup->pSuccessTextField);
	MEFieldSafeDestroy(&pGroup->pFailureTextField);
	// Animation
	MEFieldSafeDestroy(&pGroup->pAnimField);
	// Action
	MEFieldSafeDestroy(&pGroup->pAttemptActionField);
	MEFieldSafeDestroy(&pGroup->pSuccessActionField);
	MEFieldSafeDestroy(&pGroup->pFailureActionField);
	MEFieldSafeDestroy(&pGroup->pInterruptActionField);
	MEFieldSafeDestroy(&pGroup->pNoLongerActiveActionField);
	MEFieldSafeDestroy(&pGroup->pCooldownActionField);
	// Reward
	MEFieldSafeDestroy(&pGroup->pRewardTableField);
	MEFieldSafeDestroy(&pGroup->pRewardLevelTypeField);
	MEFieldSafeDestroy(&pGroup->pRewardLevelField);
	// Contact
	MEFieldSafeDestroy(&pGroup->pContactField);
	MEFieldSafeDestroy(&pGroup->pContactDialogField);
	// Crafting
	MEFieldSafeDestroy(&pGroup->pCraftSkillField);
	MEFieldSafeDestroy(&pGroup->pCraftMaxSkillField);
	MEFieldSafeDestroy(&pGroup->pCraftReward1Field);
	MEFieldSafeDestroy(&pGroup->pCraftReward2Field);
	MEFieldSafeDestroy(&pGroup->pCraftReward3Field);
	// Destructible
	MEFieldSafeDestroy(&pGroup->pCritterField);
	MEFieldSafeDestroy(&pGroup->pCritterOverrideField);
	MEFieldSafeDestroy(&pGroup->pCritterLevelField);
	MEFieldSafeDestroy(&pGroup->pDisplayNameField);
	MEFieldSafeDestroy(&pGroup->pEntityNameField);
	MEFieldSafeDestroy(&pGroup->pRespawnTimeField);
	MEFieldSafeDestroy(&pGroup->pDeathPowerField);
	// Door
	GEFreeVariableDefGroup(pGroup->pDoorDestGroup);
	MEFieldSafeDestroy(&pGroup->pPerPlayerDoorField);
	MEFieldSafeDestroy(&pGroup->pSinglePlayerDoorField);
	MEFieldSafeDestroy(&pGroup->pCollectDestStatusField);
    MEFieldSafeDestroy(&pGroup->pDestinationSameOwnerField);
	MEFieldSafeDestroy(&pGroup->pAllowJoinTeamAtDoorField);
	MEFieldSafeDestroy(&pGroup->pDoorTypeField);
	MEFieldSafeDestroy(&pGroup->pDoorIDField);
	MEFieldSafeDestroy(&pGroup->pDoorQueueField);
	MEFieldSafeDestroy(&pGroup->pDoorTransitionField);
	MEFieldSafeDestroy(&pGroup->pIncludeTeammatesDoorField);
	// FromDefinition
	MEFieldSafeDestroy(&pGroup->pInteractDefField);
	MEFieldSafeDestroy(&pGroup->pAutoExecField);
	MEFieldSafeDestroy(&pGroup->pDisablePowersInterruptField);
	MEFieldSafeDestroy(&pGroup->pAllowDuringCombatField);
	// Sound
	MEFieldSafeDestroy(&pGroup->pAttemptSoundField);
	MEFieldSafeDestroy(&pGroup->pSuccessSoundField);
	MEFieldSafeDestroy(&pGroup->pFailureSoundField);
	MEFieldSafeDestroy(&pGroup->pInterruptSoundField);

	// Free the variable def groups
	for(i=eaSize(&pGroup->eaVariableDefGroups)-1; i>=0; --i) {
		GEFreeVariableDefGroup(pGroup->eaVariableDefGroups[i]);
	}
	eaDestroy(&pGroup->eaVariableDefGroups);
	for(i=eaSize(&pGroup->eaVarLabels)-1; i>=0; --i) {
		ui_WidgetQueueFreeAndNull(&pGroup->eaVarLabels[i]);
	}
	eaDestroy(&pGroup->eaVarLabels);

	// Clear arrays to centrally allocated data
	eaDestroy(&pGroup->eaInteractDefNames);

	free(pGroup);
}

static bool InteractionFieldPreChangeCB(MEField *pField, bool bFinished, InteractionPropertiesGroup *pGroup)
{
	if (pGroup->cbPreChange) {
		return (*pGroup->cbPreChange)(pField, bFinished, pGroup->pParentData);
	}
	return true;
}

static void InteractionFieldChangeCB(MEField *pField, bool bFinished, InteractionPropertiesGroup *pGroup)
{
	if (pGroup->cbChange) {
		(*pGroup->cbChange)(pField, bFinished, pGroup->pParentData);
	}
}

static void InteractionClassChangeCB(MEField *pField, bool bFinished, InteractionPropertiesGroup *pGroup)
{
	if (pGroup->pcPreviousClass == pGroup->pProperties->pcInteractionClass) {
		// No change
		return;
	}
	pGroup->pcPreviousClass = pGroup->pProperties->pcInteractionClass;

	// Create required data
	if (!pGroup->pProperties->pContactProperties && (stricmp(pGroup->pProperties->pcInteractionClass, "Contact") == 0)) {
		pGroup->pProperties->pContactProperties = StructCreate(parse_WorldContactInteractionProperties);
	}
	if (!pGroup->pProperties->pCraftingProperties && (stricmp(pGroup->pProperties->pcInteractionClass, "CraftingStation") == 0)) {
		pGroup->pProperties->pCraftingProperties = StructCreate(parse_WorldCraftingInteractionProperties);
	}
	if (!pGroup->pProperties->pDestructibleProperties && (stricmp(pGroup->pProperties->pcInteractionClass, "Destructible") == 0)) {
		pGroup->pProperties->pDestructibleProperties = StructCreate(parse_WorldDestructibleInteractionProperties);
	}
	if (!pGroup->pProperties->pDoorProperties && (stricmp(pGroup->pProperties->pcInteractionClass, "Door") == 0)) {
		pGroup->pProperties->pDoorProperties = StructCreate(parse_WorldDoorInteractionProperties);
	}
	if (!pGroup->pProperties->pGateProperties && (stricmp(pGroup->pProperties->pcInteractionClass, "Gate") == 0)) {
		pGroup->pProperties->pGateProperties = StructCreate(parse_WorldGateInteractionProperties);
	}
	if(stricmp(pGroup->pProperties->pcInteractionClass, "FromDefinition") == 0) {
		if(pGroup->pProperties->pInteractCond || pGroup->pProperties->pSuccessCond || pGroup->pProperties->pAttemptableCond) {
			pGroup->pProperties->bOverrideInteract = true;
		}
		if(EMPTY_TO_NULL(pGroup->pProperties->pcCategoryName) || pGroup->bPriorityChanged) {
			pGroup->pProperties->bOverrideCategoryPriority = true;
		}
		if(pGroup->pProperties->pVisibleExpr) {
			pGroup->pProperties->bOverrideVisibility = true;
		}
		if(pGroup->pProperties->pSoundProperties) {
			StructDestroySafe(parse_WorldSoundInteractionProperties, &pGroup->pProperties->pSoundProperties);
		}
		StructDestroySafe(parse_WorldTimeInteractionProperties, &pGroup->pProperties->pTimeProperties);

	}

	// Clear inappropriate data
	if (pGroup->pProperties->pContactProperties && (stricmp(pGroup->pProperties->pcInteractionClass, "Contact") != 0) && (stricmp(pGroup->pProperties->pcInteractionClass, "FromDefinition") != 0) ) {
		StructDestroySafe(parse_WorldContactInteractionProperties, &pGroup->pProperties->pContactProperties);	
	}
	if (pGroup->pProperties->pCraftingProperties && (stricmp(pGroup->pProperties->pcInteractionClass, "CraftingStation") != 0)) {
		StructDestroySafe(parse_WorldCraftingInteractionProperties, &pGroup->pProperties->pCraftingProperties);
	}
	if (pGroup->pProperties->pDestructibleProperties && (stricmp(pGroup->pProperties->pcInteractionClass, "Destructible") != 0)) {
		StructDestroySafe(parse_WorldDestructibleInteractionProperties, &pGroup->pProperties->pDestructibleProperties);
	}
	if (pGroup->pProperties->pDoorProperties && (stricmp(pGroup->pProperties->pcInteractionClass, "Door") != 0)) {
		StructDestroySafe(parse_WorldDoorInteractionProperties, &pGroup->pProperties->pDoorProperties);
	}
	if (pGroup->pProperties->pGateProperties && (stricmp(pGroup->pProperties->pcInteractionClass, "Gate") != 0)) {
		StructDestroySafe(parse_WorldGateInteractionProperties, &pGroup->pProperties->pGateProperties);
	}
	if (stricmp(pGroup->pProperties->pcInteractionClass, "FromDefinition") != 0) {
		REMOVE_HANDLE(pGroup->pProperties->hInteractionDef);
		pGroup->pProperties->bOverrideInteract = false;
		pGroup->pProperties->bOverrideVisibility = false;
		pGroup->pProperties->bOverrideCategoryPriority = false;
	
	}

	if ((stricmp(pGroup->pProperties->pcInteractionClass, "Clickable") != 0) &&
		(stricmp(pGroup->pProperties->pcInteractionClass, "Contact") != 0) &&
		(stricmp(pGroup->pProperties->pcInteractionClass, "CraftingStation") != 0) &&
		(stricmp(pGroup->pProperties->pcInteractionClass, "Door") != 0) &&
		(stricmp(pGroup->pProperties->pcInteractionClass, "Gate") != 0) &&
		(stricmp(pGroup->pProperties->pcInteractionClass, "FromDefinition") != 0)
		) {
		exprDestroy(pGroup->pProperties->pInteractCond);
		pGroup->pProperties->pInteractCond = NULL;
		exprDestroy(pGroup->pProperties->pSuccessCond);
		pGroup->pProperties->pSuccessCond = NULL;
		exprDestroy(pGroup->pProperties->pAttemptableCond);
		pGroup->pProperties->pAttemptableCond = NULL;

		StructDestroySafe(parse_WorldTextInteractionProperties, &pGroup->pProperties->pTextProperties);
	}
	if ((stricmp(pGroup->pProperties->pcInteractionClass, "Clickable") != 0) &&
		(stricmp(pGroup->pProperties->pcInteractionClass, "Contact") != 0) &&
		(stricmp(pGroup->pProperties->pcInteractionClass, "Gate") != 0) &&
		(stricmp(pGroup->pProperties->pcInteractionClass, "Door") != 0) &&
		(stricmp(pGroup->pProperties->pcInteractionClass, "FromDefinition") != 0)) {
		StructDestroySafe(parse_WorldTimeInteractionProperties, &pGroup->pProperties->pTimeProperties);
		StructDestroySafe(parse_WorldActionInteractionProperties, &pGroup->pProperties->pActionProperties);
	}
	if ((stricmp(pGroup->pProperties->pcInteractionClass, "Clickable") != 0) &&
		(stricmp(pGroup->pProperties->pcInteractionClass, "Gate") != 0) &&
		(stricmp(pGroup->pProperties->pcInteractionClass, "FromDefinition") != 0)) {
		StructDestroySafe(parse_WorldRewardInteractionProperties, &pGroup->pProperties->pRewardProperties);
	}

	InteractionFieldChangeCB(pField, bFinished, pGroup);
}

static void InteractionAddDoorVariable(UIButton *pButton, InteractionPropertiesGroup *pGroup)
{
	WorldVariableDef *pVarDef = StructCreate(parse_WorldVariableDef);
	pVarDef->eDefaultType = WVARDEF_SPECIFY_DEFAULT;
	pVarDef->pSpecificValue = StructCreate(parse_WorldVariable);
	eaPush(&pGroup->pProperties->pDoorProperties->eaVariableDefs, pVarDef);

	// Notify of change
	InteractionFieldChangeCB(NULL, true, pGroup);
}

static void ShowDefValuesToggled(UICheckButton *pButton, InteractionPropertiesGroup *pGroup)
{
	pGroup->bShowDefValues = pButton->state;

	InteractionFieldChangeCB(NULL, true, pGroup);
}

static void InteractionContactToggled(UICheckButton *pButton, InteractionPropertiesGroup *pGroup)
{
	if (pGroup->pProperties->pContactProperties) {
		StructDestroySafe(parse_WorldContactInteractionProperties, &pGroup->pProperties->pContactProperties);
	} else {
		if (pGroup->pProperties->pcInteractionClass == pcPooled_FromDefinition) {
			InteractionDef *pDef = GET_REF(pGroup->pProperties->hInteractionDef);
			if (pDef && pDef->pEntry && pDef->pEntry->pContactProperties) {
				pGroup->pProperties->pContactProperties = StructClone(parse_WorldContactInteractionProperties, pDef->pEntry->pContactProperties);
			}
		}
		if (!pGroup->pProperties->pContactProperties) {
			pGroup->pProperties->pContactProperties = StructCreate(parse_WorldContactInteractionProperties);
		}
	}

	InteractionFieldChangeCB(NULL, true, pGroup);
}

static void InteractionDoorToggled(UICheckButton *pButton, InteractionPropertiesGroup *pGroup)
{
	if (pGroup->pProperties->pDoorProperties) {
		StructDestroySafe(parse_WorldDoorInteractionProperties, &pGroup->pProperties->pDoorProperties);
	} else {
		if (pGroup->pProperties->pcInteractionClass == pcPooled_FromDefinition) {
			InteractionDef *pDef = GET_REF(pGroup->pProperties->hInteractionDef);
			if (pDef && pDef->pEntry && pDef->pEntry->pDoorProperties) {
				pGroup->pProperties->pDoorProperties = StructClone(parse_WorldDoorInteractionProperties, pDef->pEntry->pDoorProperties);
			}
		}
		if (!pGroup->pProperties->pDoorProperties) {
			pGroup->pProperties->pDoorProperties = StructCreate(parse_WorldDoorInteractionProperties);
		}
	}

	InteractionFieldChangeCB(NULL, true, pGroup);
}


static void InteractionInteractToggled(UICheckButton *pButton, InteractionPropertiesGroup *pGroup)
{
	if (pGroup->pProperties->bOverrideInteract) {
		pGroup->pProperties->bOverrideInteract = false;
		exprDestroy(pGroup->pProperties->pInteractCond);
		pGroup->pProperties->pInteractCond = NULL;
		exprDestroy(pGroup->pProperties->pSuccessCond);
		pGroup->pProperties->pSuccessCond = NULL;
		exprDestroy(pGroup->pProperties->pAttemptableCond);
		pGroup->pProperties->pAttemptableCond = NULL;
	} else {
		pGroup->pProperties->bOverrideInteract = true;
		if (pGroup->pProperties->pcInteractionClass == pcPooled_FromDefinition) {
			InteractionDef *pDef = GET_REF(pGroup->pProperties->hInteractionDef);
			if (pDef && pDef->pEntry) {
				pGroup->pProperties->pInteractCond = exprClone(pDef->pEntry->pInteractCond);
				pGroup->pProperties->pSuccessCond = exprClone(pDef->pEntry->pSuccessCond);
				pGroup->pProperties->pAttemptableCond = exprClone(pDef->pEntry->pAttemptableCond);
			}
		}
	}

	InteractionFieldChangeCB(NULL, true, pGroup);
}


static void InteractionVisibilityToggled(UICheckButton *pButton, InteractionPropertiesGroup *pGroup)
{
	if (pGroup->pProperties->bOverrideVisibility) {
		pGroup->pProperties->bOverrideVisibility = false;
		exprDestroy(pGroup->pProperties->pVisibleExpr);
		pGroup->pProperties->pVisibleExpr = NULL;
	} else {
		pGroup->pProperties->bOverrideVisibility = true;
		if (pGroup->pProperties->pcInteractionClass == pcPooled_FromDefinition) {
			InteractionDef *pDef = GET_REF(pGroup->pProperties->hInteractionDef);
			if (pDef && pDef->pEntry) {
				pGroup->pProperties->pVisibleExpr = exprClone(pDef->pEntry->pVisibleExpr);
			}
		}
	}

	InteractionFieldChangeCB(NULL, true, pGroup);
}


static void InteractionCategoryToggled(UICheckButton *pButton, InteractionPropertiesGroup *pGroup)
{
	if (pGroup->pProperties->bOverrideCategoryPriority) {
		pGroup->pProperties->bOverrideCategoryPriority = false;
		pGroup->pProperties->pcCategoryName = NULL;
		pGroup->pProperties->iPriority = 0;
		pGroup->bPriorityChanged = false;
	} else {
		pGroup->pProperties->bOverrideCategoryPriority = true;
		if (pGroup->pProperties->pcInteractionClass == pcPooled_FromDefinition) {
			InteractionDef *pDef = GET_REF(pGroup->pProperties->hInteractionDef);
			if (pDef && pDef->pEntry) {
				pGroup->pProperties->pcCategoryName = pDef->pEntry->pcCategoryName;
				pGroup->pProperties->iPriority = pDef->pEntry->iPriority;
			}
		}
	}

	InteractionFieldChangeCB(NULL, true, pGroup);
}


static void InteractionAnimationToggled(UICheckButton *pButton, InteractionPropertiesGroup *pGroup)
{
	if (pGroup->pProperties->pAnimationProperties) {
		StructDestroySafe(parse_WorldAnimationInteractionProperties, &pGroup->pProperties->pAnimationProperties);
	} else {
		if (pGroup->pProperties->pcInteractionClass == pcPooled_FromDefinition) {
			InteractionDef *pDef = GET_REF(pGroup->pProperties->hInteractionDef);
			if (pDef && pDef->pEntry && pDef->pEntry->pAnimationProperties) {
				pGroup->pProperties->pAnimationProperties = StructClone(parse_WorldAnimationInteractionProperties, pDef->pEntry->pAnimationProperties);
			}
		}
		if (!pGroup->pProperties->pAnimationProperties) {
			pGroup->pProperties->pAnimationProperties = StructCreate(parse_WorldAnimationInteractionProperties);
		}
	}

	InteractionFieldChangeCB(NULL, true, pGroup);
}
static void InteractionActionToggled(UICheckButton *pButton, InteractionPropertiesGroup *pGroup)
{
	if (pGroup->pProperties->pActionProperties) {
		StructDestroySafe(parse_WorldActionInteractionProperties, &pGroup->pProperties->pActionProperties);
	} else {
		if (pGroup->pProperties->pcInteractionClass == pcPooled_FromDefinition) {
			InteractionDef *pDef = GET_REF(pGroup->pProperties->hInteractionDef);
			if (pDef && pDef->pEntry && pDef->pEntry->pActionProperties) {
				pGroup->pProperties->pActionProperties = StructClone(parse_WorldActionInteractionProperties, pDef->pEntry->pActionProperties);
			}
		}
		if (!pGroup->pProperties->pActionProperties) {
			pGroup->pProperties->pActionProperties = StructCreate(parse_WorldActionInteractionProperties);
		}
	}

	InteractionFieldChangeCB(NULL, true, pGroup);
}

static void InteractionSoundToggled(UICheckButton *pButton, InteractionPropertiesGroup *pGroup)
{
	if (pGroup->pProperties->pSoundProperties) {
		StructDestroySafe(parse_WorldSoundInteractionProperties, &pGroup->pProperties->pSoundProperties);
	} else {
		if (pGroup->pProperties->pcInteractionClass == pcPooled_FromDefinition) {
			InteractionDef *pDef = GET_REF(pGroup->pProperties->hInteractionDef);
			if (pDef && pDef->pEntry && pDef->pEntry->pSoundProperties) {
				pGroup->pProperties->pSoundProperties = StructClone(parse_WorldSoundInteractionProperties, pDef->pEntry->pSoundProperties);
			}
		}
		if (!pGroup->pProperties->pSoundProperties) {
			pGroup->pProperties->pSoundProperties = StructCreate(parse_WorldSoundInteractionProperties);
		}
	}

	InteractionFieldChangeCB(NULL, true, pGroup);
}

static void InteractionRewardToggled(UICheckButton *pButton, InteractionPropertiesGroup *pGroup)
{
	if (pGroup->pProperties->pRewardProperties) {
		StructDestroySafe(parse_WorldRewardInteractionProperties, &pGroup->pProperties->pRewardProperties);
	} else {
		if (pGroup->pProperties->pcInteractionClass == pcPooled_FromDefinition) {
			InteractionDef *pDef = GET_REF(pGroup->pProperties->hInteractionDef);
			if (pDef && pDef->pEntry && pDef->pEntry->pRewardProperties) {
				pGroup->pProperties->pRewardProperties = StructClone(parse_WorldRewardInteractionProperties, pDef->pEntry->pRewardProperties);
			}
		}
		if (!pGroup->pProperties->pRewardProperties) {
			pGroup->pProperties->pRewardProperties = StructCreate(parse_WorldRewardInteractionProperties);
		}
	}

	InteractionFieldChangeCB(NULL, true, pGroup);
}


static void InteractionTextToggled(UICheckButton *pButton, InteractionPropertiesGroup *pGroup)
{
	if (pGroup->pProperties->pTextProperties) {
		StructDestroySafe(parse_WorldTextInteractionProperties, &pGroup->pProperties->pTextProperties);
	} else {
		if (pGroup->pProperties->pcInteractionClass == pcPooled_FromDefinition) {
			InteractionDef *pDef = GET_REF(pGroup->pProperties->hInteractionDef);
			if (pDef && pDef->pEntry && pDef->pEntry->pTextProperties) {
				pGroup->pProperties->pTextProperties = StructClone(parse_WorldTextInteractionProperties, pDef->pEntry->pTextProperties);
				langMakeEditorCopy(parse_WorldTextInteractionProperties, pGroup->pProperties->pTextProperties, true);
			}
		}
		if (!pGroup->pProperties->pTextProperties) {
			pGroup->pProperties->pTextProperties = StructCreate(parse_WorldTextInteractionProperties);
		}
	}

	InteractionFieldChangeCB(NULL, true, pGroup);
}


static void InteractionTimeToggled(UICheckButton *pButton, InteractionPropertiesGroup *pGroup)
{
	if (pGroup->pProperties->pTimeProperties) {
		StructDestroySafe(parse_WorldTimeInteractionProperties, &pGroup->pProperties->pTimeProperties);
	} else {
		if (pGroup->pProperties->pcInteractionClass == pcPooled_FromDefinition) {
			InteractionDef *pDef = GET_REF(pGroup->pProperties->hInteractionDef);
			if (pDef && pDef->pEntry && pDef->pEntry->pTimeProperties) {
				pGroup->pProperties->pTimeProperties = StructClone(parse_WorldTimeInteractionProperties, pDef->pEntry->pTimeProperties);
			}
		}
		if (!pGroup->pProperties->pTimeProperties) {
			// Set up as default
			pGroup->pProperties->pTimeProperties = StructCreate(parse_WorldTimeInteractionProperties);
			pGroup->pProperties->pTimeProperties->bInterruptOnDamage = true;
			pGroup->pProperties->pTimeProperties->bInterruptOnMove = true;
			pGroup->pProperties->pTimeProperties->bInterruptOnPower = true;
		}
	}

	InteractionFieldChangeCB(NULL, true, pGroup);
}


static void InteractionExclusiveToggled(UICheckButton *pButton, InteractionPropertiesGroup *pGroup)
{
	pGroup->pProperties->bUseExclusionFlag = 1;
	pGroup->pProperties->bExclusiveInteraction = ui_CheckButtonGetState(pButton);

	InteractionFieldChangeCB(NULL, true, pGroup);
}


static void InteractionSuccessGameActionsUpdated(UIGameActionEditButton *pButton, InteractionPropertiesGroup *pGroup)
{
	if (gameactionblock_Compare(&pGroup->pProperties->pActionProperties->successActions, pButton->pActionBlock)) {
		// No change, so do nothing
		return;
	}

	StructCopyAll(parse_WorldGameActionBlock, pButton->pActionBlock, &pGroup->pProperties->pActionProperties->successActions);

	InteractionFieldChangeCB(NULL, true, pGroup);
}

static void InteractionFailureGameActionsUpdated(UIGameActionEditButton *pButton, InteractionPropertiesGroup *pGroup)
{
	if (gameactionblock_Compare(&pGroup->pProperties->pActionProperties->failureActions, pButton->pActionBlock)) {
		// No change, so do nothing
		return;
	}

	StructCopyAll(parse_WorldGameActionBlock, pButton->pActionBlock, &pGroup->pProperties->pActionProperties->failureActions);

	InteractionFieldChangeCB(NULL, true, pGroup);
}

static bool InteractionPropertiesDefPreChangeCB(MEField *pField, bool bFinished, InteractionPropertiesGroup *pGroup)
{
	if(pGroup && pGroup->cbPreChange) {
		return pGroup->cbPreChange(pField, bFinished, pGroup->pParentData);
	}
	return false;
}

static void InteractionPropertiesDefChangeCB(MEField *pField, bool bFinished, InteractionPropertiesGroup *pGroup)
{
	WorldInteractionPropertyEntry *pPropEntry = pGroup ? pGroup->pProperties: NULL;
	const char *pcClass =  pPropEntry ? interaction_GetEffectiveClass(pPropEntry) : "";
	if(pPropEntry && pPropEntry->pContactProperties && pcClass != pcPooled_Contact && pcClass != pcPooled_FromDefinition) {
		StructDestroySafe(parse_WorldContactInteractionProperties, &pPropEntry->pContactProperties);
	}
	if(pGroup && pGroup->cbChange) {
		pGroup->cbChange(pField, bFinished, pGroup->pParentData);
	}
}

static bool InteractionPropertiesFieldPreChangeCB(MEField *pField, bool bFinished, InteractionPropertiesGroup *pGroup)
{
	if(pGroup && pGroup->cbPreChange) {
		return pGroup->cbPreChange(pField, bFinished, pGroup->pParentData);
	}
	return false;
}

static void InteractionPropertiesFieldChangeCB(MEField *pField, bool bFinished, InteractionPropertiesGroup *pGroup)
{
	if(pGroup) {
		if(pGroup->cbChange)
			pGroup->cbChange(pField, bFinished, pGroup->pParentData);
	}
}

static void InteractionPropertiesPriorityChangeCB(MEField *pField, bool bFinished, InteractionPropertiesGroup *pGroup)
{
	if(pGroup) {
		pGroup->bPriorityChanged = true;
		if(pGroup->cbChange)
			pGroup->cbChange(pField, bFinished, pGroup->pParentData);
	}
}


F32 UpdateInteractionPropertiesGroup(InteractionPropertiesGroup *pGroup, WorldInteractionPropertyEntry *pPropEntry, WorldInteractionPropertyEntry *pOrigPropEntry, const char *pcSrcMap, F32 y)
{
	WorldInteractionPropertyEntry *pDefEntry = NULL;
	const char *pcClass;
	int i;
	int iNumVars = 0;
	char *estrText = NULL;
	char buf[128];

	// Set bPriorityChanged to false if this is the first update
	if(!pGroup->pProperties)
		pGroup->bPriorityChanged = false;


	pGroup->pProperties = pPropEntry;

	// Interaction Class
	if (!pGroup->bHideClassField) {
		MEExpanderRefreshLabel(&pGroup->pClassLabel, "Class", "The type of interaction to perform", pGroup->fOffsetX, 0, y, UI_WIDGET(pGroup->pExpander));
		MEExpanderRefreshDataField(&pGroup->pClassField, pOrigPropEntry, pPropEntry, parse_WorldInteractionPropertyEntry, "InteractionClass", pGroup->bDefEditMode ? &s_eaClassNames2 : &s_eaClassNames, true,
									UI_WIDGET(pGroup->pExpander), pGroup->fOffsetX + X_OFFSET_INTERACT_CONTROL, y, 0, 140, UIUnitFixed, 0, InteractionClassChangeCB, InteractionFieldPreChangeCB, pGroup);
		y += STANDARD_ROW_HEIGHT;
	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pClassLabel);
		MEFieldSafeDestroy(&pGroup->pClassField);
	}

	// Initialize static lists first time through
	if (!eaSize(&s_eaClassNames)) {
		// Set up allowed interaction class types for normal mode
		eaPush(&s_eaClassNames, (char*)pcPooled_Clickable);
		eaPush(&s_eaClassNames, (char*)pcPooled_Contact);
		eaPush(&s_eaClassNames, (char*)pcPooled_CraftingStation);
		//eaPush(&s_eaClassNames, (char*)pcPooled_Destructible); // Currently disabled since runtime doesn't handle this well
		eaPush(&s_eaClassNames, (char*)pcPooled_Door);
		eaPush(&s_eaClassNames, (char*)pcPooled_Gate);
		eaPush(&s_eaClassNames, (char*)pcPooled_FromDefinition);
		eaPush(&s_eaClassNames, (char*)pcPooled_TeamCorral);

		// Set up allowed interaction class types for Def edit mode
		eaPush(&s_eaClassNames2, (char*)pcPooled_Clickable);
		eaPush(&s_eaClassNames2, (char*)pcPooled_Contact);
		eaPush(&s_eaClassNames2, (char*)pcPooled_CraftingStation);
		//eaPush(&s_eaClassNames2, (char*)pcPooled_Destructible); // Currently disabled since runtime doesn't handle this well
		eaPush(&s_eaClassNames2, (char*)pcPooled_Door);
		eaPush(&s_eaClassNames2, (char*)pcPooled_Gate);
		//eaPush(&s_eaClassNames2, (char*)pcPooled_FromDefinition); // Not allowed in def edit mode
		eaPush(&s_eaClassNames2, (char*)pcPooled_TeamCorral);

		// Make sure all interaction defs are present
		resSetDictionaryEditMode("InteractionDef", true);
		resRequestAllResourcesInDictionary("InteractionDef");
	}
	if (!eaSize(&s_eaCategoryNames)) {
		// Set up allowed category strings
		eaPush(&s_eaCategoryNames, (char*)allocAddString("None") );
		for (i = 0; i<eaSize(&g_eaOptionalActionCategoryDefs); i++){
			eaPush(&s_eaCategoryNames, (char*)g_eaOptionalActionCategoryDefs[i]->pcName);
		}
	}

	pcClass = interaction_GetEffectiveClass(pPropEntry);

	// From Definition
	if (pPropEntry->pcInteractionClass == pcPooled_FromDefinition) {
		DictionaryEArrayStruct *pDefs = resDictGetEArrayStruct("InteractionDef");
		InteractionDef *pDef;

		MEExpanderRefreshLabel(&pGroup->pFromDefSectionLabel, "From Definition Properties", NULL, pGroup->fOffsetX, 0, y, UI_WIDGET(pGroup->pExpander));
		y += STANDARD_ROW_HEIGHT;

		MEExpanderRefreshLabel(&pGroup->pInteractDefLabel, "Interaction Def", "Which interaction def should be used. (required)", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
		MEExpanderRefreshDataField(&pGroup->pInteractDefField, pOrigPropEntry, pPropEntry, parse_WorldInteractionPropertyEntry, "InteractionDef", &pGroup->eaInteractDefNames, true,
								UI_WIDGET(pGroup->pExpander), pGroup->fOffsetX + X_OFFSET_INTERACT_CONTROL, y, 0, 1.0, UIUnitPercentage, 0, InteractionPropertiesDefChangeCB, InteractionPropertiesDefPreChangeCB, pGroup);
		y += STANDARD_ROW_HEIGHT;

		// Refresh def names
		eaClear(&pGroup->eaInteractDefNames);
		for(i=0; i<eaSize(&pDefs->ppReferents); ++i) {
			pDef = pDefs->ppReferents[i];
			if ((pGroup->eType == pDef->eType) || (pDef->eType == InteractionDefType_Any)) {
				eaPush(&pGroup->eaInteractDefNames, pDef->pcName);
			}
		}
		pDef = GET_REF(pPropEntry->hInteractionDef);

		if(pDef) {
			if(!pGroup->pShowDefValues) {
				pGroup->pShowDefValues = ui_CheckButtonCreate(pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, y, "Show Def Values", true);
				ui_WidgetSetTooltipString(UI_WIDGET(pGroup->pShowDefValues), "Shows the values inherited by the interaction def.");
				ui_CheckButtonSetToggledCallback(pGroup->pShowDefValues, ShowDefValuesToggled, pGroup);
				ui_ExpanderAddChild(pGroup->pExpander, pGroup->pShowDefValues);
				pGroup->bShowDefValues = true;
			} else {
				ui_WidgetSetPosition(UI_WIDGET(pGroup->pShowDefValues), pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, y);
			}
			y += STANDARD_ROW_HEIGHT;
		} else {
			pGroup->bShowDefValues = false;
			ui_WidgetQueueFreeAndNull(&pGroup->pShowDefValues);
		}

		if (pDef) {
			pDefEntry = pDef->pEntry;
			MEExpanderRefreshLabel(&pGroup->pDefClassLabel, "Class", "The type of interaction to perform", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
			MEExpanderRefreshLabel(&pGroup->pDefClassValueLabel, pDefEntry->pcInteractionClass, NULL, pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL, 0, y, UI_WIDGET(pGroup->pExpander));
			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pDefClassLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pDefClassValueLabel);
		}

	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pFromDefSectionLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pInteractDefLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pDefClassLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pDefClassValueLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pShowDefValues);
		MEFieldSafeDestroy(&pGroup->pInteractDefField);
	}


	// Contact
	if (pcClass == pcPooled_Contact) {
		if (pDefEntry) {
			if (!pGroup->pContactButton) {
				pGroup->pContactButton = ui_CheckButtonCreate(pGroup->fOffsetX, y, "Custom Contact", !!pPropEntry->pContactProperties);
				ui_CheckButtonSetToggledCallback(pGroup->pContactButton, InteractionContactToggled, pGroup);
				ui_WidgetSetTooltipString(UI_WIDGET(pGroup->pContactButton), "Adds custom contact information.");
				ui_ExpanderAddChild(pGroup->pExpander, pGroup->pContactButton);
			} else {
				ui_WidgetSetPosition(UI_WIDGET(pGroup->pContactButton), pGroup->fOffsetX, y);
			}
			ui_CheckButtonSetState(pGroup->pContactButton, (pPropEntry->pContactProperties != NULL));
			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pContactButton);
		}
		if (!pDefEntry && pcClass != pcPooled_FromDefinition) {
			MEExpanderRefreshLabel(&pGroup->pContactSectionLabel, "Contact Properties", NULL, pGroup->fOffsetX, 0, y, UI_WIDGET(pGroup->pExpander));
			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pContactSectionLabel);
		}

		if (pPropEntry->pContactProperties) {
			MEExpanderRefreshLabel(&pGroup->pContactLabel, "Contact", "Which contact def should be used. (required)", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
			MEExpanderRefreshGlobalDictionaryField(&pGroup->pContactField, pOrigPropEntry ? pOrigPropEntry->pContactProperties : NULL, pPropEntry->pContactProperties, parse_WorldContactInteractionProperties, "ContactDef", "Contact",
									UI_WIDGET(pGroup->pExpander), pGroup->fOffsetX + X_OFFSET_INTERACT_CONTROL, y, 0, 1.0, UIUnitPercentage, 0, pGroup->cbChange, pGroup->cbPreChange, pGroup->pParentData);
			ui_WidgetQueueFreeAndNull(&pGroup->pContactValueLabel);
			y += STANDARD_ROW_HEIGHT;
			MEExpanderRefreshLabel(&pGroup->pContactDialogLabel, "Dialog Name", "Which dialog on the contact def should be used. (optional)", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
			MEExpanderRefreshSimpleField(&pGroup->pContactDialogField, pOrigPropEntry ? pOrigPropEntry->pContactProperties : NULL, pPropEntry->pContactProperties, parse_WorldContactInteractionProperties, "DialogName", kMEFieldType_TextEntry,
				UI_WIDGET(pGroup->pExpander), pGroup->fOffsetX + X_OFFSET_INTERACT_CONTROL, y, 0, 1.0, UIUnitPercentage, 0, pGroup->cbChange, pGroup->cbPreChange, pGroup->pParentData);
			ui_WidgetQueueFreeAndNull(&pGroup->pContactDialogValueLabel);
			y += STANDARD_ROW_HEIGHT;
		} else if (pDefEntry && pDefEntry->pContactProperties && pGroup->bShowDefValues) {
			MEExpanderRefreshLabel(&pGroup->pContactLabel, "Contact", "Which contact def should be used. (required)", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
			MEExpanderRefreshLabel(&pGroup->pContactValueLabel, REF_STRING_FROM_HANDLE(pDefEntry->pContactProperties->hContactDef), NULL, pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL, 0, y, UI_WIDGET(pGroup->pExpander));
			MEFieldSafeDestroy(&pGroup->pContactField);
			y += STANDARD_ROW_HEIGHT;
			MEExpanderRefreshLabel(&pGroup->pContactDialogLabel, "Dialog Name", "Which dialog on the contact def should be used. (optional)", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
			MEExpanderRefreshLabel(&pGroup->pContactDialogValueLabel, pDefEntry->pContactProperties->pcDialogName, NULL, pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL, 0, y, UI_WIDGET(pGroup->pExpander));
			MEFieldSafeDestroy(&pGroup->pContactDialogField);
			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pContactLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pContactValueLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pContactDialogLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pContactDialogValueLabel);
			MEFieldSafeDestroy(&pGroup->pContactField);
			MEFieldSafeDestroy(&pGroup->pContactDialogField);
		}
	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pContactSectionLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pContactLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pContactValueLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pContactDialogLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pContactDialogValueLabel);
		MEFieldSafeDestroy(&pGroup->pContactDialogField);
	}

	// Crafting Station
	if (pcClass == pcPooled_CraftingStation) {
		if (!pDefEntry) {
			MEExpanderRefreshLabel(&pGroup->pCraftingSectionLabel, "Crafting Station Properties", NULL, pGroup->fOffsetX, 0, y, UI_WIDGET(pGroup->pExpander));
			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pCraftingSectionLabel);
		}

		if (pPropEntry->pCraftingProperties) {
			MEExpanderRefreshLabel(&pGroup->pCraftSkillLabel, "Skill Type", "The skill type of this crafting station.", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
			MEExpanderRefreshEnumField(&pGroup->pCraftSkillField, pOrigPropEntry ? pOrigPropEntry->pCraftingProperties : NULL, pPropEntry->pCraftingProperties, parse_WorldCraftingInteractionProperties, "SkillTypes", WorldSkillTypeEnum,
									   UI_WIDGET(pGroup->pExpander), pGroup->fOffsetX + X_OFFSET_INTERACT_CONTROL, y, 0, 120, UIUnitFixed, 0, pGroup->cbChange, pGroup->cbPreChange, pGroup->pParentData);
			ui_WidgetQueueFreeAndNull(&pGroup->pCraftSkillValueLabel);
			y += STANDARD_ROW_HEIGHT;
		} else if (pDefEntry && pDefEntry->pCraftingProperties && pGroup->bShowDefValues) {
			int iColumn;
			if (ParserFindColumn(parse_WorldCraftingInteractionProperties, "SkillTypes", &iColumn)) {
				MEExpanderRefreshLabel(&pGroup->pCraftSkillLabel, "Skill Type", "The skill type of this crafting station.", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
				FieldWriteText(parse_WorldCraftingInteractionProperties, iColumn, pDefEntry->pCraftingProperties, 0, &estrText, 0);
				MEExpanderRefreshLabel(&pGroup->pCraftSkillValueLabel, estrText, NULL, pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL, 0, y, UI_WIDGET(pGroup->pExpander));
				estrDestroy(&estrText);
			}
			MEFieldSafeDestroy(&pGroup->pCraftSkillField);
			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pCraftSkillLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pCraftSkillValueLabel);
			MEFieldSafeDestroy(&pGroup->pCraftSkillField);
		}

		if (pPropEntry->pCraftingProperties) {
			MEExpanderRefreshLabel(&pGroup->pCraftMaxSkillLabel, "Max Skill", "The maximum skill this station operates at (caps the player's skill).  Zero means no maximum.", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
			MEExpanderRefreshSimpleField(&pGroup->pCraftMaxSkillField, pOrigPropEntry ? pOrigPropEntry->pCraftingProperties : NULL, pPropEntry->pCraftingProperties, parse_WorldCraftingInteractionProperties, "MaxSkill", kMEFieldType_TextEntry,
									UI_WIDGET(pGroup->pExpander), pGroup->fOffsetX + X_OFFSET_INTERACT_CONTROL, y, 0, 80, UIUnitFixed, 0, pGroup->cbChange, pGroup->cbPreChange, pGroup->pParentData);
			ui_WidgetQueueFreeAndNull(&pGroup->pCraftMaxSkillValueLabel);
			y += STANDARD_ROW_HEIGHT;
		} else if (pDefEntry && pDefEntry->pCraftingProperties && pGroup->bShowDefValues) {
			MEExpanderRefreshLabel(&pGroup->pCraftMaxSkillLabel, "Max Skill", "The maximum skill this station operates at (caps the player's skill).  Zero means no maximum.", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
			sprintf(buf, "%d", pDefEntry->pCraftingProperties->iMaxSkill);
			MEExpanderRefreshLabel(&pGroup->pCraftMaxSkillValueLabel, buf, NULL, pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL, 0, y, UI_WIDGET(pGroup->pExpander));
			MEFieldSafeDestroy(&pGroup->pCraftMaxSkillField);
			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pCraftMaxSkillLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pCraftMaxSkillValueLabel);
			MEFieldSafeDestroy(&pGroup->pCraftMaxSkillField);
		}

		if (pPropEntry->pCraftingProperties || (pDefEntry && pDefEntry->pCraftingProperties && GET_REF(pDefEntry->pCraftingProperties->hCraftRewardTable) && pGroup->bShowDefValues)) {
			MEExpanderRefreshLabel(&pGroup->pCraftReward1Label, "Craft Reward", "Reward table granted on successful crafting. (optional)", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
			if (pPropEntry->pCraftingProperties) {
				MEExpanderRefreshGlobalDictionaryField(&pGroup->pCraftReward1Field, pOrigPropEntry ? pOrigPropEntry->pCraftingProperties : NULL, pPropEntry->pCraftingProperties, parse_WorldCraftingInteractionProperties, "CraftRewardTable", "RewardTable",
										UI_WIDGET(pGroup->pExpander), pGroup->fOffsetX + X_OFFSET_INTERACT_CONTROL, y, 0, 1.0, UIUnitPercentage, 0, pGroup->cbChange, pGroup->cbPreChange, pGroup->pParentData);
				ui_WidgetQueueFreeAndNull(&pGroup->pCraftReward1ValueLabel);
			} else {
				MEExpanderRefreshLabel(&pGroup->pCraftReward1ValueLabel, REF_STRING_FROM_HANDLE(pDefEntry->pCraftingProperties->hCraftRewardTable), NULL, pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL, 0, y, UI_WIDGET(pGroup->pExpander));
				MEFieldSafeDestroy(&pGroup->pCraftReward1Field);
			}
			y += STANDARD_ROW_HEIGHT;
		} else {
			MEFieldSafeDestroy(&pGroup->pCraftReward1Field);
			ui_WidgetQueueFreeAndNull(&pGroup->pCraftReward1ValueLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pCraftReward1Label);
		}

		if (pPropEntry->pCraftingProperties || (pDefEntry && pDefEntry->pCraftingProperties && GET_REF(pDefEntry->pCraftingProperties->hDeconstructRewardTable) && pGroup->bShowDefValues)) {
			MEExpanderRefreshLabel(&pGroup->pCraftReward2Label, "Deconstruct Reward", "Reward table granted on successful deconstruct. (optional)", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
			if (pPropEntry->pCraftingProperties) {
				MEExpanderRefreshGlobalDictionaryField(&pGroup->pCraftReward2Field, pOrigPropEntry ? pOrigPropEntry->pCraftingProperties : NULL, pPropEntry->pCraftingProperties, parse_WorldCraftingInteractionProperties, "DeconstructRewardTable", "RewardTable",
										UI_WIDGET(pGroup->pExpander), pGroup->fOffsetX + X_OFFSET_INTERACT_CONTROL, y, 0, 1.0, UIUnitPercentage, 0, pGroup->cbChange, pGroup->cbPreChange, pGroup->pParentData);
				ui_WidgetQueueFreeAndNull(&pGroup->pCraftReward2ValueLabel);
			} else {
				MEExpanderRefreshLabel(&pGroup->pCraftReward2ValueLabel, REF_STRING_FROM_HANDLE(pDefEntry->pCraftingProperties->hDeconstructRewardTable), NULL, pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL, 0, y, UI_WIDGET(pGroup->pExpander));
				MEFieldSafeDestroy(&pGroup->pCraftReward2Field);
			}
			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pCraftReward2ValueLabel);
			MEFieldSafeDestroy(&pGroup->pCraftReward2Field);
			ui_WidgetQueueFreeAndNull(&pGroup->pCraftReward2Label);
		}

		if (pPropEntry->pCraftingProperties || (pDefEntry && pDefEntry->pCraftingProperties && GET_REF(pDefEntry->pCraftingProperties->hExperimentRewardTable) && pGroup->bShowDefValues)) {
			MEExpanderRefreshLabel(&pGroup->pCraftReward3Label, "Experiment Reward", "Reward table granted on successful experiment. (optional)", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
			if (pPropEntry->pCraftingProperties) {
				MEExpanderRefreshGlobalDictionaryField(&pGroup->pCraftReward3Field, pOrigPropEntry ? pOrigPropEntry->pCraftingProperties : NULL, pPropEntry->pCraftingProperties, parse_WorldCraftingInteractionProperties, "ExperimentRewardTable", "RewardTable",
										UI_WIDGET(pGroup->pExpander), pGroup->fOffsetX + X_OFFSET_INTERACT_CONTROL, y, 0, 1.0, UIUnitPercentage, 0, pGroup->cbChange, pGroup->cbPreChange, pGroup->pParentData);
				ui_WidgetQueueFreeAndNull(&pGroup->pCraftReward3ValueLabel);
			} else {
				MEExpanderRefreshLabel(&pGroup->pCraftReward3ValueLabel, REF_STRING_FROM_HANDLE(pDefEntry->pCraftingProperties->hExperimentRewardTable), NULL, pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL, 0, y, UI_WIDGET(pGroup->pExpander));
				MEFieldSafeDestroy(&pGroup->pCraftReward3Field);
			}
			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pCraftReward3ValueLabel);
			MEFieldSafeDestroy(&pGroup->pCraftReward3Field);
			ui_WidgetQueueFreeAndNull(&pGroup->pCraftReward3Label);
		}

	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pCraftingSectionLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pCraftSkillLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pCraftSkillValueLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pCraftMaxSkillLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pCraftMaxSkillValueLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pCraftReward1Label);
		ui_WidgetQueueFreeAndNull(&pGroup->pCraftReward1ValueLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pCraftReward2Label);
		ui_WidgetQueueFreeAndNull(&pGroup->pCraftReward2ValueLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pCraftReward3Label);
		ui_WidgetQueueFreeAndNull(&pGroup->pCraftReward3ValueLabel);
		MEFieldSafeDestroy(&pGroup->pCraftSkillField);
		MEFieldSafeDestroy(&pGroup->pCraftMaxSkillField);
		MEFieldSafeDestroy(&pGroup->pCraftReward1Field);
		MEFieldSafeDestroy(&pGroup->pCraftReward2Field);
		MEFieldSafeDestroy(&pGroup->pCraftReward3Field);
	}

	// Destructible
	if ((pGroup->eType == InteractionDefType_Node) &&
		pPropEntry->pDestructibleProperties && 
		(pcClass == pcPooled_Destructible)
		) {

		if (!pDefEntry) {
			MEExpanderRefreshLabel(&pGroup->pDestructibeSectionLabel, "Destructible Properties", NULL, pGroup->fOffsetX, 0, y, UI_WIDGET(pGroup->pExpander));
			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pDestructibeSectionLabel);
		}

		MEExpanderRefreshLabel(&pGroup->pCritterLabel, "Critter", "What critter def this should turn into. (required)", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
		MEExpanderRefreshGlobalDictionaryField(&pGroup->pCritterField, pOrigPropEntry ? pOrigPropEntry->pDestructibleProperties : NULL, pPropEntry->pDestructibleProperties, parse_WorldDestructibleInteractionProperties, "CritterDef", "CritterDef",
								UI_WIDGET(pGroup->pExpander), pGroup->fOffsetX + X_OFFSET_INTERACT_CONTROL, y, 0, 1.0, UIUnitPercentage, 0, pGroup->cbChange, pGroup->cbPreChange, pGroup->pParentData);
		y += STANDARD_ROW_HEIGHT;

		MEExpanderRefreshLabel(&pGroup->pCritterOverrideLabel, "Override", "Overrides for the specified critter def. (optional)", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
		MEExpanderRefreshGlobalDictionaryField(&pGroup->pCritterOverrideField, pOrigPropEntry ? pOrigPropEntry->pDestructibleProperties : NULL, pPropEntry->pDestructibleProperties, parse_WorldDestructibleInteractionProperties, "CritterOverrideDef", "CritterOverrideDef",
								UI_WIDGET(pGroup->pExpander), pGroup->fOffsetX + X_OFFSET_INTERACT_CONTROL, y, 0, 1.0, UIUnitPercentage, 0, pGroup->cbChange, pGroup->cbPreChange, pGroup->pParentData);
		y += STANDARD_ROW_HEIGHT;

		MEExpanderRefreshLabel(&pGroup->pRespawnTimeLabel, "Respawn Time", "The amount of time in seconds before the node re-spawns.", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
		MEExpanderRefreshSimpleField(&pGroup->pRespawnTimeField, pOrigPropEntry ? pOrigPropEntry->pDestructibleProperties : NULL, pPropEntry->pDestructibleProperties, parse_WorldDestructibleInteractionProperties, "RespawnTime", kMEFieldType_TextEntry,
								UI_WIDGET(pGroup->pExpander), pGroup->fOffsetX + X_OFFSET_INTERACT_CONTROL, y, 0, 80, UIUnitFixed, 0, pGroup->cbChange, pGroup->cbPreChange, pGroup->pParentData);
		y += STANDARD_ROW_HEIGHT;

		MEExpanderRefreshLabel(&pGroup->pDeathPowerLabel, "On Death Power", "The power to be executed when the object is destroyed. (optional)", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
		MEExpanderRefreshGlobalDictionaryField(&pGroup->pDeathPowerField, pOrigPropEntry ? pOrigPropEntry->pDestructibleProperties : NULL, pPropEntry->pDestructibleProperties, parse_WorldDestructibleInteractionProperties, "OnDeathPower", "PowerDef",
								UI_WIDGET(pGroup->pExpander), pGroup->fOffsetX + X_OFFSET_INTERACT_CONTROL, y, 0, 1.0, UIUnitPercentage, 0, pGroup->cbChange, pGroup->cbPreChange, pGroup->pParentData);
		y += STANDARD_ROW_HEIGHT;

		MEExpanderRefreshLabel(&pGroup->pEntityNameLabel, "Entity Name", "The entity name to be used when created. (optional)", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
		MEExpanderRefreshSimpleField(&pGroup->pEntityNameField, pOrigPropEntry ? pOrigPropEntry->pDestructibleProperties : NULL, pPropEntry->pDestructibleProperties, parse_WorldDestructibleInteractionProperties, "EntityName", kMEFieldType_TextEntry,
								UI_WIDGET(pGroup->pExpander), pGroup->fOffsetX + X_OFFSET_INTERACT_CONTROL, y, 0, 1.0, UIUnitPercentage, 0, pGroup->cbChange, pGroup->cbPreChange, pGroup->pParentData);
		y += STANDARD_ROW_HEIGHT;

		MEExpanderRefreshLabel(&pGroup->pDisplayNameLabel, "Display Name", "The Display name for the object. (optional) If not provided, the object will have no name.", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
		MEExpanderRefreshSimpleField(&pGroup->pDisplayNameField, pOrigPropEntry && pOrigPropEntry->pDestructibleProperties ? &pOrigPropEntry->pDestructibleProperties->displayNameMsg : NULL, &pPropEntry->pDestructibleProperties->displayNameMsg, parse_DisplayMessage, "EditorCopy", kMEFieldType_Message,
								UI_WIDGET(pGroup->pExpander), pGroup->fOffsetX + X_OFFSET_INTERACT_CONTROL, y, 0, 1.0, UIUnitPercentage, 0, pGroup->cbChange, pGroup->cbPreChange, pGroup->pParentData);
		y += STANDARD_ROW_HEIGHT;

		MEExpanderRefreshLabel(&pGroup->pCritterLevelLabel, "Critter Level", "The spawned level of the critter.  If zero, the map level is used instead.", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
		MEExpanderRefreshSimpleField(&pGroup->pCritterLevelField, pOrigPropEntry ? pOrigPropEntry->pDestructibleProperties : NULL, pPropEntry->pDestructibleProperties, parse_WorldDestructibleInteractionProperties, "CritterLevel", kMEFieldType_TextEntry,
								UI_WIDGET(pGroup->pExpander), pGroup->fOffsetX + X_OFFSET_INTERACT_CONTROL, y, 0, 80, UIUnitFixed, 0, pGroup->cbChange, pGroup->cbPreChange, pGroup->pParentData);
		y += STANDARD_ROW_HEIGHT;

	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pDestructibeSectionLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pCritterLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pCritterOverrideLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pCritterLevelLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pEntityNameLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pDisplayNameLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pRespawnTimeLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pDeathPowerLabel);
		MEFieldSafeDestroy(&pGroup->pCritterField);
		MEFieldSafeDestroy(&pGroup->pCritterOverrideField);
		MEFieldSafeDestroy(&pGroup->pCritterLevelField);
		MEFieldSafeDestroy(&pGroup->pEntityNameField);
		MEFieldSafeDestroy(&pGroup->pDisplayNameField);
		MEFieldSafeDestroy(&pGroup->pRespawnTimeField);
		MEFieldSafeDestroy(&pGroup->pDeathPowerField);
	}

	// Door
	if (pcClass == pcPooled_Door) {

 		if (pDefEntry) {
			if (!pGroup->pDoorButton) {
				pGroup->pDoorButton = ui_CheckButtonCreate(pGroup->fOffsetX, y, "Custom Door", !!pPropEntry->pDoorProperties);
				ui_CheckButtonSetToggledCallback(pGroup->pDoorButton, InteractionDoorToggled, pGroup);
				ui_WidgetSetTooltipString(UI_WIDGET(pGroup->pDoorButton), "Adds custom door information.");
				ui_ExpanderAddChild(pGroup->pExpander, pGroup->pDoorButton);
			} else {
				ui_WidgetSetPosition(UI_WIDGET(pGroup->pDoorButton), pGroup->fOffsetX, y);
			}
			ui_CheckButtonSetState(pGroup->pDoorButton, (pPropEntry->pDoorProperties != NULL));
			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pDoorButton);
		}

		if (pPropEntry->pDoorProperties) {
			MEExpanderRefreshLabel(&pGroup->pDoorSectionLabel, "Door Properties", NULL, pGroup->fOffsetX, 0, y, UI_WIDGET(pGroup->pExpander));
			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pDoorSectionLabel);
		}

		if (pPropEntry->pDoorProperties || pGroup->bShowDefValues) {
			MEExpanderRefreshLabel(&pGroup->pDoorTypeLabel, "Door Type", "The 'MapMove' type is used for same and cross-map moves.  The 'QueuedInstance' type queues up a later move.", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
			if (pPropEntry->pDoorProperties) {
				MEExpanderRefreshEnumField(&pGroup->pDoorTypeField, pOrigPropEntry ? pOrigPropEntry->pDoorProperties : NULL, pPropEntry->pDoorProperties, parse_WorldDoorInteractionProperties, "DoorType", WorldDoorTypeEnum,
											UI_WIDGET(pGroup->pExpander), pGroup->fOffsetX + X_OFFSET_INTERACT_CONTROL, y, 0, 140, UIUnitFixed, 0, pGroup->cbChange, pGroup->cbPreChange, pGroup->pParentData);
				ui_WidgetQueueFreeAndNull(&pGroup->pDoorTypeValueLabel);
			} else {
				MEExpanderRefreshLabel(&pGroup->pDoorTypeValueLabel, StaticDefineIntRevLookup(WorldDoorTypeEnum, pDefEntry->pDoorProperties->eDoorType), NULL, pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL, 0, y, UI_WIDGET(pGroup->pExpander));
				MEFieldSafeDestroy(&pGroup->pDoorTypeField);
			}
			y += STANDARD_ROW_HEIGHT;

			MEExpanderRefreshLabel(&pGroup->pDoorIDLabel, "Door ID", "Used for joining teammates together when a teammate uses a 'JoinTeammate' door.", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
			if (pPropEntry->pDoorProperties) {
				MEExpanderRefreshSimpleField(&pGroup->pDoorIDField, pOrigPropEntry ? pOrigPropEntry->pDoorProperties : NULL, pPropEntry->pDoorProperties, parse_WorldDoorInteractionProperties, "DoorIdentifier", kMEFieldType_TextEntry,
					UI_WIDGET(pGroup->pExpander), pGroup->fOffsetX + X_OFFSET_INTERACT_CONTROL, y, 0, 140, UIUnitFixed, 0, pGroup->cbChange, pGroup->cbPreChange, pGroup->pParentData);
				ui_WidgetQueueFreeAndNull(&pGroup->pDoorIDValueLabel);
			} else {
				MEExpanderRefreshLabel(&pGroup->pDoorIDValueLabel, pDefEntry->pDoorProperties->pcDoorIdentifier, NULL, pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL, 0, y, UI_WIDGET(pGroup->pExpander));
				MEFieldSafeDestroy(&pGroup->pDoorIDField);
			}
			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pDoorTypeLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pDoorTypeValueLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pDoorIDLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pDoorIDValueLabel);
			MEFieldSafeDestroy(&pGroup->pDoorTypeField);
			MEFieldSafeDestroy(&pGroup->pDoorIDField);
		}
	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pDoorSectionLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pDoorTypeLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pDoorTypeValueLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pDoorIDLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pDoorIDValueLabel);
		MEFieldSafeDestroy(&pGroup->pDoorTypeField);
		MEFieldSafeDestroy(&pGroup->pDoorIDField);
	}
	if (pcClass == pcPooled_Door &&
		((pPropEntry->pDoorProperties && (pPropEntry->pDoorProperties->eDoorType == WorldDoorType_QueuedInstance)) ||
		 (!pPropEntry->pDoorProperties && pDefEntry->pDoorProperties && (pDefEntry->pDoorProperties->eDoorType == WorldDoorType_QueuedInstance) && pGroup->bShowDefValues)
		 )) {
		MEExpanderRefreshLabel(&pGroup->pDoorQueueLabel, "Queue Name", "The name of the queue to put the player on.", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
		if (pPropEntry->pDoorProperties) {
			MEExpanderRefreshGlobalDictionaryField(&pGroup->pDoorQueueField, pOrigPropEntry ? pOrigPropEntry->pDoorProperties : NULL, pPropEntry->pDoorProperties, parse_WorldDoorInteractionProperties, "QueueDef", "QueueDef",
										UI_WIDGET(pGroup->pExpander), pGroup->fOffsetX + X_OFFSET_INTERACT_CONTROL, y, 0, 1.0, UIUnitPercentage, 0, pGroup->cbChange, pGroup->cbPreChange, pGroup->pParentData);
			ui_WidgetQueueFreeAndNull(&pGroup->pDoorQueueValueLabel);
		} else {
			MEExpanderRefreshLabel(&pGroup->pDoorQueueValueLabel, REF_STRING_FROM_HANDLE(pDefEntry->pDoorProperties->hQueueDef), NULL, pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL, 0, y, UI_WIDGET(pGroup->pExpander));
			MEFieldSafeDestroy(&pGroup->pDoorQueueField);
		}
		y += STANDARD_ROW_HEIGHT;
	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pDoorQueueLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pDoorQueueValueLabel);
		MEFieldSafeDestroy(&pGroup->pDoorQueueField);
	}
	if (pcClass == pcPooled_Door &&
		((pPropEntry->pDoorProperties && (pPropEntry->pDoorProperties->eDoorType != WorldDoorType_QueuedInstance)) ||
		 (!pPropEntry->pDoorProperties && pDefEntry->pDoorProperties && (pDefEntry->pDoorProperties->eDoorType != WorldDoorType_QueuedInstance) && pGroup->bShowDefValues)
		 )) {
		const char *pcMapName = zmapInfoGetPublicName(NULL);

		if ((pPropEntry->pDoorProperties && pPropEntry->pDoorProperties->eDoorType == WorldDoorType_Keyed) || (!pPropEntry->pDoorProperties && pDefEntry->pDoorProperties && pDefEntry->pDoorProperties->eDoorType == WorldDoorType_Keyed)) {
			GEFreeVariableDefGroup(pGroup->pDoorDestGroup);
			pGroup->pDoorDestGroup = NULL;
			ui_WidgetQueueFreeAndNull(&pGroup->pDefDoor1Label);
			ui_WidgetQueueFreeAndNull(&pGroup->pDefDoor1ValueLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pDefDoor2Label);
			ui_WidgetQueueFreeAndNull(&pGroup->pDefDoor2ValueLabel);

			MEExpanderRefreshLabel(&pGroup->pDoorKeyLabel, "Key Name", "The name which must match the key name on a door key item in order for that item to create a door interaction.  A door key item's key name is the value of the map variable, \"MAP_ENTRY_KEY\" on the map where the item was created.", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
			if(pPropEntry->pDoorProperties) {
				MEExpanderRefreshSimpleField(&pGroup->pDoorKeyField, pOrigPropEntry ? pOrigPropEntry->pDoorProperties : NULL, pPropEntry->pDoorProperties, parse_WorldDoorInteractionProperties, "DoorKey", kMEFieldType_TextEntry,
					UI_WIDGET(pGroup->pExpander), pGroup->fOffsetX + X_OFFSET_INTERACT_CONTROL, y, 0, 1.0, UIUnitPercentage, 0, pGroup->cbChange, pGroup->cbPreChange, pGroup->pParentData);
				ui_WidgetQueueFreeAndNull(&pGroup->pDoorKeyValueLabel);
			} else {
				MEExpanderRefreshLabel(&pGroup->pDoorKeyValueLabel, NULL_TO_EMPTY(pDefEntry->pDoorProperties->pcDoorKey), NULL, pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL, 0, y, UI_WIDGET(pGroup->pExpander));
				MEFieldSafeDestroy(&pGroup->pDoorKeyField);
			}
			y += STANDARD_ROW_HEIGHT;
		}
		else if ((pPropEntry->pDoorProperties && pPropEntry->pDoorProperties->eDoorType == WorldDoorType_JoinTeammate) || (!pPropEntry->pDoorProperties && pDefEntry->pDoorProperties && pDefEntry->pDoorProperties->eDoorType == WorldDoorType_JoinTeammate)) {
			GEFreeVariableDefGroup(pGroup->pDoorDestGroup);
			MEFieldSafeDestroy(&pGroup->pDoorKeyField);
			pGroup->pDoorDestGroup = NULL;
			ui_WidgetQueueFreeAndNull(&pGroup->pDoorKeyLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pDoorKeyValueLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pDefDoor1Label);
			ui_WidgetQueueFreeAndNull(&pGroup->pDefDoor1ValueLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pDefDoor2Label);
			ui_WidgetQueueFreeAndNull(&pGroup->pDefDoor2ValueLabel);
		}
		else {
			if (pPropEntry->pDoorProperties) {
				if (!pGroup->pDoorDestGroup) {
					pGroup->pDoorDestGroup = calloc( 1, sizeof( *pGroup->pDoorDestGroup ));
				}
				y = GEUpdateVariableDefGroupNoName(pGroup->pDoorDestGroup, UI_WIDGET(pGroup->pExpander), &pPropEntry->pDoorProperties->doorDest, SAFE_MEMBER_ADDR2(pOrigPropEntry, pDoorProperties, doorDest), WVAR_MAP_POINT, pcSrcMap, NULL, "Destination", "How the door destination is specified.", pGroup->fOffsetX + X_OFFSET_INTERACT_BASE2, pGroup->fOffsetX + X_OFFSET_INTERACT_CONTROL, y, InteractionFieldChangeCB, InteractionFieldPreChangeCB, pGroup );
				ui_WidgetQueueFreeAndNull(&pGroup->pDefDoor1Label);
				ui_WidgetQueueFreeAndNull(&pGroup->pDefDoor1ValueLabel);
				ui_WidgetQueueFreeAndNull(&pGroup->pDefDoor2Label);
				ui_WidgetQueueFreeAndNull(&pGroup->pDefDoor2ValueLabel);
			} else {
				GEFreeVariableDefGroup(pGroup->pDoorDestGroup);
				pGroup->pDoorDestGroup = NULL;

				if (pDefEntry->pDoorProperties->doorDest.eDefaultType == WVARDEF_SPECIFY_DEFAULT) {
					const char *pcDestMapName = pDefEntry->pDoorProperties->doorDest.pSpecificValue->pcZoneMap;
					const char *pcSpawnName = pDefEntry->pDoorProperties->doorDest.pSpecificValue->pcStringVal;

					MEExpanderRefreshLabel(&pGroup->pDefDoor1Label, "Zone Map", "The target map.", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
					MEExpanderRefreshLabel(&pGroup->pDefDoor1ValueLabel, pcDestMapName ? pcDestMapName : "", NULL, pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL, 0, y, UI_WIDGET(pGroup->pExpander));
					y += STANDARD_ROW_HEIGHT;

					MEExpanderRefreshLabel(&pGroup->pDefDoor2Label, "Spawn Point", "The target spawn point.", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
					MEExpanderRefreshLabel(&pGroup->pDefDoor2ValueLabel, pcSpawnName ? pcSpawnName : "", NULL, pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL, 0, y, UI_WIDGET(pGroup->pExpander));
					y += STANDARD_ROW_HEIGHT;
				} else if (pDefEntry->pDoorProperties->doorDest.eDefaultType == WVARDEF_CHOICE_TABLE) {
					const char *pcChoiceTable = REF_STRING_FROM_HANDLE(pDefEntry->pDoorProperties->doorDest.choice_table);
					const char *pcChoiceName = pDefEntry->pDoorProperties->doorDest.choice_name;

					MEExpanderRefreshLabel(&pGroup->pDefDoor1Label, "Dest Choice Table", "The choice table picking the destination", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
					MEExpanderRefreshLabel(&pGroup->pDefDoor1ValueLabel, pcChoiceTable ? pcChoiceTable : "", NULL, pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL, 0, y, UI_WIDGET(pGroup->pExpander));
					y += STANDARD_ROW_HEIGHT;

					MEExpanderRefreshLabel(&pGroup->pDefDoor2Label, "Choice Field", "The field of the choice table to use.", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
					MEExpanderRefreshLabel(&pGroup->pDefDoor2ValueLabel, pcChoiceName ? pcChoiceName : "", NULL, pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL, 0, y, UI_WIDGET(pGroup->pExpander));
					y += STANDARD_ROW_HEIGHT;
				} else if (pDefEntry->pDoorProperties->doorDest.eDefaultType == WVARDEF_MAP_VARIABLE) {
					const char *pcMapVar = pDefEntry->pDoorProperties->doorDest.map_variable;

					MEExpanderRefreshLabel(&pGroup->pDefDoor1Label, "Dest Map Var", "The variable determining the door destination", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
					MEExpanderRefreshLabel(&pGroup->pDefDoor1ValueLabel, pcMapVar ? pcMapVar : "", NULL, pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL, 0, y, UI_WIDGET(pGroup->pExpander));
					y += STANDARD_ROW_HEIGHT;
					ui_WidgetQueueFreeAndNull(&pGroup->pDefDoor2Label);
					ui_WidgetQueueFreeAndNull(&pGroup->pDefDoor2ValueLabel);
				} else {
					ui_WidgetQueueFreeAndNull(&pGroup->pDefDoor1Label);
					ui_WidgetQueueFreeAndNull(&pGroup->pDefDoor1ValueLabel);
					ui_WidgetQueueFreeAndNull(&pGroup->pDefDoor2Label);
					ui_WidgetQueueFreeAndNull(&pGroup->pDefDoor2ValueLabel);
				}
			}
			MEFieldSafeDestroy(&pGroup->pDoorKeyField);
			ui_WidgetQueueFreeAndNull(&pGroup->pDoorKeyValueLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pDoorKeyLabel);
		}
		
		if (((pPropEntry->pDoorProperties && pPropEntry->pDoorProperties->eDoorType != WorldDoorType_JoinTeammate) || (!pPropEntry->pDoorProperties && pDefEntry->pDoorProperties && pDefEntry->pDoorProperties->eDoorType != WorldDoorType_JoinTeammate)) &&
			(pPropEntry->pDoorProperties || (pDefEntry && pDefEntry->pDoorProperties && pDefEntry->pDoorProperties->bPerPlayer && pGroup->bShowDefValues))) {
			MEExpanderRefreshLabel(&pGroup->pPerPlayerDoorLabel, "Per Player", "Creates one interaction choice for each teammate who meets the Interact condition.  Door must go to an OWNED map.", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
			if (pPropEntry->pDoorProperties) {
				MEExpanderRefreshSimpleField(&pGroup->pPerPlayerDoorField, pOrigPropEntry ? pOrigPropEntry->pDoorProperties : NULL, pPropEntry->pDoorProperties, parse_WorldDoorInteractionProperties, "PerPlayer", kMEFieldType_BooleanCombo,
											UI_WIDGET(pGroup->pExpander), pGroup->fOffsetX + X_OFFSET_INTERACT_CONTROL, y, 0, 80, UIUnitFixed, 0, pGroup->cbChange, pGroup->cbPreChange, pGroup->pParentData);
				ui_WidgetQueueFreeAndNull(&pGroup->pPerPlayerDoorValueLabel);
			} else {
				MEExpanderRefreshLabel(&pGroup->pPerPlayerDoorValueLabel, pDefEntry->pDoorProperties->bPerPlayer ? "True" : "False", NULL, pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL, 0, y, UI_WIDGET(pGroup->pExpander));
				MEFieldSafeDestroy(&pGroup->pPerPlayerDoorField);
			}
			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pPerPlayerDoorLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pPerPlayerDoorValueLabel);
			MEFieldSafeDestroy(&pGroup->pPerPlayerDoorField);
		}

		if (((pPropEntry->pDoorProperties && pPropEntry->pDoorProperties->eDoorType != WorldDoorType_JoinTeammate) || (!pPropEntry->pDoorProperties && pDefEntry->pDoorProperties && pDefEntry->pDoorProperties->eDoorType != WorldDoorType_JoinTeammate)) &&
			(pPropEntry->pDoorProperties || (pDefEntry && pDefEntry->pDoorProperties && pDefEntry->pDoorProperties->bSinglePlayer && pGroup->bShowDefValues))) {
			MEExpanderRefreshLabel(&pGroup->pSinglePlayerDoorLabel, "Single Player", "Creates one interaction choice that is not shared among teammates.  Door must go to an OWNED map.", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
			if (pPropEntry->pDoorProperties) {
				MEExpanderRefreshSimpleField(&pGroup->pSinglePlayerDoorField, pOrigPropEntry ? pOrigPropEntry->pDoorProperties : NULL, pPropEntry->pDoorProperties, parse_WorldDoorInteractionProperties, "SinglePlayer", kMEFieldType_BooleanCombo,
											UI_WIDGET(pGroup->pExpander), pGroup->fOffsetX + X_OFFSET_INTERACT_CONTROL, y, 0, 80, UIUnitFixed, 0, pGroup->cbChange, pGroup->cbPreChange, pGroup->pParentData);
				ui_WidgetQueueFreeAndNull(&pGroup->pSinglePlayerDoorValueLabel);
			} else {
				MEExpanderRefreshLabel(&pGroup->pSinglePlayerDoorValueLabel, pDefEntry->pDoorProperties->bSinglePlayer ? "True" : "False", NULL, pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL, 0, y, UI_WIDGET(pGroup->pExpander));
				MEFieldSafeDestroy(&pGroup->pSinglePlayerDoorField);
			}
			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pSinglePlayerDoorLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pSinglePlayerDoorValueLabel);
			MEFieldSafeDestroy(&pGroup->pSinglePlayerDoorField);
		}

		if (((pPropEntry->pDoorProperties && pPropEntry->pDoorProperties->eDoorType != WorldDoorType_JoinTeammate) || (!pPropEntry->pDoorProperties && pDefEntry->pDoorProperties && pDefEntry->pDoorProperties->eDoorType != WorldDoorType_JoinTeammate)) &&
			(pPropEntry->pDoorProperties || (pDefEntry && pDefEntry->pDoorProperties && pDefEntry->pDoorProperties->bIncludeTeammates && pGroup->bShowDefValues))) {
				MEExpanderRefreshLabel(&pGroup->pIncludeTeammatesDoorLabel, "Include Teammates", "Brings your teammates with you to the destination.", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
				if (pPropEntry->pDoorProperties) {
					MEExpanderRefreshSimpleField(&pGroup->pIncludeTeammatesDoorField, pOrigPropEntry ? pOrigPropEntry->pDoorProperties : NULL, pPropEntry->pDoorProperties, parse_WorldDoorInteractionProperties, "IncludeTeammates", kMEFieldType_BooleanCombo,
						UI_WIDGET(pGroup->pExpander), pGroup->fOffsetX + X_OFFSET_INTERACT_CONTROL, y, 0, 80, UIUnitFixed, 0, pGroup->cbChange, pGroup->cbPreChange, pGroup->pParentData);
					ui_WidgetQueueFreeAndNull(&pGroup->pIncludeTeammatesDoorValueLabel);
				} else {
					MEExpanderRefreshLabel(&pGroup->pIncludeTeammatesDoorValueLabel, pDefEntry->pDoorProperties->bIncludeTeammates ? "True" : "False", NULL, pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL, 0, y, UI_WIDGET(pGroup->pExpander));
					MEFieldSafeDestroy(&pGroup->pIncludeTeammatesDoorField);
				}
				y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pIncludeTeammatesDoorLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pIncludeTeammatesDoorValueLabel);
			MEFieldSafeDestroy(&pGroup->pIncludeTeammatesDoorField);
		}

		if (((pPropEntry->pDoorProperties && pPropEntry->pDoorProperties->eDoorType != WorldDoorType_JoinTeammate) || (!pPropEntry->pDoorProperties && pDefEntry->pDoorProperties && pDefEntry->pDoorProperties->eDoorType != WorldDoorType_JoinTeammate)) &&
			(pPropEntry->pDoorProperties || (pDefEntry && pDefEntry->pDoorProperties && pDefEntry->pDoorProperties->bCollectDestStatus && pGroup->bShowDefValues))) {
			MEExpanderRefreshLabel(&pGroup->pCollectDestStatusLabel, "Collect Status", "Collects summary information about the destination of this door.", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
			if (pPropEntry->pDoorProperties) {
				MEExpanderRefreshSimpleField(&pGroup->pCollectDestStatusField, pOrigPropEntry ? pOrigPropEntry->pDoorProperties : NULL, pPropEntry->pDoorProperties, parse_WorldDoorInteractionProperties, "CollectDestinationStatus", kMEFieldType_BooleanCombo,
											UI_WIDGET(pGroup->pExpander), pGroup->fOffsetX + X_OFFSET_INTERACT_CONTROL, y, 0, 80, UIUnitFixed, 0, pGroup->cbChange, pGroup->cbPreChange, pGroup->pParentData);
				ui_WidgetQueueFreeAndNull(&pGroup->pCollectDestStatusValueLabel);
			} else {
				MEExpanderRefreshLabel(&pGroup->pCollectDestStatusValueLabel, pDefEntry->pDoorProperties->bCollectDestStatus ? "True" : "False", NULL, pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL, 0, y, UI_WIDGET(pGroup->pExpander));
				MEFieldSafeDestroy(&pGroup->pCollectDestStatusField);
			}
			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pCollectDestStatusLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pCollectDestStatusValueLabel);
			MEFieldSafeDestroy(&pGroup->pCollectDestStatusField);
		}

        if (((pPropEntry->pDoorProperties && pPropEntry->pDoorProperties->eDoorType != WorldDoorType_JoinTeammate) || (!pPropEntry->pDoorProperties && pDefEntry->pDoorProperties && pDefEntry->pDoorProperties->eDoorType != WorldDoorType_JoinTeammate)) &&
            (pPropEntry->pDoorProperties || (pDefEntry && pDefEntry->pDoorProperties && pDefEntry->pDoorProperties->bDestinationSameOwner && pGroup->bShowDefValues))) {
                MEExpanderRefreshLabel(&pGroup->pDestinationSameOwnerLabel, "Destination Map Same Owner", "The destination map will have the same owner as this map.", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
                if (pPropEntry->pDoorProperties) {
                    MEExpanderRefreshSimpleField(&pGroup->pDestinationSameOwnerField, pOrigPropEntry ? pOrigPropEntry->pDoorProperties : NULL, pPropEntry->pDoorProperties, parse_WorldDoorInteractionProperties, "DestinationSameOwner", kMEFieldType_BooleanCombo,
                        UI_WIDGET(pGroup->pExpander), pGroup->fOffsetX + X_OFFSET_INTERACT_CONTROL, y, 0, 80, UIUnitFixed, 0, pGroup->cbChange, pGroup->cbPreChange, pGroup->pParentData);
                    ui_WidgetQueueFreeAndNull(&pGroup->pDestinationSameOwnerValueLabel);
                } else {
                    MEExpanderRefreshLabel(&pGroup->pDestinationSameOwnerValueLabel, pDefEntry->pDoorProperties->bDestinationSameOwner ? "True" : "False", NULL, pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL, 0, y, UI_WIDGET(pGroup->pExpander));
                    MEFieldSafeDestroy(&pGroup->pDestinationSameOwnerField);
                }
                y += STANDARD_ROW_HEIGHT;
        } else {
            ui_WidgetQueueFreeAndNull(&pGroup->pDestinationSameOwnerLabel);
            ui_WidgetQueueFreeAndNull(&pGroup->pDestinationSameOwnerValueLabel);
            MEFieldSafeDestroy(&pGroup->pDestinationSameOwnerField);
        }

		if (pPropEntry->pDoorProperties || (pDefEntry && pDefEntry->pDoorProperties && GET_REF(pDefEntry->pDoorProperties->hTransSequence) && pGroup->bShowDefValues)) {
			MEExpanderRefreshLabel(&pGroup->pDoorTransitionLabel, "Transition", "A sequence to play prior to moving to the new location. (optional)", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
			if (pPropEntry->pDoorProperties) {
				MEExpanderRefreshGlobalDictionaryField(&pGroup->pDoorTransitionField, pOrigPropEntry ? pOrigPropEntry->pDoorProperties : NULL, pPropEntry->pDoorProperties, parse_WorldDoorInteractionProperties, "TransitionOverride", "DoorTransitionSequenceDef",
											UI_WIDGET(pGroup->pExpander), pGroup->fOffsetX + X_OFFSET_INTERACT_CONTROL, y, 0, 1.0, UIUnitPercentage, 0, pGroup->cbChange, pGroup->cbPreChange, pGroup->pParentData);
				ui_WidgetQueueFreeAndNull(&pGroup->pDoorTransitionValueLabel);
			} else {
				MEExpanderRefreshLabel(&pGroup->pDoorTransitionValueLabel, REF_STRING_FROM_HANDLE(pDefEntry->pDoorProperties->hTransSequence), NULL, pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL, 0, y, UI_WIDGET(pGroup->pExpander));
				MEFieldSafeDestroy(&pGroup->pDoorTransitionField);
			}
			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pDoorTransitionLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pDoorTransitionValueLabel);
			MEFieldSafeDestroy(&pGroup->pDoorTransitionField);
		}

		if (pPropEntry->pDoorProperties) {
			WorldVariable* pDest = wleAEWorldVariableCalcVariableNonRandom(&pPropEntry->pDoorProperties->doorDest);

			if (pDest && pDest->eType == WVAR_MAP_POINT && pDest->pcZoneMap && pDest->pcZoneMap[0]) {
				iNumVars = eaSize(&pPropEntry->pDoorProperties->eaVariableDefs);

				// Refresh variable groups
				for(i=0; i<iNumVars; ++i) {
					WorldVariableDef *pVarDef = pPropEntry->pDoorProperties->eaVariableDefs[i];
					WorldVariableDef *pOrigVarDef = NULL;

					if (pOrigPropEntry && pOrigPropEntry->pDoorProperties && (i < eaSize(&pOrigPropEntry->pDoorProperties->eaVariableDefs))) {
						pOrigVarDef = pOrigPropEntry->pDoorProperties->eaVariableDefs[i];
					}
					if (i >= eaSize(&pGroup->eaVariableDefGroups)) {
						GEVariableDefGroup *pVarDefGroup = calloc(1, sizeof(GEVariableDefGroup));
						pVarDefGroup->pData = pGroup;
						eaPush(&pGroup->eaVariableDefGroups, pVarDefGroup);
					}

					y = GEUpdateVariableDefGroup(pGroup->eaVariableDefGroups[i], UI_WIDGET(pGroup->pExpander), &pPropEntry->pDoorProperties->eaVariableDefs, pVarDef, pOrigVarDef, pcSrcMap, NULL, pDest->pcZoneMap, i, pGroup->fOffsetX + X_OFFSET_INTERACT_BASE2, pGroup->fOffsetX + X_OFFSET_INTERACT_CONTROL, y, InteractionFieldChangeCB, InteractionFieldPreChangeCB, pGroup);
				}

				// Add button
				if (!pGroup->pVarAddButton) {
					pGroup->pVarAddButton = ui_ButtonCreate("Set Variable", pGroup->fOffsetX+20, y, InteractionAddDoorVariable, pGroup);
					ui_WidgetAddChild(UI_WIDGET(pGroup->pExpander), UI_WIDGET(pGroup->pVarAddButton));
				} else {
					ui_WidgetSetPosition(UI_WIDGET(pGroup->pVarAddButton), pGroup->fOffsetX+20, y);
				}

				y += STANDARD_ROW_HEIGHT;
			}
			for(i=eaSize(&pGroup->eaVarLabels)-1; i>=0; --i) {
				ui_WidgetQueueFreeAndNull(&pGroup->eaVarLabels[i]);
			}
		} else if (pDefEntry && pDefEntry->pDoorProperties && pGroup->bShowDefValues) {
			for(i=0; i<eaSize(&pDefEntry->pDoorProperties->eaVariableDefs); ++i) {
				WorldVariableDef *pVarDef = pDefEntry->pDoorProperties->eaVariableDefs[i];
				estrPrintf(&estrText, "Variable #%d: %s (%s) =", (i+1), pVarDef->pcName, worldVariableTypeToString(pVarDef->eType));
				if (pVarDef->eDefaultType == WVARDEF_SPECIFY_DEFAULT) {
					char *estrText2 = NULL;
					worldVariableToEString(pVarDef->pSpecificValue, &estrText2);
					estrConcatf(&estrText, "%s", estrText2);
					estrDestroy(&estrText2);
				} else if (pVarDef->eDefaultType == WVARDEF_CHOICE_TABLE) {
					estrConcatf(&estrText, "Choice '%s' field '%s'", REF_STRING_FROM_HANDLE(pVarDef->choice_table), pVarDef->choice_name);
				} else if (pVarDef->eDefaultType == WVARDEF_MAP_VARIABLE) {
					estrConcatf(&estrText, "Map Var '%s'", pVarDef->map_variable);
				}
				if (eaSize(&pGroup->eaVarLabels) <= i) {
					eaPush(&pGroup->eaVarLabels, NULL);
				}
				MEExpanderRefreshLabel(&pGroup->eaVarLabels[i], estrText, NULL, pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
				estrDestroy(&estrText);
				y += STANDARD_ROW_HEIGHT;
			}
			for(i=eaSize(&pGroup->eaVarLabels)-1; i>eaSize(&pDefEntry->pDoorProperties->eaVariableDefs); --i) {
				ui_WidgetQueueFreeAndNull(&pGroup->eaVarLabels[i]);
			}
			ui_WidgetQueueFreeAndNull(&pGroup->pVarAddButton);
		} else {
			for(i=eaSize(&pGroup->eaVarLabels)-1; i>=0; --i) {
				ui_WidgetQueueFreeAndNull(&pGroup->eaVarLabels[i]);
			}
			ui_WidgetQueueFreeAndNull(&pGroup->pVarAddButton);
		}
	} else {
		GEFreeVariableDefGroup(pGroup->pDoorDestGroup);
		pGroup->pDoorDestGroup = NULL;
		for(i=eaSize(&pGroup->eaVarLabels)-1; i>=0; --i) {
			ui_WidgetQueueFreeAndNull(&pGroup->eaVarLabels[i]);
		}
		ui_WidgetQueueFreeAndNull(&pGroup->pPerPlayerDoorLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pPerPlayerDoorValueLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pSinglePlayerDoorLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pSinglePlayerDoorValueLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pAllowJoinTeamAtDoorLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pAllowJoinTeamAtDoorValueLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pCollectDestStatusLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pCollectDestStatusValueLabel);
        ui_WidgetQueueFreeAndNull(&pGroup->pDestinationSameOwnerLabel);
        ui_WidgetQueueFreeAndNull(&pGroup->pDestinationSameOwnerValueLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pDefDoor1Label);
		ui_WidgetQueueFreeAndNull(&pGroup->pDefDoor1ValueLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pDefDoor2Label);
		ui_WidgetQueueFreeAndNull(&pGroup->pDefDoor2ValueLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pDoorKeyLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pDoorKeyValueLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pDoorTransitionLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pDoorTransitionValueLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pVarAddButton);
		ui_WidgetQueueFreeAndNull(&pGroup->pIncludeTeammatesDoorLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pIncludeTeammatesDoorValueLabel);
		MEFieldSafeDestroy(&pGroup->pIncludeTeammatesDoorField);
		MEFieldSafeDestroy(&pGroup->pDoorTransitionField);
		MEFieldSafeDestroy(&pGroup->pDoorKeyField);
		MEFieldSafeDestroy(&pGroup->pPerPlayerDoorField);
		MEFieldSafeDestroy(&pGroup->pSinglePlayerDoorField);
		MEFieldSafeDestroy(&pGroup->pCollectDestStatusField);
        MEFieldSafeDestroy(&pGroup->pDestinationSameOwnerField);
		MEFieldSafeDestroy(&pGroup->pAllowJoinTeamAtDoorField);
	}
	// Clear any variables if none can be set
	{
		WorldVariable* pDest = wleAEWorldVariableCalcVariableNonRandom(SAFE_MEMBER_ADDR(pPropEntry->pDoorProperties, doorDest));
		if (!(pDest && pDest->eType == WVAR_MAP_POINT && pDest->pcZoneMap && pDest->pcZoneMap[0])) {
			if (pPropEntry->pDoorProperties) {
				eaDestroyStruct(&pPropEntry->pDoorProperties->eaVariableDefs, parse_WorldVariableDef);
			}
			ui_WidgetQueueFreeAndNull(&pGroup->pVarAddButton);
		}
	}
	// Free unusued variable groups
	for(i=eaSize(&pGroup->eaVariableDefGroups)-1; i>=iNumVars; --i) {
		GEFreeVariableDefGroup(pGroup->eaVariableDefGroups[i]);
		eaRemove(&pGroup->eaVariableDefGroups, i);
	}

	if (!pDefEntry && pcClass) {
		MEExpanderRefreshLabel(&pGroup->pGeneralSectionLabel, "General Properties", NULL, pGroup->fOffsetX, 0, y, UI_WIDGET(pGroup->pExpander));
		y += STANDARD_ROW_HEIGHT;
	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pGeneralSectionLabel);
	}

	// Interact Conditions
	if (pDefEntry) {
		if (!pGroup->pInteractButton) {
			pGroup->pInteractButton = ui_CheckButtonCreate(pGroup->fOffsetX, y, "Custom Interact Condition",
														   (pPropEntry->pInteractCond || pPropEntry->pSuccessCond || pPropEntry->pAttemptableCond) );
			ui_CheckButtonSetToggledCallback(pGroup->pInteractButton, InteractionInteractToggled, pGroup);
			ui_WidgetSetTooltipString(UI_WIDGET(pGroup->pInteractButton), "Adds custom interact conditions.");
			ui_ExpanderAddChild(pGroup->pExpander, pGroup->pInteractButton);
		} else {
			ui_WidgetSetPosition(UI_WIDGET(pGroup->pInteractButton), pGroup->fOffsetX, y);
		}
		ui_CheckButtonSetState(pGroup->pInteractButton, pPropEntry->bOverrideInteract);
		y += STANDARD_ROW_HEIGHT;
	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pInteractButton);
	}
	if ((pcClass == pcPooled_Clickable) ||
		(pcClass == pcPooled_Contact) ||
		(pcClass == pcPooled_CraftingStation) ||
		(pcClass == pcPooled_Door) || 
		(pcClass == pcPooled_Gate) ||
		(pcClass == pcPooled_FromDefinition && (pPropEntry->pInteractCond || pPropEntry->pSuccessCond || pPropEntry->pAttemptableCond)) ) {

		if ((!pDefEntry && pcClass != pcPooled_FromDefinition) || pPropEntry->bOverrideInteract || (pcClass == pcPooled_FromDefinition && pPropEntry->pInteractCond) ) {
			MEExpanderRefreshLabel(&pGroup->pInteractCondLabel, "Interact Expr", INTERACT_INTERACTCOND_TOOLTIP, pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
			MEExpanderRefreshSimpleField(&pGroup->pInteractCondField, pOrigPropEntry, pPropEntry, parse_WorldInteractionPropertyEntry, "InteractCondition", kMEFieldTypeEx_Expression,
									UI_WIDGET(pGroup->pExpander), pGroup->fOffsetX + X_OFFSET_INTERACT_CONTROL, y, 0, 1.0, UIUnitPercentage, 0, pGroup->cbChange, pGroup->cbPreChange, pGroup->pParentData);
			ui_WidgetQueueFreeAndNull(&pGroup->pInteractCondValueLabel);
			y += STANDARD_ROW_HEIGHT;
		} else if(pGroup->bShowDefValues){
			char *pcText = exprGetCompleteString(pDefEntry->pInteractCond);
			MEExpanderRefreshLabel(&pGroup->pInteractCondLabel, "Interact Expr", INTERACT_INTERACTCOND_TOOLTIP, pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
			if (EMPTY_TO_NULL(pcText)) {
				MEExpanderRefreshLabel(&pGroup->pInteractCondValueLabel, pcText, NULL, pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL, 0, y, UI_WIDGET(pGroup->pExpander));
			}
			MEFieldSafeDestroy(&pGroup->pInteractCondField);
			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pInteractCondLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pInteractCondValueLabel);
			MEFieldSafeDestroy(&pGroup->pInteractCondField);
		}

		if ((!pDefEntry && pcClass != pcPooled_FromDefinition) || pPropEntry->bOverrideInteract || (pcClass == pcPooled_FromDefinition && pPropEntry->pAttemptableCond)) {
			MEExpanderRefreshLabel(&pGroup->pAttemptableCondLabel, "Usable Expr", INTERACT_ATTEMPTABLECOND_TOOLTIP, pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
			MEExpanderRefreshSimpleField(&pGroup->pAttemptableCondField, pOrigPropEntry, pPropEntry, parse_WorldInteractionPropertyEntry, "AttemptableCondition", kMEFieldTypeEx_Expression,
									UI_WIDGET(pGroup->pExpander), pGroup->fOffsetX + X_OFFSET_INTERACT_CONTROL, y, 0, 1.0, UIUnitPercentage, 0, pGroup->cbChange, pGroup->cbPreChange, pGroup->pParentData);
			ui_WidgetQueueFreeAndNull(&pGroup->pAttemptableCondValueLabel);
			y += STANDARD_ROW_HEIGHT;
		} else if(pGroup->bShowDefValues){
			char *pcText = exprGetCompleteString(pDefEntry->pAttemptableCond);
			MEExpanderRefreshLabel(&pGroup->pAttemptableCondLabel, "Usable Expr", INTERACT_ATTEMPTABLECOND_TOOLTIP, pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
			if (EMPTY_TO_NULL(pcText)) {
				MEExpanderRefreshLabel(&pGroup->pAttemptableCondValueLabel, pcText, NULL, pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL, 0, y, UI_WIDGET(pGroup->pExpander));
			}
			MEFieldSafeDestroy(&pGroup->pAttemptableCondField);
			y += STANDARD_ROW_HEIGHT;
		}else {
			ui_WidgetQueueFreeAndNull(&pGroup->pAttemptableCondLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pAttemptableCondValueLabel);
			MEFieldSafeDestroy(&pGroup->pAttemptableCondField);
		}
		
		if ((!pDefEntry && pcClass != pcPooled_FromDefinition) || pPropEntry->bOverrideInteract || (pcClass == pcPooled_FromDefinition && pPropEntry->pSuccessCond)) {
			MEExpanderRefreshLabel(&pGroup->pSuccessCondLabel, "Success Expr", INTERACT_SUCCESSCOND_TOOLTIP, pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
			MEExpanderRefreshSimpleField(&pGroup->pSuccessCondField, pOrigPropEntry, pPropEntry, parse_WorldInteractionPropertyEntry, "SuccessCondition", kMEFieldTypeEx_Expression,
									UI_WIDGET(pGroup->pExpander), pGroup->fOffsetX + X_OFFSET_INTERACT_CONTROL, y, 0, 1.0, UIUnitPercentage, 0, pGroup->cbChange, pGroup->cbPreChange, pGroup->pParentData);
			ui_WidgetQueueFreeAndNull(&pGroup->pSuccessCondValueLabel);
			y += STANDARD_ROW_HEIGHT;
		} else if(pGroup->bShowDefValues){
			char *pcText = exprGetCompleteString(pDefEntry->pSuccessCond);
			MEExpanderRefreshLabel(&pGroup->pSuccessCondLabel, "Success Expr", INTERACT_SUCCESSCOND_TOOLTIP, pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
			if (EMPTY_TO_NULL(pcText)) {
				MEExpanderRefreshLabel(&pGroup->pSuccessCondValueLabel, pcText, NULL, pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL, 0, y, UI_WIDGET(pGroup->pExpander));
			}
			MEFieldSafeDestroy(&pGroup->pSuccessCondField);
			y += STANDARD_ROW_HEIGHT;
		}else {
			ui_WidgetQueueFreeAndNull(&pGroup->pSuccessCondLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pSuccessCondValueLabel);
			MEFieldSafeDestroy(&pGroup->pSuccessCondField);
		}



	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pInteractCondLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pInteractCondValueLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pSuccessCondLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pSuccessCondValueLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pAttemptableCondLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pAttemptableCondValueLabel);
		MEFieldSafeDestroy(&pGroup->pInteractCondField);
		MEFieldSafeDestroy(&pGroup->pSuccessCondField);
		MEFieldSafeDestroy(&pGroup->pAttemptableCondField);
	}
	
	// Visible Expression
	if ((pGroup->eType == InteractionDefType_Node) &&
		(pPropEntry->pcInteractionClass == pcPooled_FromDefinition)
		) {
		if (!pGroup->pVisibleButton) {
			pGroup->pVisibleButton = ui_CheckButtonCreate(pGroup->fOffsetX, y, "Custom Visibility Condition", false);
			ui_CheckButtonSetToggledCallback(pGroup->pVisibleButton, InteractionVisibilityToggled, pGroup);
			ui_WidgetSetTooltipString(UI_WIDGET(pGroup->pVisibleButton), "Adds custom visibility conditions.");
			ui_ExpanderAddChild(pGroup->pExpander, pGroup->pVisibleButton);
		} else {
			ui_WidgetSetPosition(UI_WIDGET(pGroup->pVisibleButton), pGroup->fOffsetX, y);
		}
		ui_CheckButtonSetState(pGroup->pVisibleButton, pPropEntry->bOverrideVisibility);
		y += STANDARD_ROW_HEIGHT;
	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pVisibleButton);
	}
	if ((pGroup->eType == InteractionDefType_Node) &&
		((pcClass == pcPooled_Clickable) ||
		 (pcClass == pcPooled_Contact) ||
		 (pcClass == pcPooled_CraftingStation) ||
		 (pcClass == pcPooled_Destructible) ||
		 (pcClass == pcPooled_Door) ||
		 (pcClass == pcPooled_Gate) ||
		 (pcClass == pcPooled_FromDefinition && pPropEntry->pVisibleExpr)
		 )) {
		if ((!pDefEntry && pcClass != pcPooled_FromDefinition) || pPropEntry->bOverrideVisibility || (pcClass == pcPooled_FromDefinition && pPropEntry->pVisibleExpr) ) {
			MEExpanderRefreshLabel(&pGroup->pVisibleExprLabel, "Visible Expr", "When true, the node is visible.  Otherwise it disappears.  If this expression has a value, it always supersedes any hide on cooldown.", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
			MEExpanderRefreshSimpleField(&pGroup->pVisibleExprField, pOrigPropEntry, pPropEntry, parse_WorldInteractionPropertyEntry, "VisibleExpression", kMEFieldTypeEx_Expression,
									UI_WIDGET(pGroup->pExpander), pGroup->fOffsetX + X_OFFSET_INTERACT_CONTROL, y, 0, 1.0, UIUnitPercentage, 0, pGroup->cbChange, pGroup->cbPreChange, pGroup->pParentData);
			ui_WidgetQueueFreeAndNull(&pGroup->pVisibleExprValueLabel);
			y += STANDARD_ROW_HEIGHT;
		} else if(pGroup->bShowDefValues) {
			char *pcText = exprGetCompleteString(pDefEntry->pVisibleExpr);
			MEExpanderRefreshLabel(&pGroup->pVisibleExprLabel, "Visible Expr", "When true, the node is visible.  Otherwise it disappears.  If this expression has a value, it always supersedes any hide on cooldown.", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
			if (EMPTY_TO_NULL(pcText)) {
				MEExpanderRefreshLabel(&pGroup->pVisibleExprValueLabel, pcText, NULL, pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL, 0, y, UI_WIDGET(pGroup->pExpander));
			}
			MEFieldSafeDestroy(&pGroup->pVisibleExprField);
			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pVisibleExprLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pVisibleExprValueLabel);
			MEFieldSafeDestroy(&pGroup->pVisibleExprField);
		}
	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pVisibleExprLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pVisibleExprValueLabel);
		MEFieldSafeDestroy(&pGroup->pVisibleExprField);
	}

	// Base
	if (pDefEntry || (pcClass == pcPooled_FromDefinition && (EMPTY_TO_NULL(pPropEntry->pcCategoryName) || pGroup->bPriorityChanged)) ) {
		if (!pGroup->pCategoryButton) {
			pGroup->pCategoryButton = ui_CheckButtonCreate(pGroup->fOffsetX, y, "Custom Category Information", (pcClass == pcPooled_FromDefinition && (EMPTY_TO_NULL(pPropEntry->pcCategoryName) || pGroup->bPriorityChanged)));
			ui_CheckButtonSetToggledCallback(pGroup->pCategoryButton, InteractionCategoryToggled, pGroup);
			ui_WidgetSetTooltipString(UI_WIDGET(pGroup->pCategoryButton), "Adds custom category information.");
			ui_ExpanderAddChild(pGroup->pExpander, pGroup->pCategoryButton);
		} else {
			ui_WidgetSetPosition(UI_WIDGET(pGroup->pCategoryButton), pGroup->fOffsetX, y);
		}
		ui_CheckButtonSetState(pGroup->pCategoryButton, pPropEntry->bOverrideCategoryPriority);
		y += STANDARD_ROW_HEIGHT;
	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pCategoryButton);
	}
	if ((pcClass == pcPooled_Clickable) ||
		(pcClass == pcPooled_Contact) ||
		(pcClass == pcPooled_CraftingStation) ||
		(pcClass == pcPooled_Door) ||
		(pcClass == pcPooled_Gate) ||
		(pcClass == pcPooled_FromDefinition && (EMPTY_TO_NULL(pPropEntry->pcCategoryName) || pGroup->bPriorityChanged)) ) {

		if ((!pDefEntry && pcClass != pcPooled_FromDefinition) || pPropEntry->bOverrideCategoryPriority || (pcClass == pcPooled_FromDefinition && EMPTY_TO_NULL(pPropEntry->pcCategoryName)) ) {
			MEExpanderRefreshLabel(&pGroup->pCategoryLabel, "Category", "The category for this interaction.  This affects UI display.", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
			MEExpanderRefreshDataField(&pGroup->pCategoryField, pOrigPropEntry, pPropEntry, parse_WorldInteractionPropertyEntry, "Category", &s_eaCategoryNames, true,
										UI_WIDGET(pGroup->pExpander), pGroup->fOffsetX + X_OFFSET_INTERACT_CONTROL, y, 0, 140, UIUnitFixed, 0, pGroup->cbChange, pGroup->cbPreChange, pGroup->pParentData);
			ui_WidgetQueueFreeAndNull(&pGroup->pCategoryValueLabel);
			y += STANDARD_ROW_HEIGHT;
		} else if(pGroup->bShowDefValues && pDefEntry && pDefEntry->pcCategoryName) {
			MEExpanderRefreshLabel(&pGroup->pCategoryLabel, "Category", "The category for this interaction.  This affects UI display.", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
			MEExpanderRefreshLabel(&pGroup->pCategoryValueLabel, pDefEntry->pcCategoryName, NULL, pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL, 0, y, UI_WIDGET(pGroup->pExpander));
			MEFieldSafeDestroy(&pGroup->pCategoryField);
			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pCategoryLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pCategoryValueLabel);
			MEFieldSafeDestroy(&pGroup->pCategoryField);
		}

		if ((!pDefEntry && pcClass != pcPooled_FromDefinition) || pPropEntry->bOverrideCategoryPriority || (pcClass == pcPooled_FromDefinition && pGroup->bPriorityChanged)) {
			MEExpanderRefreshLabel(&pGroup->pPriorityLabel, "Priority", "The priority for this interaction.  This affects UI display.", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
			MEExpanderRefreshEnumField(&pGroup->pPriorityField, pOrigPropEntry, pPropEntry, parse_WorldInteractionPropertyEntry, "Priority", WorldOptionalActionPriorityEnum,
									UI_WIDGET(pGroup->pExpander), pGroup->fOffsetX + X_OFFSET_INTERACT_CONTROL, y, 0, 100, UIUnitFixed, 0, InteractionPropertiesPriorityChangeCB, InteractionPropertiesFieldPreChangeCB, pGroup);
			ui_WidgetQueueFreeAndNull(&pGroup->pPriorityValueLabel);
			y += STANDARD_ROW_HEIGHT;
		} else if(pGroup->bShowDefValues) {
			MEExpanderRefreshLabel(&pGroup->pPriorityLabel, "Priority", "The priority for this interaction.  This affects UI display.", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
			MEExpanderRefreshLabel(&pGroup->pPriorityValueLabel, StaticDefineIntRevLookup(WorldOptionalActionPriorityEnum, pDefEntry->iPriority), NULL, pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL, 0, y, UI_WIDGET(pGroup->pExpander));
			MEFieldSafeDestroy(&pGroup->pPriorityField);
			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pPriorityLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pPriorityValueLabel);
			MEFieldSafeDestroy(&pGroup->pPriorityField);
		}

		if (!pDefEntry && pcClass != pcPooled_FromDefinition) {
			MEExpanderRefreshLabel(&pGroup->pAutoExecLabel, "Auto Execute", "This determines whether this interaction will execute immediately when an entity is in range and meets interaction requirements.", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
			MEExpanderRefreshSimpleField(&pGroup->pAutoExecField, pOrigPropEntry, pPropEntry, parse_WorldInteractionPropertyEntry, "AutoExecute", kMEFieldType_BooleanCombo,
				UI_WIDGET(pGroup->pExpander), pGroup->fOffsetX + X_OFFSET_INTERACT_CONTROL, y, 0, 100, UIUnitFixed, 0, InteractionPropertiesFieldChangeCB, InteractionPropertiesFieldPreChangeCB, pGroup);
			ui_WidgetQueueFreeAndNull(&pGroup->pAutoExecValueLabel);
			y += STANDARD_ROW_HEIGHT;
		}
		else if (pGroup->bShowDefValues) {
			MEExpanderRefreshLabel(&pGroup->pAutoExecLabel, "Auto Execute", "This determines whether this interaction will execute immediately when an entity is in range and meets interaction requirements.", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
			MEExpanderRefreshLabel(&pGroup->pAutoExecValueLabel, pDefEntry->bAutoExecute ? "True" : "False", NULL, pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL, 0, y, UI_WIDGET(pGroup->pExpander));
			MEFieldSafeDestroy(&pGroup->pAutoExecField);
			y += STANDARD_ROW_HEIGHT;
		}
		else {
			ui_WidgetQueueFreeAndNull(&pGroup->pAutoExecLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pAutoExecValueLabel);
			MEFieldSafeDestroy(&pGroup->pAutoExecField);
		}

		if (!pDefEntry && pcClass != pcPooled_FromDefinition) {
			MEExpanderRefreshLabel(&pGroup->pDisablePowersInterruptLabel, "No Powers Interrupt", "This determines whether this interaction will interrupt powers.", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
			MEExpanderRefreshSimpleField(&pGroup->pDisablePowersInterruptField, pOrigPropEntry, pPropEntry, parse_WorldInteractionPropertyEntry, "DisablePowersInterrupt", kMEFieldType_BooleanCombo,
				UI_WIDGET(pGroup->pExpander), pGroup->fOffsetX + X_OFFSET_INTERACT_CONTROL, y, 0, 100, UIUnitFixed, 0, InteractionPropertiesFieldChangeCB, InteractionPropertiesFieldPreChangeCB, pGroup);
			ui_WidgetQueueFreeAndNull(&pGroup->pDisablePowersInterruptValueLabel);
			y += STANDARD_ROW_HEIGHT;
		}
		else if (pGroup->bShowDefValues) {
			MEExpanderRefreshLabel(&pGroup->pDisablePowersInterruptLabel, "No Powers Interrupt", "This determines whether this interaction will interrupt powers.", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
			MEExpanderRefreshLabel(&pGroup->pDisablePowersInterruptValueLabel, pDefEntry->bDisablePowersInterrupt ? "True" : "False", NULL, pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL, 0, y, UI_WIDGET(pGroup->pExpander));
			MEFieldSafeDestroy(&pGroup->pDisablePowersInterruptField);
			y += STANDARD_ROW_HEIGHT;
		}
		else {
			ui_WidgetQueueFreeAndNull(&pGroup->pDisablePowersInterruptLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pDisablePowersInterruptValueLabel);
			MEFieldSafeDestroy(&pGroup->pDisablePowersInterruptField);
		}

		if (!gConf.bAlwaysAllowInteractsInCombat)
		{
			if (!pDefEntry && pcClass != pcPooled_FromDefinition) {
				MEExpanderRefreshLabel(&pGroup->pAllowDuringCombatLabel, "Allow During Combat", "Is this interactable available during combat?", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
				MEExpanderRefreshSimpleField(&pGroup->pAllowDuringCombatField, pOrigPropEntry, pPropEntry, parse_WorldInteractionPropertyEntry, "AllowDuringCombat", kMEFieldType_BooleanCombo,
					UI_WIDGET(pGroup->pExpander), pGroup->fOffsetX + X_OFFSET_INTERACT_CONTROL, y, 0, 100, UIUnitFixed, 0, InteractionPropertiesFieldChangeCB, InteractionPropertiesFieldPreChangeCB, pGroup);
				ui_WidgetQueueFreeAndNull(&pGroup->pAllowDuringCombatValueLabel);
				y += STANDARD_ROW_HEIGHT;
			}
			else if (pGroup->bShowDefValues) {
				MEExpanderRefreshLabel(&pGroup->pAllowDuringCombatLabel, "Allow During Combat", "Is this interactable available during combat?", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
				MEExpanderRefreshLabel(&pGroup->pAllowDuringCombatValueLabel, pDefEntry->bAllowDuringCombat ? "True" : "False", NULL, pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL, 0, y, UI_WIDGET(pGroup->pExpander));
				MEFieldSafeDestroy(&pGroup->pAllowDuringCombatField);
				y += STANDARD_ROW_HEIGHT;
			}
			else {
				ui_WidgetQueueFreeAndNull(&pGroup->pAllowDuringCombatLabel);
				ui_WidgetQueueFreeAndNull(&pGroup->pAllowDuringCombatValueLabel);
				MEFieldSafeDestroy(&pGroup->pAllowDuringCombatField);
			}
		}

		if (!pDefEntry && pcClass != pcPooled_FromDefinition) {
			MEExpanderRefreshLabel(&pGroup->pExclusiveLabel, "Exclusive", "This determines whether only one person can interact with this entity at a time.", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
			if (!pGroup->pExclusiveButton) {
				pGroup->pExclusiveButton = ui_CheckButtonCreate(pGroup->fOffsetX + X_OFFSET_INTERACT_CONTROL, y, "", false);
				ui_CheckButtonSetToggledCallback(pGroup->pExclusiveButton, InteractionExclusiveToggled, pGroup);
				ui_ExpanderAddChild(pGroup->pExpander, pGroup->pExclusiveButton);
			} else {
				ui_WidgetSetPosition(UI_WIDGET(pGroup->pExclusiveButton), pGroup->fOffsetX + X_OFFSET_INTERACT_CONTROL, y);
			}
			ui_CheckButtonSetState(pGroup->pExclusiveButton, interaction_EntryGetExclusive(pGroup->pProperties, pGroup->eType == InteractionDefType_Node));
			y += STANDARD_ROW_HEIGHT;
		}
		else if (pGroup->bShowDefValues) {
			MEExpanderRefreshLabel(&pGroup->pExclusiveLabel, "Exclusive", "This determines whether only one person can interact with this entity at a time.", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
			MEExpanderRefreshLabel(&pGroup->pExclusiveValueLabel, interaction_EntryGetExclusive(pDefEntry, false) ? "True" : "False", NULL, pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL, 0, y, UI_WIDGET(pGroup->pExpander));
			ui_WidgetQueueFreeAndNull(&pGroup->pExclusiveButton);
			y += STANDARD_ROW_HEIGHT;
		}
		else {
			ui_WidgetQueueFreeAndNull(&pGroup->pExclusiveLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pExclusiveValueLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pExclusiveButton);
		}
	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pCategoryLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pCategoryValueLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pPriorityLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pPriorityValueLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pAutoExecLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pAutoExecValueLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pDisablePowersInterruptLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pDisablePowersInterruptValueLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pAllowDuringCombatLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pAllowDuringCombatValueLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pExclusiveLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pExclusiveButton);
		ui_WidgetQueueFreeAndNull(&pGroup->pExclusiveValueLabel);
		MEFieldSafeDestroy(&pGroup->pCategoryField);
		MEFieldSafeDestroy(&pGroup->pPriorityField);
		MEFieldSafeDestroy(&pGroup->pAutoExecField);
		MEFieldSafeDestroy(&pGroup->pDisablePowersInterruptField);
		MEFieldSafeDestroy(&pGroup->pAllowDuringCombatField);
	}

	// Time
	if (!pDefEntry &&
		((pcClass == pcPooled_Clickable) ||
		 (pcClass == pcPooled_Gate) ||
		 (pcClass == pcPooled_Contact) ||
		 (pcClass == pcPooled_Door))) {
		if (!pGroup->pTimeButton) {
			pGroup->pTimeButton = ui_CheckButtonCreate(pGroup->fOffsetX, y, "Custom Timing", false);
			ui_CheckButtonSetToggledCallback(pGroup->pTimeButton, InteractionTimeToggled, pGroup);
			ui_WidgetSetTooltipString(UI_WIDGET(pGroup->pTimeButton), "Adds custom timing.  Otherwise these defaults are used: no use time, no active time, no cooldown, interrupted on move/power/damage.");
			ui_ExpanderAddChild(pGroup->pExpander, pGroup->pTimeButton);
		} else {
			ui_WidgetSetPosition(UI_WIDGET(pGroup->pTimeButton), pGroup->fOffsetX, y);
		}
		ui_CheckButtonSetState(pGroup->pTimeButton, (pPropEntry->pTimeProperties != NULL));
		ui_WidgetQueueFreeAndNull(&pGroup->pTimeSectionLabel);
		y += STANDARD_ROW_HEIGHT;
	} else if (pDefEntry && pGroup->bShowDefValues && pDefEntry->pTimeProperties) {
		MEExpanderRefreshLabel(&pGroup->pTimeSectionLabel, "Timing", "Timing settings defined by the interaction def.", pGroup->fOffsetX, 0, y, UI_WIDGET(pGroup->pExpander));
		ui_WidgetQueueFreeAndNull(&pGroup->pTimeButton);
		y += STANDARD_ROW_HEIGHT;
	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pTimeSectionLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pTimeButton);
	}
	if ((pPropEntry->pTimeProperties || (pDefEntry && pDefEntry->pTimeProperties && pGroup->bShowDefValues)) &&
		((pcClass == pcPooled_Clickable) ||
		 (pcClass == pcPooled_Gate) ||
		 (pcClass == pcPooled_Contact) ||
		 (pcClass == pcPooled_Door))) {

		if (pPropEntry->pTimeProperties || (pDefEntry && pDefEntry->pTimeProperties && pDefEntry->pTimeProperties->fUseTime)) {
			MEExpanderRefreshLabel(&pGroup->pUseTimeLabel, "Use Time", "The time (in secs) required to interact.  Zero means no wait.", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
			if (pPropEntry->pTimeProperties) {
				MEExpanderRefreshSimpleField(&pGroup->pUseTimeField, pOrigPropEntry && pOrigPropEntry->pTimeProperties ? pOrigPropEntry->pTimeProperties : NULL, pPropEntry->pTimeProperties, parse_WorldTimeInteractionProperties, "UseTime", kMEFieldType_TextEntry,
										UI_WIDGET(pGroup->pExpander), pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL2, y, 0, 80, UIUnitFixed, 0, pGroup->cbChange, pGroup->cbPreChange, pGroup->pParentData);
				ui_WidgetQueueFreeAndNull(&pGroup->pUseTimeValueLabel);
			} else {
				sprintf(buf, "%g", pDefEntry->pTimeProperties->fUseTime);
				MEExpanderRefreshLabel(&pGroup->pUseTimeValueLabel, buf, NULL, pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL2, 0, y, UI_WIDGET(pGroup->pExpander));
				MEFieldSafeDestroy(&pGroup->pUseTimeField);
			}
			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pUseTimeLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pUseTimeValueLabel);
			MEFieldSafeDestroy(&pGroup->pUseTimeField);
		}

		if (pPropEntry->pTimeProperties || (pDefEntry && pDefEntry->pTimeProperties && pDefEntry->pTimeProperties->fActiveTime)) {
			MEExpanderRefreshLabel(&pGroup->pActiveTimeLabel, "Active Time", "The time (in secs) after interacting before cooldown starts.  Zero means no wait.", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
			if (pPropEntry->pTimeProperties) {
				MEExpanderRefreshSimpleField(&pGroup->pActiveTimeField, pOrigPropEntry && pOrigPropEntry->pTimeProperties ? pOrigPropEntry->pTimeProperties : NULL, pPropEntry->pTimeProperties, parse_WorldTimeInteractionProperties, "ActiveTime", kMEFieldType_TextEntry,
										UI_WIDGET(pGroup->pExpander), pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL2, y, 0, 80, UIUnitFixed, 0, pGroup->cbChange, pGroup->cbPreChange, pGroup->pParentData);
				ui_WidgetQueueFreeAndNull(&pGroup->pActiveTimeValueLabel);
			} else {
				sprintf(buf, "%g", pDefEntry->pTimeProperties->fActiveTime);
				MEExpanderRefreshLabel(&pGroup->pActiveTimeValueLabel, buf, NULL, pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL2, 0, y, UI_WIDGET(pGroup->pExpander));
				MEFieldSafeDestroy(&pGroup->pActiveTimeField);
			}
			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pActiveTimeLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pActiveTimeValueLabel);
			MEFieldSafeDestroy(&pGroup->pActiveTimeField);
		}

		if (pPropEntry->pTimeProperties || (pDefEntry && pDefEntry->pTimeProperties && pDefEntry->pTimeProperties->bNoRespawn)) {
			MEExpanderRefreshLabel(&pGroup->pNoRespawnLabel, "No Respawn", "If true, the object will never end cooldown.", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
			if (pPropEntry->pTimeProperties) {
				MEExpanderRefreshSimpleField(&pGroup->pNoRespawnField, pOrigPropEntry && pOrigPropEntry->pTimeProperties ? pOrigPropEntry->pTimeProperties : NULL, pPropEntry->pTimeProperties, parse_WorldTimeInteractionProperties, "NoRespawn", kMEFieldType_BooleanCombo,
										UI_WIDGET(pGroup->pExpander), pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL2, y, 0, 80, UIUnitFixed, 0, pGroup->cbChange, pGroup->cbPreChange, pGroup->pParentData);
				ui_WidgetQueueFreeAndNull(&pGroup->pNoRespawnValueLabel);
			} else {
				MEExpanderRefreshLabel(&pGroup->pNoRespawnValueLabel, pDefEntry->pTimeProperties->bNoRespawn ? "True" : "False", NULL, pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL2, 0, y, UI_WIDGET(pGroup->pExpander));
				MEFieldSafeDestroy(&pGroup->pNoRespawnField);
			}
			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pNoRespawnLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pNoRespawnValueLabel);
			MEFieldSafeDestroy(&pGroup->pNoRespawnField);
		}

		if ((pPropEntry->pTimeProperties && !pPropEntry->pTimeProperties->bNoRespawn) ||
			(pDefEntry && pDefEntry->pTimeProperties && !pDefEntry->pTimeProperties->bNoRespawn && (pDefEntry->pTimeProperties->eCooldownTime != WorldCooldownTime_None))) {
			MEExpanderRefreshLabel(&pGroup->pCooldownTimeLabel, "Cooldown Time", "The time (in seconds) after when interaction is not allowed after it stops being active.  None=0, Short=30, Medium=300, Long=3600.", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
			if (pPropEntry->pTimeProperties) {
				MEExpanderRefreshEnumField(&pGroup->pCooldownTimeField, pOrigPropEntry && pOrigPropEntry->pTimeProperties ? pOrigPropEntry->pTimeProperties : NULL, pPropEntry->pTimeProperties, parse_WorldTimeInteractionProperties, "CooldownTime", WorldCooldownTimeEnum,
										UI_WIDGET(pGroup->pExpander), pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL2, y, 0, 100, UIUnitFixed, 0, pGroup->cbChange, pGroup->cbPreChange, pGroup->pParentData);
				ui_WidgetQueueFreeAndNull(&pGroup->pCooldownTimeValueLabel);
			} else {
				MEExpanderRefreshLabel(&pGroup->pCooldownTimeValueLabel, StaticDefineIntRevLookup(WorldCooldownTimeEnum, pDefEntry->pTimeProperties->eCooldownTime), NULL, pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL2, 0, y, UI_WIDGET(pGroup->pExpander));
				MEFieldSafeDestroy(&pGroup->pCooldownTimeField);
			}
			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pCooldownTimeLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pCooldownTimeValueLabel);
			MEFieldSafeDestroy(&pGroup->pCooldownTimeField);
		}

		if ((pPropEntry->pTimeProperties && !pPropEntry->pTimeProperties->bNoRespawn && (pPropEntry->pTimeProperties->eCooldownTime == WorldCooldownTime_Custom)) ||
			(pDefEntry && pDefEntry->pTimeProperties && !pDefEntry->pTimeProperties->bNoRespawn && (pDefEntry->pTimeProperties->eCooldownTime == WorldCooldownTime_Custom))) {
			MEExpanderRefreshLabel(&pGroup->pCustomCooldownLabel, "Custom Time", "The time (in seconds) after when interaction is not allowed after it stops being active.  Zero means no cooldown.", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
			if (pPropEntry->pTimeProperties) {
				MEExpanderRefreshSimpleField(&pGroup->pCustomCooldownField, pOrigPropEntry && pOrigPropEntry->pTimeProperties ? pOrigPropEntry->pTimeProperties : NULL, pPropEntry->pTimeProperties, parse_WorldTimeInteractionProperties, "CustomCooldownTime", kMEFieldType_TextEntry,
										UI_WIDGET(pGroup->pExpander), pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL2, y, 0, 80, UIUnitFixed, 0, pGroup->cbChange, pGroup->cbPreChange, pGroup->pParentData);
				ui_WidgetQueueFreeAndNull(&pGroup->pCustomCooldownValueLabel);
			} else {
				sprintf(buf, "%g", pDefEntry->pTimeProperties->fCustomCooldownTime);
				MEExpanderRefreshLabel(&pGroup->pCustomCooldownValueLabel, buf, NULL, pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL2, 0, y, UI_WIDGET(pGroup->pExpander));
				MEFieldSafeDestroy(&pGroup->pCustomCooldownField);
			}
			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pCustomCooldownLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pCustomCooldownValueLabel);
			MEFieldSafeDestroy(&pGroup->pCustomCooldownField);
		}

		if ((pPropEntry->pTimeProperties && !pPropEntry->pTimeProperties->bNoRespawn) ||
			(pDefEntry && pDefEntry->pTimeProperties && !pDefEntry->pTimeProperties->bNoRespawn && (pDefEntry->pTimeProperties->eDynamicCooldownType != WorldDynamicSpawnType_Default))){
			MEExpanderRefreshLabel(&pGroup->pDynamicCooldownLabel, "Dynamic Cooldown", "Whether this interactable should automatically adjust its cooldown period when other nearby interactables are also on cooldown.", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
			if (pPropEntry->pTimeProperties) {
				MEExpanderRefreshEnumField(&pGroup->pDynamicCooldownField, pOrigPropEntry && pOrigPropEntry->pTimeProperties ? pOrigPropEntry->pTimeProperties : NULL, pPropEntry->pTimeProperties, parse_WorldTimeInteractionProperties, "DynamicCooldownType", WorldDynamicSpawnTypeEnum,
										UI_WIDGET(pGroup->pExpander), pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL2, y, 0, 100, UIUnitFixed, 0, pGroup->cbChange, pGroup->cbPreChange, pGroup->pParentData);
				ui_WidgetQueueFreeAndNull(&pGroup->pDynamicCooldownValueLabel);
			} else {
				MEExpanderRefreshLabel(&pGroup->pDynamicCooldownValueLabel, StaticDefineIntRevLookup(WorldDynamicSpawnTypeEnum, pDefEntry->pTimeProperties->eDynamicCooldownType), NULL, pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL2, 0, y, UI_WIDGET(pGroup->pExpander));
				MEFieldSafeDestroy(&pGroup->pDynamicCooldownField);
			}
			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pDynamicCooldownLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pDynamicCooldownValueLabel);
			MEFieldSafeDestroy(&pGroup->pDynamicCooldownField);
		}

		if ((pGroup->eType == InteractionDefType_Node) &&
			(pPropEntry->pTimeProperties || (pDefEntry && pDefEntry->pTimeProperties && pDefEntry->pTimeProperties->bTeamUsableWhenActive))) {
				MEExpanderRefreshLabel(&pGroup->pTeamUsableWhenActiveLabel, "Team Usable When Active", "If true, the object is usable by teammates of the interactor during its active period.", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
				if (pPropEntry->pTimeProperties) {
					MEExpanderRefreshSimpleField(&pGroup->pTeamUsableWhenActiveField, pOrigPropEntry && pOrigPropEntry->pTimeProperties ? pOrigPropEntry->pTimeProperties : NULL, pPropEntry->pTimeProperties, parse_WorldTimeInteractionProperties, "TeamUsableWhenActive", kMEFieldType_BooleanCombo,
						UI_WIDGET(pGroup->pExpander), pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL3, y, 0, 80, UIUnitFixed, 0, pGroup->cbChange, pGroup->cbPreChange, pGroup->pParentData);
					ui_WidgetQueueFreeAndNull(&pGroup->pTeamUsableWhenActiveValueLabel);
				} else {
					MEExpanderRefreshLabel(&pGroup->pTeamUsableWhenActiveValueLabel, pDefEntry->pTimeProperties->bTeamUsableWhenActive ? "True" : "False", NULL, pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL3, 0, y, UI_WIDGET(pGroup->pExpander));
					MEFieldSafeDestroy(&pGroup->pTeamUsableWhenActiveField);
				}
				y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pTeamUsableWhenActiveLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pTeamUsableWhenActiveValueLabel);
			MEFieldSafeDestroy(&pGroup->pTeamUsableWhenActiveField);
		}

		if ((pGroup->eType == InteractionDefType_Node) &&
			(pPropEntry->pTimeProperties || (pDefEntry && pDefEntry->pTimeProperties && pDefEntry->pTimeProperties->bHideDuringCooldown))) {
			MEExpanderRefreshLabel(&pGroup->pHideDuringCooldownLabel, "Hide During Cooldown", "If true, the object is not visible during the cooldown period.  Note that this is ignored if the Visible Expr has a value.", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
			if (pPropEntry->pTimeProperties) {
				MEExpanderRefreshSimpleField(&pGroup->pHideDuringCooldownField, pOrigPropEntry && pOrigPropEntry->pTimeProperties ? pOrigPropEntry->pTimeProperties : NULL, pPropEntry->pTimeProperties, parse_WorldTimeInteractionProperties, "HideDuringCooldown", kMEFieldType_BooleanCombo,
										UI_WIDGET(pGroup->pExpander), pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL3, y, 0, 80, UIUnitFixed, 0, pGroup->cbChange, pGroup->cbPreChange, pGroup->pParentData);
				ui_WidgetQueueFreeAndNull(&pGroup->pHideDuringCooldownValueLabel);
			} else {
				MEExpanderRefreshLabel(&pGroup->pHideDuringCooldownValueLabel, pDefEntry->pTimeProperties->bHideDuringCooldown ? "True" : "False", NULL, pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL3, 0, y, UI_WIDGET(pGroup->pExpander));
				MEFieldSafeDestroy(&pGroup->pHideDuringCooldownField);
			}
			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pHideDuringCooldownLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pHideDuringCooldownValueLabel);
			MEFieldSafeDestroy(&pGroup->pHideDuringCooldownField);
		}

		if (pPropEntry->pTimeProperties) {
			MEExpanderRefreshLabel(&pGroup->pInterruptOnMoveLabel, "Interrupt On Move", "If true, moving will interrupt the interact.", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
			MEExpanderRefreshSimpleField(&pGroup->pInterruptOnMoveField, pOrigPropEntry && pOrigPropEntry->pTimeProperties ? pOrigPropEntry->pTimeProperties : NULL, pPropEntry->pTimeProperties, parse_WorldTimeInteractionProperties, "InterruptOnMove", kMEFieldType_BooleanCombo,
									UI_WIDGET(pGroup->pExpander), pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL3, y, 0, 80, UIUnitFixed, 0, pGroup->cbChange, pGroup->cbPreChange, pGroup->pParentData);
			y += STANDARD_ROW_HEIGHT;

			MEExpanderRefreshLabel(&pGroup->pInterruptOnPowerLabel, "Interrupt On Power", "If true, using a power will interrupt the interact.", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
			MEExpanderRefreshSimpleField(&pGroup->pInterruptOnPowerField, pOrigPropEntry && pOrigPropEntry->pTimeProperties ? pOrigPropEntry->pTimeProperties : NULL, pPropEntry->pTimeProperties, parse_WorldTimeInteractionProperties, "InterruptOnPower", kMEFieldType_BooleanCombo,
									UI_WIDGET(pGroup->pExpander), pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL3, y, 0, 80, UIUnitFixed, 0, pGroup->cbChange, pGroup->cbPreChange, pGroup->pParentData);
			y += STANDARD_ROW_HEIGHT;

			MEExpanderRefreshLabel(&pGroup->pInterruptOnDamageLabel, "Interrupt On Damage", "If true, receiving damage will interrupt the interact.", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
			MEExpanderRefreshSimpleField(&pGroup->pInterruptOnDamageField, pOrigPropEntry && pOrigPropEntry->pTimeProperties ? pOrigPropEntry->pTimeProperties : NULL, pPropEntry->pTimeProperties, parse_WorldTimeInteractionProperties, "InterruptOnDamage", kMEFieldType_BooleanCombo,
									UI_WIDGET(pGroup->pExpander), pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL3, y, 0, 80, UIUnitFixed, 0, pGroup->cbChange, pGroup->cbPreChange, pGroup->pParentData);
			y += STANDARD_ROW_HEIGHT;

			ui_WidgetQueueFreeAndNull(&pGroup->pInterruptLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pInterruptValueLabel);
		} else if (pDefEntry && pDefEntry->pTimeProperties && (!pDefEntry->pTimeProperties->bInterruptOnDamage || !pDefEntry->pTimeProperties->bInterruptOnMove || !pDefEntry->pTimeProperties->bInterruptOnPower)) {
			MEExpanderRefreshLabel(&pGroup->pInterruptLabel, "Interrupt On", "What causes interrupt.", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
			if (pDefEntry->pTimeProperties->bInterruptOnMove) {
				estrConcat(&estrText, "Move ", 5);
			}
			if (pDefEntry->pTimeProperties->bInterruptOnPower) {
				estrConcat(&estrText, "Power ", 6);
			}
			if (pDefEntry->pTimeProperties->bInterruptOnDamage) {
				estrConcat(&estrText, "Damage ", 7);
			}
			if (!estrText || !estrText[0]) {
				estrConcat(&estrText, "(Nothing)", 7);
			}
			MEExpanderRefreshLabel(&pGroup->pInterruptValueLabel, estrText, NULL, pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL2, 0, y, UI_WIDGET(pGroup->pExpander));
			y += STANDARD_ROW_HEIGHT;
			estrDestroy(&estrText);

			ui_WidgetQueueFreeAndNull(&pGroup->pInterruptOnMoveLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pInterruptOnPowerLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pInterruptOnDamageLabel);
			MEFieldSafeDestroy(&pGroup->pInterruptOnMoveField);
			MEFieldSafeDestroy(&pGroup->pInterruptOnPowerField);
			MEFieldSafeDestroy(&pGroup->pInterruptOnDamageField);
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pInterruptOnMoveLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pInterruptOnPowerLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pInterruptOnDamageLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pInterruptLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pInterruptValueLabel);
			MEFieldSafeDestroy(&pGroup->pInterruptOnMoveField);
			MEFieldSafeDestroy(&pGroup->pInterruptOnPowerField);
			MEFieldSafeDestroy(&pGroup->pInterruptOnDamageField);
		}
	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pUseTimeLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pUseTimeValueLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pActiveTimeLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pActiveTimeValueLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pNoRespawnLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pNoRespawnValueLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pCooldownTimeLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pCooldownTimeValueLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pCustomCooldownLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pCustomCooldownValueLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pDynamicCooldownLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pDynamicCooldownValueLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pTeamUsableWhenActiveLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pTeamUsableWhenActiveValueLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pHideDuringCooldownLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pHideDuringCooldownValueLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pInterruptOnPowerLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pInterruptOnDamageLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pInterruptOnMoveLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pInterruptLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pInterruptValueLabel);
		MEFieldSafeDestroy(&pGroup->pUseTimeField);
		MEFieldSafeDestroy(&pGroup->pActiveTimeField);
		MEFieldSafeDestroy(&pGroup->pNoRespawnField);
		MEFieldSafeDestroy(&pGroup->pCooldownTimeField);
		MEFieldSafeDestroy(&pGroup->pCustomCooldownField);
		MEFieldSafeDestroy(&pGroup->pDynamicCooldownField);
		MEFieldSafeDestroy(&pGroup->pTeamUsableWhenActiveField);
		MEFieldSafeDestroy(&pGroup->pHideDuringCooldownField);
		MEFieldSafeDestroy(&pGroup->pInterruptOnPowerField);
		MEFieldSafeDestroy(&pGroup->pInterruptOnDamageField);
		MEFieldSafeDestroy(&pGroup->pInterruptOnMoveField);
	}

	// Animations
	if ((pcClass == pcPooled_Clickable) ||
		(pcClass == pcPooled_Contact) ||
		(pcClass == pcPooled_CraftingStation) ||
		(pcClass == pcPooled_Door) ||
		(pcClass == pcPooled_Gate) ||
		(pcClass == pcPooled_FromDefinition && pPropEntry->pAnimationProperties)
		) {
		if (!pGroup->pAnimationButton) {
			pGroup->pAnimationButton = ui_CheckButtonCreate(pGroup->fOffsetX, y, "Custom Animation", (pcClass == pcPooled_FromDefinition && pPropEntry->pAnimationProperties));
			ui_CheckButtonSetToggledCallback(pGroup->pAnimationButton, InteractionAnimationToggled, pGroup);
			ui_WidgetSetTooltipString(UI_WIDGET(pGroup->pAnimationButton), "Adds custom animations.  Otherwise no animations are performed.");
			ui_ExpanderAddChild(pGroup->pExpander, pGroup->pAnimationButton);
		} else {
			ui_WidgetSetPosition(UI_WIDGET(pGroup->pAnimationButton), pGroup->fOffsetX, y);
		}
		ui_CheckButtonSetState(pGroup->pAnimationButton, (pPropEntry->pAnimationProperties != NULL));
		y += STANDARD_ROW_HEIGHT;
	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pAnimationButton);
	}
	if ((pPropEntry->pAnimationProperties || (pDefEntry && pDefEntry->pAnimationProperties && pGroup->bShowDefValues)) &&
		((pcClass == pcPooled_Clickable) ||
		 (pcClass == pcPooled_Contact) ||
		 (pcClass == pcPooled_CraftingStation) ||
		 (pcClass == pcPooled_Door) ||
		 (pcClass == pcPooled_Gate) ||
		 (pcClass == pcPooled_FromDefinition && pPropEntry->pAnimationProperties)
		 )) {

		if (pPropEntry->pAnimationProperties || (pDefEntry && pDefEntry->pAnimationProperties && GET_REF(pDefEntry->pAnimationProperties->hInteractAnim))) {
			MEExpanderRefreshLabel(&pGroup->pAnimLabel, "Interact Anim", "Animation performed while interacting.", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
			if (pPropEntry->pAnimationProperties) {
				MEExpanderRefreshGlobalDictionaryField(&pGroup->pAnimField, pOrigPropEntry && pOrigPropEntry->pAnimationProperties ? pOrigPropEntry->pAnimationProperties : NULL, pPropEntry->pAnimationProperties, parse_WorldAnimationInteractionProperties, "InteractAnim", "AIAnimList",
										UI_WIDGET(pGroup->pExpander), pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL2, y, 0, 1.0, UIUnitPercentage, 0, pGroup->cbChange, pGroup->cbPreChange, pGroup->pParentData);
				ui_WidgetQueueFreeAndNull(&pGroup->pAnimValueLabel);
			} else {
				MEExpanderRefreshLabel(&pGroup->pAnimValueLabel, REF_STRING_FROM_HANDLE(pDefEntry->pAnimationProperties->hInteractAnim), NULL, pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL2, 0, y, UI_WIDGET(pGroup->pExpander));
				MEFieldSafeDestroy(&pGroup->pAnimField);
			}
			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pAnimLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pAnimValueLabel);
			MEFieldSafeDestroy(&pGroup->pAnimField);
		}
	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pAnimLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pAnimValueLabel);
		MEFieldSafeDestroy(&pGroup->pAnimField);
	}

	// Sound
	if (!pDefEntry &&
		((pcClass == pcPooled_Clickable) ||
		(pcClass == pcPooled_Contact) ||
		(pcClass == pcPooled_CraftingStation) ||
		(pcClass == pcPooled_Door) ||
		(pcClass == pcPooled_Gate)
		)) {
			if (!pGroup->pSoundButton) {
				pGroup->pSoundButton = ui_CheckButtonCreate(pGroup->fOffsetX, y, "Custom Sounds", false);
				ui_CheckButtonSetToggledCallback(pGroup->pSoundButton, InteractionSoundToggled, pGroup);
				ui_WidgetSetTooltipString(UI_WIDGET(pGroup->pSoundButton), "Adds custom sounds to be played from the interactable.");
				ui_ExpanderAddChild(pGroup->pExpander, pGroup->pSoundButton);
			} else {
				ui_WidgetSetPosition(UI_WIDGET(pGroup->pSoundButton), pGroup->fOffsetX, y);
			}
			ui_CheckButtonSetState(pGroup->pSoundButton, (pPropEntry->pSoundProperties != NULL));
			y += STANDARD_ROW_HEIGHT;
	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pSoundButton);
	}
	if ((pPropEntry->pSoundProperties || (pDefEntry && pDefEntry->pSoundProperties && pGroup->bShowDefValues)) &&
		((pcClass == pcPooled_Clickable) ||
		(pcClass == pcPooled_Gate) ||
		(pcClass == pcPooled_Contact) ||
		(pcClass == pcPooled_CraftingStation) ||
		(pcClass == pcPooled_Door))) {

			if(!pPropEntry->pSoundProperties)
			{
				MEExpanderRefreshLabel(&pGroup->pSoundSectionLabel, "Sounds", "Custom sounds defined in the interaction def.", pGroup->fOffsetX, 0, y, UI_WIDGET(pGroup->pExpander));
				y+= STANDARD_ROW_HEIGHT;
			} else {
				ui_WidgetQueueFreeAndNull(&pGroup->pSoundSectionLabel);
			}
			if (pPropEntry->pSoundProperties || (pDefEntry && pDefEntry->pSoundProperties && pDefEntry->pSoundProperties->pchAttemptSound)) {
				MEExpanderRefreshLabel(&pGroup->pAttemptSoundLabel, "Attempt Sound", "Sound played from the interactable when interaction starts. (optional)", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
				if (pPropEntry->pSoundProperties) {
					MEExpanderRefreshDataField(&pGroup->pAttemptSoundField, pOrigPropEntry ? pOrigPropEntry->pSoundProperties : NULL, pPropEntry->pSoundProperties, parse_WorldSoundInteractionProperties, "AttemptSound", sndGetEventListStatic(), true,
						UI_WIDGET(pGroup->pExpander), pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL2, y, 0, 1.0, UIUnitPercentage, 0, pGroup->cbChange, pGroup->cbPreChange, pGroup->pParentData);
					ui_WidgetQueueFreeAndNull(&pGroup->pAttemptSoundValueLabel);
				} else {
					MEExpanderRefreshLabel(&pGroup->pAttemptSoundValueLabel, pDefEntry->pSoundProperties->pchAttemptSound, NULL, pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL2, 0, y, UI_WIDGET(pGroup->pExpander));
					MEFieldSafeDestroy(&pGroup->pAttemptSoundField);
				}
				y += STANDARD_ROW_HEIGHT;
			} else {
				ui_WidgetQueueFreeAndNull(&pGroup->pAttemptSoundLabel);
				ui_WidgetQueueFreeAndNull(&pGroup->pAttemptSoundValueLabel);
				MEFieldSafeDestroy(&pGroup->pAttemptSoundField);
			}

			if (pPropEntry->pSoundProperties || (pDefEntry && pDefEntry->pSoundProperties && pDefEntry->pSoundProperties->pchSuccessSound)) {
				MEExpanderRefreshLabel(&pGroup->pSuccessSoundLabel, "Success Sound", "Sound played from the interactable upon success. (optional)", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
				if (pPropEntry->pSoundProperties) {
					MEExpanderRefreshDataField(&pGroup->pSuccessSoundField, pOrigPropEntry ? pOrigPropEntry->pSoundProperties : NULL, pPropEntry->pSoundProperties, parse_WorldSoundInteractionProperties, "SuccessSound", sndGetEventListStatic(), true,
						UI_WIDGET(pGroup->pExpander), pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL2, y, 0, 1.0, UIUnitPercentage, 0, pGroup->cbChange, pGroup->cbPreChange, pGroup->pParentData);
					ui_WidgetQueueFreeAndNull(&pGroup->pSuccessSoundValueLabel);
				} else {
					MEExpanderRefreshLabel(&pGroup->pSuccessSoundValueLabel, pDefEntry->pSoundProperties->pchSuccessSound, NULL, pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL2, 0, y, UI_WIDGET(pGroup->pExpander));
					MEFieldSafeDestroy(&pGroup->pSuccessSoundField);
				}
				y += STANDARD_ROW_HEIGHT;
			} else {
				ui_WidgetQueueFreeAndNull(&pGroup->pSuccessSoundLabel);
				ui_WidgetQueueFreeAndNull(&pGroup->pSuccessSoundValueLabel);
				MEFieldSafeDestroy(&pGroup->pSuccessSoundField);
			}

			if (pPropEntry->pSoundProperties || (pDefEntry && pDefEntry->pSoundProperties && pDefEntry->pSoundProperties->pchFailureSound)) {
				MEExpanderRefreshLabel(&pGroup->pFailureSoundLabel, "Failure Sound", "Sound played from the interactable upon failure. (optional)", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
				if (pPropEntry->pSoundProperties) {
					MEExpanderRefreshDataField(&pGroup->pFailureSoundField, pOrigPropEntry ? pOrigPropEntry->pSoundProperties : NULL, pPropEntry->pSoundProperties, parse_WorldSoundInteractionProperties, "FailureSound", sndGetEventListStatic(), true,
						UI_WIDGET(pGroup->pExpander), pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL2, y, 0, 1.0, UIUnitPercentage, 0, pGroup->cbChange, pGroup->cbPreChange, pGroup->pParentData);
					ui_WidgetQueueFreeAndNull(&pGroup->pFailureSoundValueLabel);
				} else {
					MEExpanderRefreshLabel(&pGroup->pFailureSoundValueLabel, pDefEntry->pSoundProperties->pchFailureSound, NULL, pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL2, 0, y, UI_WIDGET(pGroup->pExpander));
					MEFieldSafeDestroy(&pGroup->pFailureSoundField);
				}
				y += STANDARD_ROW_HEIGHT;
			} else {
				ui_WidgetQueueFreeAndNull(&pGroup->pFailureSoundLabel);
				ui_WidgetQueueFreeAndNull(&pGroup->pFailureSoundValueLabel);
				MEFieldSafeDestroy(&pGroup->pFailureSoundField);
			}

			if (pPropEntry->pSoundProperties || (pDefEntry && pDefEntry->pSoundProperties && pDefEntry->pSoundProperties->pchInterruptSound)) {
				MEExpanderRefreshLabel(&pGroup->pInterruptSoundLabel, "Interrupt Sound", "Sound played from the interactable when interact is interrupted. (optional)", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
				if (pPropEntry->pSoundProperties) {
					MEExpanderRefreshDataField(&pGroup->pInterruptSoundField, pOrigPropEntry ? pOrigPropEntry->pSoundProperties : NULL, pPropEntry->pSoundProperties, parse_WorldSoundInteractionProperties, "InterruptSound", sndGetEventListStatic(), true,
						UI_WIDGET(pGroup->pExpander), pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL2, y, 0, 1.0, UIUnitPercentage, 0, pGroup->cbChange, pGroup->cbPreChange, pGroup->pParentData);
					ui_WidgetQueueFreeAndNull(&pGroup->pInterruptSoundValueLabel);
				} else {
					MEExpanderRefreshLabel(&pGroup->pInterruptSoundValueLabel, pDefEntry->pSoundProperties->pchInterruptSound, NULL, pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL2, 0, y, UI_WIDGET(pGroup->pExpander));
					MEFieldSafeDestroy(&pGroup->pInterruptSoundField);
				}
				y += STANDARD_ROW_HEIGHT;
			} else {
				ui_WidgetQueueFreeAndNull(&pGroup->pInterruptSoundLabel);
				ui_WidgetQueueFreeAndNull(&pGroup->pInterruptSoundValueLabel);
				MEFieldSafeDestroy(&pGroup->pInterruptSoundField);
			}

	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pSoundSectionLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pAttemptSoundLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pAttemptSoundValueLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pSuccessSoundLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pSuccessSoundValueLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pFailureSoundLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pFailureSoundValueLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pInterruptSoundLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pInterruptSoundValueLabel);
		MEFieldSafeDestroy(&pGroup->pAttemptSoundField);
		MEFieldSafeDestroy(&pGroup->pSuccessSoundField);
		MEFieldSafeDestroy(&pGroup->pFailureSoundField);
		MEFieldSafeDestroy(&pGroup->pInterruptSoundField);
	}

	// Action
	if ((pcClass == pcPooled_Clickable) ||
		 (pcClass == pcPooled_Gate) ||
		 (pcClass == pcPooled_Contact) ||
		 (pcClass == pcPooled_Door)) {
		if (!pGroup->pActionButton) {
			pGroup->pActionButton = ui_CheckButtonCreate(pGroup->fOffsetX, y, "Custom Actions", false);
			ui_CheckButtonSetToggledCallback(pGroup->pActionButton, InteractionActionToggled, pGroup);
			ui_WidgetSetTooltipString(UI_WIDGET(pGroup->pActionButton), "Adds custom actions.  Otherwise no actions are performed.");
			ui_ExpanderAddChild(pGroup->pExpander, pGroup->pActionButton);
		} else {
			ui_WidgetSetPosition(UI_WIDGET(pGroup->pActionButton), pGroup->fOffsetX, y);
		}
		ui_CheckButtonSetState(pGroup->pActionButton, (pPropEntry->pActionProperties != NULL));
		y += STANDARD_ROW_HEIGHT;
	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pActionButton);
	}
	if ((pPropEntry->pActionProperties || (pDefEntry && pDefEntry->pActionProperties && pGroup->bShowDefValues)) &&
		((pcClass == pcPooled_Clickable) ||
		 (pcClass == pcPooled_Gate) ||
		 (pcClass == pcPooled_Contact) ||
		 (pcClass == pcPooled_Door))) {

		if(!pPropEntry->pActionProperties)
		{
			MEExpanderRefreshLabel(&pGroup->pActionSectionLabel, "Actions", "Custom actions defined in the interaction def.  If not set, no actions are performed.", pGroup->fOffsetX, 0, y, UI_WIDGET(pGroup->pExpander));
			y+= STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pActionSectionLabel);
		}
		if (pPropEntry->pActionProperties || (pDefEntry && pDefEntry->pActionProperties && pDefEntry->pActionProperties->pAttemptExpr)) {
			MEExpanderRefreshLabel(&pGroup->pAttemptActionLabel, "Attempt Expr", "Expression run when start interacting. (optional)", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
			if (pPropEntry->pActionProperties) {
				MEExpanderRefreshSimpleField(&pGroup->pAttemptActionField, pOrigPropEntry ? pOrigPropEntry->pActionProperties : NULL, pPropEntry->pActionProperties, parse_WorldActionInteractionProperties, "AttemptExpr", kMEFieldTypeEx_Expression,
										UI_WIDGET(pGroup->pExpander), pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL2, y, 0, 1.0, UIUnitPercentage, 0, pGroup->cbChange, pGroup->cbPreChange, pGroup->pParentData);
				ui_WidgetQueueFreeAndNull(&pGroup->pAttemptActionValueLabel);
			} else {
				MEExpanderRefreshLabel(&pGroup->pAttemptActionValueLabel, exprGetCompleteString(pDefEntry->pActionProperties->pAttemptExpr), NULL, pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL2, 0, y, UI_WIDGET(pGroup->pExpander));
				MEFieldSafeDestroy(&pGroup->pAttemptActionField);
			}
			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pAttemptActionLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pAttemptActionValueLabel);
			MEFieldSafeDestroy(&pGroup->pAttemptActionField);
		}

		if (pPropEntry->pActionProperties || (pDefEntry && pDefEntry->pActionProperties && pDefEntry->pActionProperties->pSuccessExpr)) {
			MEExpanderRefreshLabel(&pGroup->pSuccessActionLabel, "Success Expr", "Expression run on success. (optional)", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
			if (pPropEntry->pActionProperties) {
				MEExpanderRefreshSimpleField(&pGroup->pSuccessActionField, pOrigPropEntry ? pOrigPropEntry->pActionProperties : NULL, pPropEntry->pActionProperties, parse_WorldActionInteractionProperties, "SuccessExpr", kMEFieldTypeEx_Expression,
										UI_WIDGET(pGroup->pExpander), pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL2, y, 0, 1.0, UIUnitPercentage, 0, pGroup->cbChange, pGroup->cbPreChange, pGroup->pParentData);
				ui_WidgetQueueFreeAndNull(&pGroup->pSuccessActionValueLabel);
			} else {
				MEExpanderRefreshLabel(&pGroup->pSuccessActionValueLabel, exprGetCompleteString(pDefEntry->pActionProperties->pSuccessExpr), NULL, pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL2, 0, y, UI_WIDGET(pGroup->pExpander));
				MEFieldSafeDestroy(&pGroup->pSuccessActionField);
			}
			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pSuccessActionLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pSuccessActionValueLabel);
			MEFieldSafeDestroy(&pGroup->pSuccessActionField);
		}

		if (pPropEntry->pActionProperties || (pDefEntry && pDefEntry->pActionProperties && pDefEntry->pActionProperties->pFailureExpr)) {
			MEExpanderRefreshLabel(&pGroup->pFailureActionLabel, "Failure Expr", "Expression run on failure. (optional)", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
			if (pPropEntry->pActionProperties) {
				MEExpanderRefreshSimpleField(&pGroup->pFailureActionField, pOrigPropEntry ? pOrigPropEntry->pActionProperties : NULL, pPropEntry->pActionProperties, parse_WorldActionInteractionProperties, "FailureExpr", kMEFieldTypeEx_Expression,
										UI_WIDGET(pGroup->pExpander), pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL2, y, 0, 1.0, UIUnitPercentage, 0, pGroup->cbChange, pGroup->cbPreChange, pGroup->pParentData);
				ui_WidgetQueueFreeAndNull(&pGroup->pFailureActionValueLabel);
			} else {
				MEExpanderRefreshLabel(&pGroup->pFailureActionValueLabel, exprGetCompleteString(pDefEntry->pActionProperties->pFailureExpr), NULL, pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL2, 0, y, UI_WIDGET(pGroup->pExpander));
				MEFieldSafeDestroy(&pGroup->pFailureActionField);
			}
			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pFailureActionLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pFailureActionValueLabel);
			MEFieldSafeDestroy(&pGroup->pFailureActionField);
		}

		if (pPropEntry->pActionProperties || (pDefEntry && pDefEntry->pActionProperties && pDefEntry->pActionProperties->pInterruptExpr)) {
			MEExpanderRefreshLabel(&pGroup->pInterruptActionLabel, "Interrupt Expr", "Expression when interact is interrupted. (optional)", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
			if (pPropEntry->pActionProperties) {
				MEExpanderRefreshSimpleField(&pGroup->pInterruptActionField, pOrigPropEntry ? pOrigPropEntry->pActionProperties : NULL, pPropEntry->pActionProperties, parse_WorldActionInteractionProperties, "InterruptExpr", kMEFieldTypeEx_Expression,
										UI_WIDGET(pGroup->pExpander), pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL2, y, 0, 1.0, UIUnitPercentage, 0, pGroup->cbChange, pGroup->cbPreChange, pGroup->pParentData);
				ui_WidgetQueueFreeAndNull(&pGroup->pInterruptActionValueLabel);
			} else {
				MEExpanderRefreshLabel(&pGroup->pInterruptActionValueLabel, exprGetCompleteString(pDefEntry->pActionProperties->pInterruptExpr), NULL, pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL2, 0, y, UI_WIDGET(pGroup->pExpander));
				MEFieldSafeDestroy(&pGroup->pInterruptActionField);
			}
			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pInterruptActionLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pInterruptActionValueLabel);
			MEFieldSafeDestroy(&pGroup->pInterruptActionField);
		}

		if (pPropEntry->pActionProperties || (pDefEntry && pDefEntry->pActionProperties && pDefEntry->pActionProperties->pNoLongerActiveExpr)) {
			MEExpanderRefreshLabel(&pGroup->pNoLongerActiveActionLabel, "No Longer Active Expr", "Expression when it enters cooldown. (optional)", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
			if (pPropEntry->pActionProperties) {
				MEExpanderRefreshSimpleField(&pGroup->pNoLongerActiveActionField, pOrigPropEntry ? pOrigPropEntry->pActionProperties : NULL, pPropEntry->pActionProperties, parse_WorldActionInteractionProperties, "NoLongerActiveExpr", kMEFieldTypeEx_Expression,
										UI_WIDGET(pGroup->pExpander), pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL2, y, 0, 1.0, UIUnitPercentage, 0, pGroup->cbChange, pGroup->cbPreChange, pGroup->pParentData);
				ui_WidgetQueueFreeAndNull(&pGroup->pNoLongerActiveActionValueLabel);
			} else {
				MEExpanderRefreshLabel(&pGroup->pNoLongerActiveActionValueLabel, exprGetCompleteString(pDefEntry->pActionProperties->pNoLongerActiveExpr), NULL, pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL2, 0, y, UI_WIDGET(pGroup->pExpander));
				MEFieldSafeDestroy(&pGroup->pNoLongerActiveActionField);
			}
			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pNoLongerActiveActionLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pNoLongerActiveActionValueLabel);
			MEFieldSafeDestroy(&pGroup->pNoLongerActiveActionField);
		}

		if (pPropEntry->pActionProperties || (pDefEntry && pDefEntry->pActionProperties && pDefEntry->pActionProperties->pCooldownExpr)) {
			MEExpanderRefreshLabel(&pGroup->pCooldownActionLabel, "Cooldown Expr", "Expression when it finishes cooldown. (optional)", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
			if (pPropEntry->pActionProperties) {
				MEExpanderRefreshSimpleField(&pGroup->pCooldownActionField, pOrigPropEntry ? pOrigPropEntry->pActionProperties : NULL, pPropEntry->pActionProperties, parse_WorldActionInteractionProperties, "CooldownExpr", kMEFieldTypeEx_Expression,
					UI_WIDGET(pGroup->pExpander), pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL2, y, 0, 1.0, UIUnitPercentage, 0, pGroup->cbChange, pGroup->cbPreChange, pGroup->pParentData);
				ui_WidgetQueueFreeAndNull(&pGroup->pCooldownActionValueLabel);
			} else {
				MEExpanderRefreshLabel(&pGroup->pCooldownActionValueLabel, exprGetCompleteString(pDefEntry->pActionProperties->pCooldownExpr), NULL, pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL2, 0, y, UI_WIDGET(pGroup->pExpander));
				MEFieldSafeDestroy(&pGroup->pCooldownActionField);
			}
			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pCooldownActionLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pCooldownActionValueLabel);
			MEFieldSafeDestroy(&pGroup->pCooldownActionField);
		}

		if (pPropEntry->pActionProperties || (pDefEntry && pDefEntry->pActionProperties && eaSize(&pDefEntry->pActionProperties->successActions.eaActions))) {
			MEExpanderRefreshLabel(&pGroup->pSuccessGameActionsLabel, "Success Actions", "Transactional actions to perform on success. (optional)", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
			if (pPropEntry->pActionProperties) {
				if (!pGroup->pSuccessGameActionsButton) {
					pGroup->pSuccessGameActionsButton = ui_GameActionEditButtonCreate((char*)pcSrcMap, &pPropEntry->pActionProperties->successActions, NULL, InteractionSuccessGameActionsUpdated, NULL, pGroup);
					ui_ExpanderAddChild(pGroup->pExpander, pGroup->pSuccessGameActionsButton);
					ui_WidgetSetWidthEx(UI_WIDGET(pGroup->pSuccessGameActionsButton), 1.0, UIUnitPercentage);
				} else {
					if( !pcSrcMap ) {
						free(pGroup->pSuccessGameActionsButton->pcSrcZoneMap);
						pGroup->pSuccessGameActionsButton->pcSrcZoneMap = NULL;
					} else if( pGroup->pSuccessGameActionsButton->pcSrcZoneMap
							   && stricmp(pcSrcMap, pGroup->pSuccessGameActionsButton->pcSrcZoneMap) != 0 ) {
						free(pGroup->pSuccessGameActionsButton->pcSrcZoneMap);
						pGroup->pSuccessGameActionsButton->pcSrcZoneMap = strdup( pcSrcMap );
					}
				}
				ui_GameActionEditButtonSetData(pGroup->pSuccessGameActionsButton, &pPropEntry->pActionProperties->successActions, pOrigPropEntry && pOrigPropEntry->pActionProperties ? &pOrigPropEntry->pActionProperties->successActions : NULL);
				ui_WidgetSetPosition(UI_WIDGET(pGroup->pSuccessGameActionsButton), pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL2, y);
				ui_WidgetQueueFreeAndNull(&pGroup->pSuccessGameActionsValueLabel);
			} else {
				sprintf(buf, "%d Actions", eaSize(&pDefEntry->pActionProperties->successActions.eaActions));
				MEExpanderRefreshLabel(&pGroup->pSuccessGameActionsValueLabel, buf, NULL, pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL2, 0, y, UI_WIDGET(pGroup->pExpander));
				ui_WidgetQueueFreeAndNull(&pGroup->pSuccessGameActionsButton);
			}
			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pSuccessGameActionsLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pSuccessGameActionsValueLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pSuccessGameActionsButton);
		}

		if (pPropEntry->pActionProperties || (pDefEntry && pDefEntry->pActionProperties && eaSize(&pDefEntry->pActionProperties->failureActions.eaActions))) {
			MEExpanderRefreshLabel(&pGroup->pFailureGameActionsLabel, "Failure Actions", "Transactional actions to perform on Failure. (optional)", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
			if (pPropEntry->pActionProperties) {
				if (!pGroup->pFailureGameActionsButton) {
					pGroup->pFailureGameActionsButton = ui_GameActionEditButtonCreate((char*)pcSrcMap, &pPropEntry->pActionProperties->failureActions, NULL, InteractionFailureGameActionsUpdated, NULL, pGroup);
					ui_ExpanderAddChild(pGroup->pExpander, pGroup->pFailureGameActionsButton);
					ui_WidgetSetWidthEx(UI_WIDGET(pGroup->pFailureGameActionsButton), 1.0, UIUnitPercentage);
				} else {
					if( !pcSrcMap ) {
						free(pGroup->pFailureGameActionsButton->pcSrcZoneMap);
						pGroup->pFailureGameActionsButton->pcSrcZoneMap = NULL;
					} else if( pGroup->pFailureGameActionsButton->pcSrcZoneMap
						&& stricmp(pcSrcMap, pGroup->pFailureGameActionsButton->pcSrcZoneMap) != 0 ) {
							free(pGroup->pFailureGameActionsButton->pcSrcZoneMap);
							pGroup->pFailureGameActionsButton->pcSrcZoneMap = strdup( pcSrcMap );
					}
				}
				ui_GameActionEditButtonSetData(pGroup->pFailureGameActionsButton, &pPropEntry->pActionProperties->failureActions, pOrigPropEntry && pOrigPropEntry->pActionProperties ? &pOrigPropEntry->pActionProperties->failureActions : NULL);
				ui_WidgetSetPosition(UI_WIDGET(pGroup->pFailureGameActionsButton), pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL2, y);
				ui_WidgetQueueFreeAndNull(&pGroup->pFailureGameActionsValueLabel);
			} else {
				sprintf(buf, "%d Actions", eaSize(&pDefEntry->pActionProperties->failureActions.eaActions));
				MEExpanderRefreshLabel(&pGroup->pFailureGameActionsValueLabel, buf, NULL, pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL2, 0, y, UI_WIDGET(pGroup->pExpander));
				ui_WidgetQueueFreeAndNull(&pGroup->pFailureGameActionsButton);
			}
			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pFailureGameActionsLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pFailureGameActionsValueLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pFailureGameActionsButton);
		}

	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pActionSectionLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pAttemptActionLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pAttemptActionValueLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pSuccessActionLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pSuccessActionValueLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pFailureActionLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pFailureActionValueLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pInterruptActionLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pInterruptActionValueLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pNoLongerActiveActionLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pNoLongerActiveActionValueLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pCooldownActionLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pCooldownActionValueLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pSuccessGameActionsLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pSuccessGameActionsValueLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pSuccessGameActionsButton);
		ui_WidgetQueueFreeAndNull(&pGroup->pFailureGameActionsLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pFailureGameActionsValueLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pFailureGameActionsButton);
		MEFieldSafeDestroy(&pGroup->pAttemptActionField);
		MEFieldSafeDestroy(&pGroup->pSuccessActionField);
		MEFieldSafeDestroy(&pGroup->pFailureActionField);
		MEFieldSafeDestroy(&pGroup->pInterruptActionField);
		MEFieldSafeDestroy(&pGroup->pNoLongerActiveActionField);
		MEFieldSafeDestroy(&pGroup->pCooldownActionField);
	}

	// Text
	if ((pcClass == pcPooled_Clickable) ||
		(pcClass == pcPooled_Gate) ||
		(pcClass == pcPooled_Contact) ||
		(pcClass == pcPooled_CraftingStation) ||
		(pcClass == pcPooled_Door) ||
		(pcClass == pcPooled_FromDefinition && pPropEntry->pTextProperties)
		) {
		if (!pGroup->pTextButton) {
			pGroup->pTextButton = ui_CheckButtonCreate(pGroup->fOffsetX, y, "Custom Text", false);
			ui_CheckButtonSetToggledCallback(pGroup->pTextButton, InteractionTextToggled, pGroup);
			ui_WidgetSetTooltipString(UI_WIDGET(pGroup->pTextButton), "Adds custom text.  Otherwise default text is used.");
			ui_ExpanderAddChild(pGroup->pExpander, pGroup->pTextButton);
		} else {
			ui_WidgetSetPosition(UI_WIDGET(pGroup->pTextButton), pGroup->fOffsetX, y);
		}
		ui_CheckButtonSetState(pGroup->pTextButton, (pPropEntry->pTextProperties != NULL));
		y += STANDARD_ROW_HEIGHT;
	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pTextButton);
	}
	if ((pPropEntry->pTextProperties || (pDefEntry && pDefEntry->pTextProperties && pGroup->bShowDefValues)) &&
		((pcClass == pcPooled_Clickable) ||
		 (pcClass == pcPooled_Gate) ||
		 (pcClass == pcPooled_Contact) ||
		 (pcClass == pcPooled_CraftingStation) ||
		 (pcClass == pcPooled_Door) ||
		 (pcClass == pcPooled_FromDefinition && pPropEntry->pTextProperties)
		 )) {

		if (pPropEntry->pTextProperties || (pDefEntry && pDefEntry->pTextProperties && GET_REF(pDefEntry->pTextProperties->usabilityOptionText.hMessage))) {
			MEExpanderRefreshLabel(&pGroup->pUsabilityTextLabel, "Usability Text", "The text displays usability requirements. (optional)", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
			if (pPropEntry->pTextProperties) {
				MEExpanderRefreshSimpleField(&pGroup->pUsabilityTextField, pOrigPropEntry && pOrigPropEntry->pTextProperties ? &pOrigPropEntry->pTextProperties->usabilityOptionText : NULL, &pPropEntry->pTextProperties->usabilityOptionText, parse_DisplayMessage, "EditorCopy", kMEFieldType_Message,
										UI_WIDGET(pGroup->pExpander), pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL2, y, 0, 1.0, UIUnitPercentage, 0, pGroup->cbChange, pGroup->cbPreChange, pGroup->pParentData);
				ui_WidgetQueueFreeAndNull(&pGroup->pUsabilityTextValueLabel);
			} else {
				MEExpanderRefreshLabel(&pGroup->pUsabilityTextValueLabel, GET_REF(pDefEntry->pTextProperties->usabilityOptionText.hMessage)->pcDefaultString, NULL, pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL2, 0, y, UI_WIDGET(pGroup->pExpander));
				MEFieldSafeDestroy(&pGroup->pUsabilityTextField);
			}
			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pUsabilityTextLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pUsabilityTextValueLabel);
			MEFieldSafeDestroy(&pGroup->pUsabilityTextField);
		}

		if (pPropEntry->pTextProperties || (pDefEntry && pDefEntry->pTextProperties && GET_REF(pDefEntry->pTextProperties->interactOptionText.hMessage))) {
			MEExpanderRefreshLabel(&pGroup->pInteractTextLabel, "Interact Text", "The text displayed before interacting. (optional)", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
			if (pPropEntry->pTextProperties) {
				MEExpanderRefreshSimpleField(&pGroup->pInteractTextField, pOrigPropEntry && pOrigPropEntry->pTextProperties ? &pOrigPropEntry->pTextProperties->interactOptionText : NULL, &pPropEntry->pTextProperties->interactOptionText, parse_DisplayMessage, "EditorCopy", kMEFieldType_Message,
										UI_WIDGET(pGroup->pExpander), pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL2, y, 0, 1.0, UIUnitPercentage, 0, pGroup->cbChange, pGroup->cbPreChange, pGroup->pParentData);
				ui_WidgetQueueFreeAndNull(&pGroup->pInteractTextValueLabel);
			} else {
				MEExpanderRefreshLabel(&pGroup->pInteractTextValueLabel, GET_REF(pDefEntry->pTextProperties->interactOptionText.hMessage)->pcDefaultString, NULL, pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL2, 0, y, UI_WIDGET(pGroup->pExpander));
				MEFieldSafeDestroy(&pGroup->pInteractTextField);
			}
			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pInteractTextLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pInteractTextValueLabel);
			MEFieldSafeDestroy(&pGroup->pInteractTextField);
		}

		if (pPropEntry->pTextProperties || (pDefEntry && pDefEntry->pTextProperties && GET_REF(pDefEntry->pTextProperties->interactDetailText.hMessage))) {
			MEExpanderRefreshLabel(&pGroup->pDetailTextLabel, "Detail Text", "Auxiliary text that can be used by the UI. (optional)", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
			if (pPropEntry->pTextProperties) {
				MEExpanderRefreshSimpleField(&pGroup->pDetailTextField, pOrigPropEntry && pOrigPropEntry->pTextProperties ? &pOrigPropEntry->pTextProperties->interactDetailText : NULL, &pPropEntry->pTextProperties->interactDetailText, parse_DisplayMessage, "EditorCopy", kMEFieldType_Message,
					UI_WIDGET(pGroup->pExpander), pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL2, y, 0, 1.0, UIUnitPercentage, 0, pGroup->cbChange, pGroup->cbPreChange, pGroup->pParentData);
				ui_WidgetQueueFreeAndNull(&pGroup->pDetailTextValueLabel);
			} else {
				MEExpanderRefreshLabel(&pGroup->pDetailTextValueLabel, GET_REF(pDefEntry->pTextProperties->interactDetailText.hMessage)->pcDefaultString, NULL, pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL2, 0, y, UI_WIDGET(pGroup->pExpander));
				MEFieldSafeDestroy(&pGroup->pDetailTextField);
			}
			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pDetailTextLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pDetailTextValueLabel);
			MEFieldSafeDestroy(&pGroup->pDetailTextField);
		}

		if (pPropEntry->pTextProperties || (pDefEntry && pDefEntry->pTextProperties && pDefEntry->pTextProperties->interactDetailTexture)) {
			MEExpanderRefreshLabel(&pGroup->pDetailTextureLabel, "Detail Texture", "Auxiliary texture that can be used by the UI. (optional)", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
			if (pPropEntry->pTextProperties) {
				MEExpanderRefreshSimpleField(&pGroup->pDetailTextureField, pOrigPropEntry ? pOrigPropEntry->pTextProperties : NULL, pPropEntry->pTextProperties, parse_WorldTextInteractionProperties, "InteractDetailTexture", kMEFieldType_Texture,
					UI_WIDGET(pGroup->pExpander), pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL2, y, 0, 1.0, UIUnitPercentage, 0, pGroup->cbChange, pGroup->cbPreChange, pGroup->pParentData);
				ui_WidgetQueueFreeAndNull(&pGroup->pDetailTextureValueSprite);
			} else {
				MEExpanderRefreshSprite(&pGroup->pDetailTextureValueSprite, pDefEntry->pTextProperties->interactDetailTexture, pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL2, y, 64, 64, UI_WIDGET(pGroup->pExpander));
				MEFieldSafeDestroy(&pGroup->pDetailTextureField);
			}
			y += 66;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pDetailTextureLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pDetailTextureValueSprite);
			MEFieldSafeDestroy(&pGroup->pDetailTextureField);
		}

		if (pPropEntry->pTextProperties || (pDefEntry && pDefEntry->pTextProperties && GET_REF(pDefEntry->pTextProperties->successConsoleText.hMessage))) {
			MEExpanderRefreshLabel(&pGroup->pSuccessTextLabel, "Success Text", "The text displayed on successful interact. (optional)", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
			if (pPropEntry->pTextProperties) {
				MEExpanderRefreshSimpleField(&pGroup->pSuccessTextField, pOrigPropEntry && pOrigPropEntry->pTextProperties ? &pOrigPropEntry->pTextProperties->successConsoleText : NULL, &pPropEntry->pTextProperties->successConsoleText, parse_DisplayMessage, "EditorCopy", kMEFieldType_Message,
										UI_WIDGET(pGroup->pExpander), pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL2, y, 0, 1.0, UIUnitPercentage, 0, pGroup->cbChange, pGroup->cbPreChange, pGroup->pParentData);
				ui_WidgetQueueFreeAndNull(&pGroup->pSuccessTextValueLabel);
			} else {
				MEExpanderRefreshLabel(&pGroup->pSuccessTextValueLabel, GET_REF(pDefEntry->pTextProperties->successConsoleText.hMessage)->pcDefaultString, NULL, pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL2, 0, y, UI_WIDGET(pGroup->pExpander));
				MEFieldSafeDestroy(&pGroup->pSuccessTextField);
			}
			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pSuccessTextLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pSuccessTextValueLabel);
			MEFieldSafeDestroy(&pGroup->pSuccessTextField);
		}

		if (pPropEntry->pTextProperties || (pDefEntry && pDefEntry->pTextProperties && GET_REF(pDefEntry->pTextProperties->failureConsoleText.hMessage))) {
			MEExpanderRefreshLabel(&pGroup->pFailureTextLabel, "Failure Text", "The text displayed on failed interact. (optional)", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
			if (pPropEntry->pTextProperties) {
				MEExpanderRefreshSimpleField(&pGroup->pFailureTextField, pOrigPropEntry && pOrigPropEntry->pTextProperties ? &pOrigPropEntry->pTextProperties->failureConsoleText : NULL, &pPropEntry->pTextProperties->failureConsoleText, parse_DisplayMessage, "EditorCopy", kMEFieldType_Message,
										UI_WIDGET(pGroup->pExpander), pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL2, y, 0, 1.0, UIUnitPercentage, 0, pGroup->cbChange, pGroup->cbPreChange, pGroup->pParentData);
				ui_WidgetQueueFreeAndNull(&pGroup->pFailureTextValueLabel);
			} else {
				MEExpanderRefreshLabel(&pGroup->pFailureTextValueLabel, GET_REF(pDefEntry->pTextProperties->failureConsoleText.hMessage)->pcDefaultString, NULL, pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL2, 0, y, UI_WIDGET(pGroup->pExpander));
				MEFieldSafeDestroy(&pGroup->pFailureTextField);
			}
			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pFailureTextLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pFailureTextValueLabel);
			MEFieldSafeDestroy(&pGroup->pFailureTextField);
		}
	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pUsabilityTextLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pUsabilityTextValueLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pInteractTextLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pInteractTextValueLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pDetailTextLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pDetailTextValueLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pDetailTextureLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pDetailTextureValueSprite);
		ui_WidgetQueueFreeAndNull(&pGroup->pSuccessTextLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pSuccessTextValueLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pFailureTextLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pFailureTextValueLabel);
		MEFieldSafeDestroy(&pGroup->pUsabilityTextField);
		MEFieldSafeDestroy(&pGroup->pInteractTextField);
		MEFieldSafeDestroy(&pGroup->pDetailTextField);
		MEFieldSafeDestroy(&pGroup->pDetailTextureField);
		MEFieldSafeDestroy(&pGroup->pSuccessTextField);
		MEFieldSafeDestroy(&pGroup->pFailureTextField);
	}

	// Reward
	if ((pcClass == pcPooled_Clickable || pcClass == pcPooled_Gate || pcClass == pcPooled_FromDefinition)) {
		if (!pGroup->pRewardButton) {
			pGroup->pRewardButton = ui_CheckButtonCreate(pGroup->fOffsetX, y, "Custom Rewards", false);
			ui_CheckButtonSetToggledCallback(pGroup->pRewardButton, InteractionRewardToggled, pGroup);
			ui_WidgetSetTooltipString(UI_WIDGET(pGroup->pRewardButton), "Adds custom rewards.  Otherwise no rewards are granted.");
			ui_ExpanderAddChild(pGroup->pExpander, pGroup->pRewardButton);
		} else {
			ui_WidgetSetPosition(UI_WIDGET(pGroup->pRewardButton), pGroup->fOffsetX, y);
		}
		ui_CheckButtonSetState(pGroup->pRewardButton, (pPropEntry->pRewardProperties != NULL));
		ui_WidgetQueueFreeAndNull(&pGroup->pRewardSectionLabel);
		y += STANDARD_ROW_HEIGHT;
	} else if (pDefEntry && pGroup->bShowDefValues && pDefEntry->pRewardProperties) {
		MEExpanderRefreshLabel(&pGroup->pRewardSectionLabel, "Rewards", "Rewards defined by the interaction def.", pGroup->fOffsetX, 0, y, UI_WIDGET(pGroup->pExpander));
		ui_WidgetQueueFreeAndNull(&pGroup->pRewardButton);
		y += STANDARD_ROW_HEIGHT;
	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pRewardSectionLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pRewardButton);
	}

	if ((pPropEntry->pRewardProperties || (pDefEntry && pDefEntry->pRewardProperties && pGroup->bShowDefValues)) &&
		(pcClass == pcPooled_Clickable || pcClass == pcPooled_Gate || pcClass == pcPooled_FromDefinition)) {

		if (pPropEntry->pRewardProperties || (pDefEntry && pDefEntry->pRewardProperties && IS_HANDLE_ACTIVE(pDefEntry->pRewardProperties->hRewardTable))) {
			MEExpanderRefreshLabel(&pGroup->pRewardTableLabel, "Reward Table", "Reward table granted on successful interact. (required)", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
			if (pPropEntry->pRewardProperties) {
				MEExpanderRefreshGlobalDictionaryField(&pGroup->pRewardTableField, pOrigPropEntry ? pOrigPropEntry->pRewardProperties : NULL, pPropEntry->pRewardProperties, parse_WorldRewardInteractionProperties, "RewardTable", "RewardTable",
										UI_WIDGET(pGroup->pExpander), pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL2, y, 0, 1.0, UIUnitPercentage, 0, pGroup->cbChange, pGroup->cbPreChange, pGroup->pParentData);
				ui_WidgetQueueFreeAndNull(&pGroup->pRewardTableValueLabel);
			} else {
				MEExpanderRefreshLabel(&pGroup->pRewardTableValueLabel, REF_STRING_FROM_HANDLE(pDefEntry->pRewardProperties->hRewardTable), NULL, pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL2, 0, y, UI_WIDGET(pGroup->pExpander));
				MEFieldSafeDestroy(&pGroup->pRewardTableField);
			}
			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pRewardTableLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pRewardTableValueLabel);
			MEFieldSafeDestroy(&pGroup->pRewardTableField);
		}

		if (pPropEntry->pRewardProperties || (pDefEntry && pDefEntry->pRewardProperties)) {
			MEExpanderRefreshLabel(&pGroup->pRewardLevelTypeLabel, "Reward Level", "Where to get the level used when generating rewards.", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
			if (pPropEntry->pRewardProperties) {
				MEExpanderRefreshEnumField(&pGroup->pRewardLevelTypeField, pOrigPropEntry ? pOrigPropEntry->pRewardProperties : NULL, pPropEntry->pRewardProperties, parse_WorldRewardInteractionProperties, "RewardLevelType", WorldRewardLevelTypeEnum,
										UI_WIDGET(pGroup->pExpander), pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL2, y, 0, 100, UIUnitFixed, 0, pGroup->cbChange, pGroup->cbPreChange, pGroup->pParentData);
				ui_WidgetQueueFreeAndNull(&pGroup->pRewardLevelTypeValueLabel);
			} else {
				MEExpanderRefreshLabel(&pGroup->pRewardLevelTypeValueLabel, StaticDefineIntRevLookup(WorldRewardLevelTypeEnum, pDefEntry->pRewardProperties->eRewardLevelType), NULL, pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL2, 0, y, UI_WIDGET(pGroup->pExpander));
				MEFieldSafeDestroy(&pGroup->pRewardLevelTypeField);
			}
			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pRewardLevelTypeLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pRewardLevelTypeValueLabel);
			MEFieldSafeDestroy(&pGroup->pRewardLevelTypeField);
		}

		if ((pPropEntry->pRewardProperties && pPropEntry->pRewardProperties->eRewardLevelType == WorldRewardLevelType_Custom) ||
			(pDefEntry && pDefEntry->pRewardProperties && pDefEntry->pRewardProperties->eRewardLevelType == WorldRewardLevelType_Custom)) {
			MEExpanderRefreshLabel(&pGroup->pRewardLevelLabel, "Custom Level", "The level to use when generating rewards.", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
			if (pPropEntry->pRewardProperties) {
				MEExpanderRefreshSimpleField(&pGroup->pRewardLevelField, pOrigPropEntry ? pOrigPropEntry->pRewardProperties : NULL, pPropEntry->pRewardProperties, parse_WorldRewardInteractionProperties, "CustomRewardLevel", kMEFieldType_TextEntry,
										UI_WIDGET(pGroup->pExpander), pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL2, y, 0, 80, UIUnitFixed, 0, pGroup->cbChange, pGroup->cbPreChange, pGroup->pParentData);
				ui_WidgetQueueFreeAndNull(&pGroup->pRewardLevelValueLabel);
			} else {
				sprintf(buf, "%d", pDefEntry->pRewardProperties->uCustomRewardLevel);
				MEExpanderRefreshLabel(&pGroup->pRewardLevelValueLabel, buf, NULL, pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL2, 0, y, UI_WIDGET(pGroup->pExpander));
				MEFieldSafeDestroy(&pGroup->pRewardLevelField);
			}
			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pRewardLevelLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pRewardLevelValueLabel);
			MEFieldSafeDestroy(&pGroup->pRewardLevelField);
		}
		
		if ((pPropEntry->pRewardProperties && pPropEntry->pRewardProperties->eRewardLevelType == WorldRewardLevelType_MapVariable) ||
			(pDefEntry && pDefEntry->pRewardProperties && pDefEntry->pRewardProperties->eRewardLevelType == WorldRewardLevelType_MapVariable)) {
			MEExpanderRefreshLabel(&pGroup->pRewardLevelMapVarLabel, "Map Variable", "The map variable to use as level when generating rewards.", pGroup->fOffsetX+X_OFFSET_INTERACT_BASE2, 0, y, UI_WIDGET(pGroup->pExpander));
			if (pPropEntry->pRewardProperties) {
				MEExpanderRefreshSimpleField(&pGroup->pRewardLevelMapVarField, pOrigPropEntry ? pOrigPropEntry->pRewardProperties : NULL, pPropEntry->pRewardProperties, parse_WorldRewardInteractionProperties, "MapVarName", kMEFieldType_TextEntry,
										UI_WIDGET(pGroup->pExpander), pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL2, y, 0, 80, UIUnitFixed, 0, pGroup->cbChange, pGroup->cbPreChange, pGroup->pParentData);
				ui_WidgetQueueFreeAndNull(&pGroup->pRewardLevelMapVarValueLabel);
			} else {
				sprintf(buf, "%s", pDefEntry->pRewardProperties->pcMapVarName);
				MEExpanderRefreshLabel(&pGroup->pRewardLevelMapVarValueLabel, buf, NULL, pGroup->fOffsetX+X_OFFSET_INTERACT_CONTROL2, 0, y, UI_WIDGET(pGroup->pExpander));
				MEFieldSafeDestroy(&pGroup->pRewardLevelMapVarField);
			}
			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pRewardLevelMapVarLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pRewardLevelMapVarValueLabel);
			MEFieldSafeDestroy(&pGroup->pRewardLevelMapVarField);
		}
	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pRewardTableLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pRewardLevelTypeLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pRewardLevelLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pRewardLevelMapVarLabel);
		MEFieldSafeDestroy(&pGroup->pRewardTableField);
		MEFieldSafeDestroy(&pGroup->pRewardLevelTypeField);
		MEFieldSafeDestroy(&pGroup->pRewardLevelField);
		MEFieldSafeDestroy(&pGroup->pRewardLevelMapVarField);
	}

	return y;
}


//---------------------------------------------------------------------------------------------------
// UI Logic
//---------------------------------------------------------------------------------------------------

static void IEAddFieldToParent(MEField *pField, UIWidget *pParent, F32 x, F32 y, F32 xPercent, F32 w, UIUnitType wUnit, F32 padRight, InteractionEditDoc *pDoc)
{
	MEFieldAddToParent(pField, pParent, x, y);
	ui_WidgetSetPositionEx(pField->pUIWidget, x, y, xPercent, 0, UITopLeft);
	ui_WidgetSetWidthEx(pField->pUIWidget, w, wUnit);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, padRight, 0, 0);
	MEFieldSetChangeCallback(pField, IEFieldChangedCB, pDoc);
	MEFieldSetPreChangeCallback(pField, IEFieldPreChangeCB, pDoc);
}


static UIExpander *IECreateExpander(UIExpanderGroup *pExGroup, const char *pcName, int index)
{
	UIExpander *pExpander = ui_ExpanderCreate(pcName, 0);
	ui_WidgetSkin(UI_WIDGET(pExpander), gBoldExpanderSkin);
	ui_ExpanderGroupInsertExpander(pExGroup, pExpander, index);
	ui_ExpanderSetOpened(pExpander, 1);

	return pExpander;
}


// This is called whenever any def data changes to do cleanup
static void IEInteractionDefChanged(InteractionEditDoc *pDoc, bool bUndoable)
{
	if (!pDoc->bIgnoreFieldChanges) {
		IEUpdateDisplay(pDoc);

		if (bUndoable) {
			IEUndoData *pData = calloc(1, sizeof(IEUndoData));
			pData->pPreDef = pDoc->pNextUndoDef;
			pData->pPostDef = StructClone(parse_InteractionDef, pDoc->pDef);
			EditCreateUndoCustom(pDoc->emDoc.edit_undo_stack, IEInteractionDefUndoCB, IEInteractionDefRedoCB, IEInteractionDefUndoFreeCB, pData);
			pDoc->pNextUndoDef = StructClone(parse_InteractionDef, pDoc->pDef);
		}
	}
}


// This is called by MEField prior to allowing an edit
static bool IEFieldPreChangeCB(MEField *pField, bool bFinished, InteractionEditDoc *pDoc)
{
	// Make sure the resource is checked out of Gimme
	return emDocIsEditable(&pDoc->emDoc, true);
}


// This is called when an MEField is changed
static void IEFieldChangedCB(MEField *pField, bool bFinished, InteractionEditDoc *pDoc)
{
	IEInteractionDefChanged(pDoc, bFinished);
}


static void IESetScopeCB(MEField *pField, bool bFinished, InteractionEditDoc *pDoc)
{
	if (!pDoc->bIgnoreFilenameChanges) {
		// Update the filename appropriately
		resFixFilename(g_InteractionDefDictionary, pDoc->pDef->pcName, pDoc->pDef);
	}

	// Call on to do regular updates
	IEFieldChangedCB(pField, bFinished, pDoc);
}

static void IESetNameCB(MEField *pField, bool bFinished, InteractionEditDoc *pDoc)
{
	MEFieldFixupNameString(pField, &pDoc->pDef->pcName);

	// When the name changes, change the title of the window
	ui_WindowSetTitle(pDoc->pMainWindow, pDoc->pDef->pcName);

	// Make sure the browser picks up the new def name if the name changed
	sprintf(pDoc->emDoc.doc_name, "%s", pDoc->pDef->pcName);
	sprintf(pDoc->emDoc.doc_display_name, "%s", pDoc->pDef->pcName);
	pDoc->emDoc.name_changed = 1;

	// Call the scope function to avoid duplicating logic
	IESetScopeCB(pField, bFinished, pDoc);
}


static UILabel *IERefreshLabel(UILabel *pLabel, const char *pcText, const char *pcTooltip, F32 x, F32 xPercent, F32 y, UIExpander *pExpander)
{
	if (!pLabel) {
		pLabel = ui_LabelCreate(pcText, x, y);
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

static UISeparator* IERefreshSeparator(UISeparator *pSeparator, F32 y, UIExpander *pExpander)
{
	if (!pSeparator) {
		pSeparator = ui_SeparatorCreate(UIHorizontal);
		ui_ExpanderAddChild(pExpander, pSeparator);
	}

	ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);

	return pSeparator;
}

static void IERefreshInfoExpander(InteractionEditDoc *pDoc)
{
	UIExpander *pExpander = pDoc->pInfoExpander;
	F32 y = 0;

	MEExpanderRefreshLabel(&pDoc->pTypeLabel, "Interaction Type", "The type of interaction this definition is usable on.  'All' works on all types.", X_OFFSET_BASE, 0, y, UI_WIDGET(pExpander));
	MEExpanderRefreshEnumField(&pDoc->pTypeField, pDoc->pOrigDef, pDoc->pDef, parse_InteractionDef, "Type", InteractionDefTypeEnum,
							   UI_WIDGET(pExpander), X_OFFSET_BASE+X_OFFSET_INTERACT_CONTROL, y, 0, 140, UIUnitFixed, 5, IEFieldChangedCB, IEFieldPreChangeCB, pDoc);
	y += STANDARD_ROW_HEIGHT;

	// Set the expander height
	ui_ExpanderSetHeight(pExpander, y);
}

static void IERefreshPropsExpander(InteractionEditDoc *pDoc)
{
	F32 y = 0.0;

	if (!pDoc->pPropsGroup) {
		pDoc->pPropsGroup = calloc(1, sizeof(InteractionPropertiesGroup));
		pDoc->pPropsGroup->pExpander = pDoc->pPropsExpander;
		pDoc->pPropsGroup->cbChange = IEFieldChangedCB;
		pDoc->pPropsGroup->cbPreChange = IEFieldPreChangeCB;
		pDoc->pPropsGroup->pParentData = pDoc;
		pDoc->pPropsGroup->fOffsetX = X_OFFSET_BASE;
		pDoc->pPropsGroup->bDefEditMode = true;
	}
	pDoc->pPropsGroup->eType = pDoc->pDef->eType;

	y = UpdateInteractionPropertiesGroup(pDoc->pPropsGroup, pDoc->pDef->pEntry, pDoc->pOrigDef ? pDoc->pOrigDef->pEntry : NULL, NULL, y);

	// Set the expander height
	ui_ExpanderSetHeight(pDoc->pPropsExpander, y);
}

static void IEUpdateDisplay(InteractionEditDoc *pDoc)
{
	int i;

	// Ignore changes while UI refreshes
	pDoc->bIgnoreFieldChanges = true;

	// Refresh doc-level fields
	for(i=eaSize(&pDoc->eaDocFields)-1; i>=0; --i) {
		MEFieldSetAndRefreshFromData(pDoc->eaDocFields[i], pDoc->pOrigDef, pDoc->pDef);
	}

	// Refresh the dynamic expanders
	IERefreshInfoExpander(pDoc);
	IERefreshPropsExpander(pDoc);

	// Update non-field UI components
	ui_GimmeButtonSetName(pDoc->pFileButton, pDoc->pDef->pcName);
	ui_GimmeButtonSetReferent(pDoc->pFileButton, pDoc->pDef);
	ui_LabelSetText(pDoc->pFilenameLabel, pDoc->pDef->pcFilename);

	// Update saved flag
	pDoc->emDoc.saved = pDoc->pOrigDef && (StructCompare(parse_InteractionDef, pDoc->pOrigDef, pDoc->pDef, 0, 0, 0) == 0);

	// Start paying attention to changes again
	pDoc->bIgnoreFieldChanges = false;
}


//---------------------------------------------------------------------------------------------------
// UI Initialization
//---------------------------------------------------------------------------------------------------


static UIWindow *IEInitMainWindow(InteractionEditDoc *pDoc)
{
	UIWindow *pWin;
	UILabel *pLabel;
	MEField *pField;
	UISeparator *pSeparator;
	F32 y = 0;
	F32 fBottomY = 0;
	F32 fTopY = 0;

	// Create the window
	pWin = ui_WindowCreate(pDoc->pDef->pcName, 15, 50, 450, 600);
	EditorPrefGetWindowPosition(INTERACTION_EDITOR, "Window Position", "Main", pWin);

	// Name
	pLabel = ui_LabelCreate("Name", 0, y);
	ui_WindowAddChild(pWin, pLabel);
	pField = MEFieldCreateSimple(kMEFieldType_TextEntry, pDoc->pOrigDef, pDoc->pDef, parse_InteractionDef, "Name");
	IEAddFieldToParent(pField, UI_WIDGET(pWin), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 21, pDoc);
	MEFieldSetChangeCallback(pField, IESetNameCB, pDoc);
	eaPush(&pDoc->eaDocFields, pField);

	y += STANDARD_ROW_HEIGHT;

	// Scope
	pLabel = ui_LabelCreate("Scope", 0, y);
	ui_WindowAddChild(pWin, pLabel);
	pField = MEFieldCreateSimpleDataProvided(kMEFieldType_TextEntry, pDoc->pOrigDef, pDoc->pDef, parse_InteractionDef, "Scope", NULL, &geaScopes, NULL);
	IEAddFieldToParent(pField, UI_WIDGET(pWin), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 21, pDoc);
	MEFieldSetChangeCallback(pField, IESetScopeCB, pDoc);
	eaPush(&pDoc->eaDocFields, pField);

	y += STANDARD_ROW_HEIGHT;

	// File Name
	pLabel = ui_LabelCreate("File Name", 0, y);
	ui_WindowAddChild(pWin, pLabel);
	pDoc->pFileButton = ui_GimmeButtonCreate(X_OFFSET_CONTROL, y, "InteractionDef", pDoc->pDef->pcName, pDoc->pDef);
	ui_WindowAddChild(pWin, pDoc->pFileButton);
	pLabel = ui_LabelCreate(pDoc->pDef->pcFilename, X_OFFSET_CONTROL+20, y);
	ui_WindowAddChild(pWin, pLabel);
	ui_WidgetSetWidthEx(UI_WIDGET(pLabel), 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(UI_WIDGET(pLabel), 0, 21, 0, 0);
	pDoc->pFilenameLabel = pLabel;

	y += STANDARD_ROW_HEIGHT;

	// Comments
	pLabel = ui_LabelCreate("Comments", 0, y);
	ui_WindowAddChild(pWin, pLabel);
	pField = MEFieldCreateSimple(kMEFieldType_MultiText, pDoc->pOrigDef, pDoc->pDef, parse_InteractionDef, "Comments");
	IEAddFieldToParent(pField, UI_WIDGET(pWin), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 21, pDoc);
	eaPush(&pDoc->eaDocFields, pField);

	y += STANDARD_ROW_HEIGHT;

	pSeparator = ui_SeparatorCreate(UIHorizontal);
	ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
	ui_WindowAddChild(pWin, pSeparator);

	y += SEPARATOR_HEIGHT;

	// Main expander group
	pDoc->pExpanderGroup = ui_ExpanderGroupCreate();
	ui_WidgetSetPosition(UI_WIDGET(pDoc->pExpanderGroup), 0, y);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pDoc->pExpanderGroup), 1.0, 1.0, UIUnitPercentage, UIUnitPercentage);
	ui_WindowAddChild(pWin, pDoc->pExpanderGroup);

	// Define expanders
	pDoc->pInfoExpander = IECreateExpander(pDoc->pExpanderGroup, "Information", 0);
	pDoc->pPropsExpander = IECreateExpander(pDoc->pExpanderGroup, "Properties", 1);

	// Refresh the dynamic expanders
	IERefreshInfoExpander(pDoc);
	IERefreshPropsExpander(pDoc);

	return pWin;
}


static void IEInitDisplay(EMEditor *pEditor, InteractionEditDoc *pDoc)
{
	// Create the window (ignore field change callbacks during init)
	pDoc->bIgnoreFieldChanges = true;
	pDoc->bIgnoreFilenameChanges = true;
	pDoc->pMainWindow = IEInitMainWindow(pDoc);
	pDoc->bIgnoreFieldChanges = false;
	pDoc->bIgnoreFilenameChanges = false;

	// Show the window
	ui_WindowPresent(pDoc->pMainWindow);

	// Editor Manager needs to be told about the windows used
	pDoc->emDoc.primary_ui_window = pDoc->pMainWindow;
	eaPush(&pDoc->emDoc.ui_windows, pDoc->pMainWindow);

	// Update the rest of the UI
	IEUpdateDisplay(pDoc);
}


static void IEInitToolbarsAndMenus(EMEditor *pEditor)
{
	EMToolbar *pToolbar;

	// Toolbar
	pToolbar = emToolbarCreateFileToolbar(EM_FILE_TOOLBAR_NEW|EM_FILE_TOOLBAR_OPEN|EM_FILE_TOOLBAR_SAVE);
	eaPush(&pEditor->toolbars, pToolbar);
	eaPush(&pEditor->toolbars, emToolbarCreateWindowToolbar());

	// File menu
	emMenuItemCreate(pEditor, "ie_revertinteraction", "Revert", NULL, NULL, "IE_RevertInteraction");
	emMenuRegister(pEditor, emMenuCreate(pEditor, "File", "ie_revertinteraction", NULL));
}


//---------------------------------------------------------------------------------------------------
// Public Interface
//---------------------------------------------------------------------------------------------------

void IEInitData(EMEditor *pEditor)
{
	if (pEditor && !gInitializedEditor) {
		gBoldExpanderSkin = ui_SkinCreate(NULL);
		SET_HANDLE_FROM_STRING(g_ui_FontDict, "Default_Bold", gBoldExpanderSkin->hNormal);

		IEInitToolbarsAndMenus(pEditor);

		// Have Editor Manager handle a lot of change tracking
		emAutoHandleDictionaryStateChange(pEditor, "InteractionDef", true, NULL, NULL, NULL, NULL, NULL);

		resGetUniqueScopes(g_InteractionDefDictionary, &geaScopes);

		gInitializedEditor = true;

		// Request all guild stat defs from the server
		resRequestAllResourcesInDictionary("GuildStatDef");
	}

	if (!gInitializedEditorData) {
		// Make sure lists refresh if dictionary changes
		resDictRegisterEventCallback(g_InteractionDefDictionary, IEContentDictChanged, NULL);

		gInitializedEditorData = true;
	}
}


static void IEFixupMessages(InteractionDef *pDef)
{
	char scope[260];
	char nameSpace[RESOURCE_NAME_MAX_SIZE];
	char baseObjectName[RESOURCE_NAME_MAX_SIZE];
	char baseMessageKey[RESOURCE_NAME_MAX_SIZE];

	if (resExtractNameSpace(pDef->pcName, nameSpace, baseObjectName))
	{
		sprintf(baseMessageKey, "%s:InteractionDef.%s", nameSpace, baseObjectName);
	}
	else
	{
		sprintf(baseMessageKey, "InteractionDef.%s", pDef->pcName);
	}

	if (pDef->pcScope) {
		sprintf(scope, "InteractionDef/%s/%s", pDef->pcScope, pDef->pcName);
	} else {
		sprintf(scope, "InteractionDef/%s", pDef->pcName);
	}

	// Fixup property messages
	interaction_FixupMessages(pDef->pEntry, scope, baseMessageKey, "Main", 0);
}

static void IEDefPostOpenFixup(InteractionDef *pDef)
{
	if (!pDef->pEntry) {
		pDef->pEntry = StructCreate(parse_WorldInteractionPropertyEntry);
		pDef->pEntry->pcInteractionClass = allocAddString("Clickable");
		pDef->pEntry->bExclusiveInteraction = 1;
		pDef->pEntry->bUseExclusionFlag = 1;
	}

	// Make editor copy
	langMakeEditorCopy(parse_InteractionDef, pDef, true);

	// Fix messages
	IEFixupMessages(pDef);

	// Clean expressions
	interaction_CleanProperties(pDef->pEntry);
}


static void IEDefPreSaveFixup(InteractionDef *pDef)
{
	// Fix messages
	IEFixupMessages(pDef);
}


static InteractionEditDoc *IEInitDoc(InteractionDef *pDef, bool bCreated, bool bEmbedded)
{
	InteractionEditDoc *pDoc;
	char nameBuf[260];

	// Initialize the structure
	pDoc = (InteractionEditDoc*)calloc(1,sizeof(InteractionEditDoc));

	// Fill in the def data
	if (bCreated) {
		pDoc->pDef = StructCreate(parse_InteractionDef);
		assert(pDoc->pDef);
		emMakeUniqueDocName(&pDoc->emDoc, "New_Interaction_Def", "InteractionDef", "InteractionDef");
		pDoc->pDef->pcName = StructAllocString(pDoc->emDoc.doc_name);
		sprintf(nameBuf, "defs/interactiondef/%s.interactiondef", pDoc->pDef->pcName);
		pDoc->pDef->pcFilename = allocAddString(nameBuf);
		IEDefPostOpenFixup(pDoc->pDef);
	} else {
		pDoc->pDef = StructClone(parse_InteractionDef, pDef);
		assert(pDoc->pDef);
		IEDefPostOpenFixup(pDoc->pDef);
		pDoc->pOrigDef = StructClone(parse_InteractionDef, pDoc->pDef);
	}

	// Set up the undo stack
	pDoc->emDoc.edit_undo_stack = EditUndoStackCreate();
	EditUndoSetContext(pDoc->emDoc.edit_undo_stack, pDoc);
	pDoc->pNextUndoDef = StructClone(parse_InteractionDef, pDoc->pDef);

	return pDoc;
}


InteractionEditDoc *IEOpenInteractionDef(EMEditor *pEditor, char *pcName)
{
	InteractionEditDoc *pDoc = NULL;
	InteractionDef *pDef = NULL;
	bool bCreated = false;

	if (pcName && resIsEditingVersionAvailable(g_InteractionDefDictionary, pcName)) {
		// Simply open the object since it is in the dictionary
		pDef = RefSystem_ReferentFromString(g_InteractionDefDictionary, pcName);
	} else if (pcName) {
		// Wait for object to show up so we can open it
		resSetDictionaryEditMode(g_InteractionDefDictionary, true);
		emSetResourceState(pEditor, pcName, EMRES_STATE_OPENING);
		resRequestOpenResource(g_InteractionDefDictionary, pcName);
	} else {
		// Create a new object since it is not in the dictionary
		bCreated = true;
	}

	if (pDef || bCreated) {
		pDoc = IEInitDoc(pDef, bCreated, false);
		IEInitDisplay(pEditor, pDoc);
		resFixFilename(g_InteractionDefDictionary, pDoc->pDef->pcName, pDoc->pDef);
	}

	return pDoc;
}


void IERevertInteractionDef(InteractionEditDoc *pDoc)
{
	InteractionDef *pDef;

	if (!pDoc->emDoc.orig_doc_name[0]) {
		// Cannot revert if no original
		return;
	}

	pDef = RefSystem_ReferentFromString(g_InteractionDefDictionary, pDoc->emDoc.orig_doc_name);
	if (pDef) {
		// Revert the def
		StructDestroy(parse_InteractionDef, pDoc->pDef);
		StructDestroy(parse_InteractionDef, pDoc->pOrigDef);
		pDoc->pDef = StructClone(parse_InteractionDef, pDef);
		IEDefPostOpenFixup(pDoc->pDef);
		pDoc->pOrigDef = StructClone(parse_InteractionDef, pDoc->pDef);

		// Clear the undo stack on revert
		EditUndoStackClear(pDoc->emDoc.edit_undo_stack);
		StructDestroy(parse_InteractionDef, pDoc->pNextUndoDef);
		pDoc->pNextUndoDef = StructClone(parse_InteractionDef, pDoc->pDef);

		// Refresh the UI
		pDoc->bIgnoreFieldChanges = true;
		pDoc->bIgnoreFilenameChanges = true;
		IEUpdateDisplay(pDoc);
		pDoc->bIgnoreFieldChanges = false;
		pDoc->bIgnoreFilenameChanges = false;
	} 
}


void IECloseInteractionDef(InteractionEditDoc *pDoc)
{
	// Free doc fields
	eaDestroyEx(&pDoc->eaDocFields, MEFieldDestroy);

	// Free info fields
	ui_WidgetQueueFree((UIWidget*) pDoc->pTypeLabel);
	MEFieldSafeDestroy(&pDoc->pTypeField);

	// Free the groups
	FreeInteractionPropertiesGroup(pDoc->pPropsGroup);
	
	// Free the objects
	StructDestroy(parse_InteractionDef, pDoc->pDef);
	if (pDoc->pOrigDef) {
		StructDestroy(parse_InteractionDef, pDoc->pOrigDef);
	}
	StructDestroy(parse_InteractionDef, pDoc->pNextUndoDef);

	// Close the window
	ui_WindowHide(pDoc->pMainWindow);
	ui_WidgetQueueFree(UI_WIDGET(pDoc->pMainWindow));
}


EMTaskStatus IESaveInteractionDef(InteractionEditDoc* pDoc, bool bSaveAsNew)
{
	EMTaskStatus status;
	const char *pcName;
	InteractionDef *pDefCopy;

	// Deal with state changes
	pcName = pDoc->pDef->pcName;
	if (emHandleSaveResourceState(pDoc->emDoc.editor, pcName, &status)) {
		return status;
	}

	// Do cleanup before validation
	pDefCopy = StructClone(parse_InteractionDef, pDoc->pDef);
	IEDefPreSaveFixup(pDefCopy);

	// Perform validation
	if (!interactiondef_Validate(pDefCopy)) {
		StructDestroy(parse_InteractionDef, pDefCopy);
		return EM_TASK_FAILED;
	}

	// Do the save (which will free the copy)
	status = emSmartSaveDoc(&pDoc->emDoc, pDefCopy, pDoc->pOrigDef, bSaveAsNew);

	return status;
}

#endif
