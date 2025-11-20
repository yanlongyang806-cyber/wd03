/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once
GCC_SYSTEM

#ifndef NO_EDITORS

#include "EditorManager.h"
#include "MultiEditField.h"

typedef struct CESpecialDialogGroup CESpecialDialogGroup;
typedef struct CESpecialActionBlockGroup CESpecialActionBlockGroup;
typedef struct ContactDialogUINodeAction ContactDialogUINodeAction;
typedef struct ContactDef ContactDef;
typedef struct ContactEditDoc ContactEditDoc;
typedef struct ContactMissionOffer ContactMissionOffer;
typedef struct DialogBlock DialogBlock;
typedef struct SpecialDialogAction SpecialDialogAction;
typedef struct SpecialDialogBlock SpecialDialogBlock;
typedef struct SpecialActionBlock SpecialActionBlock;
typedef struct StoreRef StoreRef;
typedef struct MEField MEField;
typedef struct ParseTable ParseTable;
typedef struct UIExpander UIExpander;
typedef struct UIExpanderGroup UIExpanderGroup;
typedef struct UIGameActionEditButton UIGameActionEditButton;
typedef struct UIGimmeButton UIGimmeButton;
typedef struct UILabel UILabel;
typedef struct UIWindow UIWindow;
typedef struct MissionDef MissionDef;
typedef struct CECommonCallbackParams CECommonCallbackParams;

#define CONTACT_EDITOR "Contact Editor"
#define DEFAULT_CONTACT_NAME     "New_Contact"

typedef bool (*CECallbackFunc)(UserData);
typedef SpecialDialogBlock* (*CESpecialDialogAccessorFunc)(UserData);
typedef ContactMissionOffer* (*CEMissionOfferAccessorFunc)(UserData);
typedef SpecialActionBlock* (*CESpecialActionBlockAccessorFunc)(UserData);
typedef ContactImageMenuItem* (*CEImageMenuItemAccessorFunc)(UserData);

typedef struct CEUndoData 
{
	ContactDef *pPreContact;
	ContactDef *pPostContact;
} CEUndoData;

typedef struct CEDialogGroup
{
	CECallbackFunc pDocIsEditableFunc;
	void* pDocIsEditableData;
	CECallbackFunc pDialogChangedFunc;
	void* pDialogChangedData;
	MEFieldChangeCallback pFieldChangeFunc;
	MEFieldPreChangeCallback pFieldPreChangeFunc;
	void* pFieldChangeData;


	int index;
	DialogBlock ***peaBlocks;

	UILabel *pExpressionLabel;
	UILabel *pMessageLabel;
	UILabel *pContinueMessageLabel;
	UILabel *pContinueMessageDialogFormatterLabel;
	UILabel *pSoundLabel;
	UILabel *pPhraseLabel;
	UILabel *pAnimLabel;
	UILabel *pDialogFormatterLabel;
	UIButton *pRemoveButton;

	MEField *pExpressionField;
	MEField *pMessageField;
	MEField *pContinueMessageField;
	MEField *pContinueMessageDialogFormatterField;
	MEField *pAudioField;
	MEField *pAnimField;
	MEField *pDialogFormatterField;
	MEField *pAudioPhraseField;
} CEDialogGroup;

typedef struct CESpecialActionGroup
{
	CECommonCallbackParams *pCommonCallbackParams;

	int index;
	SpecialDialogAction ***peaActions;

	UILabel *pTitleLabel;
	UILabel *pDialogLabel;
	UILabel *pExpressionLabel;
	UILabel *pCanChooseExpressionLabel;
	UILabel *pCompleteLabel;
	UILabel *pEndDialogLabel;
	UILabel *pMessageLabel;
	UILabel *pDialogFormatterLabel;
	UILabel *pActionLabel;
	UIButton *pRemoveButton;
	UIButton *pUpButton;
	UIButton *pDownButton;
	UIGameActionEditButton *pActionButton;

	MEField *pDialogField;
	MEField *pExpressionField;
	MEField *pCanChooseExpressionField;
	MEField *pCompleteField;
	MEField *pEndDialogField;
	MEField *pMessageField;
	MEField *pDialogFormatterField;
} CESpecialActionGroup;

