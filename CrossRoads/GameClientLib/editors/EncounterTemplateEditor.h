#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef NO_EDITORS

#include "EditorManager.h"


typedef struct EncounterTemplate EncounterTemplate;
typedef struct EncounterTemplateEditDoc EncounterTemplateEditDoc;
typedef struct ETActorGroup ETActorGroup;
typedef struct GEVariableDefGroup GEVariableDefGroup;
typedef struct InteractionPropertiesGroup InteractionPropertiesGroup;
typedef struct MEField MEField;
typedef struct UIButton UIButton;
typedef struct UIComboBox UIComboBox;
typedef struct UIExpander UIExpander;
typedef struct UIExpanderGroup UIExpanderGroup;
typedef struct UIGimmeButton UIGimmeButton;
typedef struct UILabel UILabel;
typedef struct UITextEntry UITextEntry;
typedef struct UIWindow UIWindow;
typedef struct WorldVariableDef WorldVariableDef;


#define ENCOUNTER_TEMPLATE_EDITOR "Encounter Template Editor"
#define DEFAULT_TEMPLATE_NAME     "New_Encounter"

AUTO_ENUM;
typedef enum ETViewActorsType
{
	ETViewActorsType_TeamSize,
	ETViewActorsType_Difficulty,
} ETViewActorsType;
extern StaticDefineInt ETViewActorsTypeEnum[];

typedef struct ETUndoData 
{
	EncounterTemplate *pPreTemplate;
	EncounterTemplate *pPostTemplate;
} ETUndoData;

typedef struct ETNewTemplateData
{
	const char* pchOldTemplate;
	const char* pchName;
	const char* pchScope;
	const char* pchMapForVars;
	EMEditorDoc* pDoc;
	bool bCreateChild;
} ETNewTemplateData;

typedef struct ETOverrideVarGroup
{
	UIExpander *pExpander;
	EncounterTemplateEditDoc *pDoc;
	// The actor index. If -1, then these are template vars
	int iActorIndex;
	// Index of the variable whithin the var list
	int index;
	// The base variable (not set)
	WorldVariableDef *pVarDef;

	UICheckButton *pOverrideButton;
	UILabel *pInitFromLabel;
	UILabel *pInitFromValueLabel;
	UILabel *pInheritedLabel;
	UILabel *pVarLabel;
	UILabel *pVarValueLabel;
	UILabel *pVarLabel2;
	UILabel *pVarValueLabel2;

	MEField *pInitFromField;

	MEField *pIntField;
	MEField *pFloatField;
	MEField *pStringField;
	MEField *pLocStringField;
	MEField *pAnimField;
	MEField *pCritterDefField;
	MEField *pCritterGroupField;
	MEField *pMessageField;
	MEField *pZoneMapField;
	MEField *pSpawnPointField;
	MEField *pItemDefField;

	MEField *pChoiceTableField;
	MEField *pChoiceNameField;

	MEField *pMapVarField;

	char** eaSrcMapVariables;
	char** eaChoiceTableNames;


} ETOverrideVarGroup;

typedef struct ETFSMVarGroup
{
	UIExpander *pExpander;
	EncounterTemplateEditDoc *pDoc;
	int iActorIndex;

	UILabel *pFSMLabel;
	UILabel *pTemplateLabel;
	UILabel *pCritterLabel;
	UILabel *pExtraLabel;

	UIButton *pAddVarButton;


	ETOverrideVarGroup **eaFSMOverrideVarGroups;
	ETOverrideVarGroup **eaTemplateOverrideVarGroups;
	ETOverrideVarGroup **eaCritterOverrideVarGroups;

	GEVariableDefGroup **eaExtraVarDefGroups;

	WorldVariableDef **eaCachedFSMVars;
	WorldVariableDef **eaCachedOrigFSMVars;
	WorldVariableDef **eaCachedCritterVars;
	WorldVariableDef **eaCachedOrigCritterVars;

} ETFSMVarGroup;

typedef struct ETStrengthCell
{
	EncounterTemplateEditDoc *pDoc;
	
	UIButton *pTeamSizeButton;
	UILabel *pTeamSizeLabel;

	int iTeamSize;
	EncounterDifficulty eDifficulty;

} ETStrengthCell;

