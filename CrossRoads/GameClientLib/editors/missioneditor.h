/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once
GCC_SYSTEM

#ifndef NO_EDITORS

#include "EditorManager.h"

typedef struct ActionBlockOverride ActionBlockOverride;
typedef struct CEImageMenuItemGroup CEImageMenuItemGroup;
typedef struct CEOfferGroup CEOfferGroup;
typedef struct CESpecialActionBlockGroup CESpecialActionBlockGroup;
typedef struct CESpecialDialogGroup CESpecialDialogGroup;
typedef struct ContactMissionOffer ContactMissionOffer;
typedef struct DialogFlowWindowInfo DialogFlowWindowInfo;
typedef struct EventEditor EventEditor;
typedef struct GEActionGroup GEActionGroup;
typedef struct GEMissionLevelDefGroup GEMissionLevelDefGroup;
typedef struct GEVariableDefGroup GEVariableDefGroup;
typedef struct GEVariableGroup GEVariableGroup;
typedef struct GameEvent GameEvent;
typedef struct ImageMenuItemOverride ImageMenuItemOverride;
typedef struct InteractableOverride InteractableOverride;
typedef struct InteractionPropertiesGroup InteractionPropertiesGroup;
typedef struct MDEMissionGroup MDEMissionGroup;
typedef struct MEField MEField;
typedef struct MissionDef MissionDef;
typedef struct MissionDefRequest MissionDefRequest;
typedef struct MissionDrop MissionDrop;
typedef struct MissionEditCond MissionEditCond;
typedef struct MissionEditDoc MissionEditDoc;
typedef struct MissionMap MissionMap;
typedef struct MissionNumericScale MissionNumericScale;
typedef struct MissionOfferOverride MissionOfferOverride;
typedef struct MissionWaypoint MissionWaypoint;
typedef struct OpenMissionScoreEvent OpenMissionScoreEvent;
typedef struct SpecialDialogOverride SpecialDialogOverride;
typedef struct UIButton UIButton;
typedef struct UIColorButton UIColorButton;
typedef struct UIComboBox UIComboBox;
typedef struct UIExpander UIExpander;
typedef struct UIExpanderGroup UIExpanderGroup;
typedef struct UIGameActionEditButton UIGameActionEditButton;
typedef struct UIGimmeButton UIGimmeButton;
typedef struct UILabel UILabel;
typedef struct UIMenu UIMenu;
typedef struct UIPairedBox UIPairedBox;
typedef struct UIScrollArea UIScrollArea;
typedef struct UISeparator UISeparator;
typedef struct UIWindow UIWindow;
typedef struct WorldInteractionPropertyEntry WorldInteractionPropertyEntry;
typedef struct WorldVariableDef WorldVariableDef;
typedef struct ZoneMapInfo ZoneMapInfo;

#define MISSION_EDITOR			"Mission Editor"
#define DEFAULT_MISSION_NAME    "New_Mission"

AUTO_ENUM;
typedef enum MissionCondTypeForEditor
{
	MissionCondTypeForEditor_All_Of,
	MissionCondTypeForEditor_One_Of,
	MissionCondTypeForEditor_Count_Of,
} MissionCondTypeForEditor;

typedef enum MDELayoutType
{
	LayoutType_Start   = (1 << 0),
	LayoutType_Success = (1 << 1),
	LayoutType_Failure = (1 << 2),
	LayoutType_Return  = (1 << 3)
} MDELayoutType;

typedef struct MDELayoutNode
{
	struct MDELayoutNode **eaChildren;
	MDELayoutType eType;
	MDEMissionGroup *pGroup;
} MDELayoutNode;

typedef struct MDEUndoData
{
	MissionDef *pPreMission;
	MissionDef *pPostMission;
} MDEUndoData;

typedef struct MDERewardData
{
	MissionEditDoc *pDoc;
	MissionDef *pMission;
	char cNamePart[20];
	const char **ppcRewardTableName;	// Pooled strings
} MDERewardData;