typedef struct CEDialogOverrideGroup
{
	UILabel *pMessageLabel;
	UITextEntry *pMessageTextEntry;

} CEDialogOverrideGroup;

typedef struct CESpecialOverrideActionGroup
{
	UILabel *pMessageLabel;
	UITextEntry *pMessageTextEntry;

	UILabel *pNextDialogLabel;
	UITextEntry *pNextDialogTextEntry;
} CESpecialOverrideActionGroup;

typedef struct CESpecialDialogOverrideGroup
{
	const char* pchContactName;		// Contact who this special dialog belongs to.

	UIButton *pOpenMissionButton;

	UIPane *pPane;
	UISeparator *pSeparator;

	UILabel *pActionsLabel;

	UILabel *pInternalNameLabel;
	UITextEntry *pInternalNameTextEntry;

	UILabel *pDisplayNameLabel;
	UITextEntry *pDisplayNameTextEntry;

	CEDialogOverrideGroup **eaDialogOverrideGroups;
	CESpecialOverrideActionGroup **eaOverrideActionGroups;
	
} CESpecialDialogOverrideGroup;

typedef struct CESpecialActionBlockGroup
{
	CECommonCallbackParams *pCommonCallbackParams;

	int index;
	SpecialActionBlock ***peaSpecialActionBlocks;
	void ***peaSpecialActionBlockWrappers;

	UIButton *pRemoveButton;
	UILabel *pNameLabel;
	UIPane *pPane;
	UIButton *pAddActionButton;
	MEField *pNameField;

	CESpecialActionGroup **eaSpecialActionGroups;
} CESpecialActionBlockGroup;

typedef struct CECommonCallbackParams
{
	CECallbackFunc pDocIsEditableFunc;
	void* pDocIsEditableData;
	CECallbackFunc pDialogChangedFunc;
	void* pDialogChangedData;
	CECallbackFunc pMessageFixupFunc;
	void* pMessageFixupData;
	CESpecialDialogAccessorFunc pSpecialDialogFromWrapperFunc;
	CESpecialActionBlockAccessorFunc pSpecialActionBlockFromWrapperFunc;
	MEFieldChangeCallback pFieldChangeFunc;
	MEFieldPreChangeCallback pFieldPreChangeFunc;
	void* pFieldChangeData;
} CECommonCallbackParams;

typedef struct CESpecialDialogGroup
{
	CECommonCallbackParams *pCommonCallbackParams;

	const char* pchContactName;		// Contact who this special dialog belongs to.
									// Used to check for duplicate dialogs when cloning

	char ***peaVarNames;			// Map var names

	int index;
	void ***peaSpecialDialogWrappers;	// Array of structs to manipulate when performing a remove/move action
	ParseTable* pWrapperParseTable;	// Parse table of wrapper structs

	UIPane *pPane;
	UILabel *pExpressionLabel;
	UILabel *pNameLabel;
	UILabel *pInternalNameLabel;
	UILabel *pSortOrderLabel;
	UILabel *pDisplayNameLabel;
	UILabel *pDialogFormatterLabel;
	UILabel *pIndicatorLabel;
	UILabel *pCostumeLabel;
	UILabel *pCostumeTypeLabel;
	UILabel *pCritterGroupTypeLabel;
	UILabel *pCritterGroupIdentifierLabel;
	UILabel *pHeadshotLabel;
	UILabel *pCutSceneLabel;
	UILabel *pSourceTypeLabel;
	UILabel *pSourceNameLabel;
	UILabel *pSourceSecondaryNameLabel;
	UILabel *pFlagsLabel;
	UILabel *pDelayIfInCombatLabel;
	UILabel *pSpecialActionBlockLabel;
	UILabel *pSoundLabel;
	UILabel *pAnimLabel;
	UITextEntry *pInternalNameTextEntry;
	UIButton *pAddDialogButton;
	UIButton *pCloneGroupButton;
	UIButton *pRemoveButton;
	UIButton *pUpButton;
	UIButton *pDownButton;
	UISeparator *pSeparator;
	UIButton *pAddActionButton;

	// The button that opens up the dialog flow window
	UIButton *pShowDialogFlowWindowButton;

	MEField *pExpressionField;
	MEField *pDisplayNameField;
	MEField *pDialogFormatterField;
	MEField *pAudioField;
	MEField *pAnimField;
	MEField *pNameField;
	MEField *pSortOrderField;
	MEField *pIndicatorField;
	MEField *pCostumeField;
	MEField *pCostumeTypeField;
	MEField *pCritterGroupTypeField;
	MEField *pCritterGroupField;
	MEField *pCritterMapVarField;
	MEField *pCritterGroupIdentifierField;
	MEField *pPetContactField;
	MEField* pHeadshotStyleField;
	MEField* pCutSceneField;
	MEField* pSourceTypeField;
	MEField* pSourceNameField;
	MEField* pSourceSecondaryNameField;
	MEField* pFlagsField;
	MEField* pDelayIfInCombatField;
	MEField *pAppendField;

	CEDialogGroup **eaDialogGroups;
	CESpecialActionGroup **eaActionGroups;

} CESpecialDialogGroup;