typedef struct ETStrengthRow
{
	UIExpander *pExpander;

	UILabel *pDifficultyLabel;

	ETStrengthCell **eaCells;

} ETStrengthRow;

typedef struct ETStrengthGroup
{
	UIExpander *pExpander;

	ETStrengthRow **eaRows;

	UILabel *pTeamSizeHeader;
	UILabel *pStrengthHeader;
	UILabel *pNumActorsHeader;
	UILabel *pNumActors1;
	UILabel *pNumActors2;
	UILabel *pNumActors3;
	UILabel *pNumActors4;
	UILabel *pNumActors5;

} ETStrengthGroup;

typedef struct ETLevelGroup
{
	UIExpander *pExpander;

	UILabel *pLevelTypeLabel;
	UILabel *pLevelMinLabel;
	UILabel *pLevelMaxLabel;
	UILabel *pLevelOffsetMinLabel;
	UILabel *pLevelOffsetMaxLabel;
	UILabel *pMapVarLabel;
	UILabel *pClampTypeLabel;
	UILabel *pMinLevelLabel;
	UILabel *pMaxLevelLabel;
	UILabel *pClampOffsetMinLabel;
	UILabel *pClampOffsetMaxLabel;
	UILabel *pClampMapVarLabel;

	MEField *pLevelTypeField;
	MEField *pLevelMinField;
	MEField *pLevelMaxField;
	MEField *pLevelOffsetMinField;
	MEField *pLevelOffsetMaxField;
	MEField *pMapVarField;
	MEField *pClampTypeField;
	MEField *pMinLevelField;
	MEField *pMaxLevelField;
	MEField *pClampOffsetMinField;
	MEField *pClampOffsetMaxField;
	MEField *pClampMapVarField;
	UICheckButton* pOverrideParentCheckbox;
} ETLevelGroup;

typedef struct ETDifficultyGroup
{
	UIExpander *pExpander;

	UILabel *pDifficultyTypeLabel;
	UILabel *pDifficultyLabel;
	UILabel *pMapVarLabel;

	MEField *pDifficultyTypeField;
	MEField *pDifficultyField;
	MEField *pMapVarField;

} ETDifficultyGroup;

typedef struct ETSpawnGroup
{
	UIExpander *pExpander;

	UILabel *pSpawnAnimTypeLabel;
	UILabel *pSpawnAnimLabel;
	UILabel *pSpawnAnimTimeLabel;
	UILabel *pIsAmbushLabel;

	MEField *pSpawnAnimTypeField;
	MEField *pSpawnAnimField;
	MEField *pSpawnAnimTimeField;
	MEField *pIsAmbushField;
	UICheckButton* pOverrideParentCheckbox;
} ETSpawnGroup;

typedef struct ETActorSharedGroup
{
	UIExpander *pExpander;

	UILabel *pCritterGroupTypeLabel;
	UILabel *pCritterGroupLabel;
	UILabel *pCritterGroupMapVarLabel;
	UILabel *pFactionTypeLabel;
	UILabel *pFactionLabel;
	UILabel *pGangLabel;
	UILabel *pOverrideSendDistanceLabel;

	MEField *pCritterGroupTypeField;
	MEField *pCritterGroupField;
	MEField *pCritterGroupMapVarField;
	MEField *pFactionTypeField;
	MEField *pFactionField;
	MEField *pGangField;
	MEField *pOverrideSendDistanceField;
	UICheckButton* pOverrideParentCheckbox;
} ETActorSharedGroup;

typedef struct ETAIGroup
{
	EncounterTemplateEditDoc *pDoc;
	UIExpander *pExpander;

	UILabel *pCombatRoleLabel;
	UILabel *pFSMTypeLabel;
 	UILabel *pFSMLabel;
 
	MEField *pCombatRoleField;
 	MEField *pFSMTypeField;
 	MEField *pFSMField;

	ETFSMVarGroup *pFSMVarGroup;
	UICheckButton* pOverrideParentCheckbox;
} ETAIGroup;

typedef struct ETJobGroup ETJobGroup;

typedef struct ETJobSubGroup
{
	ETJobGroup *pGroup;
	int iIndex;

	UIButton *pRemoveButton;

	UILabel *pJobNameLabel;
	UILabel *pJobFSMLabel;
	UILabel *pJobRequiresLabel;
	UILabel *pJobRatingLabel;
	UILabel *pJobPriorityLabel;

	MEField *pJobNameField;
	MEField *pJobFSMField;
	MEField *pJobRequiresField;
	MEField *pJobRatingField;
	MEField *pJobPriorityField;
} ETJobSubGroup;