typedef struct MDECondGroup
{
	MDEMissionGroup *pMissionGroup;

	MissionEditCond ***peaConds;
	MissionEditCond ***peaOrigConds;
	int index;

	UIExpander *pExpander;
	UIButton *pRemoveButton;
	UILabel *pLabel;

	MEField *pExprField;
	MEField *pExprCountField;
	MEField *pMissionField;
} MDECondGroup;

typedef struct MDELevelGroup
{
	MDEMissionGroup *pMissionGroup;
	UIExpander *pExpander;

	GEMissionLevelDefGroup *pLevelDefGroup;
} MDELevelGroup;

typedef struct MDEWarpGroup
{
	MDEMissionGroup *pMissionGroup;
	UIExpander *pExpander;
} MDEWarpGroup;

typedef struct MDENumericScaleGroup
{
	MDEMissionGroup *pMissionGroup;

	MissionNumericScale ***peaNumericScales;
	MissionNumericScale ***peaOrigNumericScales;
	int index;

	UIExpander *pExpander;
	UISeparator *pSeparator;
	UIButton *pRemoveButton;

	UILabel *pNumericLabel;
	UILabel *pScaleLabel;

	MEField *pNumericField;
	MEField *pScaleField;
} MDENumericScaleGroup;

typedef struct MDEDropGroup
{
	MDEMissionGroup *pMissionGroup;

	MissionDrop ***peaDrops;
	MissionDrop ***peaOrigDrops;
	int index;

	UIExpander *pExpander;
	UISeparator *pSeparator;
	UIButton *pRemoveButton;
	UIButton *pRewardEditButton;
	UILabel *pTypeLabel;
	UILabel *pWhenLabel;
	UILabel *pNameLabel;
	UILabel *pSpawningPlayerLabel;
	UILabel *pRewardLabel;
	UILabel *pMapLabel;

	MDERewardData rewardData;

	MEField *pTypeField;
	MEField *pWhenField;
	MEField *pNameField;
	MEField *pSpawningPlayerField;
	MEField *pRewardField;
	MEField *pMapField;
} MDEDropGroup;

typedef struct MDEInteractableOverrideGroup
{
	MDEMissionGroup *pMissionGroup;
	InteractionPropertiesGroup* pInteractPropsGroup;	// Child group

	InteractableOverride ***peaOverrides;
	InteractableOverride ***peaOrigOverrides;
	int index;

	char **eaInteractableNames;

	UIExpander *pExpander;
	UISeparator *pSeparator;
	UIButton *pRemoveButton;
	UIButton *pPickButton;

	UICheckButton *pTreatAsMissionRewardButton;

	UILabel *pNameLabel;
	UILabel *pTagLabel;
	UILabel *pMapLabel;

	MEField *pNameField;
	MEField *pTagField;
	MEField *pMapField;
} MDEInteractableOverrideGroup;

typedef struct MDEContactGroup MDEContactGroup;

typedef struct MDESpecialDialogOverrideGroup
{
	MDEContactGroup *pContactGroup;
	CESpecialDialogGroup* pSpecialDialogGroup;	// Child group

	SpecialDialogOverride ***peaOverrides;
	SpecialDialogOverride ***peaOrigOverrides;
	int index;

	UIExpander *pExpander;

	UILabel *pNameLabel;
	MEField *pNameField;
} MDESpecialDialogOverrideGroup;

typedef struct MDESpecialActionBlockOverrideGroup
{
	MDEContactGroup *pContactGroup;
	CESpecialActionBlockGroup *pSpecialActionBlockGroup;

	ActionBlockOverride ***peaOverrides;
	ActionBlockOverride ***peaOrigOverrides;
	int index;

	UIExpander *pExpander;

	UILabel *pNameLabel;
	MEField *pNameField;
} MDESpecialActionBlockOverrideGroup;

typedef struct MDEMissionOfferOverrideGroup
{
	MDEContactGroup *pContactGroup;
	CEOfferGroup* pMissionOfferGroup;	// Child group

	MissionOfferOverride ***peaOverrides;
	MissionOfferOverride ***peaOrigOverrides;
	int index;

	UIExpander *pExpander;

	UILabel *pNameLabel;
	MEField *pNameField;
} MDEMissionOfferOverrideGroup;