typedef struct CEOfferGroup
{
	CECallbackFunc pDocIsEditableFunc;
	void* pDocIsEditableData;
	CECallbackFunc pDialogChangedFunc;
	void* pDialogChangedData;
	CECallbackFunc pMessageFixupFunc;
	void* pMessageFixupData;
	CEMissionOfferAccessorFunc pMissionOfferFromWrapperFunc;
	MEFieldChangeCallback pFieldChangeFunc;
	MEFieldPreChangeCallback pFieldPreChangeFunc;
	void* pFieldChangeData;

	const char* pchContactName;		// Contact who this special dialog belongs to.
	// Used to check for duplicate dialogs when cloning

	char ***peaVarNames;			// Map var names

	int index;
	void ***peaMissionOfferWrappers;	// Array of structs to manipulate when performing a remove/move action
	ParseTable* pWrapperParseTable;	// Parse table of wrapper structs

	UILabel *pMissionLabel;
	UILabel *pAllegianceLabel;
	UILabel *pSpecialDialogLabel;
	UILabel *pSpecialDialogInternalNameLabel;
	UILabel *pRemoteLabel;
	UILabel *pSubMissionLabel;
	UILabel *pUITypeLabel;
	UILabel *pChoiceLabel;
	UILabel *pAcceptLabel;
	UILabel *pAcceptDialogFormatterLabel;
	UILabel *pAcceptTargetDialogLabel;
	UILabel *pDeclineLabel;
	UILabel *pDeclineDialogFormatterLabel;
	UILabel *pDeclineTargetDialogLabel;
	UILabel *pTurnInLabel;
	UILabel *pSoundLabel;
	UILabel *pAnimLabel;
	UILabel *pRewardAcceptLabel;
	UILabel *pRewardAcceptTargetDialogLabel;
	UILabel *pRewardAcceptDialogFormatterLabel;
	UILabel *pRewardChooseLabel;
	UILabel *pRewardChooseTargetDialogLabel;
	UILabel *pRewardChooseDialogFormatterLabel;
	UILabel *pRewardAbortLabel;
	UILabel *pRewardAbortTargetDialogLabel;
	UILabel *pRewardAbortDialogFormatterLabel;

	UIButton *pRemoveButton;
	UIButton *pUpButton;
	UIButton *pDownButton;
	UISeparator *pSeparator;
	UITextEntry *pSpecialDialogInternalNameTextEntry;

	MEField *pMissionField;
	MEField *pAllegianceField;
	MEField *pSpecialDialogField;
	MEField *pSubMissionField;
	MEField *pUITypeField;
	MEField *pChoiceField;
	MEField *pAcceptField;
	MEField *pAcceptDialogFormatterField;
	MEField *pAcceptTargetDialogField;
	MEField *pRewardAcceptField;
	MEField *pRewardAcceptTargetDialogField;
	MEField *pRewardAcceptDialogFormatterField;
	MEField *pRewardChooseField;
	MEField *pRewardChooseTargetDialogField;
	MEField *pRewardChooseDialogFormatterField;
	MEField *pRewardAbortField;
	MEField *pRewardAbortTargetDialogField;
	MEField *pRewardAbortDialogFormatterField;
	MEField *pDeclineField;
	MEField *pDeclineDialogFormatterField;
	MEField *pDeclineTargetDialogField;
	MEField *pTurnInField;
	MEField *pRemoteFlagsField;
	
	CEDialogGroup **eaDialogGroups;

	char **eaSubMissionNames;  // for pSubMissionField
} CEOfferGroup;