typedef struct ETJobGroup
{
	UIExpander *pExpander;
	EncounterTemplateEditDoc *pDoc;

	UIButton *pAddButton;

	ETJobSubGroup **eaSubGroups;
	UICheckButton* pOverrideParentCheckbox;
} ETJobGroup;

typedef struct ETWaveGroup
{
	UIExpander *pExpander;

	UILabel *pWaveCondLabel;
	UILabel *pWaveIntervalTypeLabel;
	UILabel *pWaveIntervalLabel;
	UILabel *pWaveIntervalTimeLabel;
	UILabel *pWaveDelayTypeLabel;
	UILabel *pWaveDelayTimeLabel;
	UILabel *pWaveDelayMinLabel;
	UILabel *pWaveDelayMaxLabel;

	MEField *pWaveCondField;
	MEField *pWaveIntervalTypeField;
	MEField *pWaveIntervalField;
	MEField *pWaveDelayTypeField;
	MEField *pWaveDelayMinField;
	MEField *pWaveDelayMaxField;
	UICheckButton* pOverrideParentCheckbox;
} ETWaveGroup;

typedef struct ETRewardsGroup
{
	UIExpander *pExpander;

	UILabel *pRewardTypeLabel;
	UILabel *pRewardTableLabel;
	UILabel *pRewardLevelTypeLabel;
	UILabel *pRewardLevelLabel;

	MEField *pRewardTypeField;
	MEField *pRewardTableField;
	MEField *pRewardLevelTypeField;
	MEField *pRewardLevelField;

	UICheckButton* pOverrideParentCheckbox;
} ETRewardsGroup;

typedef struct ETInteractGroup
{
	ETActorGroup *pActorGroup;
	InteractionPropertiesGroup* pInteractPropsGroup;	// Child group

	int iIndex;

	UILabel *pNameLabel;
	UIButton *pRemoveButton;

} ETInteractGroup;

typedef struct ETActorSpawnGroup
{
	UIExpander *pExpander;
	EncounterTemplateEditDoc *pDoc;
	
	UILabel *pDifficultyLabel;

	MEField *pTeamSizeField;
} ETActorSpawnGroup;

typedef struct ETViewActorsButton
{
	EncounterTemplateEditDoc *pDoc;
	int iIndex;
	ETViewActorsType eType;

	UIButton *pButton;

} ETViewActorsButton;