typedef struct MDEImageMenuItemOverrideGroup {
	MDEContactGroup *pContactGroup;
	CEImageMenuItemGroup* pImageMenuItemGroup;
	
	ImageMenuItemOverride*** peaOverrides;
	ImageMenuItemOverride*** peaOrigOverrides;
	int index;

	UIExpander* pExpander;

	UILabel* pNameLabel;
	MEField* pNameField;
} MDEImageMenuItemOverrideGroup;

typedef struct MDEContactGroup
{
	// The name of the contact for this contact group
	const char *pchContactName;

	// The document
	MissionEditDoc *pDoc;

	// The window widget
	UIWindow *pWindow;

	// The expander group
	UIExpanderGroup *pExGroup;

	// Add Override buttons
	UIButton *pAddSpecialDialogOverrideButton;
	UIButton *pAddMissionOfferOverrideButton;
	UIButton *pAddActionBlockOverrideButton;
	UIButton *pAddImageMenuItemOverrideButton;

	// The context menu
	UIMenu *pContextMenu;

	// Override Groups
	MDESpecialDialogOverrideGroup **eaSpecialDialogOverrideGroups;
	MDESpecialActionBlockOverrideGroup **eaSpecialActionBlockOverrideGroups;
	MDEMissionOfferOverrideGroup **eaMissionOfferOverrideGroups;
	MDEImageMenuItemOverrideGroup **eaImageMenuItemOverrideGroups;

	// Temporary variables used for updating UI
	S32 iTmpSpecialDialogCount;
	S32 iTmpSpecialActionBlockCount;
	S32 iTmpMissionOfferCount;
	S32 iTmpImageMenuItemCount;

} MDEContactGroup;

typedef struct MDEScoreEventGroup
{
	MDEMissionGroup *pMissionGroup;
	OpenMissionScoreEvent ***peaEvents;
	OpenMissionScoreEvent ***peaOrigEvents;
	int index;

	UIExpander *pExpander;
	UISeparator *pSeparator;
	UIButton *pRemoveButton;

	UIButton *pEventButton;
	UILabel *pEventLabel;
	UILabel *pScaleLabel;

	MEField *pScaleField;

	EventEditor *pEventEditor;
} MDEScoreEventGroup;

typedef struct MDEEventGroup
{
	MDEMissionGroup *pMissionGroup;
	GameEvent ***peaEvents;
	GameEvent ***peaOrigEvents;
	int index;

	UIExpander *pExpander;
	UISeparator *pSeparator;
	UIButton *pRemoveButton;

	UIButton *pEventButton;
	UILabel *pEventLabel;
	UILabel *pEventNameLabel;

	EventEditor *pEventEditor;
} MDEEventGroup;

typedef struct MDEWaypointGroup
{
	MDEMissionGroup *pMissionGroup;
	MissionWaypoint ***peaWaypoints;
	MissionWaypoint ***peaOrigWaypoints;
	int index;

	UIExpander *pExpander;
	UISeparator *pSeparator;
	UIButton *pRemoveButton;

	UILabel *pTypeLabel;
	UILabel *pLocationLabel;
	UILabel *pMapLabel;
	UILabel *pAnyMapLabel;

	MEField *pTypeField;
	MEField *pLocationField;
	MEField *pMapField;
	MEField *pAnyMapField;

} MDEWaypointGroup;

typedef struct MDEMapGroup
{
	MDEMissionGroup *pMissionGroup;
	MissionMap* pMap;
	MissionMap* pOrigMap;
	int index;

	UIExpander *pExpander;
	UISeparator *pSeparator;

	WorldVariableDef **eaVarDefs;
	const char **eaVarNames;

	UIButton *pAddVarButton;
	UIButton *pRemoveMapButton;
	UILabel *pMapLabel;
	UILabel *pHideGotoLabel;

	MEField *pMapField;
	MEField *pHideGotoField;

	GEVariableGroup **eaVarGroups;
} MDEMapGroup;

typedef struct MDERequestGroup
{
	MDEMissionGroup *pMissionGroup;
	MissionDefRequest ***peaRequests;
	MissionDefRequest ***peaOrigRequests;
	int index;

	UIExpander *pExpander;
	UISeparator *pSeparator;
	UIButton *pRemoveButton;

	UILabel *pTypeLabel;
	UILabel *pMissionLabel;

	MEField *pTypeField;
	MEField *pMissionField;
	MEField *pMissionSetField;

} MDERequestGroup;