typedef struct CELoreDialogGroup
{
	ContactEditDoc *pDoc;

	int index;

	UIButton *pButton;
	UILabel *pLoreItemLabel;
	UILabel *pOptionTextLabel;
	UILabel *pConditionLabel;

	MEField *pLoreItemField;
	MEField *pOptionTextField;
	MEField *pConditionField;
} CELoreDialogGroup;

typedef struct CEStoreGroup
{
	ContactEditDoc *pDoc;

	int index;

	UIButton *pButton;
	UILabel *pStoreLabel;

	MEField *pStoreField;

	StoreRef*** peaStores;
} CEStoreGroup;

typedef struct CEStoreCollectionGroup
{
	ContactEditDoc *pDoc;

	int index;

	UIButton *pRemoveCollectionButton;
	UIButton *pAddStoreButton;
	UILabel *pOptionTextLabel;
	UILabel *pConditionLabel;
	UISeparator *pSeparator;

	MEField *pOptionTextField;
	MEField *pConditionField;
	CEStoreGroup** eaStoreGroups;
} CEStoreCollectionGroup;

typedef struct CEAuctionBrokerContactDataGroup
{
	ContactEditDoc *pDoc;

	int index;

	UIButton *pRemoveBrokerButton;
	UISeparator *pSeparator;
	UILabel *pOptionTextLabel;
	UILabel *pAuctionBrokerDefLabel;

	MEField *pOptionTextField;
	MEField *pAuctionBrokerDefField;
} CEAuctionBrokerContactDataGroup;

typedef struct CEUGCSearchAgentDataGroup
{
	ContactEditDoc *pDoc;

	int index;

	UIButton *pRemoveButton;
	UISeparator *pSeparator;

	UILabel *pOptionTextLabel;
	UILabel *pTitleLabel;
	UILabel *pDialogTextLabel;
	UILabel *pLocationLabel;
	UILabel *pMaxDurationLabel;
	UILabel *pLastNDaysLabel;

	MEField *pOptionTextField;
	MEField *pTitleField;
	MEField *pDialogTextField;
	MEField *pLocationField;
	MEField *pMaxDurationField;
	MEField *pLastNDaysField;
} CEUGCSearchAgentDataGroup;

typedef struct CEEndDialogAudioGroup
{
	ContactEditDoc *pDoc;

	int index;

	UIButton *pButton;
	UILabel *pAudioFileLabel;

	MEField *pAudioFileField;
} CEEndDialogAudioGroup;

typedef struct CEPowerStoreGroup
{
	ContactEditDoc *pDoc;

	int index;

	UIButton *pButton;
	UILabel *pStoreLabel;

	MEField *pStoreField;
} CEPowerStoreGroup;

typedef struct CEImageMenuItemGroup
{
	CECommonCallbackParams *pCommonCallbackParams;
	CEImageMenuItemAccessorFunc pImageMenuItemFromWrapperFunc;

	int index;
	void ***peaWrappers;	// Array of structs to manipulate when performing a remove/move action
	ParseTable* pWrapperParseTable;	// Parse table of wrapper structs

	UIButton *pRemoveButton;
	UISeparator *pSeparator;

	UILabel *pNameLabel;
	MEField *pNameField;
	UILabel *pVisibleConditionLabel;
	MEField *pVisibleConditionField;
	UILabel *pRequiresConditionLabel;
	MEField *pRequiresConditionField;
	UILabel *pRecommendedConditionLabel;
	MEField *pRecommendedConditionField;
	UILabel *pPosLabel;
	MEField *pPosXField;
	MEField *pPosYField;
	UIButton* pPosPlaceButton;
	UILabel *pIconLabel;
	MEField *pIconField;
	UILabel *pActionLabel;
	UIGameActionEditButton *pActionButton;


} CEImageMenuItemGroup;

typedef struct ContactDialogUINode ContactDialogUINode;

typedef struct ContactDialogUINodeAction
{
	// The special dialog action associated
	SpecialDialogAction *pSpecialDialogAction; // Assigned for regular nodes

	// The special dialog block associated
	SpecialDialogBlock *pSpecialDialogBlock; // Assigned for root node

	// The parent of this action
	ContactDialogUINode *pParentDialogNode;

	// The pane that contains all other UI elements
	UIPane *pPane;

} ContactDialogUINodeAction;