typedef struct ETActorGroup
{
	UIExpander *pExpander;
	EncounterTemplateEditDoc *pDoc;
	int iIndex;

	WorldVariableDef **eaVarDefs;

	UIButton *pAddVarButton;
	UIButton *pAddInteractButton;

	UIButton *pCloneButton;
	UIButton *pRemoveButton;
	UIButton *pUpButton;
	UIButton *pDownButton;

	UILabel *pNameLabel;
	UILabel *pDispNameTypeLabel;
	UILabel *pDispNameLabel;
	UILabel *pDispSubNameTypeLabel;
	UILabel *pDispSubNameLabel;
	UILabel *pCritterTypeLabel;
	UILabel *pCritterLabel1;
	UILabel *pCritterRankLabel;
	UILabel *pCritterSubRankLabel;
	UILabel *pUseTeamSizeLabel;
	UILabel *pSpawnAtDifficultyLabel;
	UILabel *pSpawnAtTeamSizeLabel;
	UILabel *pBossTeamSizeLabel;
	UILabel *pBossAtDifficultyLabel;
	UILabel *pBossAtTeamSizeLabel;
	UILabel *pFactionTypeLabel;
	UILabel *pFactionLabel;
	UILabel *pGangLabel;
	UILabel *pSpawnAnimTypeLabel;
	UILabel *pSpawnAnimLabel;
	UILabel *pSpawnAnimTimeLabel;
	UILabel *pNonCombatLabel;
	UILabel *pFSMTypeLabel;
	UILabel *pFSMLabel;
	UILabel *pInteractRootLabel;
	UILabel *pInteractRangeLabel;
	UILabel *pLevelOffsetLabel;
	UILabel *pPetContactListLabel;
	UILabel *pCombatRoleLabel;
	UILabel *pCommentsLabel;
	UILabel *pNemesisTeamIndexLabel;
	UILabel *pNemesisLeaderLabel;

	MEField *pNameField;
	MEField *pDispNameTypeField;
	MEField *pDispNameField;
	MEField *pSubNameField;
	MEField *pDispSubNameTypeField;
	MEField *pDispSubNameField;
	MEField *pCritterTypeField;
	MEField *pCritterDefField;
	MEField *pCritterGroupField;
	MEField *pCritterRankField;
	MEField *pCritterSubRankField;
	MEField *pCritterMapVarField;
	MEField *pFactionTypeField;
	MEField *pFactionField;
	MEField *pGangField;
	MEField *pSpawnAnimTypeField;
	MEField *pSpawnAnimField;
	MEField *pSpawnAnimTimeField;
	MEField *pNonCombatField;
	MEField *pFSMTypeField;
	MEField *pFSMField;
	MEField *pInteractRangeField;
	MEField *pLevelOffsetField;
	MEField *pPetContactListField;
	MEField *pCombatRoleField;
	MEField *pCommentsField;
	MEField *pNemesisTeamIndexField;
	MEField* pNemesisLeaderField;

	UICheckButton* pOverrideDisplayNameCheckbox;
	UICheckButton* pOverrideDisplaySubNameCheckbox;
	UICheckButton* pOverrideCritterTypeCheckbox;
	UICheckButton* pOverrideSpawnConditionsCheckbox;
	UICheckButton* pOverrideSpawnAnimCheckbox;
	UICheckButton* pOverrideFactionCheckbox;
	UICheckButton* pOverrideMiscCheckbox;
	UICheckButton* pOverrideActorFSMCheckbox;

	ETFSMVarGroup *pFSMVarGroup;

	ETInteractGroup **eaInteractGroups;
	ETActorSpawnGroup **eaSpawnGroups;
	ETActorSpawnGroup **eaBossSpawnGroups;

} ETActorGroup;

typedef struct EncounterTemplateEditDoc
{
	EMEditorDoc emDoc;

	EncounterTemplate *pTemplate;
	EncounterTemplate *pOrigTemplate;
	EncounterTemplate *pNextUndoTemplate;

	bool bIgnoreFieldChanges;
	bool bIgnoreFilenameChanges;

	// Standalone main window controls
	UIWindow *pMainWindow;
	UILabel *pFilenameLabel;
	UIGimmeButton *pFileButton;

	// Main Scroll Area (used in single column view)
	UIScrollArea *pScrollArea;

	// Expanders
	UIExpanderGroup *pEncounterExpanderGroup;
	UIExpanderGroup *pActorExpanderGroup;

	// Expander Labels
	UILabel *pEncounterExpanderLabel;
	UILabel *pActorExpanderLabel;

	// One-Off Display
	UILabel *pOneOffLabel;

	// Team size buttons
	UIButton *pButtonAll;
	ETViewActorsButton** eaViewActorsButtons;
	UIButton *pButtonAdd;
	UIComboBox *pViewActorsType;

	// Name
	MEField *pNameField;

	// Simple fields
	MEField **eaDocFields;

	// Groups
	ETLevelGroup *pLevelGroup;
	ETSpawnGroup *pSpawnGroup;
	ETActorSharedGroup *pActorSharedGroup;
	ETAIGroup *pAIGroup;
	ETJobGroup *pJobGroup;
	ETWaveGroup *pWaveGroup;
	ETRewardsGroup *pRewardsGroup;
	ETActorGroup **eaActorGroups;
	ETStrengthGroup *pStrengthGroup;
	ETDifficultyGroup *pDifficultyGroup;
	WorldVariableDef** eaVarDefs;

	char **eaVarNames;

	const char* pchMapForVars;
} EncounterTemplateEditDoc;

EncounterTemplateEditDoc *ETOpenTemplate(EMEditor *pEditor, char *pcName);
void ETRevertTemplate(EncounterTemplateEditDoc *pDoc);
void ETCloseTemplate(EncounterTemplateEditDoc *pDoc);
EMTaskStatus ETSaveTemplate(EncounterTemplateEditDoc* pDoc, bool bSaveAsNew);
void ETInitData(EMEditor *pEditor);

#endif
