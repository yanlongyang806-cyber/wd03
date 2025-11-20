#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/


#ifndef NO_EDITORS

#include "EditorManager.h"
#include "interaction_common.h"
#include "MultiEditField.h"

typedef struct GEVariableDefGroup GEVariableDefGroup;
typedef struct MEField MEField;
typedef struct UIButton UIButton;
typedef struct UICheckButton UICheckButton;
typedef struct UIExpander UIExpander;
typedef struct UIExpanderGroup UIExpanderGroup;
typedef struct UIGameActionEditButton UIGameActionEditButton;
typedef struct UIGimmeButton UIGimmeButton;
typedef struct UILabel UILabel;
typedef struct UIWindow UIWindow;
typedef struct WorldInteractionPropertyEntry WorldInteractionPropertyEntry;
typedef struct WorldVariableDef WorldVariableDef;


#define INTERACTION_EDITOR "Interaction Def Editor"
#define DEFAULT_INTERACTION_DEF_NAME     "New_Interaction_Def"

// ---- Shared Interaction Properties Editing ----

typedef struct InteractionPropertiesGroup
{
	WorldInteractionPropertyEntry *pProperties;
	InteractionDefType eType;
	bool bDefEditMode;    // True when editing an InteractionDef
	bool bHideClassField; // Used when don't want to offer changing the class
	bool bShowDefValues;
	bool bPriorityChanged;

	UIExpander *pExpander;	// Unowned; this is the parent's expander
	F32 fOffsetX;

	MEFieldChangeCallback cbChange;
	MEFieldPreChangeCallback cbPreChange;
	void *pParentData;	// Data for the parent

	// Base
	UILabel *pGeneralSectionLabel;
	UILabel *pClassLabel;
	UILabel *pDefClassLabel;
	UILabel *pDefClassValueLabel;
	UICheckButton *pInteractButton;
	UILabel *pInteractCondLabel;
	UILabel *pInteractCondValueLabel;
	UILabel *pSuccessCondLabel;
	UILabel *pSuccessCondValueLabel;
	UILabel *pAttemptableCondLabel;
	UILabel *pAttemptableCondValueLabel;
	UICheckButton *pVisibleButton;
	UILabel *pVisibleExprLabel;
	UILabel *pVisibleExprValueLabel;
	UICheckButton *pCategoryButton;
	UILabel *pCategoryLabel;
	UILabel *pCategoryValueLabel;
	UILabel *pPriorityLabel;
	UILabel *pPriorityValueLabel;
	UILabel *pAutoExecLabel;
	UILabel *pAutoExecValueLabel;
	UILabel *pDisablePowersInterruptLabel;
	UILabel *pDisablePowersInterruptValueLabel;
	UILabel *pAllowDuringCombatLabel;
	UILabel *pAllowDuringCombatValueLabel;
	UILabel *pExclusiveLabel;
	UICheckButton *pExclusiveButton;
	UILabel *pExclusiveValueLabel;
	// Time
	UILabel *pTimeSectionLabel;
	UICheckButton *pTimeButton;
	UILabel *pUseTimeLabel;
	UILabel *pUseTimeValueLabel;
	UILabel *pActiveTimeLabel;
	UILabel *pActiveTimeValueLabel;
	UILabel *pNoRespawnLabel;
	UILabel *pNoRespawnValueLabel;
	UILabel *pCooldownTimeLabel;
	UILabel *pCooldownTimeValueLabel;
	UILabel *pCustomCooldownLabel;
	UILabel *pCustomCooldownValueLabel;
	UILabel *pDynamicCooldownLabel;
	UILabel *pDynamicCooldownValueLabel;
	UILabel *pTeamUsableWhenActiveLabel;
	UILabel *pTeamUsableWhenActiveValueLabel;
	UILabel *pHideDuringCooldownLabel;
	UILabel *pHideDuringCooldownValueLabel;
	UILabel *pInterruptOnPowerLabel;
	UILabel *pInterruptOnDamageLabel;
	UILabel *pInterruptOnMoveLabel;
	UILabel *pInterruptLabel;
	UILabel *pInterruptValueLabel;
	// Text
	UICheckButton *pTextButton;
	UILabel *pUsabilityTextLabel;
	UILabel *pUsabilityTextValueLabel;
	UILabel *pInteractTextLabel;
	UILabel *pInteractTextValueLabel;
	UILabel *pDetailTextLabel;
	UILabel *pDetailTextValueLabel;
	UILabel *pDetailTextureLabel;
	UISprite *pDetailTextureValueSprite;
	UILabel *pSuccessTextLabel;
	UILabel *pSuccessTextValueLabel;
	UILabel *pFailureTextLabel;
	UILabel *pFailureTextValueLabel;
	// Reward
	UILabel *pRewardSectionLabel;
	UICheckButton *pRewardButton;
	UILabel *pRewardTableLabel;
	UILabel *pRewardTableValueLabel;
	UILabel *pRewardLevelTypeLabel;
	UILabel *pRewardLevelTypeValueLabel;
	UILabel *pRewardLevelLabel;
	UILabel *pRewardLevelValueLabel;
	UILabel *pRewardLevelMapVarLabel;
	UILabel *pRewardLevelMapVarValueLabel;
	// Animation
	UICheckButton *pAnimationButton;
	UILabel *pAnimLabel;
	UILabel *pAnimValueLabel;
	// Sound
	UILabel *pSoundSectionLabel;
	UICheckButton *pSoundButton;
	UILabel *pAttemptSoundLabel;
	UILabel *pAttemptSoundValueLabel;
	UILabel *pSuccessSoundLabel;
	UILabel *pSuccessSoundValueLabel;
	UILabel *pFailureSoundLabel;
	UILabel *pFailureSoundValueLabel;
	UILabel *pInterruptSoundLabel;
	UILabel *pInterruptSoundValueLabel;
	// Action
	UILabel *pActionSectionLabel;
	UICheckButton *pActionButton;
	UILabel *pAttemptActionLabel;
	UILabel *pAttemptActionValueLabel;
	UILabel *pSuccessActionLabel;
	UILabel *pSuccessActionValueLabel;
	UILabel *pFailureActionLabel;
	UILabel *pFailureActionValueLabel;
	UILabel *pInterruptActionLabel;
	UILabel *pInterruptActionValueLabel;
	UILabel *pNoLongerActiveActionLabel;
	UILabel *pNoLongerActiveActionValueLabel;
	UILabel *pCooldownActionLabel;
	UILabel *pCooldownActionValueLabel;
	UILabel *pSuccessGameActionsLabel;
	UILabel *pSuccessGameActionsValueLabel;
	UILabel *pFailureGameActionsLabel;
	UILabel *pFailureGameActionsValueLabel;
	UIGameActionEditButton *pSuccessGameActionsButton;
	UIGameActionEditButton *pFailureGameActionsButton;
	// Contact
	UILabel *pContactSectionLabel;
	UICheckButton *pContactButton;
	UILabel *pContactLabel;
	UILabel *pContactValueLabel;
	UILabel *pContactDialogLabel;
	UILabel *pContactDialogValueLabel;
	// Crafting
	UILabel *pCraftingSectionLabel;
	UILabel *pCraftSkillLabel;
	UILabel *pCraftSkillValueLabel;
	UILabel *pCraftMaxSkillLabel;
	UILabel *pCraftMaxSkillValueLabel;
	UILabel *pCraftReward1Label;
	UILabel *pCraftReward1ValueLabel;
	UILabel *pCraftReward2Label;
	UILabel *pCraftReward2ValueLabel;
	UILabel *pCraftReward3Label;
	UILabel *pCraftReward3ValueLabel;
	// Destructible
	UILabel *pDestructibeSectionLabel;
	UILabel *pCritterLabel;
	UILabel *pCritterOverrideLabel;
	UILabel *pCritterLevelLabel;
	UILabel *pDisplayNameLabel;
	UILabel *pEntityNameLabel;
	UILabel *pRespawnTimeLabel;
	UILabel *pDeathPowerLabel;
	// Door
	UICheckButton *pDoorButton;
	UILabel *pDoorSectionLabel;
	UILabel *pDoorTypeLabel;
	UILabel *pDoorTypeValueLabel;
	UILabel *pDoorIDLabel;
	UILabel *pDoorIDValueLabel;
	UILabel *pDoorQueueLabel;
	UILabel *pDoorQueueValueLabel;
	UILabel *pDoorKeyLabel;
	UILabel *pDoorKeyValueLabel;
	UILabel *pPerPlayerDoorLabel;
	UILabel *pPerPlayerDoorValueLabel;
	UILabel *pSinglePlayerDoorLabel;
	UILabel *pSinglePlayerDoorValueLabel;
	UILabel *pAllowJoinTeamAtDoorLabel;
	UILabel *pAllowJoinTeamAtDoorValueLabel;
	UILabel *pCollectDestStatusLabel;
	UILabel *pCollectDestStatusValueLabel;
    UILabel *pDestinationSameOwnerLabel;
    UILabel *pDestinationSameOwnerValueLabel;
	UILabel *pDoorTransitionLabel;
	UILabel *pDoorTransitionValueLabel;
	UILabel* pIncludeTeammatesDoorValueLabel;
	UILabel* pIncludeTeammatesDoorLabel;
	UILabel *pDefDoor1Label;
	UILabel *pDefDoor1ValueLabel;
	UILabel *pDefDoor2Label;
	UILabel *pDefDoor2ValueLabel;
	UIButton *pVarAddButton;
	// FromDefinition
	UILabel *pFromDefSectionLabel;
	UILabel *pInteractDefLabel;
	UICheckButton *pShowDefValues;

	// Base
	MEField *pClassField;
	MEField *pInteractCondField;
	MEField *pSuccessCondField;
	MEField *pAttemptableCondField;
	MEField *pVisibleExprField;
	MEField *pCategoryField;
	MEField *pPriorityField;
	MEField *pInteractionCategoryField;
	MEField *pAutoExecField;
	MEField *pDisablePowersInterruptField;
	MEField *pAllowDuringCombatField;
	// Time
	MEField *pUseTimeField;
	MEField *pActiveTimeField;
	MEField *pCooldownTimeField;
	MEField *pCustomCooldownField;
	MEField *pDynamicCooldownField;
	MEField *pTeamUsableWhenActiveField;
	MEField *pHideDuringCooldownField;
	MEField *pInterruptOnPowerField;
	MEField *pInterruptOnDamageField;
	MEField *pInterruptOnMoveField;
	MEField *pNoRespawnField;
	// Text
	MEField *pUsabilityTextField;
	MEField *pInteractTextField;
	MEField *pDetailTextField;
	MEField *pDetailTextureField;
	MEField *pSuccessTextField;
	MEField *pFailureTextField;
	// Animation
	MEField *pAnimField;
	// Sound
	MEField *pAttemptSoundField;
	MEField *pSuccessSoundField;
	MEField *pFailureSoundField;
	MEField *pInterruptSoundField;
	// Action
	MEField *pAttemptActionField;
	MEField *pSuccessActionField;
	MEField *pFailureActionField;
	MEField *pInterruptActionField;
	MEField *pNoLongerActiveActionField;
	MEField *pCooldownActionField;
	// Reward
	MEField *pRewardTableField;
	MEField *pRewardLevelTypeField;
	MEField *pRewardLevelField;
	MEField *pRewardLevelMapVarField;
	// Contact
	MEField *pContactField;
	MEField *pContactDialogField;
	// Crafting
	MEField *pCraftSkillField;
	MEField *pCraftMaxSkillField;
	MEField *pCraftReward1Field;
	MEField *pCraftReward2Field;
	MEField *pCraftReward3Field;
	// Destructible
	MEField *pCritterField;
	MEField *pCritterOverrideField;
	MEField *pCritterLevelField;
	MEField *pDisplayNameField;
	MEField *pEntityNameField;
	MEField *pRespawnTimeField;
	MEField *pDeathPowerField;
	// Door
	GEVariableDefGroup *pDoorDestGroup;
	MEField *pPerPlayerDoorField;
	MEField *pSinglePlayerDoorField;
	MEField *pCollectDestStatusField;
    MEField *pDestinationSameOwnerField;
	MEField *pAllowJoinTeamAtDoorField;
	MEField *pDoorTypeField;
	MEField *pDoorIDField;
	MEField *pDoorQueueField;
	MEField *pDoorTransitionField;
	MEField *pDoorKeyField;
	MEField* pIncludeTeammatesDoorField;
	// FromDefinition
	MEField *pInteractDefField;

	GEVariableDefGroup **eaVariableDefGroups;

	UILabel **eaVarLabels;

	const char *pcPreviousClass;
	const char **eaInteractDefNames;
} InteractionPropertiesGroup;