typedef struct DialogFlowWindowInfo DialogFlowWindowInfo;

typedef struct ContactDialogUINode
{
	// Indicates if this dialog node is the dialog root
	bool bIsDialogRoot;

	// The name of the special dialog
	char *pchSpecialDialogName;

	// The UI graph node
	UIGraphNode *pGraphNode;

	// The pane that contains all other UI elements
	UIPane *pPane;

	// The dialog flow window info
	DialogFlowWindowInfo *pInfo;

	// Incoming connection button
	UIButton *pIncomingConnectionButton;

	// Outgoing connection button
	UIButton *pOutgoingConnectionButton;

	// The actions for this node
	ContactDialogUINodeAction **eaActions;

	// Is this dialog visited for auto arrange mode
	bool bIsVisitedInAutoArrangeMode;

	// Only used for the root node
	SpecialDialogBlock **eaRootLevelSpecialDialogBlocks;

	// Only used for the mission offer nodes
	ContactMissionOffer *pMissionOffer;

	// Paired boxes for incoming and outgoing connections
	UIPairedBox **eaIncomingPairedBoxes;
	UIPairedBox **eaOutgoingPairedBoxes;

	// The add child dialog button
	UIButton *pAddChildDialogButton;

	// The clone dialog button
	UIButton *pCloneDialogButton;
} ContactDialogUINode;

typedef struct MissionEditDoc MissionEditDoc;

typedef struct DialogFlowWindowInfo
{
	// Contact doc is used in the contact editor
	ContactEditDoc *pContactDoc;

	// Following two fields are used in the mission editor
	MissionEditDoc *pMissionDoc;
	const char *pchContactName;

	// The window that represents the dialog flow
	UIWindow *pDialogFlowWin;

	// The label for the contact name
	UILabel *pContactNameLabel;
	UILabel *pContactNameValueLabel;

	// The scroll area for the flow window
	UIScrollArea * pDialogFlowScrollArea;

	// The context menu
	UIMenu *pContextMenu;

	// UI elements below are not owned by the dialog flow window
	UIExpander *pSpecialExpander;
	UIExpander *pOfferExpander;

	// The contact dialog nodes used in the dialog flow window
	ContactDialogUINode **eaDialogNodes;

	// The dialog nodes which are selected
	ContactDialogUINode **eaSelectedDialogNodes;

	Vec2 vecLastMousePoint;
	Vec2 vecMouseDownPoint;

	// The pane that is currently focused due to the dialog node window title click
	UIWidget *pWidgetToFocusOnDialogNodeClick;

	// The expander highlighted in the editor
	UIExpander *pHighlightedExpander;

	// The window that the expander is in (only for the mission editor)
	UIWindow *pHighlightedWindow;

	// Temporary variable holding the text entry when the dialog 
	UITextEntry *pMissionOfferSpecialDialogNamePromptTextEntry;

} DialogFlowWindowInfo;