typedef struct MDEContactOverrideGroup
{
	MDEMissionGroup *pMissionGroup;

	const char *pchContactName;
	
	UIExpander *pExpander;

	UIButton *pFindContactOverrideWindowButton;

} MDEContactOverrideGroup;

typedef struct MDEMissionGroup
{
	MissionEditDoc *pDoc;

	MissionDef *pMission;
	MissionDef *pOrigMission;
	char *pcName;

	U32 eMissionType;

	MDEContactOverrideGroup **eaContactOverrideGroups;
	MDELevelGroup *pLevelGroup;
	MDEWaypointGroup **eaWaypointGroups;
	MDEMapGroup **eaMapGroups;
	MDECondGroup **eaCondSuccessGroups;
	MDECondGroup **eaCondFailureGroups;
	MDECondGroup **eaCondResetGroups;
	GEActionGroup **eaActionGroups;
	MDENumericScaleGroup **eaNumericScaleGroups;
	MDEDropGroup **eaDropGroups;
	MDEInteractableOverrideGroup **eaInteractableOverrideGroups;
	MDEEventGroup **eaEventGroups;
	MDEScoreEventGroup **eaScoreEventGroups;
	MDERequestGroup **eaRequestGroups;
	GEVariableDefGroup **eaVariableDefGroups;

	UIWindow *pWindow;
	UIExpanderGroup *pExGroup;
	UIExpander *pContactOverridesExpander;
	UIExpander *pDisplayExpander;
	UIExpander *pLevelExpander;
	UIExpander *pWaypointExpander;
	UIExpander *pWarpExpander;
	UIExpander *pMapsExpander;
	UIExpander *pEventExpander;
	UIExpander *pCondExpander;
	UIExpander *pActionExpander;
	UIExpander *pRewardsExpander;
	UIExpander *pOpenMissionRewardsExpander;
	UIExpander *pNumericScalesExpander;
	UIExpander *pDropExpander;
	UIExpander *pInteractableOverrideExpander;
	UIExpander *pSpecialDialogOverrideExpander;
	UIExpander *pMissionOfferOverrideExpander;
	UIExpander *pScoreboardExpander;
	UIExpander *pRequestsExpander;
	UIExpander *pVariablesExpander;
	UIButton *pCondSuccessAddExprButton;
	UIButton *pCondSuccessAddObjButton;
	UIButton *pCondFailureAddExprButton;
	UIButton *pCondFailureAddObjButton;
	UIButton *pCondResetAddExprButton;
	UIButton *pCondResetAddObjButton;
	UIButton *pAddWaypointButton;
	UIButton *pAddMapButton;
	UIButton *pAddNumericScaleButton;
	UIButton *pAddDropButton;
	UIButton *pAddInteractableOverrideButton;
	UIButton *pAddEventButton;
	UIButton *pAddScoreEventButton;
	UIButton *pAddRequestButton;
	UILabel *pCondSuccessMainLabel;
	UILabel *pCondSuccessWhenLabel;
	UILabel *pCondSuccessWhenCountLabel;
	UILabel *pCondFailureMainLabel;
	UILabel *pCondFailureWhenLabel;
	UILabel *pCondFailureWhenCountLabel;
	UILabel *pCondFailureWhen2Label;
	UILabel *pCondDiscoverWhenLabel;
	UILabel *pCondResetMainLabel;
	UILabel *pCondResetWhenLabel;
	UILabel *pCondResetWhenCountLabel;
	UIMenu *pContextMenu;
	F32 fDisplayExpanderBaseSize;
	F32 fDisplayExpanderFullSize;

	int iNumInBoxes;
	int iNumOutBoxes;
	UIPairedBox **eaInBoxes;
	UIPairedBox **eaOutBoxes;

	MDERewardData rewardStartData;
	MDERewardData rewardSuccessData;
	MDERewardData rewardActivitySuccessData;
	MDERewardData rewardFailureData;
	MDERewardData rewardReturnData;
	MDERewardData rewardReplayReturnData;
	MDERewardData rewardActivityReturnData;

	MDERewardData rewardOpenMissionGoldData;
	MDERewardData rewardOpenMissionSilverData;
	MDERewardData rewardOpenMissionBronzeData;
	MDERewardData rewardOpenMissionDefaultData;

	MDERewardData rewardOpenMissionFailureGoldData;
	MDERewardData rewardOpenMissionFailureSilverData;
	MDERewardData rewardOpenMissionFailureBronzeData;
	MDERewardData rewardOpenMissionFailureDefaultData;

	MEField **eaDocFields;
	MEField **eaParamFields;
	MEField *pParamActivityName;
	MEField *pInfoReqAnyActivities;
	MEField *pInfoReqAllActivities;
	MEField *pRelatedEvent;
	char **ppchActivityNames;

	MEField *pDisplayNameField;
	MEField *pUIStringField;
	MEField *pSummaryField;
	MEField *pDetailField;
	MEField *pFailureTextField;
	MEField *pReturnField;
	MEField *pFailedReturnField;
	MEField *pSplatTextField;
	MEField *pTeamUpTextField;

	MEField *pCondSuccessWhenField;
	MEField *pCondSuccessWhenCountField;
	MEField *pCondFailureWhenField;
	MEField *pCondFailureWhenCountField;
	MEField *pCondDiscoverWhenField;
	MEField *pCondResetWhenCountField;
	MEField *pCondResetWhenField;

	MEField *pWarpMapField;
	MEField *pWarpSpawnField;
	MEField *pWarpCostTypeField;
	MEField *pWarpLevelField;
	MEField *pWarpTransitionField;

	bool bOldIsTutorialPerk;
} MDEMissionGroup;