F32 UpdateInteractionPropertiesGroup(InteractionPropertiesGroup *pGroup, WorldInteractionPropertyEntry *pPropEntry, SA_PARAM_OP_VALID WorldInteractionPropertyEntry *pOrigPropEntry, const char *pcSrcMap, F32 y);
void FreeInteractionPropertiesGroup(InteractionPropertiesGroup *pGroup);
void FixupInteractionPropertiesMessages(WorldInteractionPropertyEntry *pEntry, const char *pcScope, const char *pcBaseMessageKey, const char *pcSubKey, int iIndex);


// ---- Interaction Def Editor ----

typedef struct IEUndoData 
{
	InteractionDef *pPreDef;
	InteractionDef *pPostDef;
} IEUndoData;

typedef struct InteractionEditDoc
{
	EMEditorDoc emDoc;

	InteractionDef *pOrigDef;
	InteractionDef *pDef;
	InteractionDef *pNextUndoDef;

	bool bIgnoreFieldChanges;
	bool bIgnoreFilenameChanges;

	// Standalone main window controls
	UIExpanderGroup *pExpanderGroup;
	UIExpander *pInfoExpander;
	UIExpander *pPropsExpander;
	UIWindow *pMainWindow;
	UILabel *pFilenameLabel;
	UIGimmeButton *pFileButton;

	// Info Expander
	UILabel *pTypeLabel;
	UILabel *pCommentLabel;
	MEField *pTypeField;
	MEField *pCommentField;

	// Props Expander
	InteractionPropertiesGroup *pPropsGroup;

	// Simple fields
	MEField **eaDocFields;
	
} InteractionEditDoc;

InteractionEditDoc *IEOpenInteractionDef(EMEditor *pEditor, char *pcName);
void IERevertInteractionDef(InteractionEditDoc *pDoc);
void IECloseInteractionDef(InteractionEditDoc *pDoc);
EMTaskStatus IESaveInteractionDef(InteractionEditDoc* pDoc, bool bSaveAsNew);
void IEInitData(EMEditor *pEditor);

#endif