typedef struct ContactEditDoc
{
	EMEditorDoc emDoc;
	void *unusedPointerNeededToAvoidCrashesWithOldKeybindFiles;

	ContactDef *pContact;
	ContactDef *pOrigContact;
	ContactDef *pOrigContactUntouched;
	ContactDef *pNextUndoContact;

	bool bIgnoreFieldChanges;
	bool bIgnoreFilenameChanges;

	UIWindow *pMainWindow;
	UILabel *pFilenameLabel;
	UILabel *pCostumeLabel;
	UILabel *pCostumeTypeLabel;
	UILabel *pCritterGroupTypeLabel;
	UILabel *pCritterGroupIdentifierLabel;
	UILabel *pHeadshotLabel;
	UILabel *pCutSceneLabel;
	UILabel *pSourceTypeLabel;
	UILabel *pSourceNameLabel;
	UILabel *pSourceSecondaryNameLabel;
	UILabel *pAnimListLabel;
	UILabel *pMapLabel;
	UILabel *pContactIndicatorOverrideLabel;
	UILabel *pContactFlagsLabel;
	UILabel *pSkillTrainerTypeLabel;
	UILabel *pMinigameTypeLabel;
	UILabel *pCharacterClassLabel;
	UILabel *pShowLastPuppetLabel;
	UILabel *pBuyOptionLabel;
	UILabel *pSellOptionLabel;
	UILabel *pBuyBackOptionLabel;
	UILabel *pDialogExitTextOverrideLabel;
	UILabel *pResearchStoreCollectionLabel;
	UILabel *pHideFromRemoteContactListLabel;
	UILabel *pUpdateOptionsLabel;
	UILabel *pImageMenuTitleLabel;
	UILabel *pImageMenuBGImageLabel;

	UIGimmeButton *pFileButton;
	UIButton *pSpecialAddButton;
	UIButton *pActionBlockAddButton;
	UIButton *pOfferAddButton;
	UIButton *pStoreAddButton;
	UIButton *pEndDialogAudioAddButton;
	UIButton *pPowerStoreAddButton;
	UIButton *pStoreCollectionAddButton;
	UIButton *pLoreDialogAddButton;
	UIButton *pAuctionBrokerAddButton;
	UIButton *pUGCSearchAgentAddButton;
	UIButton *pImageMenuItemAddButton;
	UIExpanderGroup *pExpanderGroup;
	UIExpander *pInfoExpander;
	UIExpander *pDialogExpander;
	UIExpander *pSpecialExpander;
	UIExpander *pLoreExpander;
	UIExpander *pStoreExpander;
	UIExpander *pStoreCollectionExpander;
	UIExpander *pEndDialogAudioListExpander;
	UIExpander *pOfferExpander;
	UIExpander *pMissionSearchExpander;
	UIExpander *pItemAssignmentExpander;
	UIExpander *pAuctionBrokerExpander;
	UIExpander *pUGCSearchAgentExpander;
	UIExpander *pImageMenuExpander;

	MEField **eaDocFields;
	MEField *pDisplayNameField;
	MEField *pDialogExitTextOverrideField;
	MEField *pMapNameField;
	MEField *pInfoTextField;
	MEField* pHeadshotStyleField;
	MEField* pCutSceneField;
	MEField* pSourceTypeField;
	MEField* pSourceNameField;
	MEField* pSourceSecondaryNameField;
	MEField *pAnimListField;
	MEField *pInteractCondField;
	MEField *pSearchMsgField;
	MEField *pContactIndicatorOverrideField;
	
	MEField *pPetContactField;
	MEField *pCostumeField;
	MEField *pCostumeTypeField;
	MEField *pCritterGroupTypeField;
	MEField *pCritterGroupField;
	MEField *pCritterMapVarField;
	MEField *pCritterGroupIdentifierField;
	MEField *pContactFlagsField;
	MEField *pSkillTrainerTypeField;
	MEField *pMinigameTypeField;
	MEField *pCharacterClassField;
	MEField *pShowLastPuppetField;
	MEField *pOptionalActionCategoryField;
	MEField *pCanAccessRemotelyField;
	MEField *pBuyOptionField;
	MEField *pSellOptionField;
	MEField *pBuyBackOptionField;
	MEField *pResearchStoreCollectionField;
	MEField *pHideFromRemoteContactListField;
	MEField *pUpdateOptionsField;
	MEField *pItemAssignmentRefreshTimeField;
	MEField *pItemAssignmentRarityCountField;
	MEField *pImageMenuTitleField;
	MEField *pImageMenuBGImageField;
	CEDialogGroup **eaDialogGroups;
	CESpecialDialogGroup **eaSpecialGroups;
	CESpecialDialogOverrideGroup **eaSpecialOverrideGroups;
	CESpecialActionBlockGroup **eaSpecialActionBlockGroups;
	CELoreDialogGroup **eaLoreDialogGroups;
	CEDialogGroup **eaMissionSearchDialogGroups;
	CEOfferGroup **eaOfferGroups;
	CEStoreGroup **eaStoreGroups;
	CEPowerStoreGroup **eaPowerStoreGroups;
	CEEndDialogAudioGroup **eaEndDialogAudioGroups;
	CEStoreCollectionGroup **eaStoreCollections;
	CEAuctionBrokerContactDataGroup **eaAuctionBrokers;
	CEUGCSearchAgentDataGroup **eaUGCSearchAgents;
	CEDialogGroup **eaNoStoreDialogGroups;
	CEImageMenuItemGroup **eaImageMenuItemGroups;

	char **eaVarNames;

	// The contact flow info
	DialogFlowWindowInfo *pDialogFlowWindowInfo;

} ContactEditDoc;

