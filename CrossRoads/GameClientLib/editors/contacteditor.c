/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef NO_EDITORS

#include "GfxEditorIncludes.h"
#include "contact_common.h"
#include "contacteditor.h"
#include "EditorPrefs.h"
#include "Expression.h"
#include "gameaction_common.h"
#include "gameeditorshared.h"
#include "ItemAssignments.h"
#include "mission_common.h"
#include "MultiEditField.h"
#include "soundLib.h"
#include "StringCache.h"
#include "GameActionEditor.h"
#include "UIGimmeButton.h"
#include "Color.h"
#include "UIGraphNode.h"
#include "inputLib.h"
#include "inputText.h"
#include "GfxTexAtlas.h"
#include "EditLibUIUtil.h"
#include "missioneditor.h"
#include "StringFormat.h"
#include "Message.h"
#include "AuctionBrokerCommon.h"
#include "EditorManagerUIPickers.h"
#include "WorldGrid.h"

#include "../StaticWorld/group.h"

#include "AutoGen/contact_common_h_ast.h"
#include "AnimList_Common.h"
#include "AnimList_Common_h_ast.h"
#include "CharacterClass.h"
#include "CharacterClass_h_ast.h"
#include "cutscene_common_h_ast.h"

#define DIALOG_NODE_WINDOW_WIDTH 200
#define DIALOG_NODE_WINDOW_MIN_HEIGHT 40
#define CE_UNTITLED_ACTION "[Untitled action]"
#define CE_GREETING_LABEL "Greeting"
#define CE_COMPLETED_MISSION_LABEL "Completed"
#define DIALOG_NODE_ROOT_NAME "[DIALOG ROOT]"
#define DIALOG_NODE_UNTITLED_MISSION "Untitled Mission"
#define DIALOG_NODE_CONNECT_PAYLOAD "DIALOG_NODE_CONNECT_PAYLOAD"
#define DIALOG_NODE_ACTION_CONNECT_PAYLOAD "DIALOG_NODE_ACTION_CONNECT_PAYLOAD"
#define DIALOG_NODE_ACTION_PANE_HEIGHT 56
#define DIALOG_NODE_ACTION_PANE_DISTANCE 10
#define DIALOG_NODE_AUTO_ARRANGE_BEGIN_XPOS 45
#define DIALOG_NODE_AUTO_ARRANGE_BEGIN_YPOS 10
#define DIALOG_NODE_AUTO_ARRANGE_HSPACING 50
#define DIALOG_NODE_AUTO_ARRANGE_VSPACING 50

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););


//-----------------------------------------------------------------------------------
// Global Data
//-----------------------------------------------------------------------------------

static bool gInitializedEditor = false;
static bool gIndexChanged = false;
static bool s_bShowContinueTextAndTextFormatter = true;

static char **geaScopes = NULL;

extern EMEditor *s_ContactEditor;

static UISkin *gBoldExpanderSkin;

// UI specific data
static ContactEditorLook s_ContactEditorLook;

struct {
	ContactDialogUINodeAction *pAction;
	ContactDialogUINode *pSourceNode;
	ContactDialogUINode *pDestNode;
	UIWindow *pModalWin;
} s_ConnectCallState = { 0 };

//---------------------------------------------------------------------------------------------------
// Function Prototypes and type definitions
//---------------------------------------------------------------------------------------------------

#define X_OFFSET_BASE			15
#define X_OFFSET_CONTROL		125
#define X_OFFSET_BASE2			240
#define X_OFFSET_CONTROL2		350
#define X_OFFSET_BASE3			465
#define X_OFFSET_CONTROL3		575
#define X_OFFSET_AUDIO			10
#define	X_OFFSET_ANIM			10

#define X_PERCENT_SPLIT			0.55
#define X_PERCENT_SECOND_SML	0.20
#define X_PERCENT_SECOND		0.40
#define X_PERCENT_SMALL			0.5
#define X_PERCENT_SMALL_SECOND  0.5

#define STANDARD_ROW_HEIGHT  28
#define TEXTURE_ROW_HEIGHT	 75
#define EXPANDER_HEIGHT      11

#define CECreateLabel(pcText,pcTooltip,x,y,pExpander)  CERefreshLabel(NULL, (pcText), (pcTooltip), (x), 0, (y), (pExpander))

static void CEContactChanged(ContactEditDoc *pDoc, bool bUndoable);
static void CEFieldChangedCB(MEField *pField, bool bFinished, ContactEditDoc *pDoc);
static void CERemoteFieldsChangedCB(MEField *pField, bool bFinished, ContactEditDoc *pDoc);
static void CERemoteOfferFieldsChangedCB(MEField *pField, bool bFinished, CEOfferGroup *pGroup);
static bool CEFieldPreChangeCB(MEField *pField, bool bFinished, ContactEditDoc *pDoc);
static void CERefreshSpecialExpander(ContactEditDoc *pDoc);
static bool CEFixupMessagesWrapper(ContactEditDoc *pDoc);
//populates a CECommonCallbackParams, needed to check gimme for things with mission overrides.
static CECommonCallbackParams *CECreateCommonCallbackParams(CECallbackFunc pDocIsEditableFunc, void* pDocIsEditableData,	CECallbackFunc pDialogChangedFunc, void* pDialogChangedData, CECallbackFunc pMessageFixupFunc,	void* pMessageFixupData, CESpecialDialogAccessorFunc pDialogFromWrapperFunc,	MEFieldChangeCallback pFieldChangeFunc, MEFieldPreChangeCallback pFieldPreChangeFunc, void* pFieldChangeData);
//the game action editor uses these to check gimme and refresh things:
static void CESpecialGameActionChangeCB(UIGameActionEditButton *pButton, CESpecialActionGroup *pGroup);
static void CEImageMenuGameActionChangeCB(UIGameActionEditButton *pButton, CEImageMenuItemGroup *pGroup);
// Finds a mission offer node
static ContactDialogUINode *CEDialogUIMissionOfferNodeFromName(SA_PARAM_NN_VALID DialogFlowWindowInfo *pInfo, SA_PARAM_NN_STR const char *pchMissionName);
// Stores the dialog node window positions
static void CESaveDialogNodeWindowPositions(SA_PARAM_NN_VALID DialogFlowWindowInfo *pInfo);
// Restores the dialog node window positions
static void CERestoreDialogNodeWindowPositions(SA_PARAM_NN_VALID DialogFlowWindowInfo *pInfo);
// Updates the spline colors for an individual node
static void CEUpdateSplineColorsForNode(SA_PARAM_NN_VALID ContactDialogUINode *pDialogNode, Color inColor, Color outColor, F32 lineScale);
// Updates the spline colors for all nodes
static void CEUpdateSplineColors(SA_PARAM_NN_VALID DialogFlowWindowInfo *pInfo);
// Finds the dialog root node
static ContactDialogUINode *CEGetRootDialogUINode(SA_PARAM_NN_VALID DialogFlowWindowInfo *pInfo);
// Finds a dialog node matching the special dialog name
static ContactDialogUINode *CEDialogUINodeFromName(SA_PARAM_NN_VALID DialogFlowWindowInfo *pInfo, SA_PARAM_NN_STR const char *pchSpecialDialogName);
// Toggles the visibility of an individual special dialo pane
static void CEToggleDialogNodePane(SA_PARAM_NN_VALID ContactDialogUINode *pContactDialogNode, bool bVisible);
// Toggles the visibility of the panes of all special dialogs
static void CEToggleAllSpecialDialogPanes(SA_PARAM_NN_VALID DialogFlowWindowInfo *pInfo, bool bVisible);
// Clears all node selection
static void CEClearDialogNodeSelection(SA_PARAM_NN_VALID DialogFlowWindowInfo *pInfo);
// Selects all dialog nodes
static void CESelectAllDialogNodes(SA_PARAM_NN_VALID DialogFlowWindowInfo *pInfo);
static void CESetZoomScale(SA_PARAM_NN_VALID UIWindow *pWin, SA_PARAM_NN_VALID UIScrollArea *pScrollArea, F32 scale);
// Called whenever a dialog node loses focus
static void CEOnDialogNodeLostFocus(UIWindow *pWin, ContactDialogUINode *pContactDialogNode);
// Handles the double click event on the dialop nodes
static void CEOnDialogNodeDoubleClick(SA_PARAM_NN_VALID ContactDialogUINode *pContactDialogNode);
// Called when the mouse is down on a dialog node
static void CEOnDialogNodeMouseDown(UIGraphNode *pGraphNode, Vec2 clickPoint, ContactDialogUINode *pContactDialogNode);
// Called when the dialog node is dragged
static void CEOnDialogNodeMouseDrag(UIGraphNode *pGraphNode, Vec2 clickPoint, ContactDialogUINode *pContactDialogNode);
// Called when the mouse is down on a dialog node
static void CEOnDialogNodeMouseUp(UIGraphNode *pGraphNode, Vec2 clickPoint, ContactDialogUINode *pContactDialogNode);
// Connects an action to a special dialog node or root
static void CEConnectActionWithDialogNode(SA_PARAM_NN_VALID ContactDialogUINodeAction *pAction, SA_PARAM_NN_VALID ContactDialogUINode *pNode);
// Called when the incoming or outgoing connection buttons are dragged
static void CEOnDialogNodeDrag(UIButton *button, ContactDialogUINode *pContactDialogNode);
// Called when the incoming or outgoing connection buttons are dropped
static void CEOnDialogNodeDrop(UIButton *source, UIButton *dest, UIDnDPayload *payload, ContactDialogUINode *pDestDialogNode);
// Properly destroys a dialog node action
static void CEDestroyDialogNodeAction(ContactDialogUINodeAction *pContactDialogNodeAction);
// Properly destroys a dialog node
static void CEDestroyDialogNode(ContactDialogUINode *pContactDialogNode);
// Creates a contact dialog node
static ContactDialogUINode * CECreateDialogNode(SA_PARAM_NN_VALID DialogFlowWindowInfo *pInfo, 
												SA_PARAM_OP_STR const char * pchSpecialDialogName, 
												SA_PARAM_OP_VALID ContactMissionOffer *pMissionOffer, 
												F32 x, F32 y);
// Called when the user drags a dialog action connection button
static void CEOnDialogNodeActionConnectDrag(UIButton *pConnectionButton, ContactDialogUINodeAction *pDialogNodeAction);
// Called when the user drops a dialog action connection button
static void CEOnDialogNodeActionConnectDrop(UIAnyWidget *pSourceWidget, UIButton *pDestConnectionButton, UIDnDPayload *payload, ContactDialogUINodeAction *pDestDialogNodeAction);
// Creates a dialog node action
static ContactDialogUINodeAction * CECreateDialogNodeAction(SA_PARAM_NN_VALID ContactDialogUINode *pDialogNode,
															SA_PARAM_OP_VALID SpecialDialogBlock *pSpecialDialogBlock, // For root node
															SA_PARAM_OP_VALID SpecialDialogAction *pSpecialDialogAction, // For special dialog nodes
															S32 iIndex, F32 x, F32 y);
// Populates all actions for a dialog node
static void CEPopulateDialogNodeActions(SA_PARAM_OP_VALID SpecialDialogBlock *pSpecialDialog, SA_PARAM_NN_VALID ContactDialogUINode *pDialogNode);
static S32 CEAutoArrangeGetXPosForDepth(S32 iDepth);
// Places an individual dialog node in its proper place for auto arrangement
static void CEAutoArrangeIndividualNode(SA_PARAM_NN_VALID DialogFlowWindowInfo *pInfo, SA_PARAM_NN_VALID SpecialDialogBlock *pSpecialDialogBlock, S32 iDepth, S32 iIndex, S32 *pCurrentYPos);
// Automatically arranges the UI for the dialog nodes
static void CEAutoArrangeDialogNodeUI(SA_PARAM_NN_VALID DialogFlowWindowInfo *pInfo);
static void CEOnDialogFlowMenuActionAutoArrange(UIMenuItem *pMenuItem, SA_PARAM_NN_VALID DialogFlowWindowInfo *pInfo);
// Called for context menu action on the dialog flow window
static void CEShowDialogFlowWindowContextMenu(UIAnyWidget *pSourceWidget, SA_PARAM_NN_VALID DialogFlowWindowInfo *pInfo);
// Handles the raised and focus events for the dialog flow window
static void CEOnDialogFlowWindowRaised(UIWindow *pWin, DialogFlowWindowInfo *pInfo);
// Scrolls to the special dialog block based on the dialog node given
static void CEScrollToSpecialDialogBlock(SA_PARAM_NN_VALID ContactDialogUINode *pContactDialogNode, bool bSwitchToMainWindow);
// Handles the deletion of a dialog node action
static void CEOnDeleteDialogNodeAction(UIButton *pButton, ContactDialogUINodeAction *pAction);
// Connects two dialog nodes to each other by creating an action in the source node
static void CEConnectDialogNodes(SA_PARAM_NN_VALID ContactDialogUINode *pSourceNode, SA_PARAM_NN_VALID ContactDialogUINode *pDestNode);
// Handles the click event for the add child dialog button
static void CEOnAddChildDialogButtonClick(UIButton *pButton, ContactDialogUINode *pDialogNode);
// Creates a new special dialog block and adds it to the contact definition
static SpecialDialogBlock * CEAddSpecialDialogBlock(SA_PARAM_NN_VALID DialogFlowWindowInfo *pInfo);
// Creates a special dialog action used to connect nodes together
static SpecialDialogAction * CECreateSpecialDialogActionForConnection(SA_PARAM_NN_VALID DialogFlowWindowInfo *pInfo, SA_PARAM_OP_STR const char * pchTargetDialogName);
// Adds all the special dialogs based on the doc set to the given array
static void CEDialogFlowGetSpecialDialogs(SA_PARAM_NN_VALID DialogFlowWindowInfo *pInfo, 
										  SA_PARAM_NN_VALID SpecialDialogBlock ***peaSpecialDialogs);
static bool CESpecialDialogNameIsUnique(SA_PARAM_NN_VALID ContactEditDoc *pActiveDoc,
										SA_PARAM_NN_STR const char *pchName, 
										SA_PARAM_OP_VALID ContactMissionOffer *pMissionOfferToSkip, 
										SA_PARAM_OP_VALID SpecialDialogBlock *pSpecialDialogBlockToSkip);
// Returns the special dialog group matching the given special dialog name
static CESpecialDialogGroup * CEDialogFlowGetSpecialDialogGroupByName(SA_PARAM_NN_VALID DialogFlowWindowInfo *pInfo,
																	  SA_PARAM_NN_STR const char *pchSpecialDialogName, 
																	  SA_PARAM_OP_VALID SpecialDialogBlock **ppSpecialDialogBlockOut,
																	  SA_PARAM_OP_VALID UIWindow **ppWindowOut);
// Adds a new special dialog to either the mission or the contact document
static void CEDialogFlowWindowAddSpecialDialogToDocument(SA_PARAM_NN_VALID DialogFlowWindowInfo *pInfo,
														 SA_PARAM_NN_VALID SpecialDialogBlock *pNewSpecialDialogBlock);
//Adds a new special action block to either the mission or contact document
static void CEDialogFlowWindowAddSpecialActionBlockToDocument(SA_PARAM_NN_VALID DialogFlowWindowInfo *pInfo, SA_PARAM_NN_VALID SpecialActionBlock *pNewSpecialActionBlock);
static EMEditorDoc * CEGetEMEditorDocFromDialogNode(SA_PARAM_NN_VALID ContactDialogUINode *pNode);
static void CEDialogFlowWindowRefreshSpecialExpander(SA_PARAM_NN_VALID DialogFlowWindowInfo *pInfo);

// Initialize the look of the contact editor
void InitializeContactEditorLook(void)
{
	if (!s_ContactEditorLook.bInited)
	{
		s_ContactEditorLook.bInited = true;

		s_ContactEditorLook.pSelectedNodeSkin = ui_SkinCreate(NULL);
		ui_SkinSetBorderEx(s_ContactEditorLook.pSelectedNodeSkin, colorFromRGBA(0x0099ffFF), colorFromRGBA(0x0099ffFF));
		UI_SET_STYLE_BAR_NAME(s_ContactEditorLook.pSelectedNodeSkin->hTitlebarBar, "FSM_WindowTitle");

		s_ContactEditorLook.pUnselectedNodeSkin = ui_SkinCreate(NULL);
		ui_SkinSetBorderEx(s_ContactEditorLook.pUnselectedNodeSkin, colorFromRGBA(0x999999FF), colorFromRGBA(0x999999FF));
		UI_SET_STYLE_BAR_NAME(s_ContactEditorLook.pUnselectedNodeSkin->hTitlebarBar, "FSM_WindowTitleNoSel");

		s_ContactEditorLook.pSpecialDialogPaneSkin = ui_SkinCreate(NULL);
		ui_SkinSetBorder(s_ContactEditorLook.pSpecialDialogPaneSkin, colorFromRGBA(0xFFFFFFFF));
		ui_SkinSetBackground(s_ContactEditorLook.pSpecialDialogPaneSkin, colorFromRGBA(0xB4C8FFFF));
	}
}

//---------------------------------------------------------------------------------------------------
// Data Manipulation
//---------------------------------------------------------------------------------------------------

static ContactEditDoc *CEGetActiveDoc()
{
	EMEditorDoc *pDoc = emGetActiveEditorDoc();
	if (pDoc && stricmp(pDoc->doc_type, "contact")==0)
	{
		return (ContactEditDoc*)pDoc;
	}
	return NULL;
}

static bool CEIsDocEditable(ContactEditDoc *pDoc)
{
	return (pDoc && emDocIsEditable(&pDoc->emDoc, true));
}

static bool CEUpdateUI(ContactEditDoc *pDoc)
{
	if(!pDoc)
		return false;

	if (pDoc->pDialogFlowWindowInfo)
	{
		CERefreshDialogFlowWindow(pDoc->pDialogFlowWindowInfo);
	}

	CEContactChanged(pDoc, true);
	return true;
}


static void CEIndexChangedCB(void *unused)
{
	// Update scopes list
	gIndexChanged = false;
	resGetUniqueScopes(g_ContactDictionary, &geaScopes);
}


static void CEContactDictChanged(enumResourceEventType eType, const char *pDictName, const char *pcName, Referent pReferent, void *pUserData)
{
	if ((eType == RESEVENT_INDEX_MODIFIED) && !gIndexChanged) {
		gIndexChanged = true;
		emQueueFunctionCall(CEIndexChangedCB, NULL);
	}
}


static void CEContactUndoCB(ContactEditDoc *pDoc, CEUndoData *pData)
{
	// Put the undo contact into the editor
	StructDestroy(parse_ContactDef, pDoc->pContact);
	pDoc->pContact = StructClone(parse_ContactDef, pData->pPreContact);
	if (pDoc->pNextUndoContact) {
		StructDestroy(parse_ContactDef, pDoc->pNextUndoContact);
	}
	pDoc->pNextUndoContact= StructClone(parse_ContactDef, pDoc->pContact);

	// Update the UI
	CEContactChanged(pDoc, false);
}


static void CEContactRedoCB(ContactEditDoc *pDoc, CEUndoData *pData)
{
	// Put the undo contact into the editor
	StructDestroy(parse_ContactDef, pDoc->pContact);
	pDoc->pContact = StructClone(parse_ContactDef, pData->pPostContact);
	if (pDoc->pNextUndoContact) {
		StructDestroy(parse_ContactDef, pDoc->pNextUndoContact);
	}
	pDoc->pNextUndoContact= StructClone(parse_ContactDef, pDoc->pContact);

	// Update the UI
	CEContactChanged(pDoc, false);
}


static void CEContactUndoFreeCB(ContactEditDoc *pDoc, CEUndoData *pData)
{
	// Free the memory
	StructDestroy(parse_ContactDef, pData->pPreContact);
	StructDestroy(parse_ContactDef, pData->pPostContact);
	free(pData);
}

const char * CEGetPooledSpecialDialogNameByMission(SA_PARAM_NN_STR const char *pchSpecialDialogName, SA_PARAM_NN_STR  const char *pchMissionName)
{
	const char *pchReturnValue;	
	char *estrPrefix = NULL;

	estrStackCreate(&estrPrefix);
	estrPrintf(&estrPrefix, "%s/", pchMissionName);

	if (!strStartsWith(pchSpecialDialogName, estrPrefix))
	{
		char *estrDialogName = NULL;

		estrStackCreate(&estrDialogName);
		estrAppend2(&estrDialogName, estrPrefix);
		estrAppend2(&estrDialogName, pchSpecialDialogName);
		pchReturnValue = allocAddString(estrDialogName);

		estrDestroy(&estrDialogName);
	}
	else
	{
		pchReturnValue = allocAddString(pchSpecialDialogName);
	}

	estrDestroy(&estrPrefix);

	return pchReturnValue;
}

static CEDialogGroup *CECreateDialogGroup(CECallbackFunc pDocIsEditableFunc, void* pDocIsEditableData, 
										  CECallbackFunc pDialogChangedFunc, void* pDialogChangedData, 
										  MEFieldChangeCallback pFieldChangeFunc, MEFieldPreChangeCallback pFieldPreChangeFunc, void* pFieldChangeData)
{
	CEDialogGroup *pGroup;
	
	pGroup = calloc(1, sizeof(CEDialogGroup));
	pGroup->pDocIsEditableFunc = pDocIsEditableFunc;
	pGroup->pDocIsEditableData = pDocIsEditableData;
	pGroup->pDialogChangedFunc = pDialogChangedFunc;
	pGroup->pDialogChangedData = pDialogChangedData;
	pGroup->pFieldChangeFunc = pFieldChangeFunc;
	pGroup->pFieldPreChangeFunc = pFieldPreChangeFunc;
	pGroup->pFieldChangeData = pFieldChangeData;

	return pGroup;
}

static CESpecialActionGroup *CECreateSpecialActionGroup(CECommonCallbackParams *pParams)
{
	CESpecialActionGroup *pGroup;

	pGroup = calloc(1, sizeof(CESpecialActionGroup));
	pGroup->pCommonCallbackParams = pParams;

	return pGroup;
}


CESpecialDialogGroup *CECreateSpecialDialogGroup(const char* pchContact, char*** peaVarNames,
														CECallbackFunc pDocIsEditableFunc, void* pDocIsEditableData, 
														CECallbackFunc pDialogChangedFunc, void* pDialogChangedData, 
														CECallbackFunc pMessageFixupFunc, void* pMessageFixupData, 
														CESpecialDialogAccessorFunc pDialogFromWrapperFunc, ParseTable pWrapperParseTable[],
														MEFieldChangeCallback pFieldChangeFunc, MEFieldPreChangeCallback pFieldPreChangeFunc, void* pFieldChangeData)
{
	CESpecialDialogGroup *pGroup;

	pGroup = calloc(1, sizeof(CESpecialDialogGroup));
	pGroup->pCommonCallbackParams = (CECommonCallbackParams *)calloc(1, sizeof(CECommonCallbackParams));
	pGroup->pCommonCallbackParams->pDocIsEditableFunc = pDocIsEditableFunc;
	pGroup->pCommonCallbackParams->pDocIsEditableData = pDocIsEditableData;
	pGroup->pCommonCallbackParams->pDialogChangedFunc = pDialogChangedFunc;
	pGroup->pCommonCallbackParams->pDialogChangedData = pDialogChangedData;
	pGroup->pCommonCallbackParams->pMessageFixupFunc = pMessageFixupFunc;
	pGroup->pCommonCallbackParams->pMessageFixupData = pMessageFixupData;
	pGroup->pCommonCallbackParams->pSpecialDialogFromWrapperFunc = pDialogFromWrapperFunc;
	pGroup->pWrapperParseTable = pWrapperParseTable;
	pGroup->pCommonCallbackParams->pFieldChangeFunc = pFieldChangeFunc;
	pGroup->pCommonCallbackParams->pFieldPreChangeFunc = pFieldPreChangeFunc;
	pGroup->pCommonCallbackParams->pFieldChangeData = pFieldChangeData;
	pGroup->pchContactName = pchContact;
	pGroup->peaVarNames = peaVarNames;

	return pGroup;
}

CESpecialActionBlockGroup *CECreateSpecialActionBlockGroup(CECommonCallbackParams *pCommonCallbackParams) {
	CESpecialActionBlockGroup *pActionBlockGroup;

	pActionBlockGroup = (CESpecialActionBlockGroup *)calloc(1, sizeof(CESpecialActionBlockGroup));

	pActionBlockGroup->pCommonCallbackParams = pCommonCallbackParams;

	return pActionBlockGroup;
}

//This functions exactly the same as CECreateSpecialActionBlockGroup, but you can pass in the callback params individually
CESpecialActionBlockGroup *CECreateSpecialActionBlockGroupParams(const char* pchContact, char*** peaVarNames,
																CECallbackFunc pDocIsEditableFunc, void* pDocIsEditableData, 
																CECallbackFunc pDialogChangedFunc, void* pDialogChangedData, 
																CECallbackFunc pMessageFixupFunc, void* pMessageFixupData, 
																CESpecialActionBlockAccessorFunc pDialogFromWrapperFunc, ParseTable pWrapperParseTable[],
																MEFieldChangeCallback pFieldChangeFunc, MEFieldPreChangeCallback pFieldPreChangeFunc,
																void* pFieldChangeData)
{
	CECommonCallbackParams *pCommonCallbackParams = (CECommonCallbackParams *)calloc(1, sizeof(CECommonCallbackParams));
	//CESpecialActionBlockGroup *pGroup;
	pCommonCallbackParams = (CECommonCallbackParams *)calloc(1, sizeof(CECommonCallbackParams));
	pCommonCallbackParams->pDocIsEditableFunc = pDocIsEditableFunc;
	pCommonCallbackParams->pDocIsEditableData = pDocIsEditableData;
	pCommonCallbackParams->pDialogChangedFunc = pDialogChangedFunc;
	pCommonCallbackParams->pDialogChangedData = pDialogChangedData;
	pCommonCallbackParams->pMessageFixupFunc = pMessageFixupFunc;
	pCommonCallbackParams->pMessageFixupData = pMessageFixupData;
	pCommonCallbackParams->pSpecialActionBlockFromWrapperFunc = pDialogFromWrapperFunc;
	pCommonCallbackParams->pFieldChangeFunc = pFieldChangeFunc;
	pCommonCallbackParams->pFieldPreChangeFunc = pFieldPreChangeFunc;
	pCommonCallbackParams->pFieldChangeData = pFieldChangeData;

	return CECreateSpecialActionBlockGroup(pCommonCallbackParams);
}

static CELoreDialogGroup *CECreateLoreDialogGroup(ContactEditDoc *pDoc)
{
	CELoreDialogGroup *pGroup;

	pGroup = calloc(1, sizeof(CELoreDialogGroup));
	pGroup->pDoc = pDoc;

	return pGroup;
}

static void CEFreeSpecialOverrideActionGroup(CESpecialOverrideActionGroup *pGroup)
{
	// Free widgets
	ui_WidgetQueueFreeAndNull(&pGroup->pMessageLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pMessageTextEntry);
	ui_WidgetQueueFreeAndNull(&pGroup->pNextDialogLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pNextDialogTextEntry);

	// Free the group itself
	free(pGroup);
}

static void CEFreeDialogOverrideGroup(CEDialogOverrideGroup *pGroup)
{
	// Free widgets
	ui_WidgetQueueFreeAndNull(&pGroup->pMessageLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pMessageTextEntry);

	// Free the group itself
	free(pGroup);
}

static void CEFreeDialogGroup(CEDialogGroup *pGroup)
{
	// Free labels
	ui_WidgetQueueFreeAndNull(&pGroup->pExpressionLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pMessageLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pContinueMessageLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pContinueMessageDialogFormatterLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pSoundLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pPhraseLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pAnimLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pDialogFormatterLabel);

	// Free controls
	ui_WidgetQueueFreeAndNull(&pGroup->pRemoveButton);

	// Free fields
	MEFieldSafeDestroy(&pGroup->pExpressionField);
	MEFieldSafeDestroy(&pGroup->pMessageField);
	MEFieldSafeDestroy(&pGroup->pContinueMessageField);
	MEFieldSafeDestroy(&pGroup->pContinueMessageDialogFormatterField);
	MEFieldSafeDestroy(&pGroup->pAudioField);
	MEFieldSafeDestroy(&pGroup->pAnimField);
	MEFieldSafeDestroy(&pGroup->pDialogFormatterField);
	MEFieldSafeDestroy(&pGroup->pAudioPhraseField);

	// Free the group itself
	free(pGroup);
}

static void CEFreeSpecialActionGroup(CESpecialActionGroup *pGroup)
{
	// Free labels
	ui_WidgetQueueFreeAndNull(&pGroup->pTitleLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pDialogLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pCompleteLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pEndDialogLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pMessageLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pDialogFormatterLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pActionLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pExpressionLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pCanChooseExpressionLabel);

	// Free controls
	ui_WidgetQueueFreeAndNull(&pGroup->pRemoveButton);
	ui_WidgetQueueFreeAndNull(&pGroup->pActionButton);
	ui_WidgetQueueFreeAndNull(&pGroup->pUpButton);
	ui_WidgetQueueFreeAndNull(&pGroup->pDownButton);

	// Free fields
	MEFieldSafeDestroy(&pGroup->pDialogField);
	MEFieldSafeDestroy(&pGroup->pCompleteField);
	MEFieldSafeDestroy(&pGroup->pEndDialogField);
	MEFieldSafeDestroy(&pGroup->pMessageField);
	MEFieldSafeDestroy(&pGroup->pDialogFormatterField);
	MEFieldSafeDestroy(&pGroup->pExpressionField);
	MEFieldSafeDestroy(&pGroup->pCanChooseExpressionField);

	// Free the group itself
	free(pGroup);
}

void CEFreeSpecialDialogOverrideGroup(CESpecialDialogOverrideGroup *pGroup)
{
	S32 i;

	// Free widgets
	ui_WidgetQueueFreeAndNull(&pGroup->pActionsLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pSeparator);
	ui_WidgetQueueFreeAndNull(&pGroup->pOpenMissionButton);

	ui_WidgetQueueFreeAndNull(&pGroup->pInternalNameLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pInternalNameTextEntry);

	ui_WidgetQueueFreeAndNull(&pGroup->pDisplayNameLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pDisplayNameTextEntry);

	for (i = 0; i < eaSize(&pGroup->eaDialogOverrideGroups); i++) 
	{
		CEFreeDialogOverrideGroup(pGroup->eaDialogOverrideGroups[i]);
	}
	eaDestroy(&pGroup->eaDialogOverrideGroups);

	for (i = 0; i < eaSize(&pGroup->eaDialogOverrideGroups); i++) 
	{
		CEFreeSpecialOverrideActionGroup(pGroup->eaOverrideActionGroups[i]);
	}
	eaDestroy(&pGroup->eaOverrideActionGroups);

	ui_WidgetQueueFreeAndNull(&pGroup->pPane);

	// Free the group itself
	free(pGroup);
}

void CEFreeSpecialActionBlockGroup(CESpecialActionBlockGroup *pGroup){
	int i = 0;

	ui_WidgetQueueFreeAndNull(&pGroup->pRemoveButton)
	ui_WidgetQueueFreeAndNull(&pGroup->pNameLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pPane);

	MEFieldSafeDestroy(&pGroup->pNameField);

	for(i = eaSize(&pGroup->eaSpecialActionGroups)-1; i >= 0; --i) {
		CEFreeSpecialActionGroup(pGroup->eaSpecialActionGroups[i]);
	}
	eaDestroy(&pGroup->eaSpecialActionGroups);

	free(pGroup);
}

void CEFreeSpecialDialogGroup(CESpecialDialogGroup *pGroup)
{
	int i = 0;

	// Free labels
	ui_WidgetQueueFreeAndNull(&pGroup->pExpressionLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pNameLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pInternalNameLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pSortOrderLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pDisplayNameLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pDialogFormatterLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pIndicatorLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pCostumeLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pCostumeTypeLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pCritterGroupIdentifierLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pHeadshotLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pCutSceneLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pSourceTypeLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pSourceNameLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pSourceSecondaryNameLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pFlagsLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pDelayIfInCombatLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pSoundLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pAnimLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pSpecialActionBlockLabel);

	// Free controls
	ui_WidgetQueueFreeAndNull(&pGroup->pAddDialogButton);
	ui_WidgetQueueFreeAndNull(&pGroup->pCloneGroupButton);
	ui_WidgetQueueFreeAndNull(&pGroup->pRemoveButton);
	ui_WidgetQueueFreeAndNull(&pGroup->pUpButton);
	ui_WidgetQueueFreeAndNull(&pGroup->pDownButton);
	ui_WidgetQueueFreeAndNull(&pGroup->pSeparator);
	ui_WidgetQueueFreeAndNull(&pGroup->pAddActionButton);
	ui_WidgetQueueFreeAndNull(&pGroup->pInternalNameTextEntry);

	// Free fields
	MEFieldSafeDestroy(&pGroup->pExpressionField);
	MEFieldSafeDestroy(&pGroup->pDisplayNameField);
	MEFieldSafeDestroy(&pGroup->pDialogFormatterField);
	MEFieldSafeDestroy(&pGroup->pAudioField);
	MEFieldSafeDestroy(&pGroup->pAnimField);
	MEFieldSafeDestroy(&pGroup->pNameField);
	MEFieldSafeDestroy(&pGroup->pSortOrderField);
	MEFieldSafeDestroy(&pGroup->pIndicatorField);
	MEFieldSafeDestroy(&pGroup->pCostumeField);
	MEFieldSafeDestroy(&pGroup->pCostumeTypeField);
	MEFieldSafeDestroy(&pGroup->pCritterGroupTypeField);
	MEFieldSafeDestroy(&pGroup->pCritterGroupField);
	MEFieldSafeDestroy(&pGroup->pCritterMapVarField);
	MEFieldSafeDestroy(&pGroup->pCritterGroupIdentifierField);
	MEFieldSafeDestroy(&pGroup->pPetContactField);
	MEFieldSafeDestroy(&pGroup->pHeadshotStyleField);
	MEFieldSafeDestroy(&pGroup->pCutSceneField);
	MEFieldSafeDestroy(&pGroup->pSourceTypeField);
	MEFieldSafeDestroy(&pGroup->pSourceNameField);
	MEFieldSafeDestroy(&pGroup->pSourceSecondaryNameField);
	MEFieldSafeDestroy(&pGroup->pFlagsField);
	MEFieldSafeDestroy(&pGroup->pDelayIfInCombatField);
	MEFieldSafeDestroy(&pGroup->pAppendField);

	for(i=eaSize(&pGroup->eaActionGroups)-1; i>=0; --i) {
		CEFreeSpecialActionGroup(pGroup->eaActionGroups[i]);
	}
	eaDestroy(&pGroup->eaActionGroups);

	for(i=eaSize(&pGroup->eaDialogGroups)-1; i>=0; --i) {
		CEFreeDialogGroup(pGroup->eaDialogGroups[i]);
	}
	eaDestroy(&pGroup->eaDialogGroups);

	ui_WidgetQueueFreeAndNull(&pGroup->pPane);

	// Free the group itself
	free(pGroup);
}

static void CEFreeLoreDialogGroup(CELoreDialogGroup *pGroup)
{
	// Free labels
	ui_WidgetQueueFreeAndNull(&pGroup->pLoreItemLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pOptionTextLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pConditionLabel);

	// Free controls
	ui_WidgetQueueFreeAndNull(&pGroup->pButton);

	// Free fields
	MEFieldSafeDestroy(&pGroup->pLoreItemField);
	MEFieldSafeDestroy(&pGroup->pOptionTextField);
	MEFieldSafeDestroy(&pGroup->pConditionField);

	// Free the group itself
	free(pGroup);
}

static CEEndDialogAudioGroup *CECreateEndDialogAudioGroup(ContactEditDoc *pDoc)
{
	CEEndDialogAudioGroup *pGroup;

	pGroup = calloc(1, sizeof(CEEndDialogAudioGroup));
	pGroup->pDoc = pDoc;

	return pGroup;
}

static CEStoreGroup *CECreateStoreGroup(ContactEditDoc *pDoc, StoreRef*** peaStores)
{
	CEStoreGroup *pGroup;
	
	pGroup = calloc(1, sizeof(CEStoreGroup));
	pGroup->pDoc = pDoc;
	pGroup->peaStores = peaStores;

	return pGroup;
}

static CEStoreCollectionGroup *CECreateStoreCollectionGroup(ContactEditDoc *pDoc)
{
	CEStoreCollectionGroup *pGroup;

	pGroup = calloc(1, sizeof(CEStoreCollectionGroup));
	pGroup->pDoc = pDoc;

	return pGroup;
}

static CEAuctionBrokerContactDataGroup *CECreateAuctionBrokerContactDataGroup(ContactEditDoc *pDoc)
{
	CEAuctionBrokerContactDataGroup *pGroup;

	pGroup = calloc(1, sizeof(CEAuctionBrokerContactDataGroup));
	pGroup->pDoc = pDoc;

	return pGroup;
}

static CEUGCSearchAgentDataGroup *CECreateUGCSearchAgentDataGroup(ContactEditDoc *pDoc)
{
	CEUGCSearchAgentDataGroup *pGroup;

	pGroup = calloc(1, sizeof(CEUGCSearchAgentDataGroup));
	pGroup->pDoc = pDoc;

	return pGroup;
}

//Creates UI structure for new item.  Called when there are more on the contact then in the doc. 
//Refresh does must of the work.
CEImageMenuItemGroup *CECreateImageMenuItemGroup(const char* pchContact,
												 CECallbackFunc pDocIsEditableFunc, void* pDocIsEditableData,
												 CECallbackFunc pChangedFunc, void* pChangedData,
												 CECallbackFunc pMessageFixupFunc, void* pMessageFixupData,
												 CEImageMenuItemAccessorFunc pImageMenuItemFromWrapperFunc, ParseTable pWrapperParseTable[],
												 MEFieldChangeCallback pFieldChangeFunc, MEFieldPreChangeCallback pFieldPreChangeFunc, void* pFieldChangeData)
{
	
	CEImageMenuItemGroup *pGroup;

	pGroup = calloc(1, sizeof(CEImageMenuItemGroup));
	pGroup->pCommonCallbackParams = CECreateCommonCallbackParams(pDocIsEditableFunc, pDocIsEditableData,
																 pChangedFunc, pChangedData,
																 pMessageFixupFunc, pMessageFixupData,
																 NULL,
																 pFieldChangeFunc, pFieldPreChangeFunc, pFieldChangeData);
	pGroup->pImageMenuItemFromWrapperFunc = pImageMenuItemFromWrapperFunc;
	pGroup->pWrapperParseTable = pWrapperParseTable;
	
	return pGroup;
}

static void CEFreeStoreGroup(CEStoreGroup *pGroup)
{
	// Free labels
	if (pGroup->pStoreLabel) {
		ui_WidgetQueueFree(UI_WIDGET(pGroup->pStoreLabel));
	}

	// Free controls
	if (pGroup->pButton) {
		ui_WidgetQueueFree(UI_WIDGET(pGroup->pButton));
	}

	// Free fields
	if (pGroup->pStoreField) {
		MEFieldDestroy(pGroup->pStoreField);
	}

	// Free the group itself
	free(pGroup);
}

static void CEFreeStoreCollectionGroup(CEStoreCollectionGroup *pGroup)
{
	int i;

	// Free labels
	if (pGroup->pConditionLabel) {
		ui_WidgetQueueFree(UI_WIDGET(pGroup->pConditionLabel));
	}
	if (pGroup->pOptionTextLabel) {
		ui_WidgetQueueFree(UI_WIDGET(pGroup->pOptionTextLabel));
	}

	// Free controls
	if (pGroup->pAddStoreButton) {
		ui_WidgetQueueFree(UI_WIDGET(pGroup->pAddStoreButton));
	}
	if (pGroup->pRemoveCollectionButton) {
		ui_WidgetQueueFree(UI_WIDGET(pGroup->pRemoveCollectionButton));
	}

	// Free Separator
	if (pGroup->pSeparator) {
		ui_WidgetQueueFree(UI_WIDGET(pGroup->pSeparator));
	}

	// Free fields
	if (pGroup->pConditionField) {
		MEFieldDestroy(pGroup->pConditionField);
	}
	if (pGroup->pOptionTextField) {
		MEFieldDestroy(pGroup->pOptionTextField);
	}

	// Free stores
	for(i = 0; i < eaSize(&pGroup->eaStoreGroups); i++) {
		CEFreeStoreGroup(pGroup->eaStoreGroups[i]);
	}
	eaDestroy(&pGroup->eaStoreGroups);

	// Free the group itself
	free(pGroup);
}

static void CEFreeAuctionBrokerContactDataGroup(CEAuctionBrokerContactDataGroup *pGroup)
{
	// Free labels
	if (pGroup->pOptionTextLabel) 
	{
		ui_WidgetQueueFree(UI_WIDGET(pGroup->pOptionTextLabel));
	}
	if (pGroup->pAuctionBrokerDefLabel) 
	{
		ui_WidgetQueueFree(UI_WIDGET(pGroup->pAuctionBrokerDefLabel));
	}

	// Free controls
	if (pGroup->pRemoveBrokerButton) 
	{
		ui_WidgetQueueFree(UI_WIDGET(pGroup->pRemoveBrokerButton));
	}

	// Free fields
	if (pGroup->pOptionTextField) 
	{
		MEFieldDestroy(pGroup->pOptionTextField);
	}
	if (pGroup->pAuctionBrokerDefField) 
	{
		MEFieldDestroy(pGroup->pAuctionBrokerDefField);
	}

	// Free Separator
	if (pGroup->pSeparator) 
	{
		ui_WidgetQueueFree(UI_WIDGET(pGroup->pSeparator));
	}

	// Free the group itself
	free(pGroup);
}

static void CEFreeUGCSearchAgentDataGroup(CEUGCSearchAgentDataGroup *pGroup)
{
	// Free labels
	if (pGroup->pOptionTextLabel) 
		ui_WidgetQueueFree(UI_WIDGET(pGroup->pOptionTextLabel));	
	if (pGroup->pTitleLabel) 
		ui_WidgetQueueFree(UI_WIDGET(pGroup->pTitleLabel));	
	if (pGroup->pDialogTextLabel) 
		ui_WidgetQueueFree(UI_WIDGET(pGroup->pDialogTextLabel));	
	if (pGroup->pLocationLabel) 
		ui_WidgetQueueFree(UI_WIDGET(pGroup->pLocationLabel));	
	if (pGroup->pMaxDurationLabel) 
		ui_WidgetQueueFree(UI_WIDGET(pGroup->pMaxDurationLabel));	
	if (pGroup->pLastNDaysLabel) 
		ui_WidgetQueueFree(UI_WIDGET(pGroup->pLastNDaysLabel));	

	// Free fields
	if (pGroup->pOptionTextField) MEFieldDestroy(pGroup->pOptionTextField);
	if (pGroup->pTitleField) MEFieldDestroy(pGroup->pTitleField);
	if (pGroup->pDialogTextField) MEFieldDestroy(pGroup->pDialogTextField);
	if (pGroup->pLocationField) MEFieldDestroy(pGroup->pLocationField);
	if (pGroup->pMaxDurationField) MEFieldDestroy(pGroup->pMaxDurationField);
	if (pGroup->pLastNDaysField) MEFieldDestroy(pGroup->pLastNDaysField);

	// Free controls
	if (pGroup->pRemoveButton) ui_WidgetQueueFree(UI_WIDGET(pGroup->pRemoveButton));	
	// Free Separator
	if (pGroup->pSeparator) ui_WidgetQueueFree(UI_WIDGET(pGroup->pSeparator));

	// Free the group itself
	free(pGroup);
}

void CEFreeImageMenuItemGroup(CEImageMenuItemGroup *pGroup)
{
	// Free labels
	if (pGroup->pNameLabel) 
		ui_WidgetQueueFree(UI_WIDGET(pGroup->pNameLabel));	
	if (pGroup->pPosLabel) 
		ui_WidgetQueueFree(UI_WIDGET(pGroup->pPosLabel));	
	if (pGroup->pIconLabel) 
		ui_WidgetQueueFree(UI_WIDGET(pGroup->pIconLabel));	
	if (pGroup->pActionLabel) 
		ui_WidgetQueueFree(UI_WIDGET(pGroup->pActionLabel));
	if (pGroup->pVisibleConditionLabel) 
		ui_WidgetQueueFree(UI_WIDGET(pGroup->pVisibleConditionLabel));
	if (pGroup->pRequiresConditionLabel) 
		ui_WidgetQueueFree(UI_WIDGET(pGroup->pRequiresConditionLabel));
	if (pGroup->pRecommendedConditionLabel) 
		ui_WidgetQueueFree(UI_WIDGET(pGroup->pRecommendedConditionLabel));
	if (pGroup->pPosPlaceButton)
		ui_WidgetQueueFree(UI_WIDGET(pGroup->pPosPlaceButton));

	// Free fields
	if (pGroup->pNameField) MEFieldDestroy(pGroup->pNameField);
	if (pGroup->pPosXField) MEFieldDestroy(pGroup->pPosXField);
	if (pGroup->pPosYField) MEFieldDestroy(pGroup->pPosYField);
	if (pGroup->pIconField) MEFieldDestroy(pGroup->pIconField);
	if (pGroup->pVisibleConditionField) MEFieldDestroy(pGroup->pVisibleConditionField);
	if (pGroup->pRequiresConditionField) MEFieldDestroy(pGroup->pRequiresConditionField);
	if (pGroup->pRecommendedConditionField) MEFieldDestroy(pGroup->pRecommendedConditionField);

	// Free controls
	if (pGroup->pRemoveButton) ui_WidgetQueueFree(UI_WIDGET(pGroup->pRemoveButton));	
	if (pGroup->pActionButton) ui_WidgetQueueFree(UI_WIDGET(pGroup->pActionButton));	
	// Free Separator
	if (pGroup->pSeparator) ui_WidgetQueueFree(UI_WIDGET(pGroup->pSeparator));

	free(pGroup->pCommonCallbackParams);

	// Free the group itself
	free(pGroup);
}


static void CEFreeEndDialogAudioGroup(CEEndDialogAudioGroup *pGroup)
{
	// Free labels
	if (pGroup->pAudioFileLabel) {
		ui_WidgetQueueFree(UI_WIDGET(pGroup->pAudioFileLabel));
	}

	// Free controls
	if (pGroup->pButton) {
		ui_WidgetQueueFree(UI_WIDGET(pGroup->pButton));
	}

	// Free fields
	if (pGroup->pAudioFileField) {
		MEFieldDestroy(pGroup->pAudioFileField);
	}

	// Free the group itself
	free(pGroup);
}


static CEPowerStoreGroup *CECreatePowerStoreGroup(ContactEditDoc *pDoc)
{
	CEPowerStoreGroup *pGroup;

	pGroup = calloc(1, sizeof(CEPowerStoreGroup));
	pGroup->pDoc = pDoc;

	return pGroup;
}


static void CEFreePowerStoreGroup(CEPowerStoreGroup *pGroup)
{
	// Free labels
	if (pGroup->pStoreLabel) {
		ui_WidgetQueueFree(UI_WIDGET(pGroup->pStoreLabel));
	}

	// Free controls
	if (pGroup->pButton) {
		ui_WidgetQueueFree(UI_WIDGET(pGroup->pButton));
	}

	// Free fields
	if (pGroup->pStoreField) {
		MEFieldDestroy(pGroup->pStoreField);
	}

	// Free the group itself
	free(pGroup);
}


CEOfferGroup *CECreateOfferGroup(const char *pchContact,
		CECallbackFunc pDocIsEditableFunc, void* pDocIsEditableData, 
		CECallbackFunc pDialogChangedFunc, void* pDialogChangedData, 
		CECallbackFunc pMessageFixupFunc, void* pMessageFixupData, 
		CEMissionOfferAccessorFunc pOfferFromWrapperFunc, ParseTable pWrapperParseTable[],
		MEFieldChangeCallback pFieldChangeFunc, MEFieldPreChangeCallback pFieldPreChangeFunc, void* pFieldChangeData)
{
	CEOfferGroup *pGroup;
	
	pGroup = calloc(1, sizeof(CEOfferGroup));
	pGroup->pDocIsEditableFunc = pDocIsEditableFunc;
	pGroup->pDocIsEditableData = pDocIsEditableData;
	pGroup->pDialogChangedFunc = pDialogChangedFunc;
	pGroup->pDialogChangedData = pDialogChangedData;
	pGroup->pMessageFixupFunc = pMessageFixupFunc;
	pGroup->pMessageFixupData = pMessageFixupData;
	pGroup->pMissionOfferFromWrapperFunc = pOfferFromWrapperFunc;
	pGroup->pWrapperParseTable = pWrapperParseTable;
	pGroup->pFieldChangeFunc = pFieldChangeFunc;
	pGroup->pFieldPreChangeFunc = pFieldPreChangeFunc;
	pGroup->pFieldChangeData = pFieldChangeData;
	pGroup->pchContactName = pchContact;

	return pGroup;
}


void CEFreeOfferGroup(CEOfferGroup *pGroup)
{
	int i;

	// Free labels
	ui_WidgetQueueFree((UIWidget*)pGroup->pMissionLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pAllegianceLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pSpecialDialogLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pSpecialDialogInternalNameLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pRemoteLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pChoiceLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pAcceptLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pAcceptDialogFormatterLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pAcceptTargetDialogLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pDeclineLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pDeclineDialogFormatterLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pDeclineTargetDialogLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pTurnInLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pSoundLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pRewardAcceptLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pRewardAcceptDialogFormatterLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pRewardAcceptTargetDialogLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pRewardChooseLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pRewardChooseDialogFormatterLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pRewardChooseTargetDialogLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pRewardAbortLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pRewardAbortDialogFormatterLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pRewardAbortTargetDialogLabel);

	// Free controls
	ui_WidgetQueueFree((UIWidget*)pGroup->pRemoveButton);
	ui_WidgetQueueFree((UIWidget*)pGroup->pUpButton);
	ui_WidgetQueueFree((UIWidget*)pGroup->pDownButton);
	ui_WidgetQueueFree((UIWidget*)pGroup->pSeparator);
	ui_WidgetQueueFree((UIWidget*)pGroup->pSubMissionLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pSpecialDialogInternalNameTextEntry);
	ui_WidgetQueueFree((UIWidget*)pGroup->pUITypeLabel);

	// Free fields
	if (pGroup->pMissionField) {
		MEFieldDestroy(pGroup->pMissionField);
	}
	if (pGroup->pAllegianceField) {
		MEFieldDestroy(pGroup->pAllegianceField);
	}
	if (pGroup->pSpecialDialogField) {
		MEFieldDestroy(pGroup->pSpecialDialogField);
	}
	if (pGroup->pSubMissionField) {
		MEFieldDestroy(pGroup->pSubMissionField);
	}
	if (pGroup->pChoiceField) {
		MEFieldDestroy(pGroup->pChoiceField);
	}
	if (pGroup->pAcceptField) {
		MEFieldDestroy(pGroup->pAcceptField);
	}
	if (pGroup->pAcceptDialogFormatterField) {
		MEFieldDestroy(pGroup->pAcceptDialogFormatterField);
	}
	if (pGroup->pAcceptTargetDialogField) {
		MEFieldDestroy(pGroup->pAcceptTargetDialogField);
	}
	if (pGroup->pDeclineField) {
		MEFieldDestroy(pGroup->pDeclineField);
	}
	if (pGroup->pDeclineDialogFormatterField) {
		MEFieldDestroy(pGroup->pDeclineDialogFormatterField);
	}
	if (pGroup->pDeclineTargetDialogField) {
		MEFieldDestroy(pGroup->pDeclineTargetDialogField);
	}
	if (pGroup->pTurnInField) {
		MEFieldDestroy(pGroup->pTurnInField);
	}
	if (pGroup->pRemoteFlagsField) {
		MEFieldDestroy(pGroup->pRemoteFlagsField);
	}
	if (pGroup->pRewardAcceptField) 
	{
		MEFieldDestroy(pGroup->pRewardAcceptField);
	}
	if (pGroup->pRewardAcceptDialogFormatterField) 
	{
		MEFieldDestroy(pGroup->pRewardAcceptDialogFormatterField);
	}
	if (pGroup->pRewardAcceptTargetDialogField) 
	{
		MEFieldDestroy(pGroup->pRewardAcceptTargetDialogField);
	}
	if (pGroup->pRewardChooseField) 
	{
		MEFieldDestroy(pGroup->pRewardChooseField);
	}
	if (pGroup->pRewardChooseDialogFormatterField) 
	{
		MEFieldDestroy(pGroup->pRewardChooseDialogFormatterField);
	}
	if (pGroup->pRewardChooseTargetDialogField) 
	{
		MEFieldDestroy(pGroup->pRewardChooseTargetDialogField);
	}
	if (pGroup->pRewardAbortField) 
	{
		MEFieldDestroy(pGroup->pRewardAbortField);
	}
	if (pGroup->pRewardAbortDialogFormatterField) 
	{
		MEFieldDestroy(pGroup->pRewardAbortDialogFormatterField);
	}
	if (pGroup->pRewardAbortTargetDialogField) 
	{
		MEFieldDestroy(pGroup->pRewardAbortTargetDialogField);
	}
	if (pGroup->pUITypeField)
	{
		MEFieldDestroy(pGroup->pUITypeField);
	}

	// Free groups
	for(i=eaSize(&pGroup->eaDialogGroups)-1; i>=0; --i) {
		CEFreeDialogGroup(pGroup->eaDialogGroups[i]);
	}
	eaDestroy(&pGroup->eaDialogGroups);
	eaDestroyEx(&pGroup->eaSubMissionNames, NULL);

	// Free the group itself
	free(pGroup);
}

static void CEStripEmptyEndDialogAudios(EndDialogAudio ***peaEndDialogAudios)
{
	S32 i;

	// Remove the ones that have no audio name
	for(i = eaSize(peaEndDialogAudios) - 1; i >= 0; --i) 
	{
		EndDialogAudio *pAudio = (*peaEndDialogAudios)[i];
		if (pAudio == NULL || pAudio->pchAudioName == NULL || pAudio->pchAudioName[0] == '\0') 
		{
			eaRemove(peaEndDialogAudios, i);
		}
	}
}

void CEStripEmptyDialogBlocks(DialogBlock ***peaBlocks)
{
	int i;

	// Remove blocks that have no audio name and no display string
	for(i=eaSize(peaBlocks)-1; i>=0; --i) {
		DialogBlock *pBlock = (*peaBlocks)[i];
		if ((!pBlock->audioName || !pBlock->audioName[0]) &&
			(!pBlock->displayTextMesg.pEditorCopy || !pBlock->displayTextMesg.pEditorCopy->pcDefaultString || !pBlock->displayTextMesg.pEditorCopy->pcDefaultString[0]) &&
			(!pBlock->continueTextMesg.pEditorCopy || !pBlock->continueTextMesg.pEditorCopy->pcDefaultString || !pBlock->continueTextMesg.pEditorCopy->pcDefaultString[0])) {
			eaRemove(peaBlocks, i);
		}
	}
}


static void CEDestroyDialogBlocks(DialogBlock ***peaBlocks)
{
	int i;

	// Remove all block data
	for(i=eaSize(peaBlocks)-1; i>=0; --i) {
		StructDestroy(parse_DialogBlock, (*peaBlocks)[i]);
	}
	eaDestroy(peaBlocks);
}


static void CEFixupDialogBlockMessages(DialogBlock*** peaBlocks, const char* pcKey, const char* pcDesc)
{
	char keyBuf[1024];
	int i;

	for(i=eaSize(peaBlocks)-1; i>=0; --i) {
		DialogBlock* pBlock = (*peaBlocks)[i];
		if (pBlock) {
			sprintf(keyBuf, "%s.%d", pcKey, i);
			langFixupMessage(pBlock->displayTextMesg.pEditorCopy, keyBuf, pcDesc, "ContactDef");

			sprintf(keyBuf, "%s.%d.ContinueText", pcKey, i);
			langFixupMessage(pBlock->continueTextMesg.pEditorCopy, keyBuf, pcDesc, "ContactDef");

			// Also clean expression
			exprClean(pBlock->condition);
		}
	}
}

static void CEFixupSpecialDialogBlockMessages(SpecialDialogBlock*** peaBlocks, const char* pcKey, const char* pcDesc, const char* pcNameKey, const char* pcNameDesc, const char* pcActionDesc)
{
	int i;

	for(i=eaSize(peaBlocks)-1; i>=0; --i) {
		CEFixupSpecialDialogMessages((*peaBlocks)[i], pcKey, pcDesc, pcNameKey, pcNameDesc, pcActionDesc, i);
	}
}

static void CEFixupSpecialActionBlockMessages(SpecialActionBlock ***peaBlocks, const char *pchNameKey){
	int i;

	for(i = eaSize(peaBlocks) - 1; i >= 0; --i) {
		CEFixupActionBlockMessages((*peaBlocks)[i], pchNameKey, i);
	}
}

void CEFixupSpecialDialogMessages(SpecialDialogBlock* pBlock, const char* pcKey, const char* pcDesc, const char* pcNameKey, const char* pcNameDesc, const char* pcActionDesc, int iIndex)
{
	char keyBuf[1024];
	int j,k;

	if (pBlock) {
		if(pBlock->dialogBlock) {
			for(k = eaSize(&pBlock->dialogBlock)-1; k>=0; --k)
			{
				sprintf(keyBuf, "%s.%d.%d", pcKey, iIndex, k);
				langFixupMessage(pBlock->dialogBlock[k]->displayTextMesg.pEditorCopy, keyBuf, pcDesc, "ContactDef");

				sprintf(keyBuf, "%s.%d.%d.ContinueText", pcKey, iIndex, k);
				langFixupMessage(pBlock->dialogBlock[k]->continueTextMesg.pEditorCopy, keyBuf, pcDesc, "ContactDef");

				// Also clean expression
				exprClean(pBlock->dialogBlock[k]->condition);
			}
		}
		sprintf(keyBuf, "%s.%d", pcNameKey, iIndex);
		langFixupMessage(pBlock->displayNameMesg.pEditorCopy, keyBuf, pcNameDesc, "ContactDef");

		for(j=eaSize(&pBlock->dialogActions)-1; j>=0; --j) {
			SpecialDialogAction *pAction = pBlock->dialogActions[j];
			sprintf(keyBuf, "%s.%d.%d", pcNameKey, iIndex, j);
			langFixupMessage(pAction->displayNameMesg.pEditorCopy, keyBuf, pcActionDesc, "ContactDef");

			gameaction_FixupMessageList(&pAction->actionBlock.eaActions, "ContactDef", keyBuf, j);

			// Clean expression
			exprClean(pBlock->dialogActions[j]->condition);
			exprClean(pBlock->dialogActions[j]->canChooseCondition);
		}
	}
}

void CEFixupActionBlockMessages(SpecialActionBlock *pBlock, const char* pchNameKey, int iIndex) {
	char keyBuf[1024];
	int j;

	if(pBlock) {
		for(j = eaSize(&pBlock->dialogActions) - 1; j >= 0; --j) {
			SpecialDialogAction *pAction = pBlock->dialogActions[j];
			sprintf(keyBuf, "%s.%d.%d", pchNameKey, iIndex, j);
			langFixupMessage(pAction->displayNameMesg.pEditorCopy, keyBuf, "Description for contact special dialog action.", "ContactDef");

			gameaction_FixupMessageList(&pAction->actionBlock.eaActions, "ContactDef", keyBuf, j);

			// Clean expression
			exprClean(pBlock->dialogActions[j]->condition);
			exprClean(pBlock->dialogActions[j]->canChooseCondition);
		}
	}
}


void CEFixupOfferMessages(SA_PARAM_NN_STR const char *pchBaseKey, SA_PARAM_NN_STR const char *pchContactName, SA_PARAM_NN_VALID ContactMissionOffer *pOffer, int index)
{
	char buf1[1024];
	char buf2[1024];
	const char* missionName;

	if (GET_REF(pOffer->missionDef)) {
		missionName = GET_REF(pOffer->missionDef)->name;
	} else {
		missionName = "NoMission";
	}

	sprintf(buf1, "%s.%s.MissionOffer.%d.AcceptString", pchBaseKey, pchContactName, index);
	sprintf(buf2, "Player's response when accepting mission %s from contact %s", missionName, pchContactName);
	langFixupMessage(pOffer->acceptStringMesg.pEditorCopy, buf1, buf2, "ContactDef.MissionOffer");

	sprintf(buf1, "%s.%s.MissionOffer.%d.DeclineString", pchBaseKey, pchContactName, index);
	sprintf(buf2, "Player's response when declining mission %s from contact %s", missionName, pchContactName);
	langFixupMessage(pOffer->declineStringMesg.pEditorCopy, buf1, buf2, "ContactDef.MissionOffer");

	sprintf(buf1, "%s.%s.MissionOffer.%d.TurnInString", pchBaseKey, pchContactName, index);
	sprintf(buf2, "Player's response when turning in mission %s to contact %s", missionName, pchContactName);
	langFixupMessage(pOffer->turnInStringMesg.pEditorCopy, buf1, buf2, "ContactDef.MissionOffer");

	sprintf(buf1, "%s.%s.MissionOffer.%d.RewardAcceptString", pchBaseKey, pchContactName, index);
	sprintf(buf2, "Player's response when completing a mission without a choosable reward %s from contact %s", missionName, pchContactName);
	langFixupMessage(pOffer->rewardAcceptMesg.pEditorCopy, buf1, buf2, "ContactDef.MissionOffer");

	sprintf(buf1, "%s.%s.MissionOffer.%d.RewardChooseString", pchBaseKey, pchContactName, index);
	sprintf(buf2, "Player's response when completing a mission with a choosable reward %s from contact %s", missionName, pchContactName);
	langFixupMessage(pOffer->rewardChooseMesg.pEditorCopy, buf1, buf2, "ContactDef.MissionOffer");

	sprintf(buf1, "%s.%s.MissionOffer.%d.RewardAbortString", pchBaseKey, pchContactName, index);
	sprintf(buf2, "Player's response to back out of completing the mission %s", missionName);
	langFixupMessage(pOffer->rewardAbortMesg.pEditorCopy, buf1, buf2, "ContactDef.MissionOffer");

	sprintf(buf1, "%s.%s.MissionOffer.%d.CompletedDialog", pchBaseKey, pchContactName, index);
	sprintf(buf2, "Contact %s's dialog when player returns after completing mission %s", pchContactName, missionName);
	CEFixupDialogBlockMessages(&pOffer->completedDialog, buf1, buf2);

	sprintf(buf1, "%s.%s.MissionOffer.%d.FailureDialog", pchBaseKey, pchContactName, index);
	sprintf(buf2, "Contact %s's dialog when player returns after failing mission %s", pchContactName, missionName);
	CEFixupDialogBlockMessages(&pOffer->failureDialog, buf1, buf2);

	sprintf(buf1, "%s.%s.MissionOffer.%d.InProgress", pchBaseKey, pchContactName, index);
	sprintf(buf2, "Contact %s's dialog if player returns while mission %s is still in progress", pchContactName, missionName);
	CEFixupDialogBlockMessages(&pOffer->inProgressDialog, buf1, buf2);

	sprintf(buf1, "%s.%s.MissionOffer.%d.Offer", pchBaseKey, pchContactName, index);
	sprintf(buf2, "Contact %s's dialog when offering mission %s to the player", pchContactName, missionName);
	CEFixupDialogBlockMessages(&pOffer->offerDialog, buf1, buf2);

	sprintf(buf1, "%s.%s.MissionOffer.%d.GreetingDialog", pchBaseKey, pchContactName, index);
	sprintf(buf2, "Contact %s's dialog when greeting the player while mission %s is still in progress regardless of contact's recent status", pchContactName, missionName);
	CEFixupDialogBlockMessages(&pOffer->greetingDialog, buf1, buf2);
}

void CEFixupImageMenuItemMessages(const char* baseMessageKey, int i, ContactImageMenuItem* pImageMenuItemData )
{
	if (pImageMenuItemData)
	{
		char buf1[1024];
		char buf2[1024];
		
		sprintf(buf1, "%s.ImageMenuData.%d", baseMessageKey, i);
		sprintf(buf2, "Name of this option in the menu");
		langFixupMessage(pImageMenuItemData->name.pEditorCopy, buf1, buf2, "ContactDef");
		exprClean(pImageMenuItemData->visibleCondition);
		exprClean(pImageMenuItemData->requiresCondition);
		exprClean(pImageMenuItemData->recommendedCondition);
	}
}


static bool CEFixupMessages(ContactDef *pContact)
{
	char buf1[1024];
	char buf2[1024];
	char buf3[1024];
	char buf4[1024];
	char buf5[1024];
	char nameSpace[RESOURCE_NAME_MAX_SIZE];
	char baseObjectName[RESOURCE_NAME_MAX_SIZE];
	char baseMessageKey[RESOURCE_NAME_MAX_SIZE];

	int i;
	if (resExtractNameSpace(pContact->name, nameSpace, baseObjectName))
	{
		sprintf(baseMessageKey, "%s:ContactDef.%s", nameSpace, baseObjectName);
	}
	else
	{
		sprintf(baseMessageKey, "ContactDef.%s", pContact->name);
	}
	
	sprintf(buf1, "%s.InfoString", baseMessageKey);
	sprintf(buf2, "Background information about the contact %s", pContact->name);
	langFixupMessage(pContact->infoStringMsg.pEditorCopy, buf1, buf2, "ContactDef");

	sprintf(buf1, "%s.DisplayName", baseMessageKey);
	sprintf(buf2, "Name of this Contact");
	langFixupMessage(pContact->displayNameMsg.pEditorCopy, buf1, buf2, "ContactDef");

	sprintf(buf1, "%s.DialogExitTextOverride", baseMessageKey);
	sprintf(buf2, "This text overrides the game's default exit dialog text.");
	langFixupMessage(pContact->dialogExitTextOverrideMsg.pEditorCopy, buf1, buf2, "ContactDef");

	sprintf(buf1, "%s.MissionSearchString", baseMessageKey);
	sprintf(buf2, "Text to display when using this contact to search for more missions for the player");
	langFixupMessage(pContact->missionSearchStringMsg.pEditorCopy, buf1, buf2, "ContactDef");

	// Mission offers
	for(i=eaSize(&pContact->offerList)-1; i>=0; --i) {
		ContactMissionOffer* pOffer = pContact->offerList[i];
		CEFixupOfferMessages("ContactDef", pContact->name, pOffer, i);
	}

	sprintf(buf1, "%s.ExitDialog", baseMessageKey);
	sprintf(buf2, "Contact %s's dialog when the player exits the conversation", pContact->name);
	CEFixupDialogBlockMessages(&pContact->exitDialog, buf1, buf2);

	sprintf(buf1, "%s.GeneralCallout", baseMessageKey);
	sprintf(buf2, "Contact %s will periodically say this in a text bubble", pContact->name);
	CEFixupDialogBlockMessages(&pContact->generalCallout, buf1, buf2);

	sprintf(buf1, "%s.GreetingDialog", baseMessageKey);
	sprintf(buf2, "Contact %s's dialog when the player starts a conversation", pContact->name);
	CEFixupDialogBlockMessages(&pContact->greetingDialog, buf1, buf2);

	sprintf(buf1, "%s.InfoDialog", baseMessageKey);
	sprintf(buf2, "Contact %s's dialog when the player asks them about themselves", pContact->name);
	CEFixupDialogBlockMessages(&pContact->infoDialog, buf1, buf2);

	sprintf(buf1, "%s.DefaultDialog", baseMessageKey);
	sprintf(buf2, "Contact %s's default dialog if they have nothing else to say", pContact->name);
	CEFixupDialogBlockMessages(&pContact->defaultDialog, buf1, buf2);

	sprintf(buf1, "%s.MissionCallout", baseMessageKey);
	sprintf(buf2, "Contact %s's text bubble callout when they have a mission for the player", pContact->name);
	CEFixupDialogBlockMessages(&pContact->missionCallout, buf1, buf2);

	sprintf(buf1, "%s.MissionExitDialog", baseMessageKey);
	sprintf(buf2, "Contact %s's dialog when the player exits the conversation after accepting a mission from the contact", pContact->name);
	CEFixupDialogBlockMessages(&pContact->missionExitDialog, buf1, buf2);

	sprintf(buf1, "%s.MissionListDialog", baseMessageKey);
	sprintf(buf2, "Contact %s's dialog when displaying the list of available missions to the player", pContact->name);
	CEFixupDialogBlockMessages(&pContact->missionListDialog, buf1, buf2);

	sprintf(buf1, "%s.NoMissionsDialog", baseMessageKey);
	sprintf(buf2, "Contact %s's dialog when the player asks to see available missions but the contact has no missions to offer", pContact->name);
	CEFixupDialogBlockMessages(&pContact->noMissionsDialog, buf1, buf2);

	sprintf(buf1, "%s.RangeCallout", baseMessageKey);
	sprintf(buf2, "Contact %s's dialog when they send a message to a player (via cell phone or mail)", pContact->name);
	CEFixupDialogBlockMessages(&pContact->rangeCallout, buf1, buf2);

	sprintf(buf1, "%s.MissionSearchDialog", baseMessageKey);
	sprintf(buf2, "Text to display when viewing the results of a Mission Search");
	CEFixupDialogBlockMessages(&pContact->eaMissionSearchDialog, buf1, buf2);

	sprintf(buf1, "%s.SpecialDialog", baseMessageKey);
	sprintf(buf2, "Mission-specific dialog for contact %s", pContact->name);
	sprintf(buf3, "%s.SpecialDialogName", baseMessageKey);
	sprintf(buf4, "Command to view mission-specific dialog for contact %s", pContact->name);
	sprintf(buf5, "Special dialog action text for contact %s", pContact->name);
	CEFixupSpecialDialogBlockMessages(&pContact->specialDialog, buf1, buf2, buf3, buf4, buf5);

	sprintf(buf1, "%s.SpecialActionBlock", baseMessageKey);
	CEFixupSpecialActionBlockMessages(&pContact->specialActions, buf1);

	sprintf(buf1, "%s.BuyOption", baseMessageKey);
	sprintf(buf2, "Text to display for the option to display a list of buyable items from contact %s", pContact->name);
	langFixupMessage(pContact->buyOptionMsg.pEditorCopy, buf1, buf2, "ContactDef");

	sprintf(buf1, "%s.SellOption", baseMessageKey);
	sprintf(buf2, "Text to display for the option to display a list of items sellable to contact %s", pContact->name);
	langFixupMessage(pContact->sellOptionMsg.pEditorCopy, buf1, buf2, "ContactDef");

	sprintf(buf1, "%s.BuyBackOption", baseMessageKey);
	sprintf(buf2, "Text to display for the option to display a list of items which may be bought back from contact %s", pContact->name);
	langFixupMessage(pContact->buyBackOptionMsg.pEditorCopy, buf1, buf2, "ContactDef");

	sprintf(buf1, "%s.NoStoreItemsDialog", baseMessageKey);
	sprintf(buf2, "Contact %s's dialog when the player asks to see a store, but no items are available to buy or sell in the store", pContact->name);
	CEFixupDialogBlockMessages(&pContact->noStoreItemsDialog, buf1, buf2);

	// Lore Dialogs
	for(i=eaSize(&pContact->eaLoreDialogs)-1; i>=0; --i) {
		ContactLoreDialog* pLoreDialog = pContact->eaLoreDialogs[i];
		if (pLoreDialog){
			ItemDef *pItemDef = GET_REF(pLoreDialog->hLoreItemDef);
			sprintf(buf1, "%s.LoreDialog.%d", baseMessageKey, i);
			sprintf(buf2, "Command to view a Lore dialog on the Contact");
			langFixupMessage(pLoreDialog->optionText.pEditorCopy, buf1, buf2, "ContactDef");
			exprClean(pLoreDialog->pCondition);
		}
	}

	// Store Collections
	for(i=eaSize(&pContact->storeCollections)-1; i>=0; --i) {
		StoreCollection* pStoreCollection = pContact->storeCollections[i];
		if (pStoreCollection){
			sprintf(buf1, "%s.StoreCollection.%d", baseMessageKey, i);
			sprintf(buf2, "Command to view a Store Collection on the Contact");
			langFixupMessage(pStoreCollection->optionText.pEditorCopy, buf1, buf2, "ContactDef");
			exprClean(pStoreCollection->pCondition);
		}
	}

	// Auction broker options
	for (i = eaSize(&pContact->ppAuctionBrokerOptionList)-1; i >= 0; --i) 
	{
		AuctionBrokerContactData* pAuctionBrokerContactData = pContact->ppAuctionBrokerOptionList[i];
		if (pAuctionBrokerContactData)
		{
			sprintf(buf1, "%s.AuctionBrokerContactData.%d", baseMessageKey, i);
			sprintf(buf2, "Command to view an auction broker on the Contact");
			langFixupMessage(pAuctionBrokerContactData->optionText.pEditorCopy, buf1, buf2, "ContactDef");
		}
	}

	// UGCSearch options
	for (i = eaSize(&pContact->ppUGCSearchAgentOptionList)-1; i >= 0; --i) 
	{
		UGCSearchAgentData* pUGCSearchAgentData = pContact->ppUGCSearchAgentOptionList[i];
		if (pUGCSearchAgentData)
		{
			sprintf(buf1, "%s.UGCSearchAgentData.%d", baseMessageKey, i);
			sprintf(buf2, "Command to view a UGC Search Agent on the Contact");
			langFixupMessage(pUGCSearchAgentData->optionText.pEditorCopy, buf1, buf2, "ContactDef");

			sprintf(buf1, "%s.UGCSearchAgentDialogTitle.%d", baseMessageKey, i);
			sprintf(buf2, "Window title of mission search result");
			langFixupMessage(pUGCSearchAgentData->dialogTitle.pEditorCopy, buf1, buf2, "ContactDef");

			sprintf(buf1, "%s.UGCSearchAgentDialogText.%d", baseMessageKey, i);
			sprintf(buf2, "Dialog given with mission search result");
			langFixupMessage(pUGCSearchAgentData->dialogText.pEditorCopy, buf1, buf2, "ContactDef");
		}
	}

	// Image Menu options

	if(pContact->pImageMenuData){
		sprintf(buf1, "%s.ImageMenuData", baseMessageKey);
		sprintf(buf2, "Title of menu");
		langFixupMessage(pContact->pImageMenuData->title.pEditorCopy, buf1, buf2, "ContactDef");
		for (i = eaSize(&pContact->pImageMenuData->items)-1; i >= 0; --i) 
		{
			ContactImageMenuItem* pImageMenuItemData = pContact->pImageMenuData->items[i];
			CEFixupImageMenuItemMessages(baseMessageKey, i, pImageMenuItemData);
		}
	}
	return true;
}

static bool CEFixupMessagesWrapper(ContactEditDoc *pDoc)
{
	return CEFixupMessages(pDoc->pContact);
}

static void CEFixupExpressions(ContactDef *pContact)
{
	int i;

	// Fix main expressions
	exprClean(pContact->interactReqs);
	exprClean(pContact->canAccessRemotely);

	// Fix Special Dialog expressions
	for(i=eaSize(&pContact->specialDialog)-1; i>=0; i--)
	{
 		if(pContact->specialDialog[i]->pCondition)
			exprClean(pContact->specialDialog[i]->pCondition);
	}

	// Note: DialogBlock expressions are cleaned in message fixup functions
}


//---------------------------------------------------------------------------------------------------
// UI Logic
//---------------------------------------------------------------------------------------------------


static void CEAddFieldToParent(MEField *pField, UIWidget *pParent, F32 x, F32 y, F32 xPercent, F32 w, UIUnitType wUnit, F32 padRight, ContactEditDoc *pDoc)
{
	MEFieldAddToParent(pField, pParent, x, y);
	ui_WidgetSetPositionEx(pField->pUIWidget, x, y, xPercent, 0, UITopLeft);
	ui_WidgetSetWidthEx(pField->pUIWidget, w, wUnit);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, padRight, 0, 0);
	MEFieldSetChangeCallback(pField, CEFieldChangedCB, pDoc);
	MEFieldSetPreChangeCallback(pField, CEFieldPreChangeCB, pDoc);
}

static SpecialDialogBlock* CESpecialDialogBlockFromGroup(CESpecialDialogGroup *pGroup)
{
	if(pGroup && pGroup->peaSpecialDialogWrappers)
	{
		if(!pGroup->pCommonCallbackParams->pSpecialDialogFromWrapperFunc && pGroup->pWrapperParseTable == parse_SpecialDialogBlock)
		{
			return ((SpecialDialogBlock*) eaGet(pGroup->peaSpecialDialogWrappers, pGroup->index));
		} 
		else if(pGroup->pCommonCallbackParams->pSpecialDialogFromWrapperFunc)
		{
			void* pWrapper = eaGet(pGroup->peaSpecialDialogWrappers, pGroup->index);
			if(pWrapper)
				return pGroup->pCommonCallbackParams->pSpecialDialogFromWrapperFunc(pWrapper);
		}
	}

	return NULL;
}

static SpecialActionBlock* CESpecialActionBlockFromGroup(CESpecialActionBlockGroup *pGroup)
{
	if(pGroup && pGroup->peaSpecialActionBlocks)
	{
		return eaGet(pGroup->peaSpecialActionBlocks, pGroup->index);
	}
	if(pGroup && pGroup->peaSpecialActionBlockWrappers)
	{
		if(!pGroup->pCommonCallbackParams->pSpecialActionBlockFromWrapperFunc)
		{
			return ((SpecialActionBlock*) eaGet(pGroup->peaSpecialActionBlockWrappers, pGroup->index));
		} 
		else if(pGroup->pCommonCallbackParams->pSpecialActionBlockFromWrapperFunc)
		{
			void* pWrapper = eaGet(pGroup->peaSpecialActionBlockWrappers, pGroup->index);
			if(pWrapper)
				return pGroup->pCommonCallbackParams->pSpecialActionBlockFromWrapperFunc(pWrapper);
		}
	}

	return NULL;
}


static ContactMissionOffer* CEContactMissionOfferFromGroup(CEOfferGroup *pGroup)
{
	if(pGroup && pGroup->peaMissionOfferWrappers)
	{
		if(!pGroup->pMissionOfferFromWrapperFunc && pGroup->pWrapperParseTable == parse_SpecialDialogBlock)
		{
			return ((ContactMissionOffer*) eaGet(pGroup->peaMissionOfferWrappers, pGroup->index));
		} 
		else if(pGroup->pMissionOfferFromWrapperFunc)
		{
			void* pWrapper = eaGet(pGroup->peaMissionOfferWrappers, pGroup->index);
			if(pWrapper)
				return pGroup->pMissionOfferFromWrapperFunc(pWrapper);
		}
	}

	return NULL;
}

static CECommonCallbackParams *CECreateCommonCallbackParams(CECallbackFunc pDocIsEditableFunc, void* pDocIsEditableData,
	CECallbackFunc pDialogChangedFunc, void* pDialogChangedData, CECallbackFunc pMessageFixupFunc,
	void* pMessageFixupData, CESpecialDialogAccessorFunc pDialogFromWrapperFunc,
	MEFieldChangeCallback pFieldChangeFunc, MEFieldPreChangeCallback pFieldPreChangeFunc,
	void* pFieldChangeData) {

		CECommonCallbackParams *pParams = NULL;
		pParams = (CECommonCallbackParams *)calloc(1,sizeof(CECommonCallbackParams));

		pParams->pDocIsEditableFunc = pDocIsEditableFunc;
		pParams->pDocIsEditableData = pDocIsEditableData;
		pParams->pDialogChangedFunc = pDialogChangedFunc;
		pParams->pDialogChangedData = pDialogChangedData;
		pParams->pMessageFixupFunc = pMessageFixupFunc;
		pParams->pMessageFixupData = pMessageFixupData;
		pParams->pSpecialDialogFromWrapperFunc = pDialogFromWrapperFunc;
		pParams->pFieldChangeFunc = pFieldChangeFunc;
		pParams->pFieldPreChangeFunc = pFieldPreChangeFunc;
		pParams->pFieldChangeData = pFieldChangeData;

		return pParams;
}


static void CEAddDialogCB(UIButton *pButton, CEDialogGroup *pGroup)
{
	// Make sure the resource is checked out of Gimme
	if (pGroup->pDocIsEditableFunc && !pGroup->pDocIsEditableFunc(pGroup->pDocIsEditableData)) {
		return;
	}

	// Add a new group
	eaPush(pGroup->peaBlocks, StructCreate(parse_DialogBlock));

	// Update the UI
	if(pGroup->pDialogChangedFunc)
		pGroup->pDialogChangedFunc(pGroup->pDialogChangedData);
}


static void CERemoveDialogCB(UIButton *pButton, CEDialogGroup *pGroup)
{
	// Make sure the resource is checked out of Gimme
	if (pGroup->pDocIsEditableFunc && !pGroup->pDocIsEditableFunc(pGroup->pDocIsEditableData)) {
		return;
	}

	// Remove the group
	StructDestroy(parse_DialogBlock, (*pGroup->peaBlocks)[pGroup->index]);
	eaRemove(pGroup->peaBlocks, pGroup->index);

	// Update the UI
	if(pGroup->pDialogChangedFunc)
		pGroup->pDialogChangedFunc(pGroup->pDialogChangedData);
}

static void CEAddSpecialActionToBlockCB(UIButton *pButton, CESpecialActionBlockGroup *pGroup){
	SpecialDialogAction *pNewSpecialAction;
	SpecialActionBlock *pBlock = NULL;

	if(pGroup == NULL) {
		return;
	}

	// Make sure the resource is checked out of Gimme
	if(pGroup->pCommonCallbackParams->pDocIsEditableFunc && !pGroup->pCommonCallbackParams->pDocIsEditableFunc(pGroup->pCommonCallbackParams->pDocIsEditableData)) {
		return;
	}

	pBlock = CESpecialActionBlockFromGroup(pGroup);

	if(pBlock == NULL){
		return;
	}

	pNewSpecialAction = StructCreate(parse_SpecialDialogAction);
	pNewSpecialAction->bSendComplete = true;

	//Add the new group
	eaPush(&pBlock->dialogActions, pNewSpecialAction);

	// Update the UI
	if(pGroup->pCommonCallbackParams->pDialogChangedFunc)
		pGroup->pCommonCallbackParams->pDialogChangedFunc(pGroup->pCommonCallbackParams->pDialogChangedData);
}


static void CEAddSpecialActionCB(UIButton *pButton, CESpecialDialogGroup *pGroup)
{
	SpecialDialogAction *pNewSpecialAction;
	SpecialDialogBlock *pBlock = NULL;

	if(!pGroup)
		return;

	// Make sure the resource is checked out of Gimme
	if(pGroup->pCommonCallbackParams->pDocIsEditableFunc && !pGroup->pCommonCallbackParams->pDocIsEditableFunc(pGroup->pCommonCallbackParams->pDocIsEditableData)) {
		return;
	}

	// Get special dialog block
	pBlock = CESpecialDialogBlockFromGroup(pGroup);

	if(!pBlock)
		return;

	// Create a new special action
	pNewSpecialAction = StructCreate(parse_SpecialDialogAction);
	pNewSpecialAction->bSendComplete = true;

	// Add the new group
	eaPush(&pBlock->dialogActions, pNewSpecialAction);

	// Update the UI
	if(pGroup->pCommonCallbackParams->pDialogChangedFunc)
		pGroup->pCommonCallbackParams->pDialogChangedFunc(pGroup->pCommonCallbackParams->pDialogChangedData);
}

// Creates a new special dialog block and adds it to the contact definition
static SpecialDialogBlock * CEAddSpecialDialogBlock(SA_PARAM_NN_VALID DialogFlowWindowInfo *pInfo)
{
	SpecialDialogBlock * newSpecialDialog = NULL;

	if (pInfo)
	{
		static char nextName[512];
		int counter = 1;

		// Create a new special dialog with a unique name
		newSpecialDialog = StructCreate(parse_SpecialDialogBlock);
		newSpecialDialog->bDelayIfInCombat = true;

		strcpy(nextName, "SpecialDialog");
		while (CEDialogFlowGetSpecialDialogByName(pInfo, nextName, pInfo->pchContactName))
		{
			sprintf(nextName, "%s%i", "SpecialDialog", counter);
			counter++;
		}
		newSpecialDialog->name = (char*)allocAddString(nextName);

		//Must have at least 1 dialog block
		eaCreate(&newSpecialDialog->dialogBlock);
		eaPush(&newSpecialDialog->dialogBlock, StructCreate(parse_DialogBlock));

		//Set the UsesLocalCondExpression flag so that it doesn't get fixed up unnecessarily
		newSpecialDialog->bUsesLocalCondExpression = true;

		// Add the new group
		CEDialogFlowWindowAddSpecialDialogToDocument(pInfo, newSpecialDialog);
	}

	return newSpecialDialog;
}

static SpecialActionBlock *CEAddSpecialActionBlock(SA_PARAM_NN_VALID DialogFlowWindowInfo *pInfo){
	SpecialActionBlock *pNewSpecialActionBlock = NULL;

	if(pInfo) {
		ContactDef *pContactDef = pInfo->pContactDoc ? pInfo->pContactDoc->pContact : (ContactDef *)RefSystem_ReferentFromString(g_ContactDictionary, pInfo->pchContactName);
		static char nextName[512];
		int counter = 1;

		// Create a new special action block with a unique name
		pNewSpecialActionBlock = StructCreate(parse_SpecialActionBlock);

		strcpy(nextName, "SpecialActionBlock");
		while (CEDialogFlowGetSpecialActionBlockByName(pInfo, nextName, pInfo->pchContactName) != NULL)
		{
			sprintf(nextName, "%s%i", "SpecialActionBlock", counter);
			counter++;
		}
		pNewSpecialActionBlock->name = (char*)allocAddString(nextName);

		//Must have at least 1 dialog block
		eaCreate(&pNewSpecialActionBlock->dialogActions);

		eaPush(&pNewSpecialActionBlock->dialogActions, StructCreate(parse_SpecialDialogAction));
		
		pNewSpecialActionBlock->dialogActions[0]->bSendComplete = true;

		

		// Add the new group
		//TODO Fix for dialog flow
		CEDialogFlowWindowAddSpecialActionBlockToDocument(pInfo, pNewSpecialActionBlock);
	}

	return pNewSpecialActionBlock;
}

static void CEAddSpecialCB(UIButton *pButton, ContactEditDoc *pDoc)
{
	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pDoc->emDoc, true)) {
		return;
	}

	// Add the special dialog
	CEAddSpecialDialogBlock(pDoc->pDialogFlowWindowInfo);

	// Update the UI
	CEContactChanged(pDoc, true);
}

static void CEAddSpecialActionBlockCB(UIButton *pButton, ContactEditDoc *pDoc) {
	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pDoc->emDoc, true)) {
		return;
	}

	//Add the special action block
	CEAddSpecialActionBlock(pDoc->pDialogFlowWindowInfo);

	//Update the UI
	CEContactChanged(pDoc, true);
}


static void CERemoveSpecialActionCB(UIButton *pButton, CESpecialActionGroup *pGroup)
{
	if(!pGroup || !pGroup->pCommonCallbackParams)
		return;

	// Make sure the resource is checked out of Gimme
	if (pGroup->pCommonCallbackParams->pDocIsEditableFunc && !pGroup->pCommonCallbackParams->pDocIsEditableFunc(pGroup->pCommonCallbackParams->pDocIsEditableData)) {
		return;
	}

	// Remove the group
	if(eaSize(pGroup->peaActions) > 0) {
		StructDestroy(parse_SpecialDialogAction, (*pGroup->peaActions)[pGroup->index]);
		eaRemove(pGroup->peaActions, pGroup->index);
	}

	// Update the UI
	if(pGroup->pCommonCallbackParams->pDialogChangedFunc)
		pGroup->pCommonCallbackParams->pDialogChangedFunc(pGroup->pCommonCallbackParams->pDialogChangedData);
}

static void CERemoveSpecialActionBlockCB(UIButton *pButton, CESpecialActionBlockGroup *pGroup)
{
	if(!pGroup || !pGroup->pCommonCallbackParams)
		return;

	// Make sure the resource is checked out of Gimme
	if (pGroup->pCommonCallbackParams->pDocIsEditableFunc && !pGroup->pCommonCallbackParams->pDocIsEditableFunc(pGroup->pCommonCallbackParams->pDocIsEditableData)) {
		return;
	}

	// Remove the group
	if(pGroup->peaSpecialActionBlocks && eaSize(pGroup->peaSpecialActionBlocks) > 0) {
		StructDestroy(parse_SpecialActionBlock, (*pGroup->peaSpecialActionBlocks)[pGroup->index]);
		eaRemove(pGroup->peaSpecialActionBlocks, pGroup->index);
	}
	if(pGroup->peaSpecialActionBlockWrappers && eaSize(pGroup->peaSpecialActionBlockWrappers) > 0) {
		void* pWrapper = eaRemove(pGroup->peaSpecialActionBlockWrappers, pGroup->index);
	}

	// Update the UI
	if(pGroup->pCommonCallbackParams->pDialogChangedFunc)
		pGroup->pCommonCallbackParams->pDialogChangedFunc(pGroup->pCommonCallbackParams->pDialogChangedData);
}

void CERemoveSpecialDialogGroup(SA_PARAM_NN_VALID CESpecialDialogGroup *pGroup)
{
	if (pGroup)
	{
		// Remove the group
		if(pGroup->pWrapperParseTable && pGroup->peaSpecialDialogWrappers)
		{
			void* pWrapper = eaRemove(pGroup->peaSpecialDialogWrappers, pGroup->index);
			if(pWrapper)
			{
				StructDestroyVoid(pGroup->pWrapperParseTable, pWrapper);
			}
		}
	}
}


static void CERemoveSpecialCB(UIButton *pButton, CESpecialDialogGroup *pGroup)
{
	// Make sure the resource is checked out of Gimme
	if (pGroup->pCommonCallbackParams->pDocIsEditableFunc && !pGroup->pCommonCallbackParams->pDocIsEditableFunc(pGroup->pCommonCallbackParams->pDocIsEditableData)) {
		return;
	}

	// Remove the group
	if(pGroup->pWrapperParseTable && pGroup->peaSpecialDialogWrappers)
	{
		void* pWrapper = eaRemove(pGroup->peaSpecialDialogWrappers, pGroup->index);
		if(pWrapper)
		{
			StructDestroyVoid(pGroup->pWrapperParseTable, pWrapper);
		}
	}

	// Update the UI
	if(pGroup->pCommonCallbackParams->pDialogChangedFunc)
		pGroup->pCommonCallbackParams->pDialogChangedFunc(pGroup->pCommonCallbackParams->pDialogChangedData);
}

static void CEAddLoreDialogCB(UIButton *pButton, ContactEditDoc *pDoc)
{
	ContactDef* pContactDef = pDoc->pContact;
	ContactLoreDialog* newDialog = NULL;
	static char nextName[512];
	int counter = 1;

	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pDoc->emDoc, true)) {
		return;
	}

	// Create a new special dialog with a unique name
	newDialog = StructCreate(parse_ContactLoreDialog);

	// Add the new group
	eaPush(&pDoc->pContact->eaLoreDialogs, newDialog);

	// Update the UI
	CEContactChanged(pDoc, true);
}

static void CERemoveLoreDialogCB(UIButton *pButton, CELoreDialogGroup *pGroup)
{
	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	// Remove the group
	StructDestroy(parse_ContactLoreDialog, pGroup->pDoc->pContact->eaLoreDialogs[pGroup->index]);
	eaRemove(&pGroup->pDoc->pContact->eaLoreDialogs, pGroup->index);

	// Update the UI
	CEContactChanged(pGroup->pDoc, true);
}

static void CEAddStoreCollectionCB(UIButton *pButton, ContactEditDoc *pDoc)
{
	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pDoc->emDoc, true)) {
		return;
	}

	// Add a new group
	eaPush(&pDoc->pContact->storeCollections, StructCreate(parse_StoreCollection));

	// Update the UI
	CEContactChanged(pDoc, true);
}

static void CEAddAuctionBrokerCB(UIButton *pButton, ContactEditDoc *pDoc)
{
	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pDoc->emDoc, true)) {
		return;
	}

	// Add a new group
	eaPush(&pDoc->pContact->ppAuctionBrokerOptionList, StructCreate(parse_AuctionBrokerContactData));

	// Update the UI
	CEContactChanged(pDoc, true);
}

static void CEAddUGCSearchAgentCB(UIButton *pButton, ContactEditDoc *pDoc)
{
	UGCSearchAgentData* newUGCSearchAgentData;
	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pDoc->emDoc, true)) {
		return;
	}
	newUGCSearchAgentData = StructCreate(parse_UGCSearchAgentData);

	// Add a new group
	eaPush(&pDoc->pContact->ppUGCSearchAgentOptionList, newUGCSearchAgentData);


	// Update the UI
	CEContactChanged(pDoc, true);
}

//Creates structure for new item.  Called when you click the "add item" button. 
static void CEAddImageMenuItemCB(UIButton * pButton, ContactEditDoc *pDoc){
	ContactImageMenuItem* newImageMenuItem;
	// Make sure the resource is checked out of Gimme
	// this path should be only contact editor, so don't need to use commonCallbackParams
	if (!emDocIsEditable(&pDoc->emDoc, true)) {
		return;
	}

	pDoc->pContact->eContactFlags |= ContactFlag_ImageMenu;
	//add to list:
	newImageMenuItem = StructCreate(parse_ContactImageMenuItem);
	eaPush(&pDoc->pContact->pImageMenuData->items, newImageMenuItem);
	//update UI:
	CEContactChanged(pDoc, true);
}

static void CEAddStoreToCollectionCB(UIButton *pButton, CEStoreCollectionGroup *pGroup)
{
	ContactEditDoc* pDoc = pGroup ? pGroup->pDoc : NULL;
	// Make sure the resource is checked out of Gimme
	if (!pDoc || !emDocIsEditable(&pDoc->emDoc, true)) {
		return;
	}

	// Add a new group
	if(pDoc->pContact && pDoc->pContact->storeCollections && 
		pGroup->index >= 0 && pGroup->index < eaSize(&pGroup->pDoc->pContact->storeCollections))
	{
		eaPush(&pGroup->pDoc->pContact->storeCollections[pGroup->index]->eaStores, StructCreate(parse_StoreRef));

		// Update the UI
		CEContactChanged(pDoc, true);
	}
}

static void CEAddStoreCB(UIButton *pButton, ContactEditDoc *pDoc)
{
	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pDoc->emDoc, true)) {
		return;
	}

	// Add a new group
	eaPush(&pDoc->pContact->stores, StructCreate(parse_StoreRef));

	// Update the UI
	CEContactChanged(pDoc, true);
}

static void CEAddEndDialogAudioCB(UIButton *pButton, ContactEditDoc *pDoc)
{
	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pDoc->emDoc, true)) {
		return;
	}

	// Add a new group
	eaPush(&pDoc->pContact->eaEndDialogAudios, StructCreate(parse_EndDialogAudio));

	// Update the UI
	CEContactChanged(pDoc, true);
}

static void CERemoveStoreCollectionCB(UIButton *pButton, CEStoreCollectionGroup *pGroup)
{
	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	// Remove the group
	StructDestroy(parse_StoreCollection, pGroup->pDoc->pContact->storeCollections[pGroup->index]);
	eaRemove(&pGroup->pDoc->pContact->storeCollections, pGroup->index);

	// Update the UI
	CEContactChanged(pGroup->pDoc, true);
}

static void CERemoveAuctionBrokerCB(UIButton *pButton, CEAuctionBrokerContactDataGroup *pGroup)
{
	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	// Remove the group
	StructDestroy(parse_AuctionBrokerContactData, pGroup->pDoc->pContact->ppAuctionBrokerOptionList[pGroup->index]);
	eaRemove(&pGroup->pDoc->pContact->ppAuctionBrokerOptionList, pGroup->index);

	// Update the UI
	CEContactChanged(pGroup->pDoc, true);
}

static void CERemoveUGCSearchAgentCB(UIButton *pButton, CEUGCSearchAgentDataGroup *pGroup)
{
	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	// Remove the group
	StructDestroy(parse_UGCSearchAgentData, 
				pGroup->pDoc->pContact->ppUGCSearchAgentOptionList[pGroup->index]);
	eaRemove(&pGroup->pDoc->pContact->ppUGCSearchAgentOptionList, pGroup->index);

	// Update the UI
	CEContactChanged(pGroup->pDoc, true);
}

//removes the data from the contact.  UI fields get updated to match in refresh.
static void CERemoveImageMenuItemCB(UIButton *pButton, CEImageMenuItemGroup *pGroup)
{
	void* pWrapper;
	
	// Make sure the resource is checked out of Gimme
	if (pGroup->pCommonCallbackParams->pDocIsEditableFunc && !pGroup->pCommonCallbackParams->pDocIsEditableFunc(pGroup->pCommonCallbackParams->pDocIsEditableData)) {
		return;
	}

	pWrapper = eaGet( pGroup->peaWrappers, pGroup->index );
	if( pWrapper ) {
		// Remove the group
		StructDestroyVoid(pGroup->pWrapperParseTable, pWrapper);
		eaRemove(pGroup->peaWrappers, pGroup->index);
	}

	// Update the UI
	pGroup->pCommonCallbackParams->pDialogChangedFunc( pGroup->pCommonCallbackParams->pDialogChangedData );
}

static void CERemoveStoreCB(UIButton *pButton, CEStoreGroup *pGroup)
{
	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	// Remove the group
	if(pGroup->peaStores && pGroup->index >= 0 && pGroup->index < eaSize(pGroup->peaStores))
	{
		StoreRef* pStore = eaRemove(pGroup->peaStores, pGroup->index);
		StructDestroy(parse_StoreRef, pStore);
	}
	
	// Update the UI
	CEContactChanged(pGroup->pDoc, true);
}

static void CERemoveEndDialogAudioCB(UIButton *pButton, CEEndDialogAudioGroup *pGroup)
{
	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	// Remove the group
	StructDestroy(parse_EndDialogAudio, pGroup->pDoc->pContact->eaEndDialogAudios[pGroup->index]);
	eaRemove(&pGroup->pDoc->pContact->eaEndDialogAudios, pGroup->index);

	// Update the UI
	CEContactChanged(pGroup->pDoc, true);
}

static void CEAddPowerStoreCB(UIButton *pButton, ContactEditDoc *pDoc)
{
	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pDoc->emDoc, true)) {
		return;
	}

	// Add a new group
	eaPush(&pDoc->pContact->powerStores, StructCreate(parse_PowerStoreRef));

	// Update the UI
	CEContactChanged(pDoc, true);
}


static void CERemovePowerStoreCB(UIButton *pButton, CEStoreGroup *pGroup)
{
	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	// Remove the group
	StructDestroy(parse_PowerStoreRef, pGroup->pDoc->pContact->powerStores[pGroup->index]);
	eaRemove(&pGroup->pDoc->pContact->powerStores, pGroup->index);

	// Update the UI
	CEContactChanged(pGroup->pDoc, true);
}

ContactMissionOffer * CECreateBlankMissionOffer(void)
{
	ContactMissionOffer *pOffer = StructCreate(parse_ContactMissionOffer);
	eaPush(&pOffer->greetingDialog, StructCreate(parse_DialogBlock));
	eaPush(&pOffer->offerDialog, StructCreate(parse_DialogBlock));
	eaPush(&pOffer->inProgressDialog, StructCreate(parse_DialogBlock));
	eaPush(&pOffer->completedDialog, StructCreate(parse_DialogBlock));
	eaPush(&pOffer->failureDialog, StructCreate(parse_DialogBlock));

	return pOffer;
}

static void CEAddOfferCB(UIButton *pButton, ContactEditDoc *pDoc)
{
	ContactMissionOffer *pOffer;
	
	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pDoc->emDoc, true)) {
		return;
	}

	// Create a blank offer
	pOffer = CECreateBlankMissionOffer();
	CEFixupOfferMessages("ContactDef", pDoc->pContact->name, pOffer, eaSize(&pDoc->pContact->offerList));

	// Add a new group
	eaPush(&pDoc->pContact->offerList, pOffer);

	// Update the UI
	CEContactChanged(pDoc, true);
}

void CERemoveOffer(SA_PARAM_NN_VALID CEOfferGroup *pGroup)
{
	if (pGroup)
	{
		// Remove the group
		if(pGroup->pWrapperParseTable && pGroup->peaMissionOfferWrappers)
		{
			void* pWrapper = eaRemove(pGroup->peaMissionOfferWrappers, pGroup->index);
			if(pWrapper)
			{
				StructDestroyVoid(pGroup->pWrapperParseTable, pWrapper);
			}
		}
	}
}

static void CERemoveOfferCB(UIButton *pButton, CEOfferGroup *pGroup)
{
	// Make sure the resource is checked out of Gimme
	if (pGroup->pDocIsEditableFunc && !pGroup->pDocIsEditableFunc(pGroup->pDocIsEditableData)) 
	{
		return;
	}

	// Remove the group
	if(pGroup->pWrapperParseTable && pGroup->peaMissionOfferWrappers)
	{
		void* pWrapper = eaRemove(pGroup->peaMissionOfferWrappers, pGroup->index);
		if(pWrapper)
		{
			StructDestroyVoid(pGroup->pWrapperParseTable, pWrapper);
		}
	}

	// Update the UI
	if(pGroup->pDialogChangedFunc)
		pGroup->pDialogChangedFunc(pGroup->pDialogChangedData);
}

static void CEOpenMissionInEditorCB(UIButton *pButton, const char *pchOverrideName)
{
	// Extract the mission name	
	char *pchOnePastMissionName;
	if (pchOverrideName && (pchOnePastMissionName = strchr(pchOverrideName, '/')))
	{		
		U32 iNumBytesToCopy = (U32)((uintptr_t) pchOnePastMissionName - (uintptr_t) pchOverrideName);
		if (iNumBytesToCopy > 0)
		{
			char *estrMissionName = NULL;
			estrConcat(&estrMissionName, pchOverrideName, iNumBytesToCopy);
			emOpenFileEx(estrMissionName, "Mission");
			estrDestroy(&estrMissionName);
		}
	}
}

static void CEMoveSpecialDialogActionDownCB(UIButton *pButton, CESpecialActionGroup *pGroup)
{	
	if (pGroup && pGroup->pCommonCallbackParams)
	{
		// Make sure the document is editable
		if (pGroup->pCommonCallbackParams->pDocIsEditableFunc && !pGroup->pCommonCallbackParams->pDocIsEditableFunc(pGroup->pCommonCallbackParams->pDocIsEditableData)) {
			return;
		}

		// Move the action
		eaMove(pGroup->peaActions, pGroup->index + 1, pGroup->index);

		// Update the UI
		if(pGroup->pCommonCallbackParams->pDialogChangedFunc)
			pGroup->pCommonCallbackParams->pDialogChangedFunc(pGroup->pCommonCallbackParams->pDialogChangedData);
	}
}

static void CEMoveSpecialDialogActionUpCB(UIButton *pButton, CESpecialActionGroup *pGroup)
{
	if (pGroup && pGroup->pCommonCallbackParams)
	{
		// Make sure the document is editable
		if (pGroup->pCommonCallbackParams->pDocIsEditableFunc && !pGroup->pCommonCallbackParams->pDocIsEditableFunc(pGroup->pCommonCallbackParams->pDocIsEditableData)) {
			return;
		}

		// Move the action		
		eaMove(pGroup->peaActions, pGroup->index - 1, pGroup->index);

		// Update the UI
		if(pGroup->pCommonCallbackParams->pDialogChangedFunc)
			pGroup->pCommonCallbackParams->pDialogChangedFunc(pGroup->pCommonCallbackParams->pDialogChangedData);
	}
}

static void CEMoveOfferUpCB(UIButton *pButton, CEOfferGroup *pGroup)
{
	// Make sure the resource is checked out of Gimme
	if (pGroup->pDocIsEditableFunc && !pGroup->pDocIsEditableFunc(pGroup->pDocIsEditableData)) {
		return;
	}

	// Move the group up
	if(pGroup->peaMissionOfferWrappers)
	{
		eaMove(pGroup->peaMissionOfferWrappers, pGroup->index-1, pGroup->index);
	}

	// Update the UI
	if(pGroup->pDialogChangedFunc)
		pGroup->pDialogChangedFunc(pGroup->pDialogChangedData);
}


static void CEMoveOfferDownCB(UIButton *pButton, CEOfferGroup *pGroup)
{
	// Make sure the resource is checked out of Gimme
	if (pGroup->pDocIsEditableFunc && !pGroup->pDocIsEditableFunc(pGroup->pDocIsEditableData)) {
		return;
	}

	// Move the group up
	if(pGroup->peaMissionOfferWrappers)
	{
		eaMove(pGroup->peaMissionOfferWrappers, pGroup->index+1, pGroup->index);
	}

	// Update the UI
	if(pGroup->pDialogChangedFunc)
		pGroup->pDialogChangedFunc(pGroup->pDialogChangedData);
}

static void CEMoveSpecialUpCB(UIButton *pButton, CESpecialDialogGroup *pGroup)
{
	// Make sure the resource is checked out of Gimme
	if (pGroup->pCommonCallbackParams->pDocIsEditableFunc && !pGroup->pCommonCallbackParams->pDocIsEditableFunc(pGroup->pCommonCallbackParams->pDocIsEditableData)) {
		return;
	}

	// Move the group up
	if(pGroup->peaSpecialDialogWrappers)
	{
		eaMove(pGroup->peaSpecialDialogWrappers, pGroup->index-1, pGroup->index);
	}

	// Update the UI
	if(pGroup->pCommonCallbackParams->pDialogChangedFunc)
		pGroup->pCommonCallbackParams->pDialogChangedFunc(pGroup->pCommonCallbackParams->pDialogChangedData);
}


static void CEMoveSpecialDownCB(UIButton *pButton, CESpecialDialogGroup *pGroup)
{
	// Make sure the resource is checked out of Gimme
	if (pGroup->pCommonCallbackParams->pDocIsEditableFunc && !pGroup->pCommonCallbackParams->pDocIsEditableFunc(pGroup->pCommonCallbackParams->pDocIsEditableData)) {
		return;
	}

	// Move the group up
	if(pGroup->peaSpecialDialogWrappers)
	{
		eaMove(pGroup->peaSpecialDialogWrappers, pGroup->index+1, pGroup->index);
	}

	// Update the UI
	if(pGroup->pCommonCallbackParams->pDialogChangedFunc)
		pGroup->pCommonCallbackParams->pDialogChangedFunc(pGroup->pCommonCallbackParams->pDialogChangedData);
}


static void CESpecialGameActionChangeCB(UIGameActionEditButton *pButton, CESpecialActionGroup *pGroup)
{
	SpecialDialogAction *pAction = pGroup && pGroup->peaActions && pGroup->index >= 0 && pGroup->index < eaSize(pGroup->peaActions) ? (*pGroup->peaActions)[pGroup->index] : NULL;

	if(!pGroup || !pGroup->pCommonCallbackParams || !pAction)
		return;

	// Make sure the resource is checked out of Gimme
	if (pGroup->pCommonCallbackParams->pDocIsEditableFunc && !pGroup->pCommonCallbackParams->pDocIsEditableFunc(pGroup->pCommonCallbackParams->pDocIsEditableData)) {
		return;
	}

	if (gameactionblock_Compare(&pAction->actionBlock, pButton->pActionBlock)) {
		// No change, so do nothing
		return;
	}

	StructCopyAll(parse_WorldGameActionBlock, pButton->pActionBlock, &pAction->actionBlock);

	if(pGroup->pCommonCallbackParams->pDialogChangedFunc)
		pGroup->pCommonCallbackParams->pDialogChangedFunc(pGroup->pCommonCallbackParams->pDialogChangedData);
}

//Change update for a GameActionEditButton in an image menu item.
static void CEImageMenuGameActionChangeCB(UIGameActionEditButton *pButton, 
											CEImageMenuItemGroup *pGroup)
{
	ContactImageMenuItem *pItem = NULL;
	if ( pGroup ){
		void* pWrapper = eaGet( pGroup->peaWrappers, pGroup->index );
		if( pGroup->pImageMenuItemFromWrapperFunc)
			pItem = pGroup->pImageMenuItemFromWrapperFunc( pWrapper );
		else
			pItem = pWrapper;
	}


	if(!pGroup || !pGroup->pCommonCallbackParams || !pItem)
		return;

	// Make sure the resource is checked out of Gimme
	if (pGroup->pCommonCallbackParams->pDocIsEditableFunc && !pGroup->pCommonCallbackParams->pDocIsEditableFunc(pGroup->pCommonCallbackParams->pDocIsEditableData)) {
		return;
	}

	if (gameactionblock_Compare(pItem->action, pButton->pActionBlock)) {
		// No change, so do nothing
		return;
	}

	pItem->action = StructClone(parse_WorldGameActionBlock, pButton->pActionBlock);

	if(pGroup->pCommonCallbackParams->pDialogChangedFunc)
		pGroup->pCommonCallbackParams->pDialogChangedFunc(pGroup->pCommonCallbackParams->pDialogChangedData);
}

static UILabel *CERefreshLabel(UILabel *pLabel, const char *pcText, const char *pcTooltip, F32 x, F32 xPercent, F32 y, UIExpander *pExpander)
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

static F32 CERefreshDialogGroup(CEDialogGroup *pGroup, UIExpander *pExpander, F32 y, F32 xPercentWidth, int index, DialogBlock ***peaBlocks, char *pcLabel, char *pcTooltip, DialogBlock *pBlock, DialogBlock *pOldBlock, bool bShowAnimList, bool bShowCondition, bool bPrependLabelToCondition, bool bSplitView)
{
	char buf[260];
	int xOffsetControlIndent = bSplitView ? X_OFFSET_CONTROL : X_OFFSET_CONTROL+15;
	F32 fPadRight = bSplitView ? 0 : 90;
	
	if(bSplitView)
		xPercentWidth = X_PERCENT_SPLIT;
 
	pGroup->index = index;
	pGroup->peaBlocks = peaBlocks;

	// Update message label
	if (index == 0) {
		strcpy(buf, pcLabel);
	} else {
		sprintf(buf, "%s %d", pcLabel, index+1);
	}
	pGroup->pMessageLabel = CERefreshLabel(pGroup->pMessageLabel, buf, pcTooltip, X_OFFSET_BASE, 0, y, pExpander);

	// Update button
	if (!pGroup->pRemoveButton) {
		if(bSplitView)
			pGroup->pRemoveButton = ui_ButtonCreate((index == 0) ? "Add" : "Remove", 5, y, (index == 0) ? CEAddDialogCB : CERemoveDialogCB, pGroup);
		else
			pGroup->pRemoveButton = ui_ButtonCreate((index == 0) ? "Add" : "Remove", 0, y, (index == 0) ? CEAddDialogCB : CERemoveDialogCB, pGroup);
		ui_WidgetSetWidth(UI_WIDGET(pGroup->pRemoveButton), 80);
		if(bSplitView)
			ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pRemoveButton), 5, y, 0, 0, UITopRight);
		else
			ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pRemoveButton), -80, y, xPercentWidth, 0, UITopLeft);
		ui_ExpanderAddChild(pExpander, pGroup->pRemoveButton);
	} else {
		if(bSplitView)
			ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pRemoveButton), 5, y, 0, 0, UITopRight);
		else
			ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pRemoveButton), -80, y, xPercentWidth, 0, UITopLeft);
		ui_ButtonSetText(pGroup->pRemoveButton, (index == 0) ? "Add" : "Remove");
		ui_ButtonSetCallback(pGroup->pRemoveButton, (index == 0) ? CEAddDialogCB : CERemoveDialogCB, pGroup);
	}
 
	// Update message field
	if (!pGroup->pMessageField) {
		pGroup->pMessageField = MEFieldCreateSimple(kMEFieldType_Message, pOldBlock ? &pOldBlock->displayTextMesg : NULL, &pBlock->displayTextMesg, parse_DisplayMessage, "editorCopy");
		GEAddFieldToParent(pGroup->pMessageField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, xPercentWidth, UIUnitPercentage, fPadRight, pGroup->pFieldChangeFunc, pGroup->pFieldPreChangeFunc, pGroup->pFieldChangeData);
	} else {
		ui_WidgetSetPosition(pGroup->pMessageField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pMessageField, pOldBlock ? &pOldBlock->displayTextMesg : NULL, &pBlock->displayTextMesg);
	}

	if(bSplitView)
	{
		ui_WidgetQueueFreeAndNull(&pGroup->pSoundLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pAnimLabel);
		// Update audio
		if (!pGroup->pAudioField) {
			pGroup->pAudioField = MEFieldCreateSimpleDataProvided(kMEFieldType_ValidatedTextEntry, pOldBlock, pBlock, parse_DialogBlock, "Audio", NULL, sndGetEventListStatic(), NULL);
			GEAddFieldToParent(pGroup->pAudioField, UI_WIDGET(pExpander), X_OFFSET_AUDIO, y, X_PERCENT_SPLIT, 0.20, UIUnitPercentage, fPadRight, pGroup->pFieldChangeFunc, pGroup->pFieldPreChangeFunc, pGroup->pFieldChangeData);
		} else {
			ui_WidgetSetPositionEx(pGroup->pAudioField->pUIWidget, X_OFFSET_AUDIO, y, X_PERCENT_SPLIT, 0, UITopLeft);
			MEFieldSetAndRefreshFromData(pGroup->pAudioField, pOldBlock, pBlock);
		}

		if (bShowAnimList)
		{
			if (!pGroup->pAnimField) {
				pGroup->pAnimField = MEFieldCreateSimpleDictionary(kMEFieldType_ValidatedTextEntry, pOldBlock, pBlock, parse_DialogBlock, "AnimList", g_AnimListDict, parse_AIAnimList, "Name");
				GEAddFieldToParent(pGroup->pAnimField, UI_WIDGET(pExpander), X_OFFSET_ANIM, y, X_PERCENT_SPLIT+X_PERCENT_SECOND_SML, 0.25, UIUnitPercentage, 95, pGroup->pFieldChangeFunc, pGroup->pFieldPreChangeFunc, pGroup->pFieldChangeData);
			} else {
				ui_WidgetSetPositionEx(pGroup->pAnimField->pUIWidget, X_OFFSET_ANIM, y, X_PERCENT_SPLIT+X_PERCENT_SECOND_SML, 0, UITopLeft);
				MEFieldSetAndRefreshFromData(pGroup->pAnimField, pOldBlock, pBlock);
			}		
		}
		else
		{
			ui_WidgetQueueFreeAndNull(&pGroup->pAnimLabel);
			MEFieldSafeDestroy(&pGroup->pAnimField);
		}
	}

	y+=STANDARD_ROW_HEIGHT;

	// Show the condition field only for greetings and special dialogs in the dialog block
	if (bShowCondition)
	{
		char *estrLabel = NULL;
		int xOffset = X_OFFSET_BASE + 15;
		estrStackCreate(&estrLabel);
		if(!bPrependLabelToCondition) {
			estrPrintf(&estrLabel, "Condition");
		} else {
			estrPrintf(&estrLabel, "%s Condition", pcLabel);
			if(bSplitView)
				xOffset -= 15;
		}
		// Update expression field
		pGroup->pExpressionLabel = CERefreshLabel(pGroup->pExpressionLabel, estrLabel, "Optional expression.  If true, this dialog can be shown.", xOffset, 0, y, pExpander);
		estrDestroy(&estrLabel);
		if (!pGroup->pExpressionField) {
			pGroup->pExpressionField = MEFieldCreateSimple(kMEFieldTypeEx_Expression, pOldBlock, pBlock, parse_DialogBlock, "Condition");
			GEAddFieldToParent(pGroup->pExpressionField, UI_WIDGET(pExpander), xOffsetControlIndent, y, 0, xPercentWidth, UIUnitPercentage, fPadRight, pGroup->pFieldChangeFunc, pGroup->pFieldPreChangeFunc, pGroup->pFieldChangeData);
		} else {
			ui_WidgetSetPosition(pGroup->pExpressionField->pUIWidget, xOffsetControlIndent, y);
			MEFieldSetAndRefreshFromData(pGroup->pExpressionField, pOldBlock, pBlock);
		}
	}
	else
	{
		ui_WidgetQueueFreeAndNull(&pGroup->pExpressionLabel);
		MEFieldSafeDestroy(&pGroup->pExpressionField);
	}

	if(bSplitView)
	{
		ui_WidgetQueueFreeAndNull(&pGroup->pPhraseLabel);
		// Update audio phrase
		if (!pGroup->pAudioPhraseField) {
			pGroup->pAudioPhraseField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pOldBlock, pBlock, parse_DialogBlock, "Phrase", ContactAudioPhrasesEnum);
			GEAddFieldToParent(pGroup->pAudioPhraseField, UI_WIDGET(pExpander), X_OFFSET_AUDIO, y, X_PERCENT_SPLIT, 0.20, UIUnitPercentage, fPadRight, pGroup->pFieldChangeFunc, pGroup->pFieldPreChangeFunc, pGroup->pFieldChangeData);
		} else {
			ui_WidgetSetPositionEx(pGroup->pAudioPhraseField->pUIWidget, X_OFFSET_AUDIO, y, X_PERCENT_SPLIT, 0, UITopLeft);
			MEFieldSetAndRefreshFromData(pGroup->pAudioPhraseField, pOldBlock, pBlock);
		}

		ui_WidgetQueueFreeAndNull(&pGroup->pDialogFormatterLabel);
		// Update dialog formatter
		if (pGroup->pDialogFormatterField == NULL)
		{
			pGroup->pDialogFormatterField = MEFieldCreateSimpleDictionary(kMEFieldType_ValidatedTextEntry, pOldBlock, pBlock, parse_DialogBlock, "DialogFormatter", g_hContactDialogFormatterDefDictionary, parse_ContactDialogFormatterDef, "Name");
			GEAddFieldToParent(pGroup->pDialogFormatterField, UI_WIDGET(pExpander), X_OFFSET_ANIM, y, X_PERCENT_SPLIT + X_PERCENT_SECOND_SML, 0.20, UIUnitPercentage, fPadRight, pGroup->pFieldChangeFunc, pGroup->pFieldPreChangeFunc, pGroup->pFieldChangeData);
		}
		else 
		{
			ui_WidgetSetPositionEx(pGroup->pDialogFormatterField->pUIWidget, X_OFFSET_ANIM, y, X_PERCENT_SPLIT + X_PERCENT_SECOND_SML, 0, UITopLeft);
			MEFieldSetAndRefreshFromData(pGroup->pDialogFormatterField, pOldBlock, pBlock);
		}
		y+= STANDARD_ROW_HEIGHT + EXPANDER_HEIGHT;
	}
	else
	{
		if (bShowCondition)
		{
			y+=STANDARD_ROW_HEIGHT;
		}		
		// Update audio
		pGroup->pSoundLabel = CERefreshLabel(pGroup->pSoundLabel, "Audio", "Sound to play for the player interacting with this contact when this dialog appears", X_OFFSET_BASE+15, 0, y, pExpander);
		if (!pGroup->pAudioField) {
			pGroup->pAudioField = MEFieldCreateSimpleDataProvided(kMEFieldType_ValidatedTextEntry, pOldBlock, pBlock, parse_DialogBlock, "Audio", NULL, sndGetEventListStatic(), NULL);
			GEAddFieldToParent(pGroup->pAudioField, UI_WIDGET(pExpander), xOffsetControlIndent, y, 0, xPercentWidth, UIUnitPercentage, fPadRight, pGroup->pFieldChangeFunc, pGroup->pFieldPreChangeFunc, pGroup->pFieldChangeData);
		} else {
			ui_WidgetSetPositionEx(pGroup->pAudioField->pUIWidget, xOffsetControlIndent, y, 0, 0, UITopLeft);
			MEFieldSetAndRefreshFromData(pGroup->pAudioField, pOldBlock, pBlock);
		}

		y+=STANDARD_ROW_HEIGHT;

		// Update audio phrase
		pGroup->pPhraseLabel = CERefreshLabel(pGroup->pPhraseLabel, "Phrase", "Voice over phrase to say in a voice based on the contact's costume.", X_OFFSET_BASE+15, 0, y, pExpander);
		if (!pGroup->pAudioPhraseField) {
			pGroup->pAudioPhraseField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pOldBlock, pBlock, parse_DialogBlock, "Phrase", ContactAudioPhrasesEnum);
			GEAddFieldToParent(pGroup->pAudioPhraseField, UI_WIDGET(pExpander), xOffsetControlIndent, y, 0, xPercentWidth, UIUnitPercentage, fPadRight, pGroup->pFieldChangeFunc, pGroup->pFieldPreChangeFunc, pGroup->pFieldChangeData);
		} else {
			ui_WidgetSetPositionEx(pGroup->pAudioPhraseField->pUIWidget, xOffsetControlIndent, y, 0, 0, UITopLeft);
			MEFieldSetAndRefreshFromData(pGroup->pAudioPhraseField, pOldBlock, pBlock);
		}

		y+= STANDARD_ROW_HEIGHT;

		if (bShowAnimList)
		{
			pGroup->pAnimLabel = CERefreshLabel(pGroup->pAnimLabel, "Anim. List", "Animation list to play when this dialog is active.", X_OFFSET_BASE+15, 0, y, pExpander);
			if (!pGroup->pAnimField) {
				pGroup->pAnimField = MEFieldCreateSimpleDictionary(kMEFieldType_ValidatedTextEntry, pOldBlock, pBlock, parse_DialogBlock, "AnimList", g_AnimListDict, parse_AIAnimList, "Name");
				GEAddFieldToParent(pGroup->pAnimField, UI_WIDGET(pExpander), xOffsetControlIndent, y, 0, xPercentWidth, UIUnitPercentage, fPadRight, pGroup->pFieldChangeFunc, pGroup->pFieldPreChangeFunc, pGroup->pFieldChangeData);
			} else {
				ui_WidgetSetPositionEx(pGroup->pAnimField->pUIWidget, xOffsetControlIndent, y, 0, 0, UITopLeft);
				MEFieldSetAndRefreshFromData(pGroup->pAnimField, pOldBlock, pBlock);
			}		
			y += STANDARD_ROW_HEIGHT;
		}
		else
		{
			ui_WidgetQueueFreeAndNull(&pGroup->pAnimLabel);
			MEFieldSafeDestroy(&pGroup->pAnimField);
		}

		pGroup->pDialogFormatterLabel = CERefreshLabel(pGroup->pDialogFormatterLabel, "Dialog Formatter", "Used by UI to format dialog messages.", X_OFFSET_BASE+15, 0, y, pExpander);
		if (pGroup->pDialogFormatterField == NULL)
		{
			pGroup->pDialogFormatterField = MEFieldCreateSimpleDictionary(kMEFieldType_ValidatedTextEntry, pOldBlock, pBlock, parse_DialogBlock, "DialogFormatter", g_hContactDialogFormatterDefDictionary, parse_ContactDialogFormatterDef, "Name");
			GEAddFieldToParent(pGroup->pDialogFormatterField, UI_WIDGET(pExpander), xOffsetControlIndent, y, 0, xPercentWidth, UIUnitPercentage, fPadRight, pGroup->pFieldChangeFunc, pGroup->pFieldPreChangeFunc, pGroup->pFieldChangeData);
		}
		else 
		{
			ui_WidgetSetPositionEx(pGroup->pDialogFormatterField->pUIWidget, xOffsetControlIndent, y, 0, 0, UITopLeft);
			MEFieldSetAndRefreshFromData(pGroup->pDialogFormatterField, pOldBlock, pBlock);
		}
		y += STANDARD_ROW_HEIGHT;
	}

	// Continue Text and Text Formatter are toggle-able
	if(s_bShowContinueTextAndTextFormatter)
	{
		pGroup->pContinueMessageLabel = CERefreshLabel(pGroup->pContinueMessageLabel, "Continue Text", "This is the text displayed for the continue button.", X_OFFSET_BASE, 0, y, pExpander);
		// Update continue text field
		if (pGroup->pContinueMessageField == NULL) 
		{
			pGroup->pContinueMessageField = MEFieldCreateSimple(kMEFieldType_Message, pOldBlock ? &pOldBlock->continueTextMesg : NULL, &pBlock->continueTextMesg, parse_DisplayMessage, "editorCopy");
			GEAddFieldToParent(pGroup->pContinueMessageField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, xPercentWidth, UIUnitPercentage, fPadRight, pGroup->pFieldChangeFunc, pGroup->pFieldPreChangeFunc, pGroup->pFieldChangeData);
		} 
		else 
		{
			ui_WidgetSetPosition(pGroup->pContinueMessageField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pContinueMessageField, pOldBlock ? &pOldBlock->continueTextMesg : NULL, &pBlock->continueTextMesg);
		}

		y += STANDARD_ROW_HEIGHT;

		// Update dialog formatter field
		pGroup->pContinueMessageDialogFormatterLabel = CERefreshLabel(pGroup->pContinueMessageDialogFormatterLabel, "Text Formatter", "Used by UI to format the continue text", X_OFFSET_BASE, 0, y, pExpander);
		if (pGroup->pContinueMessageDialogFormatterField == NULL)
		{
			pGroup->pContinueMessageDialogFormatterField = MEFieldCreateSimpleDictionary(kMEFieldType_ValidatedTextEntry, pOldBlock, pBlock, parse_DialogBlock, "ContinueTextDialogFormatter", g_hContactDialogFormatterDefDictionary, parse_ContactDialogFormatterDef, "Name");
			GEAddFieldToParent(pGroup->pContinueMessageDialogFormatterField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, xPercentWidth, UIUnitPercentage, fPadRight, pGroup->pFieldChangeFunc, pGroup->pFieldPreChangeFunc, pGroup->pFieldChangeData);
		} else {
			ui_WidgetSetPosition(pGroup->pContinueMessageDialogFormatterField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pContinueMessageDialogFormatterField, pOldBlock, pBlock);
		}

		y += STANDARD_ROW_HEIGHT;
	}
	else
	{
		if(pGroup->pContinueMessageLabel != NULL)
		{
			ui_ExpanderRemoveChild(pExpander, pGroup->pContinueMessageLabel);
			ui_WidgetDestroy(&pGroup->pContinueMessageLabel);
		}
		if(pGroup->pContinueMessageField != NULL)
		{
			ui_ExpanderRemoveChild(pExpander, pGroup->pContinueMessageField);
			MEFieldSafeDestroy(&pGroup->pContinueMessageField);
		}
		if(pGroup->pContinueMessageDialogFormatterLabel != NULL)
		{
			ui_ExpanderRemoveChild(pExpander, pGroup->pContinueMessageDialogFormatterLabel);
			ui_WidgetDestroy(&pGroup->pContinueMessageDialogFormatterLabel);
		}
		if(pGroup->pContinueMessageDialogFormatterField != NULL)
		{
			ui_ExpanderRemoveChild(pExpander, pGroup->pContinueMessageDialogFormatterField);
			MEFieldSafeDestroy(&pGroup->pContinueMessageDialogFormatterField);
		}
	}

	return y;
}

static int CERefreshDialogBlocks(CEDialogGroup ***peaGroups, UIExpander *pExpander, F32* y, F32 xPercentWidth, int n, char *pcLabel, char *pcTooltip, DialogBlock ***peaDialogBlocks, DialogBlock ***peaOldDialogBlocks, bool bShowAnimList, bool bShowCondition, bool bPrependLabelToCondition, bool bSplitView,
								 CECallbackFunc pDocIsEditableFunc, void* pDocIsEditableData, 
								 CECallbackFunc pDialogChangedFunc, void* pDialogChangedData, 
								 MEFieldChangeCallback pFieldChangeFunc, MEFieldPreChangeCallback pFieldPreChangeFunc, void* pFieldChangeData)
{
	DialogBlock *pBlock, *pOldBlock;
	int i;

	for(i=0; i<eaSize(peaDialogBlocks); ++i) {
		pBlock = (*peaDialogBlocks)[i];
		if (peaOldDialogBlocks && (eaSize(peaOldDialogBlocks) > i)) {
			pOldBlock = (*peaOldDialogBlocks)[i];
		} else {
			pOldBlock = NULL;
		}
		while (n >= eaSize(peaGroups)) {
			CEDialogGroup *pGroup = CECreateDialogGroup(pDocIsEditableFunc, pDocIsEditableData, pDialogChangedFunc, pDialogChangedData, pFieldChangeFunc, pFieldPreChangeFunc, pFieldChangeData);
			eaPush(peaGroups, pGroup);
		}
		*y = CERefreshDialogGroup((*peaGroups)[n], pExpander, (*y) + STANDARD_ROW_HEIGHT, xPercentWidth, i, peaDialogBlocks, pcLabel, pcTooltip, pBlock, pOldBlock, bShowAnimList, bShowCondition, bPrependLabelToCondition, bSplitView);
		++n;
	}
	return i;
}

static int CERefreshDialogBlocksUsingDoc(ContactEditDoc *pDoc, CEDialogGroup ***peaGroups, UIExpander *pExpander, F32* y, int n, char *pcLabel, char *pcTooltip, DialogBlock ***peaDialogBlocks, DialogBlock ***peaOldDialogBlocks)
{
	bool bShowAnim = (pExpander == pDoc->pSpecialExpander || (pExpander == pDoc->pOfferExpander && stricmp(pcLabel, CE_COMPLETED_MISSION_LABEL) == 0)); // Anim lists are only displayed for the special dialogs and mission completed dialog
	bool bShowCondition = ((pExpander == pDoc->pDialogExpander && stricmp(pcLabel, CE_GREETING_LABEL) == 0) || (pExpander == pDoc->pSpecialExpander));


	return CERefreshDialogBlocks(peaGroups, pExpander, y, X_PERCENT_SPLIT, n, pcLabel, pcTooltip, peaDialogBlocks, peaOldDialogBlocks, bShowAnim, bShowCondition, (pExpander != pDoc->pSpecialExpander), true,
								CEIsDocEditable, pDoc,
								CEUpdateUI, pDoc,
								CEFieldChangedCB, CEFieldPreChangeCB, pDoc);
							
}

static void CERefreshDialogExpander(ContactEditDoc *pDoc)
{
	int n = 0;
	int i;
	F32 y = STANDARD_ROW_HEIGHT;

	// Hide dialog expander if Single Dialog type
	if (pDoc->pContact->type == ContactType_SingleDialog) {
		if (pDoc->pDialogExpander->group) {
			ui_ExpanderGroupRemoveExpander(pDoc->pExpanderGroup, pDoc->pDialogExpander);
		}
		return;
	}
	if (!pDoc->pDialogExpander->group) {
		ui_ExpanderGroupInsertExpander(pDoc->pExpanderGroup, pDoc->pDialogExpander, 1);
	}

	// Refresh the groups
	n += CERefreshDialogBlocksUsingDoc(pDoc, &pDoc->eaDialogGroups, pDoc->pDialogExpander, &y, n, CE_GREETING_LABEL, "If this text is set, an additional Greeting dialog will show when first visiting the contact each time the player enters the zone",
								&pDoc->pContact->greetingDialog, pDoc->pOrigContact ? &pDoc->pOrigContact->greetingDialog : NULL);
	n += CERefreshDialogBlocksUsingDoc(pDoc, &pDoc->eaDialogGroups, pDoc->pDialogExpander, &y, n, "Info", "If this text is set, an additional option on the contact will allow the player to read this info",
								&pDoc->pContact->infoDialog, pDoc->pOrigContact ? &pDoc->pOrigContact->infoDialog : NULL);
	n += CERefreshDialogBlocksUsingDoc(pDoc, &pDoc->eaDialogGroups, pDoc->pDialogExpander, &y, n, "Mission List", "If this text is set, it will display above the option list if there are any missions available",
								&pDoc->pContact->missionListDialog, pDoc->pOrigContact ? &pDoc->pOrigContact->missionListDialog : NULL);
	n += CERefreshDialogBlocksUsingDoc(pDoc, &pDoc->eaDialogGroups, pDoc->pDialogExpander, &y, n, "No Mission", "If this text is set, it will display above the option list if there are no missions available",
								&pDoc->pContact->noMissionsDialog, pDoc->pOrigContact ? &pDoc->pOrigContact->noMissionsDialog : NULL);
	n += CERefreshDialogBlocksUsingDoc(pDoc, &pDoc->eaDialogGroups, pDoc->pDialogExpander, &y, n, "Return to Root", "If this text is set, it will display above the option list when player goes back to the option list from a special dialog",
								&pDoc->pContact->exitDialog, pDoc->pOrigContact ? &pDoc->pOrigContact->exitDialog : NULL);
	n += CERefreshDialogBlocksUsingDoc(pDoc, &pDoc->eaDialogGroups, pDoc->pDialogExpander, &y, n, "General Callout", "If this text is set and there is no mission available, the contact will call out to the player occasionally",
								&pDoc->pContact->generalCallout, pDoc->pOrigContact ? &pDoc->pOrigContact->generalCallout : NULL);
	n += CERefreshDialogBlocksUsingDoc(pDoc, &pDoc->eaDialogGroups, pDoc->pDialogExpander, &y, n, "Mission Callout", "If this text is set and there is a mission available, the contact will call out to the player occasionally",
								&pDoc->pContact->missionCallout, pDoc->pOrigContact ? &pDoc->pOrigContact->missionCallout : NULL);

	// Free unused dialog groups
	for(i = eaSize(&pDoc->eaDialogGroups)-1; i >= n; --i) {
		assert(pDoc->eaDialogGroups);
		CEFreeDialogGroup(pDoc->eaDialogGroups[i]);
		eaRemove(&pDoc->eaDialogGroups, i);
	}

	// Dialog Exit Text Override
	pDoc->pDialogExitTextOverrideLabel = CERefreshLabel(pDoc->pDialogExitTextOverrideLabel, "Exit Text Override", "This text overrides the game's default exit dialog text.", X_OFFSET_BASE, 0, y, pDoc->pDialogExpander);
	if (pDoc->pDialogExitTextOverrideField == NULL) 
	{
		pDoc->pDialogExitTextOverrideField = MEFieldCreateSimple(kMEFieldType_Message, pDoc->pOrigContact? &pDoc->pOrigContact->dialogExitTextOverrideMsg : NULL, &pDoc->pContact->dialogExitTextOverrideMsg, parse_DisplayMessage, "editorCopy");
		CEAddFieldToParent(pDoc->pDialogExitTextOverrideField, UI_WIDGET(pDoc->pDialogExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
	} else 
	{
		ui_WidgetSetPosition(pDoc->pDialogExitTextOverrideField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pDoc->pDialogExitTextOverrideField, pDoc->pOrigContact?&pDoc->pOrigContact->dialogExitTextOverrideMsg:NULL, &pDoc->pContact->dialogExitTextOverrideMsg);
	}
	y += STANDARD_ROW_HEIGHT;

	// Se the expander height
	ui_ExpanderSetHeight(pDoc->pDialogExpander, y);
}

static void CERefreshInfoExpander(ContactEditDoc *pDoc)
{
	F32 y = 0;

	// Interact condition
	if (!pDoc->pInteractCondField) {
		CECreateLabel("Can Interact If", "(Works in Encounter 1 only!) The contact will not be interactable if this expression is false.  Blank means true.", X_OFFSET_BASE, y, pDoc->pInfoExpander);
		pDoc->pInteractCondField = MEFieldCreateSimple(kMEFieldTypeEx_Expression, pDoc->pOrigContact, pDoc->pContact, parse_ContactDef, "CanInteractIf");
		CEAddFieldToParent(pDoc->pInteractCondField, UI_WIDGET(pDoc->pInfoExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
		eaPush(&pDoc->eaDocFields, pDoc->pInteractCondField);
	}
	y += STANDARD_ROW_HEIGHT;

	// Remote Interact condition
	if (!pDoc->pCanAccessRemotelyField) {
		CECreateLabel("Remote Interact If", "If this expression is true, then the contact will be interactable remotely.  Blank means false.", X_OFFSET_BASE, y, pDoc->pInfoExpander);
		pDoc->pCanAccessRemotelyField = MEFieldCreateSimple(kMEFieldTypeEx_Expression, pDoc->pOrigContact, pDoc->pContact, parse_ContactDef, "CanAccessRemotely");
		CEAddFieldToParent(pDoc->pCanAccessRemotelyField, UI_WIDGET(pDoc->pInfoExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
		eaPush(&pDoc->eaDocFields, pDoc->pCanAccessRemotelyField);
	}
	y += STANDARD_ROW_HEIGHT;

	// Display Name
	if (!pDoc->pDisplayNameField) {
		CECreateLabel("Display Name", "The name of the Contact.  Should match the entity's name when possible.", X_OFFSET_BASE, y, pDoc->pInfoExpander);
		pDoc->pDisplayNameField = MEFieldCreateSimple(kMEFieldType_Message, pDoc->pOrigContact?&pDoc->pOrigContact->displayNameMsg:NULL, pDoc->pContact?&pDoc->pContact->displayNameMsg:NULL, parse_DisplayMessage, "editorCopy");
		CEAddFieldToParent(pDoc->pDisplayNameField, UI_WIDGET(pDoc->pInfoExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
	} else {
		ui_WidgetSetPosition(pDoc->pDisplayNameField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pDoc->pDisplayNameField, pDoc->pOrigContact?&pDoc->pOrigContact->displayNameMsg:NULL, pDoc->pContact?&pDoc->pContact->displayNameMsg:NULL);
	}
	y += STANDARD_ROW_HEIGHT;


	// Costume Section
	pDoc->pCostumeTypeLabel = CERefreshLabel(pDoc->pCostumeTypeLabel, "Costume From", "Where the contact's costume should be pulled from", X_OFFSET_BASE, 0, y, pDoc->pInfoExpander);
	if(!pDoc->pCostumeTypeField) {
		pDoc->pCostumeTypeField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pDoc->pOrigContact, pDoc->pContact, parse_ContactDef, "CostumeType", ContactCostumeTypeEnum);
		CEAddFieldToParent(pDoc->pCostumeTypeField, UI_WIDGET(pDoc->pInfoExpander), X_OFFSET_CONTROL, y, 0, X_PERCENT_SPLIT, UIUnitPercentage, 0, pDoc);
	} else {
		ui_WidgetSetPosition(pDoc->pCostumeTypeField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pDoc->pCostumeTypeField, pDoc->pOrigContact, pDoc->pContact);
	}

	// No costume override
	if(pDoc->pContact->costumePrefs.eCostumeType == ContactCostumeType_Default)
	{
		ui_WidgetQueueFreeAndNull(&pDoc->pCostumeLabel);
	}

	// Specified Costume
	if(pDoc->pContact->costumePrefs.eCostumeType == ContactCostumeType_Specified)
	{
		y += STANDARD_ROW_HEIGHT;
		pDoc->pCostumeLabel = CERefreshLabel(pDoc->pCostumeLabel, "Costume Override", "The costume to use in headshots instead of any the critter may have.", X_OFFSET_BASE+X_OFFSET_BASE, 0, y, pDoc->pInfoExpander);
		if(!pDoc->pCostumeField) {
			pDoc->pCostumeField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_ValidatedTextEntry, pDoc->pOrigContact, pDoc->pContact, parse_ContactDef, "CostumeOverride", "PlayerCostume", "ResourceName");
			CEAddFieldToParent(pDoc->pCostumeField, UI_WIDGET(pDoc->pInfoExpander), X_OFFSET_CONTROL+X_OFFSET_BASE, y, 0, X_PERCENT_SPLIT, UIUnitPercentage, 0, pDoc);
		} else {
			ui_WidgetSetPosition(pDoc->pCostumeField->pUIWidget, X_OFFSET_CONTROL+X_OFFSET_BASE, y);
			MEFieldSetAndRefreshFromData(pDoc->pCostumeField, pDoc->pOrigContact, pDoc->pContact);
		}
	} else {
		MEFieldSafeDestroy(&pDoc->pCostumeField);
	}

	// PetContactList
	if(pDoc->pContact->costumePrefs.eCostumeType == ContactCostumeType_PetContactList)
	{
		y += STANDARD_ROW_HEIGHT;
		pDoc->pCostumeLabel = CERefreshLabel(pDoc->pCostumeLabel, "Pet Contact List", "The pet contact to use for headshot.", X_OFFSET_BASE+X_OFFSET_BASE, 0, y, pDoc->pInfoExpander);
		if(!pDoc->pPetContactField) {
			pDoc->pPetContactField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_ValidatedTextEntry, pDoc->pOrigContact, pDoc->pContact, parse_ContactDef, "UsePetCostume", "PetContactList", "ResourceName");
			CEAddFieldToParent(pDoc->pPetContactField, UI_WIDGET(pDoc->pInfoExpander), X_OFFSET_CONTROL+X_OFFSET_BASE, y, 0, X_PERCENT_SPLIT,UIUnitPercentage, 0, pDoc);
		} else {
			ui_WidgetSetPosition(pDoc->pPetContactField->pUIWidget, X_OFFSET_CONTROL+X_OFFSET_BASE, y);
			MEFieldSetAndRefreshFromData(pDoc->pPetContactField, pDoc->pOrigContact, pDoc->pContact);
		}
	} else {
		MEFieldSafeDestroy(&pDoc->pPetContactField);
	}

	// CritterGroup
	if(pDoc->pContact->costumePrefs.eCostumeType == ContactCostumeType_CritterGroup)
	{
		y += STANDARD_ROW_HEIGHT;
		// Critter Group type
		pDoc->pCritterGroupTypeLabel = CERefreshLabel(pDoc->pCritterGroupTypeLabel, "Critter Group From", "Where the critter group should be gathered from.", X_OFFSET_BASE+X_OFFSET_BASE, 0, y, pDoc->pInfoExpander);
		if(!pDoc->pCritterGroupTypeField) {
			pDoc->pCritterGroupTypeField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pDoc->pOrigContact, pDoc->pContact, parse_ContactDef, "CostumeCritterGroupType", ContactMapVarOverrideTypeEnum);
			CEAddFieldToParent(pDoc->pCritterGroupTypeField, UI_WIDGET(pDoc->pInfoExpander), X_OFFSET_CONTROL+X_OFFSET_BASE, y, 0, X_PERCENT_SPLIT,UIUnitPercentage, 0, pDoc);
		} else {
			ui_WidgetSetPosition(pDoc->pCritterGroupTypeField->pUIWidget, X_OFFSET_CONTROL+X_OFFSET_BASE, y);
			MEFieldSetAndRefreshFromData(pDoc->pCritterGroupTypeField, pDoc->pOrigContact, pDoc->pContact);
		}

		y += STANDARD_ROW_HEIGHT;
		
		// Specified Critter Group
		if(pDoc->pContact->costumePrefs.eCostumeCritterGroupType == ContactMapVarOverrideType_Specified) {
			pDoc->pCostumeLabel = CERefreshLabel(pDoc->pCostumeLabel, "Critter Group", "The specified Critter Group which the contact's costume will be generated from.", X_OFFSET_BASE+X_OFFSET_BASE, 0, y, pDoc->pInfoExpander);
			if(!pDoc->pCritterGroupField) {
				pDoc->pCritterGroupField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_ValidatedTextEntry, pDoc->pOrigContact, pDoc->pContact, parse_ContactDef, "CostumeCritterGroup", "CritterGroup", "resourceName");
				CEAddFieldToParent(pDoc->pCritterGroupField, UI_WIDGET(pDoc->pInfoExpander), X_OFFSET_CONTROL+X_OFFSET_BASE, y, 0, X_PERCENT_SPLIT,UIUnitPercentage, 0, pDoc);
			} else {
				ui_WidgetSetPosition(pDoc->pCritterGroupField->pUIWidget, X_OFFSET_CONTROL+X_OFFSET_BASE, y);
				MEFieldSetAndRefreshFromData(pDoc->pCritterGroupField, pDoc->pOrigContact, pDoc->pContact);
			}
		} else {
			MEFieldSafeDestroy(&pDoc->pCritterGroupField);
		}

		// Critter Group from Map Var
		if(pDoc->pContact->costumePrefs.eCostumeCritterGroupType == ContactMapVarOverrideType_MapVar) {
			pDoc->pCostumeLabel = CERefreshLabel(pDoc->pCostumeLabel, "Map Variable", "The map variable where the Critter Group should be pulled from.", X_OFFSET_BASE+X_OFFSET_BASE, 0, y, pDoc->pInfoExpander);
			if(!pDoc->pCritterMapVarField) {
				pDoc->pCritterMapVarField = MEFieldCreateSimpleDataProvided(kMEFieldType_TextEntry, pDoc->pOrigContact, pDoc->pContact, parse_ContactDef, "CostumeMapVar", NULL, &pDoc->eaVarNames, NULL);
				CEAddFieldToParent(pDoc->pCritterMapVarField, UI_WIDGET(pDoc->pInfoExpander), X_OFFSET_CONTROL+X_OFFSET_BASE, y, 0, X_PERCENT_SPLIT,UIUnitPercentage, 0, pDoc);
			} else {
				ui_WidgetSetPosition(pDoc->pCritterMapVarField->pUIWidget, X_OFFSET_CONTROL+X_OFFSET_BASE, y);
				MEFieldSetAndRefreshFromData(pDoc->pCritterMapVarField, pDoc->pOrigContact, pDoc->pContact);
			}
		} else {
			MEFieldSafeDestroy(&pDoc->pCritterMapVarField);
		}

		// Critter Group Identifier
		pDoc->pCritterGroupIdentifierLabel = CERefreshLabel(pDoc->pCritterGroupIdentifierLabel, "Name", "A name for this costume.  To use this same costume again, use the same critter group and same name.", X_OFFSET_AUDIO, X_PERCENT_SPLIT, y, pDoc->pInfoExpander); 
		if (!pDoc->pCritterGroupIdentifierField) {
			pDoc->pCritterGroupIdentifierField = MEFieldCreateSimple(kMEFieldType_TextEntry, pDoc->pOrigContact, pDoc->pContact, parse_ContactDef, "CostumeIdentifier");
			CEAddFieldToParent(pDoc->pCritterGroupIdentifierField, UI_WIDGET(pDoc->pInfoExpander), X_OFFSET_AUDIO+45, y, X_PERCENT_SPLIT, 0.20, UIUnitPercentage, 0, pDoc);
		} else {
			ui_WidgetSetPositionEx(pDoc->pCritterGroupIdentifierField->pUIWidget, X_OFFSET_AUDIO+45, y, X_PERCENT_SPLIT, 0, UITopLeft);
			MEFieldSetAndRefreshFromData(pDoc->pCritterGroupIdentifierField, pDoc->pOrigContact, pDoc->pContact);
		}
	} else {
		ui_WidgetQueueFreeAndNull(&pDoc->pCritterGroupTypeLabel);
		ui_WidgetQueueFreeAndNull(&pDoc->pCritterGroupIdentifierLabel);
		MEFieldSafeDestroy(&pDoc->pCritterMapVarField);
		MEFieldSafeDestroy(&pDoc->pCritterGroupTypeField);
		MEFieldSafeDestroy(&pDoc->pCritterGroupField);
		MEFieldSafeDestroy(&pDoc->pCritterGroupIdentifierField);
	}

	y += STANDARD_ROW_HEIGHT;

	pDoc->pHeadshotLabel = CERefreshLabel(pDoc->pHeadshotLabel, "Headshot Style", "The headshot style to use for this contact.", X_OFFSET_BASE, 0, y, pDoc->pInfoExpander); 
	if (!pDoc->pHeadshotStyleField) {
		pDoc->pHeadshotStyleField = MEFieldCreateSimpleDictionary(kMEFieldType_ValidatedTextEntry, pDoc->pOrigContact, pDoc->pContact, parse_ContactDef, "HeadshotStyleDef", "HeadshotStyleDef", parse_HeadshotStyleDef, "Name");
		CEAddFieldToParent(pDoc->pHeadshotStyleField, UI_WIDGET(pDoc->pInfoExpander), X_OFFSET_CONTROL, y, 0, X_PERCENT_SPLIT, UIUnitPercentage, 0, pDoc);
		eaPush(&pDoc->eaDocFields, pDoc->pHeadshotStyleField);
	} else {
		ui_WidgetSetPosition(pDoc->pHeadshotStyleField->pUIWidget, X_OFFSET_CONTROL, y);
	}

	y += STANDARD_ROW_HEIGHT;

	pDoc->pCutSceneLabel = CERefreshLabel(pDoc->pCutSceneLabel, "Cut-scene", "The cut-scene played for this contact.", X_OFFSET_BASE, 0, y, pDoc->pInfoExpander); 
	if (!pDoc->pCutSceneField) {
		pDoc->pCutSceneField = MEFieldCreateSimpleDictionary(kMEFieldType_ValidatedTextEntry, pDoc->pOrigContact, pDoc->pContact, parse_ContactDef, "CutSceneDef", "CutScene", parse_CutsceneDef, "Name");
		CEAddFieldToParent(pDoc->pCutSceneField, UI_WIDGET(pDoc->pInfoExpander), X_OFFSET_CONTROL, y, 0, X_PERCENT_SPLIT, UIUnitPercentage, 0, pDoc);
		eaPush(&pDoc->eaDocFields, pDoc->pCutSceneField);
	} else {
		ui_WidgetSetPosition(pDoc->pCutSceneField->pUIWidget, X_OFFSET_CONTROL, y);
	}

	y += STANDARD_ROW_HEIGHT;

	pDoc->pSourceTypeLabel = CERefreshLabel(pDoc->pSourceTypeLabel, "Cam Src Type", "The camera source type", X_OFFSET_BASE, 0, y, pDoc->pInfoExpander);
	if (!pDoc->pSourceTypeField) {
		pDoc->pSourceTypeField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pDoc->pOrigContact, pDoc->pContact, parse_ContactDef, "SourceType", ContactSourceTypeEnum);
		CEAddFieldToParent(pDoc->pSourceTypeField, UI_WIDGET(pDoc->pInfoExpander), X_OFFSET_CONTROL, y, 0, X_PERCENT_SPLIT, UIUnitPercentage, 0, pDoc);
		eaPush(&pDoc->eaDocFields, pDoc->pSourceTypeField);
	} else {
		ui_WidgetSetPosition(pDoc->pSourceTypeField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pDoc->pSourceTypeField, pDoc->pOrigContact, pDoc->pContact);
	}

	y += STANDARD_ROW_HEIGHT;

	if (pDoc->pContact->eSourceType != ContactSourceType_None)
	{
		const char *pchSourceTypeLabelText;

		switch (pDoc->pContact->eSourceType)
		{
			xcase ContactSourceType_Clicky:
				pchSourceTypeLabelText = "Cam Src Clicky";
			xcase ContactSourceType_NamedPoint:
				pchSourceTypeLabelText = "Cam Src Nmd Pnt";
			xcase ContactSourceType_Encounter:
				pchSourceTypeLabelText = "Cam Src Enc";
			xdefault:
				pchSourceTypeLabelText = "Cam Src Unknown";
				Alertf("Contact editor encountered an unsupported contact source type and needs to be updated. Contact the contact editor programmers.");
		}

		pDoc->pSourceNameLabel = CERefreshLabel(pDoc->pSourceNameLabel, pchSourceTypeLabelText, "The camera source name", X_OFFSET_BASE, 0, y, pDoc->pInfoExpander);
		if (!pDoc->pSourceNameField) {
			pDoc->pSourceNameField = MEFieldCreateSimple(kMEFieldType_TextEntry, pDoc->pOrigContact, pDoc->pContact, parse_ContactDef, "SourceName");
			CEAddFieldToParent(pDoc->pSourceNameField, UI_WIDGET(pDoc->pInfoExpander), X_OFFSET_CONTROL, y, 0, X_PERCENT_SPLIT, UIUnitPercentage, 0, pDoc);
			eaPush(&pDoc->eaDocFields, pDoc->pSourceNameField);
		} else {
			ui_WidgetSetPosition(pDoc->pSourceNameField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pDoc->pSourceNameField, pDoc->pOrigContact, pDoc->pContact);
		}

		y += STANDARD_ROW_HEIGHT;
	}
	else
	{
		if (pDoc->pSourceNameLabel)
			ui_WidgetQueueFreeAndNull(&pDoc->pSourceNameLabel);
		if (pDoc->pSourceNameField)
		{
			eaFindAndRemove(&pDoc->eaDocFields, pDoc->pSourceNameField);
			MEFieldSafeDestroy(&pDoc->pSourceNameField);
		}
	}

	if (pDoc->pContact->eSourceType == ContactSourceType_Encounter)
	{
		pDoc->pSourceSecondaryNameLabel = CERefreshLabel(pDoc->pSourceSecondaryNameLabel, "Cam Src Actor", "The actor name", X_OFFSET_BASE, 0, y, pDoc->pInfoExpander);
		if (!pDoc->pSourceSecondaryNameField) {
			pDoc->pSourceSecondaryNameField = MEFieldCreateSimple(kMEFieldType_TextEntry, pDoc->pOrigContact, pDoc->pContact, parse_ContactDef, "SourceSecondaryName");
			CEAddFieldToParent(pDoc->pSourceSecondaryNameField, UI_WIDGET(pDoc->pInfoExpander), X_OFFSET_CONTROL, y, 0, X_PERCENT_SPLIT, UIUnitPercentage, 0, pDoc);
			eaPush(&pDoc->eaDocFields, pDoc->pSourceSecondaryNameField);
		} else {
			ui_WidgetSetPosition(pDoc->pSourceSecondaryNameField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pDoc->pSourceSecondaryNameField, pDoc->pOrigContact, pDoc->pContact);
		}

		y += STANDARD_ROW_HEIGHT;
	}
	else
	{
		if (pDoc->pSourceSecondaryNameLabel)
			ui_WidgetQueueFreeAndNull(&pDoc->pSourceSecondaryNameLabel);
		if (pDoc->pSourceSecondaryNameField)
		{
			eaFindAndRemove(&pDoc->eaDocFields, pDoc->pSourceSecondaryNameField);
			MEFieldSafeDestroy(&pDoc->pSourceSecondaryNameField);
		}
	}

	// The animlist for the contact
	pDoc->pAnimListLabel = CERefreshLabel(pDoc->pAnimListLabel, "Anim List", "The anim list override to be used as default for this contact. This animation is only seen by the player this contact is talking to.", X_OFFSET_BASE, 0, y, pDoc->pInfoExpander); 
	if (!pDoc->pAnimListField) {
		pDoc->pAnimListField = MEFieldCreateSimpleDictionary(kMEFieldType_ValidatedTextEntry, pDoc->pOrigContact, pDoc->pContact, parse_ContactDef, "AnimListToPlay", g_AnimListDict, parse_AIAnimList, "Name");
		CEAddFieldToParent(pDoc->pAnimListField, UI_WIDGET(pDoc->pInfoExpander), X_OFFSET_CONTROL, y, 0, X_PERCENT_SPLIT, UIUnitPercentage, 0, pDoc);
		eaPush(&pDoc->eaDocFields, pDoc->pAnimListField);
	} else {
		ui_WidgetSetPosition(pDoc->pAnimListField->pUIWidget, X_OFFSET_CONTROL, y);
	}

	y += STANDARD_ROW_HEIGHT;


	// Map
	pDoc->pMapLabel = CERefreshLabel(pDoc->pMapLabel, "Map", "The map that the Contact is on.", X_OFFSET_BASE, 0, y, pDoc->pInfoExpander);
	if (!pDoc->pMapNameField) {
		pDoc->pMapNameField = MEFieldCreateSimpleDataProvided(kMEFieldType_TextEntry, pDoc->pOrigContact, pDoc->pContact, parse_ContactDef, "MapName", NULL, &g_GEMapDispNames, NULL);
		CEAddFieldToParent(pDoc->pMapNameField, UI_WIDGET(pDoc->pInfoExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
		eaPush(&pDoc->eaDocFields, pDoc->pMapNameField);
	} else {
		ui_WidgetSetPosition(pDoc->pMapNameField->pUIWidget, X_OFFSET_CONTROL, y);
	}
	y += STANDARD_ROW_HEIGHT;

	// Contact Indicator Override
	pDoc->pContactIndicatorOverrideLabel = CERefreshLabel(pDoc->pContactIndicatorOverrideLabel, "Indicator Override", "If set, this overrides the automatically generated contact indicator", X_OFFSET_BASE, 0, y, pDoc->pInfoExpander);
	if (!pDoc->pContactIndicatorOverrideField) {
		pDoc->pContactIndicatorOverrideField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pDoc->pOrigContact, pDoc->pContact, parse_ContactDef, "ContactIndicatorOverride", ContactIndicatorEnum);
		CEAddFieldToParent(pDoc->pContactIndicatorOverrideField, UI_WIDGET(pDoc->pInfoExpander), X_OFFSET_CONTROL, y, 0, X_PERCENT_SPLIT, UIUnitPercentage, 0, pDoc);
		eaPush(&pDoc->eaDocFields, pDoc->pContactIndicatorOverrideField);
	} else {
		ui_WidgetSetPosition(pDoc->pContactIndicatorOverrideField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pDoc->pContactIndicatorOverrideField, pDoc->pOrigContact, pDoc->pContact);
	}

	y += STANDARD_ROW_HEIGHT;

	pDoc->pHideFromRemoteContactListLabel = CERefreshLabel(pDoc->pHideFromRemoteContactListLabel, "Hide From Remote Contacts", "If true, this will not appear in the remote contact list.", X_OFFSET_BASE, 0, y, pDoc->pInfoExpander);
	if (!pDoc->pHideFromRemoteContactListField) {
		pDoc->pHideFromRemoteContactListField = MEFieldCreateSimple(kMEFieldType_BooleanCombo, pDoc->pOrigContact, pDoc->pContact, parse_ContactDef, "HideFromRemoteContactList");
		CEAddFieldToParent(pDoc->pHideFromRemoteContactListField, UI_WIDGET(pDoc->pInfoExpander), X_OFFSET_CONTROL, y, 0, 100, UIUnitFixed, 0, pDoc);
	} else {
		ui_WidgetSetPosition(pDoc->pHideFromRemoteContactListField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pDoc->pHideFromRemoteContactListField, pDoc->pOrigContact, pDoc->pContact);
	}
	y += STANDARD_ROW_HEIGHT;

	pDoc->pUpdateOptionsLabel = CERefreshLabel(pDoc->pUpdateOptionsLabel, "Update Options Every Tick", "Dialog options will update every tick instead of every 10 seconds", X_OFFSET_BASE, 0, y, pDoc->pInfoExpander);
	if (!pDoc->pUpdateOptionsField) {
		pDoc->pUpdateOptionsField = MEFieldCreateSimple(kMEFieldType_BooleanCombo, pDoc->pOrigContact, pDoc->pContact, parse_ContactDef, "UpdateOptionsEveryTick");
		CEAddFieldToParent(pDoc->pUpdateOptionsField, UI_WIDGET(pDoc->pInfoExpander), X_OFFSET_CONTROL, y, 0, 100, UIUnitFixed, 0, pDoc);
	} else {
		ui_WidgetSetPosition(pDoc->pUpdateOptionsField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pDoc->pUpdateOptionsField, pDoc->pOrigContact, pDoc->pContact);
	}
	y += STANDARD_ROW_HEIGHT;

	if (pDoc->pContact && pDoc->pContact->type == ContactType_SingleDialog) {
		MEFieldSafeDestroy(&pDoc->pContactFlagsField);
		MEFieldSafeDestroy(&pDoc->pSkillTrainerTypeField);
		MEFieldSafeDestroy(&pDoc->pMinigameTypeField);
		MEFieldSafeDestroy(&pDoc->pCharacterClassField);
		MEFieldSafeDestroy(&pDoc->pShowLastPuppetField);
		MEFieldSafeDestroy(&pDoc->pResearchStoreCollectionField);

		ui_ExpanderSetHeight(pDoc->pInfoExpander, y);
		return;
	}
	pDoc->pContactFlagsLabel = CERefreshLabel(pDoc->pContactFlagsLabel, "Contact Flags", "Determines what contact types this contact provides", X_OFFSET_BASE, 0, y, pDoc->pInfoExpander);
	if (!pDoc->pContactFlagsField) {
		pDoc->pContactFlagsField = MEFieldCreateSimpleEnum(kMEFieldType_FlagCombo, pDoc->pOrigContact, pDoc->pContact, parse_ContactDef, "ContactFlags", ContactFlagsEnum);
		CEAddFieldToParent(pDoc->pContactFlagsField, UI_WIDGET(pDoc->pInfoExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
		MEFieldSetChangeCallback(pDoc->pContactFlagsField, CERemoteFieldsChangedCB, pDoc);
	} else {
		ui_WidgetSetPosition(pDoc->pContactFlagsField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pDoc->pContactFlagsField, pDoc->pOrigContact, pDoc->pContact);
	}

	y += STANDARD_ROW_HEIGHT;

	// Skill Trainer type
	pDoc->pSkillTrainerTypeLabel = 	CERefreshLabel(pDoc->pSkillTrainerTypeLabel, "Skill Trainer", "If true, this contact offers an option to change your crafting skill", X_OFFSET_BASE, 0, y, pDoc->pInfoExpander);
	if (!pDoc->pSkillTrainerTypeField) {
		pDoc->pSkillTrainerTypeField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pDoc->pOrigContact, pDoc->pContact, parse_ContactDef, "skillTrainerType", SkillTypeEnum);
		CEAddFieldToParent(pDoc->pSkillTrainerTypeField, UI_WIDGET(pDoc->pInfoExpander), X_OFFSET_CONTROL, y, 0, 100, UIUnitFixed, 0, pDoc);
	} else {
		ui_WidgetSetPosition(pDoc->pSkillTrainerTypeField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pDoc->pSkillTrainerTypeField, pDoc->pOrigContact, pDoc->pContact);
	}

	y += STANDARD_ROW_HEIGHT;


	// Minigame type
	if (contact_IsMinigame(pDoc->pContact))
	{
		pDoc->pMinigameTypeLabel = CERefreshLabel(pDoc->pMinigameTypeLabel, "Minigame", "If true, this contact offers a minigame", X_OFFSET_BASE, 0, y, pDoc->pInfoExpander);
		if (!pDoc->pMinigameTypeField) {
			pDoc->pMinigameTypeField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pDoc->pOrigContact, pDoc->pContact, parse_ContactDef, "MinigameType", MinigameTypeEnum);
			CEAddFieldToParent(pDoc->pMinigameTypeField, UI_WIDGET(pDoc->pInfoExpander), X_OFFSET_CONTROL, y, 0, 100, UIUnitFixed, 0, pDoc);
		} else {
			ui_WidgetSetPosition(pDoc->pMinigameTypeField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pDoc->pMinigameTypeField, pDoc->pOrigContact, pDoc->pContact);
		}
		y += STANDARD_ROW_HEIGHT;
	}
	else
	{
		pDoc->pContact->eMinigameType = kMinigameType_None;
		ui_WidgetQueueFreeAndNull(&pDoc->pMinigameTypeLabel);
		MEFieldSafeDestroy(&pDoc->pMinigameTypeField);
	}

	// Character classes
	if (contact_IsStarshipChooser(pDoc->pContact))
	{
		pDoc->pCharacterClassLabel = CERefreshLabel(pDoc->pCharacterClassLabel, "Class Categories", "Which classes categories are allowed to be displayed", X_OFFSET_BASE, 0, y, pDoc->pInfoExpander);
		if (!pDoc->pCharacterClassField) {
			pDoc->pCharacterClassField = MEFieldCreateSimpleEnum(kMEFieldType_FlagCombo, pDoc->pOrigContact, pDoc->pContact, parse_ContactDef, "AllowedClassCategory", CharClassCategoryEnum);
			CEAddFieldToParent(pDoc->pCharacterClassField, UI_WIDGET(pDoc->pInfoExpander), X_OFFSET_CONTROL, y, 0, 100, UIUnitFixed, 0, pDoc);
		} else {
			ui_WidgetSetPosition(pDoc->pCharacterClassField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pDoc->pCharacterClassField, pDoc->pOrigContact, pDoc->pContact);
		}
		y += STANDARD_ROW_HEIGHT;
	}
	else
	{
		eaiDestroy(&pDoc->pContact->peAllowedClassCategories);
		ui_WidgetQueueFreeAndNull(&pDoc->pCharacterClassLabel);
		MEFieldSafeDestroy(&pDoc->pCharacterClassField);
	}

	// Show last active puppet
	if (contact_IsStarshipChooser(pDoc->pContact))
	{
		pDoc->pShowLastPuppetLabel = CERefreshLabel(pDoc->pShowLastPuppetLabel, "Show Last Puppet", "Allow setting the last active puppet as the active puppet", X_OFFSET_BASE, 0, y, pDoc->pInfoExpander);
		if (!pDoc->pShowLastPuppetField) {
			pDoc->pShowLastPuppetField = MEFieldCreateSimple(kMEFieldType_BooleanCombo, pDoc->pOrigContact, pDoc->pContact, parse_ContactDef, "AllowSwitchToLastActivePuppet");
			CEAddFieldToParent(pDoc->pShowLastPuppetField, UI_WIDGET(pDoc->pInfoExpander), X_OFFSET_CONTROL, y, 0, 100, UIUnitFixed, 0, pDoc);
		} else {
			ui_WidgetSetPosition(pDoc->pShowLastPuppetField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pDoc->pShowLastPuppetField, pDoc->pOrigContact, pDoc->pContact);
		}
		y += STANDARD_ROW_HEIGHT;
	}
	else
	{
		pDoc->pContact->bAllowSwitchToLastActivePuppet = false;
		ui_WidgetQueueFreeAndNull(&pDoc->pShowLastPuppetLabel);
		MEFieldSafeDestroy(&pDoc->pShowLastPuppetField);
	}

	// ImageMenu fields
	if (contact_IsImageMenu(pDoc->pContact))
	{
		pDoc->pShowLastPuppetLabel = CERefreshLabel(pDoc->pShowLastPuppetLabel, "Show Last Puppet", "Allow setting the last active puppet as the active puppet", X_OFFSET_BASE, 0, y, pDoc->pInfoExpander);
		if (!pDoc->pShowLastPuppetField) {
			pDoc->pShowLastPuppetField = MEFieldCreateSimple(kMEFieldType_BooleanCombo, pDoc->pOrigContact, pDoc->pContact, parse_ContactDef, "AllowSwitchToLastActivePuppet");
			CEAddFieldToParent(pDoc->pShowLastPuppetField, UI_WIDGET(pDoc->pInfoExpander), X_OFFSET_CONTROL, y, 0, 100, UIUnitFixed, 0, pDoc);
		} else {
			ui_WidgetSetPosition(pDoc->pShowLastPuppetField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pDoc->pShowLastPuppetField, pDoc->pOrigContact, pDoc->pContact);
		}
		y += STANDARD_ROW_HEIGHT;
	}
	else
	{
		pDoc->pContact->bAllowSwitchToLastActivePuppet = false;
		ui_WidgetQueueFreeAndNull(&pDoc->pShowLastPuppetLabel);
		MEFieldSafeDestroy(&pDoc->pShowLastPuppetField);
	}

	// Update research store field
	if (eaSize(&pDoc->pContact->storeCollections))
	{
		pDoc->pResearchStoreCollectionLabel = CERefreshLabel(pDoc->pResearchStoreCollectionLabel, "Research Collection", "If true, this is a research store collection.", X_OFFSET_BASE, 0, y, pDoc->pInfoExpander);
		if (!pDoc->pResearchStoreCollectionField) {
			pDoc->pResearchStoreCollectionField = MEFieldCreateSimple(kMEFieldType_BooleanCombo, pDoc->pOrigContact, pDoc->pContact, parse_ContactDef, "IsResearchStoreCollection");
			CEAddFieldToParent(pDoc->pResearchStoreCollectionField, UI_WIDGET(pDoc->pInfoExpander), X_OFFSET_CONTROL, y, 0, 100, UIUnitFixed, 0, pDoc);
		} else {
			ui_WidgetSetPosition(pDoc->pResearchStoreCollectionField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pDoc->pResearchStoreCollectionField, pDoc->pOrigContact, pDoc->pContact);
		}
		y += STANDARD_ROW_HEIGHT;
	}
	else
	{
		pDoc->pContact->bIsResearchStoreCollection = false;
		ui_WidgetQueueFreeAndNull(&pDoc->pResearchStoreCollectionLabel);
		MEFieldSafeDestroy(&pDoc->pResearchStoreCollectionField);
	}

	ui_ExpanderSetHeight(pDoc->pInfoExpander, y);
}

static int CERefreshSpecialActionGroup(CESpecialActionGroup *pGroup, UIExpander *pExpander, F32 y, int index, SpecialDialogAction ***peaActions, SpecialDialogAction *pAction, SpecialDialogAction *pOldAction)
{
	char buf[260];

	pGroup->index = index;
	pGroup->peaActions = peaActions;

	// Update down button
	if (index != eaSize(peaActions) - 1) 
	{
		if (!pGroup->pDownButton)
		{
			pGroup->pDownButton = ui_ButtonCreate("Dn", 0, 0, CEMoveSpecialDialogActionDownCB, pGroup);
			ui_WidgetSetWidth(UI_WIDGET(pGroup->pDownButton), 25);
			ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pDownButton), 190, y, 0, 0, UITopRight);
			ui_ExpanderAddChild(pExpander, pGroup->pDownButton);
		} 
		else
		{
			ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pDownButton), 190, y, 0, 0, UITopRight);
		}
	} 
	else if (pGroup->pDownButton)
	{
		ui_WidgetQueueFree((UIWidget*)pGroup->pDownButton);
		pGroup->pDownButton = NULL;
	}

	// Update up button
	if (index != 0) 
	{
		if (!pGroup->pUpButton) 
		{
			pGroup->pUpButton = ui_ButtonCreate("Up", 0, 0, CEMoveSpecialDialogActionUpCB, pGroup);
			ui_WidgetSetWidth(UI_WIDGET(pGroup->pUpButton), 25);
			ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pUpButton), 160, y, 0, 0, UITopRight);
			ui_ExpanderAddChild(pExpander, pGroup->pUpButton);
		}
		else
		{
			ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pUpButton), 160, y, 0, 0, UITopRight);
		}
	} 
	else if (pGroup->pUpButton) 
	{
		ui_WidgetQueueFree((UIWidget*)pGroup->pUpButton);
		pGroup->pUpButton = NULL;
	}

	// Update button
	if (!pGroup->pRemoveButton) {
		pGroup->pRemoveButton = ui_ButtonCreate("Delete Action", 35, y, CERemoveSpecialActionCB, pGroup);
		ui_WidgetSetWidth(UI_WIDGET(pGroup->pRemoveButton), 120);
		ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pRemoveButton), 35, y, 0, 0, UITopRight);
		ui_ExpanderAddChild(pExpander, pGroup->pRemoveButton);
	} else {
		ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pRemoveButton), 35, y, 0, 0, UITopRight);
	}

	sprintf(buf, "Action Button #%d", index+1);
	pGroup->pTitleLabel = CERefreshLabel(pGroup->pTitleLabel, buf, NULL, X_OFFSET_BASE, 0, y, pExpander);

	y += STANDARD_ROW_HEIGHT;

	// Update text message field
	pGroup->pMessageLabel = CERefreshLabel(pGroup->pMessageLabel, "Display Text", "The text to display on the button for this action", X_OFFSET_BASE+15, 0, y, pExpander);
	if (!pGroup->pMessageField) {
		pGroup->pMessageField = MEFieldCreateSimple(kMEFieldType_Message, pOldAction ? &pOldAction->displayNameMesg : NULL, &pAction->displayNameMesg, parse_DisplayMessage, "editorCopy");
		GEAddFieldToParent(pGroup->pMessageField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, X_PERCENT_SPLIT, UIUnitPercentage, 0, pGroup->pCommonCallbackParams->pFieldChangeFunc, pGroup->pCommonCallbackParams->pFieldPreChangeFunc, pGroup->pCommonCallbackParams->pFieldChangeData);
	} else {
		ui_WidgetSetPosition(pGroup->pMessageField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pMessageField, pOldAction ? &pOldAction->displayNameMesg : NULL, &pAction->displayNameMesg);
	}

	y += STANDARD_ROW_HEIGHT;

	// Update dialog formatter field
	pGroup->pDialogFormatterLabel = CERefreshLabel(pGroup->pDialogFormatterLabel, "Text Formatter", "Used by UI to format the display text", X_OFFSET_BASE+15, 0, y, pExpander);
	if (pGroup->pDialogFormatterField == NULL)
	{
		pGroup->pDialogFormatterField = MEFieldCreateSimpleDictionary(kMEFieldType_ValidatedTextEntry, pOldAction, pAction, parse_SpecialDialogAction, "DisplayNameFormatter", g_hContactDialogFormatterDefDictionary, parse_ContactDialogFormatterDef, "Name");
		GEAddFieldToParent(pGroup->pDialogFormatterField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, X_PERCENT_SPLIT, UIUnitPercentage, 0, pGroup->pCommonCallbackParams->pFieldChangeFunc, pGroup->pCommonCallbackParams->pFieldPreChangeFunc, pGroup->pCommonCallbackParams->pFieldChangeData);
	} else {
		ui_WidgetSetPosition(pGroup->pDialogFormatterField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pDialogFormatterField, pOldAction, pAction);
	}

	y += STANDARD_ROW_HEIGHT;

	// Update expression field
	pGroup->pExpressionLabel = CERefreshLabel(pGroup->pExpressionLabel, "Visible Expr", "Optional expression.  If true, this dialog can be shown.", X_OFFSET_BASE+15, 0, y, pExpander);
	if (!pGroup->pExpressionField) {
		pGroup->pExpressionField = MEFieldCreateSimple(kMEFieldTypeEx_Expression, pOldAction, pAction, parse_SpecialDialogAction, "Condition");
		GEAddFieldToParent(pGroup->pExpressionField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, X_PERCENT_SPLIT, UIUnitPercentage, 0, pGroup->pCommonCallbackParams->pFieldChangeFunc, pGroup->pCommonCallbackParams->pFieldPreChangeFunc, pGroup->pCommonCallbackParams->pFieldChangeData);
	} else {
		ui_WidgetSetPosition(pGroup->pExpressionField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pExpressionField, pOldAction, pAction);
	}

	y += STANDARD_ROW_HEIGHT;

	// Update the can choose expression field
	pGroup->pCanChooseExpressionLabel = CERefreshLabel(pGroup->pCanChooseExpressionLabel, "Interact Expr", "Optional expression used to determine if the player can choose this dialog action. If left blank, player can choose the action.", X_OFFSET_BASE+15, 0, y, pExpander);
	if (!pGroup->pCanChooseExpressionField) 
	{
		pGroup->pCanChooseExpressionField = MEFieldCreateSimple(kMEFieldTypeEx_Expression, pOldAction, pAction, parse_SpecialDialogAction, "CanChooseCondition");
		GEAddFieldToParent(pGroup->pCanChooseExpressionField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, X_PERCENT_SPLIT, UIUnitPercentage, 0, pGroup->pCommonCallbackParams->pFieldChangeFunc, pGroup->pCommonCallbackParams->pFieldPreChangeFunc, pGroup->pCommonCallbackParams->pFieldChangeData);
	} else {
		ui_WidgetSetPosition(pGroup->pCanChooseExpressionField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pCanChooseExpressionField, pOldAction, pAction);
	}

	y += STANDARD_ROW_HEIGHT;

	// Update target dialog
	pGroup->pDialogLabel = CERefreshLabel(pGroup->pDialogLabel, "Next Dialog to Show", "The dialog to go to next.  If blank, the contact exits after this action.", X_OFFSET_BASE+15, 0, y, pExpander);
	if (!pGroup->pDialogField) {
		pGroup->pDialogField = MEFieldCreateSimpleDataProvided(kMEFieldType_ValidatedTextEntry, pOldAction, pAction, parse_SpecialDialogAction, "TargetDialog", NULL, NULL, NULL);
		GEAddFieldToParent(pGroup->pDialogField, UI_WIDGET(pExpander), X_OFFSET_CONTROL+40, y, 0, X_PERCENT_SPLIT, UIUnitPercentage, 0, pGroup->pCommonCallbackParams->pFieldChangeFunc, pGroup->pCommonCallbackParams->pFieldPreChangeFunc, pGroup->pCommonCallbackParams->pFieldChangeData);
	} else {
		ui_WidgetSetPosition(pGroup->pDialogField->pUIWidget, X_OFFSET_CONTROL+40, y);
		MEFieldSetAndRefreshFromData(pGroup->pDialogField, pOldAction, pAction);
	}

	y += STANDARD_ROW_HEIGHT;

	// Update target dialog
	pGroup->pCompleteLabel = CERefreshLabel(pGroup->pCompleteLabel, "Send Complete Event", "If true, a complete event is sent after this action is performed.", X_OFFSET_BASE+15, 0, y, pExpander);
	if (!pGroup->pCompleteField) {
		pGroup->pCompleteField = MEFieldCreateSimpleDataProvided(kMEFieldType_BooleanCombo, pOldAction, pAction, parse_SpecialDialogAction, "SendComplete", NULL, NULL, NULL);
		GEAddFieldToParent(pGroup->pCompleteField, UI_WIDGET(pExpander), X_OFFSET_CONTROL+40, y, 0, X_PERCENT_SPLIT, UIUnitPercentage, 0, pGroup->pCommonCallbackParams->pFieldChangeFunc, pGroup->pCommonCallbackParams->pFieldPreChangeFunc, pGroup->pCommonCallbackParams->pFieldChangeData);
	} else {
		ui_WidgetSetPosition(pGroup->pCompleteField->pUIWidget, X_OFFSET_CONTROL+40, y);
		MEFieldSetAndRefreshFromData(pGroup->pCompleteField, pOldAction, pAction);
	}

	y += STANDARD_ROW_HEIGHT;

	// End dialog flag
	pGroup->pEndDialogLabel = CERefreshLabel(pGroup->pEndDialogLabel, "End Dialog", "If this is checked, the player is kicked out of the dialog once this action is selected.", X_OFFSET_BASE + 15, 0, y, pExpander);
	if (pGroup->pEndDialogField == NULL) 
	{
		pGroup->pEndDialogField = MEFieldCreateSimple(kMEFieldType_Check, pOldAction, pAction, parse_SpecialDialogAction, "EndDialog");
		GEAddFieldToParent(pGroup->pEndDialogField, UI_WIDGET(pExpander), X_OFFSET_CONTROL + 40, y, 0, 20, UIUnitFixed, 3, pGroup->pCommonCallbackParams->pFieldChangeFunc, pGroup->pCommonCallbackParams->pFieldPreChangeFunc, pGroup->pCommonCallbackParams->pFieldChangeData);
	} 
	else 
	{
		ui_WidgetSetPosition(pGroup->pEndDialogField->pUIWidget, X_OFFSET_CONTROL + 40, y);
		MEFieldSetAndRefreshFromData(pGroup->pEndDialogField, pOldAction, pAction);
	}

	y += STANDARD_ROW_HEIGHT;

	// Update action button
	pGroup->pActionLabel = CERefreshLabel(pGroup->pActionLabel, "Game Actions", "Game actions to execute when this option is selected.", X_OFFSET_BASE+15, 0, y, pExpander);
	if (!pGroup->pActionButton) {
		pGroup->pActionButton = ui_GameActionEditButtonCreate(NULL, &pAction->actionBlock, pOldAction ? &pOldAction->actionBlock : NULL, CESpecialGameActionChangeCB, NULL, pGroup);
		ui_WidgetSetWidth(UI_WIDGET(pGroup->pActionButton), 150);
		ui_WidgetSetPosition(UI_WIDGET(pGroup->pActionButton), X_OFFSET_CONTROL+40, y);
		ui_ExpanderAddChild(pExpander, pGroup->pActionButton);
	} else {
		ui_WidgetSetPosition(UI_WIDGET(pGroup->pActionButton), X_OFFSET_CONTROL+40, y);
		ui_GameActionEditButtonSetData(pGroup->pActionButton, &pAction->actionBlock, pOldAction ? &pOldAction->actionBlock : NULL);
	}

	y += STANDARD_ROW_HEIGHT;

	return y;
}

int CERefreshSpecialActionBlockGroup( CESpecialActionBlockGroup *pGroup, CECommonCallbackParams *pCommonCallbackParams, UIExpander *pExpander, F32 y, int index, SpecialActionBlock ***peaActionBlocks, void ***peaActionBlockWrappers, SpecialActionBlock *pActionBlock, SpecialActionBlock *pOldActionBlock )
{
	int numActions = 0;
	int i = 0;
	F32 fInitialYPos = y;

	if (pGroup->pPane == NULL)
	{
		pGroup->pPane = ui_PaneCreate(0.f, y, 0.f, 0.f, UIUnitPercentage, UIUnitPercentage, 0);
		pGroup->pPane->invisible = true;
		ui_WidgetSkin(UI_WIDGET(pGroup->pPane), s_ContactEditorLook.pSpecialDialogPaneSkin);
		ui_ExpanderAddChild(pExpander, pGroup->pPane);
	}
	else
	{
		ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pPane), 0.f, y, 0, 0, UITopLeft);
	}

	pGroup->index = index;
	if(peaActionBlocks) {
		pGroup->peaSpecialActionBlocks = peaActionBlocks;
	}
	if(peaActionBlockWrappers) {
		pGroup->peaSpecialActionBlockWrappers = peaActionBlockWrappers;
	}

	// Update remove button
	if (!pGroup->pRemoveButton) {
		pGroup->pRemoveButton = ui_ButtonCreate("Delete Action Block", X_OFFSET_BASE, y, CERemoveSpecialActionBlockCB, pGroup);
		ui_WidgetSetWidth(UI_WIDGET(pGroup->pRemoveButton), 150);
		ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pRemoveButton), X_OFFSET_BASE, y, 0, 0, UITopLeft);
		ui_ExpanderAddChild(pExpander, pGroup->pRemoveButton);
	} else {
		ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pRemoveButton), X_OFFSET_BASE, y, 0, 0, UITopLeft);
	}

	y += STANDARD_ROW_HEIGHT;

	// Update logical name
	pGroup->pNameLabel = CERefreshLabel(pGroup->pNameLabel, "Name", "Each special action block must have a unique name", X_OFFSET_BASE, 0, y, pExpander);
	if (!pGroup->pNameField) {
		pGroup->pNameField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOldActionBlock, pActionBlock, parse_SpecialActionBlock, "Name");
		GEAddFieldToParent(pGroup->pNameField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, X_PERCENT_SPLIT, UIUnitPercentage, 0, pGroup->pCommonCallbackParams->pFieldChangeFunc, pGroup->pCommonCallbackParams->pFieldPreChangeFunc, pGroup->pCommonCallbackParams->pFieldChangeData);
	} else {
		ui_WidgetSetPosition(pGroup->pNameField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pNameField, pOldActionBlock, pActionBlock);
	}

	y += STANDARD_ROW_HEIGHT;

	// Update actions
	numActions = eaSize(&pActionBlock->dialogActions);
	for(i = 0; i < numActions; ++i) {
		SpecialDialogAction *pAction = pActionBlock->dialogActions[i];
		SpecialDialogAction *pOldAction;

		if (pOldActionBlock && (eaSize(&pOldActionBlock->dialogActions) > i)) {
			pOldAction = pOldActionBlock->dialogActions[i];
		} else {
			pOldAction = NULL;
		}
		while (i >= eaSize(&pGroup->eaSpecialActionGroups)) {
			CESpecialActionGroup *pActionGroup = CECreateSpecialActionGroup(pCommonCallbackParams);
			eaPush(&pGroup->eaSpecialActionGroups, pActionGroup);
		}
		y = CERefreshSpecialActionGroup(pGroup->eaSpecialActionGroups[i], pExpander, y, i, &pActionBlock->dialogActions, pAction, pOldAction);
	}

	// Put in Add Action button
	if (!pGroup->pAddActionButton) {
		pGroup->pAddActionButton = ui_ButtonCreate("Add Action", 5, y, CEAddSpecialActionToBlockCB, pGroup);
		ui_WidgetSetWidth(UI_WIDGET(pGroup->pAddActionButton), 100);
		ui_WidgetSetPosition(UI_WIDGET(pGroup->pAddActionButton), X_OFFSET_BASE, y);
		ui_ExpanderAddChild(pExpander, pGroup->pAddActionButton);
	} else {
		ui_WidgetSetPosition(UI_WIDGET(pGroup->pAddActionButton), X_OFFSET_BASE, y);
	}
	
	y += STANDARD_ROW_HEIGHT;

	// Set the final dimensions of the pane
	ui_WidgetSetDimensionsEx(UI_WIDGET(pGroup->pPane), 1.f, y - fInitialYPos, UIUnitPercentage, UIUnitFixed);

	return y;
}

static bool CEDialogFlowWindowGetDialogNodeNameForPrefStoreBySpecialDialogName(SA_PARAM_NN_VALID DialogFlowWindowInfo *pInfo, SA_PARAM_NN_STR const char *pchSpecialDialogName, SA_PARAM_NN_STR char **pestrNameOut)
{
	if (pInfo && pchSpecialDialogName && pchSpecialDialogName[0] && pestrNameOut)
	{
		const char *pchContactName = NULL;
		if (pInfo->pContactDoc)
		{
			pchContactName = pInfo->pContactDoc->pContact->name;
		}
		else if (pInfo->pMissionDoc && pInfo->pchContactName && pInfo->pchContactName[0])
		{
			pchContactName = pInfo->pchContactName;
		}
		if (pchContactName)
		{
			estrPrintf(pestrNameOut, "SD_%s_%s", pchContactName, pchSpecialDialogName);
			return true;
		}
	}
	return false;
}

static bool CEDialogFlowWindowGetDialogNodeNameForPrefStore(SA_PARAM_NN_VALID ContactDialogUINode *pNode, char **pestrNameOut)
{
	if (pNode && pNode->pInfo && pestrNameOut)
	{
		const char *pchContactName = NULL;
		if (pNode->pInfo->pContactDoc)
		{
			pchContactName = pNode->pInfo->pContactDoc->pContact->name;
		}
		else if (pNode->pInfo->pMissionDoc && pNode->pInfo->pchContactName && pNode->pInfo->pchContactName[0])
		{
			pchContactName = pNode->pInfo->pchContactName;
		}

		if (pchContactName)
		{
			if (pNode->bIsDialogRoot)
			{
				estrPrintf(pestrNameOut, "SD_%s__[DialogRoot]_", pchContactName);
				return true;
			}
			else if (pNode->pMissionOffer)
			{
				MissionDef *pMissionDef = GET_REF(pNode->pMissionOffer->missionDef);
				if (pMissionDef)
				{
					estrPrintf(pestrNameOut, "MO_%s_%s", pchContactName, pMissionDef->name);
					return true;
				}
			}
			else
			{
				estrPrintf(pestrNameOut, "SD_%s_%s", pchContactName, pNode->pchSpecialDialogName);
				return true;
			}
		}
	}
	return false;
}



// Stores the dialog node window positions
static void CESaveDialogNodeWindowPositions(SA_PARAM_NN_VALID DialogFlowWindowInfo *pInfo)
{
	char *estrWindowName = NULL;

	if (pInfo == NULL)
		return;	

	FOR_EACH_IN_EARRAY_FORWARDS(pInfo->eaDialogNodes, ContactDialogUINode, pDialogNode)
	{
		if (pDialogNode)
		{
			if (CEDialogFlowWindowGetDialogNodeNameForPrefStore(pDialogNode, &estrWindowName))				
			{
				if (pDialogNode->pInfo->pContactDoc)
				{
					EditorPrefStoreWindowPosition(CONTACT_EDITOR, "Window Position", estrWindowName, &pDialogNode->pGraphNode->window);
				}
				else
				{
					EditorPrefStoreWindowPosition(MISSION_EDITOR, "Window Position", estrWindowName, &pDialogNode->pGraphNode->window);
				}				
			}			
		}
	}
	FOR_EACH_END

	estrDestroy(&estrWindowName);
}

// Restores the dialog node window positions
static void CERestoreDialogNodeWindowPositions(SA_PARAM_NN_VALID DialogFlowWindowInfo *pInfo)
{
	if (pInfo)
	{
		char *estrDialogNameForPrefStore = NULL;

		// Restore stored positions
		FOR_EACH_IN_EARRAY_FORWARDS(pInfo->eaDialogNodes, ContactDialogUINode, pDialogNode)
		{
			if (CEDialogFlowWindowGetDialogNodeNameForPrefStore(pDialogNode, &estrDialogNameForPrefStore))
			{
				if (pDialogNode->pInfo->pContactDoc)
				{
					EditorPrefGetWindowPositionIgnoreDimensions(CONTACT_EDITOR, "Window Position", estrDialogNameForPrefStore, &pDialogNode->pGraphNode->window);
				}
				else if (pDialogNode->pInfo->pMissionDoc)
				{
					EditorPrefGetWindowPositionIgnoreDimensions(MISSION_EDITOR, "Window Position", estrDialogNameForPrefStore, &pDialogNode->pGraphNode->window);
				}
			}
		}
		FOR_EACH_END

		estrDestroy(&estrDialogNameForPrefStore);
	}
}

// Updates the spline colors for an individual node
static void CEUpdateSplineColorsForNode(SA_PARAM_NN_VALID ContactDialogUINode *pDialogNode, Color inColor, Color outColor, F32 lineScale)
{
	devassert(pDialogNode);

	if (pDialogNode == NULL)
		return;

	// Incoming connections
	FOR_EACH_IN_EARRAY_FORWARDS(pDialogNode->eaIncomingPairedBoxes, UIPairedBox, pPairedBox)
	{
		if(pPairedBox)
		{
			pPairedBox->color = inColor;
			if(pPairedBox->line)
			{
				pPairedBox->line->lineScale = lineScale;
			}
			if (pPairedBox->otherBox)
			{
				pPairedBox->otherBox->color = inColor;
				if(pPairedBox->otherBox->line)
				{
					pPairedBox->otherBox->line->lineScale = lineScale;
				}
			}
		}
	}
	FOR_EACH_END

	// Outgoing connections
	FOR_EACH_IN_EARRAY_FORWARDS(pDialogNode->eaOutgoingPairedBoxes, UIPairedBox, pPairedBox)
	{
		if(pPairedBox)
		{
			pPairedBox->color = outColor;
			if(pPairedBox->line)
			{
				pPairedBox->line->lineScale = lineScale;
			}
			if (pPairedBox->otherBox)
			{
				pPairedBox->otherBox->color = outColor;
				if(pPairedBox->otherBox->line)
				{
					pPairedBox->otherBox->line->lineScale = lineScale;
				}
			}
		}
	}
	FOR_EACH_END
}

// Updates the spline colors for all nodes
static void CEUpdateSplineColors(SA_PARAM_NN_VALID DialogFlowWindowInfo *pInfo)
{
	static Color blueColor = { 0x00, 0x66, 0xcc, 0xFF };
	static Color orangeColor = { 0xcc, 0x66, 0x00, 0xFF };
	static Color grayColor = { 0x80, 0x80, 0x80, 0xAA };

	devassert(pInfo);

	if (pInfo == NULL)
		return;

	// Reset all spline colors to gray
	FOR_EACH_IN_EARRAY_FORWARDS(pInfo->eaDialogNodes, ContactDialogUINode, pDialogNode)
	{
		CEUpdateSplineColorsForNode(pDialogNode, grayColor, grayColor, 2.0);
	}
	FOR_EACH_END

	// set selected colors
	FOR_EACH_IN_EARRAY_FORWARDS(pInfo->eaSelectedDialogNodes, ContactDialogUINode, pDialogNode)
	{
		CEUpdateSplineColorsForNode(pDialogNode, orangeColor, blueColor, 4.0);
	}
	FOR_EACH_END
}

// Finds the dialog root node
static ContactDialogUINode *CEGetRootDialogUINode(SA_PARAM_NN_VALID DialogFlowWindowInfo *pInfo)
{
	if (pInfo == NULL)
		return NULL;

	FOR_EACH_IN_EARRAY_FORWARDS(pInfo->eaDialogNodes, ContactDialogUINode, pContactDialogNode)
	{
		if (pContactDialogNode->bIsDialogRoot)
			return pContactDialogNode;
	}
	FOR_EACH_END

	return NULL;
}

// Finds a dialog node matching the special dialog name
static ContactDialogUINode *CEDialogUINodeFromName(DialogFlowWindowInfo *pInfo, const char *pchSpecialDialogName)
{
	char *estrStrippedName = NULL;

	if (pInfo == NULL || pchSpecialDialogName == NULL)
		return NULL;	

	estrStackCreate(&estrStrippedName);
	estrCopy2(&estrStrippedName, pchSpecialDialogName);

	// Strip the mission name
	if (strchr(pchSpecialDialogName, '/'))
	{
		estrRemoveUpToFirstOccurrence(&estrStrippedName, '/');
	}		

	FOR_EACH_IN_EARRAY_FORWARDS(pInfo->eaDialogNodes, ContactDialogUINode, pContactDialogNode)
	{
		if ((pContactDialogNode->pMissionOffer && stricmp(pContactDialogNode->pMissionOffer->pchSpecialDialogName, estrStrippedName) == 0) ||
			(pContactDialogNode->pMissionOffer == NULL && stricmp(pContactDialogNode->pchSpecialDialogName, estrStrippedName) == 0))
		{
			estrDestroy(&estrStrippedName);
			return pContactDialogNode;
		}
	}
	FOR_EACH_END

	estrDestroy(&estrStrippedName);
	return NULL;
}

// Finds a mission offer node
static ContactDialogUINode *CEDialogUIMissionOfferNodeFromName(SA_PARAM_NN_VALID DialogFlowWindowInfo *pInfo, SA_PARAM_NN_STR const char *pchMissionName)
{
	if (pInfo == NULL)
		return NULL;

	FOR_EACH_IN_EARRAY_FORWARDS(pInfo->eaDialogNodes, ContactDialogUINode, pContactDialogNode)
	{
		if (pContactDialogNode->pMissionOffer != NULL && stricmp(pContactDialogNode->pchSpecialDialogName, pchMissionName) == 0)
			return pContactDialogNode;
	}
	FOR_EACH_END

	return NULL;
}

// Toggles the visibility of an individual special dialog pane
static void CEToggleDialogNodePane(SA_PARAM_NN_VALID ContactDialogUINode *pContactDialogNode, bool bVisible)
{
	devassert(pContactDialogNode);
	if (pContactDialogNode && 
		pContactDialogNode->pInfo &&
		!pContactDialogNode->bIsDialogRoot && 
		pContactDialogNode->pchSpecialDialogName)
	{
		SpecialDialogBlock *pSpecialDialogBlock = NULL;
		CESpecialDialogGroup *pSpecialDialogGroup = CEDialogFlowGetSpecialDialogGroupByName(pContactDialogNode->pInfo, 
			pContactDialogNode->pchSpecialDialogName,
			&pSpecialDialogBlock, NULL);

		if (pSpecialDialogGroup && pSpecialDialogGroup->pPane)
		{
			pSpecialDialogGroup->pPane->invisible = !bVisible;
		}
	}
}

// Toggles the visibility of the panes of all special dialogs
static void CEToggleAllSpecialDialogPanes(SA_PARAM_NN_VALID DialogFlowWindowInfo *pInfo, bool bVisible)
{
	devassert(pInfo);
	if (pInfo)
	{
		if (pInfo->pContactDoc)
		{
			FOR_EACH_IN_EARRAY_FORWARDS(pInfo->pContactDoc->eaSpecialGroups, CESpecialDialogGroup, pSpecialDialogGroup)
			{			
				if (pSpecialDialogGroup->pPane)
				{
					pSpecialDialogGroup->pPane->invisible = !bVisible;
				}
			}
			FOR_EACH_END

			FOR_EACH_IN_EARRAY_FORWARDS(pInfo->pContactDoc->eaSpecialOverrideGroups, CESpecialDialogOverrideGroup, pSpecialDialogOverrideGroup)
			{			
				if (pSpecialDialogOverrideGroup->pPane)
				{
					pSpecialDialogOverrideGroup->pPane->invisible = !bVisible;
				}
			}
			FOR_EACH_END
		}
		else if (pInfo->pMissionDoc && pInfo->pchContactName && pInfo->pchContactName[0])
		{
			MDEContactGroup *pContactGroup = MDEFindContactGroup(pInfo->pMissionDoc, pInfo->pchContactName);
			if (pContactGroup)
			{
				FOR_EACH_IN_EARRAY_FORWARDS(pContactGroup->eaSpecialDialogOverrideGroups, MDESpecialDialogOverrideGroup, pOverrideGroup)
				{
					if (pOverrideGroup &&
						pOverrideGroup->pSpecialDialogGroup &&
						pOverrideGroup->pSpecialDialogGroup->pPane)
					{
						pOverrideGroup->pSpecialDialogGroup->pPane->invisible = !bVisible;
					}
				}
				FOR_EACH_END
			}
		}
	}
}

// Clears all node selection
static void CEClearDialogNodeSelection(SA_PARAM_NN_VALID DialogFlowWindowInfo *pInfo)
{
	devassert(pInfo);
	if (pInfo == NULL)
		return;

	CEToggleAllSpecialDialogPanes(pInfo, false);

	// Set all nodes as unselected
	FOR_EACH_IN_EARRAY_FORWARDS(pInfo->eaSelectedDialogNodes, ContactDialogUINode, pNode)
	{
		ui_GraphNodeSetSelected(pNode->pGraphNode, false);
	}
	FOR_EACH_END

	eaClear(&pInfo->eaSelectedDialogNodes);

	CEUpdateSplineColors(pInfo);
}

// Selects all dialog nodes
static void CESelectAllDialogNodes(SA_PARAM_NN_VALID DialogFlowWindowInfo *pInfo)
{
	devassert(pInfo);
	if (pInfo == NULL)
		return;	

	CEToggleAllSpecialDialogPanes(pInfo, true);

	eaClear(&pInfo->eaSelectedDialogNodes);

	// Set all nodes as unselected
	FOR_EACH_IN_EARRAY_FORWARDS(pInfo->eaDialogNodes, ContactDialogUINode, pNode)
	{
		ui_GraphNodeSetSelected(pNode->pGraphNode, true);
		eaPush(&pInfo->eaSelectedDialogNodes, pNode);
	}
	FOR_EACH_END

	CEUpdateSplineColors(pInfo);
}

// Select All
AUTO_COMMAND ACMD_CATEGORY(Interface) ACMD_NAME("ContactEditor.SelectAll");
void CECmdSelectAllDialogNodes(void)
{
#ifndef NO_EDITORS
	ContactEditDoc *pActiveDoc = CEGetActiveDoc();
	if (pActiveDoc)
	{
		CESelectAllDialogNodes(pActiveDoc->pDialogFlowWindowInfo);
	}
#endif
}

static void CESetZoomScale(SA_PARAM_NN_VALID UIWindow *pWin, SA_PARAM_NN_VALID UIScrollArea *pScrollArea, F32 scale)
{
	devassert(pWin);
	devassert(pScrollArea);
	if (pWin && pScrollArea)
	{
		UIScrollbar *sb = pScrollArea->widget.sb;
		F32 scrollWidth = ui_WidgetWidth(UI_WIDGET(pScrollArea), pWin->widget.width, 1) - ui_ScrollbarWidth(sb);
		F32 scrollHeight = ui_WidgetHeight(UI_WIDGET(pScrollArea), pWin->widget.height, 1) - ui_ScrollbarHeight(sb);
		F32 centerX, centerY;

		// we make adjustments to the position of the scrollbar to ensure that the center point
		// of the scroll area remains in the center after the zoom
		centerX = (sb->xpos + scrollWidth / 2) / pScrollArea->xSize;
		centerY = (sb->ypos + scrollHeight / 2) / pScrollArea->ySize;
		pScrollArea->childScale = scale;
		sb->xpos = pScrollArea->xSize * centerX - scrollWidth / 2;
		sb->ypos = pScrollArea->ySize * centerY - scrollHeight / 2;
	}
}

AUTO_COMMAND ACMD_CATEGORY(Interface) ACMD_NAME("ContactEditor.ZoomIn");
void CECmdZoomIn(void)
{
#ifndef NO_EDITORS
	ContactEditDoc *pActiveDoc = CEGetActiveDoc();
	if (pActiveDoc && pActiveDoc->pDialogFlowWindowInfo && pActiveDoc->pDialogFlowWindowInfo->pDialogFlowWin && pActiveDoc->pDialogFlowWindowInfo->pDialogFlowScrollArea)
	{
		F32 scale = pActiveDoc->pDialogFlowWindowInfo->pDialogFlowScrollArea->childScale;
		if (scale < 2)
			CESetZoomScale(pActiveDoc->pDialogFlowWindowInfo->pDialogFlowWin, pActiveDoc->pDialogFlowWindowInfo->pDialogFlowScrollArea, scale * 1.1);
	}
#endif
}

AUTO_COMMAND ACMD_CATEGORY(Interface) ACMD_NAME("ContactEditor.ZoomOut");
void CECmdZoomOut(void)
{
#ifndef NO_EDITORS
	ContactEditDoc *pActiveDoc = CEGetActiveDoc();
	if (pActiveDoc && pActiveDoc->pDialogFlowWindowInfo && pActiveDoc->pDialogFlowWindowInfo->pDialogFlowWin && pActiveDoc->pDialogFlowWindowInfo->pDialogFlowScrollArea)
	{
		F32 scale = pActiveDoc->pDialogFlowWindowInfo->pDialogFlowScrollArea->childScale;
		if (scale > 0.25)
			CESetZoomScale(pActiveDoc->pDialogFlowWindowInfo->pDialogFlowWin, pActiveDoc->pDialogFlowWindowInfo->pDialogFlowScrollArea, scale / 1.1);
	}
#endif
}

// Called whenever a dialog node loses focus
static void CEOnDialogNodeLostFocus(UIWindow *pWin, ContactDialogUINode *pContactDialogNode)
{
	// The Esc key appears to be unavailable to the onInput callbacks
	// since the LostFocus function captures it
	// this is a special hack to detect when the user presses the escape key (to clear selection)
	// maybe this could be moved to a keybind?

	KeyInput *pKey = inpGetKeyBuf();

	if(pKey && pKey->scancode == INP_ESCAPE && pContactDialogNode)
	{
		CEClearDialogNodeSelection(pContactDialogNode->pInfo);

		// Save all window positions
		CESaveDialogNodeWindowPositions(pContactDialogNode->pInfo);
	}
}

// Returns the special dialog group matching the given special dialog name
static CEOfferGroup * CEDialogFlowGetMissionOfferGroupByMissionOffer(SA_PARAM_NN_VALID DialogFlowWindowInfo *pInfo,
																	  SA_PARAM_NN_VALID ContactMissionOffer *pMissionOfferSearched,
																	  SA_PARAM_OP_VALID UIWindow **ppWindowOut)
{
	devassert(pInfo && pMissionOfferSearched);

	if (pInfo && pMissionOfferSearched)
	{
		if (pInfo->pContactDoc)
		{
			ContactMissionOffer *pMissionOffer = NULL;

			FOR_EACH_IN_EARRAY_FORWARDS(pInfo->pContactDoc->eaOfferGroups, CEOfferGroup, pMissionOfferGroup)
			{
				if (pMissionOfferGroup && 
					pMissionOfferGroup->pMissionField &&
					(pMissionOffer = (ContactMissionOffer *)pMissionOfferGroup->pMissionField->pNew) &&
					pMissionOffer == pMissionOfferSearched)
				{
					if (ppWindowOut)
					{
						*ppWindowOut = pInfo->pContactDoc->pMainWindow;
					}
					return pMissionOfferGroup;
				}
			}
			FOR_EACH_END
		}
		else if (pInfo->pMissionDoc)
		{
			// Get the contact group matching the 
			MDEContactGroup *pContactGroup = MDEFindContactGroup(pInfo->pMissionDoc, pInfo->pchContactName);

			if (pContactGroup)
			{
				ContactMissionOffer *pMissionOffer = NULL;

				FOR_EACH_IN_EARRAY_FORWARDS(pContactGroup->eaMissionOfferOverrideGroups, MDEMissionOfferOverrideGroup, pOverrideGroup)
				{
					if (pOverrideGroup && 
						pOverrideGroup->pMissionOfferGroup &&
						pOverrideGroup->pMissionOfferGroup->pMissionField &&
						(pMissionOffer = (ContactMissionOffer *)pOverrideGroup->pMissionOfferGroup->pMissionField->pNew) &&
						pMissionOffer == pMissionOfferSearched)
					{
						if (ppWindowOut)
						{
							*ppWindowOut = pContactGroup->pWindow;
						}
						return pOverrideGroup->pMissionOfferGroup;
					}
				}
				FOR_EACH_END
			}
		}
	}

	return NULL;
}


// Returns the special dialog group matching the given special dialog name
static CESpecialDialogGroup * CEDialogFlowGetSpecialDialogGroupByName(SA_PARAM_NN_VALID DialogFlowWindowInfo *pInfo,
																	  SA_PARAM_NN_STR const char *pchSpecialDialogName, 
																	  SA_PARAM_OP_VALID SpecialDialogBlock **ppSpecialDialogBlockOut,
																	  SA_PARAM_OP_VALID UIWindow **ppWindowOut)
{
	devassert(pInfo && pchSpecialDialogName && pchSpecialDialogName[0]);

	if (pInfo && pchSpecialDialogName && pchSpecialDialogName[0])
	{
		if (pInfo->pContactDoc)
		{
			SpecialDialogBlock *pSpecialDialogBlock = NULL;

			FOR_EACH_IN_EARRAY_FORWARDS(pInfo->pContactDoc->eaSpecialGroups, CESpecialDialogGroup, pSpecialDialogGroup)
			{
				if (pSpecialDialogGroup && 
					pSpecialDialogGroup->pNameField &&
					(pSpecialDialogBlock = (SpecialDialogBlock *)pSpecialDialogGroup->pNameField->pNew) &&
					stricmp(pSpecialDialogBlock->name, pchSpecialDialogName) == 0)
				{
					if (ppSpecialDialogBlockOut)
					{
						*ppSpecialDialogBlockOut = pSpecialDialogBlock;
					}
					if (ppWindowOut)
					{
						*ppWindowOut = pInfo->pContactDoc->pMainWindow;
					}
					return pSpecialDialogGroup;
				}
			}
			FOR_EACH_END
		}
		else if (pInfo->pMissionDoc)
		{
			// Get the contact group matching the 
			MDEContactGroup *pContactGroup = MDEFindContactGroup(pInfo->pMissionDoc, pInfo->pchContactName);
			
			if (pContactGroup)
			{
				SpecialDialogBlock *pSpecialDialogBlock = NULL;
				FOR_EACH_IN_EARRAY_FORWARDS(pContactGroup->eaSpecialDialogOverrideGroups, MDESpecialDialogOverrideGroup, pOverrideGroup)
				{
					if (pOverrideGroup && 
						pOverrideGroup->pSpecialDialogGroup &&
						pOverrideGroup->pSpecialDialogGroup->pNameField &&
						(pSpecialDialogBlock = (SpecialDialogBlock *)pOverrideGroup->pSpecialDialogGroup->pNameField->pNew) &&
						stricmp(pSpecialDialogBlock->name, pchSpecialDialogName) == 0)
					{
						if (ppSpecialDialogBlockOut)
						{
							*ppSpecialDialogBlockOut = pSpecialDialogBlock;
						}
						if (ppWindowOut)
						{
							*ppWindowOut = pContactGroup->pWindow;
						}
						return pOverrideGroup->pSpecialDialogGroup;
					}
				}
				FOR_EACH_END
			}
		}
	}

	return NULL;
}

// Finds the active special dialog expander for the dialog flow window
SA_RET_OP_VALID static UIExpander * CEDialogFlowWindowGetSpecialDialogExpander(SA_PARAM_NN_VALID DialogFlowWindowInfo *pInfo, SA_PARAM_OP_STR const char *pchSpecialDialogName)
{
	if (pInfo && pInfo->pContactDoc)
	{
		return pInfo->pContactDoc->pSpecialExpander;
	}
	else if (pInfo && pInfo->pMissionDoc)
	{
		MDEContactGroup *pContactGroup = MDEFindContactGroup(pInfo->pMissionDoc, pInfo->pchContactName);

		if (pContactGroup)
		{
			const MDESpecialDialogOverrideGroup * const pGroup = MDEFindSpecialDialogOverrideGroupByName(pContactGroup, pchSpecialDialogName);
			return pGroup ? pGroup->pExpander : NULL;
		}
	}
	return NULL;
}

// Finds the active mission offer expander for the dialog flow window
SA_RET_OP_VALID static UIExpander * CEDialogFlowWindowGetMissionOfferExpander(SA_PARAM_NN_VALID DialogFlowWindowInfo *pInfo, SA_PARAM_OP_VALID ContactMissionOffer *pMissionOffer)
{
	if (pInfo && pInfo->pContactDoc)
	{
		return pInfo->pContactDoc->pOfferExpander;
	}
	else if (pInfo && pInfo->pMissionDoc)
	{
		MDEContactGroup *pContactGroup = MDEFindContactGroup(pInfo->pMissionDoc, pInfo->pchContactName);

		if (pContactGroup)
		{
			if (pContactGroup)
			{
				const MDEMissionOfferOverrideGroup * const pGroup = MDEFindMissionOfferOverrideGroupByOffer(pContactGroup, pMissionOffer);
				return pGroup ? pGroup->pExpander : NULL;
			}
		}
	}
	return NULL;
}

// Scrolls to the special dialog block based on the dialog node given
static void CEScrollToSpecialDialogBlock(SA_PARAM_NN_VALID ContactDialogUINode *pContactDialogNode, bool bSwitchToMainWindow)
{
	if (pContactDialogNode && pContactDialogNode->pInfo && !pContactDialogNode->bIsDialogRoot)
	{
		UIExpander *pOfferExpander = CEDialogFlowWindowGetMissionOfferExpander(pContactDialogNode->pInfo, pContactDialogNode->pMissionOffer);
		UIExpander *pSpecialExpander = CEDialogFlowWindowGetSpecialDialogExpander(pContactDialogNode->pInfo, pContactDialogNode->pchSpecialDialogName);
		UIWindow *pWindow = NULL;

		if (pContactDialogNode->pMissionOffer == NULL)
		{			
			SpecialDialogBlock *pSpecialDialogBlock = NULL;			
			CESpecialDialogGroup *pSpecialDialogGroup = CEDialogFlowGetSpecialDialogGroupByName(pContactDialogNode->pInfo, 
				pContactDialogNode->pchSpecialDialogName, 
				&pSpecialDialogBlock, 
				&pWindow);
			
			if (pSpecialDialogGroup)
			{
				// Found the match, focus on the control
				if (bSwitchToMainWindow && pContactDialogNode->pInfo)
				{
					if (pContactDialogNode->pInfo->pContactDoc)
					{
						ui_WindowPresent(pWindow);
					}
					else
					{
						ui_SetFocus(pContactDialogNode->pInfo->pMissionDoc->pMainWindow);
					}					
				}

				// Open the expander
				if (pSpecialExpander && !ui_ExpanderIsOpened(pSpecialExpander))
				{
					ui_ExpanderSetOpened(pSpecialExpander, true);
				}

				// Set the focused widget
				pContactDialogNode->pInfo->pWidgetToFocusOnDialogNodeClick = &pSpecialDialogGroup->pPane->widget;
				pContactDialogNode->pInfo->pHighlightedWindow = NULL;
				pContactDialogNode->pInfo->pHighlightedExpander = NULL;

				if (pSpecialExpander)
				{
					if (pContactDialogNode->pInfo->pMissionDoc)
					{
						pContactDialogNode->pInfo->pHighlightedWindow = pWindow;
					}
					pContactDialogNode->pInfo->pHighlightedExpander = pSpecialExpander;
					pSpecialExpander->widget.highlightPercent = 1.0f;	
				}

				if (bSwitchToMainWindow)
				{
					ui_SetFocus(pSpecialDialogGroup->pNameField->pUIWidget);
				}
			}
		}
		else
		{
			CEOfferGroup *pMissionOfferGroup = CEDialogFlowGetMissionOfferGroupByMissionOffer(pContactDialogNode->pInfo, 
				pContactDialogNode->pMissionOffer,
				&pWindow);			

			if (pMissionOfferGroup)
			{
				// Found the match, focus on the control
				if (bSwitchToMainWindow && pContactDialogNode->pInfo)
				{
					if (pContactDialogNode->pInfo->pContactDoc)
					{
						ui_WindowPresent(pWindow);
					}
					else
					{
						ui_SetFocus(pContactDialogNode->pInfo->pMissionDoc->pMainWindow);
					}					
				}

				// Open the expander
				if (pOfferExpander && !ui_ExpanderIsOpened(pOfferExpander))
				{
					ui_ExpanderSetOpened(pOfferExpander, true);
				}

				// Set the focused widget
				pContactDialogNode->pInfo->pWidgetToFocusOnDialogNodeClick = pMissionOfferGroup->pMissionField->pUIWidget;
				pContactDialogNode->pInfo->pHighlightedWindow = NULL;
				pContactDialogNode->pInfo->pHighlightedExpander = NULL;

				if (pOfferExpander)
				{
					if (pContactDialogNode->pInfo->pMissionDoc)
					{
						pContactDialogNode->pInfo->pHighlightedWindow = pWindow;
					}
					pContactDialogNode->pInfo->pHighlightedExpander = pOfferExpander;
					pOfferExpander->widget.highlightPercent = 1.0f;	
				}

				if (bSwitchToMainWindow)
				{
					ui_SetFocus(pMissionOfferGroup->pMissionField->pUIWidget);
				}
			}
		}
	}
}

// Handles the double click event on the dialop nodes
static void CEOnDialogNodeDoubleClick(SA_PARAM_NN_VALID ContactDialogUINode *pContactDialogNode)
{
	CEScrollToSpecialDialogBlock(pContactDialogNode, true);
}

// Called when the mouse is down on a dialog node
static void CEOnDialogNodeMouseDown(UIGraphNode *pGraphNode, Vec2 clickPoint, ContactDialogUINode *pContactDialogNode)
{
	DialogFlowWindowInfo *pInfo = pContactDialogNode ? pContactDialogNode->pInfo : NULL;

	devassert(pGraphNode && pContactDialogNode);	

	if(pGraphNode && pInfo)
	{
		if (pGraphNode->bLeftMouseDoubleClicked)
		{
			// Handle double clicks on the title bar
			CEOnDialogNodeDoubleClick(pContactDialogNode);
		}

		if(pGraphNode->bLeftMouseDown) // only left-click
		{
			bool bIsInSelection = eaFind(&pInfo->eaSelectedDialogNodes, pContactDialogNode) >= 0;
			bool bIsControlKeyDown = inpLevelPeek(INP_CONTROL);

			if(!bIsControlKeyDown && !bIsInSelection)
			{
				// de-select old
				FOR_EACH_IN_EARRAY_FORWARDS(pInfo->eaSelectedDialogNodes, ContactDialogUINode, pNode)
				{
					CEToggleDialogNodePane(pNode, false);
					ui_GraphNodeSetSelected(pNode->pGraphNode, false);
				}
				FOR_EACH_END

				eaClear(&pInfo->eaSelectedDialogNodes);
			} 

			if(bIsControlKeyDown && bIsInSelection)
			{
				// de-select individual
				CEToggleDialogNodePane(pContactDialogNode, false);
				ui_GraphNodeSetSelected(pGraphNode, false);
				eaFindAndRemove(&pInfo->eaSelectedDialogNodes, pContactDialogNode);
			}
			else
			{
				CEToggleDialogNodePane(pContactDialogNode, true);
				eaPushUnique(&pInfo->eaSelectedDialogNodes, pContactDialogNode);
				ui_GraphNodeSetSelected(pGraphNode, true);
				CEScrollToSpecialDialogBlock(pContactDialogNode, false);
			}

			CEUpdateSplineColors(pInfo);
		}

		copyVec2(clickPoint, pInfo->vecLastMousePoint);
		copyVec2(clickPoint, pInfo->vecMouseDownPoint);

		// Save all window positions
		CESaveDialogNodeWindowPositions(pInfo);
	}
}

// Called when the dialog node is dragged
static void CEOnDialogNodeMouseDrag(UIGraphNode *pGraphNode, Vec2 clickPoint, ContactDialogUINode *pContactDialogNode)
{
	DialogFlowWindowInfo *pInfo = pContactDialogNode ? pContactDialogNode->pInfo : NULL;

	devassert(pGraphNode);

	if(pGraphNode && pInfo)
	{
		Vec2 delta;
		bool bMoveX;
		bool bMoveY;

		subVec2(clickPoint, pInfo->vecLastMousePoint, delta);

		// scale
		delta[0] /= pGraphNode->pScale;
		delta[1] /= pGraphNode->pScale;
		
		bMoveX = delta[0] != 0.0;
		bMoveY = delta[1] != 0.0;

		if(bMoveX || bMoveY)
		{
			// test all first
			FOR_EACH_IN_EARRAY_FORWARDS(pInfo->eaSelectedDialogNodes, ContactDialogUINode, pNode)
			{
				if(pNode->pGraphNode->window.widget.x + delta[0] < 0.)
				{
					bMoveX = false;
				}

				if(pNode->pGraphNode->window.widget.y + delta[1] < pNode->pGraphNode->topPad)
				{
					bMoveY = false;
				}
			}
			FOR_EACH_END

			// apply movement
			FOR_EACH_IN_EARRAY_FORWARDS(pInfo->eaSelectedDialogNodes, ContactDialogUINode, pNode)
			{
				if(bMoveX)
				{
					pNode->pGraphNode->window.widget.x += delta[0];
				}

				if(bMoveY)
				{
					pNode->pGraphNode->window.widget.y += delta[1];
				}
			}
			FOR_EACH_END
		}

		// remember to save last point
		copyVec2(clickPoint, pInfo->vecLastMousePoint);

		// Save all window positions
		CESaveDialogNodeWindowPositions(pInfo);
	}
}

// Called when the mouse is down on a dialog node
static void CEOnDialogNodeMouseUp(UIGraphNode *pGraphNode, Vec2 clickPoint, ContactDialogUINode *pContactDialogNode)
{
	DialogFlowWindowInfo *pInfo = pContactDialogNode ? pContactDialogNode->pInfo : NULL;

	devassert(pGraphNode);

	if(pGraphNode && pInfo)
	{
		if(	pInfo->vecMouseDownPoint[0] == clickPoint[0] && 
			pInfo->vecMouseDownPoint[1] == clickPoint[1])
		{
			bool bIsControlKeyDown = inpLevelPeek(INP_CONTROL);

			if(!bIsControlKeyDown)
			{
				FOR_EACH_IN_EARRAY_FORWARDS(pInfo->eaSelectedDialogNodes, ContactDialogUINode, pNode)
				{
					CEToggleDialogNodePane(pNode, false);
					ui_GraphNodeSetSelected(pNode->pGraphNode, false);
				}
				FOR_EACH_END

				eaClear(&pInfo->eaSelectedDialogNodes);

				eaPush(&pInfo->eaSelectedDialogNodes, pContactDialogNode);
				CEToggleDialogNodePane(pContactDialogNode, true);
				ui_GraphNodeSetSelected(pGraphNode, true);

				CEUpdateSplineColors(pInfo);

				// Save all window positions
				CESaveDialogNodeWindowPositions(pInfo);
			}
		}
	}
}

// Creates a special dialog action used to connect nodes together
static SpecialDialogAction * CECreateSpecialDialogActionForConnection(SA_PARAM_NN_VALID DialogFlowWindowInfo *pInfo, SA_PARAM_OP_STR const char * pchTargetDialogName)
{	
	SpecialDialogAction *pSpecialDialogAction = StructCreate(parse_SpecialDialogAction);
	pSpecialDialogAction->bSendComplete = true;
	if (pchTargetDialogName != NULL)
	{
		if (pInfo->pMissionDoc)
		{
			pSpecialDialogAction->dialogName = CEGetPooledSpecialDialogNameByMission(pchTargetDialogName, pInfo->pMissionDoc->pMission->name);
		}
		else
		{
			pSpecialDialogAction->dialogName = allocAddString(pchTargetDialogName);
		}		
	}

	return pSpecialDialogAction;

}

// Adds a new special dialog to either the mission or the contact document
static void CEDialogFlowWindowAddSpecialDialogToDocument(SA_PARAM_NN_VALID DialogFlowWindowInfo *pInfo,
														 SA_PARAM_NN_VALID SpecialDialogBlock *pNewSpecialDialogBlock)
{
	if (pInfo)
	{
		if (pInfo->pContactDoc)
		{
			eaPush(&pInfo->pContactDoc->pContact->specialDialog, pNewSpecialDialogBlock);
		}
		else if (pInfo->pMissionDoc && pInfo->pchContactName && pInfo->pchContactName[0])
		{
			SpecialDialogOverride *pOverride = StructCreate(parse_SpecialDialogOverride);
			pOverride->pcContactName = allocAddString(pInfo->pchContactName);
			pOverride->pSpecialDialog = pNewSpecialDialogBlock;

			eaPush(&pInfo->pMissionDoc->pMission->ppSpecialDialogOverrides, pOverride);
		}
	}
}

//Adds a new special action block to either the mission or the contact document
static void CEDialogFlowWindowAddSpecialActionBlockToDocument(SA_PARAM_NN_VALID DialogFlowWindowInfo *pInfo, SA_PARAM_NN_VALID SpecialActionBlock *pNewSpecialActionBlock) {
	if(pInfo) {
		if(pInfo->pContactDoc) {
			eaPush(&pInfo->pContactDoc->pContact->specialActions, pNewSpecialActionBlock);
		} else if (pInfo->pMissionDoc && pInfo->pchContactName && pInfo->pchContactName[0]) {
			ActionBlockOverride *pOverride = StructCreate(parse_ActionBlockOverride);
			pOverride->pcContactName = allocAddString(pInfo->pchContactName);
			pOverride->pSpecialActionBlock = pNewSpecialActionBlock;

			eaPush(&pInfo->pMissionDoc->pMission->ppSpecialActionBlockOverrides, pOverride);
		}
	}
}

// Handles the click event for the clone dialog button
static void CEOnCloneDialogButtonClick(UIButton *pButton, ContactDialogUINode *pDialogNode)
{
	if (pDialogNode && pDialogNode->pInfo)
	{
		// Check if editable
		if (!emDocIsEditable(CEGetEMEditorDocFromDialogNode(pDialogNode), true))
		{
			return;
		}
		else
		{
			SpecialDialogBlock *pOrigDialog = CEDialogFlowGetSpecialDialogByName(pDialogNode->pInfo, pDialogNode->pchSpecialDialogName, pDialogNode->pInfo->pchContactName);
			if (pOrigDialog)
			{
				char *estrPrefStoreName = NULL;

				SpecialDialogBlock *pNewDialog = StructClone(parse_SpecialDialogBlock, pOrigDialog);
				int iSuffix = 2;
				char pchNewName[1024];

				// Find a unique name for the dialog
				sprintf(pchNewName, "%s_%d", pNewDialog->name, iSuffix);
				while(CEDialogFlowGetSpecialDialogByName(pDialogNode->pInfo, pchNewName, pDialogNode->pInfo->pchContactName))
				{
					iSuffix++;
					sprintf(pchNewName, "%s_%d", pNewDialog->name, iSuffix);
				}
				pNewDialog->name = allocAddString(pchNewName);

				// Add the new dialog
				CEDialogFlowWindowAddSpecialDialogToDocument(pDialogNode->pInfo, pNewDialog);

				// Remove the position stored. This forces the graph node to be position automatically even if there was previously a graph node with the same name.
				if (CEDialogFlowWindowGetDialogNodeNameForPrefStoreBySpecialDialogName(pDialogNode->pInfo, pNewDialog->name, &estrPrefStoreName))
				{
					if (pDialogNode->pInfo->pContactDoc)
					{
						EditorPrefClear(CONTACT_EDITOR, "Window Position", estrPrefStoreName);
					}
					else if (pDialogNode->pInfo->pMissionDoc)
					{
						EditorPrefClear(MISSION_EDITOR, "Window Position", estrPrefStoreName);
					}				
				}

				CEDialogFlowWindowRefreshSpecialExpander(pDialogNode->pInfo);
				CERefreshDialogFlowWindow(pDialogNode->pInfo);

				estrDestroy(&estrPrefStoreName);
			}

		}
	}
}

// Handles the click event for the add child dialog button
static void CEOnAddChildDialogButtonClick(UIButton *pButton, ContactDialogUINode *pDialogNode)
{
	if (pDialogNode && pDialogNode->pInfo)
	{
		// Check if editable
		if (!emDocIsEditable(CEGetEMEditorDocFromDialogNode(pDialogNode), true))
		{
			return;
		}
		else
		{
			char *estrPrefStoreName = NULL;
			// Create a new special dialog block
			SpecialDialogBlock *pDestSpecialDialogBlock = CEAddSpecialDialogBlock(pDialogNode->pInfo);

			if (pDialogNode->bIsDialogRoot)
			{
				if (pDestSpecialDialogBlock->pCondition)
				{
					exprDestroy(pDestSpecialDialogBlock->pCondition);
					pDestSpecialDialogBlock->pCondition = NULL;
				}				
			}
			else
			{
				SpecialDialogBlock *pSourceSpecialDialogBlock = CEDialogFlowGetSpecialDialogByName(pDialogNode->pInfo, pDialogNode->pchSpecialDialogName, pDialogNode->pInfo->pchContactName);

				// Create an action that would connect the source dialog to the new one
				SpecialDialogAction *pSpecialDialogAction = CECreateSpecialDialogActionForConnection(pDialogNode->pInfo, pDestSpecialDialogBlock->name);
				
				// Add this action to the source special dialog block
				eaPush(&pSourceSpecialDialogBlock->dialogActions, pSpecialDialogAction);

				// Also we need to set the condition of the dest special dialog block to 0
				if (pDestSpecialDialogBlock->pCondition == NULL)
				{
					pDestSpecialDialogBlock->pCondition = exprCreate();
				}
				exprSetOrigStrNoFilename(pDestSpecialDialogBlock->pCondition, "0");	
			}

			// Remove the position stored. This forces the graph node to be position automatically even if there was previously a graph node with the same name.
			if (CEDialogFlowWindowGetDialogNodeNameForPrefStoreBySpecialDialogName(pDialogNode->pInfo, pDestSpecialDialogBlock->name, &estrPrefStoreName))
			{
				if (pDialogNode->pInfo->pContactDoc)
				{
					EditorPrefClear(CONTACT_EDITOR, "Window Position", estrPrefStoreName);
				}
				else if (pDialogNode->pInfo->pMissionDoc)
				{
					EditorPrefClear(MISSION_EDITOR, "Window Position", estrPrefStoreName);
				}				
			}

			CEDialogFlowWindowRefreshSpecialExpander(pDialogNode->pInfo);
			CERefreshDialogFlowWindow(pDialogNode->pInfo);
			
			estrDestroy(&estrPrefStoreName);
		}
	}
}

static void CEOnMissionOfferSpecialDialogNameOK(UIWidget *widget, DialogFlowWindowInfo *pInfo)
{
	const char *pchSpecialDialogName = ui_TextEntryGetText(pInfo->pMissionOfferSpecialDialogNamePromptTextEntry);

	if (pchSpecialDialogName == NULL || pchSpecialDialogName[0] == '\0')
	{
		emStatusPrintf("Please enter a special dialog name for the mission offer.");
	}
	else if (CEDialogFlowGetSpecialDialogByName(pInfo, pchSpecialDialogName, pInfo->pchContactName))
	{
		emStatusPrintf("Please enter a unique special dialog name for the mission offer. The name is already in use.");
	}
	else
	{
		s_ConnectCallState.pDestNode->pMissionOffer->pchSpecialDialogName = allocAddString(pchSpecialDialogName);	

		elUIWindowClose(NULL, s_ConnectCallState.pModalWin);

		if (s_ConnectCallState.pAction)
		{
			CEConnectActionWithDialogNode(s_ConnectCallState.pAction, s_ConnectCallState.pDestNode);
		}
		else
		{
			CEConnectDialogNodes(s_ConnectCallState.pSourceNode, s_ConnectCallState.pDestNode);
		}	
	}
}

static void CEPromptMissionOfferSpecialDialogName(SA_PARAM_NN_VALID DialogFlowWindowInfo *pInfo)
{
	UITextEntry *pTextentry;
	UIWindow *win = ui_WindowCreate("The mission offer does not have a special dialog name", 100, 100, 300, 80);
	UILabel *label;
	UIButton *button;

	// Set the modal window here so we can close it later on.
	s_ConnectCallState.pModalWin = win;

	label = ui_LabelCreate("Special dialog name for the mission offer:", 5, 5);
	ui_WindowAddChild(win, label);

	pTextentry = ui_TextEntryCreate("", elUINextX(label) + 5, label->widget.y);
	ui_WindowAddChild(win, pTextentry);

	pInfo->pMissionOfferSpecialDialogNamePromptTextEntry = pTextentry;

	button = elUIAddCancelOkButtons(win, NULL, NULL, CEOnMissionOfferSpecialDialogNameOK, pInfo);
	win->widget.width = elUINextX(pTextentry) + 5;
	win->widget.height = elUINextY(label) + elUINextY(button) + 5;
	elUICenterWindow(win);
	ui_WindowSetModal(win, true);
	ui_WindowShow(win);
}

static EMEditorDoc * CEGetEMEditorDocFromDialogNode(SA_PARAM_NN_VALID ContactDialogUINode *pNode)
{
	if (pNode && pNode->pInfo)
	{
		if (pNode->pInfo->pContactDoc)
		{
			return &pNode->pInfo->pContactDoc->emDoc;
		}
		else if (pNode->pInfo->pMissionDoc)
		{
			return &pNode->pInfo->pMissionDoc->emDoc;
		}
	}

	return NULL;
}

static void CEDialogFlowWindowRefreshSpecialExpander(SA_PARAM_NN_VALID DialogFlowWindowInfo *pInfo)
{
	if (pInfo)
	{
		if (pInfo->pContactDoc)
		{
			CERefreshSpecialExpander(pInfo->pContactDoc);
		}
		else if (pInfo->pMissionDoc)
		{
			MDEUpdateDisplay(pInfo->pMissionDoc);
		}
	}	
}

// Connects two dialog nodes to each other by creating an action in the source node
static void CEConnectDialogNodes(SA_PARAM_NN_VALID ContactDialogUINode *pSourceNode, SA_PARAM_NN_VALID ContactDialogUINode *pDestNode)
{
	devassert(pSourceNode);
	devassert(!pSourceNode->bIsDialogRoot);
	devassert(pDestNode);
	if (pSourceNode && 
		!pSourceNode->bIsDialogRoot && 
		pDestNode)
	{
		// Check if editable
		if (!emDocIsEditable(CEGetEMEditorDocFromDialogNode(pSourceNode), true))
		{
			ui_DragCancel();
			return;
		}
		else
		{
			SpecialDialogBlock *pSpecialDialogBlock = CEDialogFlowGetSpecialDialogByName(pSourceNode->pInfo, pSourceNode->pchSpecialDialogName, pSourceNode->pInfo->pchContactName);
			if (pSpecialDialogBlock)
			{
				if (pDestNode->pMissionOffer == NULL || 
					(pDestNode->pMissionOffer->pchSpecialDialogName && pDestNode->pMissionOffer->pchSpecialDialogName[0]))
				{
					const char *pchTargetDialogName = pDestNode->pMissionOffer ? pDestNode->pMissionOffer->pchSpecialDialogName : pDestNode->pchSpecialDialogName;

					// Create a new action
					SpecialDialogAction *pSpecialDialogAction = CECreateSpecialDialogActionForConnection(pSourceNode->pInfo, pDestNode->bIsDialogRoot ? NULL : pchTargetDialogName);

					// Add this action to the special dialog block
					eaPush(&pSpecialDialogBlock->dialogActions, pSpecialDialogAction);

					CEDialogFlowWindowRefreshSpecialExpander(pSourceNode->pInfo);
					CERefreshDialogFlowWindow(pSourceNode->pInfo);
				}
				else if (pDestNode->pMissionOffer)
				{
					s_ConnectCallState.pAction = NULL;
					s_ConnectCallState.pSourceNode = pSourceNode;
					s_ConnectCallState.pDestNode = pDestNode;

					CEPromptMissionOfferSpecialDialogName(pDestNode->pInfo);
				}
			}
		}

	}
}

// Connects an action to a special dialog node or root
static void CEConnectActionWithDialogNode(SA_PARAM_NN_VALID ContactDialogUINodeAction *pAction, SA_PARAM_NN_VALID ContactDialogUINode *pNode)
{
	devassert(pAction);
	devassert(pAction->pSpecialDialogAction);
	devassert(pNode);
	devassert(pAction->pParentDialogNode != pNode);
	if (pAction &&
		pNode &&
		(pNode->pMissionOffer == NULL || (pNode->pMissionOffer->pchSpecialDialogName && pNode->pMissionOffer->pchSpecialDialogName[0])) &&
		pAction->pParentDialogNode != pNode)
	{
		// Check if editable
		if (!emDocIsEditable(CEGetEMEditorDocFromDialogNode(pNode), true))
		{
			ui_DragCancel();
			return;
		}

		if (pNode->bIsDialogRoot)
		{
			// Connect to root
			pAction->pSpecialDialogAction->dialogName = NULL;
		}
		else if (pNode->pMissionOffer)
		{
			// Connect to the mission offer
			if (pNode->pInfo->pMissionDoc)
			{
				// Connect to the special dialog
				pAction->pSpecialDialogAction->dialogName = CEGetPooledSpecialDialogNameByMission(pNode->pMissionOffer->pchSpecialDialogName, pNode->pInfo->pMissionDoc->pMission->name);
			}
			else
			{
				pAction->pSpecialDialogAction->dialogName = allocAddString(pNode->pMissionOffer->pchSpecialDialogName);
			}			
		}
		else
		{
			if (pNode->pInfo->pMissionDoc)
			{
				// Connect to the special dialog
				pAction->pSpecialDialogAction->dialogName = CEGetPooledSpecialDialogNameByMission(pNode->pchSpecialDialogName, pNode->pInfo->pMissionDoc->pMission->name);
			}
			else
			{
				// Connect to the special dialog
				pAction->pSpecialDialogAction->dialogName = allocAddString(pNode->pchSpecialDialogName);
			}
		}

		CEDialogFlowWindowRefreshSpecialExpander(pNode->pInfo);
		CERefreshDialogFlowWindow(pNode->pInfo);
	}
	else if (pAction &&
		pNode &&
		pNode->pMissionOffer &&
		pAction->pParentDialogNode != pNode)
	{
		s_ConnectCallState.pAction = pAction;
		s_ConnectCallState.pSourceNode = NULL;
		s_ConnectCallState.pDestNode = pNode;
		CEPromptMissionOfferSpecialDialogName(pNode->pInfo);
	}
}

// Called when the incoming or outgoing connection buttons are dragged
static void CEOnDialogNodeDrag(UIButton *button, ContactDialogUINode *pContactDialogNode)
{
	ui_DragStart((UIWidget *)button, DIALOG_NODE_CONNECT_PAYLOAD, pContactDialogNode, atlasLoadTexture("button_pinned.tga"));
}

// Called when the incoming or outgoing connection buttons are dropped
static void CEOnDialogNodeDrop(UIButton *source, UIButton *dest, UIDnDPayload *payload, ContactDialogUINode *pDestDialogNode)
{
	ContactDialogUINode *pSourceDialogNode = NULL;
	ContactDialogUINodeAction *pSourceDialogNodeAction = NULL;

	// Verify the correct payload type
	if (strcmpi(payload->type, DIALOG_NODE_CONNECT_PAYLOAD) == 0)
	{
		pSourceDialogNode = (ContactDialogUINode *)payload->payload;
	}
	else if (strcmpi(payload->type, DIALOG_NODE_ACTION_CONNECT_PAYLOAD) == 0)
	{
		pSourceDialogNodeAction = (ContactDialogUINodeAction *)payload->payload;
		if (pSourceDialogNodeAction)
		{
			pSourceDialogNode = pSourceDialogNodeAction->pParentDialogNode;
		}
	}

	if (pSourceDialogNode == NULL)
	{
		ui_DragCancel();
		return;
	}

	// Do not let connections go in-to-in or out-to-out
	if ((source == pSourceDialogNode->pIncomingConnectionButton && dest == pDestDialogNode->pIncomingConnectionButton) ||
		(source == pSourceDialogNode->pOutgoingConnectionButton && dest == pDestDialogNode->pOutgoingConnectionButton))
	{
		emStatusPrintf("Cannot make input-to-input or output-to-output connections!");
		ui_DragCancel();
		return;
	}

	if (pSourceDialogNodeAction && dest == pDestDialogNode->pOutgoingConnectionButton)
	{
		emStatusPrintf("Cannot connect an action to a special dialog output!");
		ui_DragCancel();
		return;
	}

	// Do not let connections go from one state to itself
	if (pSourceDialogNode == pDestDialogNode)
	{
		emStatusPrintf("Cannot connect a dialog node to itself!");
		ui_DragCancel();
		return;
	}

	if (pSourceDialogNodeAction)
	{
		// Connect an action to a dialog node
		CEConnectActionWithDialogNode(pSourceDialogNodeAction, pDestDialogNode);
	}
	else
	{
		// Connect two dialog nodes with each other (by adding an action)
		if (source == pSourceDialogNode->pOutgoingConnectionButton)
			CEConnectDialogNodes(pSourceDialogNode, pDestDialogNode);
		else
			CEConnectDialogNodes(pDestDialogNode, pSourceDialogNode);
	}
}

// Properly destroys a dialog node action
static void CEDestroyDialogNodeAction(ContactDialogUINodeAction *pContactDialogNodeAction)
{
	if (pContactDialogNodeAction == NULL)
	{
		return;
	}

	free(pContactDialogNodeAction);
}

static void CEDestroyPairedBoxes(UIPairedBox *pPairedBox)
{
	ui_WidgetQueueFree((UIWidget*)pPairedBox);
}

// Properly destroys a dialog node
static void CEDestroyDialogNode(ContactDialogUINode *pContactDialogNode)
{
	if (pContactDialogNode)
	{
		// Disconnect
		FOR_EACH_IN_EARRAY_FORWARDS(pContactDialogNode->eaIncomingPairedBoxes, UIPairedBox, pPairedBox)
		{
			if (pPairedBox->otherBox)
				ui_PairedBoxDisconnect(pPairedBox, pPairedBox->otherBox);
		}
		FOR_EACH_END
		FOR_EACH_IN_EARRAY_FORWARDS(pContactDialogNode->eaOutgoingPairedBoxes, UIPairedBox, pPairedBox)
		{
			if (pPairedBox->otherBox)
				ui_PairedBoxDisconnect(pPairedBox, pPairedBox->otherBox);
		}
		FOR_EACH_END

		eaDestroyEx(&pContactDialogNode->eaIncomingPairedBoxes, CEDestroyPairedBoxes);
		eaDestroyEx(&pContactDialogNode->eaOutgoingPairedBoxes, CEDestroyPairedBoxes);

		// Clean up		
		if (pContactDialogNode->pGraphNode)
		{
			pContactDialogNode->pGraphNode->window.widget.onUnfocusData = NULL;
			pContactDialogNode->pGraphNode->onMouseDownUserData = NULL;
			pContactDialogNode->pGraphNode->onMouseDragUserData = NULL;
			pContactDialogNode->pGraphNode->onMouseUpUserData = NULL;
			if (pContactDialogNode->pAddChildDialogButton)
				pContactDialogNode->pAddChildDialogButton->clickedData = NULL;
			if (pContactDialogNode->pCloneDialogButton)
				pContactDialogNode->pCloneDialogButton->clickedData = NULL;
			ui_ScrollAreaRemoveChild(pContactDialogNode->pInfo->pDialogFlowScrollArea, pContactDialogNode->pGraphNode);
			ui_WidgetQueueFree((UIWidget*)pContactDialogNode->pGraphNode);
		}

		if (pContactDialogNode->pchSpecialDialogName)
			free(pContactDialogNode->pchSpecialDialogName);

		// Destroy all dialog node actions
		FOR_EACH_IN_EARRAY_FORWARDS(pContactDialogNode->eaActions, ContactDialogUINodeAction, pContactDialogNodeAction)
		{
			CEDestroyDialogNodeAction(pContactDialogNodeAction);
		}
		FOR_EACH_END

		// Destroy the lists
		eaDestroy(&pContactDialogNode->eaActions);
		eaDestroy(&pContactDialogNode->eaRootLevelSpecialDialogBlocks);

		// Destroy
		free(pContactDialogNode);
	}
}

// Returns the formatted string for a dialog text
SA_RET_OP_STR static const char * CEGetFormattedDialogText(SA_PARAM_OP_STR const char *pchDialogText, SA_PARAM_OP_VALID ContactDialogFormatterDef *pFormatter)
{
	if (pchDialogText && pchDialogText[0] && pFormatter)
	{
		static char *estrFormattedDialogText = NULL;

		estrClear(&estrFormattedDialogText);

		FormatDisplayMessage(&estrFormattedDialogText, 
			pFormatter->msgDialogFormat, 
			STRFMT_STRING("DialogText", pchDialogText),
			STRFMT_END);

		return estrFormattedDialogText;
	}

	return pchDialogText;
}

// Creates a contact dialog node
static ContactDialogUINode * CECreateDialogNode(SA_PARAM_NN_VALID DialogFlowWindowInfo *pInfo, 
												SA_PARAM_OP_STR const char * pchSpecialDialogName, 
												SA_PARAM_OP_VALID ContactMissionOffer *pMissionOffer, 
												F32 x, F32 y)
{
	ContactDialogUINode *pContactDialogNode = calloc(1, sizeof(ContactDialogUINode));
	SpecialDialogBlock *pSpecialDialogBlock = NULL;
	UIButton *pIncomingConnectionButton, *pOutgoingConnectionButton, *pAddChildDialogButton;	

	pContactDialogNode->pInfo = pInfo;

	// Empty string is not acceptable
	devassert(pchSpecialDialogName == NULL || pchSpecialDialogName[0] != '\0');
	
	// Set the special dialog name
	if (pchSpecialDialogName == NULL)
	{
		if (pMissionOffer == NULL)
		{
			pContactDialogNode->bIsDialogRoot = true;
			pContactDialogNode->pchSpecialDialogName = strdup(DIALOG_NODE_ROOT_NAME);
		}
		else
		{
			MissionDef *pMissionDef = GET_REF(pMissionOffer->missionDef);

			pContactDialogNode->bIsDialogRoot = false;
			pContactDialogNode->pMissionOffer = pMissionOffer;

			if (pMissionDef)
			{
				pContactDialogNode->pchSpecialDialogName = strdup(pMissionDef->name);
			}
			else
			{
				pContactDialogNode->pchSpecialDialogName = strdup(DIALOG_NODE_UNTITLED_MISSION);
			}
		}
	}
	else
	{
		pContactDialogNode->bIsDialogRoot = false;
		pContactDialogNode->pchSpecialDialogName = strdup(pchSpecialDialogName);
		pSpecialDialogBlock = CEDialogFlowGetSpecialDialogByName(pInfo, pchSpecialDialogName, pInfo->pchContactName);
	}	

	// Create the pane
	pContactDialogNode->pPane = ui_PaneCreate(0.f, 0.f, 1.f, 1.f, UIUnitPercentage, UIUnitPercentage, 0);
	pContactDialogNode->pPane->drawEvenIfInvisible = true;

	// Create the UI element that represents the dialog node
	if (pContactDialogNode->pMissionOffer == NULL)
	{
		pContactDialogNode->pGraphNode = ui_GraphNodeCreate(pContactDialogNode->pchSpecialDialogName, 0, 0, 0, 0, 
			s_ContactEditorLook.pSelectedNodeSkin, s_ContactEditorLook.pUnselectedNodeSkin);
	}
	else
	{
		char *estrWindowTitle = NULL;
		char *estrTooltipText = NULL;

		estrStackCreate(&estrWindowTitle);
		estrPrintf(&estrWindowTitle, "Mission Offer: %s", pContactDialogNode->pchSpecialDialogName);

		pContactDialogNode->pGraphNode = ui_GraphNodeCreate(estrWindowTitle, 0, 0, 0, 0, 
			s_ContactEditorLook.pSelectedNodeSkin, s_ContactEditorLook.pUnselectedNodeSkin);

		estrConcatf(&estrTooltipText, "<b>Mission Offer Type:</b> %s<br><br>", StaticDefineIntRevLookup(ContactMissionAllowEnum, pContactDialogNode->pMissionOffer->allowGrantOrReturn));

		if (pContactDialogNode->pMissionOffer->acceptStringMesg.pEditorCopy &&
			pContactDialogNode->pMissionOffer->acceptStringMesg.pEditorCopy->pcDefaultString)
		{
			estrConcatf(&estrTooltipText, "<b>Accept Text:</b> %s<br><br>", CEGetFormattedDialogText(pContactDialogNode->pMissionOffer->acceptStringMesg.pEditorCopy->pcDefaultString, GET_REF(pContactDialogNode->pMissionOffer->hAcceptDialogFormatter))); 
		}

		if (pContactDialogNode->pMissionOffer->declineStringMesg.pEditorCopy &&
			pContactDialogNode->pMissionOffer->declineStringMesg.pEditorCopy->pcDefaultString)
		{
			estrConcatf(&estrTooltipText, "<b>Decline Text:</b> %s<br><br>", CEGetFormattedDialogText(pContactDialogNode->pMissionOffer->declineStringMesg.pEditorCopy->pcDefaultString, GET_REF(pContactDialogNode->pMissionOffer->hDeclineDialogFormatter))); 
		}

		if (pContactDialogNode->pMissionOffer->turnInStringMesg.pEditorCopy &&
			pContactDialogNode->pMissionOffer->turnInStringMesg.pEditorCopy->pcDefaultString)
		{
			estrConcatf(&estrTooltipText, "<b>Turn In Text:</b> %s<br><br>", pContactDialogNode->pMissionOffer->turnInStringMesg.pEditorCopy->pcDefaultString); 
		}

		if (eaSize(&pContactDialogNode->pMissionOffer->completedDialog) > 0 && 
			pContactDialogNode->pMissionOffer->completedDialog[0]->displayTextMesg.pEditorCopy &&
			pContactDialogNode->pMissionOffer->completedDialog[0]->displayTextMesg.pEditorCopy->pcDefaultString)
		{
			estrConcatf(&estrTooltipText, "<b>Completed Text:</b> %s<br><br>", pContactDialogNode->pMissionOffer->completedDialog[0]->displayTextMesg.pEditorCopy->pcDefaultString); 
		}
		
		if (pContactDialogNode->pMissionOffer->rewardAcceptMesg.pEditorCopy &&
			pContactDialogNode->pMissionOffer->rewardAcceptMesg.pEditorCopy->pcDefaultString)
		{
			estrConcatf(&estrTooltipText, "<b>Reward Accept Text:</b> %s<br><br>", CEGetFormattedDialogText(pContactDialogNode->pMissionOffer->rewardAcceptMesg.pEditorCopy->pcDefaultString, GET_REF(pContactDialogNode->pMissionOffer->hRewardAcceptDialogFormatter))); 
		}

		if (pContactDialogNode->pMissionOffer->rewardChooseMesg.pEditorCopy &&
			pContactDialogNode->pMissionOffer->rewardChooseMesg.pEditorCopy->pcDefaultString)
		{
			estrConcatf(&estrTooltipText, "<b>Reward Choose Text:</b> %s<br><br>", CEGetFormattedDialogText(pContactDialogNode->pMissionOffer->rewardChooseMesg.pEditorCopy->pcDefaultString, GET_REF(pContactDialogNode->pMissionOffer->hRewardChooseDialogFormatter))); 
		}

		if (pContactDialogNode->pMissionOffer->rewardAbortMesg.pEditorCopy &&
			pContactDialogNode->pMissionOffer->rewardAbortMesg.pEditorCopy->pcDefaultString)
		{
			estrConcatf(&estrTooltipText, "<b>Reward Back Out Text:</b> %s<br><br>", CEGetFormattedDialogText(pContactDialogNode->pMissionOffer->rewardAbortMesg.pEditorCopy->pcDefaultString, GET_REF(pContactDialogNode->pMissionOffer->hRewardAbortDialogFormatter))); 
		}

		ui_WidgetSetTooltipString(UI_WIDGET(pContactDialogNode->pPane), estrTooltipText);

		estrDestroy(&estrTooltipText);
		estrDestroy(&estrWindowTitle);
	}

	ui_WindowAddChild(&pContactDialogNode->pGraphNode->window, pContactDialogNode->pPane);

	// Set the unselected skin
	ui_WidgetSkin((UIWidget*)&pContactDialogNode->pGraphNode->window.widget, s_ContactEditorLook.pUnselectedNodeSkin);

	ui_WidgetSetDimensions((UIWidget*)&pContactDialogNode->pGraphNode->window, DIALOG_NODE_WINDOW_WIDTH, DIALOG_NODE_WINDOW_MIN_HEIGHT);
	ui_WidgetSetPosition((UIWidget*)&pContactDialogNode->pGraphNode->window, x, y);

	// Dialog nodes can neither be closed nor resized
	ui_WindowSetClosable(&pContactDialogNode->pGraphNode->window, false);
	ui_WindowSetResizable(&pContactDialogNode->pGraphNode->window, false);
	ui_WindowSetShadable(&pContactDialogNode->pGraphNode->window, false);

	// Set the data for the UI element
	pContactDialogNode->pGraphNode->userData = pContactDialogNode;
	pContactDialogNode->pGraphNode->window.widget.dragData = pContactDialogNode;

	// Set the callbacks
	ui_WidgetSetUnfocusCallback((UIWidget *)pContactDialogNode->pGraphNode, CEOnDialogNodeLostFocus, pContactDialogNode); 
	ui_GraphNodeSetOnMouseDownCallback(pContactDialogNode->pGraphNode, CEOnDialogNodeMouseDown, pContactDialogNode);
	ui_GraphNodeSetOnMouseDragCallback(pContactDialogNode->pGraphNode, CEOnDialogNodeMouseDrag, pContactDialogNode);
	ui_GraphNodeSetOnMouseUpCallback(pContactDialogNode->pGraphNode, CEOnDialogNodeMouseUp, pContactDialogNode);

	// Create the buttons for incoming and outgoing connections
	pIncomingConnectionButton = ui_ButtonCreate("->In", 0, 0, NULL, NULL);	
	ui_WidgetSetPositionEx((UIWidget *)pIncomingConnectionButton, 5, 5, 0, 0, UITopLeft);
	ui_WidgetSetDragCallback((UIWidget *)pIncomingConnectionButton, CEOnDialogNodeDrag, pContactDialogNode);
	ui_WidgetSetDropCallback((UIWidget *)pIncomingConnectionButton, CEOnDialogNodeDrop, pContactDialogNode);
	ui_PaneAddChild(pContactDialogNode->pPane, pIncomingConnectionButton);
	pContactDialogNode->pIncomingConnectionButton = pIncomingConnectionButton;

	if (pContactDialogNode->pMissionOffer == NULL)
	{
		pAddChildDialogButton= ui_ButtonCreate("Add Child Dialog", 0, 0, NULL, NULL);
		ui_WidgetSetPositionEx((UIWidget *)pAddChildDialogButton, (DIALOG_NODE_WINDOW_WIDTH / 2) - (pAddChildDialogButton->widget.width / 2), 5, 0, 0, UITopLeft);
		ui_ButtonSetCallback(pAddChildDialogButton, CEOnAddChildDialogButtonClick, pContactDialogNode);
		ui_PaneAddChild(pContactDialogNode->pPane, pAddChildDialogButton);
		pContactDialogNode->pAddChildDialogButton = pAddChildDialogButton;
	}
	else
	{
		UILabel *pLabel = ui_LabelCreate(pContactDialogNode->pchSpecialDialogName, 0.f, 5.f);
		ui_WidgetSetDimensionsEx(UI_WIDGET(pLabel), 0.8f, UI_WIDGET(pLabel)->height, UIUnitPercentage, UIUnitFixed);
		ui_WidgetSetPositionEx(UI_WIDGET(pLabel), 0.f, 5.f, 0, 0, UITopRight);
		ui_PaneAddChild(pContactDialogNode->pPane, pLabel);
	}

	if (!pContactDialogNode->bIsDialogRoot && pContactDialogNode->pMissionOffer == NULL)
	{
		pOutgoingConnectionButton = ui_ButtonCreate("Out", 0, 0, NULL, NULL);
		ui_WidgetSetPositionEx((UIWidget *)pOutgoingConnectionButton, 5, 5, 0, 0, UITopRight);
		ui_WidgetSetDragCallback((UIWidget *)pOutgoingConnectionButton, CEOnDialogNodeDrag, pContactDialogNode);
		ui_WidgetSetDropCallback((UIWidget *)pOutgoingConnectionButton, CEOnDialogNodeDrop, pContactDialogNode);
		ui_PaneAddChild(pContactDialogNode->pPane, pOutgoingConnectionButton);
		pContactDialogNode->pOutgoingConnectionButton = pOutgoingConnectionButton;
	}

	return pContactDialogNode;
}

// Called when the user drags a dialog action connection button
static void CEOnDialogNodeActionConnectDrag(UIButton *pConnectionButton, ContactDialogUINodeAction *pDialogNodeAction)
{
	ui_DragStart(UI_WIDGET(pConnectionButton), DIALOG_NODE_ACTION_CONNECT_PAYLOAD, pDialogNodeAction, atlasLoadTexture("button_pinned.tga"));
}

// Called when the user drops a dialog action connection button
static void CEOnDialogNodeActionConnectDrop(UIButton *pSourceButton, UIButton *pDestConnectionButton, UIDnDPayload *payload, ContactDialogUINodeAction *pDestDialogNodeAction)
{
	ContactDialogUINode *pSourceDialogNode = NULL;

	// Verify the correct payload type
	if (strcmpi(payload->type, DIALOG_NODE_CONNECT_PAYLOAD) == 0)
	{
		pSourceDialogNode = (ContactDialogUINode *)payload->payload;
	}

	if (pSourceDialogNode && 
		pDestDialogNodeAction &&
		pDestDialogNodeAction->pParentDialogNode)
	{
		// Source and destination should be different
		if (pSourceDialogNode == pDestDialogNodeAction->pParentDialogNode)
		{
			emStatusPrintf("Cannot connect a dialog node to itself!");
			ui_DragCancel();
			return;
		}

		// Check that the source button is an input button
		if (pSourceButton == pSourceDialogNode->pOutgoingConnectionButton)
		{
			emStatusPrintf("Cannot connect an action to a special dialog output!");
			ui_DragCancel();
			return;
		}
		
		// Everything is validated
		CEConnectActionWithDialogNode(pDestDialogNodeAction, pSourceDialogNode);
	}
}

// Handles the deletion of a dialog node action
static void CEOnDeleteDialogNodeAction(UIButton *pButton, ContactDialogUINodeAction *pAction)
{
	if (pAction && 
		pAction->pSpecialDialogAction &&
		pAction->pParentDialogNode &&
		pAction->pParentDialogNode->pInfo)
	{
		// Check if editable
		if (!emDocIsEditable(CEGetEMEditorDocFromDialogNode(pAction->pParentDialogNode), true))
		{
			ui_DragCancel();
			return;
		}
		else
		{
			// Find the special dialog block
			SpecialDialogBlock *pSpecialDialogBlock = CEDialogFlowGetSpecialDialogByName(pAction->pParentDialogNode->pInfo, pAction->pParentDialogNode->pchSpecialDialogName, pAction->pParentDialogNode->pInfo->pchContactName);

			if (pSpecialDialogBlock)
			{
				FOR_EACH_IN_EARRAY(pSpecialDialogBlock->dialogActions, SpecialDialogAction, pCurrentAction)
				{
					if (pCurrentAction == pAction->pSpecialDialogAction)
					{
						// Remove from the list
						eaRemove(&pSpecialDialogBlock->dialogActions, FOR_EACH_IDX(pSpecialDialogBlock->dialogActions, pCurrentAction));
						// Destroy the struct
						StructDestroy(parse_SpecialDialogAction, pAction->pSpecialDialogAction);
						// Refresh the special dialog expander
						CEDialogFlowWindowRefreshSpecialExpander(pAction->pParentDialogNode->pInfo);
						// Refresh the window
						CERefreshDialogFlowWindow(pAction->pParentDialogNode->pInfo);					
						return;
					}
				}
				FOR_EACH_END
			}
		}
	}
}

// Creates a dialog node action
static ContactDialogUINodeAction * CECreateDialogNodeAction(SA_PARAM_NN_VALID ContactDialogUINode *pDialogNode,
															SA_PARAM_OP_VALID SpecialDialogBlock *pSpecialDialogBlock, // For root node
															SA_PARAM_OP_VALID SpecialDialogAction *pSpecialDialogAction, // For special dialog nodes
															S32 iIndex, F32 x, F32 y)
{
	ContactDialogUINodeAction *pDialogNodeAction = NULL;
	if (pDialogNode && 
		((pSpecialDialogBlock && pDialogNode->bIsDialogRoot) || (pSpecialDialogAction && !pDialogNode->bIsDialogRoot)))
	{
		ContactDialogUINode *pTargetDialogNode = NULL;
		F32 fYPos = 0.f;
		UIButton *pConnectActionButton;
		UIButton *pDeleteActionButton;
		UILabel *pLabel;
		UIPairedBox *pOutgoingPairedBox;
		static char *estrLabel = NULL;
		const char *pchBottomLabelText = NULL;
		char *estrDisplayText = NULL;

		pDialogNodeAction = calloc(1, sizeof(ContactDialogUINodeAction));
		pDialogNodeAction->pParentDialogNode = pDialogNode;
		pDialogNodeAction->pSpecialDialogAction = pSpecialDialogAction;
		pDialogNodeAction->pSpecialDialogBlock = pSpecialDialogBlock;

		// Create the pane
		pDialogNodeAction->pPane = ui_PaneCreate(x, y, DIALOG_NODE_WINDOW_WIDTH - 20.f, DIALOG_NODE_ACTION_PANE_HEIGHT, UIUnitFixed, UIUnitFixed, 0);
		pDialogNodeAction->pPane->drawEvenIfInvisible = true;

		// Prepare the tooltip text
		if (pDialogNode->bIsDialogRoot)
		{
			// Generate the display text string
			FOR_EACH_IN_CONST_EARRAY_FORWARDS(pSpecialDialogBlock->dialogBlock, DialogBlock, pDialogBlock)
			{
				estrConcatf(&estrDisplayText, "<b>Display Text %d:</b> %s<br><br>", 
					FOR_EACH_IDX(pSpecialDialogBlock->dialogBlock, pDialogBlock) + 1, 
					pDialogBlock->displayTextMesg.pEditorCopy && pDialogBlock->displayTextMesg.pEditorCopy->pcDefaultString ? CEGetFormattedDialogText(pDialogBlock->displayTextMesg.pEditorCopy->pcDefaultString, GET_REF(pDialogBlock->hDialogFormatter)) : "");

				estrConcatf(&estrDisplayText, "<b>Continue Text %d:</b> %s<br><br>", 
					FOR_EACH_IDX(pSpecialDialogBlock->dialogBlock, pDialogBlock) + 1, 
					pDialogBlock->continueTextMesg.pEditorCopy && pDialogBlock->continueTextMesg.pEditorCopy->pcDefaultString ? CEGetFormattedDialogText(pDialogBlock->continueTextMesg.pEditorCopy->pcDefaultString, GET_REF(pDialogBlock->hContinueTextDialogFormatter)) : "");
			}
			FOR_EACH_END

			estrPrintf(&estrLabel, 
				"<b>Special Dialog Name:</b> %s<br><br>"
				"%s"
				"<b>Condition:</b> %s",
				pSpecialDialogBlock->name,
				estrDisplayText,
				pSpecialDialogBlock->pCondition ? exprGetCompleteString(pSpecialDialogBlock->pCondition) : "");
		}
		else
		{
			SpecialDialogBlock *pParentSpecialDialogBlock = CEDialogFlowGetSpecialDialogByName(pDialogNode->pInfo, pDialogNode->pchSpecialDialogName, pDialogNode->pInfo->pchContactName);
			char *estrActionsText = NULL;

			if (pParentSpecialDialogBlock)
			{
				// Generate the display text string
				FOR_EACH_IN_CONST_EARRAY_FORWARDS(pParentSpecialDialogBlock->dialogBlock, DialogBlock, pDialogBlock)
				{
					estrConcatf(&estrDisplayText, "<b>Display Text %d:</b> %s<br><br>", 
						FOR_EACH_IDX(pSpecialDialogBlock->dialogBlock, pDialogBlock) + 1, 
						pDialogBlock->displayTextMesg.pEditorCopy && pDialogBlock->displayTextMesg.pEditorCopy->pcDefaultString ? CEGetFormattedDialogText(pDialogBlock->displayTextMesg.pEditorCopy->pcDefaultString, GET_REF(pDialogBlock->hDialogFormatter)) : "");

					estrConcatf(&estrDisplayText, "<b>Continue Text %d:</b> %s<br><br>", 
						FOR_EACH_IDX(pSpecialDialogBlock->dialogBlock, pDialogBlock) + 1, 
						pDialogBlock->continueTextMesg.pEditorCopy && pDialogBlock->continueTextMesg.pEditorCopy->pcDefaultString ? CEGetFormattedDialogText(pDialogBlock->continueTextMesg.pEditorCopy->pcDefaultString, GET_REF(pDialogBlock->hContinueTextDialogFormatter)) : "");
				}
				FOR_EACH_END
			}

			estrCopy2(&estrActionsText, "<b>Game Actions:</b><br>");

			FOR_EACH_IN_CONST_EARRAY_FORWARDS(pSpecialDialogAction->actionBlock.eaActions, WorldGameActionProperties, pActionProperties)
			{
				estrAppend2(&estrActionsText, "<b>Type</b>: ");
				estrAppend2(&estrActionsText, StaticDefineIntRevLookup(WorldGameActionTypeEnum, pActionProperties->eActionType));
				switch (pActionProperties->eActionType)
				{
				case WorldGameActionType_GrantMission:
					estrConcatf(&estrActionsText, ", <b>Mission:</b> %s", REF_STRING_FROM_HANDLE(pActionProperties->pGrantMissionProperties->hMissionDef));
					break;
				case WorldGameActionType_GrantSubMission:
					estrConcatf(&estrActionsText, ", <b>Submission:</b> %s", pActionProperties->pGrantSubMissionProperties->pcSubMissionName);
					break;
				case WorldGameActionType_Contact:
					estrConcatf(&estrActionsText, ", <b>Contact:</b> %s", REF_STRING_FROM_HANDLE(pActionProperties->pContactProperties->hContactDef));
					break;
				case WorldGameActionType_GiveItem:
					estrConcatf(&estrActionsText, ", <b>Item:</b> %s", REF_STRING_FROM_HANDLE(pActionProperties->pGiveItemProperties->hItemDef));
					break;
				case WorldGameActionType_Warp:
					if (pActionProperties->pWarpProperties->warpDest.pSpecificValue)
					{
						estrConcatf(&estrActionsText, ", <b>Map Name:</b> %s, <b>Spawn Name:</b> %s", pActionProperties->pWarpProperties->warpDest.pSpecificValue->pcZoneMap, 
							pActionProperties->pWarpProperties->warpDest.pSpecificValue->pcStringVal);
					}					
					break;
				case WorldGameActionType_MissionOffer:
					estrConcatf(&estrActionsText, ", <b>Mission:</b> %s", REF_STRING_FROM_HANDLE(pActionProperties->pMissionOfferProperties->hMissionDef));
					break;
				case WorldGameActionType_Expression:
					if (pActionProperties->pExpressionProperties->pExpression)
					{
						estrConcatf(&estrActionsText, ", <b>Expression:</b> %s", exprGetCompleteString(pActionProperties->pExpressionProperties->pExpression));
					}					
					break;
				}

				estrAppend2(&estrActionsText, "<br>");
			}
			FOR_EACH_END

			estrPrintf(&estrLabel, 
				"<b>Special Dialog Name:</b> %s<br><br>"
				"%s"
				"<b>Condition:</b> %s<br><br>"
				"<b>Action Display Text:</b> %s<br><br>"
				"%s",
				pDialogNode->pchSpecialDialogName,
				estrDisplayText,
				pParentSpecialDialogBlock && pParentSpecialDialogBlock->pCondition ? exprGetCompleteString(pParentSpecialDialogBlock->pCondition) : "",
				pSpecialDialogAction->displayNameMesg.pEditorCopy && pSpecialDialogAction->displayNameMesg.pEditorCopy->pcDefaultString ? CEGetFormattedDialogText(pSpecialDialogAction->displayNameMesg.pEditorCopy->pcDefaultString , GET_REF(pSpecialDialogAction->hDisplayNameFormatter)) : CE_UNTITLED_ACTION,
				estrActionsText);

			estrDestroy(&estrActionsText);			
		}
		estrDestroy(&estrDisplayText);

		// Set the tooltip for the pane
		ui_WidgetSetTooltipString(UI_WIDGET(pDialogNodeAction->pPane), estrLabel);

		// Add the label showing the action index
		if (pDialogNode->bIsDialogRoot)
		{
			estrPrintf(&estrLabel, "Special Dialog %d", iIndex + 1);
		}
		else
		{
			estrPrintf(&estrLabel, "Action %d", iIndex + 1);
		}

		pLabel = ui_LabelCreate(estrLabel, 0, fYPos);
		ui_WidgetSetDimensionsEx(UI_WIDGET(pLabel), 1.f, UI_WIDGET(pLabel)->height, UIUnitPercentage, UIUnitFixed);
		ui_WidgetSetPositionEx(UI_WIDGET(pLabel), 0, fYPos, 0, 0, UITopLeft);
		ui_PaneAddChild(pDialogNodeAction->pPane, (UIWidget *)pLabel);

		if (!pDialogNode->bIsDialogRoot)
		{
			// Add the delete button
			pDeleteActionButton = ui_ButtonCreateImageOnly("button_close", 0, fYPos, NULL, NULL);
			ui_ButtonSetCallback(pDeleteActionButton, CEOnDeleteDialogNodeAction, pDialogNodeAction);
			ui_WidgetSetDimensions((UIWidget *)pDeleteActionButton, 11, 11);
			ui_ButtonSetImageStretch( pDeleteActionButton, true );
			ui_WidgetSetPositionEx((UIWidget *)pDeleteActionButton, 25, 5, 0, 0, UITopRight);
			ui_PaneAddChild(pDialogNodeAction->pPane, (UIWidget *)pDeleteActionButton);
		}

		// Add the drag button
		pConnectActionButton = ui_ButtonCreate(NULL, 0, 0, NULL, NULL);
		ui_WidgetSetDimensions((UIWidget *)pConnectActionButton, 11, 11);
		ui_WidgetSetPositionEx((UIWidget *)pConnectActionButton, 5, 5, 0, 0, UITopRight);		
		if (!pDialogNode->bIsDialogRoot)
		{
			ui_WidgetSetDragCallback((UIWidget *)pConnectActionButton, CEOnDialogNodeActionConnectDrag, pDialogNodeAction);
			ui_WidgetSetDropCallback((UIWidget *)pConnectActionButton, CEOnDialogNodeActionConnectDrop, pDialogNodeAction);
		}
		ui_PaneAddChild(pDialogNodeAction->pPane, (UIWidget *)pConnectActionButton);

		// Create the connection to the other node this action links to
		pOutgoingPairedBox = ui_PairedBoxCreate(ColorGray);
		ui_WidgetSetDimensions((UIWidget *)pOutgoingPairedBox, 0, 0);
		ui_WidgetSetPositionEx((UIWidget *)pOutgoingPairedBox, 5, 11, 0, 0, UITopRight);
		ui_PaneAddChild(pDialogNodeAction->pPane, (UIWidget *)pOutgoingPairedBox);

		if (pDialogNode->bIsDialogRoot)
		{
			pTargetDialogNode = CEDialogUINodeFromName(pDialogNode->pInfo, pSpecialDialogBlock->name);
		}
		else if ((pSpecialDialogAction->dialogName == NULL ||
				pSpecialDialogAction->dialogName[0] == '\0' ||
				(pTargetDialogNode = CEDialogUINodeFromName(pDialogNode->pInfo, pSpecialDialogAction->dialogName)) == NULL))
		{
			// Connect to dialog root
			pTargetDialogNode = CEGetRootDialogUINode(pDialogNode->pInfo);
		}

		if (pTargetDialogNode)
		{
			UIPairedBox *pIncomingPairedBox = ui_PairedBoxCreate(ColorGray);
			ui_PairedBoxConnect(pOutgoingPairedBox, pIncomingPairedBox, pDialogNode->pInfo->pDialogFlowScrollArea);
			ui_PaneAddChild(pTargetDialogNode->pPane, pIncomingPairedBox);
			ui_WidgetSetPositionEx((UIWidget *)pIncomingPairedBox,
				pTargetDialogNode->pIncomingConnectionButton->widget.x, 
				pTargetDialogNode->pIncomingConnectionButton->widget.y + pTargetDialogNode->pIncomingConnectionButton->widget.height / 2, 
				0, 0, UITopLeft);
			ui_WidgetSetDimensions((UIWidget *)pIncomingPairedBox, 0, 0);

			// Add the outgoing paired box as an outgoing connection to this node
			eaPush(&pDialogNode->eaOutgoingPairedBoxes, pOutgoingPairedBox);

			// Add the incoming paired box as an incoming connection to the target node
			eaPush(&pTargetDialogNode->eaIncomingPairedBoxes, pIncomingPairedBox);
		}

		fYPos += UI_WIDGET(pLabel)->height;

		// Add the label
		if (pDialogNode->bIsDialogRoot)
		{
			pchBottomLabelText = pSpecialDialogBlock->name;
		}
		else
		{
			if (pSpecialDialogAction->displayNameMesg.pEditorCopy &&
				pSpecialDialogAction->displayNameMesg.pEditorCopy->pcDefaultString)
			{
				pchBottomLabelText = pSpecialDialogAction->displayNameMesg.pEditorCopy->pcDefaultString;
			}			
			else
			{
				pchBottomLabelText = CE_UNTITLED_ACTION;
			}
		}
		pLabel = ui_LabelCreate(pchBottomLabelText, 0, fYPos);
		ui_WidgetSetDimensionsEx(UI_WIDGET(pLabel), 1.f, UI_WIDGET(pLabel)->height, UIUnitPercentage, UIUnitFixed);
		ui_WidgetSetPositionEx(UI_WIDGET(pLabel), 0, fYPos, 0, 0, UITopLeft);
		ui_PaneAddChild(pDialogNodeAction->pPane, (UIWidget *)pLabel);
	}

	return pDialogNodeAction;
}

// Populates all actions for a dialog node
static void CEPopulateDialogNodeActions(SA_PARAM_OP_VALID SpecialDialogBlock *pSpecialDialog, SA_PARAM_NN_VALID ContactDialogUINode *pDialogNode)
{
	F32 fYPos = DIALOG_NODE_WINDOW_MIN_HEIGHT;
	S32 iActionCount = 0;

	if (pDialogNode == NULL || (pSpecialDialog == NULL && !pDialogNode->bIsDialogRoot))
		return;

	// Clear the existing list of actions
	FOR_EACH_IN_EARRAY_FORWARDS(pDialogNode->eaActions, ContactDialogUINodeAction, pDialogNodeAction)
	{
		CEDestroyDialogNodeAction(pDialogNodeAction);
	}
	FOR_EACH_END

	// Clear the array
	eaClear(&pDialogNode->eaActions);

	if (pDialogNode->bIsDialogRoot) // Dialog root
	{
		SpecialDialogBlock **eaSpecialDialogs = NULL;

		CEDialogFlowGetSpecialDialogs(pDialogNode->pInfo, &eaSpecialDialogs);

		// Add all special dialogs whose conditions are not 0
		FOR_EACH_IN_EARRAY_FORWARDS(eaSpecialDialogs, SpecialDialogBlock, pCurrentSpecialDialog)
		{
			if (pCurrentSpecialDialog)
			{
				S32 iDialogBlockCount = eaSize(&pCurrentSpecialDialog->dialogBlock);

				char *estrTrimmedExpressionLine = NULL;

				if (iDialogBlockCount > 0 && 
					pCurrentSpecialDialog->pCondition != NULL)
				{
					exprGetCompleteStringEstr(pCurrentSpecialDialog->pCondition,  &estrTrimmedExpressionLine);

					if (estrTrimmedExpressionLine != NULL)
					{
						// Trim the string
						estrTrimLeadingAndTrailingWhitespace(&estrTrimmedExpressionLine);
					}
				}
			
				if (estrTrimmedExpressionLine == NULL ||
					strcmp(estrTrimmedExpressionLine, "0") != 0)
				{					
					// This special dialog must be connected to the root
					ContactDialogUINodeAction *pDialogNodeAction = CECreateDialogNodeAction(pDialogNode, pCurrentSpecialDialog, NULL, iActionCount, 10.f, fYPos);

					eaPush(&pDialogNode->eaRootLevelSpecialDialogBlocks, pCurrentSpecialDialog);

					if (pDialogNodeAction)
					{
						ui_PaneAddChild(pDialogNode->pPane, pDialogNodeAction->pPane);
						eaPush(&pDialogNode->eaActions, pDialogNodeAction);

						fYPos += DIALOG_NODE_ACTION_PANE_HEIGHT + DIALOG_NODE_ACTION_PANE_DISTANCE;
						iActionCount++;
					}
				}

				estrDestroy(&estrTrimmedExpressionLine);
			}
		}
		FOR_EACH_END

		eaDestroy(&eaSpecialDialogs);
	}
	else
	{
		// Iterate through all special dialog actions
		FOR_EACH_IN_EARRAY_FORWARDS(pSpecialDialog->dialogActions, SpecialDialogAction, pSpecialDialogAction)
		{
			if (pSpecialDialogAction)
			{
				ContactDialogUINodeAction *pDialogNodeAction = CECreateDialogNodeAction(pDialogNode, NULL, pSpecialDialogAction, iActionCount, 10.f, fYPos);
				if (pDialogNodeAction)
				{
					ui_PaneAddChild(pDialogNode->pPane, pDialogNodeAction->pPane);
					eaPush(&pDialogNode->eaActions, pDialogNodeAction);

					fYPos += DIALOG_NODE_ACTION_PANE_HEIGHT + DIALOG_NODE_ACTION_PANE_DISTANCE;
					iActionCount++;
				}
			}
		}
		FOR_EACH_END
	}

	// Add the clone button
	if (!pDialogNode->bIsDialogRoot && pDialogNode->pchSpecialDialogName && pDialogNode->pchSpecialDialogName[0])
	{
		pDialogNode->pCloneDialogButton = ui_ButtonCreate("Clone Dialog", 0, 0, NULL, NULL);
		ui_WidgetSetWidth(UI_WIDGET(pDialogNode->pCloneDialogButton), DIALOG_NODE_WINDOW_WIDTH - 20.f);
		ui_WidgetSetPositionEx((UIWidget *)pDialogNode->pCloneDialogButton, 5.f, fYPos, 0, 0, UITopLeft);
		ui_ButtonSetCallback(pDialogNode->pCloneDialogButton, CEOnCloneDialogButtonClick, pDialogNode);
		ui_PaneAddChild(pDialogNode->pPane, pDialogNode->pCloneDialogButton);

		// Set the height of the window based on the number of actions
		ui_WidgetSetDimensions((UIWidget*)&pDialogNode->pGraphNode->window,
			DIALOG_NODE_WINDOW_WIDTH, 
			DIALOG_NODE_WINDOW_MIN_HEIGHT + 
			43.f + // For the clone dialog button
			(iActionCount * DIALOG_NODE_ACTION_PANE_HEIGHT) + 
			(MAX(0, iActionCount - 1) * DIALOG_NODE_ACTION_PANE_DISTANCE));
	}
	else
	{
		// Set the height of the window based on the number of actions
		ui_WidgetSetDimensions((UIWidget*)&pDialogNode->pGraphNode->window,
			DIALOG_NODE_WINDOW_WIDTH, 
			DIALOG_NODE_WINDOW_MIN_HEIGHT +
			(iActionCount * DIALOG_NODE_ACTION_PANE_HEIGHT) + 
			(MAX(0, iActionCount - 1) * DIALOG_NODE_ACTION_PANE_DISTANCE));
	}


}

static void CEPopulateActionBlockNodeActions(SA_PARAM_OP_VALID SpecialActionBlock *pActionBlock, SA_PARAM_NN_VALID ContactDialogUINode *pDialogNode) {
	F32 fYPos = DIALOG_NODE_WINDOW_MIN_HEIGHT;
	S32 iActionCount = 0;

	if (pDialogNode == NULL || (pActionBlock == NULL && !pDialogNode->bIsDialogRoot))
		return;

	// Clear the existing list of actions
	FOR_EACH_IN_EARRAY_FORWARDS(pDialogNode->eaActions, ContactDialogUINodeAction, pDialogNodeAction)
	{
		CEDestroyDialogNodeAction(pDialogNodeAction);
	}
	FOR_EACH_END

		// Clear the array
		eaClear(&pDialogNode->eaActions);

	if (pDialogNode->bIsDialogRoot) // Dialog root
	{
		//SpecialActionBlock **eaActionBlocks = NULL;

		////CEDialogFlowGetSpecialDialogs(pDialogNode->pInfo, &eaSpecialDialogs);

		//// Add all special dialogs whose conditions are not 0
		//FOR_EACH_IN_EARRAY_FORWARDS(eaActionBlocks, SpecialActionBlock, pCurrentActionBlock)
		//{
		//	//if (pCurrentActionBlock)
		//	//{
		//	//	//S32 iDialogBlockCount = eaSize(&pCurrentActionBlock->dialogBlock);

		//	//	char *estrTrimmedExpressionLine = NULL;

		//	//	if (iDialogBlockCount > 0 && 
		//	//		pCurrentSpecialDialog->pCondition != NULL)
		//	//	{
		//	//		exprGetCompleteStringEstr(pCurrentSpecialDialog->pCondition,  &estrTrimmedExpressionLine);

		//	//		if (estrTrimmedExpressionLine != NULL)
		//	//		{
		//	//			// Trim the string
		//	//			estrTrimLeadingAndTrailingWhitespace(&estrTrimmedExpressionLine);
		//	//		}
		//	//	}

		//	//	if (estrTrimmedExpressionLine == NULL ||
		//	//		strcmp(estrTrimmedExpressionLine, "0") != 0)
		//	//	{					
		//	//		// This special dialog must be connected to the root
		//	//		ContactDialogUINodeAction *pDialogNodeAction = CECreateDialogNodeAction(pDialogNode, pCurrentSpecialDialog, NULL, iActionCount, 10.f, fYPos);

		//	//		eaPush(&pDialogNode->eaRootLevelSpecialDialogBlocks, pCurrentSpecialDialog);

		//	//		if (pDialogNodeAction)
		//	//		{
		//	//			ui_PaneAddChild(pDialogNode->pPane, pDialogNodeAction->pPane);
		//	//			eaPush(&pDialogNode->eaActions, pDialogNodeAction);

		//	//			fYPos += DIALOG_NODE_ACTION_PANE_HEIGHT + DIALOG_NODE_ACTION_PANE_DISTANCE;
		//	//			iActionCount++;
		//	//		}
		//	//	}

		//	//	estrDestroy(&estrTrimmedExpressionLine);
		//	//}
		//}
		//FOR_EACH_END

		////eaDestroy(&eaSpecialDialogs);
	}
	else
	{
		// Iterate through all special dialog actions
		FOR_EACH_IN_EARRAY_FORWARDS(pActionBlock->dialogActions, SpecialDialogAction, pSpecialDialogAction)
		{
			if (pSpecialDialogAction)
			{
				ContactDialogUINodeAction *pDialogNodeAction = CECreateDialogNodeAction(pDialogNode, NULL, pSpecialDialogAction, iActionCount, 10.f, fYPos);
				if (pDialogNodeAction)
				{
					ui_PaneAddChild(pDialogNode->pPane, pDialogNodeAction->pPane);
					eaPush(&pDialogNode->eaActions, pDialogNodeAction);

					fYPos += DIALOG_NODE_ACTION_PANE_HEIGHT + DIALOG_NODE_ACTION_PANE_DISTANCE;
					iActionCount++;
				}
			}
		}
		FOR_EACH_END
	}

	//// Add the clone button
	//if (!pDialogNode->bIsDialogRoot && pDialogNode->pchSpecialDialogName && pDialogNode->pchSpecialDialogName[0])
	//{
	//	pDialogNode->pCloneDialogButton = ui_ButtonCreate("Clone Dialog", 0, 0, NULL, NULL);
	//	ui_WidgetSetWidth(UI_WIDGET(pDialogNode->pCloneDialogButton), DIALOG_NODE_WINDOW_WIDTH - 20.f);
	//	ui_WidgetSetPositionEx((UIWidget *)pDialogNode->pCloneDialogButton, 5.f, fYPos, 0, 0, UITopLeft);
	//	ui_ButtonSetCallback(pDialogNode->pCloneDialogButton, CEOnCloneDialogButtonClick, pDialogNode);
	//	ui_PaneAddChild(pDialogNode->pPane, pDialogNode->pCloneDialogButton);

	// Set the height of the window based on the number of actions
	//ui_WidgetSetDimensions((UIWidget*)&pDialogNode->pGraphNode->window,
	//		DIALOG_NODE_WINDOW_WIDTH, 
	//		DIALOG_NODE_WINDOW_MIN_HEIGHT + 
	//		43.f + // For the clone dialog button
	//		(iActionCount * DIALOG_NODE_ACTION_PANE_HEIGHT) + 
	//		(MAX(0, iActionCount - 1) * DIALOG_NODE_ACTION_PANE_DISTANCE));
	//}
	//else
	{
		// Set the height of the window based on the number of actions
		ui_WidgetSetDimensions((UIWidget*)&pDialogNode->pGraphNode->window,
			DIALOG_NODE_WINDOW_WIDTH, 
			DIALOG_NODE_WINDOW_MIN_HEIGHT +
			(iActionCount * DIALOG_NODE_ACTION_PANE_HEIGHT) + 
			(MAX(0, iActionCount - 1) * DIALOG_NODE_ACTION_PANE_DISTANCE));
	}
}

static S32 CEAutoArrangeGetXPosForDepth(S32 iDepth)
{
	return DIALOG_NODE_AUTO_ARRANGE_BEGIN_XPOS + (iDepth * (DIALOG_NODE_WINDOW_WIDTH + DIALOG_NODE_AUTO_ARRANGE_HSPACING));
}

// Places an individual dialog node in its proper place for auto arrangement
static void CEAutoArrangeIndividualNode(SA_PARAM_NN_VALID DialogFlowWindowInfo *pInfo, SA_PARAM_NN_VALID SpecialDialogBlock *pSpecialDialogBlock, S32 iDepth, S32 iIndex, S32 *pCurrentYPos)
{
	devassert(pCurrentYPos);

	if (pInfo && pSpecialDialogBlock && pCurrentYPos)
	{
		// Get the dialog node
		ContactDialogUINode *pDialogNode = CEDialogUINodeFromName(pInfo, pSpecialDialogBlock->name);
		SpecialDialogBlock *pTargetSpecialDialogBlock = NULL;
		S32 iCurrentIndex = 0;
		S32 iInitialYPos = *pCurrentYPos;

		if (pDialogNode && 
			!pDialogNode->bIsVisitedInAutoArrangeMode)
		{
			S32 iHeightDelta = pDialogNode->pGraphNode->window.widget.height + DIALOG_NODE_AUTO_ARRANGE_VSPACING;
			if (iDepth == 1 && iIndex == 0) // Hack for the first item
			{
				iHeightDelta += 22;
			}
			// Mark the node as visited
			pDialogNode->bIsVisitedInAutoArrangeMode = true;

			// Position this window
			ui_WidgetSetPosition((UIWidget*)&pDialogNode->pGraphNode->window, CEAutoArrangeGetXPosForDepth(iDepth), *pCurrentYPos);

			// Iterate thru all special dialog actions
			FOR_EACH_IN_EARRAY_FORWARDS(pSpecialDialogBlock->dialogActions, SpecialDialogAction, pSpecialDialogAction)
			{
				if (pSpecialDialogAction && 
					pSpecialDialogAction->dialogName &&
					(pTargetSpecialDialogBlock = CEDialogFlowGetSpecialDialogByName(pInfo, pSpecialDialogAction->dialogName, pInfo->pchContactName)) != NULL)
				{
					CEAutoArrangeIndividualNode(pInfo, pTargetSpecialDialogBlock, iDepth + 1, iCurrentIndex, pCurrentYPos);
					++iCurrentIndex;
				}				
			}
			FOR_EACH_END

			if (iCurrentIndex == 0 ||
				*pCurrentYPos < (iInitialYPos + iHeightDelta))
			{
				// Increment the Y position
				(*pCurrentYPos) = iInitialYPos + iHeightDelta;
			}
		}
	}	
}

// Automatically arranges the UI for the dialog nodes
static void CEAutoArrangeDialogNodeUI(SA_PARAM_NN_VALID DialogFlowWindowInfo *pInfo)
{
	ContactDialogUINode *pDialogNode = NULL;
	S32 itNode = 0;
	S32 y = DIALOG_NODE_AUTO_ARRANGE_BEGIN_YPOS;
	S32 yPosForMissionOffers = DIALOG_NODE_AUTO_ARRANGE_BEGIN_YPOS;

	if (pInfo->pMissionDoc)
	{
		// Leave some space for the contact name label
		y += 50;
		yPosForMissionOffers += 50;
	}

	// Mark all nodes as not visited
	FOR_EACH_IN_EARRAY_FORWARDS(pInfo->eaDialogNodes, ContactDialogUINode, pNodeTmp)
	{
		pNodeTmp->bIsVisitedInAutoArrangeMode = false;
	}
	FOR_EACH_END

	// Place the dialog root
	pDialogNode = CEGetRootDialogUINode(pInfo);

	if (pDialogNode)
	{		
		S32 iCurrentIndex = 0;

		pDialogNode->bIsVisitedInAutoArrangeMode = true;
		ui_WidgetSetPosition((UIWidget*)&pDialogNode->pGraphNode->window, CEAutoArrangeGetXPosForDepth(0), y);

		// Now handle all children
		FOR_EACH_IN_EARRAY_FORWARDS(pDialogNode->eaRootLevelSpecialDialogBlocks, SpecialDialogBlock, pSpecialDialogBlock)
		{
			if (pSpecialDialogBlock)
			{
				CEAutoArrangeIndividualNode(pInfo, pSpecialDialogBlock, 1, iCurrentIndex, &y);
				++iCurrentIndex;
			}
		}
		FOR_EACH_END	
	}

	// Place mission offers right underneath dialog root
	pDialogNode = CEGetRootDialogUINode(pInfo);
	if (pDialogNode)
	{
		yPosForMissionOffers += pDialogNode->pGraphNode->window.widget.height + (DIALOG_NODE_AUTO_ARRANGE_VSPACING * 2);

		FOR_EACH_IN_EARRAY_FORWARDS(pInfo->eaDialogNodes, ContactDialogUINode, pNodeTmp)
		{
			if (pNodeTmp->pMissionOffer != NULL && !pNodeTmp->bIsVisitedInAutoArrangeMode)
			{
				// Mark the node as visited
				pNodeTmp->bIsVisitedInAutoArrangeMode = true;

				// Position this window
				ui_WidgetSetPosition((UIWidget*)&pNodeTmp->pGraphNode->window, CEAutoArrangeGetXPosForDepth(0), yPosForMissionOffers);

				// Increment the y position
				yPosForMissionOffers += pNodeTmp->pGraphNode->window.widget.height + DIALOG_NODE_AUTO_ARRANGE_VSPACING;
			}
		}
		FOR_EACH_END

			y = MAX(yPosForMissionOffers, y);

		y += DIALOG_NODE_AUTO_ARRANGE_VSPACING;

		// Handle all non visited nodes
		FOR_EACH_IN_EARRAY_FORWARDS(pInfo->eaDialogNodes, ContactDialogUINode, pNodeTmp)
		{
			if (pNodeTmp->pMissionOffer == NULL && !pNodeTmp->bIsVisitedInAutoArrangeMode)
			{
				SpecialDialogBlock *pSpecialDialogBlock = CEDialogFlowGetSpecialDialogByName(pInfo, pNodeTmp->pchSpecialDialogName, pInfo->pchContactName);
				if (pSpecialDialogBlock)
					CEAutoArrangeIndividualNode(pInfo, pSpecialDialogBlock, 0, -1, &y);
			}
		}
		FOR_EACH_END
	}
}

static void CEOnDialogFlowMenuActionAutoArrange(UIMenuItem *pMenuItem, SA_PARAM_NN_VALID DialogFlowWindowInfo *pInfo)
{
	if (pInfo)
	{
		// Refresh the dialog flow window
		CERefreshDialogFlowWindow(pInfo);

		// Auto arrange
		CEAutoArrangeDialogNodeUI(pInfo);

		// Save positions
		CESaveDialogNodeWindowPositions(pInfo);
	}
}

// Adds all the mission offers based on the doc set to the given array
static void CEDialogFlowGetMissionOffers(SA_PARAM_NN_VALID DialogFlowWindowInfo *pInfo, 
										SA_PARAM_NN_VALID ContactMissionOffer ***peaMissionOffers)
{
	devassert(pInfo && peaMissionOffers);
	if (pInfo && peaMissionOffers)
	{
		if (pInfo->pContactDoc)
		{
			eaCopy(peaMissionOffers, &pInfo->pContactDoc->pContact->offerList);
		}
		else if (pInfo->pMissionDoc && pInfo->pchContactName && pInfo->pchContactName[0])
		{
			FOR_EACH_IN_EARRAY_FORWARDS(pInfo->pMissionDoc->pMission->ppMissionOfferOverrides, MissionOfferOverride, pOverride)
			{
				if (pOverride && pOverride->pMissionOffer && stricmp(pInfo->pchContactName, pOverride->pcContactName) == 0)
				{
					eaPush(peaMissionOffers, pOverride->pMissionOffer);
				}
			}
			FOR_EACH_END
		}
	}
}

// Finds the special dialog with the given name
SpecialDialogBlock * CEDialogFlowGetSpecialDialogByName(SA_PARAM_NN_VALID DialogFlowWindowInfo *pInfo, SA_PARAM_NN_STR const char *pchName, SA_PARAM_OP_STR const char *pchContactName)
{	
	devassert(pInfo && pchName && pchName[0]);

	if (pInfo && pchName && pchName[0])
	{
		char *estrStrippedName = NULL;
		estrStackCreate(&estrStrippedName);
		estrCopy2(&estrStrippedName, pchName);

		// Strip the mission name
		if (strchr(pchName, '/'))
		{
			estrRemoveUpToFirstOccurrence(&estrStrippedName, '/');
		}		

		if (pInfo->pContactDoc)
		{
			FOR_EACH_IN_EARRAY(pInfo->pContactDoc->pContact->specialDialog, SpecialDialogBlock, pSpecialDialog)
			{
				if (pSpecialDialog && stricmp(pSpecialDialog->name, estrStrippedName) == 0)
				{
					estrDestroy(&estrStrippedName);
					return pSpecialDialog;
				}
			}
			FOR_EACH_END
		}
		else if (pInfo->pMissionDoc)
		{
			FOR_EACH_IN_EARRAY_FORWARDS(pInfo->pMissionDoc->pMission->ppSpecialDialogOverrides, SpecialDialogOverride, pOverride)
			{
				if (pOverride && pOverride->pSpecialDialog && (pchContactName == NULL || stricmp(pchContactName, pOverride->pcContactName) == 0) &&
					stricmp(pOverride->pSpecialDialog->name, estrStrippedName) == 0)
				{
					estrDestroy(&estrStrippedName);
					return pOverride->pSpecialDialog;
				}
			}
			FOR_EACH_END
		}

		estrDestroy(&estrStrippedName);
	}

	return NULL;
}

// Finds the special action block with the given name
SpecialActionBlock * CEDialogFlowGetSpecialActionBlockByName(SA_PARAM_NN_VALID DialogFlowWindowInfo *pInfo, SA_PARAM_NN_STR const char *pchName, SA_PARAM_OP_STR const char *pchContactName)
{	
	devassert(pInfo && pchName && pchName[0]);

	if (pInfo && pchName && pchName[0])
	{
		char *estrStrippedName = NULL;
		estrStackCreate(&estrStrippedName);
		estrCopy2(&estrStrippedName, pchName);

		// Strip the mission name
		if (strchr(pchName, '/'))
		{
			estrRemoveUpToFirstOccurrence(&estrStrippedName, '/');
		}		

		if (pInfo->pContactDoc)
		{
			FOR_EACH_IN_EARRAY(pInfo->pContactDoc->pContact->specialActions, SpecialActionBlock, pSpecialActionBlock)
			{
				if (pSpecialActionBlock && stricmp(pSpecialActionBlock->name, estrStrippedName) == 0)
				{
					estrDestroy(&estrStrippedName);
					return pSpecialActionBlock;
				}
			}
			FOR_EACH_END
		}
		else if (pInfo->pMissionDoc)
		{
			FOR_EACH_IN_EARRAY_FORWARDS(pInfo->pMissionDoc->pMission->ppSpecialActionBlockOverrides, ActionBlockOverride, pOverride)
			{
				if (pOverride && pOverride->pSpecialActionBlock && (pchContactName == NULL || stricmp(pchContactName, pOverride->pcContactName) == 0) &&
					stricmp(pOverride->pSpecialActionBlock->name, estrStrippedName) == 0)
				{
					estrDestroy(&estrStrippedName);
					return pOverride->pSpecialActionBlock;
				}
			}
			FOR_EACH_END
		}

		estrDestroy(&estrStrippedName);
	}

	return NULL;
}

// Adds all the special dialogs based on the doc set to the given array
static void CEDialogFlowGetSpecialDialogs(SA_PARAM_NN_VALID DialogFlowWindowInfo *pInfo, 
										 SA_PARAM_NN_VALID SpecialDialogBlock ***peaSpecialDialogs)
{
	devassert(pInfo && peaSpecialDialogs);
	if (pInfo && peaSpecialDialogs)
	{
		if (pInfo->pContactDoc)
		{
			eaCopy(peaSpecialDialogs, &pInfo->pContactDoc->pContact->specialDialog);
		}
		else if (pInfo->pMissionDoc && pInfo->pchContactName && pInfo->pchContactName[0])
		{
			FOR_EACH_IN_EARRAY_FORWARDS(pInfo->pMissionDoc->pMission->ppSpecialDialogOverrides, SpecialDialogOverride, pOverride)
			{
				if (pOverride && pOverride->pSpecialDialog && stricmp(pInfo->pchContactName, pOverride->pcContactName) == 0)
				{
					eaPush(peaSpecialDialogs, pOverride->pSpecialDialog);
				}
			}
			FOR_EACH_END
		}
	}
}

// Adds all the special action blocks based on the doc set to the given array
static void CEDialogFlowGetSpecialActionBlocks(SA_PARAM_NN_VALID DialogFlowWindowInfo *pInfo, 
	SA_PARAM_NN_VALID SpecialActionBlock ***peaSpecialActionBlocks)
{
	devassert(pInfo && peaSpecialActionBlocks);
	if (pInfo && peaSpecialActionBlocks) {
		if (pInfo->pContactDoc) {
			eaCopy(peaSpecialActionBlocks, &pInfo->pContactDoc->pContact->specialActions);
		} else if (pInfo->pMissionDoc && pInfo->pchContactName && pInfo->pchContactName[0]) {
			//FOR_EACH_IN_EARRAY_FORWARDS(pInfo->pMissionDoc->pMission->ppSpecialDialogOverrides, SpecialDialogOverride, pOverride)
			//{
			//	if (pOverride && pOverride->pSpecialDialog && stricmp(pInfo->pchContactName, pOverride->pcContactName) == 0)
			//	{
			//		eaPush(peaSpecialDialogs, pOverride->pSpecialDialog);
			//	}
			//}
			//FOR_EACH_END
		}
	}
}

// Refreshes the whole dialog flow window to reflect the changes
void CERefreshDialogFlowWindow(SA_PARAM_NN_VALID DialogFlowWindowInfo *pInfo)
{
	ContactDialogUINode *pContactDialog = NULL;
	ContactMissionOffer **eaMissionOffers = NULL;
	SpecialDialogBlock **eaSpecialDialogs = NULL;
	SpecialActionBlock **eaSpecialActionBlocks = NULL;

	if (pInfo->pDialogFlowWin == NULL)
		return;

	// Set the title of the window
	ui_WindowSetTitle(pInfo->pDialogFlowWin, "Dialog Flow");

	// Destroy all dialog nodes
	eaDestroyEx(&pInfo->eaDialogNodes, CEDestroyDialogNode);

	// Clear all selected nodes
	eaClear(&pInfo->eaSelectedDialogNodes);
	CEClearDialogNodeSelection(pInfo);

	// Add the label for the contact name
	if (pInfo->pMissionDoc)
	{
		if (pInfo->pContactNameLabel == NULL)
		{
			pInfo->pContactNameLabel = ui_LabelCreate("Contact name:", DIALOG_NODE_AUTO_ARRANGE_BEGIN_XPOS, 0.f);
			ui_ScrollAreaAddChild(pInfo->pDialogFlowScrollArea, pInfo->pContactNameLabel);
			ui_WidgetSetDimensions(UI_WIDGET(pInfo->pContactNameLabel), 90.f, pInfo->pContactNameLabel->widget.height);
		}

		if (pInfo->pContactNameValueLabel == NULL)
		{
			pInfo->pContactNameValueLabel = ui_LabelCreate("You must use the 'Show Dialog Flow' button in the contact override window to see the dialog flow.", DIALOG_NODE_AUTO_ARRANGE_BEGIN_XPOS + 90.f, 0.f);
			ui_ScrollAreaAddChild(pInfo->pDialogFlowScrollArea, pInfo->pContactNameValueLabel);
			ui_WidgetSetDimensions(UI_WIDGET(pInfo->pContactNameValueLabel), 350.f, pInfo->pContactNameLabel->widget.height);
		}

		if (pInfo->pchContactName && pInfo->pchContactName[0])
		{
			ui_LabelSetText(pInfo->pContactNameValueLabel, pInfo->pchContactName);
		}
		else
		{
			// Contact name must be specified in the mission editor
			ui_LabelSetText(pInfo->pContactNameValueLabel, "You must use the 'Show Dialog Flow' button in the contact override window to see the dialog flow.");
			return;
		}
	}

	// Create a root node no matter what
	pContactDialog = CECreateDialogNode(pInfo, NULL, NULL, 0.f, 0.f);

	// Add to the window
	ui_ScrollAreaAddChild(pInfo->pDialogFlowScrollArea, pContactDialog->pGraphNode);

	// Add to the list of dialog nodes
	eaPush(&pInfo->eaDialogNodes, pContactDialog);

	// Get all mission offers
	CEDialogFlowGetMissionOffers(pInfo, &eaMissionOffers);

	// Add all mission offers
	FOR_EACH_IN_EARRAY_FORWARDS(eaMissionOffers, ContactMissionOffer, pMissionOffer)
	{
		if (pMissionOffer)
		{
			// Create a dialog node for this mission
			pContactDialog = CECreateDialogNode(pInfo, NULL, pMissionOffer, 0.f, 0.f);
			
			// Add to the window
			ui_ScrollAreaAddChild(pInfo->pDialogFlowScrollArea, pContactDialog->pGraphNode);

			// Add to the list of dialog nodes
			eaPush(&pInfo->eaDialogNodes, pContactDialog);
		}
	}
	FOR_EACH_END

	eaDestroy(&eaMissionOffers);

	// Get all special dialogs
	CEDialogFlowGetSpecialDialogs(pInfo, &eaSpecialDialogs);

	// Now add all special dialog nodes
	FOR_EACH_IN_EARRAY_FORWARDS(eaSpecialDialogs, SpecialDialogBlock, pSpecialDialog)
	{
		if (pSpecialDialog)
		{
			// Create a dialog node
			pContactDialog = CECreateDialogNode(pInfo, pSpecialDialog->name, NULL, 0.f, 0.f);

			// Add to the window
			ui_ScrollAreaAddChild(pInfo->pDialogFlowScrollArea, pContactDialog->pGraphNode);

			// Add to the list of dialog nodes
			eaPush(&pInfo->eaDialogNodes, pContactDialog);
		}
	}
	FOR_EACH_END

	//Get all the special action blocks
	//TODO: When we want to add the functionality for action blocks in the dialog flow, uncomment the following line
	//CEDialogFlowGetSpecialActionBlocks(pInfo, &eaSpecialActionBlocks);

	FOR_EACH_IN_EARRAY_FORWARDS(eaSpecialActionBlocks, SpecialActionBlock, pSpecialActionBlock)
	{
		if(pSpecialActionBlock) {
			//create the node
			pContactDialog = CECreateDialogNode(pInfo, pSpecialActionBlock->name, NULL, 0.0f, 0.0f);

			//Add it to the window
			ui_ScrollAreaAddChild(pInfo->pDialogFlowScrollArea, pContactDialog->pGraphNode);

			//Add to the list of nodes
			eaPush(&pInfo->eaDialogNodes, pContactDialog);
		}
	}
	FOR_EACH_END

	// Populate dialog actions for the dialog root node
	CEPopulateDialogNodeActions(NULL, CEGetRootDialogUINode(pInfo));

	// At this point all special dialog nodes are created.
	// Now is the time to populate actions and connect all dialog nodes
	FOR_EACH_IN_EARRAY_FORWARDS(eaSpecialDialogs, SpecialDialogBlock, pSpecialDialog)
	{
		if (pSpecialDialog)
		{
			CEPopulateDialogNodeActions(pSpecialDialog, CEDialogUINodeFromName(pInfo, pSpecialDialog->name));
		}		
	}
	FOR_EACH_END

	FOR_EACH_IN_EARRAY_FORWARDS(eaSpecialActionBlocks, SpecialActionBlock, pActionBlock)
	{
		if (pActionBlock)
		{
			CEPopulateActionBlockNodeActions(pActionBlock, CEDialogUINodeFromName(pInfo, pActionBlock->name));
		}		
	}
	FOR_EACH_END

	eaDestroy(&eaSpecialDialogs);
	eaDestroy(&eaSpecialActionBlocks);

	// Auto arrange
	CEAutoArrangeDialogNodeUI(pInfo);	

	// Restore all saved positions
	CERestoreDialogNodeWindowPositions(pInfo);

	// Update spline colors
	CEUpdateSplineColors(pInfo);
}

// Called for context menu action on the dialog flow window
static void CEShowDialogFlowWindowContextMenu(UIAnyWidget *pSourceWidget, SA_PARAM_NN_VALID DialogFlowWindowInfo *pInfo)
{
	devassert(pInfo);
	if (pInfo)
	{	
		if (pInfo->pContextMenu == NULL)
		{
			pInfo->pContextMenu = ui_MenuCreateWithItems("Node Menu",
				ui_MenuItemCreate("Auto arrange dialog nodes", UIMenuCallback, CEOnDialogFlowMenuActionAutoArrange, pInfo, NULL),
				NULL);
		}
		if (pInfo->pContextMenu)
		{
			ui_MenuPopupAtCursor(pInfo->pContextMenu);
		}
	}
}

// Handles the raised and focus events for the dialog flow window
static void CEOnDialogFlowWindowRaised(UIWindow *pWin, DialogFlowWindowInfo *pInfo)
{
	devassert(pInfo);
	if (pInfo)
	{
		CERefreshDialogFlowWindow(pInfo);
	}
}

// Sets the contact name for the dialog flow window
void CESetContactForDialogFlowWindow(SA_PARAM_NN_VALID DialogFlowWindowInfo *pInfo, SA_PARAM_NN_STR const char *pchContactName)
{
	if (pInfo && pchContactName && pchContactName[0])
	{
		// Set the contact name
		pInfo->pchContactName = allocAddString(pchContactName);

		// Refresh the window
		CERefreshDialogFlowWindow(pInfo);
	}
}

// Initializes the dialog flow window
static DialogFlowWindowInfo * CEInitDialogFlowWindow(void)
{
	UIWindow *pDialogFlowWin = ui_WindowCreate("Dialog Flow", 700, 50, 1000, 800);
	UIScrollArea * pScrollArea = ui_ScrollAreaCreate(0, 0, 1, 1, 0, 0, true, true);
	DialogFlowWindowInfo *pInfo = calloc(1, sizeof(DialogFlowWindowInfo));
	pScrollArea->scrollPadding = 50;

	// Set the main window
	pInfo->pDialogFlowWin = pDialogFlowWin;

	// Set the scroll area
	pInfo->pDialogFlowScrollArea = pScrollArea;

	// Set the callback for context menu
	ui_WidgetSetContextCallback(UI_WIDGET(pScrollArea), CEShowDialogFlowWindowContextMenu, pInfo);
	
	// Set the focus callback for the window
	ui_WindowSetRaisedCallback(pInfo->pDialogFlowWin, CEOnDialogFlowWindowRaised, pInfo);
	ui_WidgetSetFocusCallback(UI_WIDGET(pInfo->pDialogFlowWin), CEOnDialogFlowWindowRaised, pInfo);
	
	pScrollArea->autosize = true;
	ui_WidgetSetDimensionsEx((UIWidget *)pScrollArea, 1, 1, UIUnitPercentage, UIUnitPercentage);
	ui_WindowAddChild(pDialogFlowWin, pScrollArea);

	return pInfo;
}

// Initializes the dialog flow window for a contact document
DialogFlowWindowInfo * CEInitDialogFlowWindowWithContactDoc(SA_PARAM_NN_VALID ContactEditDoc *pDoc)
{
	DialogFlowWindowInfo *pInfo = CEInitDialogFlowWindow();

	// Set the doc
	pInfo->pContactDoc = pDoc;

	eaPush(&pDoc->emDoc.ui_windows, pInfo->pDialogFlowWin);

	return pInfo;
}

// Initializes the dialog flow window for a mission document
DialogFlowWindowInfo * CEInitDialogFlowWindowWithMissionDoc(SA_PARAM_NN_VALID MissionEditDoc *pDoc)
{
	DialogFlowWindowInfo *pInfo = CEInitDialogFlowWindow();

	// Set the doc
	pInfo->pMissionDoc = pDoc;

	eaPush(&pDoc->emDoc.ui_windows, pInfo->pDialogFlowWin);

	return pInfo;
}

static void CECloneSpecialCB(UIButton *pButton, CESpecialDialogGroup *pGroup)
{
	void *pWrapper = NULL;
	ContactDef *pDef = pGroup ? contact_DefFromName(pGroup->pchContactName) : NULL;

	if(!pGroup || !pDef || (!pGroup->pCommonCallbackParams->pSpecialDialogFromWrapperFunc && pGroup->pWrapperParseTable != parse_SpecialDialogBlock))
		return;

	// Make sure the resource is checked out of Gimme
	if (pGroup->pCommonCallbackParams->pDocIsEditableFunc && !pGroup->pCommonCallbackParams->pDocIsEditableFunc(pGroup->pCommonCallbackParams->pDocIsEditableData)) {
		return;
	}

	if(pGroup->index >= 0 && pGroup->index < eaSize(pGroup->peaSpecialDialogWrappers))
	{
		SpecialDialogBlock *pNewBlock = NULL;
		void *pNewWrapper = NULL;
		int iSuffix = 2;
		char pchNewName[1024];

		pWrapper = eaGet(pGroup->peaSpecialDialogWrappers, pGroup->index);

		pNewWrapper = StructCloneVoid(pGroup->pWrapperParseTable, pWrapper);

		if(!pNewWrapper)
			return;
	
		if(pGroup->pCommonCallbackParams->pSpecialDialogFromWrapperFunc)
		{
			pNewBlock = pGroup->pCommonCallbackParams->pSpecialDialogFromWrapperFunc(pNewWrapper);
		}
		else if(pGroup->pWrapperParseTable == parse_SpecialDialogBlock)
		{
			pNewBlock = (SpecialDialogBlock*) pNewWrapper;
		}

		if(!pNewBlock)
			return;

		sprintf(pchNewName, "%s_%d", pNewBlock->name, iSuffix);
		while(contact_SpecialDialogFromName(pDef, pchNewName))
		{
			iSuffix++;
			sprintf(pchNewName, "%s_%d", pNewBlock->name, iSuffix);
		}

		pNewBlock->name = allocAddString(pchNewName);

		eaPush(pGroup->peaSpecialDialogWrappers, pNewWrapper);
		if(pGroup->pCommonCallbackParams->pMessageFixupFunc)
			pGroup->pCommonCallbackParams->pMessageFixupFunc(pGroup->pCommonCallbackParams->pMessageFixupData);

		// Update the UI
		if(pGroup->pCommonCallbackParams->pDialogChangedFunc)
			pGroup->pCommonCallbackParams->pDialogChangedFunc(pGroup->pCommonCallbackParams->pDialogChangedData);
	}
}

static int CERefreshOverrideActionGroups(UILabel **pActionsLabel, CESpecialOverrideActionGroup ***peaGroups, UIExpander *pExpander, F32* y, F32 xPercentWidth, const SpecialDialogAction * const * const * const peaActions)
{
	S32 i;

	if (eaSize(peaActions) > 0)
	{
		*pActionsLabel = CERefreshLabel(*pActionsLabel, "Actions", "The list of actions defined for this special dialog", X_OFFSET_BASE, 0, *y, pExpander);
		*y += STANDARD_ROW_HEIGHT;
	}
	else
	{
		ui_WidgetQueueFreeAndNull(pActionsLabel);
	}

	for (i = 0; i < eaSize(peaActions); ++i) 
	{
		CESpecialOverrideActionGroup *pGroup;

		// Set the dialog block
		const SpecialDialogAction * const pAction = (*peaActions)[i];

		if (i >= eaSize(peaGroups))
		{
			eaPush(peaGroups, calloc(1, sizeof(CESpecialOverrideActionGroup)));
		}

		pGroup = (*peaGroups)[i];

		// Update text message field
		pGroup->pMessageLabel = CERefreshLabel(pGroup->pMessageLabel, "Display Text", "The text to display on the button for this action", X_OFFSET_BASE+15, 0, *y, pExpander);
		if (pGroup->pMessageTextEntry == NULL)
		{
			pGroup->pMessageTextEntry = ui_TextEntryCreate("", X_OFFSET_CONTROL + 40, *y);
			ui_SetActive(UI_WIDGET(pGroup->pMessageTextEntry), false);
			ui_ExpanderAddChild(pExpander, pGroup->pMessageTextEntry);
		}
		ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pMessageTextEntry), X_OFFSET_CONTROL + 40, *y, 0, 0, UITopLeft);
		ui_WidgetSetWidthEx(UI_WIDGET(pGroup->pMessageTextEntry), xPercentWidth, UIUnitPercentage);					
		ui_TextEntrySetText(pGroup->pMessageTextEntry, TranslateDisplayMessage(pAction->displayNameMesg));

		*y += STANDARD_ROW_HEIGHT;

		// Update the next dialog field
		pGroup->pNextDialogLabel = CERefreshLabel(pGroup->pNextDialogLabel, "Next Dialog to Show", "The dialog to go to next.  If blank, the contact exits after this action.", X_OFFSET_BASE+15, 0, *y, pExpander);
		if (pGroup->pNextDialogTextEntry == NULL)
		{
			pGroup->pNextDialogTextEntry = ui_TextEntryCreate("", X_OFFSET_CONTROL + 40, *y);
			ui_SetActive(UI_WIDGET(pGroup->pNextDialogTextEntry), false);
			ui_ExpanderAddChild(pExpander, pGroup->pNextDialogTextEntry);
		}
		ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pNextDialogTextEntry), X_OFFSET_CONTROL + 40, *y, 0, 0, UITopLeft);
		ui_WidgetSetWidthEx(UI_WIDGET(pGroup->pNextDialogTextEntry), xPercentWidth, UIUnitPercentage);					
		ui_TextEntrySetText(pGroup->pNextDialogTextEntry, pAction->dialogName);

		*y += STANDARD_ROW_HEIGHT;

	}
	return i;
}

static int CERefreshDialogOverrideBlocks(CEDialogOverrideGroup ***peaGroups, UIExpander *pExpander, F32* y, F32 xPercentWidth, char *pcLabel, char *pcTooltip, const DialogBlock * const * const * const peaDialogBlocks)
{	
	S32 i;

	for (i = 0; i < eaSize(peaDialogBlocks); ++i) 
	{
		CEDialogOverrideGroup *pGroup;

		// Set the dialog block
		const DialogBlock * const pBlock = (*peaDialogBlocks)[i];

		if (i >= eaSize(peaGroups))
		{
			eaPush(peaGroups, calloc(1, sizeof(CEDialogOverrideGroup)));
		}

		// Set the group
		pGroup = (*peaGroups)[i];

		// Update display string message field
		pGroup->pMessageLabel = CERefreshLabel(pGroup->pMessageLabel, pcLabel, pcTooltip, X_OFFSET_BASE, 0, *y, pExpander);
		if (pGroup->pMessageTextEntry == NULL)
		{
			pGroup->pMessageTextEntry = ui_TextEntryCreate("", X_OFFSET_CONTROL, *y);
			ui_SetActive(UI_WIDGET(pGroup->pMessageTextEntry), false);
			ui_ExpanderAddChild(pExpander, pGroup->pMessageTextEntry);
		}
		ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pMessageTextEntry), X_OFFSET_CONTROL, *y, 0, 0, UITopLeft);
		ui_WidgetSetWidthEx(UI_WIDGET(pGroup->pMessageTextEntry), xPercentWidth, UIUnitPercentage);					
		ui_TextEntrySetText(pGroup->pMessageTextEntry, TranslateDisplayMessage(pBlock->displayTextMesg));

		*y += STANDARD_ROW_HEIGHT;

	}
	return i;
}

static int CERefreshSpecialOverrideGroup(SA_PARAM_NN_VALID CESpecialDialogOverrideGroup *pGroup, SA_PARAM_NN_VALID UIExpander *pExpander, F32 y, F32 xPercentWidth, SA_PARAM_NN_VALID const SpecialDialogBlock * const pBlock)
{
	S32 n = 0;
	F32 fInitialYPos = y;

	if (pGroup->pPane == NULL)
	{
		pGroup->pPane = ui_PaneCreate(0.f, y, 0.f, 0.f, UIUnitPercentage, UIUnitPercentage, 0);
		pGroup->pPane->invisible = true;
		ui_WidgetSkin(UI_WIDGET(pGroup->pPane), s_ContactEditorLook.pSpecialDialogPaneSkin);
		ui_ExpanderAddChild(pExpander, pGroup->pPane);
	}
	else
	{
		ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pPane), 0.f, y, 0, 0, UITopLeft);
	}

	// Update internal name
	pGroup->pInternalNameLabel = CERefreshLabel(pGroup->pInternalNameLabel, "Name", "This is the actual name of the dialog used by the system. Use this name for event listening.", X_OFFSET_BASE, 0, y, pExpander);
	if (pGroup->pInternalNameTextEntry == NULL)
	{
		pGroup->pInternalNameTextEntry = ui_TextEntryCreate("", X_OFFSET_CONTROL, y);
		ui_SetActive(UI_WIDGET(pGroup->pInternalNameTextEntry), false);
		ui_ExpanderAddChild(pExpander, pGroup->pInternalNameTextEntry);
	}
	ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pInternalNameTextEntry), X_OFFSET_CONTROL, y, 0, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pGroup->pInternalNameTextEntry), xPercentWidth, UIUnitPercentage);		
	ui_TextEntrySetText(pGroup->pInternalNameTextEntry, pBlock->name);

	// Update open mission button
	if (pGroup->pOpenMissionButton == NULL)
	{
		pGroup->pOpenMissionButton = ui_ButtonCreate("Open in Editor", 0, 0, CEOpenMissionInEditorCB, (void *)pBlock->name);
		ui_WidgetSetWidth(UI_WIDGET(pGroup->pOpenMissionButton), 120);
		ui_ExpanderAddChild(pExpander, pGroup->pOpenMissionButton);
	}
	else
	{
		ui_ButtonSetCallback(pGroup->pOpenMissionButton, CEOpenMissionInEditorCB, (void *)pBlock->name);
	}
	ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pOpenMissionButton), 0, y, 0, 0, UITopRight);

	y += STANDARD_ROW_HEIGHT;

	// Update display string message field
	pGroup->pDisplayNameLabel = CERefreshLabel(pGroup->pDisplayNameLabel, "Display Name", "The display name for the title of the dialog", X_OFFSET_BASE, 0, y, pExpander);

	if (pGroup->pDisplayNameTextEntry == NULL)
	{
		pGroup->pDisplayNameTextEntry = ui_TextEntryCreate("", X_OFFSET_CONTROL, y);
		ui_SetActive(UI_WIDGET(pGroup->pDisplayNameTextEntry), false);
		ui_ExpanderAddChild(pExpander, pGroup->pDisplayNameTextEntry);
	}
	ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pDisplayNameTextEntry), X_OFFSET_CONTROL, y, 0, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pGroup->pDisplayNameTextEntry), xPercentWidth, UIUnitPercentage);				
	ui_TextEntrySetText(pGroup->pDisplayNameTextEntry, TranslateDisplayMessage(pBlock->displayNameMesg));

	y += STANDARD_ROW_HEIGHT;

	// Update the dialogs
	n = CERefreshDialogOverrideBlocks(&pGroup->eaDialogOverrideGroups, pExpander, &y, xPercentWidth,
		"Display Text", "The text for the body of the dialog",  &pBlock->dialogBlock);

	while (eaSize(&pGroup->eaDialogOverrideGroups) > n)
	{
		CEFreeDialogOverrideGroup(eaPop(&pGroup->eaDialogOverrideGroups));
	}

	// Update the actions
	n = CERefreshOverrideActionGroups(&pGroup->pActionsLabel, &pGroup->eaOverrideActionGroups, pExpander, &y, xPercentWidth, &pBlock->dialogActions);

	while (eaSize(&pGroup->eaOverrideActionGroups) > n)
	{
		CEFreeSpecialOverrideActionGroup(eaPop(&pGroup->eaOverrideActionGroups));
	}

	// Add line
	if (!pGroup->pSeparator) {
		pGroup->pSeparator = ui_SeparatorCreate(UIHorizontal);
		pGroup->pSeparator->widget.leftPad = X_OFFSET_BASE;
		ui_ExpanderAddChild(pExpander, pGroup->pSeparator);
	}
	ui_WidgetSetPosition(UI_WIDGET(pGroup->pSeparator), 0, y);

	y+=EXPANDER_HEIGHT;

	// Set the final dimensions of the pane
	ui_WidgetSetDimensionsEx(UI_WIDGET(pGroup->pPane), 1.f, y - fInitialYPos, UIUnitPercentage, UIUnitFixed);

	return y;
}

int CERefreshSpecialGroup(CESpecialDialogGroup *pGroup, UIExpander *pExpander, F32 y, F32 xPercentWidth, int index, void ***peaSpecialDialogWrappers, SpecialDialogBlock *pBlock, SpecialDialogBlock *pOldBlock, bool bSplitView, MissionDef *pMissionDef)
{
	int i;
	int n = 0;
	int numActions;
	F32 fInitialYPos = y;
	char *estrDialogInternalName = NULL;

	pGroup->index = index;
	pGroup->peaSpecialDialogWrappers = peaSpecialDialogWrappers;

	if (pGroup->pPane == NULL)
	{
		pGroup->pPane = ui_PaneCreate(0.f, y, 0.f, 0.f, UIUnitPercentage, UIUnitPercentage, 0);
		pGroup->pPane->invisible = true;
		ui_WidgetSkin(UI_WIDGET(pGroup->pPane), s_ContactEditorLook.pSpecialDialogPaneSkin);
		ui_ExpanderAddChild(pExpander, pGroup->pPane);
	}
	else
	{
		ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pPane), 0.f, y, 0, 0, UITopLeft);
	}

	// Update remove button
	if (!pGroup->pRemoveButton) {
		pGroup->pRemoveButton = ui_ButtonCreate("Delete Special Dialog", X_OFFSET_BASE+215, y, CERemoveSpecialCB, pGroup);
		ui_WidgetSetWidth(UI_WIDGET(pGroup->pRemoveButton), 150);
		ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pRemoveButton), X_OFFSET_BASE+215, y, 0, 0, UITopLeft);
		ui_ExpanderAddChild(pExpander, pGroup->pRemoveButton);
	} else {
		ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pRemoveButton), X_OFFSET_BASE+215, y, 0, 0, UITopLeft);
	}

	// Update clone button
	if (!pGroup->pCloneGroupButton) {
		pGroup->pCloneGroupButton = ui_ButtonCreate("Clone Special Dialog", X_OFFSET_BASE+60, y, CECloneSpecialCB, pGroup);
		ui_WidgetSetWidth(UI_WIDGET(pGroup->pCloneGroupButton), 150);
		ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pCloneGroupButton), X_OFFSET_BASE+60, y, 0, 0, UITopLeft);
		ui_ExpanderAddChild(pExpander, pGroup->pCloneGroupButton);
	} else {
		ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pCloneGroupButton), X_OFFSET_BASE+60, y, 0, 0, UITopLeft);
	}

	// Update up button
	if (index != 0) {
		if (!pGroup->pUpButton) {
			pGroup->pUpButton = ui_ButtonCreate("Up", X_OFFSET_BASE+30, y, CEMoveSpecialUpCB, pGroup);
			ui_WidgetSetWidth(UI_WIDGET(pGroup->pUpButton), 25);
			ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pUpButton), X_OFFSET_BASE+30, y, 0, 0, UITopLeft);
			ui_ExpanderAddChild(pExpander, pGroup->pUpButton);
		} else {
			ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pUpButton), X_OFFSET_BASE+30, y, 0, 0, UITopLeft);
		}
	} else if (pGroup->pUpButton) {
		ui_WidgetQueueFree((UIWidget*)pGroup->pUpButton);
		pGroup->pUpButton = NULL;
	}

	// Update down button
	if (index != eaSize(peaSpecialDialogWrappers)-1) {
		if (!pGroup->pDownButton) {
			pGroup->pDownButton = ui_ButtonCreate("Dn", X_OFFSET_BASE, y, CEMoveSpecialDownCB, pGroup);
			ui_WidgetSetWidth(UI_WIDGET(pGroup->pDownButton), 25);
			ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pDownButton), X_OFFSET_BASE, y, 0, 0, UITopLeft);
			ui_ExpanderAddChild(pExpander, pGroup->pDownButton);
		} else {
			ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pDownButton), X_OFFSET_BASE, y, 0, 0, UITopLeft);
		}
	} else if (pGroup->pDownButton) {
		ui_WidgetQueueFree((UIWidget*)pGroup->pDownButton);
		pGroup->pDownButton = NULL;
	}

	y+=STANDARD_ROW_HEIGHT;

	// Update logical name
	pGroup->pNameLabel = CERefreshLabel(pGroup->pNameLabel, "Name", "Each special dialog must have a unique name", X_OFFSET_BASE, 0, y, pExpander);
	if (!pGroup->pNameField) {
		pGroup->pNameField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOldBlock, pBlock, parse_SpecialDialogBlock, "Name");
		GEAddFieldToParent(pGroup->pNameField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, xPercentWidth, UIUnitPercentage, 0, pGroup->pCommonCallbackParams->pFieldChangeFunc, pGroup->pCommonCallbackParams->pFieldPreChangeFunc, pGroup->pCommonCallbackParams->pFieldChangeData);
	} else {
		ui_WidgetSetPosition(pGroup->pNameField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pNameField, pOldBlock, pBlock);
	}

	y += STANDARD_ROW_HEIGHT;

	// Update internal name
	if (pMissionDef)
	{
		pGroup->pInternalNameLabel = CERefreshLabel(pGroup->pInternalNameLabel, "Internal Name", "This is the actual name of the dialog used by the system. Use this name for event listening.", X_OFFSET_BASE, 0, y, pExpander);
		if (pGroup->pInternalNameTextEntry == NULL)
		{
			pGroup->pInternalNameTextEntry = ui_TextEntryCreate("", X_OFFSET_CONTROL, y);
			ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pInternalNameTextEntry), X_OFFSET_CONTROL, y, 0, 0, UITopLeft);
			ui_WidgetSetWidthEx(UI_WIDGET(pGroup->pInternalNameTextEntry), xPercentWidth, UIUnitPercentage);			
			ui_ExpanderAddChild(pExpander, pGroup->pInternalNameTextEntry);
			ui_SetActive(UI_WIDGET(pGroup->pInternalNameTextEntry), false);
		}
		estrCreate(&estrDialogInternalName);
		estrCopy2(&estrDialogInternalName, pBlock->name);
		if (estrLength(&estrDialogInternalName) > 0)
		{
			MDEMissionFixupDialogNameForSaving(pMissionDef, pGroup->pchContactName, &estrDialogInternalName);
		}		
		ui_TextEntrySetText(pGroup->pInternalNameTextEntry, estrDialogInternalName);
		estrDestroy(&estrDialogInternalName);

		y += STANDARD_ROW_HEIGHT;
	}
	else
	{
		ui_WidgetQueueFreeAndNull(&pGroup->pInternalNameLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pInternalNameTextEntry);
	}

	// Update sort order field
	pGroup->pSortOrderLabel = CERefreshLabel(pGroup->pSortOrderLabel, "Sort Order", "The number used to sort this special dialog across all valid special dialogs. The default value of 0 uses the visual order of special dialogs.", X_OFFSET_BASE, 0, y, pExpander);
	if (pGroup->pSortOrderField == NULL)
	{
		pGroup->pSortOrderField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOldBlock, pBlock, parse_SpecialDialogBlock, "SortOrder");
		GEAddFieldToParent(pGroup->pSortOrderField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, xPercentWidth, UIUnitPercentage, 0, pGroup->pCommonCallbackParams->pFieldChangeFunc, pGroup->pCommonCallbackParams->pFieldPreChangeFunc, pGroup->pCommonCallbackParams->pFieldChangeData);
	} 
	else 
	{
		ui_WidgetSetPosition(pGroup->pSortOrderField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pSortOrderField, pOldBlock, pBlock);
	}

	y+=STANDARD_ROW_HEIGHT;

	// Update expression field
	pGroup->pExpressionLabel = CERefreshLabel(pGroup->pExpressionLabel, "Condition", "Optional expression.  If true, this dialog can be shown.", X_OFFSET_BASE, 0, y, pExpander);
	if (!pGroup->pExpressionField) { 
		pGroup->pExpressionField = MEFieldCreateSimple(kMEFieldTypeEx_Expression, pOldBlock, pBlock, parse_SpecialDialogBlock, "Condition");
		GEAddFieldToParent(pGroup->pExpressionField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, xPercentWidth, UIUnitPercentage, 0, pGroup->pCommonCallbackParams->pFieldChangeFunc, pGroup->pCommonCallbackParams->pFieldPreChangeFunc, pGroup->pCommonCallbackParams->pFieldChangeData);
	} else {
		ui_WidgetSetPosition(pGroup->pExpressionField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pExpressionField, pOldBlock, pBlock);
	}

	y+=STANDARD_ROW_HEIGHT;

	// Update display string message field
	pGroup->pDisplayNameLabel = CERefreshLabel(pGroup->pDisplayNameLabel, "Display Name", "The display name for the title of the dialog", X_OFFSET_BASE, 0, y, pExpander);
	if (!pGroup->pDisplayNameField) {
		pGroup->pDisplayNameField = MEFieldCreateSimple(kMEFieldType_Message, pOldBlock ? &pOldBlock->displayNameMesg : NULL, &pBlock->displayNameMesg, parse_DisplayMessage, "editorCopy");
		GEAddFieldToParent(pGroup->pDisplayNameField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, xPercentWidth, UIUnitPercentage, 0, pGroup->pCommonCallbackParams->pFieldChangeFunc, pGroup->pCommonCallbackParams->pFieldPreChangeFunc, pGroup->pCommonCallbackParams->pFieldChangeData);
	} else {
		ui_WidgetSetPosition(pGroup->pDisplayNameField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pDisplayNameField, pOldBlock ? &pOldBlock->displayNameMesg : NULL, &pBlock->displayNameMesg);
	}

	y += STANDARD_ROW_HEIGHT;

	// Update dialog formatter field
	pGroup->pDialogFormatterLabel = CERefreshLabel(pGroup->pDialogFormatterLabel, "Text Formatter", "Used by UI to format the display text", X_OFFSET_BASE, 0, y, pExpander);
	if (pGroup->pDialogFormatterField == NULL)
	{
		pGroup->pDialogFormatterField = MEFieldCreateSimpleDictionary(kMEFieldType_ValidatedTextEntry, pOldBlock, pBlock, parse_SpecialDialogBlock, "DisplayNameFormatter", g_hContactDialogFormatterDefDictionary, parse_ContactDialogFormatterDef, "Name");
		GEAddFieldToParent(pGroup->pDialogFormatterField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, xPercentWidth, UIUnitPercentage, 0, pGroup->pCommonCallbackParams->pFieldChangeFunc, pGroup->pCommonCallbackParams->pFieldPreChangeFunc, pGroup->pCommonCallbackParams->pFieldChangeData);
	} else {
		ui_WidgetSetPosition(pGroup->pDialogFormatterField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pDialogFormatterField, pOldBlock, pBlock);
	}	

	if(bSplitView)
	{
		pGroup->pSoundLabel = CERefreshLabel(pGroup->pSoundLabel, "Sound to Play / Phrase to Say", "The sound file to play (top field), or the voice over phrase to say (bottom field) in a voice based on the contact's costume", X_OFFSET_AUDIO, X_PERCENT_SPLIT, y, pExpander);
		pGroup->pAnimLabel = CERefreshLabel(pGroup->pAnimLabel, "Anim List to Play / Dialog Formatter", "Anim List (Top Dropdown): The anim list override to be used as default for this contact. Dialog Formatter (Bottom Dropdown): Used by UI to format dialog messages", X_OFFSET_ANIM, X_PERCENT_SPLIT+X_PERCENT_SECOND_SML, y, pExpander);
	}
	else
	{
		ui_WidgetQueueFreeAndNull(&pGroup->pSoundLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pAnimLabel);
	}

	y+=STANDARD_ROW_HEIGHT;

	//Update the dialogs
	n += CERefreshDialogBlocks(&pGroup->eaDialogGroups, pExpander, &y, xPercentWidth, n, "Display Text", "The text for the body of the dialog",  
		&pBlock->dialogBlock, pOldBlock ? &pOldBlock->dialogBlock : NULL, true, true, true, bSplitView,
		pGroup->pCommonCallbackParams->pDocIsEditableFunc, pGroup->pCommonCallbackParams->pDocIsEditableData,
		pGroup->pCommonCallbackParams->pDialogChangedFunc, pGroup->pCommonCallbackParams->pDialogChangedData,
		pGroup->pCommonCallbackParams->pFieldChangeFunc, pGroup->pCommonCallbackParams->pFieldPreChangeFunc, pGroup->pCommonCallbackParams->pFieldChangeData);

	while (eaSize(&pGroup->eaDialogGroups) > n){
		CEFreeDialogGroup(eaPop(&pGroup->eaDialogGroups));
	}

	// Costume Section
	pGroup->pCostumeTypeLabel = CERefreshLabel(pGroup->pCostumeTypeLabel, "Costume From", "Where the contact's costume should be pulled from", X_OFFSET_BASE, 0, y, pExpander);
	if(!pGroup->pCostumeTypeField) {
		pGroup->pCostumeTypeField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pOldBlock, pBlock, parse_SpecialDialogBlock, "CostumeType", ContactCostumeTypeEnum);
		GEAddFieldToParent(pGroup->pCostumeTypeField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, xPercentWidth, UIUnitPercentage, 0, pGroup->pCommonCallbackParams->pFieldChangeFunc, pGroup->pCommonCallbackParams->pFieldPreChangeFunc, pGroup->pCommonCallbackParams->pFieldChangeData);
	} else {
		ui_WidgetSetPosition(pGroup->pCostumeTypeField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pCostumeTypeField, pOldBlock, pBlock);
	}

	// No costume override
	if(pBlock->costumePrefs.eCostumeType == ContactCostumeType_Default)
	{
		ui_WidgetQueueFreeAndNull(&pGroup->pCostumeLabel);
	}

	// Specified Costume
	if(pBlock->costumePrefs.eCostumeType == ContactCostumeType_Specified)
	{
		y+=STANDARD_ROW_HEIGHT;
		pGroup->pCostumeLabel = CERefreshLabel(pGroup->pCostumeLabel, "Costume Override", "The costume to use in headshots instead of any the critter may have.", X_OFFSET_BASE+X_OFFSET_BASE, 0, y, pExpander);
		if(!pGroup->pCostumeField) {
			pGroup->pCostumeField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_ValidatedTextEntry, pOldBlock, pBlock, parse_SpecialDialogBlock, "CostumeOverride", "PlayerCostume", "ResourceName");
			GEAddFieldToParent(pGroup->pCostumeField, UI_WIDGET(pExpander), X_OFFSET_CONTROL+X_OFFSET_BASE, y, 0, xPercentWidth, UIUnitPercentage, 0, pGroup->pCommonCallbackParams->pFieldChangeFunc, pGroup->pCommonCallbackParams->pFieldPreChangeFunc, pGroup->pCommonCallbackParams->pFieldChangeData);
		} else {
			ui_WidgetSetPosition(pGroup->pCostumeField->pUIWidget, X_OFFSET_CONTROL+X_OFFSET_BASE, y);
			MEFieldSetAndRefreshFromData(pGroup->pCostumeField, pOldBlock, pBlock);
		}
	} else {
		MEFieldSafeDestroy(&pGroup->pCostumeField);
	}

	// PetContactList
	if(pBlock->costumePrefs.eCostumeType == ContactCostumeType_PetContactList)
	{
		y+=STANDARD_ROW_HEIGHT;
		pGroup->pCostumeLabel = CERefreshLabel(pGroup->pCostumeLabel, "Pet Contact List", "The pet contact to use for headshot.", X_OFFSET_BASE+X_OFFSET_BASE, 0, y, pExpander);
		if(!pGroup->pPetContactField) {
			pGroup->pPetContactField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_ValidatedTextEntry, pOldBlock, pBlock, parse_SpecialDialogBlock, "UsePetCostume", "PetContactList", "ResourceName");
			GEAddFieldToParent(pGroup->pPetContactField, UI_WIDGET(pExpander), X_OFFSET_CONTROL+X_OFFSET_BASE, y, 0, xPercentWidth,UIUnitPercentage, 0, pGroup->pCommonCallbackParams->pFieldChangeFunc, pGroup->pCommonCallbackParams->pFieldPreChangeFunc, pGroup->pCommonCallbackParams->pFieldChangeData);
		} else {
			ui_WidgetSetPosition(pGroup->pPetContactField->pUIWidget, X_OFFSET_CONTROL+X_OFFSET_BASE, y);
			MEFieldSetAndRefreshFromData(pGroup->pPetContactField, pOldBlock, pBlock);
		}
	} else {
		MEFieldSafeDestroy(&pGroup->pPetContactField);
	}

	// CritterGroup
	if(pBlock->costumePrefs.eCostumeType == ContactCostumeType_CritterGroup)
	{
		y+=STANDARD_ROW_HEIGHT;
		// Critter Group type
		pGroup->pCritterGroupTypeLabel = CERefreshLabel(pGroup->pCritterGroupTypeLabel, "Critter Group From", "Where the critter group should be gathered from.", X_OFFSET_BASE+X_OFFSET_BASE, 0, y, pExpander);
		if(!pGroup->pCritterGroupTypeField) {
			pGroup->pCritterGroupTypeField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pOldBlock, pBlock, parse_SpecialDialogBlock, "CostumeCritterGroupType", ContactMapVarOverrideTypeEnum);
			GEAddFieldToParent(pGroup->pCritterGroupTypeField, UI_WIDGET(pExpander), X_OFFSET_CONTROL+X_OFFSET_BASE, y, 0, xPercentWidth,UIUnitPercentage, 0, pGroup->pCommonCallbackParams->pFieldChangeFunc, pGroup->pCommonCallbackParams->pFieldPreChangeFunc, pGroup->pCommonCallbackParams->pFieldChangeData);
		} else {
			ui_WidgetSetPosition(pGroup->pCritterGroupTypeField->pUIWidget, X_OFFSET_CONTROL+X_OFFSET_BASE, y);
			MEFieldSetAndRefreshFromData(pGroup->pCritterGroupTypeField, pOldBlock, pBlock);
		}

		y += STANDARD_ROW_HEIGHT;

		// Specified Critter Group
		if(pBlock->costumePrefs.eCostumeCritterGroupType == ContactMapVarOverrideType_Specified) {
			pGroup->pCostumeLabel = CERefreshLabel(pGroup->pCostumeLabel, "Critter Group", "The specified Critter Group which the contact's costume will be generated from.", X_OFFSET_BASE+X_OFFSET_BASE, 0, y, pExpander);
			if(!pGroup->pCritterGroupField) {
				pGroup->pCritterGroupField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_ValidatedTextEntry, pOldBlock, pBlock, parse_SpecialDialogBlock, "CostumeCritterGroup", "CritterGroup", "resourceName");
				GEAddFieldToParent(pGroup->pCritterGroupField, UI_WIDGET(pExpander), X_OFFSET_CONTROL+X_OFFSET_BASE, y, 0, xPercentWidth,UIUnitPercentage, 0, pGroup->pCommonCallbackParams->pFieldChangeFunc, pGroup->pCommonCallbackParams->pFieldPreChangeFunc, pGroup->pCommonCallbackParams->pFieldChangeData);
			} else {
				ui_WidgetSetPosition(pGroup->pCritterGroupField->pUIWidget, X_OFFSET_CONTROL+X_OFFSET_BASE, y);
				MEFieldSetAndRefreshFromData(pGroup->pCritterGroupField, pOldBlock, pBlock);
			}
		} else {
			MEFieldSafeDestroy(&pGroup->pCritterGroupField);
		}

		// Critter Group from Map Var
		if(pBlock->costumePrefs.eCostumeCritterGroupType == ContactMapVarOverrideType_MapVar) {
			pGroup->pCostumeLabel = CERefreshLabel(pGroup->pCostumeLabel, "Map Variable", "The map variable where the Critter Group should be pulled from.", X_OFFSET_BASE+X_OFFSET_BASE, 0, y, pExpander);
			if(!pGroup->pCritterMapVarField) {
				pGroup->pCritterMapVarField = MEFieldCreateSimpleDataProvided(kMEFieldType_TextEntry, pOldBlock, pBlock, parse_SpecialDialogBlock, "CostumeMapVar", NULL, pGroup->peaVarNames, NULL);
				GEAddFieldToParent(pGroup->pCritterMapVarField, UI_WIDGET(pExpander), X_OFFSET_CONTROL+X_OFFSET_BASE, y, 0, xPercentWidth,UIUnitPercentage, 0, pGroup->pCommonCallbackParams->pFieldChangeFunc, pGroup->pCommonCallbackParams->pFieldPreChangeFunc, pGroup->pCommonCallbackParams->pFieldChangeData);
			} else {
				ui_WidgetSetPosition(pGroup->pCritterMapVarField->pUIWidget, X_OFFSET_CONTROL+X_OFFSET_BASE, y);
				MEFieldSetAndRefreshFromData(pGroup->pCritterMapVarField, pOldBlock, pBlock);
			}
		} else {
			MEFieldSafeDestroy(&pGroup->pCritterMapVarField);
		}

		y+=STANDARD_ROW_HEIGHT;

		// Critter Group Identifier
		pGroup->pCritterGroupIdentifierLabel = CERefreshLabel(pGroup->pCritterGroupIdentifierLabel, "Name", "A name for this costume.  To use this same costume again, use the same critter group and same name.", X_OFFSET_BASE+X_OFFSET_BASE, 0, y, pExpander); 
		if (!pGroup->pCritterGroupIdentifierField) {
			pGroup->pCritterGroupIdentifierField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOldBlock, pBlock, parse_SpecialDialogBlock, "CostumeIdentifier");
			GEAddFieldToParent(pGroup->pCritterGroupIdentifierField, UI_WIDGET(pExpander), X_OFFSET_CONTROL+X_OFFSET_BASE, y, 0, xPercentWidth, UIUnitPercentage, 0, pGroup->pCommonCallbackParams->pFieldChangeFunc, pGroup->pCommonCallbackParams->pFieldPreChangeFunc, pGroup->pCommonCallbackParams->pFieldChangeData);
		} else {
			ui_WidgetSetPositionEx(pGroup->pCritterGroupIdentifierField->pUIWidget, X_OFFSET_CONTROL+X_OFFSET_BASE, y, 0, 0, UITopLeft);
			MEFieldSetAndRefreshFromData(pGroup->pCritterGroupIdentifierField, pOldBlock, pBlock);
		}


	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pCritterGroupTypeLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pCritterGroupIdentifierLabel);
		MEFieldSafeDestroy(&pGroup->pCritterMapVarField);
		MEFieldSafeDestroy(&pGroup->pCritterGroupField);
		MEFieldSafeDestroy(&pGroup->pCritterGroupTypeField);
		MEFieldSafeDestroy(&pGroup->pCritterGroupIdentifierField);
	}

	y += STANDARD_ROW_HEIGHT;

	pGroup->pHeadshotLabel = CERefreshLabel(pGroup->pHeadshotLabel, "Headshot Style", "The headshot style to use for this contact.", X_OFFSET_BASE, 0, y, pExpander); 
	if (!pGroup->pHeadshotStyleField) {
		pGroup->pHeadshotStyleField = MEFieldCreateSimpleDictionary(kMEFieldType_ValidatedTextEntry, pOldBlock, pBlock, parse_SpecialDialogBlock, "HeadshotStyleOverride", "HeadshotStyleDef", parse_HeadshotStyleDef, "Name");
		GEAddFieldToParent(pGroup->pHeadshotStyleField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, xPercentWidth, UIUnitPercentage, 0, pGroup->pCommonCallbackParams->pFieldChangeFunc, pGroup->pCommonCallbackParams->pFieldPreChangeFunc, pGroup->pCommonCallbackParams->pFieldChangeData);
	} else {
		ui_WidgetSetPosition(pGroup->pHeadshotStyleField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pHeadshotStyleField, pOldBlock, pBlock);
	}

	y += STANDARD_ROW_HEIGHT;

	pGroup->pCutSceneLabel = CERefreshLabel(pGroup->pCutSceneLabel, "Cut-scene", "The cut-scene played for this contact.", X_OFFSET_BASE, 0, y, pExpander); 
	if (!pGroup->pCutSceneField) {
		pGroup->pCutSceneField = MEFieldCreateSimpleDictionary(kMEFieldType_ValidatedTextEntry, pOldBlock, pBlock, parse_SpecialDialogBlock, "CutSceneDef", "CutScene", parse_CutsceneDef, "Name");
		GEAddFieldToParent(pGroup->pCutSceneField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, xPercentWidth, UIUnitPercentage, 0, pGroup->pCommonCallbackParams->pFieldChangeFunc, pGroup->pCommonCallbackParams->pFieldPreChangeFunc, pGroup->pCommonCallbackParams->pFieldChangeData);
	} else {
		ui_WidgetSetPosition(pGroup->pCutSceneField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pCutSceneField, pOldBlock, pBlock);
	}

	y += STANDARD_ROW_HEIGHT;

	pGroup->pSourceTypeLabel = CERefreshLabel(pGroup->pSourceTypeLabel, "Cam Src Type", "The camera source type", X_OFFSET_BASE, 0, y, pExpander);
	if (!pGroup->pSourceTypeField) {
		pGroup->pSourceTypeField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pOldBlock, pBlock, parse_SpecialDialogBlock, "SourceType", ContactSourceTypeEnum);
		GEAddFieldToParent(pGroup->pSourceTypeField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, xPercentWidth, UIUnitPercentage, 0, pGroup->pCommonCallbackParams->pFieldChangeFunc, pGroup->pCommonCallbackParams->pFieldPreChangeFunc, pGroup->pCommonCallbackParams->pFieldChangeData);
	} else {
		ui_WidgetSetPosition(pGroup->pSourceTypeField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pSourceTypeField, pOldBlock, pBlock);
	}

	y += STANDARD_ROW_HEIGHT;

	if (pBlock->eSourceType != ContactSourceType_None)
	{
		const char *pchSourceTypeLabelText;

		switch (pBlock->eSourceType)
		{
			xcase ContactSourceType_Clicky:
				pchSourceTypeLabelText = "Cam Src Clicky";
			xcase ContactSourceType_NamedPoint:
				pchSourceTypeLabelText = "Cam Src Nmd Pnt";
			xcase ContactSourceType_Encounter:
				pchSourceTypeLabelText = "Cam Src Enc";
			xdefault:
				pchSourceTypeLabelText = "Cam Src Unknown";
				Alertf("Contact editor encountered an unsupported contact source type and needs to be updated. Contact the contact editor programmers.");
		}

		pGroup->pSourceNameLabel = CERefreshLabel(pGroup->pSourceNameLabel, pchSourceTypeLabelText, "The camera source name", X_OFFSET_BASE, 0, y, pExpander);
		if (!pGroup->pSourceNameField) {
			pGroup->pSourceNameField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOldBlock, pBlock, parse_SpecialDialogBlock, "SourceName");
			GEAddFieldToParent(pGroup->pSourceNameField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, xPercentWidth, UIUnitPercentage, 0, pGroup->pCommonCallbackParams->pFieldChangeFunc, pGroup->pCommonCallbackParams->pFieldPreChangeFunc, pGroup->pCommonCallbackParams->pFieldChangeData);
		} else {
			ui_WidgetSetPosition(pGroup->pSourceNameField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pSourceNameField, pOldBlock, pBlock);
		}

		y += STANDARD_ROW_HEIGHT;
	}
	else
	{
		if (pGroup->pSourceNameLabel)
			ui_WidgetQueueFreeAndNull(&pGroup->pSourceNameLabel);
		if (pGroup->pSourceNameField)
			MEFieldSafeDestroy(&pGroup->pSourceNameField);
	}

	if (pBlock->eSourceType == ContactSourceType_Encounter)
	{
		pGroup->pSourceSecondaryNameLabel = CERefreshLabel(pGroup->pSourceSecondaryNameLabel, "Cam Src Actor", "The actor name", X_OFFSET_BASE, 0, y, pExpander);
		if (!pGroup->pSourceSecondaryNameField) {
			pGroup->pSourceSecondaryNameField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOldBlock, pBlock, parse_SpecialDialogBlock, "SourceSecondaryName");
			GEAddFieldToParent(pGroup->pSourceSecondaryNameField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, xPercentWidth, UIUnitPercentage, 0, pGroup->pCommonCallbackParams->pFieldChangeFunc, pGroup->pCommonCallbackParams->pFieldPreChangeFunc, pGroup->pCommonCallbackParams->pFieldChangeData);
		} else {
			ui_WidgetSetPosition(pGroup->pSourceSecondaryNameField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pSourceSecondaryNameField, pOldBlock, pBlock);
		}

		y += STANDARD_ROW_HEIGHT;
	}
	else
	{
		if (pGroup->pSourceSecondaryNameLabel)
			ui_WidgetQueueFreeAndNull(&pGroup->pSourceSecondaryNameLabel);
		if (pGroup->pSourceSecondaryNameField)
			MEFieldSafeDestroy(&pGroup->pSourceSecondaryNameField);
	}


	// Update indicator field
	pGroup->pIndicatorLabel = CERefreshLabel(pGroup->pIndicatorLabel, "Indicator", "If set, this option will display an indicator on the Contact.", X_OFFSET_BASE, 0, y, pExpander);
	if (!pGroup->pIndicatorField) {
		pGroup->pIndicatorField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pOldBlock, pBlock, parse_SpecialDialogBlock, "Indicator", SpecialDialogIndicatorEnum);
		GEAddFieldToParent(pGroup->pIndicatorField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, xPercentWidth, UIUnitPercentage, 0, pGroup->pCommonCallbackParams->pFieldChangeFunc, pGroup->pCommonCallbackParams->pFieldPreChangeFunc, pGroup->pCommonCallbackParams->pFieldChangeData);
	} else {
		ui_WidgetSetPosition(pGroup->pIndicatorField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pIndicatorField, pOldBlock, pBlock);
	}

	y+=STANDARD_ROW_HEIGHT;

	pGroup->pFlagsLabel = CERefreshLabel(pGroup->pFlagsLabel, "Flags", "Add extra behavior to this special dialog block.  ForceOnTeam: Forces this special dialog onto all teammates; if the teammate has a dialog already open, this one will queue.  Synchronized: Not yet implemented.", X_OFFSET_BASE, 0, y, pExpander);
	if (!pGroup->pFlagsField) {
		pGroup->pFlagsField = MEFieldCreateSimpleEnum(kMEFieldType_FlagCombo, pOldBlock, pBlock, parse_SpecialDialogBlock, "Flags", SpecialDialogFlagsEnum);
		GEAddFieldToParent(pGroup->pFlagsField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, xPercentWidth, UIUnitPercentage, 0, pGroup->pCommonCallbackParams->pFieldChangeFunc, pGroup->pCommonCallbackParams->pFieldPreChangeFunc, pGroup->pCommonCallbackParams->pFieldChangeData);
	} else {
		ui_WidgetSetPosition(pGroup->pFlagsField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pFlagsField, pOldBlock, pBlock);
	}

	y+=STANDARD_ROW_HEIGHT;

	// Update delay if in combat field
	MEExpanderRefreshLabel(&pGroup->pDelayIfInCombatLabel, "Delay In Combat", "If true, delay showing this dialog while the player is in combat.", X_OFFSET_BASE, 0, y, UI_WIDGET(pExpander));
	MEExpanderRefreshSimpleField(&pGroup->pDelayIfInCombatField, pOldBlock, pBlock, parse_SpecialDialogBlock, "DelayIfInCombat", kMEFieldType_BooleanCombo,
								 UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, xPercentWidth, UIUnitPercentage, 0,
								 pGroup->pCommonCallbackParams->pFieldChangeFunc, pGroup->pCommonCallbackParams->pFieldPreChangeFunc, pGroup->pCommonCallbackParams->pFieldChangeData);
	y+=STANDARD_ROW_HEIGHT;

	//Special dialog actions to append
	MEExpanderRefreshLabel(&pGroup->pSpecialActionBlockLabel, "Action Block", "This refers to a special action block you would like to append.", X_OFFSET_BASE, 0, y, UI_WIDGET(pExpander));
	if (!pGroup->pAppendField)
	{
		pGroup->pAppendField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOldBlock, pBlock, parse_SpecialDialogBlock, "SpecialDialogAppendName");
		GEAddFieldToParent(pGroup->pAppendField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, X_PERCENT_SMALL, UIUnitPercentage, 0, pGroup->pCommonCallbackParams->pFieldChangeFunc, pGroup->pCommonCallbackParams->pFieldPreChangeFunc, pGroup->pCommonCallbackParams->pFieldChangeData);
	} else {
		ui_WidgetSetPosition(pGroup->pAppendField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pAppendField, pOldBlock, pBlock);
	}

	y+=STANDARD_ROW_HEIGHT;

	// Update actions
	numActions = eaSize(&pBlock->dialogActions);
	for(i=0; i<numActions; ++i) {
		SpecialDialogAction *pAction = pBlock->dialogActions[i];
		SpecialDialogAction *pOldAction;

		if (pOldBlock && (eaSize(&pOldBlock->dialogActions) > i)) {
			pOldAction = pOldBlock->dialogActions[i];
		} else {
			pOldAction = NULL;
		}
		while (i >= eaSize(&pGroup->eaActionGroups)) {
			CESpecialActionGroup *pActionGroup = CECreateSpecialActionGroup(pGroup->pCommonCallbackParams);
			eaPush(&pGroup->eaActionGroups, pActionGroup);
		}
		y = CERefreshSpecialActionGroup(pGroup->eaActionGroups[i], pExpander, y, i, &pBlock->dialogActions, pAction, pOldAction);
	}

	// Free unused dialog groups
	for(i = eaSize(&pGroup->eaActionGroups)-1; i >= numActions; --i) {
		assert(pGroup->eaActionGroups);
		CEFreeSpecialActionGroup(pGroup->eaActionGroups[i]);
		eaRemove(&pGroup->eaActionGroups, i);
	}

	// Put in Add Action button
	if (!pGroup->pAddActionButton) {
		pGroup->pAddActionButton = ui_ButtonCreate("Add Action", 5, y, CEAddSpecialActionCB, pGroup);
		ui_WidgetSetWidth(UI_WIDGET(pGroup->pAddActionButton), 100);
		ui_WidgetSetPosition(UI_WIDGET(pGroup->pAddActionButton), X_OFFSET_BASE, y);
		ui_ExpanderAddChild(pExpander, pGroup->pAddActionButton);
	} else {
		ui_WidgetSetPosition(UI_WIDGET(pGroup->pAddActionButton), X_OFFSET_BASE, y);
	}

	y+=STANDARD_ROW_HEIGHT;

	// Add line
	if (!pGroup->pSeparator) {
		pGroup->pSeparator = ui_SeparatorCreate(UIHorizontal);
		pGroup->pSeparator->widget.leftPad = X_OFFSET_BASE;
		ui_ExpanderAddChild(pExpander, pGroup->pSeparator);
	}
	ui_WidgetSetPosition(UI_WIDGET(pGroup->pSeparator), 0, y);

	y+=EXPANDER_HEIGHT;

	// Set the final dimensions of the pane
	ui_WidgetSetDimensionsEx(UI_WIDGET(pGroup->pPane), 1.f, y - fInitialYPos, UIUnitPercentage, UIUnitFixed);

	return y;
}

static void CERefreshSpecialExpander(ContactEditDoc *pDoc)
{
	S32 iNumOverrides = 0;

	SpecialDialogBlock *pBlock, *pOldBlock;
	int i;
	int totalHeight = 0;
	int numDialogs = eaSize(&pDoc->pContact->specialDialog);

	// Refresh the group
	for(i=0; i<numDialogs; ++i) {
		pBlock = pDoc->pContact->specialDialog[i];
		if (pDoc->pOrigContact && (eaSize(&pDoc->pOrigContact->specialDialog) > i)) {
			pOldBlock = pDoc->pOrigContact->specialDialog[i];
		} else {
			pOldBlock = NULL;
		}
		while (i >= eaSize(&pDoc->eaSpecialGroups)) {
			CESpecialDialogGroup *pGroup = CECreateSpecialDialogGroup(	pDoc->pContact->name, &pDoc->eaVarNames,
																		CEIsDocEditable, pDoc,
																		CEUpdateUI, pDoc,
																		CEFixupMessagesWrapper, pDoc,
																		NULL, parse_SpecialDialogBlock,
																		CEFieldChangedCB, CEFieldPreChangeCB, pDoc);
			eaPush(&pDoc->eaSpecialGroups, pGroup);
		}
		totalHeight = CERefreshSpecialGroup(pDoc->eaSpecialGroups[i], pDoc->pSpecialExpander, totalHeight, X_PERCENT_SPLIT, i, &pDoc->pContact->specialDialog, pBlock, pOldBlock, true, NULL);
	}

	// Free unused dialog groups
	for(i = eaSize(&pDoc->eaSpecialGroups)-1; i >= numDialogs; --i) {
		assert(pDoc->eaSpecialGroups);
		CEFreeSpecialDialogGroup(pDoc->eaSpecialGroups[i]);
		eaRemove(&pDoc->eaSpecialGroups, i);
	}

	// Put in the Add button
	if (!pDoc->pSpecialAddButton) {
		pDoc->pSpecialAddButton = ui_ButtonCreate("Add Special Dialog", X_OFFSET_BASE, totalHeight, CEAddSpecialCB, pDoc);
		ui_WidgetSetWidth(UI_WIDGET(pDoc->pSpecialAddButton), 150);
		ui_ExpanderAddChild(pDoc->pSpecialExpander, pDoc->pSpecialAddButton);
	} else {
		ui_WidgetSetPosition(UI_WIDGET(pDoc->pSpecialAddButton), X_OFFSET_BASE, totalHeight);
	}

	totalHeight += STANDARD_ROW_HEIGHT;

	//This code handles the special action blocks
	{
		SpecialActionBlock *pActionBlock, *pOldActionBlock;
		int iNumActionBlocks = eaSize(&pDoc->pContact->specialActions);
		CECommonCallbackParams *pCommonCallbackParams = CECreateCommonCallbackParams(CEIsDocEditable, pDoc,
																						CEUpdateUI, pDoc,
																						CEFixupMessagesWrapper, pDoc,
																						NULL,
																						CEFieldChangedCB, CEFieldPreChangeCB, pDoc);

		for(i = 0; i < iNumActionBlocks; ++i) {
			pActionBlock = pDoc->pContact->specialActions[i];

			if (pDoc->pOrigContact && (eaSize(&pDoc->pOrigContact->specialActions) > i)) {
				pOldActionBlock = pDoc->pOrigContact->specialActions[i];
			} else {
				pOldActionBlock = NULL;
			}

			//Fill in groups for every action block from the def
			while (i >= eaSize(&pDoc->eaSpecialActionBlockGroups)) {
				CESpecialActionBlockGroup *pGroup = CECreateSpecialActionBlockGroup(pCommonCallbackParams);

				eaPush(&pDoc->eaSpecialActionBlockGroups, pGroup);

				//totalHeight = CERefreshSpecialActionBlockGroup(pGroup, pCommonCallbackParams, pDoc->pSpecialExpander, totalHeight, i, &pDoc->pContact->specialActions, pActionBlock, pOldActionBlock);
			}
			//totalHeight = CERefreshSpecialActionBlockGroup(pDoc->eaSpecialGroups[i], pDoc->pSpecialExpander, totalHeight, X_PERCENT_SPLIT, i, &pDoc->pContact->specialDialog, pBlock, pOldBlock, true, NULL);
			totalHeight = CERefreshSpecialActionBlockGroup(pDoc->eaSpecialActionBlockGroups[i], pCommonCallbackParams, pDoc->pSpecialExpander, totalHeight, i, &pDoc->pContact->specialActions, NULL, pActionBlock, pOldActionBlock);
		}

		for(i = eaSize(&pDoc->eaSpecialActionBlockGroups)-1; i >= iNumActionBlocks; --i) {
			assert(pDoc->eaSpecialActionBlockGroups);
			CEFreeSpecialActionBlockGroup(pDoc->eaSpecialActionBlockGroups[i]);
			eaRemove(&pDoc->eaSpecialActionBlockGroups, i);
		}
	}

	if (!pDoc->pActionBlockAddButton) {
		pDoc->pActionBlockAddButton = ui_ButtonCreate("Add Special Action Block", X_OFFSET_BASE, totalHeight, CEAddSpecialActionBlockCB, pDoc);
		ui_WidgetSetWidth(UI_WIDGET(pDoc->pActionBlockAddButton), 200);
		ui_ExpanderAddChild(pDoc->pSpecialExpander, pDoc->pActionBlockAddButton);
	} else {
		ui_WidgetSetPosition(UI_WIDGET(pDoc->pActionBlockAddButton), X_OFFSET_BASE, totalHeight);
	}
	totalHeight += STANDARD_ROW_HEIGHT;

	FOR_EACH_IN_CONST_EARRAY_FORWARDS(pDoc->pContact->eaSpecialDialogOverrides, SpecialDialogBlock, pSpecialDialogBlock)
	{
		if (iNumOverrides >= eaSize(&pDoc->eaSpecialOverrideGroups))
		{
			eaPush(&pDoc->eaSpecialOverrideGroups, calloc(1, sizeof(CESpecialDialogOverrideGroup)));
		}

		// Set the contact name
		pDoc->eaSpecialOverrideGroups[iNumOverrides]->pchContactName = pDoc->pContact->name;

		// Refresh the special dialog override group
		totalHeight = CERefreshSpecialOverrideGroup(pDoc->eaSpecialOverrideGroups[iNumOverrides], pDoc->pSpecialExpander, totalHeight, 0.75, pSpecialDialogBlock);

		++iNumOverrides;
	}
	FOR_EACH_END

	// Free unused override groups
	for (i = eaSize(&pDoc->eaSpecialOverrideGroups) - 1; i >= iNumOverrides; --i) 
	{
		CEFreeSpecialDialogOverrideGroup(pDoc->eaSpecialOverrideGroups[i]);
		eaRemove(&pDoc->eaSpecialOverrideGroups, i);
	}

	// Set the expander height
	ui_ExpanderSetHeight(pDoc->pSpecialExpander, totalHeight);
}


static void CERefreshLoreDialogGroup(CELoreDialogGroup *pGroup, UIExpander *pExpander, F32 y, int index, ContactLoreDialog ***peaLoreDialogs, ContactLoreDialog *pLoreDialog, ContactLoreDialog *pOldLoreDialog)
{
	pGroup->index = index;

	// Update "Remove" button
	if (!pGroup->pButton) {
		pGroup->pButton = ui_ButtonCreate("Remove", 5, y, CERemoveLoreDialogCB, pGroup);
		ui_WidgetSetWidth(UI_WIDGET(pGroup->pButton), 80);
		ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pButton), 10, y, X_PERCENT_SMALL, 0, UITopLeft);
		ui_ExpanderAddChild(pExpander, pGroup->pButton);
	} else {
		ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pButton), 10, y, X_PERCENT_SMALL, 0, UITopLeft);
	}

	// Update lore item field
	pGroup->pLoreItemLabel = CERefreshLabel(pGroup->pLoreItemLabel, "Lore Item", "The Lore ItemDef to display.", X_OFFSET_BASE, 0, y, pExpander);
	if (!pGroup->pLoreItemField) {
		pGroup->pLoreItemField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_ValidatedTextEntry, pOldLoreDialog, pLoreDialog, parse_ContactLoreDialog, "LoreItem", "ItemDef", "resourceName");
		CEAddFieldToParent(pGroup->pLoreItemField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, X_PERCENT_SMALL, UIUnitPercentage, 0, pGroup->pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pLoreItemField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pLoreItemField, pOldLoreDialog, pLoreDialog);
	}

	y += STANDARD_ROW_HEIGHT;

	// Update Option Text field
	pGroup->pOptionTextLabel = CERefreshLabel(pGroup->pOptionTextLabel, "Option Text", "The text to display on the Contact Dialog option.", X_OFFSET_BASE, 0, y, pExpander);
	if (!pGroup->pOptionTextField) {
		pGroup->pOptionTextField = MEFieldCreateSimple(kMEFieldType_Message, pOldLoreDialog?(&pOldLoreDialog->optionText):NULL, &pLoreDialog->optionText, parse_DisplayMessage, "editorCopy");
		CEAddFieldToParent(pGroup->pOptionTextField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, X_PERCENT_SMALL, UIUnitPercentage, 0, pGroup->pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pOptionTextField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pOptionTextField, pOldLoreDialog?(&pOldLoreDialog->optionText):NULL, &pLoreDialog->optionText);
	}

	y += STANDARD_ROW_HEIGHT;

	// Update Condition field
	pGroup->pConditionLabel = CERefreshLabel(pGroup->pConditionLabel, "Condition", "When to display this Lore dialog option.", X_OFFSET_BASE, 0, y, pExpander);
	if (!pGroup->pConditionField) {
		pGroup->pConditionField = MEFieldCreateSimple(kMEFieldTypeEx_Expression, pOldLoreDialog, pLoreDialog, parse_ContactLoreDialog, "Condition");
		CEAddFieldToParent(pGroup->pConditionField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, X_PERCENT_SMALL, UIUnitPercentage, 0, pGroup->pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pConditionField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pConditionField, pOldLoreDialog, pLoreDialog);
	}
}


static void CERefreshLoreDialogExpander(ContactEditDoc *pDoc)
{
	ContactLoreDialog *pLoreDialog, *pOldLoreDialog;
	int i;
	int numDialogs = eaSize(&pDoc->pContact->eaLoreDialogs);

	// Hide store expander if Single Dialog type
	if (pDoc->pContact->type == ContactType_SingleDialog) {
		if (pDoc->pLoreExpander->group) {
			ui_ExpanderGroupRemoveExpander(pDoc->pExpanderGroup, pDoc->pLoreExpander);
		}
		return;
	}
	if (!pDoc->pLoreExpander->group) {
		ui_ExpanderGroupInsertExpander(pDoc->pExpanderGroup, pDoc->pLoreExpander, 4);
	}

	// Refresh the store groups
	for(i=0; i<numDialogs; ++i) {
		pLoreDialog = pDoc->pContact->eaLoreDialogs[i];
		if (pDoc->pOrigContact && (eaSize(&pDoc->pOrigContact->eaLoreDialogs) > i)) {
			pOldLoreDialog = pDoc->pOrigContact->eaLoreDialogs[i];
		} else {
			pOldLoreDialog = NULL;
		}
		while (i >= eaSize(&pDoc->eaLoreDialogGroups)) {
			CELoreDialogGroup *pGroup = CECreateLoreDialogGroup(pDoc);
			eaPush(&pDoc->eaLoreDialogGroups, pGroup);
		}
		CERefreshLoreDialogGroup(pDoc->eaLoreDialogGroups[i], pDoc->pLoreExpander, i*STANDARD_ROW_HEIGHT*3, i, &pDoc->pContact->eaLoreDialogs, pLoreDialog, pOldLoreDialog);
	}

	// Free unused store groups
	for(i = eaSize(&pDoc->eaLoreDialogGroups)-1; i >= numDialogs; --i) {
		assert(pDoc->eaLoreDialogGroups);
		CEFreeLoreDialogGroup(pDoc->eaLoreDialogGroups[i]);
		eaRemove(&pDoc->eaLoreDialogGroups, i);
	}

	// Put in the Add button
	if (!pDoc->pLoreDialogAddButton) {
		pDoc->pLoreDialogAddButton = ui_ButtonCreate("Add Lore", X_OFFSET_BASE, (numDialogs*STANDARD_ROW_HEIGHT*3), CEAddLoreDialogCB, pDoc);
		ui_WidgetSetWidth(UI_WIDGET(pDoc->pLoreDialogAddButton), 150);
		ui_ExpanderAddChild(pDoc->pLoreExpander, pDoc->pLoreDialogAddButton);
	} else {
		ui_WidgetSetPosition(UI_WIDGET(pDoc->pLoreDialogAddButton), X_OFFSET_BASE, (numDialogs*STANDARD_ROW_HEIGHT*3));
	}

	// Set the expander height
	ui_ExpanderSetHeight(pDoc->pLoreExpander, STANDARD_ROW_HEIGHT + ((numDialogs) * STANDARD_ROW_HEIGHT * 3));
}

static void CERefreshStoreGroup(CEStoreGroup *pGroup, UIExpander *pExpander, F32 y, int index, StoreRef ***peaStores, StoreRef *pStore, StoreRef *pOldStore, F32 fIndent)
{
	pGroup->index = index;
	pGroup->peaStores = peaStores;

	// Update button
	if (!pGroup->pButton) {
		pGroup->pButton = ui_ButtonCreate("Remove", 5, y, CERemoveStoreCB, pGroup);
		ui_WidgetSetWidth(UI_WIDGET(pGroup->pButton), 80);
		ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pButton), 10, y, X_PERCENT_SMALL, 0, UITopLeft);
		ui_ExpanderAddChild(pExpander, pGroup->pButton);
	} else {
		ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pButton), 10, y, X_PERCENT_SMALL, 0, UITopLeft);
	}

	// Update store field
	pGroup->pStoreLabel = CERefreshLabel(pGroup->pStoreLabel, "Store", "The name of the store this contact has available.", X_OFFSET_BASE+fIndent, 0, y, pExpander);
	if (!pGroup->pStoreField) {
		pGroup->pStoreField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_ValidatedTextEntry, pOldStore, pStore, parse_StoreRef, "Ref", "Store", "resourceName");
		CEAddFieldToParent(pGroup->pStoreField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, X_PERCENT_SMALL, UIUnitPercentage, 0, pGroup->pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pStoreField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pStoreField, pOldStore, pStore);
	}
}

static F32 CERefreshStoreCollectionGroup(CEStoreCollectionGroup *pGroup, UIExpander *pExpander, F32 y, int index, StoreCollection ***peaStoreCollections, StoreCollection *pStoreCollection, StoreCollection *pOldStoreCollection)
{
	int i;
	int numStores = pStoreCollection ? eaSize(&pStoreCollection->eaStores) : 0;
	pGroup->index = index;

	if(!pStoreCollection)
		return y;

	// Update Option Text field
	pGroup->pOptionTextLabel = CERefreshLabel(pGroup->pOptionTextLabel, "Option Text", "The text to display on the Contact Dialog option.", X_OFFSET_BASE, 0, y, pExpander);
	if (!pGroup->pOptionTextField) {
		pGroup->pOptionTextField = MEFieldCreateSimple(kMEFieldType_Message, pOldStoreCollection?(&pOldStoreCollection->optionText):NULL, &pStoreCollection->optionText, parse_DisplayMessage, "editorCopy");
		CEAddFieldToParent(pGroup->pOptionTextField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, X_PERCENT_SMALL, UIUnitPercentage, 0, pGroup->pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pOptionTextField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pOptionTextField, pOldStoreCollection?(&pOldStoreCollection->optionText):NULL, &pStoreCollection->optionText);
	}

	y += STANDARD_ROW_HEIGHT;

	// Update Condition field
	pGroup->pConditionLabel = CERefreshLabel(pGroup->pConditionLabel, "Condition", "When to display this Store Collection dialog option.", X_OFFSET_BASE, 0, y, pExpander);
	if (!pGroup->pConditionField) {
		pGroup->pConditionField = MEFieldCreateSimple(kMEFieldTypeEx_Expression, pOldStoreCollection, pStoreCollection, parse_StoreCollection, "Condition");
		CEAddFieldToParent(pGroup->pConditionField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, X_PERCENT_SMALL, UIUnitPercentage, 0, pGroup->pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pConditionField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pConditionField, pOldStoreCollection, pStoreCollection);
	}

	y += STANDARD_ROW_HEIGHT;

	// Refresh the store groups
	for(i=0; i<numStores; ++i) {
		StoreRef* pStore = pStoreCollection->eaStores[i];
		StoreRef* pOldStore = NULL;
		if (pOldStoreCollection && (eaSize(&pOldStoreCollection->eaStores) > i)) {
			pOldStore = pOldStoreCollection->eaStores[i];
		} 

		while (i >= eaSize(&pGroup->eaStoreGroups)) {
			CEStoreGroup *pStoreGroup = CECreateStoreGroup(pGroup->pDoc, &pStoreCollection->eaStores);
			eaPush(&pGroup->eaStoreGroups, pStoreGroup);
		}
		CERefreshStoreGroup(pGroup->eaStoreGroups[i], pExpander, y, i, &pStoreCollection->eaStores, pStore, pOldStore, X_OFFSET_BASE);

		y += STANDARD_ROW_HEIGHT;
	}

	// Free unused store groups
	for(i = eaSize(&pGroup->eaStoreGroups)-1; i >= numStores; --i) {
		assert(pGroup->eaStoreGroups);
		CEFreeStoreGroup(pGroup->eaStoreGroups[i]);
		eaRemove(&pGroup->eaStoreGroups, i);
	}

	// Put in the Add button
	if (!pGroup->pAddStoreButton) {
		pGroup->pAddStoreButton = ui_ButtonCreate("Add Store", X_OFFSET_BASE, y, CEAddStoreToCollectionCB, pGroup);
		ui_WidgetSetWidth(UI_WIDGET(pGroup->pAddStoreButton), 150);
		ui_ExpanderAddChild(pExpander, pGroup->pAddStoreButton);
	} else {
		ui_WidgetSetPosition(UI_WIDGET(pGroup->pAddStoreButton), X_OFFSET_BASE, y);
	}

	// Update "Remove" button
	if (!pGroup->pRemoveCollectionButton) {
		pGroup->pRemoveCollectionButton = ui_ButtonCreate("Remove Collection", X_OFFSET_BASE+160, y, CERemoveStoreCollectionCB, pGroup);
		ui_WidgetSetWidth(UI_WIDGET(pGroup->pRemoveCollectionButton), 150);
		ui_ExpanderAddChild(pExpander, pGroup->pRemoveCollectionButton);
	} else {
		ui_WidgetSetPosition(UI_WIDGET(pGroup->pRemoveCollectionButton), X_OFFSET_BASE+160, y);
	}

	y += STANDARD_ROW_HEIGHT;

	// Add line
	if (!pGroup->pSeparator) {
		pGroup->pSeparator = ui_SeparatorCreate(UIHorizontal);
		ui_ExpanderAddChild(pExpander, pGroup->pSeparator);
	}
	ui_WidgetSetPosition(UI_WIDGET(pGroup->pSeparator), 0, y);
	
	y += EXPANDER_HEIGHT;

	return y;
}

static F32 CERefreshAuctionBrokerGroup(CEAuctionBrokerContactDataGroup *pGroup, UIExpander *pExpander, F32 y, int index, AuctionBrokerContactData ***peaAuctionBrokers, AuctionBrokerContactData *pAuctionBroker, AuctionBrokerContactData *pOldAuctionBroker)
{
	pGroup->index = index;

	if(!pAuctionBroker)
		return y;

	// Update Option Text field
	pGroup->pOptionTextLabel = CERefreshLabel(pGroup->pOptionTextLabel, "Option Text", "The text to display on the Contact Dialog option.", X_OFFSET_BASE, 0, y, pExpander);
	if (!pGroup->pOptionTextField) 
	{
		pGroup->pOptionTextField = MEFieldCreateSimple(kMEFieldType_Message, pOldAuctionBroker ? (&pOldAuctionBroker->optionText):NULL, &pAuctionBroker->optionText, parse_DisplayMessage, "editorCopy");
		CEAddFieldToParent(pGroup->pOptionTextField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, X_PERCENT_SMALL, UIUnitPercentage, 0, pGroup->pDoc);
	} 
	else 
	{
		ui_WidgetSetPosition(pGroup->pOptionTextField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pOptionTextField, pOldAuctionBroker ? (&pOldAuctionBroker->optionText):NULL, &pAuctionBroker->optionText);
	}

	y += STANDARD_ROW_HEIGHT;

	// Update Auction Broker def field
	pGroup->pAuctionBrokerDefLabel = CERefreshLabel(pGroup->pAuctionBrokerDefLabel, "Auction Broker Def", "The def file which will define the auction house search for the player.", X_OFFSET_BASE, 0, y, pExpander);
	if (!pGroup->pAuctionBrokerDefField) 
	{
		pGroup->pAuctionBrokerDefField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_ValidatedTextEntry, pOldAuctionBroker, pAuctionBroker, parse_AuctionBrokerContactData, "AuctionBrokerDef", g_hAuctionBrokerDictionary, "resourceName");
		CEAddFieldToParent(pGroup->pAuctionBrokerDefField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, X_PERCENT_SMALL, UIUnitPercentage, 0, pGroup->pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pAuctionBrokerDefField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pAuctionBrokerDefField, pOldAuctionBroker, pAuctionBroker);
	}

	y += STANDARD_ROW_HEIGHT;

	// Update "Remove" button
	if (!pGroup->pRemoveBrokerButton) 
	{
		pGroup->pRemoveBrokerButton = ui_ButtonCreate("Remove Auction Broker", X_OFFSET_BASE, y, CERemoveAuctionBrokerCB, pGroup);
		ui_WidgetSetWidth(UI_WIDGET(pGroup->pRemoveBrokerButton), 150);
		ui_ExpanderAddChild(pExpander, pGroup->pRemoveBrokerButton);
	} 
	else 
	{
		ui_WidgetSetPosition(UI_WIDGET(pGroup->pRemoveBrokerButton), X_OFFSET_BASE, y);
	}

	y += STANDARD_ROW_HEIGHT;

	// Add line
	if (!pGroup->pSeparator) {
		pGroup->pSeparator = ui_SeparatorCreate(UIHorizontal);
		ui_ExpanderAddChild(pExpander, pGroup->pSeparator);
	}
	ui_WidgetSetPosition(UI_WIDGET(pGroup->pSeparator), 0, y);

	y += EXPANDER_HEIGHT;

	return y;
}

static F32 CERefreshUGCSearchAgentGroup(CEUGCSearchAgentDataGroup *pGroup, UIExpander *pExpander,
						F32 y, int index, UGCSearchAgentData ***peaUGCSearchAgents,
						UGCSearchAgentData *pUGCSearchAgent, UGCSearchAgentData *pOldUGCSearchAgent)
{
	pGroup->index = index;

	if(!pUGCSearchAgent)
		return y;

	// Update Option Text field
	pGroup->pOptionTextLabel = CERefreshLabel(pGroup->pOptionTextLabel, "Option Text", 
									"The text to display on the Contact Dialog option.", 
									X_OFFSET_BASE, 0, y, pExpander);
	if (!pGroup->pOptionTextField){
		pGroup->pOptionTextField = MEFieldCreateSimple(kMEFieldType_Message, 
								pOldUGCSearchAgent ? (&pOldUGCSearchAgent->optionText):NULL, 
								&pUGCSearchAgent->optionText, parse_DisplayMessage, "editorCopy");
		CEAddFieldToParent(pGroup->pOptionTextField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 
							X_PERCENT_SMALL, UIUnitPercentage, 0, pGroup->pDoc);
	}else{
		ui_WidgetSetPosition(pGroup->pOptionTextField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pOptionTextField, 
									pOldUGCSearchAgent ? (&pOldUGCSearchAgent->optionText):NULL, 
									&pUGCSearchAgent->optionText);
	}
	y += STANDARD_ROW_HEIGHT;
	// Update dialog title field
	pGroup->pTitleLabel = CERefreshLabel(pGroup->pTitleLabel, "Dialog Title", 
						"The title for the search results dialog.", X_OFFSET_BASE, 0, y, pExpander);
	if (!pGroup->pTitleField){
		pGroup->pTitleField = MEFieldCreateSimple(kMEFieldType_Message, 
								pOldUGCSearchAgent ? (&pOldUGCSearchAgent->dialogTitle):NULL,
								&pUGCSearchAgent->dialogTitle, parse_DisplayMessage, "editorCopy");
		CEAddFieldToParent(pGroup->pTitleField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 
							X_PERCENT_SMALL, UIUnitPercentage, 0, pGroup->pDoc);
	}else{
		ui_WidgetSetPosition(pGroup->pTitleField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pTitleField, 
								pOldUGCSearchAgent ? (&pOldUGCSearchAgent->dialogTitle):NULL, 
								&pUGCSearchAgent->dialogTitle);
	}
	y += STANDARD_ROW_HEIGHT;
	// Update dialog text field
	pGroup->pDialogTextLabel = CERefreshLabel(pGroup->pDialogTextLabel, "Dialog Text", 
							"The text to display on the search results dialog.", X_OFFSET_BASE, 
							0, y, pExpander);
	if (!pGroup->pDialogTextField){
		pGroup->pDialogTextField = MEFieldCreateSimple(kMEFieldType_Message, 
								pOldUGCSearchAgent ? (&pOldUGCSearchAgent->dialogText):NULL, 
								&pUGCSearchAgent->dialogText, parse_DisplayMessage, "editorCopy");
		CEAddFieldToParent(pGroup->pDialogTextField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0,
							X_PERCENT_SMALL, UIUnitPercentage, 0, pGroup->pDoc);
	}else{
		ui_WidgetSetPosition(pGroup->pDialogTextField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pDialogTextField, 
								pOldUGCSearchAgent ? (&pOldUGCSearchAgent->dialogText):NULL, 
								&pUGCSearchAgent->dialogText);
	}
	y += STANDARD_ROW_HEIGHT;
	// Update location field
	pGroup->pLocationLabel = CERefreshLabel(pGroup->pLocationLabel, "Location", 
							"The location to limit the search to. Blank for all.", 
							X_OFFSET_BASE, 0, y, pExpander);
	if (!pGroup->pLocationField) {
		pGroup->pLocationField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOldUGCSearchAgent,
									pUGCSearchAgent, parse_UGCSearchAgentData, "location");
		CEAddFieldToParent(pGroup->pLocationField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 
							X_PERCENT_SMALL, UIUnitPercentage, 0, pGroup->pDoc);
	}else{
		ui_WidgetSetPosition(pGroup->pLocationField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pLocationField, pOldUGCSearchAgent, pUGCSearchAgent);
	}
	y += STANDARD_ROW_HEIGHT;
	// Update max duration field
	pGroup->pMaxDurationLabel = CERefreshLabel(pGroup->pMaxDurationLabel, "Max Duration", 
								"The longest average mission duration in minutes to search for. 0 for all.",
								X_OFFSET_BASE, 0, y, pExpander);
	if (!pGroup->pMaxDurationField){
		pGroup->pMaxDurationField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOldUGCSearchAgent,
									pUGCSearchAgent, parse_UGCSearchAgentData, "maxDuration");
		CEAddFieldToParent(pGroup->pMaxDurationField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0,
							X_PERCENT_SMALL, UIUnitPercentage, 0, pGroup->pDoc);
	}else{
		ui_WidgetSetPosition(pGroup->pMaxDurationField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pMaxDurationField, pOldUGCSearchAgent, pUGCSearchAgent);
	}
	y += STANDARD_ROW_HEIGHT;
	// Update LastNDays field
	pGroup->pLastNDaysLabel = CERefreshLabel(pGroup->pLastNDaysLabel, "Last N Days", 
			"Limit the search to missions that passed review at most this many days ago.  0 for all.",
			X_OFFSET_BASE, 0, y, pExpander);
	if (!pGroup->pLastNDaysField){
		pGroup->pLastNDaysField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOldUGCSearchAgent, 
										pUGCSearchAgent, parse_UGCSearchAgentData, "LastNDays");
		CEAddFieldToParent(pGroup->pLastNDaysField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 
							X_PERCENT_SMALL, UIUnitPercentage, 0, pGroup->pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pLastNDaysField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pLastNDaysField, pOldUGCSearchAgent,	pUGCSearchAgent);
	}
	y += STANDARD_ROW_HEIGHT;

	// Update "Remove" button
	if (!pGroup->pRemoveButton) {
		pGroup->pRemoveButton = ui_ButtonCreate("Remove UGC Search Contact", X_OFFSET_BASE, y,
												CERemoveUGCSearchAgentCB, pGroup);
		ui_WidgetSetWidth(UI_WIDGET(pGroup->pRemoveButton), 200);
		ui_ExpanderAddChild(pExpander, pGroup->pRemoveButton);
	} else {
		ui_WidgetSetPosition(UI_WIDGET(pGroup->pRemoveButton), X_OFFSET_BASE, y);
	}

	y += STANDARD_ROW_HEIGHT;

	// Add line
	if (!pGroup->pSeparator) {
		pGroup->pSeparator = ui_SeparatorCreate(UIHorizontal);
		ui_ExpanderAddChild(pExpander, pGroup->pSeparator);
	}
	ui_WidgetSetPosition(UI_WIDGET(pGroup->pSeparator), 0, y);

	y += EXPANDER_HEIGHT;

	return y;
}

static ContactImageMenuItem* CEImageMenuItemFromGroup( CEImageMenuItemGroup* pGroup, void*** peaOldWrappers, ContactImageMenuItem** out_ppOldItem )
{
	void* pWrapper = eaGet( pGroup->peaWrappers, pGroup->index );
	void* pOldWrapper = NULL;

	if( peaOldWrappers ) {
		pOldWrapper = peaOldWrappers ? eaGet( peaOldWrappers, pGroup->index ) : NULL;
	}

	if( pGroup->pImageMenuItemFromWrapperFunc ) {
		if( out_ppOldItem ) {
			*out_ppOldItem = pGroup->pImageMenuItemFromWrapperFunc( pOldWrapper );
		}
		return pGroup->pImageMenuItemFromWrapperFunc( pWrapper );
	} else {
		if( out_ppOldItem ) {
			*out_ppOldItem = pOldWrapper;
		}
		return pWrapper;
	}
}

static void CEPlaceImageMenuItemIconCB( const char* zmName, const char* logicalName, const float* mapPos, const char* mapIcon, UserData userData )
{
	CEImageMenuItemGroup *pGroup = userData;
	ContactImageMenuItem* pImageMenuItem = CEImageMenuItemFromGroup( pGroup, NULL, NULL );
	assert( !zmName && !logicalName );

	if( mapPos && mapIcon ) {
		StructCopyString( &pImageMenuItem->iconImage, mapIcon );
		pImageMenuItem->x = mapPos[ 0 ];
		pImageMenuItem->y = mapPos[ 1 ];
		pGroup->pCommonCallbackParams->pDialogChangedFunc( pGroup->pCommonCallbackParams->pDialogChangedData );
	}
}

static int sortstrings( const char** pstr1, const char** pstr2 )
{
	return stricmp( *pstr1, *pstr2 );
}

static void CEPlaceImageMenuItemIcon( UIButton* ignored, CEImageMenuItemGroup *pGroup )
{
	ContactImageMenuItem* pImageMenuItem = CEImageMenuItemFromGroup( pGroup, NULL, NULL );
	Vec2 pItemPos;
	const char** eaIcons = NULL;

	if( pGroup->pCommonCallbackParams->pDocIsEditableFunc && !pGroup->pCommonCallbackParams->pDocIsEditableFunc( pGroup->pCommonCallbackParams->pDocIsEditableData )) {
		return;
	}

	setVec2( pItemPos, pImageMenuItem->x, pImageMenuItem->y );
	FOR_EACH_IN_EARRAY_FORWARDS( g_basicTextures, BasicTexture, tex ) {
		eaPush( &eaIcons, texGetName( tex ));
	} FOR_EACH_END;
	eaQSort( eaIcons, sortstrings );
	emShowOverworldMapPicker( eaIcons, pItemPos, pImageMenuItem->iconImage, CEPlaceImageMenuItemIconCB, pGroup );
	eaDestroy( &eaIcons );
}

//called by expander's refresh.
F32 CERefreshImageMenuItemGroup(CEImageMenuItemGroup *pGroup, UIExpander *pExpander,
								F32 y, int index,
								void*** peaWrappers, void*** peaOldWrappers)
{
	ContactImageMenuItem* pImageMenuItem;
	ContactImageMenuItem* pOldImageMenuItem;	
	pGroup->index = index;
	pGroup->peaWrappers = peaWrappers;
	
	pImageMenuItem = CEImageMenuItemFromGroup( pGroup, peaOldWrappers, &pOldImageMenuItem );
	if( !pImageMenuItem ) {
		return y;
	}
	
	// Add line
	if (!pGroup->pSeparator) {
		pGroup->pSeparator = ui_SeparatorCreate(UIHorizontal);
		ui_ExpanderAddChild(pExpander, pGroup->pSeparator);
	}
	ui_WidgetSetPosition(UI_WIDGET(pGroup->pSeparator), 0, y);

	y += EXPANDER_HEIGHT;

	//name:
	pGroup->pNameLabel = CERefreshLabel(pGroup->pNameLabel, "Item Name", "The name of this item (shown to player)", X_OFFSET_BASE, 0, y, pExpander);

	if (!pGroup->pNameField){
		pGroup->pNameField = MEFieldCreateSimple(kMEFieldType_Message, 
			pOldImageMenuItem ? (&pOldImageMenuItem->name):NULL, 
			&pImageMenuItem->name, parse_DisplayMessage, "editorCopy");
		GEAddFieldToParent(pGroup->pNameField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1, UIUnitPercentage, 0, pGroup->pCommonCallbackParams->pFieldChangeFunc, pGroup->pCommonCallbackParams->pFieldPreChangeFunc, pGroup->pCommonCallbackParams->pFieldChangeData);
	}else{
		ui_WidgetSetPosition(pGroup->pNameField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pNameField, 
			pOldImageMenuItem ? (&pOldImageMenuItem->name):NULL, 
			&pImageMenuItem->name);
	}
	pGroup->pNameField->pUIWidget->rightPad = X_OFFSET_CONTROL + 4;
	
	// Update "Remove" button:
	if (!pGroup->pRemoveButton) {
		pGroup->pRemoveButton = ui_ButtonCreate("Remove Item", 0, y,
			CERemoveImageMenuItemCB, pGroup);
		ui_ExpanderAddChild(pExpander, pGroup->pRemoveButton);
	}
	ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pRemoveButton), 0, y, 0, 0, UITopRight);
	ui_WidgetSetWidth( UI_WIDGET(pGroup->pRemoveButton), X_OFFSET_CONTROL );

	y += STANDARD_ROW_HEIGHT;
	//visible condition:
	pGroup->pVisibleConditionLabel = CERefreshLabel(pGroup->pVisibleConditionLabel, 
		"Visible Condition", "if this is true or blank this item will be visible on the menu.",
		X_OFFSET_BASE, 0, y, pExpander);
	if(!pGroup->pVisibleConditionField){
		pGroup->pVisibleConditionField = MEFieldCreateSimple(kMEFieldTypeEx_Expression, 
			pOldImageMenuItem, pImageMenuItem, parse_ContactImageMenuItem, "VisibleCondition");
		GEAddFieldToParent(pGroup->pVisibleConditionField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1, UIUnitPercentage, 0, pGroup->pCommonCallbackParams->pFieldChangeFunc, pGroup->pCommonCallbackParams->pFieldPreChangeFunc, pGroup->pCommonCallbackParams->pFieldChangeData);
	} else {
		ui_WidgetSetPosition(pGroup->pVisibleConditionField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pVisibleConditionField, pOldImageMenuItem, pImageMenuItem);
	}
	y += STANDARD_ROW_HEIGHT;
	//requires condition:
	pGroup->pRequiresConditionLabel = CERefreshLabel(pGroup->pRequiresConditionLabel, 
		"Requires Condition", "if this is true or blank the user will be able to choose this option.",
		X_OFFSET_BASE, 0, y, pExpander);
	if(!pGroup->pRequiresConditionField){
		pGroup->pRequiresConditionField = MEFieldCreateSimple(kMEFieldTypeEx_Expression, 
			pOldImageMenuItem, pImageMenuItem, parse_ContactImageMenuItem, "RequiresCondition");
		GEAddFieldToParent(pGroup->pRequiresConditionField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1, UIUnitPercentage, 0, pGroup->pCommonCallbackParams->pFieldChangeFunc, pGroup->pCommonCallbackParams->pFieldPreChangeFunc, pGroup->pCommonCallbackParams->pFieldChangeData);
	} else {
		ui_WidgetSetPosition(pGroup->pRequiresConditionField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pRequiresConditionField, pOldImageMenuItem, pImageMenuItem);
	}
	y += STANDARD_ROW_HEIGHT;
	//requires condition:
	pGroup->pRecommendedConditionLabel = CERefreshLabel(pGroup->pRecommendedConditionLabel, 
		"Recommended Condition", "if this is not blank and false, the UI will discourage the user from picking this.",
		X_OFFSET_BASE, 0, y, pExpander);
	if(!pGroup->pRecommendedConditionField){
		pGroup->pRecommendedConditionField = MEFieldCreateSimple(kMEFieldTypeEx_Expression, 
			pOldImageMenuItem, pImageMenuItem, parse_ContactImageMenuItem, "RecommendedCondition");
		GEAddFieldToParent(pGroup->pRecommendedConditionField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1, UIUnitPercentage, 0, pGroup->pCommonCallbackParams->pFieldChangeFunc, pGroup->pCommonCallbackParams->pFieldPreChangeFunc, pGroup->pCommonCallbackParams->pFieldChangeData);
	} else {
		ui_WidgetSetPosition(pGroup->pRecommendedConditionField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pRecommendedConditionField, pOldImageMenuItem, pImageMenuItem);
	}

	y += STANDARD_ROW_HEIGHT;
	//icon:
	pGroup->pIconLabel = CERefreshLabel(pGroup->pIconLabel, "Icon", 
							"The icon to show for this item.", X_OFFSET_BASE, 0, y, pExpander);
	if (!pGroup->pIconField) {
		pGroup->pIconField = MEFieldCreateSimple(kMEFieldType_Texture, 
							pOldImageMenuItem, pImageMenuItem, parse_ContactImageMenuItem, 
							"IconImage");
		GEAddFieldToParent(pGroup->pIconField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1, UIUnitPercentage, 0, pGroup->pCommonCallbackParams->pFieldChangeFunc, pGroup->pCommonCallbackParams->pFieldPreChangeFunc, pGroup->pCommonCallbackParams->pFieldChangeData);
	} else {
		ui_WidgetSetPosition(pGroup->pIconField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pIconField, pOldImageMenuItem, pImageMenuItem);
	}
	y += TEXTURE_ROW_HEIGHT;
	//position
	pGroup->pPosLabel = CERefreshLabel(pGroup->pPosLabel, "Position X,Y (0-1):", 
						"X and Y position from 0.0 to 1.0 on the Image Menu for this Item", 
						X_OFFSET_BASE, 0, y, pExpander);
	if (!pGroup->pPosXField){
		pGroup->pPosXField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOldImageMenuItem,
			pImageMenuItem, parse_ContactImageMenuItem, "XPosition");
		GEAddFieldToParent(pGroup->pPosXField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 80, UIUnitFixed, 0, pGroup->pCommonCallbackParams->pFieldChangeFunc, pGroup->pCommonCallbackParams->pFieldPreChangeFunc, pGroup->pCommonCallbackParams->pFieldChangeData);
	}else{
		ui_WidgetSetPosition(pGroup->pPosXField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pPosXField, pOldImageMenuItem, pImageMenuItem);
	}
	if (!pGroup->pPosYField) {
		pGroup->pPosYField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOldImageMenuItem,
			pImageMenuItem, parse_ContactImageMenuItem, "YPosition");
		GEAddFieldToParent(pGroup->pPosYField, UI_WIDGET(pExpander), 0, y, 0, 80, UIUnitFixed, 0, pGroup->pCommonCallbackParams->pFieldChangeFunc, pGroup->pCommonCallbackParams->pFieldPreChangeFunc, pGroup->pCommonCallbackParams->pFieldChangeData);
	}else{
		MEFieldSetAndRefreshFromData(pGroup->pPosYField, pOldImageMenuItem, pImageMenuItem);
	}
	ui_WidgetSetPosition(pGroup->pPosYField->pUIWidget, X_OFFSET_CONTROL + 84, y);

	if( !pGroup->pPosPlaceButton ) {
		pGroup->pPosPlaceButton = ui_ButtonCreate( "Visual Placement", 0, 0, CEPlaceImageMenuItemIcon, pGroup );
		ui_WidgetAddChild(UI_WIDGET(pExpander), UI_WIDGET(pGroup->pPosPlaceButton));
	}
	ui_WidgetSetPosition(UI_WIDGET(pGroup->pPosPlaceButton), X_OFFSET_CONTROL + 84*2, y);
	
	y += STANDARD_ROW_HEIGHT;
	//add game action button:
	pGroup->pActionLabel = CERefreshLabel(pGroup->pActionLabel, "Game Actions:", 
		"Game Actions to do when this item is clicked", 
		X_OFFSET_BASE, 0, y, pExpander);
	if (!pGroup->pActionButton) {
		pGroup->pActionButton = ui_GameActionEditButtonCreate(NULL, pImageMenuItem->action, pOldImageMenuItem ? pOldImageMenuItem->action : NULL, CEImageMenuGameActionChangeCB, NULL, pGroup);
		ui_WidgetSetWidth(UI_WIDGET(pGroup->pActionButton), 150);
		ui_WidgetSetPosition(UI_WIDGET(pGroup->pActionButton), X_OFFSET_CONTROL, y);
		ui_ExpanderAddChild(pExpander, pGroup->pActionButton);
	}else{
		ui_WidgetSetPosition(UI_WIDGET(pGroup->pActionButton), X_OFFSET_CONTROL, y);
	}

	y += STANDARD_ROW_HEIGHT;


	return y;
}


static void CERefreshEndDialogAudioGroup(CEEndDialogAudioGroup *pGroup, UIExpander *pExpander, F32 y, int index, EndDialogAudio *** peaEndDialogAudios, EndDialogAudio * pEndDialogAudio, EndDialogAudio * pOldEndDialogAudio)
{
	pGroup->index = index;

	// Update button
	if (!pGroup->pButton) {
		pGroup->pButton = ui_ButtonCreate("Remove", 5, y, CERemoveEndDialogAudioCB, pGroup);
		ui_WidgetSetWidth(UI_WIDGET(pGroup->pButton), 80);
		ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pButton), 10, y, X_PERCENT_SMALL, 0, UITopLeft);
		ui_ExpanderAddChild(pExpander, pGroup->pButton);
	} else {
		ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pButton), 10, y, X_PERCENT_SMALL, 0, UITopLeft);
	}

	// Update audio name field
	pGroup->pAudioFileLabel = CERefreshLabel(pGroup->pAudioFileLabel, "Audio File", "The name of the audio file to play when the dialog with the contact ends.", X_OFFSET_BASE, 0, y, pExpander);
	if (!pGroup->pAudioFileField) {
		pGroup->pAudioFileField = MEFieldCreateSimpleDataProvided(kMEFieldType_ValidatedTextEntry, pOldEndDialogAudio, pEndDialogAudio, parse_EndDialogAudio, "Audio", NULL, sndGetEventListStatic(), NULL);
		CEAddFieldToParent(pGroup->pAudioFileField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, X_PERCENT_SMALL, UIUnitPercentage, 0, pGroup->pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pAudioFileField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pAudioFileField, pOldEndDialogAudio, pEndDialogAudio);
	}
}


static void CERefreshPowerStoreGroup(CEPowerStoreGroup *pGroup, UIExpander *pExpander, F32 y, int index, PowerStoreRef ***peaStores, PowerStoreRef *pStore, PowerStoreRef *pOldStore)
{
	pGroup->index = index;

	// Update button
	if (!pGroup->pButton) {
		pGroup->pButton = ui_ButtonCreate("Remove", 5, y, CERemovePowerStoreCB, pGroup);
		ui_WidgetSetWidth(UI_WIDGET(pGroup->pButton), 80);
		ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pButton), 10, y, X_PERCENT_SMALL, 0, UITopLeft);
		ui_ExpanderAddChild(pExpander, pGroup->pButton);
	} else {
		ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pButton), 10, y, X_PERCENT_SMALL, 0, UITopLeft);
	}

	// Update store field
	pGroup->pStoreLabel = CERefreshLabel(pGroup->pStoreLabel, "Power Store", "The name of the power store this contact has available.", X_OFFSET_BASE, 0, y, pExpander);
	if (!pGroup->pStoreField) {
		pGroup->pStoreField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_ValidatedTextEntry, pOldStore, pStore, parse_PowerStoreRef, "Ref", "PowerStore", "resourceName");
		CEAddFieldToParent(pGroup->pStoreField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, X_PERCENT_SMALL, UIUnitPercentage, 0, pGroup->pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pStoreField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pStoreField, pOldStore, pStore);
	}
}

static void CERefreshEndDialogAudioListExpander(ContactEditDoc *pDoc)
{
	EndDialogAudio * pEndDialogAudio, * pOldEndDialogAudio;
	int i;
	int iNumAudioFiles = eaSize(&pDoc->pContact->eaEndDialogAudios);

	if (!pDoc->pEndDialogAudioListExpander->group) 
	{
		ui_ExpanderGroupInsertExpander(pDoc->pExpanderGroup, pDoc->pEndDialogAudioListExpander, 7);
	}

	// Refresh the audio file groups
	for(i = 0; i < iNumAudioFiles; ++i)
	{
		pEndDialogAudio = pDoc->pContact->eaEndDialogAudios[i];
		if (pDoc->pOrigContact && (eaSize(&pDoc->pOrigContact->eaEndDialogAudios) > i)) {
			pOldEndDialogAudio = pDoc->pOrigContact->eaEndDialogAudios[i];
		} else {
			pOldEndDialogAudio = NULL;
		}
		while (i >= eaSize(&pDoc->eaEndDialogAudioGroups)) {
			CEEndDialogAudioGroup *pGroup = CECreateEndDialogAudioGroup(pDoc);
			eaPush(&pDoc->eaEndDialogAudioGroups, pGroup);
		}
		CERefreshEndDialogAudioGroup(pDoc->eaEndDialogAudioGroups[i], pDoc->pEndDialogAudioListExpander, i * STANDARD_ROW_HEIGHT, i, &pDoc->pContact->eaEndDialogAudios, pEndDialogAudio, pOldEndDialogAudio);
	}

	// Free unused audio file groups
	for(i = eaSize(&pDoc->eaEndDialogAudioGroups) - 1; i >= iNumAudioFiles; --i) {
		assert(pDoc->eaEndDialogAudioGroups);
		CEFreeEndDialogAudioGroup(pDoc->eaEndDialogAudioGroups[i]);
		eaRemove(&pDoc->eaEndDialogAudioGroups, i);
	}

	// Put in the Add button
	if (!pDoc->pEndDialogAudioAddButton) {
		pDoc->pEndDialogAudioAddButton = ui_ButtonCreate("Add End Dialog Audio", X_OFFSET_BASE, iNumAudioFiles * STANDARD_ROW_HEIGHT, CEAddEndDialogAudioCB, pDoc);
		ui_WidgetSetWidth(UI_WIDGET(pDoc->pEndDialogAudioAddButton), 150);
		ui_ExpanderAddChild(pDoc->pEndDialogAudioListExpander, pDoc->pEndDialogAudioAddButton);
	} else {
		ui_WidgetSetPosition(UI_WIDGET(pDoc->pEndDialogAudioAddButton), X_OFFSET_BASE, iNumAudioFiles * STANDARD_ROW_HEIGHT);
	}

	// Se the expander height
	ui_ExpanderSetHeight(pDoc->pEndDialogAudioListExpander, STANDARD_ROW_HEIGHT + (iNumAudioFiles * STANDARD_ROW_HEIGHT));
}

static void CERefreshStoresExpander(ContactEditDoc *pDoc)
{
	StoreRef *pStore, *pOldStore;
	PowerStoreRef *pPowerStore, *pOldPowerStore;
	int i;
	int numStores = eaSize(&pDoc->pContact->stores);
	int numPowerStores = eaSize(&pDoc->pContact->powerStores);
	F32 y = 0;
	int n = 0;

	// Hide store expander if Single Dialog type
	if (pDoc->pContact->type == ContactType_SingleDialog) {
		if (pDoc->pStoreExpander->group) {
			ui_ExpanderGroupRemoveExpander(pDoc->pExpanderGroup, pDoc->pStoreExpander);
		}
		return;
	}
	if (!pDoc->pStoreExpander->group) {
		ui_ExpanderGroupInsertExpander(pDoc->pExpanderGroup, pDoc->pStoreExpander, 5);
	}

	// Refresh store option messages
	// Buy Tab
	pDoc->pBuyOptionLabel = CERefreshLabel(pDoc->pBuyOptionLabel, "Buy Text", "The text to be displayed as the \"Buy\" tab.  If not set, the default \"Buy\" tab message will be used.", X_OFFSET_BASE, 0, y, pDoc->pStoreExpander);
	if (!pDoc->pBuyOptionField) {
		pDoc->pBuyOptionField = MEFieldCreateSimple(kMEFieldType_Message, pDoc->pOrigContact?&pDoc->pOrigContact->buyOptionMsg:NULL, pDoc->pContact?&pDoc->pContact->buyOptionMsg:NULL, parse_DisplayMessage, "editorCopy");
		CEAddFieldToParent(pDoc->pBuyOptionField, UI_WIDGET(pDoc->pStoreExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
	} else {
		ui_WidgetSetPosition(pDoc->pDisplayNameField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pDoc->pBuyOptionField, pDoc->pOrigContact?&pDoc->pOrigContact->buyOptionMsg:NULL, pDoc->pContact?&pDoc->pContact->buyOptionMsg:NULL);
	}
	y += STANDARD_ROW_HEIGHT;

	// Sell Tab
	pDoc->pSellOptionLabel = CERefreshLabel(pDoc->pSellOptionLabel, "Sell Text", "The text to be displayed as the \"Sell\" tab.  If not set, the default \"Sell\" tab message will be used.", X_OFFSET_BASE, 0, y, pDoc->pStoreExpander);
	if (!pDoc->pSellOptionField) {
		pDoc->pSellOptionField = MEFieldCreateSimple(kMEFieldType_Message, pDoc->pOrigContact?&pDoc->pOrigContact->sellOptionMsg:NULL, pDoc->pContact?&pDoc->pContact->sellOptionMsg:NULL, parse_DisplayMessage, "editorCopy");
		CEAddFieldToParent(pDoc->pSellOptionField, UI_WIDGET(pDoc->pStoreExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
	} else {
		ui_WidgetSetPosition(pDoc->pDisplayNameField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pDoc->pSellOptionField, pDoc->pOrigContact?&pDoc->pOrigContact->sellOptionMsg:NULL, pDoc->pContact?&pDoc->pContact->sellOptionMsg:NULL);
	}
	y += STANDARD_ROW_HEIGHT;

	// BuyBack Tab
	pDoc->pBuyBackOptionLabel = CERefreshLabel(pDoc->pBuyBackOptionLabel, "Buyback Text", "The text to be displayed as the \"Buy Back\" tab.  If not set, the default \"Buy Back\" tab message will be used.", X_OFFSET_BASE, 0, y, pDoc->pStoreExpander);
	if (!pDoc->pBuyBackOptionField) {
		pDoc->pBuyBackOptionField = MEFieldCreateSimple(kMEFieldType_Message, pDoc->pOrigContact?&pDoc->pOrigContact->buyBackOptionMsg:NULL, pDoc->pContact?&pDoc->pContact->buyBackOptionMsg:NULL, parse_DisplayMessage, "editorCopy");
		CEAddFieldToParent(pDoc->pBuyBackOptionField, UI_WIDGET(pDoc->pStoreExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
	} else {
		ui_WidgetSetPosition(pDoc->pDisplayNameField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pDoc->pBuyBackOptionField, pDoc->pOrigContact?&pDoc->pOrigContact->buyBackOptionMsg:NULL, pDoc->pContact?&pDoc->pContact->buyBackOptionMsg:NULL);
	}
	y += STANDARD_ROW_HEIGHT;

	n += CERefreshDialogBlocksUsingDoc(pDoc, &pDoc->eaNoStoreDialogGroups, pDoc->pStoreExpander, &y, n, "No Store Text", "If this text is set, it will display instead of the store if no items are available to buy or sell to the player.",
		&pDoc->pContact->noStoreItemsDialog, pDoc->pOrigContact ? &pDoc->pOrigContact->noStoreItemsDialog : NULL);

	while (eaSize(&pDoc->eaNoStoreDialogGroups) > n){
		CEFreeDialogGroup(eaPop(&pDoc->eaNoStoreDialogGroups));
	}

	// Refresh the store groups
	for(i=0; i<numStores; ++i) {
		pStore = pDoc->pContact->stores[i];
		if (pDoc->pOrigContact && (eaSize(&pDoc->pOrigContact->stores) > i)) {
			pOldStore = pDoc->pOrigContact->stores[i];
		} else {
			pOldStore = NULL;
		}
		while (i >= eaSize(&pDoc->eaStoreGroups)) {
			CEStoreGroup *pGroup = CECreateStoreGroup(pDoc, &pDoc->pContact->stores);
			eaPush(&pDoc->eaStoreGroups, pGroup);
		}
		CERefreshStoreGroup(pDoc->eaStoreGroups[i], pDoc->pStoreExpander, y, i, &pDoc->pContact->stores, pStore, pOldStore, 0);
		y+= STANDARD_ROW_HEIGHT;
	}

	// Free unused store groups
	for(i = eaSize(&pDoc->eaStoreGroups)-1; i >= numStores; --i) {
		assert(pDoc->eaStoreGroups);
		CEFreeStoreGroup(pDoc->eaStoreGroups[i]);
		eaRemove(&pDoc->eaStoreGroups, i);
	}

	// Refresh the power store groups
	for(i=0; i<numPowerStores; ++i) {
		pPowerStore = pDoc->pContact->powerStores[i];
		if (pDoc->pOrigContact && (eaSize(&pDoc->pOrigContact->powerStores) > i)) {
			pOldPowerStore = pDoc->pOrigContact->powerStores[i];
		} else {
			pOldPowerStore = NULL;
		}
		while (i >= eaSize(&pDoc->eaPowerStoreGroups)) {
			CEPowerStoreGroup *pGroup = CECreatePowerStoreGroup(pDoc);
			eaPush(&pDoc->eaPowerStoreGroups, pGroup);
		}
		CERefreshPowerStoreGroup(pDoc->eaPowerStoreGroups[i], pDoc->pStoreExpander, y, i, &pDoc->pContact->powerStores, pPowerStore, pOldPowerStore);
		y+= STANDARD_ROW_HEIGHT;
	}

	// Free unused power store groups
	for(i = eaSize(&pDoc->eaPowerStoreGroups)-1; i >= numPowerStores; --i) {
		assert(pDoc->eaPowerStoreGroups);
		CEFreePowerStoreGroup(pDoc->eaPowerStoreGroups[i]);
		eaRemove(&pDoc->eaPowerStoreGroups, i);
	}

	// Put in the Add button
	if (!pDoc->pStoreAddButton) {
		pDoc->pStoreAddButton = ui_ButtonCreate("Add Store", X_OFFSET_BASE, y, CEAddStoreCB, pDoc);
		ui_WidgetSetWidth(UI_WIDGET(pDoc->pStoreAddButton), 150);
		ui_ExpanderAddChild(pDoc->pStoreExpander, pDoc->pStoreAddButton);
	} else {
		ui_WidgetSetPosition(UI_WIDGET(pDoc->pStoreAddButton), X_OFFSET_BASE, y);
	}

	// Put in the Add button
	if (!pDoc->pPowerStoreAddButton) {
		pDoc->pPowerStoreAddButton = ui_ButtonCreate("Add Power Store", X_OFFSET_BASE+160, y, CEAddPowerStoreCB, pDoc);
		ui_WidgetSetWidth(UI_WIDGET(pDoc->pPowerStoreAddButton), 150);
		ui_ExpanderAddChild(pDoc->pStoreExpander, pDoc->pPowerStoreAddButton);
	} else {
		ui_WidgetSetPosition(UI_WIDGET(pDoc->pPowerStoreAddButton), X_OFFSET_BASE+160, y);
	}

	y+= STANDARD_ROW_HEIGHT;

	// Se the expander height
	ui_ExpanderSetHeight(pDoc->pStoreExpander, y);
}

static void CERefreshStoreCollectionsExpander(ContactEditDoc *pDoc)
{
	StoreCollection *pStoreCollection, *pOldStoreCollection;
	int i, y = 0;
	int numStoreCollections = eaSize(&pDoc->pContact->storeCollections);

	// Hide store expander if Single Dialog type
	if (pDoc->pContact->type == ContactType_SingleDialog) {
		if (pDoc->pStoreCollectionExpander->group) {
			ui_ExpanderGroupRemoveExpander(pDoc->pExpanderGroup, pDoc->pStoreCollectionExpander);
		}
		return;
	}
	if (!pDoc->pStoreCollectionExpander->group) {
		ui_ExpanderGroupInsertExpander(pDoc->pExpanderGroup, pDoc->pStoreCollectionExpander, 5);
	}

	// Refresh store collections
	for(i=0; i<numStoreCollections; ++i) {
		pStoreCollection = pDoc->pContact->storeCollections[i];
		if (pDoc->pOrigContact && (eaSize(&pDoc->pOrigContact->storeCollections) > i)) {
			pOldStoreCollection = pDoc->pOrigContact->storeCollections[i];
		} else {
			pOldStoreCollection = NULL;
		}
		while (i >= eaSize(&pDoc->eaStoreCollections)) {
			CEStoreCollectionGroup *pGroup = CECreateStoreCollectionGroup(pDoc);
			eaPush(&pDoc->eaStoreCollections, pGroup);
		}
		y = CERefreshStoreCollectionGroup(pDoc->eaStoreCollections[i], pDoc->pStoreCollectionExpander, y, i, &pDoc->pContact->storeCollections, pStoreCollection, pOldStoreCollection);
	}

	// Free unused store groups
	for(i = eaSize(&pDoc->eaStoreCollections)-1; i >= numStoreCollections; --i) {
		assert(pDoc->eaStoreCollections);
		CEFreeStoreCollectionGroup(pDoc->eaStoreCollections[i]);
		eaRemove(&pDoc->eaStoreCollections, i);
	}

	// Put in the Add button
	if (!pDoc->pStoreCollectionAddButton) {
		pDoc->pStoreCollectionAddButton = ui_ButtonCreate("Add Store Collection", X_OFFSET_BASE, y, CEAddStoreCollectionCB, pDoc);
		ui_WidgetSetWidth(UI_WIDGET(pDoc->pStoreCollectionAddButton), 150);
		ui_ExpanderAddChild(pDoc->pStoreCollectionExpander, pDoc->pStoreCollectionAddButton);
	} else {
		ui_WidgetSetPosition(UI_WIDGET(pDoc->pStoreCollectionAddButton), X_OFFSET_BASE, y);
	}

	y += STANDARD_ROW_HEIGHT;

	// Se the expander height
	ui_ExpanderSetHeight(pDoc->pStoreCollectionExpander, y);
}

static void CERefreshAuctionBrokersExpander(ContactEditDoc *pDoc)
{
	AuctionBrokerContactData *pAuctionBroker, *pOldAuctionBroker;
	int i, y = 0;
	int iNumAuctionBroker = eaSize(&pDoc->pContact->ppAuctionBrokerOptionList);

	// Hide auction broker expander if Single Dialog type
	if (pDoc->pContact->type == ContactType_SingleDialog) 
	{
		if (pDoc->pAuctionBrokerExpander->group) 
		{
			ui_ExpanderGroupRemoveExpander(pDoc->pExpanderGroup, pDoc->pAuctionBrokerExpander);
		}
		return;
	}

	if (!pDoc->pAuctionBrokerExpander->group) 
	{
		ui_ExpanderGroupInsertExpander(pDoc->pExpanderGroup, pDoc->pAuctionBrokerExpander, 5);
	}

	// Refresh Auction Brokers
	for (i = 0; i < iNumAuctionBroker; ++i) 
	{
		pAuctionBroker = pDoc->pContact->ppAuctionBrokerOptionList[i];
		if (pDoc->pOrigContact && (eaSize(&pDoc->pOrigContact->ppAuctionBrokerOptionList) > i)) 
		{
			pOldAuctionBroker = pDoc->pOrigContact->ppAuctionBrokerOptionList[i];
		} 
		else 
		{
			pOldAuctionBroker = NULL;
		}

		while (i >= eaSize(&pDoc->eaAuctionBrokers)) 
		{
			CEAuctionBrokerContactDataGroup *pGroup = CECreateAuctionBrokerContactDataGroup(pDoc);
			eaPush(&pDoc->eaAuctionBrokers, pGroup);
		}
		y = CERefreshAuctionBrokerGroup(pDoc->eaAuctionBrokers[i], pDoc->pAuctionBrokerExpander, y, i, &pDoc->pContact->ppAuctionBrokerOptionList, pAuctionBroker, pOldAuctionBroker);
	}

	// Free unused auction broker groups
	for (i = eaSize(&pDoc->eaAuctionBrokers)-1; i >= iNumAuctionBroker; --i) 
	{
		assert(pDoc->eaAuctionBrokers);
		CEFreeAuctionBrokerContactDataGroup(pDoc->eaAuctionBrokers[i]);
		eaRemove(&pDoc->eaAuctionBrokers, i);
	}

	// Put in the Add button
	if (!pDoc->pAuctionBrokerAddButton) 
	{
		pDoc->pAuctionBrokerAddButton = ui_ButtonCreate("Add Auction Broker", X_OFFSET_BASE, y, CEAddAuctionBrokerCB, pDoc);
		ui_WidgetSetWidth(UI_WIDGET(pDoc->pAuctionBrokerAddButton), 150);
		ui_ExpanderAddChild(pDoc->pAuctionBrokerExpander, pDoc->pAuctionBrokerAddButton);
	} 
	else 
	{
		ui_WidgetSetPosition(UI_WIDGET(pDoc->pAuctionBrokerAddButton), X_OFFSET_BASE, y);
	}

	y += STANDARD_ROW_HEIGHT;

	// Se the expander height
	ui_ExpanderSetHeight(pDoc->pAuctionBrokerExpander, y);
}

static void CERefreshUGCSearchAgentExpander(ContactEditDoc *pDoc)
{
	UGCSearchAgentData *pUGCSearchAgent, *pOldUGCSearchAgent;
	int i, y = 0;
	int iNumUGCSearchAgents = eaSize(&pDoc->pContact->ppUGCSearchAgentOptionList);
	
	if (!pDoc->pUGCSearchAgentExpander->group) 
	{
		ui_ExpanderGroupInsertExpander(pDoc->pExpanderGroup, pDoc->pUGCSearchAgentExpander, 5);
	}

	// Refresh UGC Search Agents
	for (i = 0; i < iNumUGCSearchAgents; ++i) 
	{
		pUGCSearchAgent = pDoc->pContact->ppUGCSearchAgentOptionList[i];
		if (pDoc->pOrigContact && (eaSize(&pDoc->pOrigContact->ppUGCSearchAgentOptionList) > i)) 
		{
			pOldUGCSearchAgent = pDoc->pOrigContact->ppUGCSearchAgentOptionList[i];
		} 
		else 
		{
			pOldUGCSearchAgent = NULL;
		}

		while (i >= eaSize(&pDoc->eaUGCSearchAgents)) 
		{
			CEUGCSearchAgentDataGroup *pGroup = CECreateUGCSearchAgentDataGroup(pDoc);
			eaPush(&pDoc->eaUGCSearchAgents, pGroup);
		}
		y = CERefreshUGCSearchAgentGroup(pDoc->eaUGCSearchAgents[i], pDoc->pUGCSearchAgentExpander, y, i, &pDoc->pContact->ppUGCSearchAgentOptionList, pUGCSearchAgent, pOldUGCSearchAgent);
	}

	// Free unused UGC Search Agent groups
	for (i = eaSize(&pDoc->eaUGCSearchAgents)-1; i >= iNumUGCSearchAgents; --i) {
		assert(pDoc->eaUGCSearchAgents);
		CEFreeUGCSearchAgentDataGroup(pDoc->eaUGCSearchAgents[i]);
		eaRemove(&pDoc->eaUGCSearchAgents, i);
	}

	// Put in the Add button
	if (!pDoc->pUGCSearchAgentAddButton){
		pDoc->pUGCSearchAgentAddButton = ui_ButtonCreate("Add UGC Search Agent", X_OFFSET_BASE, y, CEAddUGCSearchAgentCB, pDoc);
		ui_WidgetSetWidth(UI_WIDGET(pDoc->pUGCSearchAgentAddButton), 200);
		ui_ExpanderAddChild(pDoc->pUGCSearchAgentExpander, pDoc->pUGCSearchAgentAddButton);
	}else {
		ui_WidgetSetPosition(UI_WIDGET(pDoc->pUGCSearchAgentAddButton), X_OFFSET_BASE, y);
	}
	y += STANDARD_ROW_HEIGHT;
	// Se the expander height
	ui_ExpanderSetHeight(pDoc->pUGCSearchAgentExpander, y);
}

static void CERefreshImageMenuExpander(ContactEditDoc *pDoc){
	ContactImageMenuData *pImageMenu, *pOldImageMenu = NULL;
	int i, y = 0;
	int iNumItems;
	
	if( pDoc->pOrigContact ){
		pOldImageMenu = pDoc->pOrigContact->pImageMenuData;
	}

	if(contact_IsImageMenu(pDoc->pContact))
	{
		if(!pDoc->pContact->pImageMenuData){
			if(pOldImageMenu){
				pDoc->pContact->pImageMenuData = StructClone(parse_ContactImageMenuData, pOldImageMenu);
			}else{
				pDoc->pContact->pImageMenuData = StructCreate(parse_ContactImageMenuData);
			}
		}
		pImageMenu = pDoc->pContact->pImageMenuData;
		iNumItems = eaSize(&pImageMenu->items);


		if (!pDoc->pImageMenuExpander->group) 
		{
			ui_ExpanderGroupInsertExpander(pDoc->pExpanderGroup, pDoc->pImageMenuExpander, 5);
		}

		// Update Title
		pDoc->pImageMenuTitleLabel = CERefreshLabel(pDoc->pImageMenuTitleLabel, "Title", 
													"The title of this image menu.", X_OFFSET_BASE, 0, y, pDoc->pImageMenuExpander);
		if (!pDoc->pImageMenuTitleField){
			pDoc->pImageMenuTitleField = MEFieldCreateSimple(kMEFieldType_Message, 
															 pOldImageMenu ? (&pOldImageMenu->title):NULL, 
															 &pImageMenu->title, parse_DisplayMessage, "editorCopy");
			CEAddFieldToParent(pDoc->pImageMenuTitleField, UI_WIDGET(pDoc->pImageMenuExpander), 
							   X_OFFSET_CONTROL, y, 0, X_PERCENT_SMALL, UIUnitPercentage, 0, pDoc);
		}else{
			ui_WidgetSetPosition(pDoc->pImageMenuTitleField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pDoc->pImageMenuTitleField, 
										 pOldImageMenu ? (&pOldImageMenu->title):NULL, &pImageMenu->title);
		}
		y += STANDARD_ROW_HEIGHT;

		// Update BG Image field
		pDoc->pImageMenuBGImageLabel = CERefreshLabel(pDoc->pImageMenuBGImageLabel, "BG Image", 
													  "The background image used by this image menu.", X_OFFSET_BASE, 0, y, pDoc->pImageMenuExpander);
		if (!pDoc->pImageMenuBGImageField){
			pDoc->pImageMenuBGImageField = MEFieldCreateSimple(
					kMEFieldType_Texture, pOldImageMenu, 
					pImageMenu, parse_ContactImageMenuData, "BackgroundImage");
			CEAddFieldToParent(pDoc->pImageMenuBGImageField, UI_WIDGET(pDoc->pImageMenuExpander), 
							   X_OFFSET_CONTROL, y, 0, X_PERCENT_SMALL, UIUnitPercentage, 0, pDoc);
		}else{
			ui_WidgetSetPosition(pDoc->pImageMenuBGImageField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pDoc->pImageMenuBGImageField, pOldImageMenu, pImageMenu);
		}
		y += TEXTURE_ROW_HEIGHT;
	
		// Refresh ImageMenu items
		for (i = 0; i < iNumItems; ++i) 
		{
			ContactImageMenuItem ***peaItems, ***peaOldItems;

			peaItems = &pDoc->pContact->pImageMenuData->items;
			if (pDoc->pOrigContact && pDoc->pOrigContact->pImageMenuData){
				peaOldItems = &pDoc->pOrigContact->pImageMenuData->items;
			} else {
				peaOldItems = NULL;
			}

			while (i >= eaSize(&pDoc->eaImageMenuItemGroups)) {
				CEImageMenuItemGroup *pImageMenuItemGroup = CECreateImageMenuItemGroup(pDoc->pContact->name,
																					   CEIsDocEditable, pDoc,
																					   CEUpdateUI, pDoc,
																					   CEFixupMessagesWrapper, pDoc,
																					   NULL, parse_ContactImageMenuItem,
																					   CEFieldChangedCB, CEFieldPreChangeCB, pDoc);
				eaPush(&pDoc->eaImageMenuItemGroups, pImageMenuItemGroup);
			}

			y = CERefreshImageMenuItemGroup(pDoc->eaImageMenuItemGroups[i], pDoc->pImageMenuExpander, 
											y, i, peaItems, peaOldItems);
		}

		// Free unused ImageMenu Items
		for (i = eaSize(&pDoc->eaImageMenuItemGroups)-1; i >= iNumItems; --i) 
		{
			CEFreeImageMenuItemGroup(pDoc->eaImageMenuItemGroups[i]);
			eaRemove(&pDoc->eaImageMenuItemGroups, i);
		}
	}
	else
	{
		eaDestroyEx(&pDoc->eaImageMenuItemGroups, CEFreeImageMenuItemGroup);
	}

	// Put in the Add button
	if (!pDoc->pImageMenuItemAddButton) 
	{
		pDoc->pImageMenuItemAddButton = ui_ButtonCreate("Add Item to Image Menu", X_OFFSET_BASE, y, CEAddImageMenuItemCB, pDoc);
		ui_WidgetSetWidth(UI_WIDGET(pDoc->pImageMenuItemAddButton), 200);
		ui_ExpanderAddChild(pDoc->pImageMenuExpander, pDoc->pImageMenuItemAddButton);
	} 
	else 
	{
		ui_WidgetSetPosition(UI_WIDGET(pDoc->pImageMenuItemAddButton), X_OFFSET_BASE, y);
	}

	y += STANDARD_ROW_HEIGHT;

	// Set the expander height
	ui_ExpanderSetHeight(pDoc->pImageMenuExpander, y);
}




F32 CERefreshOfferGroup(CEOfferGroup *pGroup, UIExpander *pExpander, F32 y, F32 xPercentWidth, int index, void ***peaMissionOfferWrappers, ContactMissionOffer *pOffer, ContactMissionOffer *pOldOffer, bool bSplitView, MissionDef *pMissionDef)
{	
	bool bAllowGrant = (pOffer->allowGrantOrReturn == ContactMissionAllow_GrantAndReturn || pOffer->allowGrantOrReturn == ContactMissionAllow_GrantOnly || pOffer->allowGrantOrReturn == ContactMissionAllow_ReplayGrant);
	bool bAllowReturn = (pOffer->allowGrantOrReturn == ContactMissionAllow_GrantAndReturn || pOffer->allowGrantOrReturn == ContactMissionAllow_ReturnOnly);
	bool bAllowSubMissionComplete = (pOffer->allowGrantOrReturn == ContactMissionAllow_SubMissionComplete);
	bool bIsFauxChest = (bAllowSubMissionComplete && pOffer->eUIType == ContactMissionUIType_FauxTreasureChest);
	int i, n = 0;

	pGroup->index = index;
	pGroup->peaMissionOfferWrappers = peaMissionOfferWrappers;

	// Update remove button
	if (!pGroup->pRemoveButton) {
		pGroup->pRemoveButton = ui_ButtonCreate("Delete Offer", 5, y, CERemoveOfferCB, pGroup);
		ui_WidgetSetWidth(UI_WIDGET(pGroup->pRemoveButton), 90);
		ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pRemoveButton), 5, y, 0, 0, UITopRight);
		ui_ExpanderAddChild(pExpander, pGroup->pRemoveButton);
	} else {
		ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pRemoveButton), 5, y, 0, 0, UITopRight);
	}

	// Update up button
	if (index != 0) {
		if (!pGroup->pUpButton) {
			pGroup->pUpButton = ui_ButtonCreate("Up", 100, y, CEMoveOfferUpCB, pGroup);
			ui_WidgetSetWidth(UI_WIDGET(pGroup->pUpButton), 25);
			ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pUpButton), 100, y, 0, 0, UITopRight);
			ui_ExpanderAddChild(pExpander, pGroup->pUpButton);
		} else {
			ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pUpButton), 100, y, 0, 0, UITopRight);
		}
	} else if (pGroup->pUpButton) {
		ui_WidgetQueueFree((UIWidget*)pGroup->pUpButton);
		pGroup->pUpButton = NULL;
	}

	// Update down button
	if (index != eaSize(peaMissionOfferWrappers)-1) {
		if (!pGroup->pDownButton) {
			pGroup->pDownButton = ui_ButtonCreate("Dn", 130, y, CEMoveOfferDownCB, pGroup);
			ui_WidgetSetWidth(UI_WIDGET(pGroup->pDownButton), 25);
			ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pDownButton), 130, y, 0, 0, UITopRight);
			ui_ExpanderAddChild(pExpander, pGroup->pDownButton);
		} else {
			ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pDownButton), 130, y, 0, 0, UITopRight);
		}
	} else if (pGroup->pDownButton) {
		ui_WidgetQueueFree((UIWidget*)pGroup->pDownButton);
		pGroup->pDownButton = NULL;
	}

	// Update mission field
	pGroup->pMissionLabel = CERefreshLabel(pGroup->pMissionLabel, "Mission", "The name of the mission to be offered", X_OFFSET_BASE, 0, y, pExpander);
	if (!pGroup->pMissionField) {
		pGroup->pMissionField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_ValidatedTextEntry, pOldOffer, pOffer, parse_ContactMissionOffer, "MissionDef", "MissionDef", "resourceName");
		GEAddFieldToParent(pGroup->pMissionField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, X_PERCENT_SMALL, UIUnitPercentage, 0, pGroup->pFieldChangeFunc, pGroup->pFieldPreChangeFunc, pGroup->pFieldChangeData);
	} else {
		ui_WidgetSetPosition(pGroup->pMissionField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pMissionField, pOldOffer, pOffer);
	}

	y += STANDARD_ROW_HEIGHT;

	pGroup->pAllegianceLabel = CERefreshLabel(pGroup->pAllegianceLabel, "Required Allegiances", "Player must be one of these allegiances to get this offer.", X_OFFSET_BASE, 0, y, pExpander);
	if (!pGroup->pAllegianceField) {
		pGroup->pAllegianceField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_FlagCombo, pOldOffer, pOffer, parse_ContactMissionOffer, "RequiredAllegiance", "Allegiance", "resourceName");
		GEAddFieldToParent(pGroup->pAllegianceField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 0, pGroup->pFieldChangeFunc, pGroup->pFieldPreChangeFunc, pGroup->pFieldChangeData);
	} else {
		ui_WidgetSetPosition(pGroup->pAllegianceField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pAllegianceField, pOldOffer, pOffer);
	}

	y += STANDARD_ROW_HEIGHT;

	// Special dialog name
	pGroup->pSpecialDialogLabel = CERefreshLabel(pGroup->pSpecialDialogLabel, "Spc. Dlg. Name", "The name used to offer this mission from a special dialog action.", X_OFFSET_BASE, 0, y, pExpander);
	if (!pGroup->pSpecialDialogField) 
	{
		pGroup->pSpecialDialogField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOldOffer, pOffer, parse_ContactMissionOffer, "SpecialDialogName");
		GEAddFieldToParent(pGroup->pSpecialDialogField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, X_PERCENT_SMALL, UIUnitPercentage, 0, pGroup->pFieldChangeFunc, pGroup->pFieldPreChangeFunc, pGroup->pFieldChangeData);
	} else {
		ui_WidgetSetPosition(pGroup->pSpecialDialogField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pSpecialDialogField, pOldOffer, pOffer);
	}

	y += STANDARD_ROW_HEIGHT;

	// Update internal name
	if (pMissionDef)
	{
		char *estrDialogInternalName = NULL;

		pGroup->pSpecialDialogInternalNameLabel = CERefreshLabel(pGroup->pSpecialDialogInternalNameLabel, "Internal Name", "This is the actual name of the special dialog used by the system. Use this name for event listening.", X_OFFSET_BASE, 0, y, pExpander);
		if (pGroup->pSpecialDialogInternalNameTextEntry == NULL)
		{
			pGroup->pSpecialDialogInternalNameTextEntry = ui_TextEntryCreate("", X_OFFSET_CONTROL, y);
			ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pSpecialDialogInternalNameTextEntry), X_OFFSET_CONTROL, y, 0, 0, UITopLeft);
			ui_WidgetSetWidthEx(UI_WIDGET(pGroup->pSpecialDialogInternalNameTextEntry), xPercentWidth, UIUnitPercentage);			
			ui_ExpanderAddChild(pExpander, pGroup->pSpecialDialogInternalNameTextEntry);
			ui_SetActive(UI_WIDGET(pGroup->pSpecialDialogInternalNameTextEntry), false);
		}
		estrCreate(&estrDialogInternalName);
		estrCopy2(&estrDialogInternalName, pOffer->pchSpecialDialogName);
		if (estrLength(&estrDialogInternalName) > 0)
		{
			MDEMissionFixupDialogNameForSaving(pMissionDef, pGroup->pchContactName, &estrDialogInternalName);
		}		
		ui_TextEntrySetText(pGroup->pSpecialDialogInternalNameTextEntry, estrDialogInternalName);
		estrDestroy(&estrDialogInternalName);

		y += STANDARD_ROW_HEIGHT * 2;
	}

	// Update choice
	if (!pGroup->pChoiceField) {
		CECreateLabel("Offer Type", "Determines whether the contact is allowed to grant or return this mission.", X_OFFSET_BASE, y, pExpander);
		pGroup->pChoiceField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pOldOffer, pOffer, parse_ContactMissionOffer, "AllowGrantOrReturn", ContactMissionAllowEnum);
		GEAddFieldToParent(pGroup->pChoiceField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, X_PERCENT_SMALL, UIUnitPercentage, 0, pGroup->pFieldChangeFunc, pGroup->pFieldPreChangeFunc, pGroup->pFieldChangeData);
		MEFieldSetChangeCallback(pGroup->pChoiceField, CERemoteOfferFieldsChangedCB, pGroup);
	} else {
		ui_WidgetSetPosition(pGroup->pChoiceField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pChoiceField, pOldOffer, pOffer);
	}

	//Update Remote Flags
	if (!pGroup->pRemoteFlagsField) {
		pGroup->pRemoteLabel = CERefreshLabel(pGroup->pRemoteLabel, "Remote Flags", "Determines whether the contact can offer/return this mission remotely", X_OFFSET_AUDIO, X_PERCENT_SPLIT, y-STANDARD_ROW_HEIGHT, pExpander);
		pGroup->pRemoteFlagsField = MEFieldCreateSimpleEnum(kMEFieldType_FlagCombo, pOldOffer, pOffer, parse_ContactMissionOffer, "RemoteFlags", ContactMissionRemoteFlagsEnum);
		GEAddFieldToParent(pGroup->pRemoteFlagsField, UI_WIDGET(pExpander), X_OFFSET_AUDIO, y, X_PERCENT_SMALL, X_PERCENT_SMALL_SECOND, UIUnitPercentage, 165, pGroup->pFieldChangeFunc, pGroup->pFieldPreChangeFunc, pGroup->pFieldChangeData);
		GEAddFieldToParent(pGroup->pRemoteFlagsField, UI_WIDGET(pExpander), X_OFFSET_AUDIO, y, X_PERCENT_SPLIT, 0.20, UIUnitPercentage, 0, pGroup->pFieldChangeFunc, pGroup->pFieldPreChangeFunc, pGroup->pFieldChangeData);
		MEFieldSetChangeCallback(pGroup->pRemoteFlagsField, CERemoteOfferFieldsChangedCB, pGroup);
	} else {
		ui_WidgetSetPositionEx(pGroup->pRemoteFlagsField->pUIWidget, X_OFFSET_AUDIO, y, X_PERCENT_SPLIT, 0, UITopLeft);
		MEFieldSetAndRefreshFromData(pGroup->pRemoteFlagsField, pOldOffer, pOffer);
	}

	y+=STANDARD_ROW_HEIGHT;

	// Update sub-mission
	if (bAllowSubMissionComplete){
		pGroup->pSubMissionLabel = CERefreshLabel(pGroup->pSubMissionLabel, "Sub-Mission", "The name of the sub-mission that will complete after visiting this contact", X_OFFSET_BASE, 0, y, pExpander);

		// Refresh list of sub-mission names
		eaClearEx(&pGroup->eaSubMissionNames, NULL);
		if (GET_REF(pOffer->missionDef)){
			MissionDef *pRootMission = GET_REF(pOffer->missionDef);
			for (i = 0; i < eaSize(&pRootMission->subMissions); i++){
				eaPush(&pGroup->eaSubMissionNames, strdup(pRootMission->subMissions[i]->name));
			}
		}

		if (!pGroup->pSubMissionField) {
			pGroup->pSubMissionField = MEFieldCreateSimpleDataProvided(kMEFieldType_ValidatedTextEntry, pOldOffer, pOffer, parse_ContactMissionOffer, "SubMissionName", NULL, &pGroup->eaSubMissionNames, NULL);
			GEAddFieldToParent(pGroup->pSubMissionField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, X_PERCENT_SMALL, UIUnitPercentage, 0, pGroup->pFieldChangeFunc, pGroup->pFieldPreChangeFunc, pGroup->pFieldChangeData);
		} else {
			ui_WidgetSetPosition(pGroup->pSubMissionField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pSubMissionField, pOldOffer, pOffer);
		}

		y+=STANDARD_ROW_HEIGHT;
		
		pGroup->pUITypeLabel = CERefreshLabel(pGroup->pUITypeLabel, "UI Type", "How this dialogue will be treated by the UI.", X_OFFSET_BASE, 0, y, pExpander);
		
		if (!pGroup->pUITypeField) {
			pGroup->pUITypeField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pOldOffer, pOffer, parse_ContactMissionOffer, "UIType", ContactMissionUITypeEnum);
			GEAddFieldToParent(pGroup->pUITypeField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, X_PERCENT_SMALL, UIUnitPercentage, 0, pGroup->pFieldChangeFunc, pGroup->pFieldPreChangeFunc, pGroup->pFieldChangeData);
		} else {
			ui_WidgetSetPosition(pGroup->pUITypeField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pUITypeField, pOldOffer, pOffer);
		}

	} else {
		if (pGroup->pSubMissionLabel){
			ui_WidgetQueueFree((UIWidget*)pGroup->pSubMissionLabel);
			pGroup->pSubMissionLabel = NULL;
		}
		MEFieldSafeDestroy(&pGroup->pSubMissionField);

		if (pGroup->pUITypeLabel){
			ui_WidgetQueueFree((UIWidget*)pGroup->pUITypeLabel);
			pGroup->pUITypeLabel = NULL;
		}
		MEFieldSafeDestroy(&pGroup->pUITypeField);
	}



	// Update accept field
	if (bAllowGrant){
		pGroup->pAcceptLabel = CERefreshLabel(pGroup->pAcceptLabel, "Accept Text", "The text displayed on the button for accepting the mission", X_OFFSET_BASE, 0, y, pExpander);
		if (!pGroup->pAcceptField) {
			pGroup->pAcceptField = MEFieldCreateSimple(kMEFieldType_Message, pOldOffer ? &pOldOffer->acceptStringMesg : NULL, &pOffer->acceptStringMesg, parse_DisplayMessage, "editorCopy");
			GEAddFieldToParent(pGroup->pAcceptField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, X_PERCENT_SPLIT, UIUnitPercentage, 0, pGroup->pFieldChangeFunc, pGroup->pFieldPreChangeFunc, pGroup->pFieldChangeData);
		} else {
			ui_WidgetSetPosition(pGroup->pAcceptField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pAcceptField, pOldOffer ? &pOldOffer->acceptStringMesg : NULL, &pOffer->acceptStringMesg);
		}
		y+=STANDARD_ROW_HEIGHT;

		// Update dialog formatter field for mission accept text
		pGroup->pAcceptDialogFormatterLabel = CERefreshLabel(pGroup->pAcceptDialogFormatterLabel, "Text Formatter", "Used by UI to format the mission accept text", X_OFFSET_BASE, 0, y, pExpander);

		if (pGroup->pAcceptDialogFormatterField == NULL)
		{
			pGroup->pAcceptDialogFormatterField = MEFieldCreateSimpleDictionary(kMEFieldType_ValidatedTextEntry, pOldOffer, pOffer, parse_ContactMissionOffer, "AcceptDialogFormatter", g_hContactDialogFormatterDefDictionary, parse_ContactDialogFormatterDef, "Name");
			GEAddFieldToParent(pGroup->pAcceptDialogFormatterField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, xPercentWidth, UIUnitPercentage, 0, pGroup->pFieldChangeFunc, pGroup->pFieldPreChangeFunc, pGroup->pFieldChangeData);
		} else {
			ui_WidgetSetPosition(pGroup->pAcceptDialogFormatterField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pAcceptDialogFormatterField, pOldOffer, pOffer);
		}

		y += STANDARD_ROW_HEIGHT;

		// Accept target dialog name
		pGroup->pAcceptTargetDialogLabel = CERefreshLabel(pGroup->pAcceptTargetDialogLabel, "Accept Dialog", "The name of the special dialog launched when the player accepts the mission.", X_OFFSET_BASE, 0, y, pExpander);
		if (!pGroup->pAcceptTargetDialogField) 
		{
			pGroup->pAcceptTargetDialogField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOldOffer, pOffer, parse_ContactMissionOffer, "AcceptTargetDialog");
			GEAddFieldToParent(pGroup->pAcceptTargetDialogField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, X_PERCENT_SMALL, UIUnitPercentage, 0, pGroup->pFieldChangeFunc, pGroup->pFieldPreChangeFunc, pGroup->pFieldChangeData);
		} else {
			ui_WidgetSetPosition(pGroup->pAcceptTargetDialogField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pAcceptTargetDialogField, pOldOffer, pOffer);
		}

		y += STANDARD_ROW_HEIGHT;
	}
	else{
		if (pGroup->pAcceptLabel){
			ui_WidgetQueueFree((UIWidget*)pGroup->pAcceptLabel);
			pGroup->pAcceptLabel = NULL;
		}
		MEFieldSafeDestroy(&pGroup->pAcceptField);

		if (pGroup->pAcceptDialogFormatterLabel)
		{
			ui_WidgetQueueFree((UIWidget*)pGroup->pAcceptDialogFormatterLabel);
			pGroup->pAcceptDialogFormatterLabel = NULL;
		}
		MEFieldSafeDestroy(&pGroup->pAcceptDialogFormatterField);

		if (pGroup->pAcceptTargetDialogLabel){
			ui_WidgetQueueFree((UIWidget*)pGroup->pAcceptTargetDialogLabel);
			pGroup->pAcceptTargetDialogLabel = NULL;
		}
		MEFieldSafeDestroy(&pGroup->pAcceptTargetDialogField);
	}

	if (bSplitView)
	{
		pGroup->pSoundLabel = CERefreshLabel(pGroup->pSoundLabel, "Sound to Play / Phrase to Say", "The sound file to play (top field), or the voice over phrase to say (bottom field) in a voice based on the contact's costume", X_OFFSET_AUDIO, X_PERCENT_SPLIT, y, pExpander);
		pGroup->pAnimLabel = CERefreshLabel(pGroup->pAnimLabel, "Anim List to Play / Dialog Formatter", "Anim List (Top Dropdown): The anim list override to be used as default for this contact. Dialog Formatter (Bottom Dropdown): Used by UI to format dialog messages", X_OFFSET_ANIM, X_PERCENT_SPLIT+X_PERCENT_SECOND_SML, y, pExpander);
	}
	else
	{
		ui_WidgetQueueFreeAndNull(&pGroup->pSoundLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pAnimLabel);
	}

	// Update decline field
	if (bAllowGrant){
		pGroup->pDeclineLabel = CERefreshLabel(pGroup->pDeclineLabel, "Decline Text", "The text displayed on the button for declining the mission", X_OFFSET_BASE, 0, y, pExpander);
		if (!pGroup->pDeclineField) {
			pGroup->pDeclineField = MEFieldCreateSimple(kMEFieldType_Message, pOldOffer ? &pOldOffer->declineStringMesg : NULL, &pOffer->declineStringMesg, parse_DisplayMessage, "editorCopy");
			GEAddFieldToParent(pGroup->pDeclineField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, X_PERCENT_SPLIT, UIUnitPercentage, 0, pGroup->pFieldChangeFunc, pGroup->pFieldPreChangeFunc, pGroup->pFieldChangeData);
		} else {
			ui_WidgetSetPosition(pGroup->pDeclineField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pDeclineField, pOldOffer ? &pOldOffer->declineStringMesg : NULL, &pOffer->declineStringMesg);
		}
		y += STANDARD_ROW_HEIGHT;

		// Update dialog formatter field for mission decline text
		pGroup->pDeclineDialogFormatterLabel = CERefreshLabel(pGroup->pDeclineDialogFormatterLabel, "Text Formatter", "Used by UI to format the mission decline text", X_OFFSET_BASE, 0, y, pExpander);

		if (pGroup->pDeclineDialogFormatterField == NULL)
		{
			pGroup->pDeclineDialogFormatterField = MEFieldCreateSimpleDictionary(kMEFieldType_ValidatedTextEntry, pOldOffer, pOffer, parse_ContactMissionOffer, "DeclineDialogFormatter", g_hContactDialogFormatterDefDictionary, parse_ContactDialogFormatterDef, "Name");
			GEAddFieldToParent(pGroup->pDeclineDialogFormatterField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, xPercentWidth, UIUnitPercentage, 0, pGroup->pFieldChangeFunc, pGroup->pFieldPreChangeFunc, pGroup->pFieldChangeData);
		} else {
			ui_WidgetSetPosition(pGroup->pDeclineDialogFormatterField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pDeclineDialogFormatterField, pOldOffer, pOffer);
		}

		y += STANDARD_ROW_HEIGHT;

		// Decline target dialog name
		pGroup->pDeclineTargetDialogLabel = CERefreshLabel(pGroup->pDeclineTargetDialogLabel, "Decline Dialog", "The name of the special dialog launched when the player declines the mission.", X_OFFSET_BASE, 0, y, pExpander);
		if (!pGroup->pDeclineTargetDialogField) 
		{
			pGroup->pDeclineTargetDialogField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOldOffer, pOffer, parse_ContactMissionOffer, "DeclineTargetDialog");
			GEAddFieldToParent(pGroup->pDeclineTargetDialogField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, X_PERCENT_SMALL, UIUnitPercentage, 0, pGroup->pFieldChangeFunc, pGroup->pFieldPreChangeFunc, pGroup->pFieldChangeData);
		} else {
			ui_WidgetSetPosition(pGroup->pDeclineTargetDialogField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pDeclineTargetDialogField, pOldOffer, pOffer);
		}

		y += STANDARD_ROW_HEIGHT;
	}
	else{
		if (pGroup->pDeclineLabel){
			ui_WidgetQueueFree((UIWidget*)pGroup->pDeclineLabel);
			pGroup->pDeclineLabel = NULL;
		}
		MEFieldSafeDestroy(&pGroup->pDeclineField);

		if (pGroup->pDeclineDialogFormatterLabel)
		{
			ui_WidgetQueueFree((UIWidget*)pGroup->pDeclineDialogFormatterLabel);
			pGroup->pDeclineDialogFormatterLabel = NULL;
		}
		MEFieldSafeDestroy(&pGroup->pDeclineDialogFormatterField);

		if (pGroup->pDeclineTargetDialogLabel){
			ui_WidgetQueueFree((UIWidget*)pGroup->pDeclineTargetDialogLabel);
			pGroup->pDeclineTargetDialogLabel = NULL;
		}
		MEFieldSafeDestroy(&pGroup->pDeclineTargetDialogField);

		y += STANDARD_ROW_HEIGHT;
	}

	// Update turn in field
	if (bAllowReturn){
		pGroup->pTurnInLabel = CERefreshLabel(pGroup->pTurnInLabel, "Turn In Text", "The text displayed on the button for turning in the mission.", X_OFFSET_BASE, 0, y, pExpander);
		if (!pGroup->pTurnInField) {
			pGroup->pTurnInField = MEFieldCreateSimple(kMEFieldType_Message, pOldOffer ? &pOldOffer->turnInStringMesg : NULL, &pOffer->turnInStringMesg, parse_DisplayMessage, "editorCopy");
			GEAddFieldToParent(pGroup->pTurnInField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, X_PERCENT_SPLIT, UIUnitPercentage, 0, pGroup->pFieldChangeFunc, pGroup->pFieldPreChangeFunc, pGroup->pFieldChangeData);
		} else {
			ui_WidgetSetPosition(pGroup->pTurnInField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pTurnInField, pOldOffer ? &pOldOffer->turnInStringMesg : NULL, &pOffer->turnInStringMesg);
		}
		y += STANDARD_ROW_HEIGHT;
	}
	else{
		if (pGroup->pTurnInLabel){
			ui_WidgetQueueFree((UIWidget*)pGroup->pTurnInLabel);
			pGroup->pTurnInLabel = NULL;
		}
		MEFieldSafeDestroy(&pGroup->pTurnInField);

		y += STANDARD_ROW_HEIGHT;
	}

	if (!bIsFauxChest)
	{
		n += CERefreshDialogBlocks(&pGroup->eaDialogGroups, pExpander, &y, xPercentWidth, n, CE_GREETING_LABEL, "This greeting is used when the mission is in progress regardless of contact's recent status. If there is more than one dialog block defined, one will be randomly chosen.",
			&pOffer->greetingDialog, pOldOffer ? &pOldOffer->greetingDialog : NULL, false, false, true, bSplitView,
			pGroup->pDocIsEditableFunc, pGroup->pDocIsEditableData,
			pGroup->pDialogChangedFunc, pGroup->pDialogChangedData,
			pGroup->pFieldChangeFunc, pGroup->pFieldPreChangeFunc, pGroup->pFieldChangeData);
	}

	// Update dialog groups
	if (bAllowGrant || bAllowReturn)
	{
		n += CERefreshDialogBlocks(&pGroup->eaDialogGroups, pExpander, &y, xPercentWidth, n, "In Progress", "This text is displayed if the player visits the contact while the mission is in progress. If there is more than one dialog block defined, one will be randomly chosen.",
			&pOffer->inProgressDialog, pOldOffer ? &pOldOffer->inProgressDialog : NULL, 
			false, false, true, bSplitView,
			pGroup->pDocIsEditableFunc, pGroup->pDocIsEditableData,
			pGroup->pDialogChangedFunc, pGroup->pDialogChangedData,
			pGroup->pFieldChangeFunc, pGroup->pFieldPreChangeFunc, pGroup->pFieldChangeData);
	}

	if (pOffer->allowGrantOrReturn != ContactMissionAllow_FlashbackGrant && !bIsFauxChest)
	{
		n += CERefreshDialogBlocks(&pGroup->eaDialogGroups, pExpander, &y, xPercentWidth, n, CE_COMPLETED_MISSION_LABEL, "This text is displayed if the player visits the contact when the mission is completed. If there is more than one dialog block defined, one will be randomly chosen.",
			&pOffer->completedDialog, pOldOffer ? &pOldOffer->completedDialog : NULL, 
			true, false, true, bSplitView,
			pGroup->pDocIsEditableFunc, pGroup->pDocIsEditableData,
			pGroup->pDialogChangedFunc, pGroup->pDialogChangedData,
			pGroup->pFieldChangeFunc, pGroup->pFieldPreChangeFunc, pGroup->pFieldChangeData);

		// Reward accept text
		pGroup->pRewardAcceptLabel = CERefreshLabel(pGroup->pRewardAcceptLabel, "Rwd. Accept Text", "The text displayed on the button which completes a mission which does not have a reward to choose", X_OFFSET_BASE, 0, y, pExpander);
		if (!pGroup->pRewardAcceptField) 
		{
			pGroup->pRewardAcceptField = MEFieldCreateSimple(kMEFieldType_Message, pOldOffer ? &pOldOffer->rewardAcceptMesg : NULL, &pOffer->rewardAcceptMesg, parse_DisplayMessage, "editorCopy");
			GEAddFieldToParent(pGroup->pRewardAcceptField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, X_PERCENT_SPLIT, UIUnitPercentage, 0, pGroup->pFieldChangeFunc, pGroup->pFieldPreChangeFunc, pGroup->pFieldChangeData);
		} else {
			ui_WidgetSetPosition(pGroup->pRewardAcceptField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pRewardAcceptField, pOldOffer ? &pOldOffer->rewardAcceptMesg : NULL, &pOffer->rewardAcceptMesg);
		}

		y+=STANDARD_ROW_HEIGHT;

		// Update dialog formatter field for reward accept text
		pGroup->pRewardAcceptDialogFormatterLabel = CERefreshLabel(pGroup->pRewardAcceptDialogFormatterLabel, "Text Formatter", "Used by UI to format the reward accept text", X_OFFSET_BASE, 0, y, pExpander);

		if (pGroup->pRewardAcceptDialogFormatterField == NULL)
		{
			pGroup->pRewardAcceptDialogFormatterField = MEFieldCreateSimpleDictionary(kMEFieldType_ValidatedTextEntry, pOldOffer, pOffer, parse_ContactMissionOffer, "RewardAcceptDialogFormatter", g_hContactDialogFormatterDefDictionary, parse_ContactDialogFormatterDef, "Name");
			GEAddFieldToParent(pGroup->pRewardAcceptDialogFormatterField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, xPercentWidth, UIUnitPercentage, 0, pGroup->pFieldChangeFunc, pGroup->pFieldPreChangeFunc, pGroup->pFieldChangeData);
		} else {
			ui_WidgetSetPosition(pGroup->pRewardAcceptDialogFormatterField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pRewardAcceptDialogFormatterField, pOldOffer, pOffer);
		}

		y += STANDARD_ROW_HEIGHT;

		// Reward accept target dialog name
		pGroup->pRewardAcceptTargetDialogLabel = CERefreshLabel(pGroup->pRewardAcceptTargetDialogLabel, "Rwd. Accept Dlg.", "Optional. If set, the player is taken to this dialog when they click on the reward accept button.", X_OFFSET_BASE, 0, y, pExpander);
		if (!pGroup->pRewardAcceptTargetDialogField) 
		{
			pGroup->pRewardAcceptTargetDialogField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOldOffer, pOffer, parse_ContactMissionOffer, "RewardAcceptTargetDialog");
			GEAddFieldToParent(pGroup->pRewardAcceptTargetDialogField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, X_PERCENT_SMALL, UIUnitPercentage, 0, pGroup->pFieldChangeFunc, pGroup->pFieldPreChangeFunc, pGroup->pFieldChangeData);
		} else {
			ui_WidgetSetPosition(pGroup->pRewardAcceptTargetDialogField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pRewardAcceptTargetDialogField, pOldOffer, pOffer);
		}

		y += STANDARD_ROW_HEIGHT;

		// Reward choose text
		pGroup->pRewardChooseLabel = CERefreshLabel(pGroup->pRewardChooseLabel, "Rwd. Choose Text", "The text displayed on the button which completes a mission which has a reward to choose", X_OFFSET_BASE, 0, y, pExpander);
		if (!pGroup->pRewardChooseField) 
		{
			pGroup->pRewardChooseField = MEFieldCreateSimple(kMEFieldType_Message, pOldOffer ? &pOldOffer->rewardChooseMesg : NULL, &pOffer->rewardChooseMesg, parse_DisplayMessage, "editorCopy");
			GEAddFieldToParent(pGroup->pRewardChooseField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, X_PERCENT_SPLIT, UIUnitPercentage, 0, pGroup->pFieldChangeFunc, pGroup->pFieldPreChangeFunc, pGroup->pFieldChangeData);
		} else {
			ui_WidgetSetPosition(pGroup->pRewardChooseField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pRewardChooseField, pOldOffer ? &pOldOffer->rewardChooseMesg : NULL, &pOffer->rewardChooseMesg);
		}

		y+=STANDARD_ROW_HEIGHT;

		// Update dialog formatter field for reward choose text
		pGroup->pRewardChooseDialogFormatterLabel = CERefreshLabel(pGroup->pRewardChooseDialogFormatterLabel, "Text Formatter", "Used by UI to format the reward choose text", X_OFFSET_BASE, 0, y, pExpander);

		if (pGroup->pRewardChooseDialogFormatterField == NULL)
		{
			pGroup->pRewardChooseDialogFormatterField = MEFieldCreateSimpleDictionary(kMEFieldType_ValidatedTextEntry, pOldOffer, pOffer, parse_ContactMissionOffer, "RewardChooseDialogFormatter", g_hContactDialogFormatterDefDictionary, parse_ContactDialogFormatterDef, "Name");
			GEAddFieldToParent(pGroup->pRewardChooseDialogFormatterField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, xPercentWidth, UIUnitPercentage, 0, pGroup->pFieldChangeFunc, pGroup->pFieldPreChangeFunc, pGroup->pFieldChangeData);
		} else {
			ui_WidgetSetPosition(pGroup->pRewardChooseDialogFormatterField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pRewardChooseDialogFormatterField, pOldOffer, pOffer);
		}

		y += STANDARD_ROW_HEIGHT;

		// Reward choose target dialog name
		pGroup->pRewardChooseTargetDialogLabel = CERefreshLabel(pGroup->pRewardChooseTargetDialogLabel, "Rwd. Choose Dlg.", "Optional. If set, the player is taken to this dialog when they click on the reward choose button.", X_OFFSET_BASE, 0, y, pExpander);
		if (!pGroup->pRewardChooseTargetDialogField) 
		{
			pGroup->pRewardChooseTargetDialogField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOldOffer, pOffer, parse_ContactMissionOffer, "RewardChooseTargetDialog");
			GEAddFieldToParent(pGroup->pRewardChooseTargetDialogField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, X_PERCENT_SMALL, UIUnitPercentage, 0, pGroup->pFieldChangeFunc, pGroup->pFieldPreChangeFunc, pGroup->pFieldChangeData);
		} else {
			ui_WidgetSetPosition(pGroup->pRewardChooseTargetDialogField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pRewardChooseTargetDialogField, pOldOffer, pOffer);
		}

		y += STANDARD_ROW_HEIGHT;

		// Reward back out text
		pGroup->pRewardAbortLabel = CERefreshLabel(pGroup->pRewardAbortLabel, "Rwd. Back Out Text", "The text displayed on the button which backs out of completing a mission", X_OFFSET_BASE, 0, y, pExpander);
		if (!pGroup->pRewardAbortField) 
		{
			pGroup->pRewardAbortField = MEFieldCreateSimple(kMEFieldType_Message, pOldOffer ? &pOldOffer->rewardAbortMesg : NULL, &pOffer->rewardAbortMesg, parse_DisplayMessage, "editorCopy");
			GEAddFieldToParent(pGroup->pRewardAbortField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, X_PERCENT_SPLIT, UIUnitPercentage, 0, pGroup->pFieldChangeFunc, pGroup->pFieldPreChangeFunc, pGroup->pFieldChangeData);
		} else {
			ui_WidgetSetPosition(pGroup->pRewardAbortField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pRewardAbortField, pOldOffer ? &pOldOffer->rewardAbortMesg : NULL, &pOffer->rewardAbortMesg);
		}

		y+=STANDARD_ROW_HEIGHT;

		// Update dialog formatter field for reward back out text
		pGroup->pRewardAbortDialogFormatterLabel = CERefreshLabel(pGroup->pRewardAbortDialogFormatterLabel, "Text Formatter", "Used by UI to format the reward back out text", X_OFFSET_BASE, 0, y, pExpander);

		if (pGroup->pRewardAbortDialogFormatterField == NULL)
		{
			pGroup->pRewardAbortDialogFormatterField = MEFieldCreateSimpleDictionary(kMEFieldType_ValidatedTextEntry, pOldOffer, pOffer, parse_ContactMissionOffer, "RewardAbortDialogFormatter", g_hContactDialogFormatterDefDictionary, parse_ContactDialogFormatterDef, "Name");
			GEAddFieldToParent(pGroup->pRewardAbortDialogFormatterField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, xPercentWidth, UIUnitPercentage, 0, pGroup->pFieldChangeFunc, pGroup->pFieldPreChangeFunc, pGroup->pFieldChangeData);
		} else {
			ui_WidgetSetPosition(pGroup->pRewardAbortDialogFormatterField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pRewardAbortDialogFormatterField, pOldOffer, pOffer);
		}

		y += STANDARD_ROW_HEIGHT;

		// Reward back out target dialog name
		pGroup->pRewardAbortTargetDialogLabel = CERefreshLabel(pGroup->pRewardAbortTargetDialogLabel, "Rwd. Back Out Dlg.", "The player is taken to this dialog when they click on the complete mission back out button.", X_OFFSET_BASE, 0, y, pExpander);
		if (!pGroup->pRewardAbortTargetDialogField) 
		{
			pGroup->pRewardAbortTargetDialogField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOldOffer, pOffer, parse_ContactMissionOffer, "RewardAbortTargetDialog");
			GEAddFieldToParent(pGroup->pRewardAbortTargetDialogField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, X_PERCENT_SMALL, UIUnitPercentage, 0, pGroup->pFieldChangeFunc, pGroup->pFieldPreChangeFunc, pGroup->pFieldChangeData);
		} else {
			ui_WidgetSetPosition(pGroup->pRewardAbortTargetDialogField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pRewardAbortTargetDialogField, pOldOffer, pOffer);
		}

		y += STANDARD_ROW_HEIGHT;
	}
	else
	{
		if (pGroup->pRewardAcceptLabel)
		{
			ui_WidgetQueueFree((UIWidget*)pGroup->pRewardAcceptLabel);
			pGroup->pRewardAcceptLabel = NULL;
		}
		MEFieldSafeDestroy(&pGroup->pRewardAcceptField);

		if (pGroup->pRewardAcceptTargetDialogLabel)
		{
			ui_WidgetQueueFree((UIWidget*)pGroup->pRewardAcceptTargetDialogLabel);
			pGroup->pRewardAcceptTargetDialogLabel = NULL;
		}
		MEFieldSafeDestroy(&pGroup->pRewardAcceptTargetDialogField);

		if (pGroup->pRewardChooseLabel)
		{
			ui_WidgetQueueFree((UIWidget*)pGroup->pRewardChooseLabel);
			pGroup->pRewardChooseLabel = NULL;
		}
		MEFieldSafeDestroy(&pGroup->pRewardChooseField);

		if (pGroup->pRewardChooseTargetDialogLabel)
		{
			ui_WidgetQueueFree((UIWidget*)pGroup->pRewardChooseTargetDialogLabel);
			pGroup->pRewardChooseTargetDialogLabel = NULL;
		}
		MEFieldSafeDestroy(&pGroup->pRewardChooseTargetDialogField);

		if (pGroup->pRewardAbortLabel)
		{
			ui_WidgetQueueFree((UIWidget*)pGroup->pRewardAbortLabel);
			pGroup->pRewardAbortLabel = NULL;
		}
		MEFieldSafeDestroy(&pGroup->pRewardAbortField);

		if (pGroup->pRewardAbortTargetDialogLabel)
		{
			ui_WidgetQueueFree((UIWidget*)pGroup->pRewardAbortTargetDialogLabel);
			pGroup->pRewardAbortTargetDialogLabel = NULL;
		}
		MEFieldSafeDestroy(&pGroup->pRewardAbortTargetDialogField);
	}
	
	if (bAllowGrant)
	{
		n += CERefreshDialogBlocks(&pGroup->eaDialogGroups, pExpander, &y, xPercentWidth, n, "Failed", "This text is displayed if the player visits the contact when the mission is failed",
			&pOffer->failureDialog, pOldOffer ? &pOldOffer->failureDialog : NULL, 
			false, false, true, bSplitView,
			pGroup->pDocIsEditableFunc, pGroup->pDocIsEditableData,
			pGroup->pDialogChangedFunc, pGroup->pDialogChangedData,
			pGroup->pFieldChangeFunc, pGroup->pFieldPreChangeFunc, pGroup->pFieldChangeData);
	}

	while (eaSize(&pGroup->eaDialogGroups) > n){
		CEFreeDialogGroup(eaPop(&pGroup->eaDialogGroups));
	}

	// Add line
	if (!pGroup->pSeparator) {
		pGroup->pSeparator = ui_SeparatorCreate(UIHorizontal);
		ui_ExpanderAddChild(pExpander, pGroup->pSeparator);
	}
	ui_WidgetSetPosition(UI_WIDGET(pGroup->pSeparator), 0, y);

	y += EXPANDER_HEIGHT;

	return y;
}


static void CERefreshOfferExpander(ContactEditDoc *pDoc)
{
	ContactMissionOffer *pOffer, *pOldOffer;
	int numOffers = eaSize(&pDoc->pContact->offerList);
	F32 y = 0;
	int i;

	// Hide offer expander if Single Dialog type
	if (!pDoc->pOfferExpander->group) {
		ui_ExpanderGroupInsertExpander(pDoc->pExpanderGroup, pDoc->pOfferExpander, 3);
	}

	// Refresh the group
	for(i=0; i<numOffers; ++i) {
		pOffer = pDoc->pContact->offerList[i];
		if (pDoc->pOrigContact && (eaSize(&pDoc->pOrigContact->offerList) > i)) {
			pOldOffer = pDoc->pOrigContact->offerList[i];
		} else {
			pOldOffer = NULL;
		}
		while (i >= eaSize(&pDoc->eaOfferGroups)) {
			CEOfferGroup *pGroup = CECreateOfferGroup(pDoc->pContact->name,
				CEIsDocEditable, pDoc,
				CEUpdateUI, pDoc,
				CEFixupMessagesWrapper, pDoc,
				NULL, parse_ContactMissionOffer,
				CEFieldChangedCB, CEFieldPreChangeCB, pDoc);

			eaPush(&pDoc->eaOfferGroups, pGroup);
		}
		y = CERefreshOfferGroup(pDoc->eaOfferGroups[i], pDoc->pOfferExpander, y, X_PERCENT_SPLIT, i, &pDoc->pContact->offerList, pOffer, pOldOffer, true, NULL);
	}

	// Free unused offer groups
	for(i = eaSize(&pDoc->eaOfferGroups)-1; i >= numOffers; --i) {
		assert(pDoc->eaOfferGroups);
		CEFreeOfferGroup(pDoc->eaOfferGroups[i]);
		eaRemove(&pDoc->eaOfferGroups, i);
	}

	// Put in the Add button
	if (!pDoc->pOfferAddButton) {
		pDoc->pOfferAddButton = ui_ButtonCreate("Add Mission Offer", X_OFFSET_BASE, y, CEAddOfferCB, pDoc);
		ui_WidgetSetWidth(UI_WIDGET(pDoc->pOfferAddButton), 150);
		ui_ExpanderAddChild(pDoc->pOfferExpander, pDoc->pOfferAddButton);
	} else {
		ui_WidgetSetPosition(UI_WIDGET(pDoc->pOfferAddButton), X_OFFSET_BASE, y);
	}
	y += STANDARD_ROW_HEIGHT;

	// Se the expander height
	ui_ExpanderSetHeight(pDoc->pOfferExpander, y);
}

static void CERefreshMissionSearchExpander(ContactEditDoc *pDoc)
{
	F32 y = 0;
	int i, n = 0;

	// Hide expander if not a Mission Search contact
	if (!contact_IsMissionSearch(pDoc->pContact)) {
		if (pDoc->pMissionSearchExpander->group) {
			ui_ExpanderGroupRemoveExpander(pDoc->pExpanderGroup, pDoc->pMissionSearchExpander);
		}
		return;
	}
	if (!pDoc->pMissionSearchExpander->group) {
		ui_ExpanderGroupInsertExpander(pDoc->pExpanderGroup, pDoc->pMissionSearchExpander, 6);
	}

	// Mission Search display string
	if (!pDoc->pSearchMsgField) {
		CECreateLabel("Search Option Text", "The text to display on the option for a Mission Search.", X_OFFSET_BASE, y, pDoc->pMissionSearchExpander);
		pDoc->pSearchMsgField = MEFieldCreateSimple(kMEFieldType_Message, pDoc->pOrigContact?&pDoc->pOrigContact->missionSearchStringMsg:NULL, pDoc->pContact?&pDoc->pContact->missionSearchStringMsg:NULL, parse_DisplayMessage, "editorCopy");
		CEAddFieldToParent(pDoc->pSearchMsgField, UI_WIDGET(pDoc->pMissionSearchExpander), X_OFFSET_CONTROL, y, 0, X_PERCENT_SPLIT, UIUnitPercentage, 0, pDoc);
	} else {
		ui_WidgetSetPosition(pDoc->pSearchMsgField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pDoc->pSearchMsgField, pDoc->pOrigContact?&pDoc->pOrigContact->missionSearchStringMsg:NULL, pDoc->pContact?&pDoc->pContact->missionSearchStringMsg:NULL);
	}

	y += STANDARD_ROW_HEIGHT;

	// Refresh the groups
	n += CERefreshDialogBlocksUsingDoc(pDoc, &pDoc->eaMissionSearchDialogGroups, pDoc->pMissionSearchExpander, &y, n, "Search Dialog", "Text to display while viewing the results of a Mission Search",
								&pDoc->pContact->eaMissionSearchDialog, pDoc->pOrigContact ? &pDoc->pOrigContact->eaMissionSearchDialog : NULL);

	// Free unused dialog groups
	for(i = eaSize(&pDoc->eaMissionSearchDialogGroups)-1; i >= n; --i) {
		assert(pDoc->eaMissionSearchDialogGroups);
		CEFreeDialogGroup(pDoc->eaMissionSearchDialogGroups[i]);
		eaRemove(&pDoc->eaMissionSearchDialogGroups, i);
	}

	// Set the expander height
	ui_ExpanderSetHeight(pDoc->pMissionSearchExpander, y);
}

static void CERefreshItemAssignmentExpander(ContactEditDoc *pDoc)
{
	F32 y = 0;

	// Hide expander if not a ItemAssignmentGiver contact
	if (!contact_IsItemAssignmentGiver(pDoc->pContact)) {
		if (pDoc->pItemAssignmentExpander->group) {
			ui_ExpanderGroupRemoveExpander(pDoc->pExpanderGroup, pDoc->pItemAssignmentExpander);
		}
		return;
	}
	if (!pDoc->pItemAssignmentExpander->group) {
		ui_ExpanderGroupInsertExpander(pDoc->pExpanderGroup, pDoc->pItemAssignmentExpander, 6);
	}

	// Create ItemAssignment data if the pointer is NULL
	if (!pDoc->pContact->pItemAssignmentData) {
		pDoc->pContact->pItemAssignmentData = StructCreate(parse_ContactItemAssignmentData);
	}

	// ItemAssignment refresh time
	if (!pDoc->pItemAssignmentRefreshTimeField) {
		CECreateLabel("Refresh Time", "How often (in seconds) to refresh item assignments offered by this contact", X_OFFSET_BASE, y, pDoc->pItemAssignmentExpander);
		pDoc->pItemAssignmentRefreshTimeField = MEFieldCreateSimple(kMEFieldType_TextEntry, pDoc->pOrigContact?pDoc->pOrigContact->pItemAssignmentData:NULL, pDoc->pContact?pDoc->pContact->pItemAssignmentData:NULL, parse_ContactItemAssignmentData, "RefreshTime");
		CEAddFieldToParent(pDoc->pItemAssignmentRefreshTimeField, UI_WIDGET(pDoc->pItemAssignmentExpander), X_OFFSET_CONTROL, y, 0, X_PERCENT_SPLIT, UIUnitPercentage, 0, pDoc);
	} else {
		ui_WidgetSetPosition(pDoc->pItemAssignmentRefreshTimeField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pDoc->pItemAssignmentRefreshTimeField, pDoc->pOrigContact?pDoc->pOrigContact->pItemAssignmentData:NULL, pDoc->pContact?pDoc->pContact->pItemAssignmentData:NULL);
	}

	y += STANDARD_ROW_HEIGHT;

	// ItemAssignment Rarity Count
	if (!pDoc->pItemAssignmentRarityCountField) {
		CECreateLabel("Rarity Counts", "Rarity counts used to generate this contact's list of assignments", X_OFFSET_BASE, y, pDoc->pItemAssignmentExpander);
		pDoc->pItemAssignmentRarityCountField = MEFieldCreateSimpleEnum(kMEFieldType_FlagCombo, pDoc->pOrigContact?pDoc->pOrigContact->pItemAssignmentData:NULL, pDoc->pContact?pDoc->pContact->pItemAssignmentData:NULL, parse_ContactItemAssignmentData, "RarityCount", ItemAssignmentRarityCountTypeEnum);
		CEAddFieldToParent(pDoc->pItemAssignmentRarityCountField, UI_WIDGET(pDoc->pItemAssignmentExpander), X_OFFSET_CONTROL, y, 0, X_PERCENT_SPLIT, UIUnitPercentage, 0, pDoc);
	} else {
		ui_WidgetSetPosition(pDoc->pItemAssignmentRarityCountField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pDoc->pItemAssignmentRarityCountField, pDoc->pOrigContact?pDoc->pOrigContact->pItemAssignmentData:NULL, pDoc->pContact?pDoc->pContact->pItemAssignmentData:NULL);
	}

	y += STANDARD_ROW_HEIGHT;

	// Set the expander height
	ui_ExpanderSetHeight(pDoc->pItemAssignmentExpander, y);
}

void CEUpdateDisplay(ContactEditDoc *pDoc)
{
	int i;

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

	// Refresh doc-level fields
	for(i=eaSize(&pDoc->eaDocFields)-1; i>=0; --i) {
		MEFieldSetAndRefreshFromData(pDoc->eaDocFields[i], pDoc->pOrigContact, pDoc->pContact);
	}

	// Refresh the dynamic expanders
	// The order of refresh is important and must be in sync with other refreshes and expander show/hide behaviors
	CERefreshInfoExpander(pDoc);
	CERefreshDialogExpander(pDoc);
	CERefreshSpecialExpander(pDoc);
	CERefreshOfferExpander(pDoc);
	CERefreshLoreDialogExpander(pDoc);
	CERefreshStoresExpander(pDoc);
	CERefreshStoreCollectionsExpander(pDoc);
	CERefreshEndDialogAudioListExpander(pDoc);
	CERefreshMissionSearchExpander(pDoc);
	CERefreshItemAssignmentExpander(pDoc);
	CERefreshAuctionBrokersExpander(pDoc);
	CERefreshUGCSearchAgentExpander(pDoc);
	CERefreshImageMenuExpander(pDoc);

	// Update non-field UI components
	ui_GimmeButtonSetName(pDoc->pFileButton, pDoc->pContact->name);
	ui_GimmeButtonSetReferent(pDoc->pFileButton, pDoc->pContact);
	ui_LabelSetText(pDoc->pFilenameLabel, pDoc->pContact->filename);

	// Update saved flag
	pDoc->emDoc.saved = pDoc->pOrigContact && (StructCompare(parse_ContactDef, pDoc->pOrigContact, pDoc->pContact, 0, 0, 0) == 0);

	// Start paying attention to changes again
	pDoc->bIgnoreFieldChanges = false;
}


// This is called whenever any contact data changes to do cleanup
static void CEContactChanged(ContactEditDoc *pDoc, bool bUndoable)
{
	if (!pDoc->bIgnoreFieldChanges) {
		CEUpdateDisplay(pDoc);

		if (bUndoable) {
			CEUndoData *pData = calloc(1, sizeof(CEUndoData));
			pData->pPreContact = pDoc->pNextUndoContact;
			pData->pPostContact = StructClone(parse_ContactDef, pDoc->pContact);
			EditCreateUndoCustom(pDoc->emDoc.edit_undo_stack, CEContactUndoCB, CEContactRedoCB, CEContactUndoFreeCB, pData);
			pDoc->pNextUndoContact = StructClone(parse_ContactDef, pDoc->pContact);
		}
	}
}

static bool CESpecialDialogNameIsUnique(SA_PARAM_NN_VALID ContactEditDoc *pActiveDoc,
										SA_PARAM_NN_STR const char *pchName, 
										SA_PARAM_OP_VALID ContactMissionOffer *pMissionOfferToSkip, 
										SA_PARAM_OP_VALID SpecialDialogBlock *pSpecialDialogBlockToSkip)
{
	FOR_EACH_IN_EARRAY_FORWARDS(pActiveDoc->pContact->specialDialog, SpecialDialogBlock, pCurrentSpecialDialogBlock)
	{
		if ((pSpecialDialogBlockToSkip == NULL || pCurrentSpecialDialogBlock != pSpecialDialogBlockToSkip) &&
			stricmp(pCurrentSpecialDialogBlock->name, pchName) == 0)
		{
			return false;
		}
	}
	FOR_EACH_END

	FOR_EACH_IN_EARRAY_FORWARDS(pActiveDoc->pContact->offerList, ContactMissionOffer, pMissionOffer)
	{
		if ((pMissionOfferToSkip == NULL || pMissionOffer != pMissionOfferToSkip) &&
			stricmp(pMissionOffer->pchSpecialDialogName, pchName) == 0)
		{
			return false;
		}
	}
	FOR_EACH_END

	return true;
}

static void CEUpdateTargetsAfterSpecialDialogNameChange(ContactEditDoc *pActiveDoc, 
														const char *pchOldName,
														const char *pchNewName)
{
	if (pActiveDoc)
	{
		// Update all actions pointing to this special dialog block
		FOR_EACH_IN_EARRAY_FORWARDS(pActiveDoc->pContact->specialDialog, SpecialDialogBlock, pSpecialDialogBlock)
		{
			FOR_EACH_IN_EARRAY_FORWARDS(pSpecialDialogBlock->dialogActions, SpecialDialogAction, pSpecialDialogAction)
			{
				if (pSpecialDialogAction && pSpecialDialogAction->dialogName &&
					stricmp(pSpecialDialogAction->dialogName, pchOldName) == 0)
				{
					pSpecialDialogAction->dialogName = allocAddString(pchNewName);
				}
			}
			FOR_EACH_END
		}
		FOR_EACH_END

		FOR_EACH_IN_EARRAY_FORWARDS(pActiveDoc->pContact->offerList, ContactMissionOffer, pMissionOffer)
		{
			if (pMissionOffer)
			{
				if (pMissionOffer->pchAcceptTargetDialog && stricmp(pMissionOffer->pchAcceptTargetDialog, pchOldName) == 0)
				{
					pMissionOffer->pchAcceptTargetDialog = allocAddString(pchNewName);
				}
				if (pMissionOffer->pchDeclineTargetDialog && stricmp(pMissionOffer->pchDeclineTargetDialog, pchOldName) == 0)
				{
					pMissionOffer->pchDeclineTargetDialog = allocAddString(pchNewName);
				}
				if (pMissionOffer->pchRewardAcceptTargetDialog && stricmp(pMissionOffer->pchRewardAcceptTargetDialog, pchOldName) == 0)
				{
					pMissionOffer->pchRewardAcceptTargetDialog = allocAddString(pchNewName);
				}
				if (pMissionOffer->pchRewardChooseTargetDialog && stricmp(pMissionOffer->pchRewardChooseTargetDialog, pchOldName) == 0)
				{
					pMissionOffer->pchRewardChooseTargetDialog = allocAddString(pchNewName);
				}
				if (pMissionOffer->pchRewardAbortTargetDialog && stricmp(pMissionOffer->pchRewardAbortTargetDialog, pchOldName) == 0)
				{
					pMissionOffer->pchRewardAbortTargetDialog = allocAddString(pchNewName);
				}
			}
		}
		FOR_EACH_END
	}
}

// Handles the special dialog name change for the mission offer by updating all action targets
static void CEHandleSpecialDialogNameChangeForMissionOffer(ContactMissionOffer *pMissionOffer, const char *pchOldName)
{
	ContactEditDoc *pActiveDoc = CEGetActiveDoc();
	if (pActiveDoc && pMissionOffer)
	{
		bool bHasUniqueName;

		ANALYSIS_ASSUME(pActiveDoc != NULL);
		bHasUniqueName = pMissionOffer->pchSpecialDialogName ? CESpecialDialogNameIsUnique(pActiveDoc, pMissionOffer->pchSpecialDialogName, pMissionOffer, NULL) : true;

		if (!bHasUniqueName)
		{
			// Revert to the old name
			pMissionOffer->pchSpecialDialogName = allocAddString(pchOldName);

			emStatusPrintf("Duplicate special dialog names are not allowed!");
		}
		else if (pchOldName || pMissionOffer->pchSpecialDialogName)
		{
			CEUpdateTargetsAfterSpecialDialogNameChange(pActiveDoc, pchOldName, pMissionOffer->pchSpecialDialogName);
		}
	}
}

// Handles the special dialog name change by updating all action targets
static void CEHandleSpecialDialogNameChange(SpecialDialogBlock *pModifiedSpecialDialogBlock, const char *pchOldName)
{
	ContactEditDoc *pActiveDoc = CEGetActiveDoc();
	if (pActiveDoc && pModifiedSpecialDialogBlock)
	{
		bool bHasUniqueName;

		ANALYSIS_ASSUME(pActiveDoc != NULL);
		bHasUniqueName = CESpecialDialogNameIsUnique(pActiveDoc, pModifiedSpecialDialogBlock->name, NULL, pModifiedSpecialDialogBlock);

		// Validate for duplicate and null names
		if (pModifiedSpecialDialogBlock->name == NULL || pModifiedSpecialDialogBlock->name[0] == '\0')
		{
			// Revert to the old name
			pModifiedSpecialDialogBlock->name = allocAddString(pchOldName);

			emStatusPrintf("Empty special dialog names are not allowed!");
		}
		else if (!bHasUniqueName)
		{
			// Revert to the old name
			pModifiedSpecialDialogBlock->name = allocAddString(pchOldName);

			emStatusPrintf("Duplicate special dialog names are not allowed!");
		}
		else
		{
			CEUpdateTargetsAfterSpecialDialogNameChange(pActiveDoc, pchOldName, pModifiedSpecialDialogBlock->name);
		}
	}	
}

// This is called by MEField prior to allowing an edit
static bool CEFieldPreChangeCB(MEField *pField, bool bFinished, ContactEditDoc *pDoc)
{
	// Make sure the resource is checked out of Gimme
	return emDocIsEditable(&pDoc->emDoc, true);
}


// This is called when an MEField is changed
static void CEFieldChangedCB(MEField *pField, bool bFinished, ContactEditDoc *pDoc)
{
	if(pDoc->bIgnoreFieldChanges)
		return;

	// Is the user changing the name of a special dialog
	if (pField && bFinished &&
		stricmp(pField->pchFieldName, "Name") == 0 &&
		pField->pTable == parse_SpecialDialogBlock &&
		pDoc->pNextUndoContact->specialDialog)
	{
		int idx = eaFind(&pDoc->pContact->specialDialog, pField->pNew);
		if (pDoc->pNextUndoContact && idx >= 0 && idx < eaSize(&pDoc->pNextUndoContact->specialDialog))
		{
			CEHandleSpecialDialogNameChange(pField->pNew, pDoc->pNextUndoContact->specialDialog[idx]->name);
		}
	}
	// Is the user changing the special dialog name of a mission offer
	else if (pField && bFinished &&
		stricmp(pField->pchFieldName, "SpecialDialogName") == 0 &&
		pField->pTable == parse_ContactMissionOffer && 
		pDoc->pNextUndoContact->offerList)
	{
		int idx = eaFind(&pDoc->pContact->offerList, pField->pNew);
		if (pDoc->pNextUndoContact && idx >= 0 && idx < eaSize(&pDoc->pNextUndoContact->offerList))
		{
			CEHandleSpecialDialogNameChangeForMissionOffer(pField->pNew, pDoc->pNextUndoContact->offerList[idx]->pchSpecialDialogName);
		}		
	}

	CEContactChanged(pDoc, bFinished);
}

// This is called if the eContactFlagsField is changed
static void CERemoteFieldsChangedCB(MEField *pField, bool bFinished, ContactEditDoc *pDoc)
{
	//Clear the remote flags
	pDoc->pContact->eContactFlags =  pDoc->pContact->eContactFlags & (~ContactFlag_RemoteSpecDialog & ~ContactFlag_RemoteOfferGrant & ~ContactFlag_RemoteOfferReturn);
	CEContactChanged(pDoc, bFinished);
	if (pDoc->bIgnoreFieldChanges)
	{
		CERefreshInfoExpander(pDoc);
	}
}

// This is called if the pRemoteFlagsField or pChoiceField is changed
static void CERemoteOfferFieldsChangedCB(MEField *pField, bool bFinished, CEOfferGroup *pGroup)
{
	ContactMissionOffer *pOffer;

	if(!pGroup) {
		return;
	}

	pOffer = (ContactMissionOffer *)(*pGroup->peaMissionOfferWrappers)[pGroup->index];

	if(!pOffer) {
		return;
	}

	//pRemoteFlagsField
	if(pField->eType == kMEFieldType_FlagCombo) {
		if((pOffer->eRemoteFlags & ContactMissionRemoteFlag_Grant) && pOffer->allowGrantOrReturn == ContactMissionAllow_ReturnOnly) {	
				pOffer->allowGrantOrReturn = ContactMissionAllow_GrantAndReturn;
		}
		if((pOffer->eRemoteFlags & ContactMissionRemoteFlag_Return) && pOffer->allowGrantOrReturn == ContactMissionAllow_GrantOnly) {
				pOffer->allowGrantOrReturn = ContactMissionAllow_GrantAndReturn;
		}
	} else {
		//pChoiceField
		if(pOffer->allowGrantOrReturn == ContactMissionAllow_GrantOnly && (pOffer->eRemoteFlags & ContactMissionRemoteFlag_Return)) {
			pOffer->eRemoteFlags = pOffer->eRemoteFlags & ~ContactMissionRemoteFlag_Return;
		} else if(pOffer->allowGrantOrReturn == ContactMissionAllow_ReturnOnly && (pOffer->eRemoteFlags & ContactMissionRemoteFlag_Grant)) {
			pOffer->eRemoteFlags = pOffer->eRemoteFlags & ~ContactMissionRemoteFlag_Grant;
		}
	}

	// Update the UI
	if(pGroup->pDialogChangedFunc)
		pGroup->pDialogChangedFunc(pGroup->pDialogChangedData);
}

static void CESetScopeCB(MEField *pField, bool bFinished, ContactEditDoc *pDoc)
{
	if (!pDoc->bIgnoreFilenameChanges) {
		// Update the filename appropriately
		resFixFilename(g_ContactDictionary, pDoc->pContact->name, pDoc->pContact);
	}

	// Call on to do regular updates
	CEFieldChangedCB(pField, bFinished, pDoc);
}


static void CESetNameCB(MEField *pField, bool bFinished, ContactEditDoc *pDoc)
{
	MEFieldFixupNameString(pField, &pDoc->pContact->name);

	// When the name changes, change the title of the window
	ui_WindowSetTitle(pDoc->pMainWindow, pDoc->pContact->name);

	// Make sure the browser picks up the new contact name if the name changed
	sprintf(pDoc->emDoc.doc_name, "%s", pDoc->pContact->name);
	sprintf(pDoc->emDoc.doc_display_name, "%s", pDoc->pContact->name);
	pDoc->emDoc.name_changed = 1;

	// Call the scope function to avoid duplicating logic
	CESetScopeCB(pField, bFinished, pDoc);
}


void CEExpandChanged(UIExpander *pExpander, char *pcExpanderName)
{
	EditorPrefStoreInt(CONTACT_EDITOR, "Expander", pcExpanderName, ui_ExpanderIsOpened(pExpander));
}


//---------------------------------------------------------------------------------------------------
// UI Initialization
//---------------------------------------------------------------------------------------------------

static void CEExpanderTick(UIExpander *expand, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(expand);

	if (expand->widget.highlightPercent >= 1.0)
	{
		ContactEditDoc *pDoc = CEGetActiveDoc();
		if (pDoc)
		{
			// Focus on the widget
			ui_ScrollbarParentSetScrollPos(0.f, expand->widget.y + pDoc->pDialogFlowWindowInfo->pWidgetToFocusOnDialogNodeClick->y);		
		}
	}

	ui_ExpanderTick(expand, UI_PARENT_VALUES);
}

static UIExpander *CECreateExpander(UIExpanderGroup *pExGroup, const char *pcName, char *pcPrefName)
{
	UIExpander *pExpander = ui_ExpanderCreate(pcName, 0);
	pExpander->widget.tickF = CEExpanderTick;
	ui_WidgetSkin(UI_WIDGET(pExpander), gBoldExpanderSkin);
	ui_ExpanderGroupAddExpander(pExGroup, pExpander);
	ui_ExpanderSetOpened(pExpander, EditorPrefGetInt(CONTACT_EDITOR, "Expander", pcPrefName, 1));
	ui_ExpanderSetExpandCallback(pExpander, CEExpandChanged, pcPrefName);

	return pExpander;
}

static void ShowContinueTextAndTextFormatterCheckButtonToggle(UICheckButton *checkButton, UserData unused)
{
	s_bShowContinueTextAndTextFormatter = ui_CheckButtonGetState(checkButton);

	CERefreshDialogExpander((ContactEditDoc*)emGetActiveEditorDoc());
}

static void CEInitDialogExpander(ContactEditDoc *pDoc)
{
	UILabel *pLabel;
	UICheckButton *pCheckButton;

	pCheckButton = ui_CheckButtonCreate(X_OFFSET_BASE, 0, "Advanced Mode (show 'Continue Text' and 'Text Formatter')", s_bShowContinueTextAndTextFormatter);
	ui_CheckButtonSetToggledCallback(pCheckButton, ShowContinueTextAndTextFormatterCheckButtonToggle, NULL);
	ui_ExpanderAddChild(pDoc->pDialogExpander, pCheckButton);

	// Put row of labels at the top of the expander
	CECreateLabel("Dialog Text", NULL, X_OFFSET_CONTROL, elUINextY(pCheckButton), pDoc->pDialogExpander);

	pLabel = CECreateLabel("Sound To Play / Phrase to Say", "The sound file to play (top field), or the voice over phrase to say (bottom field) in a voice based on the contact's costume", 0, 0, pDoc->pDialogExpander);
	ui_WidgetSetPositionEx(UI_WIDGET(pLabel), X_OFFSET_AUDIO, elUINextY(pCheckButton), X_PERCENT_SPLIT, 0, UITopLeft);

	pLabel = CECreateLabel("Anim List to Play / Dialog Formatter", "Anim List (Top Dropdown When Applicable): The anim list override to be used as default for this contact. Dialog Formatter (Bottom Dropdown): Used by UI to format dialog messages", 0, 0, pDoc->pDialogExpander);
	ui_WidgetSetPositionEx(UI_WIDGET(pLabel), X_OFFSET_ANIM, elUINextY(pCheckButton), X_PERCENT_SPLIT + X_PERCENT_SECOND_SML, 0, UITopLeft);
}

static void CEExportDialogBlockToCSV(FILE *pFile, DialogBlock *pDialogBlock, 
									 const char *pchDialogType, const char *pchDateExported, 
									 const char *pchContactName, const char *pchMissionName)
{
	if (pFile && 
		pDialogBlock && 
		pDialogBlock->displayTextMesg.pEditorCopy && 
		pDialogBlock->displayTextMesg.pEditorCopy->pcDefaultString)
	{
		char *estrDialogText = NULL;

		fprintf(pFile, "%s,%s,,%s,%s,\"", pchDateExported, pchContactName, pchMissionName, pchDialogType);

		// Write the text for the dialog
		estrClear(&estrDialogText);
		estrAppend2(&estrDialogText, pDialogBlock->displayTextMesg.pEditorCopy->pcDefaultString);
		estrReplaceOccurrences(&estrDialogText, "\"", "\"\"");

		fprintf(pFile, "%s", estrDialogText);

		// End quote for the dialog text
		fprintf(pFile, "\",");

		// Write the audio file
		if (pDialogBlock->audioName)
		{
			fprintf(pFile, "%s", pDialogBlock->audioName);
		}

		fprintf(pFile, "\n");

		// Clean up
		estrDestroy(&estrDialogText);
	}
}

void CEExportDialogBlocksToCSV(FILE *pFile, DialogBlock ***ppDialogBlocks, 
									  const char *pchDialogType, const char *pchDateExported, 
									  const char *pchContactName, const char *pchMissionName)
{
	if (pFile)
	{
		S32 i;
		for (i = 0; i < eaSize(ppDialogBlocks); i++)
		{
			CEExportDialogBlockToCSV(pFile, (*ppDialogBlocks)[i], pchDialogType, pchDateExported, pchContactName, pchMissionName);
		}
	}
}

static void CEExportDialogTextAndAudio(SA_PARAM_NN_VALID ContactDef *pContactDef)
{
	if (pContactDef)
	{
		char exportedFileName[CRYPTIC_MAX_PATH];
		sprintf(exportedFileName, "%s/export/contact_audio/%s_%s.csv", 
			fileLocalDataDir(),
			pContactDef->name,
			timeGetFilenameDateStringFromSecondsSince2000(timeSecondsSince2000() + timeLocalOffsetFromUTC()));

		// Make sure all sub directories are created
		if (makeDirectoriesForFile(exportedFileName))
		{
			// Try to open the file for writing, overwrite if it already exists
			FileWrapper *pFile = fopen(exportedFileName, "w");

			if (pFile)
			{
				const char *pchContactName = pContactDef->name;
				const char *pchExportDate = timeGetDateStringFromSecondsSince2000(timeSecondsSince2000() + timeLocalOffsetFromUTC());

				// Write the headers
				fprintf(pFile, "Date Exported, Contact Name, Gender, Mission Name, Dialog Text Type, Dialog Text, Audio File\n");

				// Greetings
				CEExportDialogBlocksToCSV(pFile, &pContactDef->greetingDialog, "Greeting", pchExportDate, pchContactName, "");

				// Info
				CEExportDialogBlocksToCSV(pFile, &pContactDef->infoDialog, "Info", pchExportDate, pchContactName, "");

				// Mission List
				CEExportDialogBlocksToCSV(pFile, &pContactDef->missionListDialog, "Mission List", pchExportDate, pchContactName, "");

				// No Mission
				CEExportDialogBlocksToCSV(pFile, &pContactDef->noMissionsDialog, "No Mission", pchExportDate, pchContactName, "");

				// Return to Root
				CEExportDialogBlocksToCSV(pFile, &pContactDef->exitDialog, "Return to Root", pchExportDate, pchContactName, "");

				// General Callout
				CEExportDialogBlocksToCSV(pFile, &pContactDef->generalCallout, "General Callout", pchExportDate, pchContactName, "");

				// Mission Callout
				CEExportDialogBlocksToCSV(pFile, &pContactDef->missionCallout, "Mission Callout", pchExportDate, pchContactName, "");

				// Special Dialogs
				FOR_EACH_IN_EARRAY_FORWARDS(pContactDef->specialDialog, SpecialDialogBlock, pSpecialDialog)
				{
					if (pSpecialDialog)
					{
						// Special dialogs
						CEExportDialogBlocksToCSV(pFile, &pSpecialDialog->dialogBlock, "Special Dialog", pchExportDate, pchContactName, "");
					}
				}
				FOR_EACH_END

				// Iterate thru the mission offers
				FOR_EACH_IN_EARRAY_FORWARDS(pContactDef->offerList, ContactMissionOffer, pMissionOffer)
				{
					MissionDef *pMissionDef = pMissionOffer ? GET_REF(pMissionOffer->missionDef) : NULL;
					if (pMissionDef)
					{
						// Mission Greetings
						CEExportDialogBlocksToCSV(pFile, &pMissionOffer->greetingDialog, "Mission - Greeting", pchExportDate, pchContactName, pMissionDef->name);

						// Mission In-Progress
						CEExportDialogBlocksToCSV(pFile, &pMissionOffer->inProgressDialog, "Mission - In Progress", pchExportDate, pchContactName, pMissionDef->name);

						// Mission Completed
						CEExportDialogBlocksToCSV(pFile, &pMissionOffer->completedDialog, "Mission - Completed", pchExportDate, pchContactName, pMissionDef->name);

						// Mission Failure
						CEExportDialogBlocksToCSV(pFile, &pMissionOffer->failureDialog, "Mission - Failure", pchExportDate, pchContactName, pMissionDef->name);
					}
				}
				FOR_EACH_END

				// Close the file
				fclose(pFile);

				emStatusPrintf("Export succeeded. The file location is: %s", exportedFileName);

				return;
			}
		}
	}
	emStatusPrintf("Export failed!");
}

static void CEExportDialogTextAndAudioButtonCB(UIButton *pButton, ContactEditDoc *pDoc)
{
	if (pDoc->pContact)
	{
		CEExportDialogTextAndAudio(pDoc->pContact);
	}
}


static UIWindow *CEInitMainWindow(ContactEditDoc *pDoc)
{
	UIWindow *pWin;
	UILabel *pLabel;
	UIExpanderGroup *pExGroup;
	MEField *pField;
	UIButton *pButton = NULL;
	UISeparator *pSeparator = NULL;
	F32 y = 0;
	
	// Create the window
	pWin = ui_WindowCreate(pDoc->pContact->name, 15, 50, 350, 600);
	EditorPrefGetWindowPosition(CONTACT_EDITOR, "Window Position", "Main", pWin);

	// Contact Name
	pLabel = ui_LabelCreate("Contact Name", 0, y);
	ui_WindowAddChild(pWin, pLabel);
	pField = MEFieldCreateSimple(kMEFieldType_TextEntry, pDoc->pOrigContact, pDoc->pContact, parse_ContactDef, "Name");
	CEAddFieldToParent(pField, UI_WIDGET(pWin), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 21, pDoc);
	MEFieldSetChangeCallback(pField, CESetNameCB, pDoc);
	eaPush(&pDoc->eaDocFields, pField);
	y += STANDARD_ROW_HEIGHT;

	// Scope
	pLabel = ui_LabelCreate("Scope", 0, y);
	ui_WindowAddChild(pWin, pLabel);
	pField = MEFieldCreateSimpleDataProvided(kMEFieldType_TextEntry, pDoc->pOrigContact, pDoc->pContact, parse_ContactDef, "Scope", NULL, &geaScopes, NULL);
	CEAddFieldToParent(pField, UI_WIDGET(pWin), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 21, pDoc);
	MEFieldSetChangeCallback(pField, CESetScopeCB, pDoc);
	eaPush(&pDoc->eaDocFields, pField);
	y += STANDARD_ROW_HEIGHT;

	// File Name
	pLabel = ui_LabelCreate("File Name", 0, y);
	ui_WindowAddChild(pWin, pLabel);
	pDoc->pFileButton = ui_GimmeButtonCreate(X_OFFSET_CONTROL, y, "Contact", pDoc->pContact->name, pDoc->pContact);
	ui_WindowAddChild(pWin, pDoc->pFileButton);
	pLabel = ui_LabelCreate(pDoc->pContact->filename, X_OFFSET_CONTROL+20, y);
	ui_WindowAddChild(pWin, pLabel);
	ui_WidgetSetWidthEx(UI_WIDGET(pLabel), 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(UI_WIDGET(pLabel), 0, 21, 0, 0);
	pDoc->pFilenameLabel = pLabel;
	y += STANDARD_ROW_HEIGHT;

	// Contact type
	pLabel = ui_LabelCreate("Contact Type", 0, y);
	ui_LabelEnableTooltips(pLabel);
	ui_WidgetSetTooltipString(UI_WIDGET(pLabel), "The LIST type is flexible and enables all features of contacts.  The SINGLE DIALOG type only allows special dialogs and a few other fields.");
	ui_WindowAddChild(pWin, pLabel);
	pField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pDoc->pOrigContact, pDoc->pContact, parse_ContactDef, "ContactType", ContactTypeEnum);
	CEAddFieldToParent(pField, UI_WIDGET(pWin), X_OFFSET_CONTROL, y, 0, 160, UIUnitFixed, 21, pDoc);
	eaPush(&pDoc->eaDocFields, pField);
	y += STANDARD_ROW_HEIGHT;

	// Contact dialog audio export
	pLabel = ui_LabelCreate("Audio Export", 0, y);
	ui_LabelEnableTooltips(pLabel);
	ui_WidgetSetTooltipString(UI_WIDGET(pLabel), "Exports all dialog text which can be associated with an audio file to a CSV file.");
	ui_WindowAddChild(pWin, pLabel);

	pButton = ui_ButtonCreate("Begin Export", X_OFFSET_CONTROL, y, CEExportDialogTextAndAudioButtonCB, pDoc);
	ui_WidgetSetWidth(UI_WIDGET(pButton), 150);
	ui_WindowAddChild(pWin, pButton);

	y += STANDARD_ROW_HEIGHT;

	// Put a separator above the expander group
	pSeparator = ui_SeparatorCreate(UIHorizontal);
	ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
	ui_WindowAddChild(pWin, pSeparator);
	y += 2;

	// Main expander group
	pExGroup = ui_ExpanderGroupCreate();
	ui_WidgetSetPosition(UI_WIDGET(pExGroup), 0, y);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pExGroup), 1.0, 1.0, UIUnitPercentage, UIUnitPercentage);
	ui_WindowAddChild(pWin, pExGroup);
	pDoc->pExpanderGroup = pExGroup;

	// Other expanders
	pDoc->pInfoExpander = CECreateExpander(pExGroup, "Information", "Information");		// 0
	pDoc->pDialogExpander = CECreateExpander(pExGroup, "Dialog Text", "Dialog");		// 1
	pDoc->pSpecialExpander = CECreateExpander(pExGroup, "Special Dialogs", "Special");	// 2
	pDoc->pOfferExpander = CECreateExpander(pExGroup, "Mission Offers", "Offers");		// 3
	pDoc->pLoreExpander = CECreateExpander(pExGroup, "Lore", "Lore");					// 4
	pDoc->pStoreExpander = CECreateExpander(pExGroup, "Stores", "Stores");				// 5
	pDoc->pStoreCollectionExpander = CECreateExpander(pExGroup, "Store Collections", "Store Collections");				// 6
	pDoc->pMissionSearchExpander = CECreateExpander(pExGroup, "Mission Search", "MissionSearch");	// 7
	pDoc->pEndDialogAudioListExpander = CECreateExpander(pExGroup, "End Dialog Audio List", "End Dialog Audio List");				// 8
	pDoc->pItemAssignmentExpander = CECreateExpander(pExGroup, "Item Assignments", "Item Assignments"); // 9
	pDoc->pAuctionBrokerExpander = CECreateExpander(pExGroup, "Auction Broker Options", "Auction Broker Options"); // 10
	pDoc->pUGCSearchAgentExpander = CECreateExpander(pExGroup, "UGC Search Agents", "UGC Search Agents"); // 11
	pDoc->pImageMenuExpander = CECreateExpander(pExGroup, "Image Menu Options", "Image Menu Options"); // 12

	// Create the static expander contents
	CEInitDialogExpander(pDoc);

	// Refresh the dynamic expanders
	// The order of refresh is important and must be in sync with other refreshes and expander show/hide behaviors
	CERefreshInfoExpander(pDoc);
	CERefreshDialogExpander(pDoc);
	CERefreshSpecialExpander(pDoc);
	CERefreshOfferExpander(pDoc);
	CERefreshLoreDialogExpander(pDoc);
	CERefreshStoresExpander(pDoc);
	CERefreshStoreCollectionsExpander(pDoc);
	CERefreshEndDialogAudioListExpander(pDoc);
	CERefreshMissionSearchExpander(pDoc);
	CERefreshItemAssignmentExpander(pDoc);
	CERefreshAuctionBrokersExpander(pDoc);
	CERefreshUGCSearchAgentExpander(pDoc);
	CERefreshImageMenuExpander(pDoc);

	return pWin;
}


static void CEInitDisplay(EMEditor *pEditor, ContactEditDoc *pDoc)
{
	// Create the window (ignore field change callbacks during init)
	pDoc->bIgnoreFieldChanges = true;
	pDoc->bIgnoreFilenameChanges = true;
	pDoc->pMainWindow = CEInitMainWindow(pDoc);
	pDoc->bIgnoreFieldChanges = false;
	pDoc->bIgnoreFilenameChanges = false;

	// Editor Manager needs to be told about the windows used
	ui_WindowPresent(pDoc->pMainWindow);
	pDoc->emDoc.primary_ui_window = pDoc->pMainWindow;
	eaPush(&pDoc->emDoc.ui_windows, pDoc->pMainWindow);
}


static void CEInitToolbarsAndMenus(EMEditor *pEditor)
{
	EMToolbar *pToolbar;

	// Toolbar
	pToolbar = emToolbarCreateFileToolbar(EM_FILE_TOOLBAR_NEW|EM_FILE_TOOLBAR_OPEN|EM_FILE_TOOLBAR_SAVE);
	eaPush(&pEditor->toolbars, pToolbar);
	eaPush(&pEditor->toolbars, emToolbarCreateWindowToolbar());

	// File menu
	emMenuItemCreate(pEditor, "ce_revertcontact", "Revert", NULL, NULL, "CE_RevertContact");
	emMenuRegister(pEditor, emMenuCreate(pEditor, "File", "ce_revertcontact", NULL));
}


//---------------------------------------------------------------------------------------------------
// Public Interface
//---------------------------------------------------------------------------------------------------

void CEInitData(EMEditor *pEditor)
{
	if (!gInitializedEditor) {
		CEInitToolbarsAndMenus(pEditor);

		// Have Editor Manager handle a lot of change tracking
		emAutoHandleDictionaryStateChange(pEditor, "Contact", true, NULL, NULL, NULL, NULL, NULL);

		// Register dictionary change listeners
		resDictRegisterEventCallback(g_ContactDictionary, CEContactDictChanged, NULL);
		//resDictRegisterEventCallback(g_MessageDictionary, CEMessageDictChanged, NULL);

		resGetUniqueScopes(g_ContactDictionary, &geaScopes);

		gBoldExpanderSkin = ui_SkinCreate(NULL);
		SET_HANDLE_FROM_STRING(g_ui_FontDict, "Default_Bold", gBoldExpanderSkin->hNormal);

		// Initialize the contact editor look
		InitializeContactEditorLook();

		// Request all animlists from the server
		resRequestAllResourcesInDictionary(g_AnimListDict);

		// Request all guild stat defs from the server
		resRequestAllResourcesInDictionary("GuildStatDef");

		// Request all cut-scenes
		resRequestAllResourcesInDictionary("Cutscene");

		gInitializedEditor = true;
	}
}


static void CEContactPostOpenFixup(ContactDef *pContact)
{
	int i;

	// Fill in one dialog on each dialog block array
	if (eaSize(&pContact->greetingDialog) == 0) {
		eaPush(&pContact->greetingDialog, StructCreate(parse_DialogBlock));
	}
	if (eaSize(&pContact->defaultDialog) == 0) {
		eaPush(&pContact->defaultDialog, StructCreate(parse_DialogBlock));
	}
	if (eaSize(&pContact->infoDialog) == 0) {
		eaPush(&pContact->infoDialog, StructCreate(parse_DialogBlock));
	}
	if (eaSize(&pContact->missionListDialog) == 0) {
		eaPush(&pContact->missionListDialog, StructCreate(parse_DialogBlock));
	}
	if (eaSize(&pContact->noMissionsDialog) == 0) {
		eaPush(&pContact->noMissionsDialog, StructCreate(parse_DialogBlock));
	}
	if (eaSize(&pContact->exitDialog) == 0) {
		eaPush(&pContact->exitDialog, StructCreate(parse_DialogBlock));
	}
	if (eaSize(&pContact->missionExitDialog) == 0) {
		eaPush(&pContact->missionExitDialog, StructCreate(parse_DialogBlock));
	}
	if (eaSize(&pContact->generalCallout) == 0) {
		eaPush(&pContact->generalCallout, StructCreate(parse_DialogBlock));
	}
	if (eaSize(&pContact->missionCallout) == 0) {
		eaPush(&pContact->missionCallout, StructCreate(parse_DialogBlock));
	}
	if (eaSize(&pContact->eaMissionSearchDialog) == 0) {
		eaPush(&pContact->eaMissionSearchDialog, StructCreate(parse_DialogBlock));
	}
	if (eaSize(&pContact->noStoreItemsDialog) == 0) {
		eaPush(&pContact->noStoreItemsDialog, StructCreate(parse_DialogBlock));
	}

	for(i=eaSize(&pContact->offerList)-1; i>=0; --i) {
		ContactMissionOffer *pOffer = pContact->offerList[i];
		if (eaSize(&pOffer->greetingDialog) == 0) {
			eaPush(&pOffer->greetingDialog, StructCreate(parse_DialogBlock));
		}
		if (eaSize(&pOffer->offerDialog) == 0) {
			eaPush(&pOffer->offerDialog, StructCreate(parse_DialogBlock));
		}
		if (eaSize(&pOffer->inProgressDialog) == 0) {
			eaPush(&pOffer->inProgressDialog, StructCreate(parse_DialogBlock));
		}
		if (eaSize(&pOffer->completedDialog) == 0) {
			eaPush(&pOffer->completedDialog, StructCreate(parse_DialogBlock));
		}
		if (eaSize(&pOffer->failureDialog) == 0) {
			eaPush(&pOffer->failureDialog, StructCreate(parse_DialogBlock));
		}
	}

	if(pContact->costumePrefs.eCostumeType == ContactCostumeType_Default) {
		if(IS_HANDLE_ACTIVE(pContact->costumePrefs.costumeOverride)) {
			pContact->costumePrefs.eCostumeType = ContactCostumeType_Specified;
		} else if(IS_HANDLE_ACTIVE(pContact->costumePrefs.hPetOverride)) {
			pContact->costumePrefs.eCostumeType = ContactCostumeType_PetContactList;
		}
	}

	// Convert old special dialog block expressions
	for(i=eaSize(&pContact->specialDialog)-1; i>=0; --i)
	{
		SpecialDialogBlock *pSpecialDialog = pContact->specialDialog[i];
		if(!pSpecialDialog->bUsesLocalCondExpression)
		{
			pSpecialDialog->bUsesLocalCondExpression = true;
			if(pSpecialDialog->dialogBlock && eaSize(&pSpecialDialog->dialogBlock) > 0)
			{
				pSpecialDialog->pCondition = exprClone(pSpecialDialog->dialogBlock[0]->condition);
				exprDestroy(pSpecialDialog->dialogBlock[0]->condition);
				pSpecialDialog->dialogBlock[0]->condition = NULL;
			}
		}
	}

	// Make editor copy
	langMakeEditorCopy(parse_ContactDef, pContact, true);

	// Fix up editor copies of messages
	CEFixupMessages(pContact);

	// Fix up expressions
	CEFixupExpressions(pContact);
}


static void CEContactPreSaveFixup(ContactDef *pContact)
{
	int i;

	StructFreeStringSafe(&pContact->genesisZonemap);

	if (pContact->eSourceType == ContactSourceType_None)
	{
		pContact->pchSourceName = NULL;
	}
	else if (pContact->pchSourceName == NULL || pContact->pchSourceName[0] == '\0')
	{
		pContact->eSourceType = ContactSourceType_None;
	}

	if (pContact->eSourceType == ContactSourceType_Encounter &&
		(pContact->pchSourceSecondaryName == NULL || pContact->pchSourceSecondaryName[0] == '\0'))
	{
		pContact->eSourceType = ContactSourceType_None;
	}

	// Fix up special dialog blocks
	FOR_EACH_IN_EARRAY_FORWARDS(pContact->specialDialog, SpecialDialogBlock, pSpecialDialogBlock)
	{
		if (pSpecialDialogBlock->eSourceType == ContactSourceType_None)
		{
			pSpecialDialogBlock->pchSourceName = NULL;
		}
		else if (pSpecialDialogBlock->pchSourceName == NULL || pSpecialDialogBlock->pchSourceName[0] == '\0')
		{
			pSpecialDialogBlock->eSourceType = ContactSourceType_None;
		}

		if (pSpecialDialogBlock->eSourceType == ContactSourceType_Encounter &&
			(pSpecialDialogBlock->pchSourceSecondaryName == NULL || pSpecialDialogBlock->pchSourceSecondaryName[0] == '\0'))
		{
			pSpecialDialogBlock->eSourceType = ContactSourceType_None;
		}

		if(pSpecialDialogBlock->costumePrefs.eCostumeType != ContactCostumeType_Specified) {
			if(IS_HANDLE_ACTIVE(pSpecialDialogBlock->costumePrefs.costumeOverride))
				REMOVE_HANDLE(pSpecialDialogBlock->costumePrefs.costumeOverride);
		}

		if(pSpecialDialogBlock->costumePrefs.eCostumeType != ContactCostumeType_PetContactList) {
			if(IS_HANDLE_ACTIVE(pSpecialDialogBlock->costumePrefs.hPetOverride))
				REMOVE_HANDLE(pSpecialDialogBlock->costumePrefs.hPetOverride);
		}

		if(pSpecialDialogBlock->costumePrefs.eCostumeType != ContactCostumeType_CritterGroup) {
			if(IS_HANDLE_ACTIVE(pSpecialDialogBlock->costumePrefs.hCostumeCritterGroup))
				REMOVE_HANDLE(pSpecialDialogBlock->costumePrefs.hCostumeCritterGroup);
			pSpecialDialogBlock->costumePrefs.pchCostumeIdentifier = NULL;
			pSpecialDialogBlock->costumePrefs.pchCostumeMapVar = NULL;
			pSpecialDialogBlock->costumePrefs.eCostumeCritterGroupType = 0;
		} else if(pSpecialDialogBlock->costumePrefs.eCostumeCritterGroupType == ContactMapVarOverrideType_Specified) {
			pSpecialDialogBlock->costumePrefs.pchCostumeMapVar = NULL;
		} else {
			if(IS_HANDLE_ACTIVE(pSpecialDialogBlock->costumePrefs.hCostumeCritterGroup))
				REMOVE_HANDLE(pSpecialDialogBlock->costumePrefs.hCostumeCritterGroup);
		}
	}
	FOR_EACH_END

	// Remove empty dialog blocks
	CEStripEmptyDialogBlocks(&pContact->greetingDialog);
	CEStripEmptyDialogBlocks(&pContact->defaultDialog);
	CEStripEmptyDialogBlocks(&pContact->infoDialog);
	CEStripEmptyDialogBlocks(&pContact->missionListDialog);
	CEStripEmptyDialogBlocks(&pContact->noMissionsDialog);
	CEStripEmptyDialogBlocks(&pContact->exitDialog);
	CEStripEmptyDialogBlocks(&pContact->missionExitDialog);
	CEStripEmptyDialogBlocks(&pContact->generalCallout);
	CEStripEmptyDialogBlocks(&pContact->missionCallout);
	CEStripEmptyDialogBlocks(&pContact->eaMissionSearchDialog);
	CEStripEmptyDialogBlocks(&pContact->noStoreItemsDialog);

	// Remove empty end dialog audios
	CEStripEmptyEndDialogAudios(&pContact->eaEndDialogAudios);

	for(i=eaSize(&pContact->offerList)-1; i>=0; --i) {
		ContactMissionOffer *pOffer = pContact->offerList[i];

		CEStripEmptyDialogBlocks(&pOffer->greetingDialog);
		CEStripEmptyDialogBlocks(&pOffer->offerDialog);
		CEStripEmptyDialogBlocks(&pOffer->inProgressDialog);
		CEStripEmptyDialogBlocks(&pOffer->completedDialog);
		CEStripEmptyDialogBlocks(&pOffer->failureDialog);
	}

	// If single dialog type, remove excess data
	if (pContact->type == ContactType_SingleDialog) {
		// Clear stores list
		for(i=eaSize(&pContact->stores)-1; i>=0; --i) {
			StructDestroy(parse_StoreRef, pContact->stores[i]);
		}
		eaDestroy(&pContact->stores);

		// Clear unusable dialog blocks
		CEDestroyDialogBlocks(&pContact->greetingDialog);
		CEDestroyDialogBlocks(&pContact->defaultDialog);
		CEDestroyDialogBlocks(&pContact->infoDialog);
		CEDestroyDialogBlocks(&pContact->missionListDialog);
		CEDestroyDialogBlocks(&pContact->noMissionsDialog);
		CEDestroyDialogBlocks(&pContact->exitDialog);
		CEDestroyDialogBlocks(&pContact->missionExitDialog);
		CEDestroyDialogBlocks(&pContact->generalCallout);
		CEDestroyDialogBlocks(&pContact->missionCallout);
		CEDestroyDialogBlocks(&pContact->eaMissionSearchDialog);

		// Reset flags
		pContact->eContactFlags = 0;
	}

	// If this is NOT a Mission Search contact, strip excess data
	if (!contact_IsMissionSearch(pContact)){
		StructReset(parse_Message, pContact->missionSearchStringMsg.pEditorCopy);
		CEDestroyDialogBlocks(&pContact->eaMissionSearchDialog);
	}

	if (!contact_IsItemAssignmentGiver(pContact)) {
		StructDestroySafe(parse_ContactItemAssignmentData, &pContact->pItemAssignmentData);
	}

	if(pContact->costumePrefs.eCostumeType != ContactCostumeType_Specified) {
		if(IS_HANDLE_ACTIVE(pContact->costumePrefs.costumeOverride))
			REMOVE_HANDLE(pContact->costumePrefs.costumeOverride);
	}

	if(pContact->costumePrefs.eCostumeType != ContactCostumeType_PetContactList) {
		if(IS_HANDLE_ACTIVE(pContact->costumePrefs.hPetOverride))
			REMOVE_HANDLE(pContact->costumePrefs.hPetOverride);
	}

	if(pContact->costumePrefs.eCostumeType != ContactCostumeType_CritterGroup) {
		if(IS_HANDLE_ACTIVE(pContact->costumePrefs.hCostumeCritterGroup))
			REMOVE_HANDLE(pContact->costumePrefs.hCostumeCritterGroup);
		pContact->costumePrefs.pchCostumeIdentifier = NULL;
		pContact->costumePrefs.pchCostumeMapVar = NULL;
		pContact->costumePrefs.eCostumeCritterGroupType = 0;
	} else if(pContact->costumePrefs.eCostumeCritterGroupType == ContactMapVarOverrideType_Specified) {
		pContact->costumePrefs.pchCostumeMapVar = NULL;
	} else {
		if(IS_HANDLE_ACTIVE(pContact->costumePrefs.hCostumeCritterGroup))
			REMOVE_HANDLE(pContact->costumePrefs.hCostumeCritterGroup);
	}

	if (pContact->eSourceType == ContactSourceType_None)
	{
		if (pContact->pchSourceName)
		{
			pContact->pchSourceName = NULL;
		}

		if (pContact->pchSourceSecondaryName)
		{
			pContact->pchSourceSecondaryName = NULL;
		}
	}
	else if (pContact->eSourceType != ContactSourceType_Encounter)
	{
		if (pContact->pchSourceSecondaryName)
		{
			pContact->pchSourceName = NULL;
		}
	}

	if( !contact_IsImageMenu(pContact)) {
		StructDestroySafe( parse_ContactImageMenuData, &pContact->pImageMenuData );
	}

	// Fix up editor copies of messages
	CEFixupMessages(pContact);
}


static ContactEditDoc *CEInitDoc(ContactDef *pContact, bool bCreated)
{
	ContactEditDoc *pDoc;
	char nameBuf[260];

	// Initialize the structure
	pDoc = (ContactEditDoc*)calloc(1,sizeof(ContactEditDoc));

	// Fill in the contact data
	if (bCreated) {
		pDoc->pContact = StructCreate(parse_ContactDef);
		assert(pDoc->pContact);
		emMakeUniqueDocName(&pDoc->emDoc, "New_Contact", "contact", "Contact");
		pDoc->pContact->name = (char*)allocAddString(pDoc->emDoc.doc_name);
		sprintf(nameBuf, "defs/contacts/%s.contact", pDoc->pContact->name);
		pDoc->pContact->filename = allocAddString(nameBuf);
		CEContactPostOpenFixup(pDoc->pContact);
	} else {
		pDoc->pContact = StructClone(parse_ContactDef, pContact);
		pDoc->pOrigContactUntouched = StructClone(parse_ContactDef, pContact);
		assert(pDoc->pContact);
		CEContactPostOpenFixup(pDoc->pContact);
		pDoc->pOrigContact = StructClone(parse_ContactDef, pDoc->pContact);
	}

	// Set up the undo stack
	pDoc->emDoc.edit_undo_stack = EditUndoStackCreate();
	EditUndoSetContext(pDoc->emDoc.edit_undo_stack, pDoc);
	pDoc->pNextUndoContact = StructClone(parse_ContactDef, pDoc->pContact);

	return pDoc;
}


ContactEditDoc *CEOpenContact(EMEditor *pEditor, char *pcName)
{
	ContactEditDoc *pDoc = NULL;
	ContactDef *pContact = NULL;
	bool bCreated = false;

	if (pcName && resIsEditingVersionAvailable(g_ContactDictionary, pcName)) {
		// Simply open the object since it is in the dictionary
		pContact = RefSystem_ReferentFromString(g_ContactDictionary, pcName);
	} else if (pcName) {
		// Wait for object to show up so we can open it
		resSetDictionaryEditMode(g_ContactDictionary, true);
		resSetDictionaryEditMode(gMessageDict, true);
		resSetDictionaryEditMode("PetContactList", true);
		emSetResourceState(pEditor, pcName, EMRES_STATE_OPENING);
		resRequestOpenResource(g_ContactDictionary, pcName);
	} else {
		// Create a new object since it is not in the dictionary
		bCreated = true;
	}

	if (pContact && pContact->genesisZonemap) {
		ZoneMapInfo* zmapInfo = zmapInfoGetByPublicName(pContact->genesisZonemap);
		if (zmapInfo && zmapInfoHasGenesisData(zmapInfo) && !stricmp(getUserName(), "jfinder")) {
			Alertf( "Editing is not allowed on unfrozen Contacts.");
			return NULL;
		}
	}

	if (pContact || bCreated) {
		pDoc = CEInitDoc(pContact, bCreated);
		CEInitDisplay(pEditor, pDoc);
		resFixFilename(g_ContactDictionary, pDoc->pContact->name, pDoc->pContact);
	}

	return pDoc;
}


void CERevertContact(ContactEditDoc *pDoc)
{
	ContactDef *pContact;

	if (!pDoc->emDoc.orig_doc_name[0]) {
		// Cannot revert if no original
		return;
	}

	pContact = RefSystem_ReferentFromString(g_ContactDictionary, pDoc->emDoc.orig_doc_name);
	if (pContact) {
		// Revert the contact
		StructDestroy(parse_ContactDef, pDoc->pContact);
		StructDestroy(parse_ContactDef, pDoc->pOrigContact);
		StructDestroy(parse_ContactDef, pDoc->pOrigContactUntouched);
		pDoc->pContact = StructClone(parse_ContactDef, pContact);
		pDoc->pOrigContactUntouched = StructClone(parse_ContactDef, pContact);
		CEContactPostOpenFixup(pDoc->pContact);
		pDoc->pOrigContact = StructClone(parse_ContactDef, pDoc->pContact);

		// Clear the undo stack on revert
		EditUndoStackClear(pDoc->emDoc.edit_undo_stack);
		StructDestroy(parse_ContactDef, pDoc->pNextUndoContact);
		pDoc->pNextUndoContact = StructClone(parse_ContactDef, pDoc->pContact);

		// Refresh the UI
		pDoc->bIgnoreFieldChanges = true;
		pDoc->bIgnoreFilenameChanges = true;
		CEUpdateDisplay(pDoc);
		pDoc->bIgnoreFieldChanges = false;
		pDoc->bIgnoreFilenameChanges = false;
	} 
}

void CEFreeDialogFlowWindow(DialogFlowWindowInfo *pInfo)
{
	if (pInfo)
	{	
		// Destroy all dialog nodes
		eaDestroyEx(&pInfo->eaDialogNodes, CEDestroyDialogNode);
		eaDestroy(&pInfo->eaSelectedDialogNodes);

		// Close the dialog flow window and free it
		if (pInfo->pDialogFlowWin)
		{
			ui_WindowHide(pInfo->pDialogFlowWin);
			ui_WidgetQueueFree(UI_WIDGET(pInfo->pDialogFlowWin));
		}

		// Free self
		free(pInfo);
	}

}


void CECloseContact(ContactEditDoc *pDoc)
{
	int i;

	// Free standalone fields
	MEFieldSafeDestroy(&pDoc->pDisplayNameField);
	MEFieldSafeDestroy(&pDoc->pInfoTextField);
	MEFieldSafeDestroy(&pDoc->pContactFlagsField);
	MEFieldSafeDestroy(&pDoc->pSkillTrainerTypeField);
	MEFieldSafeDestroy(&pDoc->pMinigameTypeField);
	MEFieldSafeDestroy(&pDoc->pCharacterClassField);
	MEFieldSafeDestroy(&pDoc->pShowLastPuppetField);
	MEFieldSafeDestroy(&pDoc->pPetContactField);
	MEFieldSafeDestroy(&pDoc->pCostumeField);
	MEFieldSafeDestroy(&pDoc->pCostumeTypeField);
	MEFieldSafeDestroy(&pDoc->pCritterGroupTypeField);
	MEFieldSafeDestroy(&pDoc->pCritterGroupField);
	MEFieldSafeDestroy(&pDoc->pCritterMapVarField);
	MEFieldSafeDestroy(&pDoc->pCritterGroupIdentifierField);
	MEFieldSafeDestroy(&pDoc->pBuyOptionField);
	MEFieldSafeDestroy(&pDoc->pSellOptionField);
	MEFieldSafeDestroy(&pDoc->pBuyBackOptionField);
	MEFieldSafeDestroy(&pDoc->pDialogExitTextOverrideField);
	MEFieldSafeDestroy(&pDoc->pSearchMsgField);
	MEFieldSafeDestroy(&pDoc->pOptionalActionCategoryField);
	MEFieldSafeDestroy(&pDoc->pResearchStoreCollectionField);
	MEFieldSafeDestroy(&pDoc->pHideFromRemoteContactListField);
	MEFieldSafeDestroy(&pDoc->pUpdateOptionsField);
	MEFieldSafeDestroy(&pDoc->pItemAssignmentRefreshTimeField);
	MEFieldSafeDestroy(&pDoc->pItemAssignmentRarityCountField);
	MEFieldSafeDestroy(&pDoc->pImageMenuBGImageField);
	MEFieldSafeDestroy(&pDoc->pImageMenuTitleField);

	// Don't free these since they are in eaDocFields and are freed later
	//   pMapNameField
	//   pAnimListField
	//   pSourceSecondaryNameField
	//   pSourceNameField
	//   pSourceTypeField
	//   pCutSceneField
	//   pHeadshotStyleField
	//   pCanAccessRemotelyField
	//   pInteractCondField
	//	 pContactIndicatorOverrideField

	// Free the labels
	ui_WidgetQueueFreeAndNull(&pDoc->pFilenameLabel);
	ui_WidgetQueueFreeAndNull(&pDoc->pCostumeLabel);
	ui_WidgetQueueFreeAndNull(&pDoc->pCostumeTypeLabel);
	ui_WidgetQueueFreeAndNull(&pDoc->pCritterGroupTypeLabel);
	ui_WidgetQueueFreeAndNull(&pDoc->pCritterGroupIdentifierLabel);
	ui_WidgetQueueFreeAndNull(&pDoc->pHeadshotLabel);
	ui_WidgetQueueFreeAndNull(&pDoc->pCutSceneLabel);
	ui_WidgetQueueFreeAndNull(&pDoc->pSourceTypeLabel);
	ui_WidgetQueueFreeAndNull(&pDoc->pSourceNameLabel);
	ui_WidgetQueueFreeAndNull(&pDoc->pSourceSecondaryNameLabel);
	ui_WidgetQueueFreeAndNull(&pDoc->pAnimListLabel);
	ui_WidgetQueueFreeAndNull(&pDoc->pMapLabel);
	ui_WidgetQueueFreeAndNull(&pDoc->pContactIndicatorOverrideLabel);
	ui_WidgetQueueFreeAndNull(&pDoc->pContactFlagsLabel);
	ui_WidgetQueueFreeAndNull(&pDoc->pSkillTrainerTypeLabel);
	ui_WidgetQueueFreeAndNull(&pDoc->pMinigameTypeLabel);
	ui_WidgetQueueFreeAndNull(&pDoc->pCharacterClassLabel);
	ui_WidgetQueueFreeAndNull(&pDoc->pShowLastPuppetLabel);
	ui_WidgetQueueFreeAndNull(&pDoc->pBuyOptionLabel);
	ui_WidgetQueueFreeAndNull(&pDoc->pSellOptionLabel);
	ui_WidgetQueueFreeAndNull(&pDoc->pBuyBackOptionLabel);
	ui_WidgetQueueFreeAndNull(&pDoc->pDialogExitTextOverrideLabel);
	ui_WidgetQueueFreeAndNull(&pDoc->pResearchStoreCollectionLabel);
	ui_WidgetQueueFreeAndNull(&pDoc->pHideFromRemoteContactListLabel);
	ui_WidgetQueueFreeAndNull(&pDoc->pUpdateOptionsLabel);
	ui_WidgetQueueFreeAndNull(&pDoc->pImageMenuBGImageLabel);
	ui_WidgetQueueFreeAndNull(&pDoc->pImageMenuTitleLabel);

	// Free the map vars
	eaDestroy(&pDoc->eaVarNames);

	// Free doc fields
	eaDestroyEx(&pDoc->eaDocFields, MEFieldDestroy);

	// Free dialog groups
	eaDestroyEx(&pDoc->eaDialogGroups, CEFreeDialogGroup);
	eaDestroyEx(&pDoc->eaMissionSearchDialogGroups, CEFreeDialogGroup);
	eaDestroyEx(&pDoc->eaNoStoreDialogGroups, CEFreeDialogGroup);

	// Free special groups
	eaDestroyEx(&pDoc->eaSpecialGroups, CEFreeSpecialDialogGroup);

	//Free special action block
	eaDestroyEx(&pDoc->eaSpecialActionBlockGroups, CEFreeSpecialActionBlockGroup);

	// Free special dialog override groups
	eaDestroyEx(&pDoc->eaSpecialOverrideGroups, CEFreeSpecialDialogOverrideGroup);

	// Free Lore Dialog groups
	eaDestroyEx(&pDoc->eaLoreDialogGroups, CEFreeLoreDialogGroup);

	// Free store groups
	eaDestroyEx(&pDoc->eaStoreGroups, CEFreeStoreGroup);
	eaDestroyEx(&pDoc->eaStoreCollections, CEFreeStoreCollectionGroup);

	// Free auction brokers
	eaDestroyEx(&pDoc->eaAuctionBrokers, CEFreeAuctionBrokerContactDataGroup);

	// Free UGC Search Agents
	eaDestroyEx(&pDoc->eaUGCSearchAgents, CEFreeUGCSearchAgentDataGroup);

	// Free Image Menu
	eaDestroyEx(&pDoc->eaImageMenuItemGroups, CEFreeImageMenuItemGroup);

	// Free offer groups
	eaDestroyEx(&pDoc->eaOfferGroups, CEFreeOfferGroup);

	// Free end dialog audio groups
	eaDestroyEx(&pDoc->eaEndDialogAudioGroups, CEFreeEndDialogAudioGroup);

	for(i = eaSize(&pDoc->eaPowerStoreGroups)-1; i >= 0; --i)
	{
		assert(pDoc->eaPowerStoreGroups);
		CEFreePowerStoreGroup(pDoc->eaPowerStoreGroups[i]);
		eaRemove(&pDoc->eaPowerStoreGroups, i);
	}
	// Free the objects
	StructDestroy(parse_ContactDef, pDoc->pContact);
	if (pDoc->pOrigContactUntouched)
	{
		StructDestroy(parse_ContactDef, pDoc->pOrigContactUntouched);
	}
	if (pDoc->pOrigContact) {
		StructDestroy(parse_ContactDef, pDoc->pOrigContact);
	}

	// Close the window
	ui_WindowHide(pDoc->emDoc.primary_ui_window);
	ui_WidgetQueueFree(UI_WIDGET(pDoc->emDoc.primary_ui_window));

	CEFreeDialogFlowWindow(pDoc->pDialogFlowWindowInfo);
}

EMTaskStatus CESaveContact(ContactEditDoc* pDoc, bool bSaveAsNew)
{
	EMTaskStatus status;
	const char *pcName;
	ContactDef *pContactCopy;

	ui_SetFocus(NULL);

	// Deal with state changes
	pcName = pDoc->pContact->name;
	if (emHandleSaveResourceState(pDoc->emDoc.editor, pcName, &status)) {
		return status;
	}

	// Do cleanup before validation
	pContactCopy = StructClone(parse_ContactDef, pDoc->pContact);
	CEContactPreSaveFixup(pContactCopy);

	// Perform validation
	if (!contact_Validate(pContactCopy)) {
		StructDestroy(parse_ContactDef, pContactCopy);
		return EM_TASK_FAILED;
	}

	// Do the save (which will free the copy)
	status = emSmartSaveDoc(&pDoc->emDoc, pContactCopy, pDoc->pOrigContact, bSaveAsNew);

	return status;
}

#endif