typedef struct MissionEditDoc
{
	EMEditorDoc emDoc;
	void *unusedPointerNeededToAvoidCrashesWithOldKeybindFiles;

	MissionDef *pMission;
	MissionDef *pOrigMission;
	MissionDef *pNextUndoMission;

	MissionDef **eaSubMissions;

	bool bIgnoreFieldChanges;
	bool bIgnoreFilenameChanges;
	bool bSkipNameCapture;
	bool bNeedsUpdate;

	UIWindow *pMainWindow;
	UIScrollArea *pScrollArea;
	UILabel *pFilenameLabel;
	UIGimmeButton *pFileButton;

	MDEMissionGroup *pMainMissionGroup;
	MDEMissionGroup **eaSubMissionGroups;

	// Contact groups
	MDEContactGroup **eaContactGroups;

	char **eaVarNames;

	// The contact flow info
	DialogFlowWindowInfo *pDialogFlowWindowInfo;

} MissionEditDoc;

extern EMEditor s_MissionEditor;

void MDEOncePerFrame(EMEditorDoc *pDoc);
MissionEditDoc *MDEOpenMission(EMEditor *pEditor, char *pcName);
void MDERevertMission(MissionEditDoc *pDoc);
void MDECloseMission(MissionEditDoc *pDoc);
EMTaskStatus MDESaveMission(MissionEditDoc* pDoc, bool bSaveAsNew);
void MDEInitData(EMEditor *pEditor);

MDEContactGroup * MDEFindContactGroup(MissionEditDoc *pDoc, const char *pchContactName);
void MDEUpdateContactDisplay(MissionEditDoc *pDoc);
void MDEUpdateDisplay(MissionEditDoc *pDoc);
bool MDEMissionFixupDialogNameForSaving(MissionDef* pMission, const char* pchContactName, char **pestrDialogName);
const MDESpecialDialogOverrideGroup * const MDEFindSpecialDialogOverrideGroupByName(SA_PARAM_NN_VALID MDEContactGroup *pContactGroup, SA_PARAM_OP_STR const char *pchSpecialDialogName);
const MDEMissionOfferOverrideGroup * const MDEFindMissionOfferOverrideGroupByOffer(SA_PARAM_NN_VALID MDEContactGroup *pContactGroup, SA_PARAM_NN_VALID ContactMissionOffer *pMissionOffer);
#endif 