typedef struct ContactEditorLook
{
	bool bInited;

	// Skin for unselected dialog nodes
	UISkin *pUnselectedNodeSkin;

	// Skin for selected dialog nodes
	UISkin *pSelectedNodeSkin;

	// Special dialog pane skin
	UISkin *pSpecialDialogPaneSkin;
} ContactEditorLook;

ContactEditDoc *CEOpenContact(EMEditor *pEditor, char *pcName);
void CERevertContact(ContactEditDoc *pDoc);
void CECloseContact(ContactEditDoc *pDoc);
EMTaskStatus CESaveContact(ContactEditDoc* pDoc, bool bSaveAsNew);
void CEInitData(EMEditor *pEditor);

// Initializes the dialog flow window based on the document given
DialogFlowWindowInfo * CEInitDialogFlowWindowWithContactDoc(SA_PARAM_NN_VALID ContactEditDoc *pDoc);

// Initializes the dialog flow window for a mission document
DialogFlowWindowInfo * CEInitDialogFlowWindowWithMissionDoc(SA_PARAM_NN_VALID MissionEditDoc *pDoc);

// Refreshes the whole dialog flow window to reflect the changes
void CERefreshDialogFlowWindow(SA_PARAM_NN_VALID DialogFlowWindowInfo *pInfo);

// Sets the contact name for the dialog flow window
void CESetContactForDialogFlowWindow(SA_PARAM_NN_VALID DialogFlowWindowInfo *pInfo, SA_PARAM_NN_STR const char *pchContactName);

//-----------------------------------------------------------
// Functions to add special dialog groups to other editors
//-----------------------------------------------------------

CESpecialDialogGroup *CECreateSpecialDialogGroup(const char* pchContact, char*** peaVarNames,
												 CECallbackFunc pDocIsEditableFunc, void* pDocIsEditableData, 
												 CECallbackFunc pDialogChangedFunc, void* pDialogChangedData, 
												 CECallbackFunc pMessageFixupFunc, void* pMessageFixupData, 
												 CESpecialDialogAccessorFunc pDialogFromWrapperFunc, ParseTable pWrapperParseTable[],
												 MEFieldChangeCallback pFieldChangeFunc, MEFieldPreChangeCallback pFieldPreChangeFunc, void* pFieldChangeData);

CESpecialActionBlockGroup *CECreateSpecialActionBlockGroupParams(const char* pchContact, char*** peaVarNames,
																 CECallbackFunc pDocIsEditableFunc, void* pDocIsEditableData, 
																 CECallbackFunc pDialogChangedFunc, void* pDialogChangedData, 
																 CECallbackFunc pMessageFixupFunc, void* pMessageFixupData, 
																 CESpecialActionBlockAccessorFunc pDialogFromWrapperFunc, ParseTable pWrapperParseTable[],
																 MEFieldChangeCallback pFieldChangeFunc, MEFieldPreChangeCallback pFieldPreChangeFunc, void* pFieldChangeData);

void CEFreeSpecialDialogGroup(CESpecialDialogGroup *pGroup);

int CERefreshSpecialGroup(CESpecialDialogGroup *pGroup, UIExpander *pExpander, F32 y, F32 xPercentWidth, int index, void ***peaSpecialDialogWrappers, SpecialDialogBlock *pBlock, SpecialDialogBlock *pOldBlock, bool bSplitView, MissionDef *pMissionDef);

int CERefreshSpecialActionBlockGroup( CESpecialActionBlockGroup *pGroup, CECommonCallbackParams *pCommonCallbackParams, UIExpander *pExpander, F32 y, int index, SpecialActionBlock ***peaActionBlocks, void ***peaActionBlockWrappers, SpecialActionBlock *pActionBlock, SpecialActionBlock *pOldActionBlock );

void CEFixupSpecialDialogMessages(SpecialDialogBlock* pBlock, const char* pcKey, const char* pcDesc, const char* pcNameKey, const char* pcNameDesc, const char* pcActionDesc, int iIndex);

//Fixes the messages for a single special action block
void CEFixupActionBlockMessages(SpecialActionBlock *pBlock, const char* pchNameKey, int iIndex);

ContactMissionOffer * CECreateBlankMissionOffer(void);

void CEFreeOfferGroup(CEOfferGroup *pGroup);

void CEFreeSpecialActionBlockGroup(CESpecialActionBlockGroup *pGroup);

CEOfferGroup *CECreateOfferGroup(const char *pchContact,
								 CECallbackFunc pDocIsEditableFunc, void* pDocIsEditableData, 
								 CECallbackFunc pDialogChangedFunc, void* pDialogChangedData, 
								 CECallbackFunc pMessageFixupFunc, void* pMessageFixupData, 
								 CEMissionOfferAccessorFunc pOfferFromWrapperFunc, ParseTable pWrapperParseTable[],
								 MEFieldChangeCallback pFieldChangeFunc, MEFieldPreChangeCallback pFieldPreChangeFunc, void* pFieldChangeData);

F32 CERefreshOfferGroup(CEOfferGroup *pGroup, UIExpander *pExpander, F32 y, F32 xPercentWidth, int index, void ***peaMissionOfferWrappers, ContactMissionOffer *pOffer, ContactMissionOffer *pOldOffer, bool bSplitView, MissionDef *pMissionDef);

void CEFixupOfferMessages(SA_PARAM_NN_STR const char *pchBaseKey, SA_PARAM_NN_STR const char *pchContactName, SA_PARAM_NN_VALID ContactMissionOffer *pOffer, int index);

void CEStripEmptyDialogBlocks(DialogBlock ***peaBlocks);

void CERemoveSpecialDialogGroup(SA_PARAM_NN_VALID CESpecialDialogGroup *pGroup);
void CERemoveOffer(SA_PARAM_NN_VALID CEOfferGroup *pGroup);

CEImageMenuItemGroup *CECreateImageMenuItemGroup(const char* pchContact,
												 CECallbackFunc pDocIsEditableFunc, void* pDocIsEditableData,
												 CECallbackFunc pChangedFunc, void* pChangedData,
												 CECallbackFunc pMessageFixupFunc, void* pMessageFixupData,
												 CEImageMenuItemAccessorFunc pImageMenuItemFromWrapperFunc, ParseTable pWrapperParseTable[],
												 MEFieldChangeCallback pFieldChangeFunc, MEFieldPreChangeCallback pFieldPreChangeFunc, void* pFieldChangeData);
F32 CERefreshImageMenuItemGroup(CEImageMenuItemGroup *pGroup, UIExpander *pExpander,
								F32 y, int index,
								void ***peaWrappers, void ***peaOldWrappers);
void CEFreeImageMenuItemGroup(CEImageMenuItemGroup *pGroup);

void CEFixupImageMenuItemMessages(const char* baseMessageKey, int i, ContactImageMenuItem* pImageMenuItemData );


void CEFreeDialogFlowWindow(DialogFlowWindowInfo *pInfo);
// Initialize the look of the contact editor
void InitializeContactEditorLook(void);

void CEExportDialogBlocksToCSV(FILE *pFile, DialogBlock ***ppDialogBlocks, 
	const char *pchDialogType, const char *pchDateExported, 
	const char *pchContactName, const char *pchMissionName);

//Finds the special action block with the given name
SpecialActionBlock * CEDialogFlowGetSpecialActionBlockByName(SA_PARAM_NN_VALID DialogFlowWindowInfo *pInfo, SA_PARAM_NN_STR const char *pchName, SA_PARAM_OP_STR const char *pchContactName);

// Finds the special dialog with the given name
SpecialDialogBlock * CEDialogFlowGetSpecialDialogByName(SA_PARAM_NN_VALID DialogFlowWindowInfo *pInfo, SA_PARAM_NN_STR const char *pchName, SA_PARAM_OP_STR const char *pchContactName);

void CEUpdateDisplay(ContactEditDoc *pDoc);

const char * CEGetPooledSpecialDialogNameByMission(SA_PARAM_NN_STR const char *pchSpecialDialogName, SA_PARAM_NN_STR  const char *pchMissionName);

#endif
