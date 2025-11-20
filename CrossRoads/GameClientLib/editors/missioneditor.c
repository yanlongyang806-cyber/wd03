/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
 
#ifndef NO_EDITORS

#include "missioneditor.h"

#include "ActivityCommon.h"
#include "Color.h"
#include "EditorManagerUIPickers.h"
#include "EditorPrefs.h"
#include "Entity.h"
#include "EventEditor.h"
#include "Expression.h"
#include "GameActionEditor.h"
#include "InteractionEditor.h"
#include "MultiEditField.h"
#include "RewardTableEditor.h"
#include "StringCache.h"
#include "StringUtil.h"
#include "UIGimmeButton.h"
#include "UIScrollbar.h"
#include "allegiance.h"
#include "contacteditor.h"
#include "dynFxInfo.h"
#include "entCritter.h"
#include "gameaction_common.h"
#include "gameeditorshared.h"
#include "gameevent.h"
#include "mission_common.h"
#include "oldencounter_common.h"
#include "rewardCommon.h"
#include "soundLib.h"
#include "sysutil.h"
#include "wlEncounter.h"
#include "worldgrid.h"

#include "AutoGen/Entity_h_ast.h"
#include "AutoGen/allegiance_h_ast.h"
#include "AutoGen/gameevent_h_ast.h"
#include "AutoGen/mission_common_h_ast.h"
#include "AutoGen/mission_enums_h_ast.h"

#include "AutoGen/missioneditor_h_ast.c"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););


//-----------------------------------------------------------------------------------
// Global Data
//-----------------------------------------------------------------------------------

static bool gInitializedEditor = false;
static bool gIndexChanged = false;

static MissionDef *gMissionClipboard = NULL;
static MissionDef **gOpenMissions = NULL;

static char **geaScopes = NULL;
static char **geaWhenOptions = NULL;
static char **s_eaClassNames = NULL;
static char **s_eaSpawnNames = NULL;



static bool gShowLines = true;

extern EMEditor s_MissionEditor;

static UISkin *gBoldExpanderSkin;
static UISkin *gMainWindowSkin;

extern const char** g_GEMapDispNames;


//---------------------------------------------------------------------------------------------------
// Function Prototypes and type definitions
//---------------------------------------------------------------------------------------------------

#define X_OFFSET_BASE     15
#define X_OFFSET_BASE2    35
#define X_OFFSET_INDENT   50
#define X_OFFSET_CONTROL  115
#define X_OFFSET_CONTROL2 150
#define X_OFFSET_CONTROL3 180
#define X_OFFSET_SPACING  80
#define Y_OFFSET_SEPARATOR 8
#define Y_OFFSET_ROW      26
#define Y_OFFSET_SPACING  80
#define CONTACT_GROUP_EXPANDER_GROUP_Y_OFFSET 38.f

#define MDE_DEFAULT_ZOOM		1.0
#define MDE_SCROLLAREA_WIDTH	4000

#define MDE_UNASSIGNED_CONTACT_GROUP_NAME "[Unassigned special dialogs and mission offers]"

static void MDEMissionChanged(MissionEditDoc *pDoc, bool bUndoable);
static void MDEFieldChangedCB(MEField *pField, bool bFinished, MissionEditDoc *pDoc);
static bool MDEFieldPreChangeCB(MEField *pField, bool bFinished, MissionEditDoc *pDoc);
static MDEMissionGroup *MDEInitMissionGroup(MissionEditDoc *pDoc, MissionDef *pMission, MissionDef *pOrigMission, bool bIsRoot, int index);
static void MDELayoutAddChildNodes(MissionEditDoc *pDoc, MDELayoutNode ***peaAllNodes, MDEMissionGroup ***peaGroups, MDELayoutNode *pCurrentNode, int iDepth, bool bIsRoot);
static void MDEInitMissionGroupDisplay(MDEMissionGroup *pGroup, bool bIsRoot);

static void MDEFreeInteractableOverrideGroup(MDEInteractableOverrideGroup *pGroup);
static void MDEFreeScoreEventGroup(MDEScoreEventGroup *pGroup);
static void MDEFreeEventGroup(MDEEventGroup *pGroup);
static void MDEFreeRequestGroup(MDERequestGroup *pGroup);
static void MDEFreeWaypointGroup(MDEWaypointGroup *pGroup);

static void MDEMakeUniqueEventName(GameEvent*** peaEvents, GameEvent *pEvent);

static void MDEUpdateVariablesExpander(MDEMissionGroup *pGroup, bool bIsRoot);
static void MDEAddMissionOfferOverrideCB(UIButton *pButton, MDEContactGroup *pGroup);
static void MDEAddActionBlockOverrideCB(UIButton *pButton, MDEContactGroup *pGroup);
static void MDEAddImageMenuItemOverrideCB(UIButton *pButton, MDEContactGroup *pGroup);
static F32 MDEFindLowYForNewEntry(MissionEditDoc *pDoc);
static void MDEFreeContactGroup(MDEContactGroup *pGroup);
static UIExpander *MDECreateExpander(UIExpanderGroup *pExGroup, const char *pcName, char *pcPrefName, bool bDefaultOpen);

#define WindowIsSelected(pWin) (eaGet(UI_WIDGET(pWin)->group, 0) == UI_WIDGET(pWin))


//---------------------------------------------------------------------------------------------------
// Data Manipulation
//---------------------------------------------------------------------------------------------------

static MissionEditDoc *MDEGetActiveDoc()
{
	EMEditorDoc *pDoc = emGetActiveEditorDoc();
	if (pDoc && stricmp(pDoc->doc_type, "mission")==0)
	{
		return (MissionEditDoc*)pDoc;
	}
	return NULL;
}

static ExprContext * MDEGetMissionExprContext(void)
{
	static ExprContext* s_pExprContext = NULL;

	if(s_pExprContext == NULL)
	{
		ExprFuncTable* s_pFuncTable = exprContextCreateFunctionTable();

		s_pExprContext = exprContextCreate();		
		
		exprContextAddFuncsToTableByTag(s_pFuncTable, "Mission");
		exprContextAddFuncsToTableByTag(s_pFuncTable, "util");
		exprContextAddFuncsToTableByTag(s_pFuncTable, "gameutil");
		exprContextAddFuncsToTableByTag(s_pFuncTable, "entity");
		exprContextAddFuncsToTableByTag(s_pFuncTable, "entityutil");
		exprContextAddFuncsToTableByTag(s_pFuncTable, "OpenMission");

		exprContextSetFuncTable(s_pExprContext, s_pFuncTable);

		exprContextSetPointerVarPooled(s_pExprContext, g_MissionVarName, NULL, parse_Mission, false, true);
		exprContextSetPointerVarPooled(s_pExprContext, g_PlayerVarName, NULL, parse_Entity, false, true);
	}

	return s_pExprContext;
}

static ExprContext * MDEGetMissionRequiresExprContext(void)
{
	static ExprContext* s_pExprContext = NULL;

	if(s_pExprContext == NULL)
	{
		ExprFuncTable* s_pFuncTable = exprContextCreateFunctionTable();

		s_pExprContext = exprContextCreate();		

		exprContextAddFuncsToTableByTag(s_pFuncTable, "util");
		exprContextAddFuncsToTableByTag(s_pFuncTable, "gameutil");
		exprContextAddFuncsToTableByTag(s_pFuncTable, "player");
		exprContextAddFuncsToTableByTag(s_pFuncTable, "PTECharacter");
		exprContextAddFuncsToTableByTag(s_pFuncTable, "entity");
		exprContextAddFuncsToTableByTag(s_pFuncTable, "entityutil");
		exprContextAddFuncsToTableByTag(s_pFuncTable, "encounter_action");

		exprContextSetFuncTable(s_pExprContext, s_pFuncTable);

		exprContextSetPointerVarPooled(s_pExprContext, g_MissionDefVarName, NULL, parse_MissionDef, false, true);
		exprContextSetPointerVarPooled(s_pExprContext, g_PlayerVarName, NULL, parse_Entity, false, true);
	}

	return s_pExprContext;
}

static void MDERefreshOpenMissionList()
{
	int i;
	DictionaryEArrayStruct* pMissionDefs = NULL;
	pMissionDefs = resDictGetEArrayStruct("MissionDef");

	if(!gOpenMissions){
		eaCreate(&gOpenMissions);
	} else {
		eaClear(&gOpenMissions);
	}

	for(i = eaSize(&pMissionDefs->ppReferents)-1; i >=0; i--) {
		MissionDef* pDef = pMissionDefs->ppReferents[i];
		if(pDef->missionType == MissionType_OpenMission) {
			eaPush(&gOpenMissions, pDef);
		}
	}
}

static void MDEIndexChangedCB(void *unused)
{
	// Update scopes list
	gIndexChanged = false;
	resGetUniqueScopes(g_MissionDictionary, &geaScopes);
}


static void MDEMissionDictChanged(enumResourceEventType eType, const char *pDictName, const char *pcName, Referent pReferent, void *pUserData)
{
	if ((eType == RESEVENT_INDEX_MODIFIED) && !gIndexChanged) {
		gIndexChanged = true;
		emQueueFunctionCall(MDEIndexChangedCB, NULL);
	}

	MDERefreshOpenMissionList();
}


static void MDEMissionUndoCB(MissionEditDoc *pDoc, MDEUndoData *pData)
{
	// Put the undo mission into the editor
	StructDestroy(parse_MissionDef, pDoc->pMission);
	pDoc->pMission = StructClone(parse_MissionDef, pData->pPreMission);
	if (pDoc->pNextUndoMission) {
		StructDestroy(parse_MissionDef, pDoc->pNextUndoMission);
	}
	pDoc->pNextUndoMission = StructClone(parse_MissionDef, pDoc->pMission);

	if (pDoc->pDialogFlowWindowInfo)
	{
		CERefreshDialogFlowWindow(pDoc->pDialogFlowWindowInfo);
	}

	// Update the UI
	MDEMissionChanged(pDoc, false);
}


static void MDEMissionRedoCB(MissionEditDoc *pDoc, MDEUndoData *pData)
{
	// Put the undo mission into the editor
	StructDestroy(parse_MissionDef, pDoc->pMission);
	pDoc->pMission = StructClone(parse_MissionDef, pData->pPostMission);
	if (pDoc->pNextUndoMission) {
		StructDestroy(parse_MissionDef, pDoc->pNextUndoMission);
	}
	pDoc->pNextUndoMission = StructClone(parse_MissionDef, pDoc->pMission);

	if (pDoc->pDialogFlowWindowInfo)
	{
		CERefreshDialogFlowWindow(pDoc->pDialogFlowWindowInfo);
	}

	// Update the UI
	MDEMissionChanged(pDoc, false);
}


static void MDEMissionUndoFreeCB(MissionEditDoc *pDoc, MDEUndoData *pData)
{
	// Free the memory
	StructDestroy(parse_MissionDef, pData->pPreMission);
	StructDestroy(parse_MissionDef, pData->pPostMission);
	free(pData);
}


static int MDECompareStrings(const char** left, const char** right)
{
	return stricmp(*left,*right);
}


static void MDEFixupTemplateVarMessages(MissionDef *pRootMission, MissionDef *pMission, char *pcScope, TemplateVariableGroup *pGroup)
{
	char buf1[1024];
	char buf2[1024];
	int i;
	char nameSpace[RESOURCE_NAME_MAX_SIZE];
	char baseObjectName[RESOURCE_NAME_MAX_SIZE];
	char baseMessageKey[RESOURCE_NAME_MAX_SIZE];

	if (resExtractNameSpace(pMission->pchRefString, nameSpace, baseObjectName))
	{
		sprintf(baseMessageKey, "%s:MissionDef.%s", nameSpace, baseObjectName);
	}
	else
	{
		sprintf(baseMessageKey, "MissionDef.%s", pMission->pchRefString);
	}

	for(i=eaSize(&pGroup->variables)-1; i>=0; --i) {
		TemplateVariable *pVar = pGroup->variables[i];
		if (pVar->varType == TemplateVariableType_Message) {
			const char *pcMessageKey = MultiValGetString(&pVar->varValue, NULL);
			if (pcMessageKey) {
				DisplayMessage *pDispMsg = langGetDisplayMessageFromList(&pMission->varMessageList, pcMessageKey, true);
				if (pDispMsg && pDispMsg->pEditorCopy) {
					sprintf(buf1, "%s.vars.%s.id%d", baseMessageKey, pVar->varName, pVar->id);
					sprintf(buf2, "This is the \"%s\" value for a MissionDef.", pVar->varName);
					langFixupMessage(pDispMsg->pEditorCopy, buf1, buf2, pcScope);
					MultiValSetString(&pVar->varValue, pDispMsg->pEditorCopy->pcMessageKey);
				}
			}
		}
	}

	for(i=eaSize(&pGroup->subGroups)-1; i>=0; --i) {
		MDEFixupTemplateVarMessages(pRootMission, pMission, pcScope, pGroup->subGroups[i]);
	}
}


static void MDEFixupMessages(MissionDef *pRootMission, MissionDef *pMission)
{
	char scope[260] = {0};
	char buf1[1024] = {0};
	char buf2[1024] = {0};
	char buf3[1024] = {0};
	char buf4[1024] = {0};
	char buf5[1024] = {0};
	int i;
	char nameSpace[RESOURCE_NAME_MAX_SIZE] = {0};
	char baseObjectName[RESOURCE_NAME_MAX_SIZE] = {0};
	char baseMessageKey[RESOURCE_NAME_MAX_SIZE] = {0};

	if( pRootMission == pMission )
	{
		if (resExtractNameSpace(pMission->pchRefString, nameSpace, baseObjectName))
		{
			sprintf(baseMessageKey, "%s:MissionDef.%s", nameSpace, baseObjectName);
		}
		else
		{
			sprintf(baseMessageKey, "MissionDef.%s", pMission->pchRefString);
		}
	}
	else
	{
		if (resExtractNameSpace(pRootMission->pchRefString, nameSpace, baseObjectName))
		{
			sprintf(baseMessageKey, "%s:MissionDef.%s::%s", nameSpace, baseObjectName, pMission->name);
		}
		else
		{
			sprintf(baseMessageKey, "MissionDef.%s::%s", pRootMission->pchRefString, pMission->name);
		}
	}

	if (pRootMission->scope) {
		sprintf(scope, "MissionDef/%s/%s", pRootMission->scope, pRootMission->name);
	} else {
		sprintf(scope, "MissionDef/%s", pRootMission->name);
	}

	sprintf(buf1, "%s.DisplayName", baseMessageKey);
	sprintf(buf2, "This is the display name text for %s.", pMission->name);
	
	langFixupMessageWithTerseKey(pMission->displayNameMsg.pEditorCopy,
		MKP_MISSIONNAME, buf1, buf2, scope);

	sprintf(buf1, "%s.UIString", baseMessageKey);
	sprintf(buf2, "This is the UI display text for %s.", pMission->name);
	langFixupMessageWithTerseKey(pMission->uiStringMsg.pEditorCopy, 
		MKP_MISSIONUISTR, buf1, buf2, scope);

	sprintf(buf1, "%s.DetailString", baseMessageKey);
	sprintf(buf2, "This is the detail text for %s.", pMission->name);
	langFixupMessageWithTerseKey(pMission->detailStringMsg.pEditorCopy,
		MKP_MISSIONDETAIL, buf1, buf2, scope);

	sprintf(buf1, "%s.Summary", baseMessageKey);
	sprintf(buf2, "This is the Mission Summary text for %s.", pMission->name);
	langFixupMessageWithTerseKey(pMission->summaryMsg.pEditorCopy, 
		MKP_MISSIONSUMMARY, buf1, buf2, scope);

	sprintf(buf1, "%s.FailureString", baseMessageKey);
	sprintf(buf2, "This is the Failure text for %s.", pMission->name);
	langFixupMessageWithTerseKey(pMission->failureMsg.pEditorCopy,
		MKP_MISSIONFAIL, buf1, buf2, scope);

	sprintf(buf1, "%s.FailReturnString", baseMessageKey);
	sprintf(buf2, "This is the Failure Return text for %s.", pMission->name);
	langFixupMessageWithTerseKey(pMission->failReturnMsg.pEditorCopy, 
		MKP_MISSIONFAILRETURN, buf1, buf2, scope);

	sprintf(buf1, "%s.ReturnString", baseMessageKey);
	sprintf(buf2, "This is Mission Return text for %s.", pMission->name);
	langFixupMessageWithTerseKey(pMission->msgReturnStringMsg.pEditorCopy,
		MKP_MISSIONRETURN, buf1, buf2, scope);

	sprintf(buf1, "%s.FXString", baseMessageKey);
	sprintf(buf2, "This is text that is used to display text words fx to the player for %s.", pMission->name);
	langFixupMessageWithTerseKey(pMission->splatDisplayMsg.pEditorCopy,
		MKP_MISSIONSFXTEXT, buf1, buf2, scope);

	sprintf(buf1, "%s.TeamUpString", baseMessageKey);
	sprintf(buf2, "This is text that will be used to display a team up name to the player for %s.", pMission->name);
	langFixupMessageWithTerseKey(pMission->TeamUpDisplayName.pEditorCopy,
		MKP_MISSIONTEAMUPTEXT, buf1, buf2, scope);

	// fix Template variables
	if (pMission->missionTemplate) {
		MDEFixupTemplateVarMessages(pRootMission, pMission, scope, pMission->missionTemplate->rootVarGroup);
	}

	// fix Action variables
	gameaction_FixupMessageList(&pMission->ppOnStartActions, scope, baseMessageKey, 0);
	gameaction_FixupMessageList(&pMission->ppSuccessActions, scope, baseMessageKey, eaSize(&pMission->ppOnStartActions));
	gameaction_FixupMessageList(&pMission->ppFailureActions, scope, baseMessageKey, eaSize(&pMission->ppOnStartActions) + eaSize(&pMission->ppSuccessActions));
	gameaction_FixupMessageList(&pMission->ppOnReturnActions, scope, baseMessageKey, eaSize(&pMission->ppOnStartActions) + eaSize(&pMission->ppSuccessActions) + eaSize(&pMission->ppFailureActions));

	// Fixup interactable override messages
	for(i=0; i<eaSize(&pMission->ppInteractableOverrides); i++) {
		InteractableOverride *pOverride = pMission->ppInteractableOverrides[i];
		if (pOverride->pcMapName) {
			sprintf(buf1, "%s.%s", baseMessageKey, pOverride->pcMapName);
		} else {
			sprintf(buf1, "%s.NO_MAP", baseMessageKey);
		}
		sprintf(buf2, "%s.%s", NULL_TO_EMPTY(pOverride->pcInteractableName), NULL_TO_EMPTY(pOverride->pcTypeTagName));
		interaction_FixupMessages(pOverride->pPropertyEntry, scope, buf1, buf2, i);
	}

	// Fixup special dialog override messages
	for(i=0; i<eaSize(&pMission->ppSpecialDialogOverrides); i++) {
		SpecialDialogOverride *pOverride = pMission->ppSpecialDialogOverrides[i];
		if (pOverride->pcContactName) {
			sprintf(buf1, "%s.%s.SpecialDialog", baseMessageKey, pOverride->pcContactName);
			sprintf(buf3, "%s.%s.SpecialDialogName", baseMessageKey, pOverride->pcContactName);
		} else {
			sprintf(buf1, "%s.NO_CONTACT.SpecialDialog", baseMessageKey);
			sprintf(buf3, "%s.NO_CONTACT.SpecialDialogName", baseMessageKey);
		}
		sprintf(buf2, "Special dialog override in %s for %s", pMission->name, pOverride->pcContactName);
		sprintf(buf4, "Text for special dialog override in %s for %s", pMission->name, pOverride->pcContactName);
		sprintf(buf4, "Special dialog override action in %s for %s", pMission->name, pOverride->pcContactName);
		CEFixupSpecialDialogMessages(pOverride->pSpecialDialog, buf1, buf2, buf3, buf4, buf5, i);
	}

	// Fixup special action block override messages
	for(i=0; i<eaSize(&pMission->ppSpecialActionBlockOverrides); i++) {
		ActionBlockOverride *pOverride = pMission->ppSpecialActionBlockOverrides[i];
		if (pOverride->pcContactName) {
			sprintf(buf1, "%s.%s.SpecialActionBlock", baseMessageKey, pOverride->pcContactName);
			sprintf(buf2, "%s.%s.SpecialActionBlockName", baseMessageKey, pOverride->pcContactName);
		} else {
			sprintf(buf1, "%s.NO_CONTACT.SpecialActionBlock", baseMessageKey);
			sprintf(buf2, "%s.NO_CONTACT.SpecialActionBlockName", baseMessageKey);
		}
		CEFixupActionBlockMessages(pOverride->pSpecialActionBlock, buf1, i); 
	}

	// Fixup mission offer override messages
	for(i = 0; i < eaSize(&pMission->ppMissionOfferOverrides); i++) 
	{
		MissionOfferOverride* pOverride = pMission->ppMissionOfferOverrides[i];
		CEFixupOfferMessages(baseMessageKey, pOverride->pcContactName, pOverride->pMissionOffer, i);
	}

	// Fixup Image Menu Item messages
	for( i = 0; i < eaSize( &pMission->ppImageMenuItemOverrides ); ++i ) {
		ImageMenuItemOverride* pOverride = pMission->ppImageMenuItemOverrides[ i ];
		CEFixupImageMenuItemMessages(baseMessageKey, i, pOverride->pImageMenuItem );
	}

	// fix submissions
	for(i=eaSize(&pMission->subMissions)-1; i>=0; --i) {
		MDEFixupMessages(pRootMission, pMission->subMissions[i]);
	}
}

static void MDESanitizeMissionForVersionCompareRecursive(MissionDef *pDef)
{
	int i;

	if (!pDef)
		return;

	pDef->scope = NULL;
	pDef->filename = NULL;
	pDef->version = 0;
	eaDestroyStruct(&pDef->eaObjectiveMaps, parse_MissionMap);
	pDef->pchReturnMap = NULL;
	REMOVE_HANDLE(pDef->hCategory);
	eaClearEx(&pDef->eaOpenMissionVolumes, NULL);
	StructFreeString(pDef->comments);
	pDef->comments = NULL;

	StructDestroySafe(parse_Message, &pDef->displayNameMsg.pEditorCopy);
	StructDestroySafe(parse_Message, &pDef->uiStringMsg.pEditorCopy);
	StructDestroySafe(parse_Message, &pDef->detailStringMsg.pEditorCopy);
	StructDestroySafe(parse_Message, &pDef->summaryMsg.pEditorCopy);
	StructDestroySafe(parse_Message, &pDef->failureMsg.pEditorCopy);
	StructDestroySafe(parse_Message, &pDef->failReturnMsg.pEditorCopy);
	StructDestroySafe(parse_Message, &pDef->splatDisplayMsg.pEditorCopy);
	StructDestroySafe(parse_Message, &pDef->TeamUpDisplayName.pEditorCopy);

	pDef->pchIconName = NULL;
	StructReset(parse_MissionLevelDef, &pDef->levelDef);
	pDef->uTimeout = 0;
	pDef->autoGrantOnMap = NULL;
	pDef->repeatable = false;
	pDef->fRepeatCooldownHours = 0;
	pDef->fRepeatCooldownHoursFromStart = 0;
	pDef->needsReturn = false;
	pDef->lockoutType = 0;
	pDef->iSuggestedTeamSize = 0;
	pDef->bScalesForTeamSize = false;
	pDef->eShareable = 0;
	pDef->iRepeatCooldownCount = 0;
	pDef->bSuppressUnreliableOpenRewardErrors = false;

	StructDestroySafe(parse_MissionDefParams, &pDef->params);
	StructDestroySafe(parse_MissionMapWarpData, &pDef->pWarpToMissionDoor);

	// Remove SendFloater and SendNotification actions
	for (i = eaSize(&pDef->ppOnStartActions)-1; i >= 0; --i){
		if ((pDef->ppOnStartActions[i]->eActionType == WorldGameActionType_SendFloaterMsg) ||
			(pDef->ppOnStartActions[i]->eActionType == WorldGameActionType_SendNotification)) {
			StructDestroy(parse_WorldGameActionProperties, pDef->ppOnStartActions[i]);
			eaRemove(&pDef->ppOnStartActions, i);
		}
	}
	for (i = eaSize(&pDef->ppSuccessActions)-1; i >= 0; --i){
		if ((pDef->ppSuccessActions[i]->eActionType == WorldGameActionType_SendFloaterMsg) ||
			(pDef->ppSuccessActions[i]->eActionType == WorldGameActionType_SendNotification)) {
			StructDestroy(parse_WorldGameActionProperties, pDef->ppSuccessActions[i]);
			eaRemove(&pDef->ppSuccessActions, i);
		}
	}
	for (i = eaSize(&pDef->ppFailureActions)-1; i >= 0; --i){
		if ((pDef->ppFailureActions[i]->eActionType == WorldGameActionType_SendFloaterMsg) ||
			(pDef->ppFailureActions[i]->eActionType == WorldGameActionType_SendNotification)) {
			StructDestroy(parse_WorldGameActionProperties, pDef->ppFailureActions[i]);
			eaRemove(&pDef->ppFailureActions, i);
		}
	}
	for (i = eaSize(&pDef->ppOnReturnActions)-1; i >= 0; --i){
		if ((pDef->ppOnReturnActions[i]->eActionType == WorldGameActionType_SendFloaterMsg) ||
			(pDef->ppOnReturnActions[i]->eActionType == WorldGameActionType_SendNotification)) {
			StructDestroy(parse_WorldGameActionProperties, pDef->ppOnReturnActions[i]);
			eaRemove(&pDef->ppOnReturnActions, i);
		}
	}

	eaDestroyStruct(&pDef->ppInteractableOverrides, parse_InteractableOverride);
	eaDestroyStruct(&pDef->eaWaypoints, parse_MissionWaypoint);
	pDef->eReturnType = 0;
	StructDestroySafe(parse_Message, &pDef->msgReturnStringMsg.pEditorCopy);
	pDef->iPerkPoints = 0;
	pDef->iSortPriority = 0;
	pDef->iMinLevel = 0;
	pDef->bIsHandoff = 0;
	pDef->doNotUncomplete = 0;
	pDef->bSuppressUnreliableOpenRewardErrors = 0;
	pDef->doNotAllowDrop = 0;
	pDef->bIsTutorialPerk = 0;
	pDef->eTutorialScreenRegion = 0;
	eaDestroyStruct(&pDef->eaRequiredAllegiances, parse_AllegianceRef);

	if (pDef->pDiscoverCond){
		exprDestroy(pDef->pDiscoverCond);
		pDef->pDiscoverCond = NULL;
	}
	
	if (pDef->missionReqs){
		exprDestroy(pDef->missionReqs);
		pDef->missionReqs = NULL;
	}

	if (pDef->pMapRequirements){
		exprDestroy(pDef->pMapRequirements);
		pDef->pMapRequirements = NULL;
	}

	if (pDef->pMapSuccess){
		exprDestroy(pDef->pMapSuccess);
		pDef->pMapSuccess = NULL;
	}

	if (pDef->pMapFailure){
		exprDestroy(pDef->pMapFailure);
		pDef->pMapFailure = NULL;
	}

	eaDestroyStruct(&pDef->eaTrackedEvents, parse_GameEvent);

	StructDestroySafe(parse_MissionEditCond, &pDef->meSuccessCond);
	StructDestroySafe(parse_MissionEditCond, &pDef->meFailureCond);
	StructDestroySafe(parse_MissionEditCond, &pDef->meResetCond);

	for (i = 0; i < eaSize(&pDef->subMissions); i++){
		MDESanitizeMissionForVersionCompareRecursive(pDef->subMissions[i]);
	}
}

static void MDEUpdateVersionNumber(MissionEditDoc *pDoc)
{
	if (pDoc->pMission && pDoc->pOrigMission && pDoc->pMission->version == pDoc->pOrigMission->version)
	{
		// Copy MissionDefs
		MissionDef *pOldDef = StructClone(parse_MissionDef, pDoc->pOrigMission);
		MissionDef *pNewDef = StructClone(parse_MissionDef, pDoc->pMission);

		// NULL out all fields that don't require a version update when changed
		MDESanitizeMissionForVersionCompareRecursive(pOldDef);
		MDESanitizeMissionForVersionCompareRecursive(pNewDef);

		// StructCompare the two missions
		if (StructCompare(parse_MissionDef, pOldDef, pNewDef, 0, 0, TOK_NO_TEXT_SAVE) != 0) {
			// A version update is necessary; update the version
			pDoc->pMission->version = pDoc->pOrigMission->version+1;
		}

		// Free copies
		StructDestroy(parse_MissionDef, pOldDef);
		StructDestroy(parse_MissionDef, pNewDef);
	}
}

static bool MDEFindMissionEventCountRecursive(MissionEditCond *pCond)
{
	int i;
	if (pCond){
		if (pCond->type == MissionCondType_Expression && pCond->valStr && strstri(pCond->valStr, "MissionEventCount")){
			return true;
		}
		for (i = 0; i < eaSize(&pCond->subConds); i++){
			if (MDEFindMissionEventCountRecursive(pCond->subConds[i]))
				return true;
		}
	}
	return false;
}

// Event-based missions should be Never Uncomplete
static void MDEUpdateDoNotUncompleteFlag(MissionDef *pDef)
{
	int i;
	if (pDef)
	{
		if (MDEFindMissionEventCountRecursive(pDef->meSuccessCond)){
			pDef->doNotUncomplete = true;
		}
		for (i=eaSize(&pDef->subMissions)-1; i>=0; --i){
			MDEUpdateDoNotUncompleteFlag(pDef->subMissions[i]);
		}
	}
}

static void MDEFillOptionalStructs(MissionDef *pMission)
{
	int i;

	if (!pMission->params) {
		pMission->params = StructCreate(parse_MissionDefParams);
	}

	if (!pMission->pWarpToMissionDoor) {
		pMission->pWarpToMissionDoor = StructCreate(parse_MissionMapWarpData);
	}

	if (!pMission->meSuccessCond) {
		pMission->meSuccessCond = StructCreate(parse_MissionEditCond);
		pMission->meSuccessCond->type = MissionCondType_And;
	} else if ((pMission->meSuccessCond->type != MissionCondType_And) && (pMission->meSuccessCond->type != MissionCondType_Or) && (pMission->meSuccessCond->type != MissionCondType_Count)) {
		MissionEditCond *pCond = StructCreate(parse_MissionEditCond);
		pCond->type = MissionCondType_And;
		eaPush(&pCond->subConds, pMission->meSuccessCond);
		pMission->meSuccessCond = pCond;
	}

	if (!pMission->meFailureCond) {
		pMission->meFailureCond = StructCreate(parse_MissionEditCond);
		pMission->meFailureCond->type = MissionCondType_And;
	} else if ((pMission->meFailureCond->type != MissionCondType_And) && (pMission->meFailureCond->type != MissionCondType_Or) && (pMission->meFailureCond->type != MissionCondType_Count)) {
		MissionEditCond *pCond = StructCreate(parse_MissionEditCond);
		pCond->type = MissionCondType_And;
		eaPush(&pCond->subConds, pMission->meFailureCond);
		pMission->meFailureCond = pCond;
	}

	if (!pMission->meResetCond) {
		pMission->meResetCond = StructCreate(parse_MissionEditCond);
		pMission->meResetCond->type = MissionCondType_And;
	} else if ((pMission->meResetCond->type != MissionCondType_And) && (pMission->meResetCond->type != MissionCondType_Or) && (pMission->meResetCond->type != MissionCondType_Count)) {
		MissionEditCond *pCond = StructCreate(parse_MissionEditCond);
		pCond->type = MissionCondType_And;
		eaPush(&pCond->subConds, pMission->meResetCond);
		pMission->meResetCond = pCond;
	}

	for(i=eaSize(&pMission->subMissions)-1; i>=0; --i) {
		MDEFillOptionalStructs(pMission->subMissions[i]);
	}
}


static void MDERemoveUnusedOptionalStructs(MissionDef *pMission)
{
	int i;

	if (pMission->pWarpToMissionDoor && !pMission->pWarpToMissionDoor->pchMapName) {
		StructDestroySafe(parse_MissionMapWarpData, &pMission->pWarpToMissionDoor);
	}

	if (eaSize(&pMission->meSuccessCond->subConds) == 0) {
		eaDestroyStruct(&pMission->meSuccessCond->subConds, parse_MissionEditCond);
		pMission->meSuccessCond = NULL;
	} else if (eaSize(&pMission->meSuccessCond->subConds) == 1) {
		MissionEditCond *pCond = pMission->meSuccessCond->subConds[0];
		eaRemove(&pMission->meSuccessCond->subConds, 0);
		StructDestroy(parse_MissionEditCond, pMission->meSuccessCond);
		pMission->meSuccessCond = pCond;
	}

	FOR_EACH_IN_EARRAY(pMission->eaWaypoints, MissionWaypoint, pWaypoint)
	{
		if (pWaypoint->type == MissionWaypointType_None)
		{
			StructDestroy(parse_MissionWaypoint, pWaypoint);
			eaRemove(&pMission->eaWaypoints, FOR_EACH_IDX(pMission->eaWaypoints, pWaypoint));
		}
	}
	FOR_EACH_END

	if (eaSize(&pMission->meFailureCond->subConds) == 0) {
		eaDestroyStruct(&pMission->meFailureCond->subConds, parse_MissionEditCond);
		pMission->meFailureCond = NULL;
	} else if (eaSize(&pMission->meFailureCond->subConds) == 1) {
		MissionEditCond *pCond = pMission->meFailureCond->subConds[0];
		eaRemove(&pMission->meFailureCond->subConds, 0);
		StructDestroy(parse_MissionEditCond, pMission->meFailureCond);
		pMission->meFailureCond = pCond;
	}

	if (eaSize(&pMission->meResetCond->subConds) == 0) {
		eaDestroyStruct(&pMission->meResetCond->subConds, parse_MissionEditCond);
		pMission->meResetCond = NULL;
	} else if (eaSize(&pMission->meResetCond->subConds) == 1) {
		MissionEditCond *pCond = pMission->meResetCond->subConds[0];
		eaRemove(&pMission->meResetCond->subConds, 0);
		StructDestroy(parse_MissionEditCond, pMission->meResetCond);
		pMission->meResetCond = pCond;
	}

	for(i=eaSize(&pMission->ppInteractableOverrides)-1; i>= 0; --i) {
		WorldInteractionPropertyEntry* propEntry = pMission->ppInteractableOverrides[i]->pPropertyEntry;
		
		if (!propEntry) {
			eaRemove(&pMission->ppInteractableOverrides, i);
			continue;
		}
	}

	for(i=eaSize(&pMission->subMissions)-1; i>=0; --i) {
		MDERemoveUnusedOptionalStructs(pMission->subMissions[i]);
	}
}


static void MDEFreeCondGroup(MDECondGroup *pGroup)
{
	ui_WidgetQueueFree((UIWidget*)pGroup->pLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pRemoveButton);

	MEFieldSafeDestroy(&pGroup->pExprField);
	MEFieldSafeDestroy(&pGroup->pExprCountField);
	MEFieldSafeDestroy(&pGroup->pMissionField);

	// Free the group itself
	free(pGroup);
}

static bool MDEActionIsEditable(MissionEditDoc *pDoc)
{
	if (pDoc)
		return emDocIsEditable((&pDoc->emDoc), true);
	return false;
}

static GEActionGroup *MDECreateActionGroup(MDEMissionGroup *pMissionGroup)
{
	GEActionGroup *pGroup = calloc(1, sizeof(GEActionGroup));
	pGroup->pWidgetParent = UI_WIDGET(pMissionGroup->pActionExpander);

	pGroup->dataChangedCB = MDEMissionChanged;
	pGroup->updateDisplayCB = MDEUpdateDisplay;
	pGroup->isEditableFunc = MDEActionIsEditable;
	pGroup->pUserData = pMissionGroup->pDoc;

	return pGroup;
}

static void MDEFreeContactOverrideGroup(MDEContactOverrideGroup *pGroup)
{
	ui_WidgetQueueFree((UIWidget*)pGroup->pFindContactOverrideWindowButton);

	// Free the group itself
	free(pGroup);
}

static void MDEFreeNumericScaleGroup(MDENumericScaleGroup *pGroup)
{
	ui_WidgetQueueFree((UIWidget*)pGroup->pNumericLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pScaleLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pRemoveButton);
	ui_WidgetQueueFree((UIWidget*)pGroup->pSeparator);

	MEFieldSafeDestroy(&pGroup->pNumericField);
	MEFieldSafeDestroy(&pGroup->pScaleField);

	// Free the group itself
	free(pGroup);
}

static void MDEFreeDropGroup(MDEDropGroup *pGroup)
{
	ui_WidgetQueueFree((UIWidget*)pGroup->pTypeLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pWhenLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pNameLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pRewardLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pMapLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pRemoveButton);
	ui_WidgetQueueFree((UIWidget*)pGroup->pRewardEditButton);
	ui_WidgetQueueFree((UIWidget*)pGroup->pSeparator);

	MEFieldSafeDestroy(&pGroup->pTypeField);
	MEFieldSafeDestroy(&pGroup->pWhenField);
	MEFieldSafeDestroy(&pGroup->pNameField);
	MEFieldSafeDestroy(&pGroup->pSpawningPlayerField);
	MEFieldSafeDestroy(&pGroup->pRewardField);
	MEFieldSafeDestroy(&pGroup->pMapField);

	// Free the group itself
	free(pGroup);
}

static void MDEFreeMapGroup(MDEMapGroup* pGroup)
{
	int i;

	ui_WidgetQueueFree((UIWidget*)pGroup->pSeparator);
	ui_WidgetQueueFree((UIWidget*)pGroup->pAddVarButton);
	ui_WidgetQueueFree((UIWidget*)pGroup->pRemoveMapButton);
	ui_WidgetQueueFree((UIWidget*)pGroup->pMapLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pHideGotoLabel);

	MEFieldSafeDestroy(&pGroup->pMapField);
	MEFieldSafeDestroy(&pGroup->pHideGotoField);

	if(pGroup->eaVarGroups) {
		for(i = eaSize(&pGroup->eaVarGroups)-1; i>=0; i--) {
			GEFreeVariableGroup(pGroup->eaVarGroups[i]);
		}
		eaDestroy(&pGroup->eaVarGroups);
	}
	if(pGroup->eaVarDefs)
		eaDestroy(&pGroup->eaVarDefs);
	if(pGroup->eaVarNames)
		eaDestroy(&pGroup->eaVarNames);

	free(pGroup);
}

static void MDEFreeLevelGroup(MDELevelGroup* pGroup) {
	if(pGroup) {
		GEFreeMissionLevelDefGroupSafe( &pGroup->pLevelDefGroup );
		free(pGroup);
	}
}

static void MDEFreeSpecialDialogOverrideGroup(MDESpecialDialogOverrideGroup *pGroup)
{
	ui_WidgetQueueFree((UIWidget*)pGroup->pNameLabel);

	MEFieldSafeDestroy(&pGroup->pNameField);

	// Free the child interaction properties group
	if (pGroup->pSpecialDialogGroup) {
		CEFreeSpecialDialogGroup(pGroup->pSpecialDialogGroup);
	}

	if (pGroup->pExpander->group)
	{
		ui_ExpanderGroupRemoveExpander(pGroup->pExpander->group, pGroup->pExpander);
	}	
	ui_WidgetQueueFree(UI_WIDGET(pGroup->pExpander));

	// Free the group itself
	free(pGroup);
}

static void MDEFreeSpecialActionBlockOverrideGroup(MDESpecialActionBlockOverrideGroup *pGroup)
{
	ui_WidgetQueueFree((UIWidget*)pGroup->pNameLabel);

	MEFieldSafeDestroy(&pGroup->pNameField);

	if(pGroup->pSpecialActionBlockGroup) {
		CEFreeSpecialActionBlockGroup(pGroup->pSpecialActionBlockGroup);
	}

	if(pGroup->pExpander->group)
	{
		ui_ExpanderGroupRemoveExpander(pGroup->pExpander->group, pGroup->pExpander);
	}

	ui_WidgetQueueFree(UI_WIDGET(pGroup->pExpander));

	// Free the group itself
	free(pGroup);
}

static void MDEFreeMissionOfferOverrideGroup(MDEMissionOfferOverrideGroup *pGroup)
{
	ui_WidgetQueueFree((UIWidget*)pGroup->pNameLabel);

	MEFieldSafeDestroy(&pGroup->pNameField);

	// Free the child interaction properties group
	if (pGroup->pMissionOfferGroup) 
	{
		CEFreeOfferGroup(pGroup->pMissionOfferGroup);
	}

	if (pGroup->pExpander->group)
	{
		ui_ExpanderGroupRemoveExpander(pGroup->pExpander->group, pGroup->pExpander);
	}
	ui_WidgetQueueFree(UI_WIDGET(pGroup->pExpander));

	// Free the group itself
	free(pGroup);
}

static void MDEFreeImageMenuItemOverrideGroup(MDEImageMenuItemOverrideGroup *pGroup)
{
	MEFieldSafeDestroy(&pGroup->pNameField);

	if( pGroup->pImageMenuItemGroup)
	{
		CEFreeImageMenuItemGroup( pGroup->pImageMenuItemGroup );
	}
	
	if (pGroup->pExpander->group)
	{
		ui_ExpanderGroupRemoveExpander(pGroup->pExpander->group, pGroup->pExpander);
	}
	ui_WidgetQueueFree(UI_WIDGET(pGroup->pExpander));

	// Free the group itself
	free(pGroup);
}

static void MDEFreeMissionGroupFields(MDEMissionGroup *pGroup)
{

	// Free doc fields
	eaDestroyEx(&pGroup->eaDocFields, MEFieldDestroy);

	// Free param fields
	eaDestroyEx(&pGroup->eaParamFields, MEFieldDestroy);

	// Free activity param field
	MEFieldSafeDestroy(&pGroup->pParamActivityName);
	// Free required activity fields
	MEFieldSafeDestroy(&pGroup->pInfoReqAnyActivities);
	MEFieldSafeDestroy(&pGroup->pInfoReqAllActivities);
	MEFieldSafeDestroy(&pGroup->pRelatedEvent);
	// Free activity name list
	eaDestroyEx(&pGroup->ppchActivityNames, StructFreeString);

	// Free sub-groups
	eaDestroyEx(&pGroup->eaContactOverrideGroups, MDEFreeContactOverrideGroup);
	eaDestroyEx(&pGroup->eaActionGroups, GEFreeActionGroup);
	eaDestroyEx(&pGroup->eaCondSuccessGroups, MDEFreeCondGroup);
	eaDestroyEx(&pGroup->eaCondFailureGroups, MDEFreeCondGroup);
	eaDestroyEx(&pGroup->eaCondResetGroups, MDEFreeCondGroup);
	eaDestroyEx(&pGroup->eaNumericScaleGroups, MDEFreeNumericScaleGroup);
	eaDestroyEx(&pGroup->eaDropGroups, MDEFreeDropGroup);
	eaDestroyEx(&pGroup->eaInteractableOverrideGroups, MDEFreeInteractableOverrideGroup);
	eaDestroyEx(&pGroup->eaScoreEventGroups, MDEFreeScoreEventGroup);
	eaDestroyEx(&pGroup->eaEventGroups, MDEFreeEventGroup);
	eaDestroyEx(&pGroup->eaRequestGroups, MDEFreeRequestGroup);
	eaDestroyEx(&pGroup->eaMapGroups, MDEFreeMapGroup);
	eaDestroyEx(&pGroup->eaVariableDefGroups, GEFreeVariableDefGroup);
	eaDestroyEx(&pGroup->eaWaypointGroups, MDEFreeWaypointGroup);

	MDEFreeLevelGroup(pGroup->pLevelGroup);

	// Free special fields
	MEFieldSafeDestroy(&pGroup->pDisplayNameField);
	MEFieldSafeDestroy(&pGroup->pUIStringField);
	MEFieldSafeDestroy(&pGroup->pSummaryField);
	MEFieldSafeDestroy(&pGroup->pDetailField);
	MEFieldSafeDestroy(&pGroup->pFailureTextField);
	MEFieldSafeDestroy(&pGroup->pReturnField);
	MEFieldSafeDestroy(&pGroup->pFailedReturnField);
	MEFieldSafeDestroy(&pGroup->pSplatTextField);
	MEFieldSafeDestroy(&pGroup->pCondSuccessWhenField);
	MEFieldSafeDestroy(&pGroup->pCondSuccessWhenCountField);
	MEFieldSafeDestroy(&pGroup->pCondFailureWhenField);
	MEFieldSafeDestroy(&pGroup->pCondFailureWhenCountField);
	MEFieldSafeDestroy(&pGroup->pCondDiscoverWhenField);
	MEFieldSafeDestroy(&pGroup->pCondResetWhenField);
	MEFieldSafeDestroy(&pGroup->pCondResetWhenCountField);
	MEFieldSafeDestroy(&pGroup->pTeamUpTextField);
	

	eaDestroyEx(&pGroup->pWindow->widget.children, ui_WidgetQueueFree);
	eaDestroy(&pGroup->eaInBoxes);
	eaDestroy(&pGroup->eaOutBoxes);
	pGroup->pExGroup = NULL;
	pGroup->pContactOverridesExpander = NULL;
	pGroup->pDisplayExpander = NULL;
	pGroup->pLevelExpander = NULL;
	pGroup->pLevelGroup = NULL;
	pGroup->pEventExpander = NULL;
	pGroup->pWaypointExpander = NULL;
	pGroup->pWarpExpander = NULL;
	pGroup->pCondExpander = NULL;
	pGroup->pActionExpander = NULL;
	pGroup->pRewardsExpander = NULL;
	pGroup->pOpenMissionRewardsExpander = NULL;
	pGroup->pDropExpander = NULL;
	pGroup->pInteractableOverrideExpander = NULL;
	pGroup->pSpecialDialogOverrideExpander = NULL;
	pGroup->pMissionOfferOverrideExpander = NULL;
	pGroup->pScoreboardExpander = NULL;
	pGroup->pRequestsExpander = NULL;
	pGroup->pVariablesExpander = NULL;
	pGroup->pCondSuccessAddExprButton = NULL;
	pGroup->pCondSuccessAddObjButton = NULL;
	pGroup->pCondFailureAddExprButton = NULL;
	pGroup->pCondFailureAddObjButton = NULL;
	pGroup->pCondResetAddExprButton = NULL;
	pGroup->pAddNumericScaleButton = NULL;
	pGroup->pAddDropButton = NULL;
	pGroup->pAddInteractableOverrideButton = NULL;
	pGroup->pAddScoreEventButton = NULL;
	pGroup->pAddEventButton = NULL;
	pGroup->pAddWaypointButton = NULL;
	pGroup->pCondSuccessMainLabel = NULL;
	pGroup->pCondSuccessWhenLabel = NULL;
	pGroup->pCondSuccessWhenCountLabel = NULL;
	pGroup->pCondFailureMainLabel = NULL;
	pGroup->pCondFailureWhenLabel = NULL;
	pGroup->pCondFailureWhenCountLabel = NULL;
	pGroup->pCondFailureWhen2Label = NULL;
	pGroup->pCondDiscoverWhenLabel = NULL;
	pGroup->pCondResetMainLabel = NULL;
	pGroup->pCondResetWhenLabel = NULL;
	pGroup->pCondResetWhenCountLabel = NULL;
}

static void MDEFreeMissionGroup(MDEMissionGroup *pGroup)
{
	// Destroy all UI fields
	MDEFreeMissionGroupFields(pGroup);

	// Free the UI resources
	ui_WidgetQueueFree(UI_WIDGET(pGroup->pWindow));
	ui_WidgetQueueFree(UI_WIDGET(pGroup->pContextMenu));

	// Free the group itself
	free(pGroup->pcName);
	free(pGroup);
}

static void MDEFullRefreshMissionGroup(MDEMissionGroup *pGroup, bool bIsRoot)
{
	MDEFreeMissionGroupFields(pGroup);
	MDEInitMissionGroupDisplay(pGroup, bIsRoot);	
}


static void MDEFixNameCond(MissionEditCond *pCond, const char *pcOldName, const char *pcNewName)
{
	int i;

	if (!pCond) {
		return;
	}

	if ((pCond->type == MissionCondType_Objective) && pCond->valStr && (stricmp(pCond->valStr, pcOldName) == 0)) {
		StructFreeString(pCond->valStr);
		pCond->valStr = StructAllocString(pcNewName);
	}

	// Recurse
	for(i=eaSize(&pCond->subConds)-1; i>=0; --i) {
		MDEFixNameCond(pCond->subConds[i], pcOldName, pcNewName);
	}
}


void MDEFixNameActionArray(WorldGameActionProperties ***peaActions, const char *pcOldName, const char *pcNewName)
{
	int i;

	for(i=eaSize(peaActions)-1; i>=0; --i) {
		WorldGameActionProperties* pAction = (*peaActions)[i];
		if (pAction->eActionType == WorldGameActionType_GrantSubMission) {
			const char* pcCurrName = pAction->pGrantSubMissionProperties->pcSubMissionName;
			if (stricmp(pcCurrName, pcOldName) == 0) {
				pAction->pGrantSubMissionProperties->pcSubMissionName = allocAddString(pcNewName);
			}
		}
	}
}


static void MDERenameSubMission(MissionDef *pMission, const char *pcOldName, const char *pcNewName, bool bIsRoot)
{
	int i;

	// Fix grant actions
	MDEFixNameActionArray(&pMission->ppOnStartActions, pcOldName, pcNewName);
	MDEFixNameActionArray(&pMission->ppSuccessActions, pcOldName, pcNewName);
	MDEFixNameActionArray(&pMission->ppFailureActions, pcOldName, pcNewName);
	MDEFixNameActionArray(&pMission->ppOnReturnActions, pcOldName, pcNewName);

	// Fix conditions
	MDEFixNameCond(pMission->meSuccessCond, pcOldName, pcNewName);
	MDEFixNameCond(pMission->meFailureCond, pcOldName, pcNewName);
	MDEFixNameCond(pMission->meResetCond, pcOldName, pcNewName);

	// Recurse on sub-missions
	for(i=eaSize(&pMission->subMissions)-1; i>=0; --i) {
		MDERenameSubMission(pMission->subMissions[i], pcOldName, pcNewName, false);
	}
}


static bool MDEIsNameUnique(MissionDef *pRootMission, MissionDef *pMission, const char *pcName)
{
	int i;

	// Check against root mission
	if ((pMission != pRootMission) && (stricmp(pcName, pRootMission->name) == 0)) {
		return false;
	}

	// Check against sub-missions
	for(i=eaSize(&pRootMission->subMissions)-1; i>=0; --i) {
		MissionDef *pSubMission = pRootMission->subMissions[i];
		if ((pSubMission != pMission) && pSubMission->name && (stricmp(pSubMission->name, pcName) == 0)) {
			return false;
		}
	}

	return true;
}


static void MDESavePrefs(MissionEditDoc *pDoc)
{
	char *estrWindowName = NULL;

	EditorPrefStoreFloat(MISSION_EDITOR, "Options", "MissionWidth", pDoc->pMainMissionGroup->pWindow->widget.width);

	// Find the max width for contact windows
	FOR_EACH_IN_EARRAY(pDoc->eaContactGroups, MDEContactGroup, pContactGroup)
	{
		if (pContactGroup && pContactGroup->pWindow && (eaSize(&pContactGroup->eaSpecialDialogOverrideGroups) > 0 || eaSize(&pContactGroup->eaMissionOfferOverrideGroups) > 0))
		{
			// Store the contact window position
			estrPrintf(&estrWindowName, "Contact_%s", pContactGroup->pchContactName);
			EditorPrefStoreWindowPosition(MISSION_EDITOR, "Window Position", estrWindowName, pContactGroup->pWindow);
		}
	}
	FOR_EACH_END

	estrDestroy(&estrWindowName);
}


//---------------------------------------------------------------------------------------------------
// UI Logic
//---------------------------------------------------------------------------------------------------


static void MDEAddFieldToParent(MEField *pField, UIWidget *pParent, F32 x, F32 y, F32 xPercent, F32 w, UIUnitType wUnit, F32 padRight, MissionEditDoc *pDoc)
{
	MEFieldAddToParent(pField, pParent, x, y);
	ui_WidgetSetPositionEx(pField->pUIWidget, x, y, xPercent, 0, UITopLeft);
	ui_WidgetSetWidthEx(pField->pUIWidget, w, wUnit);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, padRight, 0, 0);
	MEFieldSetChangeCallback(pField, MDEFieldChangedCB, pDoc);
	MEFieldSetPreChangeCallback(pField, MDEFieldPreChangeCB, pDoc);
}


static void MDEZoom(MissionEditDoc *pDoc, int step)
{
	F32 fScale = CLAMP(pDoc->pScrollArea->childScale + (0.1 * step), 0.20, 2.0);
	ui_ScrollAreaSetChildScale(pDoc->pScrollArea, fScale);
	EditorPrefStoreFloat(MISSION_EDITOR, "Window Zoom", "Main", fScale);
}


static void MDEZoomOutCB(UIButton *pButton, void *unused)
{
	MissionEditDoc *pDoc = MDEGetActiveDoc();
	MDEZoom(pDoc, -1);
}


static void MDEZoomInCB(UIButton *pButton, void *unused)
{
	MissionEditDoc *pDoc = MDEGetActiveDoc();
	MDEZoom(pDoc, 1);
}


static void MDERewardEditCB(UIButton *pButton, MDERewardData *pData)
{
	if (*pData->ppcRewardTableName) {
		emOpenFileEx(*pData->ppcRewardTableName, "RewardTable");
	} else {
		rewardTableEditor_InitData *pInitData;
		char buf[1024];
		char *ptr;

		sprintf(buf, "Mission_%s_%s", pData->pMission->pchRefString, pData->cNamePart);
		while(ptr = strchr(buf, ':')) {
			*ptr = '_';
		}
		*pData->ppcRewardTableName = StructAllocString(buf);
		MDEMissionChanged(pData->pDoc, true);

		pInitData = calloc(1, sizeof(rewardTableEditor_InitData));
		pInitData->TableName = strdup(buf);
		pInitData->scope = strdup("Mission");
		emNewDoc("RewardTable", pInitData);
	}
}


static MissionDef *MDEGetSelectedMission(MissionEditDoc *pDoc)
{
	int i;

	
	if (WindowIsSelected(pDoc->pMainMissionGroup->pWindow)) {
		return pDoc->pMission;
	}
	for(i=eaSize(&pDoc->eaSubMissionGroups)-1; i>=0; --i) {
		MDEMissionGroup *pGroup = pDoc->eaSubMissionGroups[i];
		if (WindowIsSelected(pGroup->pWindow)) {
			return pGroup->pMission;
		}
	}
	return NULL;
}


void MDEDeleteUnusedSubMissions(MissionEditDoc *pDoc)
{
	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pDoc->emDoc, true)) {
		return;
	}

	// Do the removal
	if (missiondef_RemoveUnusedSubmissions(pDoc->pMission)) {
		MDEMissionChanged(pDoc, true);
	}
}


MissionDef* MDECreateSubMission(MissionEditDoc* pDoc, MissionDef *pMissionToClone)
{
	MissionDef *pSubMission;
	char subMissionName[260];
	const char *pcName = "Objective";
	bool bGoodName = false;
	int count = 0;

	if (pMissionToClone) {
		if (MDEIsNameUnique(pDoc->pMission, NULL, pMissionToClone->name)) {
			strcpy(subMissionName, pMissionToClone->name);
			bGoodName = true;
		} else {
			pcName = pMissionToClone->name;
		}
	}

	// If the original name wasn't available, add numbers to it
	while (!bGoodName) { 
		++count;
		sprintf(subMissionName, "%s%i", pcName, count);
		if (MDEIsNameUnique(pDoc->pMission, NULL, subMissionName)) {
			bGoodName = true;
		}
	}

	// Create the new objective
	if (pMissionToClone) {
		pSubMission = StructClone(parse_MissionDef, pMissionToClone);
	} else {
		pSubMission = StructCreate(parse_MissionDef);
	}

	// Set name, filename, and handle
	pSubMission->filename = StructAllocString(pDoc->pMission->filename);
	pSubMission->name = (char*)allocAddString(subMissionName);
	eaPush(&pDoc->pMission->subMissions, pSubMission);
	SET_HANDLE_FROM_STRING(g_MissionDictionary, pDoc->pMission->name, pSubMission->parentDef);
	missiondef_CreateRefString(pSubMission, pDoc->pMission);

	if (pMissionToClone) {
		// Clear fields that submissions do not have
		missiondef_CleanSubmission(pSubMission);
	}

	// Make sure optional structs are present
	MDEFillOptionalStructs(pSubMission);

	// Make editor copy
	langMakeEditorCopy(parse_MissionDef, pSubMission, true);

	// Fix up editor copies of messages
	MDEFixupMessages(pDoc->pMission, pSubMission);

	return pSubMission;
}


static void MDECreateSubMissionCB(UIButton *pButton, void *unused)
{
	MissionEditDoc *pDoc = MDEGetActiveDoc();
	assert(pDoc);

	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pDoc->emDoc, true)) {
		return;
	}

	MDECreateSubMission(pDoc, NULL);
	MDEMissionChanged(pDoc, true);
}


static bool MDESelectedIsSubMission(void *unused)
{
	MissionEditDoc *pDoc = MDEGetActiveDoc();
	if (pDoc) {
		MissionDef *pMission = MDEGetSelectedMission(pDoc);
		return (pMission != pDoc->pMission);
	} else {
		return false;
	}
}

static bool MDEUpdateUI(MissionEditDoc *pDoc)
{
	if(!pDoc)
		return false;

	if (pDoc->pDialogFlowWindowInfo)
	{
		CERefreshDialogFlowWindow(pDoc->pDialogFlowWindowInfo);
	}	

	MDEMissionChanged(pDoc, true);
	return true;
}

static void MDEDeleteContactGroup(MissionEditDoc *pDoc, MDEContactGroup *pGroup)
{
	S32 i;

	DialogFlowWindowInfo *pInfo = pDoc ? pDoc->pDialogFlowWindowInfo : NULL;

	if (pGroup == NULL || pDoc == NULL)
	{
		return;
	}

	if (!emDocIsEditable(&pDoc->emDoc, true)) 
	{
		return;
	}

	if (pInfo && stricmp(pInfo->pchContactName, pGroup->pchContactName) == 0)
	{
		pInfo->pchContactName = NULL;
	}

	// Remove all special dialogs and offers
	FOR_EACH_IN_EARRAY(pGroup->eaSpecialDialogOverrideGroups, MDESpecialDialogOverrideGroup, pOverrideGroup)
	{
		if (pOverrideGroup && pOverrideGroup->pSpecialDialogGroup)
		{
			CERemoveSpecialDialogGroup(pOverrideGroup->pSpecialDialogGroup);
		}
	}
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY(pGroup->eaMissionOfferOverrideGroups, MDEMissionOfferOverrideGroup, pOverrideGroup)
	{
		if (pOverrideGroup && pOverrideGroup->pMissionOfferGroup)
		{
			CERemoveOffer(pOverrideGroup->pMissionOfferGroup);
		}
	}
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY(pGroup->eaImageMenuItemOverrideGroups, MDEImageMenuItemOverrideGroup, pOverrideGroup)
	{
		if (pOverrideGroup)
		{
			ImageMenuItemOverride* pOverride = eaRemoveVoid(pOverrideGroup->peaOverrides, pOverrideGroup->index);
			StructDestroySafe( parse_ImageMenuItemOverride, &pOverride );
		}
	}
	FOR_EACH_END;

	for (i = eaSize(&pDoc->eaContactGroups) - 1; i >= 0; --i)
	{
		if (pDoc->eaContactGroups[i] == pGroup)
		{			
			eaRemove(&pDoc->eaContactGroups, i);
		}
	}

	MDEFreeContactGroup(pGroup);

	// Update the UI
	MDEUpdateUI(pDoc);
}

static void MDEDeleteSubMission(MissionEditDoc *pDoc, MissionDef *pMission)
{
	int i;

	for(i=eaSize(&pDoc->pMission->subMissions)-1; i>=0; --i) {
		if (pDoc->pMission->subMissions[i] == pMission) {
			// Make sure the resource is checked out of Gimme
			if (!emDocIsEditable(&pDoc->emDoc, true)) {
				return;
			}

			// Rename references from the mission to nothing
			MDERenameSubMission(pDoc->pMission, pMission->name, "", true);

			// Destroy the mission group
			if (i < eaSize(&pDoc->eaSubMissionGroups)) {
				MDEFreeMissionGroup(pDoc->eaSubMissionGroups[i]);
				eaRemove(&pDoc->eaSubMissionGroups, i);
			}

			// Destroy the submission
			StructDestroy(parse_MissionDef, pMission);
			eaRemove(&pDoc->pMission->subMissions, i);

			MDEMissionChanged(pDoc, true);
			return;
		}
	}
}


void MDEDeleteSelectedMission(MissionEditDoc *pDoc)
{
	MissionDef *pMission = MDEGetSelectedMission(pDoc);
	MDEDeleteSubMission(pDoc, pMission);
}


static void MDEDeleteMenuCB(UIMenuItem *pItem, MDEMissionGroup *pGroup)
{
	MDEDeleteSubMission(pGroup->pDoc, pGroup->pMission);
}

static void MDEDeleteContactOverrideMenuCB(UIMenuItem *pItem, MDEContactGroup *pGroup)
{
	MDEDeleteContactGroup(pGroup->pDoc, pGroup);
}

static bool MDEIsMissionOnClipboard(EMEditorDoc *pEditorDoc)
{
	return (gMissionClipboard != NULL);
}


static void MDECloneSubMission(MissionEditDoc *pDoc, MissionDef *pMission)
{
	if (pMission) {
		// Make sure the resource is checked out of Gimme
		if (!emDocIsEditable(&pDoc->emDoc, true)) {
			return;
		}

		MDECreateSubMission(pDoc, pMission);
		MDEMissionChanged(pDoc, true);
	}
}


void MDECloneSelectedMission(MissionEditDoc *pDoc)
{
	MissionDef *pMission = MDEGetSelectedMission(pDoc);
	MDECloneSubMission(pDoc, pMission);
}


static void MDECloneMenuCB(UIMenuItem *pItem, MDEMissionGroup *pGroup)
{
	MDECloneSubMission(pGroup->pDoc, pGroup->pMission);
}


void MDECopySubMission(MissionEditDoc *pDoc, MissionDef *pMission)
{
	if (pMission) {
		if (gMissionClipboard) {
			StructDestroy(parse_MissionDef, gMissionClipboard);
		}
		gMissionClipboard = StructClone(parse_MissionDef, pMission);
	}
}


void MDECopySelectedMission(MissionEditDoc *pDoc)
{
	MissionDef *pMission = MDEGetSelectedMission(pDoc);
	MDECopySubMission(pDoc, pMission);
}


static void MDECopyMenuCB(UIMenuItem *pItem, MDEMissionGroup *pGroup)
{
	MDECopySubMission(pGroup->pDoc, pGroup->pMission);
}


void MDEPasteMission(MissionEditDoc *pDoc)
{
	if (gMissionClipboard) {
		// Make sure the resource is checked out of Gimme
		if (!emDocIsEditable(&pDoc->emDoc, true)) {
			return;
		}

		MDECreateSubMission(pDoc, gMissionClipboard);
		MDEMissionChanged(pDoc, true);
	}
}


static bool MDEIsSelectedMissionTemplated(void *unused)
{
	MissionEditDoc *pDoc = MDEGetActiveDoc();
	if (pDoc) {
		MissionDef *pMission = MDEGetSelectedMission(pDoc);
		return (pMission && (pMission->missionTemplate != NULL));
	} else {
		return false;
	}
}


static void MDERemoveTemplate(MissionEditDoc *pDoc, MissionDef *pMission)
{
	// If there's a template remove it
	if (pMission && pMission->missionTemplate) {
		// Make sure the resource is checked out of Gimme
		if (!emDocIsEditable(&pDoc->emDoc, true)) {
			return;
		}

		StructDestroy(parse_MissionTemplate, pMission->missionTemplate);
		pMission->missionTemplate = NULL;

		MDEMissionChanged(pDoc, true);
	}
}


void MDESelectedRemoveTemplate(MissionEditDoc *pDoc)
{
	MissionDef *pMission = MDEGetSelectedMission(pDoc);
	MDERemoveTemplate(pDoc, pMission);
}


static void MDERemoveTemplateMenuCB(UIMenuItem *pItem, MDEMissionGroup *pGroup)
{
	MDERemoveTemplate(pGroup->pDoc, pGroup->pMission);
}


static void MDEAddCondSuccessExprCB(UIButton *pButton, MDEMissionGroup *pGroup)
{
	MissionEditCond *pCond;

	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	// Add a new cond
	pCond = StructCreate(parse_MissionEditCond);
	pCond->type = MissionCondType_Expression;
	eaPush(&pGroup->pMission->meSuccessCond->subConds, pCond);

	// Update the UI
	MDEMissionChanged(pGroup->pDoc, true);
}


static void MDEAddCondSuccessObjCB(UIButton *pButton, MDEMissionGroup *pGroup)
{
	MissionEditCond *pCond;

	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	// Add a new cond
	pCond = StructCreate(parse_MissionEditCond);
	pCond->type = MissionCondType_Objective;
	eaPush(&pGroup->pMission->meSuccessCond->subConds, pCond);

	// Update the UI
	MDEMissionChanged(pGroup->pDoc, true);
}


static void MDEAddCondFailureExprCB(UIButton *pButton, MDEMissionGroup *pGroup)
{
	MissionEditCond *pCond;

	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	// Add a new cond
	pCond = StructCreate(parse_MissionEditCond);
	pCond->type = MissionCondType_Expression;
	eaPush(&pGroup->pMission->meFailureCond->subConds, pCond);

	// Update the UI
	MDEMissionChanged(pGroup->pDoc, true);
}


static void MDEAddCondFailureObjCB(UIButton *pButton, MDEMissionGroup *pGroup)
{
	MissionEditCond *pCond;

	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	// Add a new cond
	pCond = StructCreate(parse_MissionEditCond);
	pCond->type = MissionCondType_Objective;
	eaPush(&pGroup->pMission->meFailureCond->subConds, pCond);

	// Update the UI
	MDEMissionChanged(pGroup->pDoc, true);
}

static void MDEAddCondResetExprCB(UIButton *pButton, MDEMissionGroup *pGroup)
{
	MissionEditCond *pCond;

	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	// Add a new cond
	pCond = StructCreate(parse_MissionEditCond);
	pCond->type = MissionCondType_Expression;
	eaPush(&pGroup->pMission->meResetCond->subConds, pCond);

	// Update the UI
	MDEMissionChanged(pGroup->pDoc, true);
}

static void MDERemoveCondCB(UIButton *pButton, MDECondGroup *pGroup)
{
	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pGroup->pMissionGroup->pDoc->emDoc, true)) {
		return;
	}

	// Remove the drop
	StructDestroy(parse_MissionEditCond, (*pGroup->peaConds)[pGroup->index]);
	eaRemove(pGroup->peaConds, pGroup->index);

	// Update the UI
	MDEMissionChanged(pGroup->pMissionGroup->pDoc, true);
}

static void MDECondWhenSuccessCB(UIComboBox *pCombo, MDEMissionGroup *pGroup)
{
	int eValue = ui_ComboBoxGetSelected(pCombo);

	if (eValue != pGroup->pMission->meSuccessCond->type) {
		// Make sure the resource is checked out of Gimme
		if (!emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
			return;
		}

		// Change the value
		pGroup->pMission->meSuccessCond->type = eValue;

		// Update the UI
		MDEMissionChanged(pGroup->pDoc, true);
	}
}


static void MDECondWhenFailureCB(UIComboBox *pCombo, MDEMissionGroup *pGroup)
{
	int eValue = ui_ComboBoxGetSelected(pCombo);

	if (eValue != pGroup->pMission->meFailureCond->type) {
		// Make sure the resource is checked out of Gimme
		if (!emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
			return;
		}

		// Change the value
		pGroup->pMission->meFailureCond->type = eValue;

		// Update the UI
		MDEMissionChanged(pGroup->pDoc, true);
	}
}


static void MDEAddStartActionCB(UIButton *pButton, MDEMissionGroup *pGroup)
{
	WorldGameActionProperties *pAction;

	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	// Add a new group
	pAction = StructCreate(parse_WorldGameActionProperties);
	pAction->eActionType = WorldGameActionType_GrantSubMission;
	eaPush(&pGroup->pMission->ppOnStartActions, pAction);

	// Update the UI
	MDEMissionChanged(pGroup->pDoc, true);
}


static void MDEAddSuccessActionCB(UIButton *pButton, MDEMissionGroup *pGroup)
{
	WorldGameActionProperties *pAction;

	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	// Add a new group
	pAction = StructCreate(parse_WorldGameActionProperties);
	pAction->eActionType = WorldGameActionType_GrantSubMission;
	eaPush(&pGroup->pMission->ppSuccessActions, pAction);

	// Update the UI
	MDEMissionChanged(pGroup->pDoc, true);
}


static void MDEAddFailureActionCB(UIButton *pButton, MDEMissionGroup *pGroup)
{
	WorldGameActionProperties *pAction;

	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	// Add a new group
	pAction = StructCreate(parse_WorldGameActionProperties);
	pAction->eActionType = WorldGameActionType_GrantSubMission;
	eaPush(&pGroup->pMission->ppFailureActions, pAction);

	// Update the UI
	MDEMissionChanged(pGroup->pDoc, true);
}


static void MDEAddReturnActionCB(UIButton *pButton, MDEMissionGroup *pGroup)
{
	WorldGameActionProperties *pAction;

	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	// Add a new group
	// Default this one to TakeItem, since that's probably most common for Return actions
	pAction = StructCreate(parse_WorldGameActionProperties);
	pAction->eActionType  = WorldGameActionType_TakeItem;
	eaPush(&pGroup->pMission->ppOnReturnActions, pAction);

	// Update the UI
	MDEMissionChanged(pGroup->pDoc, true);
}


static void MDEAddNumericScaleCB(UIButton *pButton, MDEMissionGroup *pGroup)
{
	MissionNumericScale *pScale;

	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	// Add a new drop
	pScale = StructCreate(parse_MissionNumericScale);
	eaPush(&pGroup->pMission->params->eaNumericScales, pScale);

	// Update the UI
	MDEMissionChanged(pGroup->pDoc, true);
}


static void MDERemoveNumericScaleCB(UIButton *pButton, MDENumericScaleGroup *pGroup)
{
	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pGroup->pMissionGroup->pDoc->emDoc, true)) {
		return;
	}

	// Remove the drop
	StructDestroy(parse_MissionNumericScale, (*pGroup->peaNumericScales)[pGroup->index]);
	eaRemove(pGroup->peaNumericScales, pGroup->index);

	// Update the UI
	MDEMissionChanged(pGroup->pMissionGroup->pDoc, true);
}

static void MDEAddDropCB(UIButton *pButton, MDEMissionGroup *pGroup)
{
	MissionDrop *pDrop;

	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	// Add a new drop
	pDrop = StructCreate(parse_MissionDrop);
	pDrop->type = MissionDropTargetType_Critter;
	eaPush(&pGroup->pMission->params->missionDrops, pDrop);

	// Set the map to be the current map
	pDrop->pchMapName = zmapInfoGetPublicName(NULL);

	// Update the UI
	MDEMissionChanged(pGroup->pDoc, true);
}


static void MDERemoveDropCB(UIButton *pButton, MDEDropGroup *pGroup)
{
	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pGroup->pMissionGroup->pDoc->emDoc, true)) {
		return;
	}

	// Remove the drop
	StructDestroy(parse_MissionDrop, (*pGroup->peaDrops)[pGroup->index]);
	eaRemove(pGroup->peaDrops, pGroup->index);

	// Update the UI
	MDEMissionChanged(pGroup->pMissionGroup->pDoc, true);
}

static void MDEContactWindowRightClickCB(UIWindow *pWindow, MDEContactGroup *pGroup)
{
	if (pGroup->pContextMenu == NULL) 
	{
		pGroup->pContextMenu = ui_MenuCreate(NULL);
	}
	else
	{
		ui_MenuClearAndFreeItems(pGroup->pContextMenu);
	}

	ui_MenuAppendItems(pGroup->pContextMenu, 
		ui_MenuItemCreate("Delete Contact Override", UIMenuCallback, MDEDeleteContactOverrideMenuCB, pGroup, NULL),
		NULL);

	// Show the menu
	ui_MenuPopupAtCursor(pGroup->pContextMenu);
}

static void MDEWindowRightClickCB(UIWindow *pWindow, MDEMissionGroup *pGroup)
{
	if (!pGroup->pContextMenu) {
		pGroup->pContextMenu = ui_MenuCreate(NULL);
	}else{
		ui_MenuClearAndFreeItems(pGroup->pContextMenu);
	}

	ui_MenuAppendItems(pGroup->pContextMenu, 
		ui_MenuItemCreate("Copy Mission", UIMenuCallback, MDECopyMenuCB, pGroup, NULL),
		ui_MenuItemCreate("Clone Mission", UIMenuCallback, MDECloneMenuCB, pGroup, NULL),
		ui_MenuItemCreate("Delete Mission", UIMenuCallback, MDEDeleteMenuCB, pGroup, NULL),
		ui_MenuItemCreate("Un-Template Mission", UIMenuCallback, MDERemoveTemplateMenuCB, pGroup, NULL),
		NULL);

	// Enable delete only if not main mission
	pGroup->pContextMenu->items[2]->active = (pGroup->pMission != pGroup->pDoc->pMission);
	
	// Enable untemplate only if templated
	pGroup->pContextMenu->items[3]->active = (pGroup->pMission->missionTemplate != NULL);

	// Show the menu
	ui_MenuPopupAtCursor(pGroup->pContextMenu);
}


// This is called whenever any mission data changes to do cleanup
static void MDEMissionChanged(MissionEditDoc *pDoc, bool bUndoable)
{
	if (!pDoc->bIgnoreFieldChanges) {
		MDEUpdateVersionNumber(pDoc);
		MDEUpdateDoNotUncompleteFlag(pDoc->pMission);
		MDEUpdateDisplay(pDoc);

		if (bUndoable) {
			MDEUndoData *pData = calloc(1, sizeof(MDEUndoData));
			pData->pPreMission = pDoc->pNextUndoMission;
			pData->pPostMission = StructClone(parse_MissionDef, pDoc->pMission);
			EditCreateUndoCustom(pDoc->emDoc.edit_undo_stack, MDEMissionUndoCB, MDEMissionRedoCB, MDEMissionUndoFreeCB, pData);
			pDoc->pNextUndoMission = StructClone(parse_MissionDef, pDoc->pMission);
		}
	}
}

// This is called by MEField prior to allowing an edit
static bool MDEFieldPreChangeCB(MEField *pField, bool bFinished, MissionEditDoc *pDoc)
{
	// Make sure the resource is checked out of Gimme
	return emDocIsEditable(&pDoc->emDoc, true);
}

static bool MDESpecialDialogNameIsUnique(SA_PARAM_NN_VALID MissionDef *pMissionDef,
	SA_PARAM_NN_STR const char *pchContactName,
	SA_PARAM_NN_STR const char *pchDialogName,
	SA_PARAM_OP_VALID ContactMissionOffer *pMissionOfferToSkip, 
	SA_PARAM_OP_VALID SpecialDialogBlock *pSpecialDialogBlockToSkip)
{
	FOR_EACH_IN_EARRAY_FORWARDS(pMissionDef->ppSpecialDialogOverrides, SpecialDialogOverride, pOverride)
	{
		if (pOverride->pcContactName ==  pchContactName &&
			pOverride->pSpecialDialog->name == pchDialogName &&
			(pSpecialDialogBlockToSkip == NULL || pOverride->pSpecialDialog != pSpecialDialogBlockToSkip))
		{
			return false;
		}
	}
	FOR_EACH_END

	FOR_EACH_IN_EARRAY_FORWARDS(pMissionDef->ppMissionOfferOverrides, MissionOfferOverride, pOverride)
	{
		if (pOverride->pcContactName ==  pchContactName &&
			pOverride->pMissionOffer->pchSpecialDialogName == pchDialogName &&
			(pMissionOfferToSkip == NULL || pOverride->pMissionOffer != pMissionOfferToSkip))
		{
			return false;
		}
	}
	FOR_EACH_END

	return true;
}

static void MDEUpdateTargetsAfterSpecialDialogNameChange(MissionDef *pMissionDef,
	const char *pchContactName,
	const char *pchOldName,
	const char *pchNewName)
{
	pchOldName = CEGetPooledSpecialDialogNameByMission(pchOldName, pMissionDef->name);
	pchNewName = CEGetPooledSpecialDialogNameByMission(pchNewName, pMissionDef->name);

	// Update all actions pointing to this special dialog block
	FOR_EACH_IN_EARRAY_FORWARDS(pMissionDef->ppSpecialDialogOverrides, SpecialDialogOverride, pOverride)
	{
		if (pOverride->pcContactName == pchContactName)
		{
			FOR_EACH_IN_EARRAY_FORWARDS(pOverride->pSpecialDialog->dialogActions, SpecialDialogAction, pSpecialDialogAction)
			{
				if (pSpecialDialogAction && pSpecialDialogAction->dialogName &&
					pSpecialDialogAction->dialogName == pchOldName)
				{
					pSpecialDialogAction->dialogName = pchNewName;
				}
			}
			FOR_EACH_END
		}
	}
	FOR_EACH_END

	FOR_EACH_IN_EARRAY_FORWARDS(pMissionDef->ppMissionOfferOverrides, MissionOfferOverride, pOverride)
	{
		if (pOverride->pcContactName == pchContactName)
		{
			if (pOverride->pMissionOffer->pchAcceptTargetDialog && pOverride->pMissionOffer->pchAcceptTargetDialog == pchOldName)
			{
				pOverride->pMissionOffer->pchAcceptTargetDialog = pchNewName;
			}
			if (pOverride->pMissionOffer->pchDeclineTargetDialog && pOverride->pMissionOffer->pchDeclineTargetDialog == pchOldName)
			{
				pOverride->pMissionOffer->pchDeclineTargetDialog = pchNewName;
			}
			if (pOverride->pMissionOffer->pchRewardAcceptTargetDialog && pOverride->pMissionOffer->pchRewardAcceptTargetDialog == pchOldName)
			{
				pOverride->pMissionOffer->pchRewardAcceptTargetDialog = pchNewName;
			}
			if (pOverride->pMissionOffer->pchRewardChooseTargetDialog && pOverride->pMissionOffer->pchRewardChooseTargetDialog == pchOldName)
			{
				pOverride->pMissionOffer->pchRewardChooseTargetDialog = pchNewName;
			}
			if (pOverride->pMissionOffer->pchRewardAbortTargetDialog && pOverride->pMissionOffer->pchRewardAbortTargetDialog == pchOldName)
			{
				pOverride->pMissionOffer->pchRewardAbortTargetDialog = pchNewName;
			}
		}
	}
	FOR_EACH_END
}

// Handles the special dialog name change by updating all action targets
static void MDEHandleSpecialDialogNameChange(SA_PARAM_NN_VALID SpecialDialogOverride *pOverride, SA_PARAM_NN_VALID SpecialDialogBlock *pModifiedSpecialDialogBlock, SA_PARAM_OP_STR const char *pchOldName)
{
	MissionEditDoc *pActiveDoc = MDEGetActiveDoc();
	if (pActiveDoc && pModifiedSpecialDialogBlock)
	{
		bool bHasUniqueName = MDESpecialDialogNameIsUnique(pActiveDoc->pMission, pOverride->pcContactName, pModifiedSpecialDialogBlock->name, NULL, pModifiedSpecialDialogBlock);

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
			MDEUpdateTargetsAfterSpecialDialogNameChange(pActiveDoc->pMission, pOverride->pcContactName, pchOldName, pModifiedSpecialDialogBlock->name);
		}
	}	
}

// Handles the special dialog name change for the mission offer by updating all action targets
static void MDEHandleSpecialDialogNameChangeForMissionOffer(SA_PARAM_NN_VALID MissionOfferOverride *pOverride, SA_PARAM_NN_VALID ContactMissionOffer *pMissionOffer, SA_PARAM_OP_STR const char *pchOldName)
{
	MissionEditDoc *pActiveDoc = MDEGetActiveDoc();
	if (pActiveDoc && pMissionOffer)
	{
		bool bHasUniqueName = pMissionOffer->pchSpecialDialogName ? MDESpecialDialogNameIsUnique(pActiveDoc->pMission, pOverride->pcContactName, pMissionOffer->pchSpecialDialogName, pMissionOffer, NULL) : true;

		if (!bHasUniqueName)
		{
			// Revert to the old name
			pMissionOffer->pchSpecialDialogName = allocAddString(pchOldName);

			emStatusPrintf("Duplicate special dialog names are not allowed!");
		}
		else if (pchOldName || pMissionOffer->pchSpecialDialogName)
		{
			MDEUpdateTargetsAfterSpecialDialogNameChange(pActiveDoc->pMission, pOverride->pcContactName, pchOldName, pMissionOffer->pchSpecialDialogName);
		}
	}
}

static S32 MDEFindSpecialDialogOverrideBySpecialDialogBlock(SA_PARAM_NN_VALID MissionDef *pDef, SA_PARAM_NN_VALID SpecialDialogBlock *pSpecialDialogBlockToLookFor)
{
	S32 i;
	for (i = 0; i < eaSize(&pDef->ppSpecialDialogOverrides); i++)
	{
		if (pDef->ppSpecialDialogOverrides[i]->pSpecialDialog == pSpecialDialogBlockToLookFor)
		{
			return i;
		}
	}

	return -1;
}

static S32 MDEFindMissionOfferOverrideByContactMissionOffer(SA_PARAM_NN_VALID MissionDef *pDef, SA_PARAM_NN_VALID ContactMissionOffer *pContactMissionOfferToLookFor)
{
	S32 i;
	for (i = 0; i < eaSize(&pDef->ppMissionOfferOverrides); i++)
	{
		if (pDef->ppMissionOfferOverrides[i]->pMissionOffer == pContactMissionOfferToLookFor)
		{
			return i;
		}
	}

	return -1;
}


// This is called when an MEField is changed
static void MDEFieldChangedCB(MEField *pField, bool bFinished, MissionEditDoc *pDoc)
{
	if (!pDoc->bIgnoreFieldChanges)
	{
		// Is the user changing the name of a special dialog
		if (pField && bFinished &&
			stricmp(pField->pchFieldName, "Name") == 0 &&
			pField->pTable == parse_SpecialDialogBlock &&
			pDoc->pNextUndoMission->ppSpecialDialogOverrides)
		{
			S32 iSpecialDialogOverrideIndex = MDEFindSpecialDialogOverrideBySpecialDialogBlock(pDoc->pMission, pField->pNew);
			SpecialDialogOverride *pOverride;
			if (pDoc->pNextUndoMission && iSpecialDialogOverrideIndex >= 0 && iSpecialDialogOverrideIndex < eaSize(&pDoc->pNextUndoMission->ppSpecialDialogOverrides))
			{
				pOverride = pDoc->pMission->ppSpecialDialogOverrides[iSpecialDialogOverrideIndex];
				MDEHandleSpecialDialogNameChange(pOverride, pField->pNew, pDoc->pNextUndoMission->ppSpecialDialogOverrides[iSpecialDialogOverrideIndex]->pSpecialDialog->name);
			}
		}
		// Is the user changing the special dialog name of a mission offer
		else if (pField && bFinished &&
			stricmp(pField->pchFieldName, "SpecialDialogName") == 0 &&
			pField->pTable == parse_ContactMissionOffer &&
			pDoc->pNextUndoMission->ppMissionOfferOverrides)
		{
			S32 iMissionOfferOverrideIndex = MDEFindMissionOfferOverrideByContactMissionOffer(pDoc->pMission, pField->pNew);
			MissionOfferOverride *pOverride;
			if (pDoc->pNextUndoMission && iMissionOfferOverrideIndex >= 0 && iMissionOfferOverrideIndex < eaSize(&pDoc->pNextUndoMission->ppMissionOfferOverrides))
			{
				pOverride = pDoc->pMission->ppMissionOfferOverrides[iMissionOfferOverrideIndex];
				MDEHandleSpecialDialogNameChangeForMissionOffer(pOverride, pField->pNew, pDoc->pNextUndoMission->ppMissionOfferOverrides[iMissionOfferOverrideIndex]->pMissionOffer->pchSpecialDialogName);
			}
		}
	}

	MDEMissionChanged(pDoc, bFinished);
}


static void MDESetScopeCB(MEField *pField, bool bFinished, MissionEditDoc *pDoc)
{
	if (!pDoc->bIgnoreFilenameChanges) {
		// Update the filename appropriately
		resFixFilename(g_MissionDictionary, pDoc->pMission->name, pDoc->pMission);
	}

	// Call on to do regular updates
	MDEFieldChangedCB(pField, bFinished, pDoc);
}


static void MDESetNameCB(MEField *pField, bool bFinished, MDEMissionGroup *pGroup)
{
	MEFieldFixupNameString(pField, &pGroup->pMission->name);

	// When the name changes, change the title of the local window
	if (pGroup->pMission->name && pGroup->pMission->name[0]) {
		ui_WindowSetTitle(pGroup->pWindow, pGroup->pMission->name);
	} else {
		ui_WindowSetTitle(pGroup->pWindow, "Unnamed");
	}

	if (pGroup == pGroup->pDoc->pMainMissionGroup) {
		// If this is a change to the main group, also update main window
		if (pGroup->pMission->name && pGroup->pMission->name[0]) {
			ui_WindowSetTitle(pGroup->pDoc->pMainWindow, pGroup->pMission->name);
		} else {
			ui_WindowSetTitle(pGroup->pDoc->pMainWindow, "Unnamed");
		}

		// Make sure editor manager picks up the new mission name if the name changed
		sprintf(pGroup->pDoc->emDoc.doc_name, "%s", pGroup->pDoc->pMission->name);
		sprintf(pGroup->pDoc->emDoc.doc_display_name, "%s", pGroup->pDoc->pMission->name);
		pGroup->pDoc->emDoc.name_changed = 1;

		if (!pGroup->pDoc->bIgnoreFieldChanges) {
			if (bFinished) {
				// Make sure name is unique
				if (!pGroup->pMission->name) {
					// Name cannot be empty
					Alertf("Cannot rename mission to an empty string");
					pGroup->pMission->name = (char*)allocAddString(pGroup->pcName);
				} else if (!MDEIsNameUnique(pGroup->pDoc->pMission, pGroup->pMission, pGroup->pMission->name)) {
					// Name is not unique, so revert change
					Alertf("Cannot rename mission to '%s' because that name is already in use as a sub-mission name", pGroup->pMission->name);
					pGroup->pMission->name = (char*)allocAddString(pGroup->pcName);
				} else {
					int i;

					// Update ref strings used in messages
					missiondef_CreateRefStringsRecursive(pGroup->pMission, NULL);

					// Update references on all sub-missions
					for(i=eaSize(&pGroup->pMission->subMissions)-1; i>=0; --i) {
						MissionDef *pSubMission = pGroup->pMission->subMissions[i];
						SET_HANDLE_FROM_STRING(g_MissionDictionary, pGroup->pDoc->pMission->name, pSubMission->parentDef);
					}
				}
			}

			// Call the scope function to avoid duplicating logic
			pGroup->pDoc->bSkipNameCapture = !bFinished;
			MDESetScopeCB(pField, bFinished, pGroup->pDoc);
			pGroup->pDoc->bSkipNameCapture = false;
		}
	} else if (!pGroup->pDoc->bIgnoreFieldChanges) {
		if (bFinished) {
			// Make sure name is unique
			if (!pGroup->pMission->name) {
				// Name cannot be empty
				Alertf("Cannot rename sub-mission to an empty string");
				pGroup->pMission->name = (char*)allocAddString(pGroup->pcName);
			} else if (!MDEIsNameUnique(pGroup->pDoc->pMission, pGroup->pMission, pGroup->pMission->name)) {
				// Name is not unique, so revert change
				Alertf("Cannot rename sub-mission to '%s' because that name is already in use", pGroup->pMission->name);
				pGroup->pMission->name = (char*)allocAddString(pGroup->pcName);
			} else {
				// Update ref strings used in messages
				missiondef_CreateRefStringsRecursive(pGroup->pMission, pGroup->pDoc->pMission);

				// Rename the mission
				MDERenameSubMission(pGroup->pDoc->pMission, pGroup->pcName, pGroup->pMission->name, true);
			}
		}

		// Call to do UI updates
		pGroup->pDoc->bSkipNameCapture = !bFinished;
		MDEFieldChangedCB(pField, bFinished, pGroup->pDoc);
		pGroup->pDoc->bSkipNameCapture = false;
	}
}


static void MDEToggleLinesCB(UIButton *pButton, void *unused)
{
	int i;

	// Toggle it
	gShowLines = !gShowLines;

	// Update all open editor displays
	for(i=eaSize(&s_MissionEditor.open_docs)-1; i>=0; --i) {
		MDEUpdateDisplay((MissionEditDoc*)s_MissionEditor.open_docs[i]);
	}
}

static void MDERefreshAddButtonPositions(SA_PARAM_NN_VALID MDEContactGroup *pGroup)
{
	ui_WidgetSetPosition(UI_WIDGET(pGroup->pAddSpecialDialogOverrideButton), X_OFFSET_BASE, CONTACT_GROUP_EXPANDER_GROUP_Y_OFFSET + pGroup->pExGroup->totalHeight + 10.f);
	ui_WidgetSetPosition(UI_WIDGET(pGroup->pAddMissionOfferOverrideButton), X_OFFSET_BASE + 200.f, CONTACT_GROUP_EXPANDER_GROUP_Y_OFFSET + pGroup->pExGroup->totalHeight + 10.f);
	ui_WidgetSetPosition(UI_WIDGET(pGroup->pAddActionBlockOverrideButton), X_OFFSET_BASE, CONTACT_GROUP_EXPANDER_GROUP_Y_OFFSET + 28 + pGroup->pExGroup->totalHeight + 10.f);
	ui_WidgetSetPosition(UI_WIDGET(pGroup->pAddImageMenuItemOverrideButton), X_OFFSET_BASE + 200.f, CONTACT_GROUP_EXPANDER_GROUP_Y_OFFSET + 28 + pGroup->pExGroup->totalHeight + 10.f);
}

static void MDEMissionOfferExpandChangeCB(UIExpander *pExpander, MDEMissionOfferOverrideGroup *pGroup)
{
	MissionOfferOverride *pOverride;
	MissionDef *pMissionDef;
	devassert(pGroup->index >= 0 && pGroup->index < eaSize(pGroup->peaOverrides));
	pOverride = (*pGroup->peaOverrides)[pGroup->index];
	pMissionDef = pOverride && pOverride->pMissionOffer ? GET_REF(pOverride->pMissionOffer->missionDef) : NULL;

	if (pMissionDef)
	{
		char *estrPrefName = NULL;
		estrStackCreate(&estrPrefName);
		estrConcatf(&estrPrefName, "MissionOfferOverrideExpanderPref_%s_%s", pGroup->pMissionOfferGroup->pchContactName, pMissionDef->name);

		EditorPrefStoreInt(MISSION_EDITOR, "Expander", estrPrefName, ui_ExpanderIsOpened(pExpander));

		estrDestroy(&estrPrefName);
	}

	MDERefreshAddButtonPositions(pGroup->pContactGroup);
}

static void MDESpecialExpandChangeCB(UIExpander *pExpander, MDESpecialDialogOverrideGroup *pGroup)
{
	SpecialDialogOverride *pOverride;
	devassert(pGroup->index >= 0 && pGroup->index < eaSize(pGroup->peaOverrides));
	pOverride = (*pGroup->peaOverrides)[pGroup->index];	

	if (pOverride->pSpecialDialog->name && pOverride->pSpecialDialog->name[0])
	{
		char *estrPrefName = NULL;
		estrStackCreate(&estrPrefName);
		estrConcatf(&estrPrefName, "SpecialDialogOverrideExpanderPref_%s_%s", pGroup->pSpecialDialogGroup->pchContactName, pOverride->pSpecialDialog->name);

		EditorPrefStoreInt(MISSION_EDITOR, "Expander", estrPrefName, ui_ExpanderIsOpened(pExpander));

		estrDestroy(&estrPrefName);
	}

	MDERefreshAddButtonPositions(pGroup->pContactGroup);
}

static void MDEExpandChangeCB(UIExpander *pExpander, char *pcPrefName)
{
	// Save expander size
	EditorPrefStoreInt(MISSION_EDITOR, "Expander", pcPrefName, ui_ExpanderIsOpened(pExpander));
}


static void MDEExpandReflowCB(UIExpanderGroup *pExGroup, MDEMissionGroup *pGroup)
{
	// Force window to fit the expander group
	ui_WidgetSetHeight(UI_WIDGET(pGroup->pWindow), pExGroup->totalHeight + pExGroup->widget.y + 3);
}

static void MDEExpandContactReflowCB(UIExpanderGroup *pExGroup, MDEContactGroup *pGroup)
{
	// Force window to fit the expander group
	ui_WidgetSetHeight(UI_WIDGET(pGroup->pWindow), pExGroup->totalHeight + pExGroup->widget.y + 35 + 28); //TODO REPLACE WITH CONSTANT
}


//---------------------------------------------------------------------------------------------------
// Layout Logic
//---------------------------------------------------------------------------------------------------


static void MDEFreeLayoutNodes(MDELayoutNode ***peaNodes)
{
	int i;

	for(i=eaSize(peaNodes)-1; i>=0; --i) {
		MDELayoutNode *pNode = (*peaNodes)[i];
		MDEFreeLayoutNodes(&pNode->eaChildren);
		eaDestroy(&pNode->eaChildren);
		free(pNode);
	}
	eaDestroy(peaNodes);
}


static bool MDELayoutFindNode(MDELayoutNode ***peaSearchNodes, MDELayoutNode *pCurrentNode, int iDepth, MDEMissionGroup *pGroupToFind, MDELayoutNode **ppChildNode, MDELayoutNode **ppParentNode, int *piChildDepth)
{
	int i;

	for(i=eaSize(peaSearchNodes)-1; i>=0; --i) {
		MDELayoutNode *pNode = (*peaSearchNodes)[i];

		// See if this is the one
		if (pNode->pGroup == pGroupToFind) {
			if (ppParentNode) {
				*ppParentNode = pCurrentNode;
			}
			if (ppChildNode) {
				*ppChildNode = pNode;
			}
			if (piChildDepth) {
				*piChildDepth = iDepth;
			}
			return true;
		}

		// Recurse
		if (MDELayoutFindNode(&pNode->eaChildren, pNode, iDepth+1, pGroupToFind, ppChildNode, ppParentNode, piChildDepth)) {
			return true;
		}
	}

	return false;
}

static void MDELayoutCheckChild(MissionEditDoc *pDoc, MDELayoutNode ***peaAllNodes, MDEMissionGroup ***peaGroups, MDELayoutNode *pCurrentNode, int iDepth, MDEMissionGroup *pOtherGroup, MDELayoutType eLayoutType)
{
	MDELayoutNode *pChildNode;
	MDELayoutNode *pParentNode;
	int iChildDepth;
	int i;

	// Look to see if node is not yet in use
	for(i=eaSize(peaGroups)-1; i>=0; --i) {
		if ((*peaGroups)[i] == pOtherGroup) {
			// Create a child node
			MDELayoutNode *pNode = calloc(1, sizeof(MDELayoutNode));
			pNode->pGroup = pOtherGroup;
			pNode->eType = eLayoutType;
			eaPush(&pCurrentNode->eaChildren, pNode);
			eaRemove(peaGroups, i);

			// Recurse on the child node
			MDELayoutAddChildNodes(pDoc, peaAllNodes, peaGroups, pNode, iDepth+1, false);
			return;
		}
	}

	// Node is apparently already in use so figure out depth
	// If depth is less than current and child is not also a parent, move the child over here
	if (MDELayoutFindNode(peaAllNodes, NULL, 0, pOtherGroup, &pChildNode, &pParentNode, &iChildDepth)) {
		if ((iChildDepth < iDepth) && (!pParentNode || !MDELayoutFindNode(&pParentNode->eaChildren, NULL, 0, pCurrentNode->pGroup, NULL, NULL, NULL))) {
			eaPush(&pCurrentNode->eaChildren, pChildNode);
			if (!pParentNode)
				eaFindAndRemove(peaAllNodes, pChildNode);
			else
			{
				for(i=eaSize(&pParentNode->eaChildren)-1; i>=0; --i) {
					if (pParentNode->eaChildren[i] == pChildNode) {
						eaRemove(&pParentNode->eaChildren, i);
						break;
					}
				}
			}
		} else {
			pChildNode->eType |= eLayoutType;
		}
	}
}


static void MDELayoutCheckActions(MissionEditDoc *pDoc, MDELayoutNode ***peaAllNodes, MDEMissionGroup ***peaGroups, MDELayoutNode *pCurrentNode, int iDepth, WorldGameActionProperties **eaActions, MDELayoutType eLayoutType)
{
	int i, j;

	for(i=0; i<eaSize(&eaActions); ++i) {
		WorldGameActionProperties *pAction = eaActions[i];
		if (eaActions[i]->eActionType == WorldGameActionType_GrantSubMission) {
			const char *pcName = pAction->pGrantSubMissionProperties->pcSubMissionName;
			for(j=eaSize(&pDoc->eaSubMissionGroups)-1; j>=0; --j) {
				MDEMissionGroup *pOtherGroup = pDoc->eaSubMissionGroups[j];
				if (pOtherGroup->pMission->name && pcName && (stricmp(pcName, pOtherGroup->pMission->name) == 0)) {
					MDELayoutCheckChild(pDoc, peaAllNodes, peaGroups, pCurrentNode, iDepth, pOtherGroup, eLayoutType);
					break;
				}
			}
		}
	}
}


static void MDELayoutAddChildNodes(MissionEditDoc *pDoc, MDELayoutNode ***peaAllNodes, MDEMissionGroup ***peaGroups, MDELayoutNode *pCurrentNode, int iDepth, bool bIsRoot)
{
	// Find all grants
	MDELayoutCheckActions(pDoc, peaAllNodes, peaGroups, pCurrentNode, iDepth, pCurrentNode->pGroup->pMission->ppOnStartActions, LayoutType_Start);
	if (!bIsRoot) {
		MDELayoutCheckActions(pDoc, peaAllNodes, peaGroups, pCurrentNode, iDepth, pCurrentNode->pGroup->pMission->ppSuccessActions, LayoutType_Success);
		MDELayoutCheckActions(pDoc, peaAllNodes, peaGroups, pCurrentNode, iDepth, pCurrentNode->pGroup->pMission->ppFailureActions, LayoutType_Failure);
		MDELayoutCheckActions(pDoc, peaAllNodes, peaGroups, pCurrentNode, iDepth, pCurrentNode->pGroup->pMission->ppOnReturnActions, LayoutType_Return);
	}
}


static void MDELayoutPositionWindows(MDELayoutNode **eaNodes, F32 fWidth, F32 *pX, F32 *pY)
{
	F32 fChildX, fChildY;
	int i;

	for(i=0; i<eaSize(&eaNodes); ++i) {
		MDEMissionGroup *pGroup = eaNodes[i]->pGroup;

		// Position current window
		ui_WidgetSetPosition(UI_WIDGET(pGroup->pWindow), *pX, *pY);
		ui_WidgetSetWidth(UI_WIDGET(pGroup->pWindow), fWidth);

		// Recurse on its children
		fChildX = *pX + fWidth + X_OFFSET_SPACING;
		fChildY = *pY;
		MDELayoutPositionWindows(eaNodes[i]->eaChildren, fWidth, &fChildX, &fChildY);

		// Increment Y
		*pY = MAX(fChildY, *pY + pGroup->pWindow->widget.height);
		if (i != eaSize(&eaNodes)-1) {
			*pY += Y_OFFSET_SPACING;
		}
	}
}

// Lays out the contact windows
static F32 MDELayoutContactWindows(SA_PARAM_NN_VALID MissionEditDoc *pDoc, F32 fCurrentY)
{
	if (pDoc)
	{		
		F32 fRowBeginPos = 15.f;
		F32 fCurrentX = fRowBeginPos;
		F32 fMainWindowWidth = pDoc->pMainWindow->widget.width;
		F32 fMaxWinHeight = 0.f;
		bool bCreatedRow = false;

		fCurrentY += Y_OFFSET_SPACING;

		FOR_EACH_IN_EARRAY_FORWARDS(pDoc->eaContactGroups, MDEContactGroup, pContactGroup)
		{
			if (pContactGroup && pContactGroup->pWindow)
			{
				// Position current window
				ui_WidgetSetPosition(UI_WIDGET(pContactGroup->pWindow), fCurrentX, fCurrentY);

				// Store the max window height
				MAX1(fMaxWinHeight, pContactGroup->pWindow->widget.height);

				fCurrentX += pContactGroup->pWindow->widget.width + X_OFFSET_SPACING;

				bCreatedRow = false;

				if (fCurrentX >= fMainWindowWidth)
				{
					// Jump to a new line
					fCurrentX = fRowBeginPos;
					fCurrentY += fMaxWinHeight + Y_OFFSET_SPACING;

					fMaxWinHeight = 0.f;
					bCreatedRow = true;
				}
			}
		}
		FOR_EACH_END

		if (!bCreatedRow)
		{
			fCurrentY += fMaxWinHeight + Y_OFFSET_SPACING;
		}
	}

	return fCurrentY;
}

static F32 MDELayoutMissions(MissionEditDoc *pDoc)
{
	MDELayoutNode **eaNodes = NULL;
	MDELayoutNode *pNode;
	MDEMissionGroup **eaGroups = NULL;
	F32 x = 15, y = 15;
	F32 fWidth;

	// Copy set of groups
	eaPushEArray(&eaGroups, &pDoc->eaSubMissionGroups);

	// Make initial node
	pNode = calloc(1, sizeof(MDELayoutNode));
	pNode->pGroup = pDoc->pMainMissionGroup;
	eaPush(&eaNodes, pNode);

	// Recurse on main mission
	MDELayoutAddChildNodes(pDoc, &eaNodes, &eaGroups, pNode, 0, true);

	// Recurse on remaining sub-missions
	while(eaSize(&eaGroups) > 0) {
		// Make the node
		pNode = calloc(1, sizeof(MDELayoutNode));
		pNode->pGroup = eaGroups[0];
		eaPush(&eaNodes, pNode);
		eaRemove(&eaGroups, 0);

		MDELayoutAddChildNodes(pDoc, &eaNodes, &eaGroups, pNode, 0, false);
	}

	// At this point we have the layout structure
	// So do the actual layout
	fWidth = pDoc->pMainMissionGroup->pWindow->widget.width;
	MDELayoutPositionWindows(eaNodes, fWidth, &x, &y);

	// Cleanup
	MDEFreeLayoutNodes(&eaNodes);
	eaDestroy(&eaGroups);

	return y;
}


static void MDELayoutCB(UIButton *pButton, void *unused) 
{
	MissionEditDoc *pDoc = MDEGetActiveDoc();
	if (pDoc)
	{
		F32 y = MDELayoutMissions(pDoc);
		MDELayoutContactWindows(pDoc, y);
	}
}

static void MDEAudioExportCB(UIButton *pButton, void *unused) 
{
	MissionEditDoc *pDoc = MDEGetActiveDoc();
	if (pDoc)
	{
		MissionDef *pMissionDef = pDoc->pMission;

		if (pMissionDef)
		{
			char exportedFileName[CRYPTIC_MAX_PATH];
			sprintf(exportedFileName, "%s/export/contact_audio/mission/%s_%s.csv", 
				fileLocalDataDir(),
				pMissionDef->name,
				timeGetFilenameDateStringFromSecondsSince2000(timeSecondsSince2000() + timeLocalOffsetFromUTC()));

			// Make sure all sub directories are created
			if (makeDirectoriesForFile(exportedFileName))
			{
				// Try to open the file for writing, overwrite if it already exists
				FileWrapper *pFile = fopen(exportedFileName, "w");

				if (pFile)
				{					
					const char *pchExportDate = timeGetDateStringFromSecondsSince2000(timeSecondsSince2000() + timeLocalOffsetFromUTC());

					// Write the headers
					fprintf(pFile, "Date Exported, Contact Name, Gender, Mission Name, Dialog Text Type, Dialog Text, Audio File\n");

					FOR_EACH_IN_EARRAY_FORWARDS(pMissionDef->ppSpecialDialogOverrides, SpecialDialogOverride, pSpecialDialogOverride)
					{
						if (pSpecialDialogOverride->pSpecialDialog)
						{
							const char *pchContactName = pSpecialDialogOverride->pcContactName;

							// Special dialogs
							CEExportDialogBlocksToCSV(pFile, &pSpecialDialogOverride->pSpecialDialog->dialogBlock, "Special Dialog", pchExportDate, pchContactName, "");
						}
					}
					FOR_EACH_END

					// Close the file
					fclose(pFile);

					emStatusPrintf("Export succeeded. The file location is: %s", exportedFileName);
				}
			}
		}
	}
}


//---------------------------------------------------------------------------------------------------
// UI Update Logic
//---------------------------------------------------------------------------------------------------


static UILabel *MDECreateLabel(const char *pcText, char *pcTooltip, F32 x, F32 xPercent, F32 y, UIExpander *pExpander)
{
	UILabel *pLabel;

	pLabel = ui_LabelCreate(pcText, x, y);
	ui_WidgetSetTooltipString(UI_WIDGET(pLabel), pcTooltip);
	ui_LabelEnableTooltips(pLabel);
	ui_WidgetSetPositionEx(UI_WIDGET(pLabel), x, y, xPercent, 0, UITopLeft);
	ui_ExpanderAddChild(pExpander, pLabel);

	return pLabel;
}

static UILabel *MDERefreshLabel(UILabel *pLabel, const char *pcText, const char *pcTooltip, F32 x, F32 xPercent, F32 y, UIExpander *pExpander)
{
	if (!pLabel) {
		pLabel = ui_LabelCreate(pcText, x, y);
		ui_WidgetSetTooltipString(UI_WIDGET(pLabel), pcTooltip);
		ui_LabelEnableTooltips(pLabel);
		ui_WidgetSetPositionEx(UI_WIDGET(pLabel), x, y, xPercent, 0, UITopLeft);
		ui_ExpanderAddChild(pExpander, pLabel);
	} else {
		ui_LabelSetText(pLabel, pcText);
		ui_WidgetSetTooltipString(UI_WIDGET(pLabel), pcTooltip);
		ui_WidgetSetPositionEx(UI_WIDGET(pLabel), x, y, xPercent, 0, UITopLeft);
	}
	return pLabel;
}


static UIButton *MDEAddRewardEditButton(UIExpander *pExpander, F32 y, MDERewardData *pData)
{
	UIButton *pButton;

	pButton = ui_ButtonCreate("Edit", 3, y, MDERewardEditCB, pData);
	ui_WidgetSetWidth(UI_WIDGET(pButton), 60);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), 3, y, 0, 0, UITopRight);
	ui_ExpanderAddChild(pExpander, pButton);

	return pButton;
}


static void MDEUpdateLinesForActions(MDEMissionGroup *pGroup, WorldGameActionProperties **eaActions)
{
	int i, j;

	for(i=eaSize(&eaActions)-1; i>=0; --i) {
		if (eaActions[i]->eActionType == WorldGameActionType_GrantSubMission) {
			const char *pcName = eaActions[i]->pGrantSubMissionProperties->pcSubMissionName;
			for(j=eaSize(&pGroup->pDoc->eaSubMissionGroups)-1; j>=0; --j) {
				MDEMissionGroup *pOtherGroup = pGroup->pDoc->eaSubMissionGroups[j];
				if (pOtherGroup->pMission->name && pcName && (stricmp(pcName, pOtherGroup->pMission->name) == 0)) {
					++pGroup->iNumOutBoxes;
					++pOtherGroup->iNumInBoxes;
					if (eaSize(&pGroup->eaOutBoxes) < pGroup->iNumOutBoxes) {
						UIPairedBox *pBox = ui_PairedBoxCreate(ColorBlack);
						ui_WidgetSetDimensions(UI_WIDGET(pBox), 10, 10);
						ui_WidgetSetPositionEx(UI_WIDGET(pBox), 3, 5, 0, 0, UITopRight);
						ui_WindowAddChild(pGroup->pWindow, pBox);
						eaPush(&pGroup->eaOutBoxes, pBox);
					}
					if (eaSize(&pOtherGroup->eaInBoxes) < pOtherGroup->iNumInBoxes) {
						UIPairedBox *pBox = ui_PairedBoxCreate(ColorBlack);
						ui_WidgetSetDimensions(UI_WIDGET(pBox), 10, 10);
						ui_WidgetSetPositionEx(UI_WIDGET(pBox), 3, 5, 0, 0, UITopLeft);
						ui_WindowAddChild(pOtherGroup->pWindow, pBox);
						eaPush(&pOtherGroup->eaInBoxes, pBox);
					}
					ui_PairedBoxConnect(pGroup->eaOutBoxes[pGroup->iNumOutBoxes-1],
										pOtherGroup->eaInBoxes[pOtherGroup->iNumInBoxes-1],
										pGroup->pDoc->pScrollArea);
					break;
				}
			}
		}
	}
}


static void MDEUpdateLines(MDEMissionGroup *pGroup, bool bIsRoot)
{
	// Put in required out lines
	MDEUpdateLinesForActions(pGroup, pGroup->pMission->ppOnStartActions);
	if (!bIsRoot) {
		MDEUpdateLinesForActions(pGroup, pGroup->pMission->ppSuccessActions);
		MDEUpdateLinesForActions(pGroup, pGroup->pMission->ppFailureActions);
		MDEUpdateLinesForActions(pGroup, pGroup->pMission->ppOnReturnActions);
	}
}


static void MDECleanupLines(MDEMissionGroup *pGroup)
{
	// Free excess out boxes
	while(pGroup->iNumOutBoxes < eaSize(&pGroup->eaOutBoxes)) {
		assert(pGroup->eaOutBoxes);
		ui_WidgetQueueFree((UIWidget*)pGroup->eaOutBoxes[pGroup->iNumOutBoxes]);
		eaRemove(&pGroup->eaOutBoxes, pGroup->iNumOutBoxes);
	}

	// Free excess in boxes
	while(pGroup->iNumInBoxes < eaSize(&pGroup->eaInBoxes)) {
		assert(pGroup->eaInBoxes);
		ui_WidgetQueueFree((UIWidget*)pGroup->eaInBoxes[pGroup->iNumInBoxes]);
		eaRemove(&pGroup->eaInBoxes, pGroup->iNumInBoxes);
	}
}


static F32 MDEUpdateCondGroup(MDECondGroup *pGroup, MissionEditCond *pCond, MissionEditCond *pOrigCond, F32 y, bool bIsSuccess)
{
	if (pCond->type == MissionCondType_Expression) {
		pGroup->pLabel = MDERefreshLabel(pGroup->pLabel, "Expression", NULL, X_OFFSET_INDENT, 0, y, pGroup->pExpander);
		if (!pGroup->pExprField) {
			pGroup->pExprField = MEFieldCreateSimpleExpression(kMEFieldTypeEx_Expression, pOrigCond, pCond, parse_MissionEditCond, "valStr", MDEGetMissionExprContext());
			MDEAddFieldToParent(pGroup->pExprField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 100, pGroup->pMissionGroup->pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pExprField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pExprField, pOrigCond, pCond);
		}
		if (!pGroup->pExprCountField) {
			pGroup->pExprCountField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pOrigCond, pCond, parse_MissionEditCond, "ShowCount", MDEShowCountEnum);
			MDEAddFieldToParent(pGroup->pExprCountField, UI_WIDGET(pGroup->pExpander), 5, y, 0, 90, UIUnitFixed, 0, pGroup->pMissionGroup->pDoc);
			ui_WidgetSetPositionEx(pGroup->pExprCountField->pUIWidget, 5, y, 0, 0, UITopRight);
		} else {
			ui_WidgetSetPositionEx(pGroup->pExprCountField->pUIWidget, 5, y, 0, 0, UITopRight);
			MEFieldSetAndRefreshFromData(pGroup->pExprCountField, pOrigCond, pCond);
		}
		MEFieldSafeDestroy(&pGroup->pMissionField);
	} else if (pCond->type == MissionCondType_Objective) {
		pGroup->pLabel = MDERefreshLabel(pGroup->pLabel, "Objective", bIsSuccess ? "Requires this sub-mission to succeed": "Fails if this sub-mission fails", X_OFFSET_INDENT, 0, y, pGroup->pExpander);
		if (!pGroup->pMissionField) {
			pGroup->pMissionField = MEFieldCreateSimpleDataProvided(kMEFieldType_ValidatedTextEntry, pOrigCond, pCond, parse_MissionEditCond, "valStr", parse_MissionDef, &pGroup->pMissionGroup->pDoc->eaSubMissions, "name");
			MDEAddFieldToParent(pGroup->pMissionField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pGroup->pMissionGroup->pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pMissionField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pMissionField, pOrigCond, pCond);
		}
		MEFieldSafeDestroy(&pGroup->pExprField);
		MEFieldSafeDestroy(&pGroup->pExprCountField);
	} else {
		pGroup->pLabel = MDERefreshLabel(pGroup->pLabel, "Unsupported Type", NULL, X_OFFSET_INDENT, 0, y, pGroup->pExpander);
		MEFieldSafeDestroy(&pGroup->pMissionField);
		MEFieldSafeDestroy(&pGroup->pExprField);
		MEFieldSafeDestroy(&pGroup->pExprCountField);
	}

	// Update button
	if (!pGroup->pRemoveButton) {
		pGroup->pRemoveButton = ui_ButtonCreate("X", 5, y, MDERemoveCondCB, pGroup);
		ui_WidgetSetWidth(UI_WIDGET(pGroup->pRemoveButton), 15);
		ui_WidgetSetPosition(UI_WIDGET(pGroup->pRemoveButton), X_OFFSET_INDENT - 18, y);
		ui_ExpanderAddChild(pGroup->pExpander, pGroup->pRemoveButton);
	} else {
		ui_WidgetSetPosition(UI_WIDGET(pGroup->pRemoveButton), X_OFFSET_INDENT - 18, y);
	}

	y += Y_OFFSET_ROW;

	return y;
}


static F32 MDEUpdateConds(MDEMissionGroup *pMissionGroup, MDECondGroup ***peaCondGroups, MissionEditCond ***peaConds, MissionEditCond ***peaOrigConds, F32 y, bool bIsSuccess)
{
	int i;

	for(i=0; i<eaSize(peaConds); ++i) {
		MDECondGroup *pGroup;
		MissionEditCond *pCond;
		MissionEditCond *pOrigCond = NULL;

		if (i >= eaSize(peaCondGroups)) {
			pGroup = calloc(1, sizeof(MDECondGroup));
			pGroup->pMissionGroup = pMissionGroup;
			pGroup->pExpander = pMissionGroup->pCondExpander;
			eaPush(peaCondGroups, pGroup);
		} else {
			pGroup = (*peaCondGroups)[i];
		}

		pGroup->peaConds = peaConds;
		pGroup->peaOrigConds = peaOrigConds;
		pGroup->index = i;
		pCond = (*peaConds)[i];
		if (peaOrigConds && (i < eaSize(peaOrigConds))) {
			pOrigCond = (*peaOrigConds)[i];
		}

		y = MDEUpdateCondGroup(pGroup, pCond, pOrigCond, y, bIsSuccess);
	}

	// Clean up extra groups
	while(i < eaSize(peaCondGroups)) {
		MDEFreeCondGroup((*peaCondGroups)[i]);
		eaRemove(peaCondGroups, i);
	}

	return y;
}


static void MDEUpdateCondExpander(MDEMissionGroup *pMissionGroup, bool bIsRoot)
{
	F32 y = 0;
	bool bHasSuccessConds;
	bool bHasFailureConds;
	bool bHasResetConds;
	bool bHasSubMissions = false;
	int i;

	bHasSuccessConds = (eaSize(&pMissionGroup->pMission->meSuccessCond->subConds) > 0);
	bHasFailureConds = (eaSize(&pMissionGroup->pMission->meFailureCond->subConds) > 0);
	bHasResetConds = (eaSize(&pMissionGroup->pMission->meResetCond->subConds) > 0);
	for(i=eaSize(&pMissionGroup->pMission->ppOnStartActions)-1; i>=0; --i) {
		if (pMissionGroup->pMission->ppOnStartActions[i]->eActionType == WorldGameActionType_GrantSubMission) {
			if(pMissionGroup->pMission->ppOnStartActions[i]->pGrantSubMissionProperties && pMissionGroup->pMission->ppOnStartActions[i]->pGrantSubMissionProperties->pcSubMissionName) {
				bHasSubMissions = true;
				break;
			}
		}
	}

	// Perk "Discovered" condition
	if (bIsRoot && missiondef_GetType(pMissionGroup->pMission) == MissionType_Perk){
		pMissionGroup->pCondDiscoverWhenLabel = MDERefreshLabel(pMissionGroup->pCondDiscoverWhenLabel, "Discover When", "Determines when the Perk will be Discovered by the player.  Defaults to 'any progress made on the Perk'.", X_OFFSET_BASE, 0, y, pMissionGroup->pCondExpander);
		if (!pMissionGroup->pCondDiscoverWhenField){
			pMissionGroup->pCondDiscoverWhenField = MEFieldCreateSimpleExpression(kMEFieldTypeEx_Expression, pMissionGroup->pOrigMission, pMissionGroup->pMission, parse_MissionDef, "DiscoverCond", MDEGetMissionExprContext());
			MDEAddFieldToParent(pMissionGroup->pCondDiscoverWhenField, UI_WIDGET(pMissionGroup->pCondExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 0, pMissionGroup->pDoc);
		} else {
			ui_WidgetSetPosition(pMissionGroup->pCondDiscoverWhenField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pMissionGroup->pCondDiscoverWhenField, pMissionGroup->pOrigMission, pMissionGroup->pMission);
		}
		y += Y_OFFSET_ROW;
	} else {
		if (pMissionGroup->pCondDiscoverWhenLabel){
			ui_ExpanderRemoveChild(pMissionGroup->pCondExpander, pMissionGroup->pCondDiscoverWhenLabel);
			ui_WidgetQueueFree(UI_WIDGET(pMissionGroup->pCondDiscoverWhenLabel));
			pMissionGroup->pCondDiscoverWhenLabel = NULL;
		}
		MEFieldSafeDestroy(&pMissionGroup->pCondDiscoverWhenField);
	}

	pMissionGroup->pCondSuccessMainLabel = MDERefreshLabel(pMissionGroup->pCondSuccessMainLabel, "Success Condition", NULL, X_OFFSET_BASE, 0, y, pMissionGroup->pCondExpander);
	if (!pMissionGroup->pCondSuccessAddExprButton) {
		pMissionGroup->pCondSuccessAddExprButton = ui_ButtonCreate("Add Expr", X_OFFSET_BASE + 150, y, MDEAddCondSuccessExprCB, pMissionGroup);
		ui_WidgetSetWidth(UI_WIDGET(pMissionGroup->pCondSuccessAddExprButton), 80);
		ui_ExpanderAddChild(pMissionGroup->pCondExpander, pMissionGroup->pCondSuccessAddExprButton);
	} else {
		ui_WidgetSetPosition(UI_WIDGET(pMissionGroup->pCondSuccessAddExprButton), X_OFFSET_BASE + 150, y);
	}
	if (!pMissionGroup->pCondSuccessAddObjButton) {
		pMissionGroup->pCondSuccessAddObjButton = ui_ButtonCreate("Add Objective", X_OFFSET_BASE + 240, y, MDEAddCondSuccessObjCB, pMissionGroup);
		ui_WidgetSetWidth(UI_WIDGET(pMissionGroup->pCondSuccessAddObjButton), 100);
		ui_ExpanderAddChild(pMissionGroup->pCondExpander, pMissionGroup->pCondSuccessAddObjButton);
	} else {
		ui_WidgetSetPosition(UI_WIDGET(pMissionGroup->pCondSuccessAddObjButton), X_OFFSET_BASE + 240, y);
	}

	y += Y_OFFSET_ROW;

	if (!bHasSuccessConds && bHasSubMissions) {
		pMissionGroup->pCondSuccessWhenLabel = MDERefreshLabel(pMissionGroup->pCondSuccessWhenLabel, "<< When all sub-missions are successful >>", NULL, X_OFFSET_INDENT, 0, y, pMissionGroup->pCondExpander);
		MEFieldSafeDestroy(&pMissionGroup->pCondSuccessWhenField);
		pMissionGroup->pMission->meSuccessCond->type = MissionCondType_And;
		ui_WidgetQueueFreeAndNull(&pMissionGroup->pCondSuccessWhenCountLabel);
		MEFieldSafeDestroy(&pMissionGroup->pCondSuccessWhenCountField);
		pMissionGroup->pMission->meSuccessCond->iCount = 0;

		y += Y_OFFSET_ROW;
		
	} else if (!bHasSuccessConds && !bHasSubMissions) {
		pMissionGroup->pCondSuccessWhenLabel = MDERefreshLabel(pMissionGroup->pCondSuccessWhenLabel, "<< Will succeed immediately>>", NULL, X_OFFSET_INDENT, 0, y, pMissionGroup->pCondExpander);
		MEFieldSafeDestroy(&pMissionGroup->pCondSuccessWhenField);
		pMissionGroup->pMission->meSuccessCond->type = MissionCondType_And;
		ui_WidgetQueueFreeAndNull(&pMissionGroup->pCondSuccessWhenCountLabel);
		MEFieldSafeDestroy(&pMissionGroup->pCondSuccessWhenCountField);
		pMissionGroup->pMission->meSuccessCond->iCount = 0;

		y += Y_OFFSET_ROW;
		
	} else if (eaSize(&pMissionGroup->pMission->meSuccessCond->subConds) == 1) {
		// No need for the "all/one/count" choice if only one condition
		ui_WidgetQueueFreeAndNull(&pMissionGroup->pCondSuccessWhenLabel);
		MEFieldSafeDestroy(&pMissionGroup->pCondSuccessWhenField);
		pMissionGroup->pMission->meSuccessCond->type = MissionCondType_And;
		ui_WidgetQueueFreeAndNull(&pMissionGroup->pCondSuccessWhenCountLabel);
		MEFieldSafeDestroy(&pMissionGroup->pCondSuccessWhenCountField);
		pMissionGroup->pMission->meSuccessCond->iCount = 0;
	} else {
		pMissionGroup->pCondSuccessWhenLabel = MDERefreshLabel(pMissionGroup->pCondSuccessWhenLabel, "When", NULL, X_OFFSET_INDENT, 0, y, pMissionGroup->pCondExpander);

		if (!pMissionGroup->pCondSuccessWhenField) {
			pMissionGroup->pCondSuccessWhenField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pMissionGroup->pOrigMission ? pMissionGroup->pOrigMission->meSuccessCond : NULL, pMissionGroup->pMission->meSuccessCond, parse_MissionEditCond, "type", MissionCondTypeForEditorEnum);
			MDEAddFieldToParent(pMissionGroup->pCondSuccessWhenField, UI_WIDGET(pMissionGroup->pCondExpander), X_OFFSET_CONTROL, y, 0, 120, UIUnitFixed, 0, pMissionGroup->pDoc);
		} else {
			ui_WidgetSetPosition(pMissionGroup->pCondSuccessWhenField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pMissionGroup->pCondSuccessWhenField, pMissionGroup->pOrigMission ? pMissionGroup->pOrigMission->meSuccessCond : NULL, pMissionGroup->pMission->meSuccessCond);
		}

		if(pMissionGroup->pMission->meSuccessCond->type == MissionCondType_Count)
		{
			pMissionGroup->pCondSuccessWhenCountLabel = MDERefreshLabel(pMissionGroup->pCondSuccessWhenCountLabel, "Count", NULL, X_OFFSET_CONTROL + 140, 0, y, pMissionGroup->pCondExpander);

			if (!pMissionGroup->pCondSuccessWhenCountField) {
				pMissionGroup->pCondSuccessWhenCountField = MEFieldCreateSimple(kMEFieldType_Spinner, pMissionGroup->pOrigMission ? pMissionGroup->pOrigMission->meSuccessCond : NULL, pMissionGroup->pMission->meSuccessCond, parse_MissionEditCond, "count");
				MDEAddFieldToParent(pMissionGroup->pCondSuccessWhenCountField, UI_WIDGET(pMissionGroup->pCondExpander), X_OFFSET_CONTROL + 200, y, 0, 120, UIUnitFixed, 0, pMissionGroup->pDoc);
			} else {
				ui_WidgetSetPosition(pMissionGroup->pCondSuccessWhenCountField->pUIWidget, X_OFFSET_CONTROL + 200, y);
				MEFieldSetAndRefreshFromData(pMissionGroup->pCondSuccessWhenCountField, pMissionGroup->pOrigMission ? pMissionGroup->pOrigMission->meSuccessCond : NULL, pMissionGroup->pMission->meSuccessCond);
			}
		}
		else
		{
			ui_WidgetQueueFreeAndNull(&pMissionGroup->pCondSuccessWhenCountLabel);
			MEFieldSafeDestroy(&pMissionGroup->pCondSuccessWhenCountField);
			pMissionGroup->pMission->meSuccessCond->iCount = 0;
		}

		y += Y_OFFSET_ROW;
	}

	// Update the dynamic portion
	y = MDEUpdateConds(pMissionGroup, &pMissionGroup->eaCondSuccessGroups, &pMissionGroup->pMission->meSuccessCond->subConds, pMissionGroup->pOrigMission ? &pMissionGroup->pOrigMission->meSuccessCond->subConds : NULL, y, true);

	y += 8; // Put a little spacing between sections

	pMissionGroup->pCondFailureMainLabel = MDERefreshLabel(pMissionGroup->pCondFailureMainLabel, "Failure Condition", NULL, X_OFFSET_BASE, 0, y, pMissionGroup->pCondExpander);
	if (!pMissionGroup->pCondFailureAddExprButton) {
		pMissionGroup->pCondFailureAddExprButton = ui_ButtonCreate("Add Expr", X_OFFSET_BASE + 150, y, MDEAddCondFailureExprCB, pMissionGroup);
		ui_WidgetSetWidth(UI_WIDGET(pMissionGroup->pCondFailureAddExprButton), 80);
		ui_ExpanderAddChild(pMissionGroup->pCondExpander, pMissionGroup->pCondFailureAddExprButton);
	} else {
		ui_WidgetSetPosition(UI_WIDGET(pMissionGroup->pCondFailureAddExprButton), X_OFFSET_BASE + 150, y);
	}
	if (!pMissionGroup->pCondFailureAddObjButton) {
		pMissionGroup->pCondFailureAddObjButton = ui_ButtonCreate("Add Objective", X_OFFSET_BASE + 240, y, MDEAddCondFailureObjCB, pMissionGroup);
		ui_WidgetSetWidth(UI_WIDGET(pMissionGroup->pCondFailureAddObjButton), 100);
		ui_ExpanderAddChild(pMissionGroup->pCondExpander, pMissionGroup->pCondFailureAddObjButton);
	} else {
		ui_WidgetSetPosition(UI_WIDGET(pMissionGroup->pCondFailureAddObjButton), X_OFFSET_BASE + 240, y);
	}

	y += Y_OFFSET_ROW;

	if (!bHasSuccessConds && !bHasFailureConds && bHasSubMissions) {
		pMissionGroup->pCondFailureWhenLabel = MDERefreshLabel(pMissionGroup->pCondFailureWhenLabel, "<< When any one sub-mission fails >>", NULL, X_OFFSET_INDENT, 0, y, pMissionGroup->pCondExpander);
		MEFieldSafeDestroy(&pMissionGroup->pCondFailureWhenField);

		y += Y_OFFSET_ROW;

	} else if (!bHasFailureConds) { // Also has either success cond or no sub missions
		pMissionGroup->pCondFailureWhenLabel = MDERefreshLabel(pMissionGroup->pCondFailureWhenLabel, "<< Will NEVER fail >>", NULL, X_OFFSET_INDENT, 0, y, pMissionGroup->pCondExpander);
		MEFieldSafeDestroy(&pMissionGroup->pCondFailureWhenField);

		y += Y_OFFSET_ROW;

	} else if (eaSize(&pMissionGroup->pMission->meFailureCond->subConds) == 1) {
		// No need for the "all/one/count" choice if only one condition
		ui_WidgetQueueFreeAndNull(&pMissionGroup->pCondFailureWhenLabel);
		MEFieldSafeDestroy(&pMissionGroup->pCondFailureWhenField);
		ui_WidgetQueueFreeAndNull(&pMissionGroup->pCondFailureWhenCountLabel);
		MEFieldSafeDestroy(&pMissionGroup->pCondFailureWhenCountField);
	} else {
		pMissionGroup->pCondFailureWhenLabel = MDERefreshLabel(pMissionGroup->pCondFailureWhenLabel, "When", NULL, X_OFFSET_INDENT, 0, y, pMissionGroup->pCondExpander);

		if (!pMissionGroup->pCondFailureWhenField) {
			pMissionGroup->pCondFailureWhenField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pMissionGroup->pOrigMission ? pMissionGroup->pOrigMission->meFailureCond : NULL, pMissionGroup->pMission->meFailureCond, parse_MissionEditCond, "type", MissionCondTypeForEditorEnum);
			MDEAddFieldToParent(pMissionGroup->pCondFailureWhenField, UI_WIDGET(pMissionGroup->pCondExpander), X_OFFSET_CONTROL, y, 0, 120, UIUnitFixed, 0, pMissionGroup->pDoc);
		} else {
			ui_WidgetSetPosition(pMissionGroup->pCondFailureWhenField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pMissionGroup->pCondFailureWhenField, pMissionGroup->pOrigMission ? pMissionGroup->pOrigMission->meFailureCond : NULL, pMissionGroup->pMission->meFailureCond);
		}

		if(pMissionGroup->pMission->meFailureCond->type == MissionCondType_Count)
		{
			pMissionGroup->pCondFailureWhenCountLabel = MDERefreshLabel(pMissionGroup->pCondFailureWhenCountLabel, "Count", NULL, X_OFFSET_CONTROL + 140, 0, y, pMissionGroup->pCondExpander);

			if (!pMissionGroup->pCondFailureWhenCountField) {
				pMissionGroup->pCondFailureWhenCountField = MEFieldCreateSimple(kMEFieldType_Spinner, pMissionGroup->pOrigMission ? pMissionGroup->pOrigMission->meFailureCond : NULL, pMissionGroup->pMission->meFailureCond, parse_MissionEditCond, "count");
				MDEAddFieldToParent(pMissionGroup->pCondFailureWhenCountField, UI_WIDGET(pMissionGroup->pCondExpander), X_OFFSET_CONTROL + 200, y, 0, 120, UIUnitFixed, 0, pMissionGroup->pDoc);
			} else {
				ui_WidgetSetPosition(pMissionGroup->pCondFailureWhenCountField->pUIWidget, X_OFFSET_CONTROL + 200, y);
				MEFieldSetAndRefreshFromData(pMissionGroup->pCondFailureWhenCountField, pMissionGroup->pOrigMission ? pMissionGroup->pOrigMission->meFailureCond : NULL, pMissionGroup->pMission->meFailureCond);
			}
		}
		else
		{
			ui_WidgetQueueFreeAndNull(&pMissionGroup->pCondFailureWhenCountLabel);
			MEFieldSafeDestroy(&pMissionGroup->pCondFailureWhenCountField);
		}

		y += Y_OFFSET_ROW;
	}
		
	// Update the dynamic portion
	y = MDEUpdateConds(pMissionGroup, &pMissionGroup->eaCondFailureGroups, &pMissionGroup->pMission->meFailureCond->subConds, pMissionGroup->pOrigMission ? &pMissionGroup->pOrigMission->meFailureCond->subConds : NULL, y, false);

	if (!bHasSuccessConds && bHasFailureConds && bHasSubMissions) {
		pMissionGroup->pCondFailureWhen2Label = MDERefreshLabel(pMissionGroup->pCondFailureWhen2Label, "<< OR When any sub-mission fails >>", NULL, X_OFFSET_INDENT, 0, y, pMissionGroup->pCondExpander);
		y += Y_OFFSET_ROW;
	} else {
		ui_WidgetQueueFreeAndNull(&pMissionGroup->pCondFailureWhen2Label);
	}

	// Reset condition
	pMissionGroup->pCondResetMainLabel = MDERefreshLabel(pMissionGroup->pCondResetMainLabel, "Reset Condition", NULL, X_OFFSET_BASE, 0, y, pMissionGroup->pCondExpander);
	if (!pMissionGroup->pCondResetAddExprButton) {
		pMissionGroup->pCondResetAddExprButton = ui_ButtonCreate("Add Expr", X_OFFSET_BASE + 150, y, MDEAddCondResetExprCB, pMissionGroup);
		ui_WidgetSetWidth(UI_WIDGET(pMissionGroup->pCondResetAddExprButton), 80);
		ui_ExpanderAddChild(pMissionGroup->pCondExpander, pMissionGroup->pCondResetAddExprButton);
	} else {
		ui_WidgetSetPosition(UI_WIDGET(pMissionGroup->pCondResetAddExprButton), X_OFFSET_BASE + 150, y);
	}
	

	y += Y_OFFSET_ROW;

	if (!bHasResetConds) {
		pMissionGroup->pCondResetWhenLabel = MDERefreshLabel(pMissionGroup->pCondResetWhenLabel, "<< Will NEVER reset >>", NULL, X_OFFSET_INDENT, 0, y, pMissionGroup->pCondExpander);
		MEFieldSafeDestroy(&pMissionGroup->pCondResetWhenField);

		y += Y_OFFSET_ROW;

	} else if (eaSize(&pMissionGroup->pMission->meResetCond->subConds) == 1) {
		// No need for the "all/one/count" choice if only one condition
		ui_WidgetQueueFreeAndNull(&pMissionGroup->pCondResetWhenLabel);
		MEFieldSafeDestroy(&pMissionGroup->pCondResetWhenField);
		ui_WidgetQueueFreeAndNull(&pMissionGroup->pCondResetWhenCountLabel);
		MEFieldSafeDestroy(&pMissionGroup->pCondResetWhenCountField);
	} else {
		pMissionGroup->pCondResetWhenLabel = MDERefreshLabel(pMissionGroup->pCondResetWhenLabel, "When", NULL, X_OFFSET_INDENT, 0, y, pMissionGroup->pCondExpander);

		if (!pMissionGroup->pCondResetWhenField) {
			pMissionGroup->pCondResetWhenField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, 
																		pMissionGroup->pOrigMission ? pMissionGroup->pOrigMission->meResetCond : NULL, 
																		pMissionGroup->pMission->meResetCond, 
																		parse_MissionEditCond, "type", 
																		MissionCondTypeForEditorEnum);
			MDEAddFieldToParent(pMissionGroup->pCondResetWhenField, UI_WIDGET(pMissionGroup->pCondExpander), X_OFFSET_CONTROL, y, 0, 120, UIUnitFixed, 0, pMissionGroup->pDoc);
		} else {
			ui_WidgetSetPosition(pMissionGroup->pCondResetWhenField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pMissionGroup->pCondResetWhenField, 
										 pMissionGroup->pOrigMission ? pMissionGroup->pOrigMission->meResetCond : NULL, 
										 pMissionGroup->pMission->meResetCond);
		}

		if(pMissionGroup->pMission->meResetCond->type == MissionCondType_Count)
		{
			pMissionGroup->pCondResetWhenCountLabel = MDERefreshLabel(pMissionGroup->pCondResetWhenCountLabel, "Count", NULL, X_OFFSET_CONTROL + 140, 0, y, pMissionGroup->pCondExpander);

			if (!pMissionGroup->pCondResetWhenCountField) {
				pMissionGroup->pCondResetWhenCountField = MEFieldCreateSimple(kMEFieldType_Spinner, pMissionGroup->pOrigMission ? pMissionGroup->pOrigMission->meResetCond : NULL, pMissionGroup->pMission->meResetCond, parse_MissionEditCond, "count");
				MDEAddFieldToParent(pMissionGroup->pCondResetWhenCountField, UI_WIDGET(pMissionGroup->pCondExpander), X_OFFSET_CONTROL + 200, y, 0, 120, UIUnitFixed, 0, pMissionGroup->pDoc);
			} else {
				ui_WidgetSetPosition(pMissionGroup->pCondResetWhenCountField->pUIWidget, X_OFFSET_CONTROL + 200, y);
				MEFieldSetAndRefreshFromData(pMissionGroup->pCondResetWhenCountField, pMissionGroup->pOrigMission ? pMissionGroup->pOrigMission->meResetCond : NULL, pMissionGroup->pMission->meResetCond);
			}
		}
		else
		{
			ui_WidgetQueueFreeAndNull(&pMissionGroup->pCondResetWhenCountLabel);
			MEFieldSafeDestroy(&pMissionGroup->pCondResetWhenCountField);
		}

		y += Y_OFFSET_ROW;
	}

	// Update the dynamic portion
	y = MDEUpdateConds(pMissionGroup, &pMissionGroup->eaCondResetGroups, &pMissionGroup->pMission->meResetCond->subConds, 
						pMissionGroup->pOrigMission ? &pMissionGroup->pOrigMission->meResetCond->subConds : NULL, y, false);
		
	// Set the expander height
	ui_ExpanderSetHeight(pMissionGroup->pCondExpander, y);
}

static void MDEUpdateActionGroupModels(GEActionGroup *pActionGroup, MDEMissionGroup *pMissionGroup)
{
	pActionGroup->peaSubMissions = &pMissionGroup->pDoc->pMission->subMissions;
	eaDestroy(&pActionGroup->peaVarDefs);
	eaIndexedEnable(&pActionGroup->peaVarDefs, parse_WorldVariableDef);
	eaPushEArray(&pActionGroup->peaVarDefs, &pMissionGroup->pMission->eaVariableDefs);
	eaPushEArray(&pActionGroup->peaVarDefs, &pMissionGroup->pDoc->pMission->eaVariableDefs);
}

static void MDEUpdateActionExpander(MDEMissionGroup *pGroup, bool bIsRoot)
{
	F32 y = Y_OFFSET_ROW;
	int i;
	int n = 0;
	int total;

	// Create necessary action groups
	total = eaSize(&pGroup->pMission->ppOnStartActions) + eaSize(&pGroup->pMission->ppSuccessActions) +
			eaSize(&pGroup->pMission->ppFailureActions) + eaSize(&pGroup->pMission->ppOnReturnActions);
	while(total > eaSize(&pGroup->eaActionGroups)) {
		eaPush(&pGroup->eaActionGroups, MDECreateActionGroup(pGroup));
	}

	// Update the action groups
	for(i=0; i<eaSize(&pGroup->pMission->ppOnStartActions); ++i) {
		WorldGameActionProperties ***peaActions = &pGroup->pMission->ppOnStartActions;
		WorldGameActionProperties ***peaOrigActions = (pGroup->pOrigMission ? &pGroup->pOrigMission->ppOnStartActions : NULL);
	
		assert(pGroup->eaActionGroups);
		MDEUpdateActionGroupModels(pGroup->eaActionGroups[n], pGroup);
		y = GEUpdateAction(pGroup->eaActionGroups[n], "On Start", NULL, peaActions, peaOrigActions, y, i);
		++n;
	}

	for(i=0; i<eaSize(&pGroup->pMission->ppSuccessActions); ++i) {
		WorldGameActionProperties ***peaActions = &pGroup->pMission->ppSuccessActions;
		WorldGameActionProperties ***peaOrigActions = (pGroup->pOrigMission ? &pGroup->pOrigMission->ppSuccessActions : NULL);
	
		assert(pGroup->eaActionGroups);
		MDEUpdateActionGroupModels(pGroup->eaActionGroups[n], pGroup);
		y = GEUpdateAction(pGroup->eaActionGroups[n], "On Success", NULL, peaActions, peaOrigActions, y, i);
		++n;
	}

	for(i=0; i<eaSize(&pGroup->pMission->ppFailureActions); ++i) {
		WorldGameActionProperties ***peaActions = &pGroup->pMission->ppFailureActions;
		WorldGameActionProperties ***peaOrigActions = (pGroup->pOrigMission ? &pGroup->pOrigMission->ppFailureActions : NULL);
	
		assert(pGroup->eaActionGroups);
		MDEUpdateActionGroupModels(pGroup->eaActionGroups[n], pGroup);
		y = GEUpdateAction(pGroup->eaActionGroups[n], "On Failure", NULL, peaActions, peaOrigActions, y, i);
		++n;
	}

	for(i=0; i<eaSize(&pGroup->pMission->ppOnReturnActions); ++i) {
		WorldGameActionProperties ***peaActions = &pGroup->pMission->ppOnReturnActions;
		WorldGameActionProperties ***peaOrigActions = (pGroup->pOrigMission ? &pGroup->pOrigMission->ppOnReturnActions : NULL);
	
		assert(pGroup->eaActionGroups);
		MDEUpdateActionGroupModels(pGroup->eaActionGroups[n], pGroup);
		y = GEUpdateAction(pGroup->eaActionGroups[n], "On Return", NULL, peaActions, peaOrigActions, y, i);
		++n;
	}

	// Remove unused action groups
	while(n < eaSize(&pGroup->eaActionGroups)) {
		GEFreeActionGroup(pGroup->eaActionGroups[n]);
		eaRemove(&pGroup->eaActionGroups, n);
	}

	ui_ExpanderSetHeight(pGroup->pActionExpander, y);
}

static void MDEScrollToContactOverrideCB(UIButton *pButton, MDEContactOverrideGroup *pGroup)
{
	MDEContactGroup *pContactGroup = MDEFindContactGroup(pGroup->pMissionGroup->pDoc, pGroup->pchContactName);

	if (pContactGroup)
	{
		ui_ScrollAreaScrollToPosition(pGroup->pMissionGroup->pDoc->pScrollArea, MAX(pContactGroup->pWindow->widget.x - 10.f, 0), MAX(pContactGroup->pWindow->widget.y - 35.f, 0.f));
	}
}

static F32 MDEUpdateContactOverrideGroup(MDEContactOverrideGroup *pGroup, F32 y)
{
	if (pGroup->pFindContactOverrideWindowButton == NULL)
	{
		pGroup->pFindContactOverrideWindowButton = ui_ButtonCreate(pGroup->pchContactName, X_OFFSET_BASE, y, MDEScrollToContactOverrideCB, pGroup);
		ui_WidgetSetWidthEx(UI_WIDGET(pGroup->pFindContactOverrideWindowButton), 1.f, UIUnitPercentage);
		ui_ExpanderAddChild(pGroup->pExpander, pGroup->pFindContactOverrideWindowButton);
	}
	else
	{
		ui_ButtonSetCallback(pGroup->pFindContactOverrideWindowButton, MDEScrollToContactOverrideCB, pGroup);
		ui_WidgetSetPosition(UI_WIDGET(pGroup->pFindContactOverrideWindowButton), X_OFFSET_BASE, y);
	}

	y += Y_OFFSET_ROW + 3.f;

	return y;
}

static F32 MDEUpdateNumericScaleGroup(MDENumericScaleGroup *pGroup, MissionNumericScale *pScale, MissionNumericScale *pOrigScale, int index, F32 y, bool bIsRoot)
{
	if (!pGroup->pSeparator) {
		pGroup->pSeparator = ui_SeparatorCreate(UIHorizontal);
		ui_ExpanderAddChild(pGroup->pExpander, pGroup->pSeparator);
	}
	ui_WidgetSetPosition(UI_WIDGET(pGroup->pSeparator), 0, y);

	y += Y_OFFSET_SEPARATOR;

	// Update numeric field
	pGroup->pNumericLabel = MDERefreshLabel(pGroup->pNumericLabel, "Numeric", NULL, X_OFFSET_BASE, 0, y, pGroup->pExpander);
	if (!pGroup->pNumericField) {
		pGroup->pNumericField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_ValidatedTextEntry, pOrigScale, pScale, parse_MissionNumericScale, "Numeric", "ItemDef", "resourceName");
		MDEAddFieldToParent(pGroup->pNumericField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 95, pGroup->pMissionGroup->pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pNumericField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pNumericField, pOrigScale, pScale);
	}

	// Update button
	if (!pGroup->pRemoveButton) {
		pGroup->pRemoveButton = ui_ButtonCreate("Remove", 5, y, MDERemoveNumericScaleCB, pGroup);
		ui_WidgetSetWidth(UI_WIDGET(pGroup->pRemoveButton), 80);
		ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pRemoveButton), 5, y, 0, 0, UITopRight);
		ui_ExpanderAddChild(pGroup->pExpander, pGroup->pRemoveButton);
	} else {
		ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pRemoveButton), 5, y, 0, 0, UITopRight);
	}

	y += Y_OFFSET_ROW;

	// Update scale field
	pGroup->pScaleLabel = MDERefreshLabel(pGroup->pScaleLabel, "Scale", NULL, X_OFFSET_BASE, 0, y, pGroup->pExpander);
	if (!pGroup->pScaleField) {
		pGroup->pScaleField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigScale, pScale, parse_MissionNumericScale, "Scale");
		MDEAddFieldToParent(pGroup->pScaleField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 95, pGroup->pMissionGroup->pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pScaleField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pScaleField, pOrigScale, pScale);
	}

	y += Y_OFFSET_ROW;

	return y;
}

static void MDEUpdateNumericScalesExpander(MDEMissionGroup *pMissionGroup, bool bIsRoot)
{
	F32 y = Y_OFFSET_ROW;
	int i;

	if (!pMissionGroup->pNumericScalesExpander)
		return;

	// Put in numeric scale entries
	for(i=0; i<eaSize(&pMissionGroup->pMission->params->eaNumericScales); ++i) {
		MDENumericScaleGroup *pGroup;
		MissionNumericScale *pScale;
		MissionNumericScale *pOrigScale = NULL;

		if (i >= eaSize(&pMissionGroup->eaNumericScaleGroups)) {
			pGroup = calloc(1, sizeof(MDENumericScaleGroup));
			pGroup->pMissionGroup = pMissionGroup;
			pGroup->pExpander = pMissionGroup->pNumericScalesExpander;
			eaPush(&pMissionGroup->eaNumericScaleGroups, pGroup);
		} else {
			pGroup = pMissionGroup->eaNumericScaleGroups[i];
		}

		pGroup->peaNumericScales = &pMissionGroup->pMission->params->eaNumericScales;
		pGroup->peaOrigNumericScales = (pMissionGroup->pOrigMission ? &pMissionGroup->pOrigMission->params->eaNumericScales : NULL);
		pGroup->index = i;
		pScale = pMissionGroup->pMission->params->eaNumericScales[i];
		if (pMissionGroup->pOrigMission && (i < eaSize(&pMissionGroup->pOrigMission->params->eaNumericScales))) {
			pOrigScale = pMissionGroup->pOrigMission->params->eaNumericScales[i];
		}

		y = MDEUpdateNumericScaleGroup(pMissionGroup->eaNumericScaleGroups[i], pScale, pOrigScale, i, y, bIsRoot);
	}

	// Clean up extra groups
	while(i < eaSize(&pMissionGroup->eaNumericScaleGroups)) {
		MDEFreeNumericScaleGroup(pMissionGroup->eaNumericScaleGroups[i]);
		eaRemove(&pMissionGroup->eaNumericScaleGroups, i);
	}

	// Set the expander height
	ui_ExpanderSetHeight(pMissionGroup->pNumericScalesExpander, y);
}

static F32 MDEUpdateDropGroup(MDEDropGroup *pGroup, MissionDrop *pDrop, MissionDrop *pOrigDrop, int index, F32 y, bool bIsRoot)
{
	char *pcText;
	char *pcDict;

	if (!pGroup->pSeparator) {
		pGroup->pSeparator = ui_SeparatorCreate(UIHorizontal);
		ui_ExpanderAddChild(pGroup->pExpander, pGroup->pSeparator);
	}
	ui_WidgetSetPosition(UI_WIDGET(pGroup->pSeparator), 0, y);

	y += Y_OFFSET_SEPARATOR;

	// Update type field
	pGroup->pTypeLabel = MDERefreshLabel(pGroup->pTypeLabel, "Drop Type", NULL, X_OFFSET_BASE, 0, y, pGroup->pExpander);
	if (!pGroup->pTypeField) {
		pGroup->pTypeField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pOrigDrop, pDrop, parse_MissionDrop, "type", MissionDropTargetTypeEnum);
		MDEAddFieldToParent(pGroup->pTypeField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 95, pGroup->pMissionGroup->pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pTypeField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pTypeField, pOrigDrop, pDrop);
	}

	// Update button
	if (!pGroup->pRemoveButton) {
		pGroup->pRemoveButton = ui_ButtonCreate("Remove", 5, y, MDERemoveDropCB, pGroup);
		ui_WidgetSetWidth(UI_WIDGET(pGroup->pRemoveButton), 80);
		ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pRemoveButton), 5, y, 0, 0, UITopRight);
		ui_ExpanderAddChild(pGroup->pExpander, pGroup->pRemoveButton);
	} else {
		ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pRemoveButton), 5, y, 0, 0, UITopRight);
	}

	y += Y_OFFSET_ROW;

	// Update When field
	if (bIsRoot){
		pGroup->pWhenLabel = MDERefreshLabel(pGroup->pWhenLabel, "Drop When", NULL, X_OFFSET_BASE, 0, y, pGroup->pExpander);
		if (!pGroup->pWhenField) {
			pGroup->pWhenField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pOrigDrop, pDrop, parse_MissionDrop, "whenType", MissionDropWhenTypeEnum);
			MDEAddFieldToParent(pGroup->pWhenField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 95, pGroup->pMissionGroup->pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pWhenField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pWhenField, pOrigDrop, pDrop);
		}

		y += Y_OFFSET_ROW;
	}

	// Update name field
	switch(pDrop->type) {
	  xcase MissionDropTargetType_Critter:
				pcText = "Critter Def";
				pcDict = "CritterDef";
	  xcase MissionDropTargetType_Group:
				pcText = "Critter Group";
				pcDict = "CritterGroup";
	  xcase MissionDropTargetType_Actor:
				pcText = "Actor";
				pcDict = NULL;
	  xcase MissionDropTargetType_EncounterGroup:
				pcText = "Encounter Group";
				pcDict = NULL;
	  xdefault:
				pcText = NULL;
				pcDict = NULL;
	}

	if (pcText)
	{
		pGroup->pNameLabel = MDERefreshLabel(pGroup->pNameLabel, pcText, NULL, X_OFFSET_BASE, 0, y, pGroup->pExpander);
		if (pGroup->pNameField && ((pcDict != pGroup->pNameField->pchGlobalDictName) || (pGroup->pNameField->pchGlobalDictName && (stricmp(pGroup->pNameField->pchGlobalDictName, pcDict) != 0)))) {
			MEFieldSafeDestroy(&pGroup->pNameField);
		}
		if (!pGroup->pNameField) {
			if (pcDict) {
				pGroup->pNameField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_ValidatedTextEntry, pOrigDrop, pDrop, parse_MissionDrop, "value", pcDict, "ResourceName");
				MDEAddFieldToParent(pGroup->pNameField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 0, pGroup->pMissionGroup->pDoc);
				ui_TextEntryComboValidate(pGroup->pNameField->pUIText);
			} else {
				pGroup->pNameField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigDrop, pDrop, parse_MissionDrop, "value");
				MDEAddFieldToParent(pGroup->pNameField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 0, pGroup->pMissionGroup->pDoc);
			}
		} else {
			ui_WidgetSetPosition(pGroup->pNameField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pNameField, pOrigDrop, pDrop);
		}

		y += Y_OFFSET_ROW;
	}
	else
	{
		ui_WidgetQueueFreeAndNull(&pGroup->pNameLabel);
		MEFieldSafeDestroy(&pGroup->pNameField);
	}

	// Update reward table field
	pGroup->pRewardLabel = MDERefreshLabel(pGroup->pRewardLabel, "Reward Table", NULL, X_OFFSET_BASE, 0, y, pGroup->pExpander);
	if (!pGroup->pRewardField) {
		pGroup->pRewardField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_ValidatedTextEntry, pOrigDrop, pDrop, parse_MissionDrop, "RewardTableName", "RewardTable", "ResourceName");
		MDEAddFieldToParent(pGroup->pRewardField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 75, pGroup->pMissionGroup->pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pRewardField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pRewardField, pOrigDrop, pDrop);
	}
	pGroup->rewardData.pDoc = pGroup->pMissionGroup->pDoc;
	pGroup->rewardData.pMission = pGroup->pMissionGroup->pMission;
	pGroup->rewardData.ppcRewardTableName = &pDrop->RewardTableName;
	sprintf(pGroup->rewardData.cNamePart, "Drop_%d", index+1);
	if (!pGroup->pRewardEditButton) {
		pGroup->pRewardEditButton = MDEAddRewardEditButton(pGroup->pExpander, y, &pGroup->rewardData);
	} else {
		ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pRewardEditButton), 3, y, 0, 0, UITopRight);
	}

	y += Y_OFFSET_ROW;

	// Update Map field
	pGroup->pMapLabel = MDERefreshLabel(pGroup->pMapLabel, "Map", NULL, X_OFFSET_BASE, 0, y, pGroup->pExpander);
	if (!pGroup->pMapField) {
		pGroup->pMapField = MEFieldCreateSimpleDataProvided(kMEFieldType_ValidatedTextEntry, pOrigDrop, pDrop, parse_MissionDrop, "MapName", NULL, &g_GEMapDispNames, NULL);
		MDEAddFieldToParent(pGroup->pMapField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 95, pGroup->pMissionGroup->pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pMapField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pMapField, pOrigDrop, pDrop);
	}

	y += Y_OFFSET_ROW;

	return y;
}

static void MDEUpdateContactOverridesExpander(MDEMissionGroup *pMissionGroup)
{
	F32 y = 3.f;
	int i;

	const char ** eaContactNames = NULL;

	if (pMissionGroup->pContactOverridesExpander == NULL)
		return;

	FOR_EACH_IN_CONST_EARRAY_FORWARDS(pMissionGroup->pMission->ppSpecialDialogOverrides, SpecialDialogOverride, pOverride)
	{
		// We do a linear search but the idea is that there cannot be too many contact overrides in a mission.
		if (eaFindString(&eaContactNames, pOverride->pcContactName) < 0)
		{
			eaPush(&eaContactNames, pOverride->pcContactName);
		}
	}
	FOR_EACH_END

	FOR_EACH_IN_CONST_EARRAY_FORWARDS(pMissionGroup->pMission->ppMissionOfferOverrides, MissionOfferOverride, pOverride)
	{
		// We do a linear search but the idea is that there cannot be too many contact overrides in a mission.
		if (eaFindString(&eaContactNames, pOverride->pcContactName) < 0)
		{
			eaPush(&eaContactNames, pOverride->pcContactName);
		}
	}
	FOR_EACH_END

	for (i = 0; i < eaSize(&eaContactNames); ++i) 
	{
		MDEContactOverrideGroup *pGroup;

		if (i >= eaSize(&pMissionGroup->eaContactOverrideGroups)) 
		{
			pGroup = calloc(1, sizeof(MDEContactOverrideGroup));
			pGroup->pMissionGroup = pMissionGroup;
			pGroup->pExpander = pMissionGroup->pContactOverridesExpander;
			eaPush(&pMissionGroup->eaContactOverrideGroups, pGroup);
		} 
		else 
		{
			pGroup = pMissionGroup->eaContactOverrideGroups[i];
		}
		pGroup->pchContactName = eaContactNames[i];

		y = MDEUpdateContactOverrideGroup(pMissionGroup->eaContactOverrideGroups[i], y);
	}

	eaDestroy(&eaContactNames);

	// Clean up extra groups
	while (i < eaSize(&pMissionGroup->eaContactOverrideGroups)) 
	{
		MDEFreeContactOverrideGroup(pMissionGroup->eaContactOverrideGroups[i]);
		eaRemove(&pMissionGroup->eaContactOverrideGroups, i);
	}

	// Set the expander height
	ui_ExpanderSetHeight(pMissionGroup->pContactOverridesExpander, y);
}


static void MDEUpdateDropExpander(MDEMissionGroup *pMissionGroup, bool bIsRoot)
{
	F32 y = Y_OFFSET_ROW;
	int i;

	if (!pMissionGroup->pDropExpander)
		return;

	// Put in drop entries
	for(i=0; i<eaSize(&pMissionGroup->pMission->params->missionDrops); ++i) {
		MDEDropGroup *pGroup;
		MissionDrop *pDrop;
		MissionDrop *pOrigDrop = NULL;

		if (i >= eaSize(&pMissionGroup->eaDropGroups)) {
			pGroup = calloc(1, sizeof(MDEDropGroup));
			pGroup->pMissionGroup = pMissionGroup;
			pGroup->pExpander = pMissionGroup->pDropExpander;
			eaPush(&pMissionGroup->eaDropGroups, pGroup);
		} else {
			pGroup = pMissionGroup->eaDropGroups[i];
		}

		pGroup->peaDrops = &pMissionGroup->pMission->params->missionDrops;
		pGroup->peaOrigDrops = (pMissionGroup->pOrigMission ? &pMissionGroup->pOrigMission->params->missionDrops : NULL);
		pGroup->index = i;
		pDrop = pMissionGroup->pMission->params->missionDrops[i];
		if (pMissionGroup->pOrigMission && (i < eaSize(&pMissionGroup->pOrigMission->params->missionDrops))) {
			pOrigDrop = pMissionGroup->pOrigMission->params->missionDrops[i];
		}

		y = MDEUpdateDropGroup(pMissionGroup->eaDropGroups[i], pDrop, pOrigDrop, i, y, bIsRoot);
	}

	// Clean up extra groups
	while(i < eaSize(&pMissionGroup->eaDropGroups)) {
		MDEFreeDropGroup(pMissionGroup->eaDropGroups[i]);
		eaRemove(&pMissionGroup->eaDropGroups, i);
	}

	// Set the expander height
	ui_ExpanderSetHeight(pMissionGroup->pDropExpander, y);
}

static void MDEAddInteractableOverrideCB(UIButton *pButton, MDEMissionGroup *pGroup)
{
	InteractableOverride *pOverride;

	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	// Add a new override
	pOverride = StructCreate(parse_InteractableOverride);
	assert(pOverride);
	pOverride->pPropertyEntry = StructCreate(parse_WorldInteractionPropertyEntry);
	pOverride->pPropertyEntry->bExclusiveInteraction = 1;
	pOverride->pPropertyEntry->bUseExclusionFlag = 1;
	assert(pOverride->pPropertyEntry);
	pOverride->pPropertyEntry->pcInteractionClass = pcPooled_Clickable;
	langMakeEditorCopy(parse_InteractableOverride, pOverride, true);

	eaPush(&pGroup->pMission->ppInteractableOverrides, pOverride);

	// Update the UI
	MDEMissionChanged(pGroup->pDoc, true);
}

static void MDERemoveInteractableOverrideCB(UIButton *pButton, MDEInteractableOverrideGroup *pGroup)
{
	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pGroup->pMissionGroup->pDoc->emDoc, true)) {
		return;
	}

	// Remove the group
	StructDestroy(parse_InteractableOverride, (*pGroup->peaOverrides)[pGroup->index]);
	eaRemove(pGroup->peaOverrides, pGroup->index);

	// Update the UI
	MDEMissionChanged(pGroup->pMissionGroup->pDoc, true);
}

static void MDEPickInteractableOverrideSetCB( const char* zmName, const char* logicalName, const float* mapPos, const char* mapIcon, MDEInteractableOverrideGroup* pGroup )
{
	InteractableOverride* override = (*pGroup->peaOverrides)[pGroup->index];

	assert( !mapPos && !mapIcon );
	override->pcMapName = allocAddString( zmName );
	override->pcInteractableName = allocAddString( logicalName );
	
	// Update the UI
	MDEMissionChanged(pGroup->pMissionGroup->pDoc, true);
}

static void MDEPickInteractableOverrideCB(UIButton* pButton, MDEInteractableOverrideGroup* pGroup)
{
	InteractableOverride* override = (*pGroup->peaOverrides)[pGroup->index];
	
	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pGroup->pMissionGroup->pDoc->emDoc, true)) {
		return;
	}

	if( !emShowZeniObjectPicker( NULL, 0, false, override->pcMapName, override->pcInteractableName, NULL, NULL, NULL, NULL, MDEPickInteractableOverrideSetCB, pGroup)) {
		ui_DialogPopup("No maps available", "There are no maps available that contain this component type.");
	}
}

static void MDEFreeInteractableOverrideGroup(MDEInteractableOverrideGroup *pGroup)
{
	ui_WidgetQueueFree((UIWidget*)pGroup->pSeparator);
	ui_WidgetQueueFree((UIWidget*)pGroup->pNameLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pTagLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pMapLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pRemoveButton);
	ui_WidgetQueueFree((UIWidget*)pGroup->pPickButton);

	MEFieldSafeDestroy(&pGroup->pNameField);
	MEFieldSafeDestroy(&pGroup->pTagField);
	MEFieldSafeDestroy(&pGroup->pMapField);

	// Free the child interaction properties group
	if (pGroup->pInteractPropsGroup) {
		FreeInteractionPropertiesGroup(pGroup->pInteractPropsGroup);
	}

	eaDestroy(&pGroup->eaInteractableNames);

	// Free the group itself
	free(pGroup);
}

static void TreatAsMissionRewardToggled(UICheckButton *pButton, MDEInteractableOverrideGroup *pGroup)
{
	(*pGroup->peaOverrides[pGroup->index])->bTreatAsMissionReward = pButton->state;

	MDEMissionChanged(pGroup->pMissionGroup->pDoc, true);
}

static F32 MDEUpdateInteractableOverrideGroup(MDEInteractableOverrideGroup *pGroup, InteractableOverride *pOverride, InteractableOverride *pOrigOverride, F32 y)
{
	// Populate the list this function uses
	const char *pcMapName = zmapInfoGetPublicName(NULL);
	if (!pOverride->pcMapName || !pOverride->pcMapName[0] || (stricmp(pOverride->pcMapName, pcMapName) == 0)) {
		GERefreshVolumeAndInteractableList(&pGroup->eaInteractableNames);
	}

	if (!pGroup->pSeparator) {
		pGroup->pSeparator = ui_SeparatorCreate(UIHorizontal);
		ui_ExpanderAddChild(pGroup->pExpander, pGroup->pSeparator);
	}
	ui_WidgetSetPosition(UI_WIDGET(pGroup->pSeparator), 0, y);

	y += Y_OFFSET_SEPARATOR;

	pGroup->pMapLabel = MDERefreshLabel(pGroup->pMapLabel, "Map Name", "Name of the map on which to override the interactable.  If not supplied, override interactables of the given name on any map.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
	if (!pGroup->pMapField) {
		pGroup->pMapField = MEFieldCreateSimpleDataProvided(kMEFieldType_TextEntry, pOrigOverride, pOverride, parse_InteractableOverride, "mapName", NULL, &g_GEMapDispNames, NULL);
		MDEAddFieldToParent(pGroup->pMapField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL2, y, 0, 1.0, UIUnitPercentage, 90, pGroup->pMissionGroup->pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pMapField->pUIWidget, X_OFFSET_CONTROL2, y);
		MEFieldSetAndRefreshFromData(pGroup->pMapField, pOrigOverride, pOverride);
	}

	// Update button
	if (!pGroup->pRemoveButton) {
		pGroup->pRemoveButton = ui_ButtonCreate("Remove", 5, y, MDERemoveInteractableOverrideCB, pGroup);
		ui_WidgetSetWidth(UI_WIDGET(pGroup->pRemoveButton), 80);
		ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pRemoveButton), 5, y, 0, 0, UITopRight);
		ui_ExpanderAddChild(pGroup->pExpander, pGroup->pRemoveButton);
	} else {
		ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pRemoveButton), 5, y, 0, 0, UITopRight);
	}
	y += Y_OFFSET_ROW;

	pGroup->pNameLabel = MDERefreshLabel(pGroup->pNameLabel, "Interactable/Volume Name", "Name of the interactable or volume to override.  Either this or a tag name is required.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
	if (!pGroup->pNameField) {
		pGroup->pNameField = MEFieldCreateSimpleDataProvided(kMEFieldType_TextEntry, pOrigOverride, pOverride, parse_InteractableOverride, "interactableName", NULL, &pGroup->eaInteractableNames, NULL);
		MDEAddFieldToParent(pGroup->pNameField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL2, y, 0, 1.0, UIUnitPercentage, 90, pGroup->pMissionGroup->pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pNameField->pUIWidget, X_OFFSET_CONTROL2, y);
		MEFieldSetAndRefreshFromData(pGroup->pNameField, pOrigOverride, pOverride);
	}

	if( !pGroup->pPickButton ) {
		pGroup->pPickButton = ui_ButtonCreate( "Pick", 5, y, MDEPickInteractableOverrideCB, pGroup);
		ui_WidgetSetWidth(UI_WIDGET(pGroup->pPickButton), 80);
		ui_ExpanderAddChild(pGroup->pExpander, pGroup->pPickButton);
	}
	ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pPickButton), 5, y, 0, 0, UITopRight);
	
	y += Y_OFFSET_ROW;

	pGroup->pTagLabel = MDERefreshLabel(pGroup->pTagLabel, "Tag Name", "Tag to look for when applying this override on interactables.  Either this or an interactable name is required.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
	if (!pGroup->pTagField) {
		pGroup->pTagField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigOverride, pOverride, parse_InteractableOverride, "TypeTagName");
		MDEAddFieldToParent(pGroup->pTagField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL2, y, 0, 1.0, UIUnitPercentage, 90, pGroup->pMissionGroup->pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pTagField->pUIWidget, X_OFFSET_CONTROL2, y);
		MEFieldSetAndRefreshFromData(pGroup->pTagField, pOrigOverride, pOverride);
	}
	y += Y_OFFSET_ROW;

	if (!pGroup->pTreatAsMissionRewardButton) 
	{
		pGroup->pTreatAsMissionRewardButton = ui_CheckButtonCreate(X_OFFSET_CONTROL2, y, "Treat As Mission Reward", pOverride->bTreatAsMissionReward);
		ui_CheckButtonSetToggledCallback(pGroup->pTreatAsMissionRewardButton, TreatAsMissionRewardToggled, pGroup);
		ui_WidgetSetTooltipString(UI_WIDGET(pGroup->pTreatAsMissionRewardButton), "When checked, various UI elements will treat this interaction's Rewards block as part of the mission reward. If your game's UI hasn't been built to take advantage of this, it will have no effect.");
		ui_ExpanderAddChild(pGroup->pExpander, pGroup->pTreatAsMissionRewardButton);
	} 
	else 
	{
		ui_WidgetSetPosition(UI_WIDGET(pGroup->pTreatAsMissionRewardButton), X_OFFSET_CONTROL2, y);
	}
	ui_CheckButtonSetState(pGroup->pTreatAsMissionRewardButton, pOverride->bTreatAsMissionReward);
	y += Y_OFFSET_ROW;

	// Override properties
	if(!pGroup->pInteractPropsGroup)
	{
		pGroup->pInteractPropsGroup = calloc(sizeof(InteractionPropertiesGroup), 1);
		pGroup->pInteractPropsGroup->cbChange = MDEFieldChangedCB;
		pGroup->pInteractPropsGroup->cbPreChange = MDEFieldPreChangeCB;
		pGroup->pInteractPropsGroup->pParentData = pGroup->pMissionGroup->pDoc;
		pGroup->pInteractPropsGroup->pExpander = pGroup->pExpander;
		pGroup->pInteractPropsGroup->fOffsetX = X_OFFSET_BASE;
		pGroup->pInteractPropsGroup->eType = InteractionDefType_Node;
	}
	y = UpdateInteractionPropertiesGroup(pGroup->pInteractPropsGroup, pOverride->pPropertyEntry, pOrigOverride ? pOrigOverride->pPropertyEntry : NULL, (char*)pOverride->pcMapName, y);
	
	return y;
}



static void MDEAddSpecialDialogOverrideCB(UIButton *pButton, MDEContactGroup *pGroup)
{
	ContactDef *pContactDef = (ContactDef *)RefSystem_ReferentFromString(g_ContactDictionary, pGroup->pchContactName);
	SpecialDialogOverride *pOverride;
	static char nextName[512];
	S32 counter = 1;

	devassert(pGroup);

	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	// Add a new override
	pOverride = StructCreate(parse_SpecialDialogOverride);
	assert(pOverride);
	if (strcmp(MDE_UNASSIGNED_CONTACT_GROUP_NAME, pGroup->pchContactName) != 0)
	{
		pOverride->pcContactName = pGroup->pchContactName;
	}	
	pOverride->pSpecialDialog = StructCreate(parse_SpecialDialogBlock);
	assert(pOverride->pSpecialDialog);	
	pOverride->pSpecialDialog->bUsesLocalCondExpression = true;

	strcpy(nextName, "SpecialDialog");
	while (CEDialogFlowGetSpecialDialogByName(pGroup->pDoc->pDialogFlowWindowInfo, nextName, NULL))
	{
		sprintf(nextName, "%s%i", "SpecialDialog", counter);
		counter++;
	}
	pOverride->pSpecialDialog->name = (char*)allocAddString(nextName);

	if (pContactDef && contact_IsSingleScreen(pContactDef))
	{
		pOverride->pSpecialDialog->pCondition = exprCreate();
		exprSetOrigStrNoFilename(pOverride->pSpecialDialog->pCondition, "0");
	}

	langMakeEditorCopy(parse_SpecialDialogOverride, pOverride, true);

	//Must have at least 1 dialog block
	eaCreate(&pOverride->pSpecialDialog->dialogBlock);
	eaPush(&pOverride->pSpecialDialog->dialogBlock, StructCreate(parse_DialogBlock));

	eaPush(&pGroup->pDoc->pMission->ppSpecialDialogOverrides, pOverride);

	// Update the UI
	MDEMissionChanged(pGroup->pDoc, true);
}

static void MDESpecialDialogOverrideContactChangedCB(MEField *pField, bool bFinished, MDESpecialDialogOverrideGroup *pGroup)
{
	MissionEditDoc* pDoc = pGroup && pGroup->pContactGroup ? pGroup->pContactGroup->pDoc : NULL;
	SpecialDialogOverride* pOverride = pGroup && pGroup->peaOverrides && pGroup->index > 0 && pGroup->index < eaSize(pGroup->peaOverrides) ? eaGet(pGroup->peaOverrides, pGroup->index) : NULL;

	if(!pDoc)
		return;

	if(pOverride && pGroup->pSpecialDialogGroup && !pDoc->bIgnoreFieldChanges)
	{
		pGroup->pSpecialDialogGroup->pchContactName = pOverride->pcContactName;
	}

	MDEFieldChangedCB(pField, bFinished, pDoc);
}

static void MDEActionBlockOverrideContactChangedCB(MEField *pField, bool bFinished, MDESpecialActionBlockOverrideGroup *pGroup)
{
	MissionEditDoc* pDoc = pGroup && pGroup->pContactGroup ? pGroup->pContactGroup->pDoc : NULL;
	ActionBlockOverride* pOverride = pGroup && pGroup->peaOverrides && pGroup->index > 0 && pGroup->index < eaSize(pGroup->peaOverrides) ? eaGet(pGroup->peaOverrides, pGroup->index) : NULL;

	if(!pDoc)
		return;

	//if(pOverride && pGroup->pSpecialActionBlockGroup && !pDoc->bIgnoreFieldChanges)
	//{
	//	//pGroup->pSpecialActionBlockGroup = pOverride->pcContactName;
	//}

	MDEFieldChangedCB(pField, bFinished, pDoc);

}

static void MDEMissionOfferOverrideContactChangedCB(MEField *pField, bool bFinished, MDEMissionOfferOverrideGroup *pGroup)
{
	MissionEditDoc* pDoc = pGroup && pGroup->pContactGroup ? pGroup->pContactGroup->pDoc : NULL;
	MissionOfferOverride* pOverride = pGroup && pGroup->peaOverrides && pGroup->index > 0 && pGroup->index < eaSize(pGroup->peaOverrides) ? eaGet(pGroup->peaOverrides, pGroup->index) : NULL;

	if(!pDoc)
		return;

	if(pOverride && pGroup->pMissionOfferGroup && !pDoc->bIgnoreFieldChanges)
	{
		pGroup->pMissionOfferGroup->pchContactName = pOverride->pcContactName;
	}

	MDEFieldChangedCB(pField, bFinished, pDoc);
}

static bool MDEIsDocEditable(MissionEditDoc *pDoc)
{
	return (pDoc && emDocIsEditable(&pDoc->emDoc, true));
}

static bool MDESpecialDialogOverride_FixupMessages(MDESpecialDialogOverrideGroup *pGroup)
{
	MDEContactGroup* pContactGroup = pGroup ? pGroup->pContactGroup : NULL;
	MissionEditDoc* pDoc = pContactGroup ? pContactGroup->pDoc : NULL;
	MissionDef* pRootMission = pDoc ? pDoc->pMission : NULL;

	if(pRootMission)
	{
		MDEFixupMessages(pRootMission, pRootMission);
		return true;
	}
	return false;
}

static bool MDESpecialActionBlockOverride_FixupMessages(MDESpecialActionBlockOverrideGroup *pGroup)
{
	MDEContactGroup* pContactGroup = pGroup ? pGroup->pContactGroup : NULL;
	MissionEditDoc* pDoc = pContactGroup ? pContactGroup->pDoc : NULL;
	MissionDef* pRootMission = pDoc ? pDoc->pMission : NULL;

	if(pRootMission)
	{
		MDEFixupMessages(pRootMission, pRootMission);
		return true;
	}
	return false;
}

static SpecialDialogBlock* MDESpecialDialogBlockFromOverride(void *pData)
{
	SpecialDialogOverride *pOverride = pData ? (SpecialDialogOverride*) pData : NULL;

	if(pOverride)
	{
		return pOverride->pSpecialDialog;
	}

	return NULL;
}


static SpecialActionBlock *MDESpecialActionBlockFromOverride(void *pData)
{

	ActionBlockOverride *pOverride = pData ? (ActionBlockOverride *)pData : NULL;

	if(pOverride)
		return pOverride->pSpecialActionBlock;

	return NULL;
}
static ContactMissionOffer* MDEContactMissionOfferFromOverride(void *pData)
{
	MissionOfferOverride *pOverride = pData ? (MissionOfferOverride*) pData : NULL;

	if(pOverride)
	{
		return pOverride->pMissionOffer;
	}

	return NULL;
}

static void MDEUpdateSpecialDialogOverrideGroup(MDESpecialDialogOverrideGroup *pGroup, SpecialDialogOverride *pOverride, SpecialDialogOverride *pOrigOverride)
{
	MissionEditDoc *pDoc = pGroup->pContactGroup ? pGroup->pContactGroup->pDoc : NULL;
	MissionDef *pMissionDef = pDoc ? pDoc->pMission : NULL;
	F32 y = 0.f;

	pGroup->pNameLabel = MDERefreshLabel(pGroup->pNameLabel, "Contact Name", "Name of the contact to add the special dialog to.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
	if (!pGroup->pNameField) {
		pGroup->pNameField = MEFieldCreateSimpleDictionary(kMEFieldType_ValidatedTextEntry, pOrigOverride, pOverride, parse_SpecialDialogOverride, "ContactName", g_ContactDictionary, parse_ContactDef, "name");
		MEFieldSetChangeCallback(pGroup->pNameField, MDESpecialDialogOverrideContactChangedCB, pGroup);
		MDEAddFieldToParent(pGroup->pNameField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 0, pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pNameField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pNameField, pOrigOverride, pOverride);
	}
	y += Y_OFFSET_ROW;

	// Override properties
	if(!pGroup->pSpecialDialogGroup && pOverride && pDoc)
	{

		pGroup->pSpecialDialogGroup = CECreateSpecialDialogGroup(	pOverride->pcContactName, &pDoc->eaVarNames,
																	MDEIsDocEditable, pDoc,
																	MDEUpdateUI, pDoc,
																	MDESpecialDialogOverride_FixupMessages, pGroup,
																	MDESpecialDialogBlockFromOverride, parse_SpecialDialogOverride,
																	MDEFieldChangedCB, MDEFieldPreChangeCB, pDoc);
	}
	if(pGroup->pSpecialDialogGroup && pOverride)
		y = CERefreshSpecialGroup(pGroup->pSpecialDialogGroup, pGroup->pExpander, y, 1, pGroup->index, pGroup->peaOverrides, pOverride->pSpecialDialog, pOrigOverride?pOrigOverride->pSpecialDialog:NULL, false, pMissionDef);

	// Set the expander height
	ui_ExpanderSetHeight(pGroup->pExpander, y);
}

static void MDEGetSpecialActionBlocksFromOverrides(ActionBlockOverride ***eaOverrides, SpecialActionBlock ***eaBlocks){
	int i = 0;
	for(i = 0; i < eaSize(eaOverrides); ++i) {
		eaPush(eaBlocks, MDESpecialActionBlockFromOverride((*eaOverrides)[i]));
	}
}

static void MDEUpdateSpecialActionBlockOverrideGroup(MDESpecialActionBlockOverrideGroup *pGroup, ActionBlockOverride *pOverride, ActionBlockOverride *pOrigOverride)
{
	MissionEditDoc *pDoc = pGroup->pContactGroup ? pGroup->pContactGroup->pDoc : NULL;
	MissionDef *pMissionDef = pDoc ? pDoc->pMission : NULL;
	F32 y = 0.f;
	CECommonCallbackParams *pCommonCallbackParams = (CECommonCallbackParams *)calloc(1, sizeof(CECommonCallbackParams));

	pGroup->pNameLabel = MDERefreshLabel(pGroup->pNameLabel, "Contact Name", "Name of the contact to which to add the action block.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
	if (!pGroup->pNameField) {
		pGroup->pNameField = MEFieldCreateSimpleDictionary(kMEFieldType_ValidatedTextEntry, pOrigOverride, pOverride, parse_ActionBlockOverride, "ContactName", g_ContactDictionary, parse_ContactDef, "name");
		MEFieldSetChangeCallback(pGroup->pNameField, MDEActionBlockOverrideContactChangedCB, pGroup);
		MDEAddFieldToParent(pGroup->pNameField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 0, pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pNameField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pNameField, pOrigOverride, pOverride);
	}
	y += Y_OFFSET_ROW;

	// Override properties
	if(!pGroup->pSpecialActionBlockGroup && pOverride && pDoc)
	{

		pGroup->pSpecialActionBlockGroup = CECreateSpecialActionBlockGroupParams(	pOverride->pcContactName, &pDoc->eaVarNames,
			MDEIsDocEditable, pDoc,
			MDEUpdateUI, pDoc,
			MDESpecialActionBlockOverride_FixupMessages, pGroup,
			MDESpecialActionBlockFromOverride, parse_ActionBlockOverride,
			MDEFieldChangedCB, MDEFieldPreChangeCB, pDoc);
	}

	pCommonCallbackParams->pDocIsEditableFunc = MDEIsDocEditable;
	pCommonCallbackParams->pDocIsEditableData = pDoc;
	pCommonCallbackParams->pDialogChangedFunc = MDEUpdateUI;
	pCommonCallbackParams->pDialogChangedData = pDoc;
	pCommonCallbackParams->pMessageFixupFunc = MDESpecialActionBlockOverride_FixupMessages;
	pCommonCallbackParams->pMessageFixupData = pGroup;
	pCommonCallbackParams->pSpecialActionBlockFromWrapperFunc = MDESpecialActionBlockFromOverride;
	pCommonCallbackParams->pFieldChangeFunc = MDEFieldChangedCB;
	pCommonCallbackParams->pFieldPreChangeFunc = MDEFieldPreChangeCB;
	pCommonCallbackParams->pFieldChangeData = pDoc;

	if(pGroup->pSpecialActionBlockGroup && pOverride)
		y = CERefreshSpecialActionBlockGroup(pGroup->pSpecialActionBlockGroup, pCommonCallbackParams, pGroup->pExpander, y, pGroup->index, NULL, pGroup->peaOverrides, pOverride->pSpecialActionBlock, pOrigOverride?pOrigOverride->pSpecialActionBlock:NULL);
	
	// Set the expander height
	ui_ExpanderSetHeight(pGroup->pExpander, y);
}

static void MDEFreeContactGroupFields(MDEContactGroup *pGroup)
{
	// Free sub-groups
	eaDestroyEx(&pGroup->eaSpecialDialogOverrideGroups, MDEFreeSpecialDialogOverrideGroup);
	eaDestroyEx(&pGroup->eaMissionOfferOverrideGroups, MDEFreeMissionOfferOverrideGroup);
	eaDestroyEx(&pGroup->eaSpecialActionBlockOverrideGroups, MDEFreeSpecialActionBlockOverrideGroup);
	eaDestroyEx(&pGroup->eaImageMenuItemOverrideGroups, MDEFreeImageMenuItemOverrideGroup);

	eaDestroyEx(&pGroup->pWindow->widget.children, ui_WidgetQueueFree);

	pGroup->pExGroup = NULL;
}

static void MDEFreeContactGroup(MDEContactGroup *pGroup)
{
	// Destroy all UI fields
	MDEFreeContactGroupFields(pGroup);	

	// Free the UI resources
	ui_WidgetQueueFree(UI_WIDGET(pGroup->pWindow));
	ui_WidgetQueueFree(UI_WIDGET(pGroup->pContextMenu));

	// Free the group itself
	free(pGroup);
}

MDEContactGroup * MDEFindContactGroup(MissionEditDoc *pDoc, const char *pchContactName)
{
	if (pDoc)
	{
		FOR_EACH_IN_EARRAY(pDoc->eaContactGroups, MDEContactGroup, pCurrentGroup)
		{
			if (pCurrentGroup && stricmp(pCurrentGroup->pchContactName, pchContactName) == 0)
			{
				return pCurrentGroup;
			}
		}
		FOR_EACH_END
	}

	return NULL;
}

static void MDEOnContactGroupShowDialogFlowWindow(UIButton *pButton, MDEContactGroup *pGroup)
{
	MissionEditDoc *pDoc = MDEGetActiveDoc();

	if (pDoc && pDoc->pDialogFlowWindowInfo && pGroup && pGroup->pchContactName)
	{
		CESetContactForDialogFlowWindow(pDoc->pDialogFlowWindowInfo, pGroup->pchContactName);

		// otherwise, just show it
		pDoc->pDialogFlowWindowInfo->pDialogFlowWin->show = true;
		ui_SetFocus(pDoc->pDialogFlowWindowInfo->pDialogFlowWin);
	}
}

static MDEContactGroup * MDEGetOrCreateContactGroup(MissionEditDoc *pDoc, const char *pchContactName)
{
	MDEContactGroup *pGroup;
	char pchContactWindowName[512];
	UISprite *pSprite;
	UIButton *pShowDialogFlowWindowButton;
	
	if (pchContactName == NULL || pchContactName[0] == '\0')
	{
		pchContactName = MDE_UNASSIGNED_CONTACT_GROUP_NAME;
	}

	pGroup = MDEFindContactGroup(pDoc, pchContactName);

	if (pDoc == NULL)
	{
		return NULL;
	}

	if (pGroup)
	{
		return pGroup;
	}

	pGroup = calloc(1, sizeof(MDEContactGroup));
	pGroup->pchContactName = allocAddString(pchContactName);
	pGroup->pDoc = pDoc;

	// Create the window
	pGroup->pWindow = ui_WindowCreate(pGroup->pchContactName, 15.f, MDEFindLowYForNewEntry(pDoc), 420.f, 600);
	ui_WindowSetClosable(pGroup->pWindow, false);
	ui_WidgetSetContextCallback(UI_WIDGET(pGroup->pWindow), MDEContactWindowRightClickCB, pGroup);
	ui_ScrollAreaAddChild(pDoc->pScrollArea, UI_WIDGET(pGroup->pWindow));

	// Add the group to the document
	eaPush(&pDoc->eaContactGroups, pGroup);

	// Create the red box to distinguish this window from the mission windows
	pSprite = ui_SpriteCreate(0.f, 0.f, 0.f, 0.f, "white");
	pSprite->tint.r = 0x80;
	pSprite->tint.g = 0x00;
	pSprite->tint.b = 0x80;
	pSprite->tint.a = 0xFF;
	ui_WindowAddChild(pGroup->pWindow, pSprite);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pSprite), 1.f, 10.f, UIUnitPercentage, UIUnitFixed);

	// Show dialog flow window button
	pShowDialogFlowWindowButton = ui_ButtonCreate("Show Dialog Flow", 0.f, 13.f, MDEOnContactGroupShowDialogFlowWindow, pGroup);
	ui_WindowAddChild(pGroup->pWindow, pShowDialogFlowWindowButton);

	// Create the expander group
	pGroup->pExGroup = ui_ExpanderGroupCreate();
	ui_ExpanderGroupSetReflowCallback(pGroup->pExGroup, MDEExpandContactReflowCB, pGroup);
	ui_WidgetSetPosition(UI_WIDGET(pGroup->pExGroup), 0.f, CONTACT_GROUP_EXPANDER_GROUP_Y_OFFSET);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pGroup->pExGroup), 1.0, 1.0, UIUnitPercentage, UIUnitPercentage);
	ui_WindowAddChild(pGroup->pWindow, pGroup->pExGroup);

	sprintf(pchContactWindowName, "Contact_%s", pchContactName);

	// Create the add special dialog button
	pGroup->pAddSpecialDialogOverrideButton = ui_ButtonCreate("Add Special Dialog", 0.f, 0.f, MDEAddSpecialDialogOverrideCB, pGroup);
	ui_WidgetSetWidth(UI_WIDGET(pGroup->pAddSpecialDialogOverrideButton), 140);
	ui_WindowAddChild(pGroup->pWindow, pGroup->pAddSpecialDialogOverrideButton);

	// Create the add mission offer override button
	pGroup->pAddMissionOfferOverrideButton = ui_ButtonCreate("Add Mission Offer", 0.f, 0.f, MDEAddMissionOfferOverrideCB, pGroup);
	ui_WidgetSetWidth(UI_WIDGET(pGroup->pAddMissionOfferOverrideButton), 140);
	ui_WindowAddChild(pGroup->pWindow, pGroup->pAddMissionOfferOverrideButton);

	// Create the add special action block override button
	pGroup->pAddActionBlockOverrideButton = ui_ButtonCreate("Add Action Block", 0.f, 0.f, MDEAddActionBlockOverrideCB, pGroup);
	ui_WidgetSetWidth(UI_WIDGET(pGroup->pAddActionBlockOverrideButton), 140);
	ui_WindowAddChild(pGroup->pWindow, pGroup->pAddActionBlockOverrideButton);

	pGroup->pAddImageMenuItemOverrideButton = ui_ButtonCreate("Add Image Menu Item", 0.f, 0.f, MDEAddImageMenuItemOverrideCB, pGroup);
	ui_WidgetSetWidth(UI_WIDGET(pGroup->pAddImageMenuItemOverrideButton), 140);
	ui_WindowAddChild(pGroup->pWindow, pGroup->pAddImageMenuItemOverrideButton);

	// Get the window position if already stored
	EditorPrefGetWindowPosition(MISSION_EDITOR, "Window Position", pchContactWindowName, pGroup->pWindow);

	return pGroup;
}

static void MDEBeginContactGroupsDisplayUpdate(MissionEditDoc *pDoc)
{
	if (pDoc == NULL)
	{
		return;
	}

	FOR_EACH_IN_EARRAY(pDoc->eaContactGroups, MDEContactGroup, pCurrentGroup)
	{
		if (pCurrentGroup)
		{
			pCurrentGroup->iTmpSpecialDialogCount = 0;
			pCurrentGroup->iTmpSpecialActionBlockCount = 0;
			pCurrentGroup->iTmpMissionOfferCount = 0;
			pCurrentGroup->iTmpImageMenuItemCount = 0;
		}
	}
	FOR_EACH_END
}

static void MDEEndContactGroupsDisplayUpdate(MissionEditDoc *pDoc)
{
	if (pDoc == NULL)
	{
		return;
	}

	FOR_EACH_IN_EARRAY(pDoc->eaContactGroups, MDEContactGroup, pCurrentGroup)
	{
		if (pCurrentGroup)
		{
			// Destroy all unused groups
			if (pCurrentGroup->eaSpecialDialogOverrideGroups)
			{
				while (pCurrentGroup->iTmpSpecialDialogCount < eaSize(&pCurrentGroup->eaSpecialDialogOverrideGroups)) 
				{
					MDEFreeSpecialDialogOverrideGroup(pCurrentGroup->eaSpecialDialogOverrideGroups[pCurrentGroup->iTmpSpecialDialogCount]);
					eaRemove(&pCurrentGroup->eaSpecialDialogOverrideGroups, pCurrentGroup->iTmpSpecialDialogCount);
				}
			}

			if (pCurrentGroup->eaMissionOfferOverrideGroups)
			{
				while (pCurrentGroup->iTmpMissionOfferCount < eaSize(&pCurrentGroup->eaMissionOfferOverrideGroups)) 
				{
					MDEFreeMissionOfferOverrideGroup(pCurrentGroup->eaMissionOfferOverrideGroups[pCurrentGroup->iTmpMissionOfferCount]);
					eaRemove(&pCurrentGroup->eaMissionOfferOverrideGroups, pCurrentGroup->iTmpMissionOfferCount);
				}
			}

			while (pCurrentGroup->iTmpImageMenuItemCount < eaSize(&pCurrentGroup->eaImageMenuItemOverrideGroups)) {
				MDEFreeImageMenuItemOverrideGroup(pCurrentGroup->eaImageMenuItemOverrideGroups[pCurrentGroup->iTmpImageMenuItemCount]);
				eaRemove(&pCurrentGroup->eaImageMenuItemOverrideGroups, pCurrentGroup->iTmpImageMenuItemCount);
			}

			MDERefreshAddButtonPositions(pCurrentGroup);
		}
	}
	FOR_EACH_END
}

static void MDEUpdateSpecialDialogOverrideExpander(MissionEditDoc *pDoc)
{
	F32 y = 0;
	S32 i;
	MDESpecialDialogOverrideGroup ***peaOverrideGroups;
	SpecialDialogOverride ***peaOverrides = &pDoc->pMission->ppSpecialDialogOverrides;
	SpecialDialogOverride ***peaOrigOverrides = pDoc->pOrigMission ? &pDoc->pOrigMission->ppSpecialDialogOverrides : NULL;

	for (i = 0; i < eaSize(peaOverrides); ++i) 
	{
		MDESpecialDialogOverrideGroup *pGroup;
		SpecialDialogOverride *pOverride;
		SpecialDialogOverride *pOrigOverride = NULL;

		// Get the contact group
		MDEContactGroup *pContactGroup = MDEGetOrCreateContactGroup(pDoc, ((*peaOverrides)[i])->pcContactName);

		// Get the special dialog override groups in the contact group
		peaOverrideGroups = &pContactGroup->eaSpecialDialogOverrideGroups;

		if (pContactGroup->iTmpSpecialDialogCount >= eaSize(peaOverrideGroups)) 
		{
			pGroup = calloc(1, sizeof(MDESpecialDialogOverrideGroup));
			pGroup->pContactGroup = pContactGroup;			
			eaPush(peaOverrideGroups, pGroup);
		} else {
			pGroup = (*peaOverrideGroups)[pContactGroup->iTmpSpecialDialogCount];
		}

		pGroup->peaOverrides = peaOverrides;
		pGroup->peaOrigOverrides = peaOrigOverrides;
		pGroup->index = i;
		pOverride = (*peaOverrides)[i];
		if (peaOrigOverrides && (i < eaSize(peaOrigOverrides))) {
			pOrigOverride = (*peaOrigOverrides)[i];
		}

		{
			char *estrTitle = NULL;
			char *estrPrefName = NULL;			

			estrStackCreate(&estrTitle);
			estrStackCreate(&estrPrefName);
			if (pOverride && pOverride->pSpecialDialog && pOverride->pSpecialDialog->name)
			{
				estrConcatf(&estrTitle, "Special Dialog: %s", pOverride->pSpecialDialog->name);
				estrConcatf(&estrPrefName, "SpecialDialogOverrideExpanderPref_%s_%s", pOverride->pcContactName, pOverride->pSpecialDialog->name);
			}
			else
			{
				estrCopy2(&estrTitle, "Special Dialog: [Unnamed Special Dialog]");
				estrConcatf(&estrPrefName, "SpecialDialogOverrideExpanderPref_%s_%d", pOverride->pcContactName, i);
			}

			if (pGroup->pExpander == NULL)
			{
				pGroup->pExpander = MDECreateExpander(pContactGroup->pExGroup, estrTitle, estrPrefName, true);
				ui_ExpanderSetExpandCallback(pGroup->pExpander, MDESpecialExpandChangeCB, pGroup);
			}
			else
			{
				// To update the ordering remove/add from/to expander group
				ui_ExpanderGroupRemoveExpander(pContactGroup->pExGroup, pGroup->pExpander);
				ui_ExpanderGroupAddExpander(pContactGroup->pExGroup, pGroup->pExpander);
				ui_ExpanderSetOpened(pGroup->pExpander, EditorPrefGetInt(MISSION_EDITOR, "Expander", estrPrefName, true));

				// Set the title of the expander
				ui_WidgetSetTextString(UI_WIDGET(pGroup->pExpander), estrTitle);
			}
			estrDestroy(&estrTitle);
			estrDestroy(&estrPrefName);
		}

		MDEUpdateSpecialDialogOverrideGroup(pGroup, pOverride, pOrigOverride);
		++pContactGroup->iTmpSpecialDialogCount;
	}
}

static void MDEUpdateActionBlockOverrideExpander(MissionEditDoc *pDoc)
{
	F32 y = 0;
	S32 i;
	MDESpecialActionBlockOverrideGroup ***peaOverrideGroups;
	ActionBlockOverride ***peaOverrides = &pDoc->pMission->ppSpecialActionBlockOverrides;
	ActionBlockOverride ***peaOrigOverrides = pDoc->pOrigMission ? &pDoc->pOrigMission->ppSpecialActionBlockOverrides : NULL;

	for (i = 0; i < eaSize(peaOverrides); ++i) 
	{
		MDESpecialActionBlockOverrideGroup *pGroup;
		ActionBlockOverride *pOverride;
		ActionBlockOverride *pOrigOverride = NULL;

		// Get the contact group
		MDEContactGroup *pContactGroup = MDEGetOrCreateContactGroup(pDoc, ((*peaOverrides)[i])->pcContactName);

		// Get the special dialog override groups in the contact group
		peaOverrideGroups = &pContactGroup->eaSpecialActionBlockOverrideGroups;

		if (pContactGroup->iTmpSpecialActionBlockCount >= eaSize(peaOverrideGroups)) 
		{
			pGroup = (MDESpecialActionBlockOverrideGroup *)calloc(1, sizeof(MDESpecialActionBlockOverrideGroup));
			pGroup->pContactGroup = pContactGroup;			
			eaPush(peaOverrideGroups, pGroup);
		} else {
			pGroup = (*peaOverrideGroups)[pContactGroup->iTmpSpecialActionBlockCount];
		}

		pGroup->peaOverrides = peaOverrides;
		pGroup->peaOrigOverrides = peaOrigOverrides;
		pGroup->index = i;
		pOverride = (*peaOverrides)[i];
		if (peaOrigOverrides && (i < eaSize(peaOrigOverrides))) {
			pOrigOverride = (*peaOrigOverrides)[i];
		}

		{
			char *estrTitle = NULL;
			char *estrPrefName = NULL;			

			estrStackCreate(&estrTitle);
			estrStackCreate(&estrPrefName);
			if (pOverride && pOverride->pSpecialActionBlock && pOverride->pSpecialActionBlock->name)
			{
				estrConcatf(&estrTitle, "Special Action Block: %s", pOverride->pSpecialActionBlock->name);
				estrConcatf(&estrPrefName, "ActionBlockOverrideExpanderPref_%s_%s", pOverride->pcContactName, pOverride->pSpecialActionBlock->name);
			}
			else
			{
				estrCopy2(&estrTitle, "Special Action Block: [Unnamed Special Action Block]");
				estrConcatf(&estrPrefName, "ActionBlockOverrideExpanderPref_%s_%d", pOverride->pcContactName, i);
			}

			if (pGroup->pExpander == NULL)
			{
				pGroup->pExpander = MDECreateExpander(pContactGroup->pExGroup, estrTitle, estrPrefName, true);
				ui_ExpanderSetExpandCallback(pGroup->pExpander, MDESpecialExpandChangeCB, pGroup);
			}
			else
			{
				// To update the ordering remove/add from/to expander group
				ui_ExpanderGroupRemoveExpander(pContactGroup->pExGroup, pGroup->pExpander);
				ui_ExpanderGroupAddExpander(pContactGroup->pExGroup, pGroup->pExpander);
				ui_ExpanderSetOpened(pGroup->pExpander, EditorPrefGetInt(MISSION_EDITOR, "Expander", estrPrefName, true));

				// Set the title of the expander
				ui_WidgetSetTextString(UI_WIDGET(pGroup->pExpander), estrTitle);
			}
			estrDestroy(&estrTitle);
			estrDestroy(&estrPrefName);
		}

		MDEUpdateSpecialActionBlockOverrideGroup(pGroup, pOverride, pOrigOverride);
		++pContactGroup->iTmpSpecialActionBlockCount;
	}
}

static void MDEUpdateMissionOfferOverrideGroup(MDEMissionOfferOverrideGroup *pGroup, MissionOfferOverride *pOverride, MissionOfferOverride *pOrigOverride)
{
	F32 y = 0;
	MissionEditDoc *pDoc = pGroup->pContactGroup ? pGroup->pContactGroup->pDoc : NULL;
	MissionDef *pMissionDef = pDoc ? pDoc->pMission : NULL;

	pGroup->pNameLabel = MDERefreshLabel(pGroup->pNameLabel, "Contact Name", "Name of the contact to add the mission offer to.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
	if (!pGroup->pNameField) {
		pGroup->pNameField = MEFieldCreateSimpleDictionary(kMEFieldType_ValidatedTextEntry, pOrigOverride, pOverride, parse_MissionOfferOverride, "ContactName", g_ContactDictionary, parse_ContactDef, "name");
		MEFieldSetChangeCallback(pGroup->pNameField, MDEMissionOfferOverrideContactChangedCB, pGroup);
		MDEAddFieldToParent(pGroup->pNameField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 0, pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pNameField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pNameField, pOrigOverride, pOverride);
	}
	y += Y_OFFSET_ROW;

	// Override properties
	if(!pGroup->pMissionOfferGroup && pOverride && pDoc)
	{
		pGroup->pMissionOfferGroup = CECreateOfferGroup(pOverride->pcContactName,
			MDEIsDocEditable, pDoc,
			MDEUpdateUI, pDoc,
			NULL, NULL,
			MDEContactMissionOfferFromOverride, parse_MissionOfferOverride,
			MDEFieldChangedCB, MDEFieldPreChangeCB, pDoc);
	}
	if(pGroup->pMissionOfferGroup && pOverride)
		y = CERefreshOfferGroup(pGroup->pMissionOfferGroup, pGroup->pExpander, y, 1, pGroup->index, pGroup->peaOverrides, pOverride->pMissionOffer, pOrigOverride ? pOrigOverride->pMissionOffer : NULL, false, pMissionDef);

	// Set the expander height
	ui_ExpanderSetHeight(pGroup->pExpander, y);
}

static void MDEUpdateMissionOfferOverrideExpander(MissionEditDoc *pDoc)
{
	F32 y = 0;
	S32 i;

	MDEMissionOfferOverrideGroup*** peaOverrideGroups;
	MissionOfferOverride*** peaOverrides = &pDoc->pMission->ppMissionOfferOverrides;	
	MissionOfferOverride*** peaOrigOverrides = pDoc->pOrigMission ? &pDoc->pOrigMission->ppMissionOfferOverrides : NULL;

	for (i=0; i < eaSize(peaOverrides); ++i) 
	{
		MDEMissionOfferOverrideGroup *pGroup;
		MissionOfferOverride *pOverride;
		MissionOfferOverride *pOrigOverride = NULL;

		// Get the contact group
		MDEContactGroup *pContactGroup = MDEGetOrCreateContactGroup(pDoc, ((*peaOverrides)[i])->pcContactName);

		// Get the mission offer override groups in the contact group
		peaOverrideGroups = &pContactGroup->eaMissionOfferOverrideGroups;

		if (pContactGroup->iTmpMissionOfferCount >= eaSize(peaOverrideGroups)) 
		{
			pGroup = calloc(1, sizeof(MDEMissionOfferOverrideGroup));
			pGroup->pContactGroup = pContactGroup;
			eaPush(peaOverrideGroups, pGroup);
		} else {
			pGroup = (*peaOverrideGroups)[pContactGroup->iTmpMissionOfferCount];
		}

		pGroup->peaOverrides = peaOverrides;
		pGroup->peaOrigOverrides = peaOrigOverrides;
		pGroup->index = i;
		pOverride = (*peaOverrides)[i];
		if (peaOrigOverrides && (i < eaSize(peaOrigOverrides))) {
			pOrigOverride = (*peaOrigOverrides)[i];
		}

		{
			MissionDef *pMissionDef;
			char *estrTitle = NULL;
			char *estrPrefName = NULL;			

			estrStackCreate(&estrTitle);
			estrStackCreate(&estrPrefName);
			if (pOverride && pOverride->pMissionOffer && (pMissionDef = GET_REF(pOverride->pMissionOffer->missionDef)))
			{
				estrConcatf(&estrTitle, "Mission Offer: %s", pMissionDef->name);
				estrConcatf(&estrPrefName, "MissionOfferOverrideExpanderPref_%s_%s", pOverride->pcContactName, pMissionDef->name);
			}
			else
			{
				estrCopy2(&estrTitle, "Mission Offer: [No mission selected]");
				estrConcatf(&estrPrefName, "MissionOfferOverrideExpanderPref_%s_%d", pOverride->pcContactName, pGroup->index);
			}

			if (pGroup->pExpander == NULL)
			{
				pGroup->pExpander = MDECreateExpander(pContactGroup->pExGroup, estrTitle, estrPrefName, true);
				ui_ExpanderSetExpandCallback(pGroup->pExpander, MDEMissionOfferExpandChangeCB, pGroup);
			}
			else
			{
				// To update the ordering remove/add from/to expander group
				ui_ExpanderGroupRemoveExpander(pContactGroup->pExGroup, pGroup->pExpander);
				ui_ExpanderGroupAddExpander(pContactGroup->pExGroup, pGroup->pExpander);

				// Set the title of the expander
				ui_WidgetSetTextString(UI_WIDGET(pGroup->pExpander), estrTitle);
			}
			estrDestroy(&estrTitle);
			estrDestroy(&estrPrefName);
		}

		MDEUpdateMissionOfferOverrideGroup(pGroup, pOverride, pOrigOverride);
		++pContactGroup->iTmpMissionOfferCount;
	}
}

static ContactImageMenuItem* MDEImageMenuItemFromOverride(void *pData)
{
	ImageMenuItemOverride* pOverride = pData;

	if(pOverride)
	{
		return pOverride->pImageMenuItem;
	}

	return NULL;
}

static void MDEUpdateImageMenuItemOverrideExpander(MissionEditDoc *pDoc)
{
	ImageMenuItemOverride*** peaOverrides = &pDoc->pMission->ppImageMenuItemOverrides;	
	ImageMenuItemOverride*** peaOrigOverrides = pDoc->pOrigMission ? &pDoc->pOrigMission->ppImageMenuItemOverrides : NULL;
	
	int i;

	for( i = 0; i < eaSize(peaOverrides); ++i ) {
		ImageMenuItemOverride* pOverride = (*peaOverrides)[i];
		ImageMenuItemOverride* pOrigOverride = (peaOrigOverrides ? eaGet( peaOrigOverrides, i ) : NULL);
		MDEContactGroup* pContactGroup = MDEGetOrCreateContactGroup(pDoc, pOverride->pcContactName);
		MDEImageMenuItemOverrideGroup* pGroup = NULL;
		float y = 0;

		if( pContactGroup->iTmpImageMenuItemCount >= eaSize( &pContactGroup->eaImageMenuItemOverrideGroups )) {
			pGroup = calloc( 1, sizeof( *pGroup ));
			pGroup->pContactGroup = pContactGroup;
			eaPush( &pContactGroup->eaImageMenuItemOverrideGroups, pGroup );
		} else {
			pGroup = pContactGroup->eaImageMenuItemOverrideGroups[ pContactGroup->iTmpImageMenuItemCount ];
		}
		pGroup->peaOverrides = peaOverrides;
		pGroup->peaOrigOverrides = peaOrigOverrides;
		pGroup->index = i;

		if (pGroup->pExpander == NULL) {
			pGroup->pExpander = MDECreateExpander(pContactGroup->pExGroup, "Image Menu Item", NULL, true);
		}
		else
		{
			// To update the ordering remove/add from/to expander group
			ui_ExpanderGroupRemoveExpander(pContactGroup->pExGroup, pGroup->pExpander);
			ui_ExpanderGroupAddExpander(pContactGroup->pExGroup, pGroup->pExpander);

			// Set the title of the expander
			ui_WidgetSetTextString(UI_WIDGET(pGroup->pExpander), "Image Menu Item");
		}

		pGroup->pNameLabel = MDERefreshLabel(pGroup->pNameLabel, "Contact Name", "Name of the contact to which to add the action block.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
		if (!pGroup->pNameField) {
			pGroup->pNameField = MEFieldCreateSimpleDictionary(kMEFieldType_ValidatedTextEntry, pOrigOverride, pOverride, parse_ActionBlockOverride, "ContactName", g_ContactDictionary, parse_ContactDef, "name");
			MEFieldSetChangeCallback(pGroup->pNameField, MDEActionBlockOverrideContactChangedCB, pGroup);
			MDEAddFieldToParent(pGroup->pNameField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 0, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pNameField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pNameField, pOrigOverride, pOverride);
		}
		y += Y_OFFSET_ROW;

		if( !pGroup->pImageMenuItemGroup ) {
			pGroup->pImageMenuItemGroup = CECreateImageMenuItemGroup( pOverride->pcContactName,
																	  MDEIsDocEditable, pDoc,
																	  MDEUpdateUI, pDoc,
																	  NULL, NULL,
																	  MDEImageMenuItemFromOverride, parse_ImageMenuItemOverride,
																	  MDEFieldChangedCB, MDEFieldPreChangeCB, pDoc );
		}
		if(pGroup->pImageMenuItemGroup && pOverride)
			y = CERefreshImageMenuItemGroup(pGroup->pImageMenuItemGroup, pGroup->pExpander, y, i,
											peaOverrides, peaOrigOverrides);

		// Set the expander height
		ui_ExpanderSetHeight(pGroup->pExpander, y);
		
		++pContactGroup->iTmpImageMenuItemCount;
	}
}

static void MDEUpdateInteractableOverrideExpander(MDEMissionGroup *pMissionGroup, bool bIsRoot)
{
	F32 y = Y_OFFSET_ROW;
	int i;
	InteractableOverride*** peaOverrides = &pMissionGroup->pMission->ppInteractableOverrides;
	MDEInteractableOverrideGroup*** peaOverrideGroups = &pMissionGroup->eaInteractableOverrideGroups;
	InteractableOverride*** peaOrigOverrides = pMissionGroup->pOrigMission ? &pMissionGroup->pOrigMission->ppInteractableOverrides : NULL;

	if (!pMissionGroup->pInteractableOverrideExpander)
		return;

	for(i=0; i<eaSize(peaOverrides); ++i) {
		MDEInteractableOverrideGroup *pGroup;
		InteractableOverride *pOverride;
		InteractableOverride *pOrigOverride = NULL;

		if (i >= eaSize(peaOverrideGroups)) {
			pGroup = calloc(1, sizeof(MDEInteractableOverrideGroup));
			pGroup->pMissionGroup = pMissionGroup;
			pGroup->pExpander = pMissionGroup->pInteractableOverrideExpander;
			eaPush(peaOverrideGroups, pGroup);
		} else {
			pGroup = (*peaOverrideGroups)[i];
		}

		pGroup->peaOverrides = peaOverrides;
		pGroup->peaOrigOverrides = peaOrigOverrides;
		pGroup->index = i;
		pOverride = (*peaOverrides)[i];
		if (peaOrigOverrides && (i < eaSize(peaOrigOverrides))) {
			pOrigOverride = (*peaOrigOverrides)[i];
		}

		y = MDEUpdateInteractableOverrideGroup(pGroup, pOverride, pOrigOverride, y);
	}

	// Clean up extra groups
	while(i < eaSize(peaOverrideGroups)) {
		MDEFreeInteractableOverrideGroup((*peaOverrideGroups)[i]);
		eaRemove(peaOverrideGroups, i);
	}


	// Set the expander height
	ui_ExpanderSetHeight(pMissionGroup->pInteractableOverrideExpander, y);
}


static void MDEAddScoreEventCB(UIButton *pButton, MDEMissionGroup *pGroup)
{
	OpenMissionScoreEvent *pScoreEvent;

	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	// Add a new override
	pScoreEvent = StructCreate(parse_OpenMissionScoreEvent);
	eaPush(&pGroup->pMission->eaOpenMissionScoreEvents, pScoreEvent);

	// Update the UI
	MDEMissionChanged(pGroup->pDoc, true);
}

static void MDERemoveScoreEventCB(UIButton *pButton, MDEScoreEventGroup *pGroup)
{
	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pGroup->pMissionGroup->pDoc->emDoc, true)) {
		return;
	}

	// Remove the group
	StructDestroy(parse_OpenMissionScoreEvent, (*pGroup->peaEvents)[pGroup->index]);
	eaRemove(pGroup->peaEvents, pGroup->index);

	// Update the UI
	MDEMissionChanged(pGroup->pMissionGroup->pDoc, true);
}

static void MDEFreeScoreEventGroup(MDEScoreEventGroup *pGroup)
{
	// Free Event Editor if it's open
	if (pGroup->pEventEditor){
		eventeditor_Destroy(pGroup->pEventEditor);
		pGroup->pEventEditor = NULL;
	}

	ui_WidgetQueueFree((UIWidget*)pGroup->pSeparator);
	ui_WidgetQueueFree((UIWidget*)pGroup->pRemoveButton);
	ui_WidgetQueueFree((UIWidget*)pGroup->pEventButton);
	ui_WidgetQueueFree((UIWidget*)pGroup->pScaleLabel);
	MEFieldSafeDestroy(&pGroup->pScaleField);

	// Free the group itself
	free(pGroup);
}

static void MDEAddMapCB(UIButton* pButton, MDEMissionGroup *pGroup)
{	
	MissionMap* pMap;

	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	// Add a new override
	pMap = StructCreate(parse_MissionMap);
	eaPush(&pGroup->pMission->eaObjectiveMaps, pMap);

	// Update the UI
	MDEMissionChanged(pGroup->pDoc, true);
}

static void MDERemoveMapCB(UIButton *pButton, MDEMapGroup *pGroup)
{
	MissionMap** eaObjectiveMaps = pGroup->pMissionGroup->pMission->eaObjectiveMaps;
	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pGroup->pMissionGroup->pDoc->emDoc, true)) {
		return;
	}

	// Remove the group
	StructDestroy(parse_MissionMap, eaObjectiveMaps[pGroup->index]);
	eaRemove(&eaObjectiveMaps, pGroup->index);

	// Update the UI
	MDEMissionChanged(pGroup->pMissionGroup->pDoc, true);
}

static void MDERemoveWaypointCB(UIButton *pButton, MDEWaypointGroup *pGroup)
{
	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pGroup->pMissionGroup->pDoc->emDoc, true)) {
		return;
	}

	// Remove the group
	StructDestroy(parse_MissionWaypoint, (*pGroup->peaWaypoints)[pGroup->index]);
	eaRemove(pGroup->peaWaypoints, pGroup->index);

	// Update the UI
	MDEMissionChanged(pGroup->pMissionGroup->pDoc, true);
}

static void MDEAddWaypointCB(UIButton *pButton, MDEMissionGroup *pGroup)
{
	MissionWaypoint *pWaypoint;

	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	// Add a new override
	pWaypoint = StructCreate(parse_MissionWaypoint);
	eaPush(&pGroup->pMission->eaWaypoints, pWaypoint);

	// Update the UI
	MDEMissionChanged(pGroup->pDoc, true);
}

static void MDEAddMapVarCB(UIButton *pButton, MDEMapGroup *pGroup)
{
	WorldVariable *pNewVar;

	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pGroup->pMissionGroup->pDoc->emDoc, true)) {
		return;
	}

	pNewVar = StructCreate(parse_WorldVariable);
	eaPush(&pGroup->pMap->eaWorldVars, pNewVar);

	// Notify of change
	MDEMissionChanged(pGroup->pMissionGroup->pDoc, true);
}

static void MDEAddEventCB(UIButton *pButton, MDEMissionGroup *pGroup)
{
	GameEvent *pEvent;

	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	// Add a new override
	pEvent = StructCreate(parse_GameEvent);
	eaPush(&pGroup->pMission->eaTrackedEvents, pEvent);

	pEvent->type = EventType_Kills;
	if (pGroup->pDoc->pMission && missiondef_GetType(pGroup->pDoc->pMission) != MissionType_OpenMission){
		pEvent->tMatchSourceTeam = TriState_Yes;
	}

	// Update the UI
	MDEMissionChanged(pGroup->pDoc, true);
}

static void MDERemoveEventCB(UIButton *pButton, MDEEventGroup *pGroup)
{
	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pGroup->pMissionGroup->pDoc->emDoc, true)) {
		return;
	}

	// Remove the group
	StructDestroy(parse_GameEvent, (*pGroup->peaEvents)[pGroup->index]);
	eaRemove(pGroup->peaEvents, pGroup->index);

	// Update the UI
	MDEMissionChanged(pGroup->pMissionGroup->pDoc, true);
}

static void MDEFreeEventGroup(MDEEventGroup *pGroup)
{
	// Free Event Editor if it's open
	if (pGroup->pEventEditor){
		eventeditor_Destroy(pGroup->pEventEditor);
		pGroup->pEventEditor = NULL;
	}

	ui_WidgetQueueFree((UIWidget*)pGroup->pSeparator);
	ui_WidgetQueueFree((UIWidget*)pGroup->pRemoveButton);
	ui_WidgetQueueFree((UIWidget*)pGroup->pEventButton);
	ui_WidgetQueueFree((UIWidget*)pGroup->pEventNameLabel);

	// Free the group itself
	free(pGroup);
}

static void MDEFreeWaypointGroup(MDEWaypointGroup *pGroup)
{
	ui_WidgetQueueFree((UIWidget*)pGroup->pSeparator);
	ui_WidgetQueueFree((UIWidget*)pGroup->pRemoveButton);
	ui_WidgetQueueFree((UIWidget*)pGroup->pTypeLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pLocationLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pMapLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pAnyMapLabel);

	MEFieldSafeDestroy(&pGroup->pAnyMapField);
	MEFieldSafeDestroy(&pGroup->pLocationField);
	MEFieldSafeDestroy(&pGroup->pMapField);
	MEFieldSafeDestroy(&pGroup->pTypeField);

	// Free the group itself
	free(pGroup);
}

static void MDEAddRequestCB(UIButton *pButton, MDEMissionGroup *pGroup)
{
	MissionDefRequest *pRequest;

	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	// Add a new Request
	pRequest = StructCreate(parse_MissionDefRequest);
	eaPush(&pGroup->pMission->eaRequests, pRequest);

	// Update the UI
	MDEMissionChanged(pGroup->pDoc, true);
}

static void MDERemoveRequestCB(UIButton *pButton, MDERequestGroup *pGroup)
{
	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pGroup->pMissionGroup->pDoc->emDoc, true)) {
		return;
	}

	// Remove the group
	StructDestroy(parse_MissionDefRequest, (*pGroup->peaRequests)[pGroup->index]);
	eaRemove(pGroup->peaRequests, pGroup->index);

	// Update the UI
	MDEMissionChanged(pGroup->pMissionGroup->pDoc, true);
}

static void MDEFreeRequestGroup(MDERequestGroup *pGroup)
{
	ui_WidgetQueueFree((UIWidget*)pGroup->pSeparator);
	ui_WidgetQueueFree((UIWidget*)pGroup->pRemoveButton);
	ui_WidgetQueueFree((UIWidget*)pGroup->pMissionLabel);
	ui_WidgetQueueFree((UIWidget*)pGroup->pTypeLabel);

	MEFieldSafeDestroy(&pGroup->pMissionField);
	MEFieldSafeDestroy(&pGroup->pMissionSetField);
	MEFieldSafeDestroy(&pGroup->pTypeField);

	// Free the group itself
	free(pGroup);
}


static void MDEScoreEventChangedCB(EventEditor *pEventEditor, MDEScoreEventGroup *pGroup)
{
	bool bChanged = false;
	if (pGroup->pEventEditor && emDocIsEditable(&pGroup->pMissionGroup->pDoc->emDoc, true))
	{
		GameEvent *pEvent = eventeditor_GetBoundEvent(pGroup->pEventEditor);
		OpenMissionScoreEvent *pScoreEvent = eaGet(pGroup->peaEvents, pGroup->index);
		if (pScoreEvent){
			if (pScoreEvent->pEvent && pEvent){
				if (StructCompare(parse_GameEvent, pScoreEvent->pEvent, pEvent, 0, 0, 0) != 0){
					StructCopy(parse_GameEvent, pEvent, pScoreEvent->pEvent, 0, 0, 0);
					bChanged = true;
				}
			}else if (pScoreEvent->pEvent || pEvent){
				pScoreEvent->pEvent = StructClone(parse_GameEvent, pEvent);
				bChanged = true;
			}
		}
		eventeditor_Destroy(pGroup->pEventEditor);
		pGroup->pEventEditor = NULL;
	}
	if (bChanged)
		MDEMissionChanged(pGroup->pMissionGroup->pDoc, true);
}

static void MDEScoreEventEditorClosedCB(EventEditor *pEventEditor, MDEScoreEventGroup *pGroup)
{
	pGroup->pEventEditor = NULL;
}

static void MDEOpenScoreEventEditor(UIButton *pButton, MDEScoreEventGroup *pGroup)
{
	OpenMissionScoreEvent *pScoreEvent = eaGet(pGroup->peaEvents, pGroup->index);
	if (!pGroup->pEventEditor){
		pGroup->pEventEditor = eventeditor_CreateConst(pScoreEvent?pScoreEvent->pEvent:NULL, MDEScoreEventChangedCB, pGroup, false);
	}
	eventeditor_Open(pGroup->pEventEditor);
	eventeditor_SetCloseFunc(pGroup->pEventEditor, MDEScoreEventEditorClosedCB, pGroup);
}

static F32 MDEUpdateScoreEventGroup(MDEScoreEventGroup *pGroup, OpenMissionScoreEvent *pEvent, OpenMissionScoreEvent *pOrigEvent, F32 y)
{
	GameEvent *pGameEvent = pEvent?pEvent->pEvent:NULL;
	GameEvent *pOrigGameEvent = pOrigEvent?pOrigEvent->pEvent:NULL;

	if (!pGroup->pSeparator) {
		pGroup->pSeparator = ui_SeparatorCreate(UIHorizontal);
		ui_ExpanderAddChild(pGroup->pExpander, pGroup->pSeparator);
	}
	ui_WidgetSetPosition(UI_WIDGET(pGroup->pSeparator), 0, y);
	y += Y_OFFSET_SEPARATOR;

	// Update Remove button
	if (!pGroup->pRemoveButton) {
		pGroup->pRemoveButton = ui_ButtonCreate("Remove", 5, y, MDERemoveScoreEventCB, pGroup);
		ui_WidgetSetWidth(UI_WIDGET(pGroup->pRemoveButton), 80);
		ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pRemoveButton), 5, y, 0, 0, UITopRight);
		ui_ExpanderAddChild(pGroup->pExpander, pGroup->pRemoveButton);
	} else {
		ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pRemoveButton), 5, y, 0, 0, UITopRight);
	}

	// If Event Editor is open, close it
	if (pGroup->pEventEditor){
		eventeditor_Destroy(pGroup->pEventEditor);
		pGroup->pEventEditor = NULL;
	}

	// Update Event button
	if (!pGroup->pEventButton){
		pGroup->pEventButton = ui_ButtonCreate("", X_OFFSET_BASE, y, MDEOpenScoreEventEditor, pGroup);
		ui_ExpanderAddChild(pGroup->pExpander, pGroup->pEventButton);
		ui_WidgetSetWidthEx(UI_WIDGET(pGroup->pEventButton), 1.0, UIUnitPercentage);
		ui_WidgetSetPaddingEx(UI_WIDGET(pGroup->pEventButton), 0, 86, 0, 0);

		// I'm using a label to display the text since I don't want the text centered
		pGroup->pEventLabel = ui_LabelCreate("", 0, 0);
		ui_WidgetSetDimensionsEx(UI_WIDGET(pGroup->pEventLabel), 1.0, 1.0, UIUnitPercentage, UIUnitPercentage);
		ui_WidgetAddChild(UI_WIDGET(pGroup->pEventButton), UI_WIDGET(pGroup->pEventLabel));
	} else {
		ui_WidgetSetPosition(UI_WIDGET(pGroup->pEventButton), X_OFFSET_BASE, y);
	}
	// Update text on Event button
	if (pGameEvent && pGroup->pEventLabel){
		char *estr = NULL;
		estrStackCreate(&estr);
		gameevent_WriteEventSingleLine(pGameEvent, &estr);
		ui_LabelSetText(pGroup->pEventLabel, estr);
		ui_WidgetSetTooltipString(UI_WIDGET(pGroup->pEventButton), estr);
		estrDestroy(&estr);
	} else if (pGroup->pEventLabel){
		ui_LabelSetText(pGroup->pEventLabel, "Create Event");
	}

	// Change color of button if the Event is changed
	if (pGroup->pEventButton && (!pOrigEvent || (!!pGameEvent != !!pOrigGameEvent) || (pGameEvent && pOrigGameEvent && StructCompare(parse_GameEvent, pGameEvent, pOrigGameEvent, 0, 0, 0) != 0))){
		ui_SetChanged(UI_WIDGET(pGroup->pEventButton), true);
	}else if(pGroup->pEventButton){
		ui_SetChanged(UI_WIDGET(pGroup->pEventButton), false);
	}

	y += Y_OFFSET_ROW;

	// Update Scale field
	pGroup->pScaleLabel = MDERefreshLabel(pGroup->pScaleLabel, "Points", "Scales how many points each Event is worth", X_OFFSET_BASE, 0, y, pGroup->pExpander);
	if (!pGroup->pScaleField) {
		pGroup->pScaleField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigEvent, pEvent, parse_OpenMissionScoreEvent, "scale");
		MDEAddFieldToParent(pGroup->pScaleField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 80, UIUnitFixed, 0, pGroup->pMissionGroup->pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pScaleField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pScaleField, pOrigEvent, pEvent);
	}
	y += Y_OFFSET_ROW;


	return y;
}

static void MDEUpdateScoreboardExpander(MDEMissionGroup *pMissionGroup, bool bIsRoot)
{
	MissionDef *pDef = pMissionGroup->pMission;
	UIExpander *pExpander = pMissionGroup->pScoreboardExpander;
	OpenMissionScoreEvent*** peaEvents = &pMissionGroup->pMission->eaOpenMissionScoreEvents;
	OpenMissionScoreEvent*** peaOrigEvents = pMissionGroup->pOrigMission ? &pMissionGroup->pOrigMission->eaOpenMissionScoreEvents : NULL;
	MDEScoreEventGroup*** peaScoreEventGroups = &pMissionGroup->eaScoreEventGroups;
	F32 y = Y_OFFSET_ROW;
	int i;

	if (!pExpander)
		return;

	if (missiondef_GetType(pDef) != MissionType_OpenMission){
		ui_ExpanderGroupRemoveExpander(pMissionGroup->pExGroup, pExpander);
	} else {
		if (!pExpander->group)
			ui_ExpanderGroupAddExpander(pMissionGroup->pExGroup, pExpander);

		for(i=0; i<eaSize(peaEvents); ++i) {
			MDEScoreEventGroup *pGroup;
			OpenMissionScoreEvent *pEvent;
			OpenMissionScoreEvent *pOrigEvent = NULL;

			if (i >= eaSize(peaScoreEventGroups)) {
				pGroup = calloc(1, sizeof(MDEScoreEventGroup));
				pGroup->pMissionGroup = pMissionGroup;
				pGroup->pExpander = pExpander;
				eaPush(peaScoreEventGroups, pGroup);
			} else {
				pGroup = (*peaScoreEventGroups)[i];
			}

			pGroup->peaEvents = peaEvents;
			pGroup->peaOrigEvents = peaOrigEvents;
			pGroup->index = i;
			pEvent = (*peaEvents)[i];
			if (peaOrigEvents && (i < eaSize(peaOrigEvents))) {
				pOrigEvent = (*peaOrigEvents)[i];
			}

			y = MDEUpdateScoreEventGroup(pGroup, pEvent, pOrigEvent, y);
		}

		// Clean up extra groups
		while(i < eaSize(peaScoreEventGroups)) {
			MDEFreeScoreEventGroup((*peaScoreEventGroups)[i]);
			eaRemove(peaScoreEventGroups, i);
		}

		// Set the expander height
		ui_ExpanderSetHeight(pMissionGroup->pScoreboardExpander, y);
	}
}

static F32 MDEUpdateRequestGroup(MDERequestGroup *pGroup, MissionDefRequest *pRequest, MissionDefRequest *pOrigRequest, F32 y)
{
	if (!pGroup->pSeparator) {
		pGroup->pSeparator = ui_SeparatorCreate(UIHorizontal);
		ui_ExpanderAddChild(pGroup->pExpander, pGroup->pSeparator);
	}
	ui_WidgetSetPosition(UI_WIDGET(pGroup->pSeparator), 0, y);
	y += Y_OFFSET_SEPARATOR;

	// Update Remove button
	if (!pGroup->pRemoveButton) {
		pGroup->pRemoveButton = ui_ButtonCreate("Remove", 5, y, MDERemoveRequestCB, pGroup);
		ui_WidgetSetWidth(UI_WIDGET(pGroup->pRemoveButton), 80);
		ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pRemoveButton), 5, y, 0, 0, UITopRight);
		ui_ExpanderAddChild(pGroup->pExpander, pGroup->pRemoveButton);
	} else {
		ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pRemoveButton), 5, y, 0, 0, UITopRight);
	}

	// Update Request Type field
	pGroup->pTypeLabel = MDERefreshLabel(pGroup->pTypeLabel, "Type", "The Type of this Mission Request", X_OFFSET_BASE, 0, y, pGroup->pExpander);
	if (!pGroup->pTypeField) {
		pGroup->pTypeField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pOrigRequest, pRequest, parse_MissionDefRequest, "type", MissionDefRequestTypeEnum);
		MDEAddFieldToParent(pGroup->pTypeField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 80, UIUnitFixed, 0, pGroup->pMissionGroup->pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pTypeField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pTypeField, pOrigRequest, pRequest);
	}
	y += Y_OFFSET_ROW;

	if (pRequest->eType == MissionDefRequestType_Mission)
	{
		// Update Mission field
		pGroup->pMissionLabel = MDERefreshLabel(pGroup->pMissionLabel, "Mission", "The Mission that should be requested", X_OFFSET_BASE, 0, y, pGroup->pExpander);
		if (!pGroup->pMissionField) {
			pGroup->pMissionField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_ValidatedTextEntry, pOrigRequest, pRequest, parse_MissionDefRequest, "RequestedDef", "MissionDef", "resourceName");
			MDEAddFieldToParent(pGroup->pMissionField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 0, pGroup->pMissionGroup->pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pMissionField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pMissionField, pOrigRequest, pRequest);
		}
		y += Y_OFFSET_ROW;

		MEFieldSafeDestroy(&pGroup->pMissionSetField);
	}
	else
	{
		// Update MissionSet field
		pGroup->pMissionLabel = MDERefreshLabel(pGroup->pMissionLabel, "Mission Set", "The MissionSet that should be chosen from", X_OFFSET_BASE, 0, y, pGroup->pExpander);
		if (!pGroup->pMissionSetField) {
			pGroup->pMissionSetField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_ValidatedTextEntry, pOrigRequest, pRequest, parse_MissionDefRequest, "RequestedMissionSet", "MissionSet", "resourceName");
			MDEAddFieldToParent(pGroup->pMissionSetField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 0, pGroup->pMissionGroup->pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pMissionSetField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pMissionSetField, pOrigRequest, pRequest);
		}
		y += Y_OFFSET_ROW;

		MEFieldSafeDestroy(&pGroup->pMissionField);
	}


	return y;
}

static void MDEUpdateRequestsExpander(MDEMissionGroup *pMissionGroup, bool bIsRoot)
{
	MissionDef *pDef = pMissionGroup->pMission;
	UIExpander *pExpander = pMissionGroup->pRequestsExpander;
	MissionDefRequest*** peaRequests = &pMissionGroup->pMission->eaRequests;
	MissionDefRequest*** peaOrigRequests = pMissionGroup->pOrigMission ? &pMissionGroup->pOrigMission->eaRequests : NULL;
	MDERequestGroup*** peaRequestGroups = &pMissionGroup->eaRequestGroups;
	F32 y = Y_OFFSET_ROW;
	int i;

	if (missiondef_GetType(pDef) != MissionType_NemesisArc && missiondef_GetType(pDef) != MissionType_NemesisSubArc){
		ui_ExpanderGroupRemoveExpander(pMissionGroup->pExGroup, pExpander);
	} else {
		if (!pExpander->group)
			ui_ExpanderGroupAddExpander(pMissionGroup->pExGroup, pExpander);

		if (!pExpander)
			return;

		if (missiondef_GetType(pDef) == MissionType_OpenMission || missiondef_GetType(pDef) == MissionType_Perk){
			ui_ExpanderGroupRemoveExpander(pMissionGroup->pExGroup, pExpander);
		} else {
			if (!pExpander->group)
				ui_ExpanderGroupAddExpander(pMissionGroup->pExGroup, pExpander);

			for(i=0; i<eaSize(peaRequests); ++i) {
				MDERequestGroup *pGroup;
				MissionDefRequest *pRequest;
				MissionDefRequest *pOrigRequest = NULL;

				if (i >= eaSize(peaRequestGroups)) {
					pGroup = calloc(1, sizeof(MDERequestGroup));
					pGroup->pMissionGroup = pMissionGroup;
					pGroup->pExpander = pExpander;
					eaPush(peaRequestGroups, pGroup);
				} else {
					pGroup = (*peaRequestGroups)[i];
				}

				pGroup->peaRequests = peaRequests;
				pGroup->peaOrigRequests = peaOrigRequests;
				pGroup->index = i;
				pRequest = (*peaRequests)[i];
				if (peaOrigRequests && (i < eaSize(peaOrigRequests))) {
					pOrigRequest = (*peaOrigRequests)[i];
				}

				y = MDEUpdateRequestGroup(pGroup, pRequest, pOrigRequest, y);
			}

			// Clean up extra groups
			while(i < eaSize(peaRequestGroups)) {
				MDEFreeRequestGroup((*peaRequestGroups)[i]);
				eaRemove(peaRequestGroups, i);
			}

			// Set the expander height
			ui_ExpanderSetHeight(pMissionGroup->pRequestsExpander, y);
		}
	}
}

static void MDEEventChangedCB(EventEditor *pEventEditor, MDEEventGroup *pGroup)
{
	bool bChanged = false;
	if (pGroup->pEventEditor && emDocIsEditable(&pGroup->pMissionGroup->pDoc->emDoc, true))
	{
		GameEvent *pEvent = eventeditor_GetBoundEvent(pGroup->pEventEditor);
		GameEvent *pMissionEvent = eaGet(pGroup->peaEvents, pGroup->index);

		if (pMissionEvent && pEvent){
			if (StructCompare(parse_GameEvent, pMissionEvent, pEvent, 0, 0, 0) != 0){
				StructCopy(parse_GameEvent, pEvent, pMissionEvent, 0, 0, 0);
				bChanged = true;
				
				// Add an autogenerated name if no name was set
				if (!pMissionEvent->pchEventName){
					pMissionEvent->pchEventName = allocAddString(StaticDefineIntRevLookup(EventTypeEnum, pMissionEvent->type));
					MDEMakeUniqueEventName(pGroup->peaEvents, pMissionEvent);
				}
			}
		}
		eventeditor_Destroy(pGroup->pEventEditor);
		pGroup->pEventEditor = NULL;
	}
	if (bChanged)
		MDEMissionChanged(pGroup->pMissionGroup->pDoc, true);
}

static void MDEEventEditorClosedCB(EventEditor *pEventEditor, MDEEventGroup *pGroup)
{
	pGroup->pEventEditor = NULL;
}

static void MDEOpenEventEditor(UIButton *pButton, MDEEventGroup *pGroup)
{
	GameEvent *pEvent = eaGet(pGroup->peaEvents, pGroup->index);
	if (!pGroup->pEventEditor){
		pGroup->pEventEditor = eventeditor_CreateConst(pEvent, MDEEventChangedCB, pGroup, true);
	}
	eventeditor_Open(pGroup->pEventEditor);
	eventeditor_SetCloseFunc(pGroup->pEventEditor, MDEEventEditorClosedCB, pGroup);
}

static void MDEEventContextMenuCopy(UIMenuItem *pItem, MDEEventGroup *pGroup)
{
	GameEvent *pEvent = eaGet(pGroup->peaEvents, pGroup->index);
	if (pEvent){
		char *buffer = NULL;
		estrStackCreate(&buffer);
		gameevent_WriteEventEscaped(pEvent, &buffer);
		winCopyToClipboard(buffer);
		estrDestroy(&buffer);
	}
}


static void MDEMakeUniqueEventName(GameEvent*** peaEvents, GameEvent *pEvent)
{
	char *pchDesiredName = strdup(pEvent->pchEventName);
	char *tmp;
	int count = 1;
	int i;

	// Remove counts from the end of the event name (so if we copy event_2 we don't get event_2_2)
	tmp = pchDesiredName + strlen(pchDesiredName)-1;
	while (tmp > pchDesiredName && (*tmp >= '0' && *tmp <= '9'))
		--tmp;
	if (tmp > pchDesiredName && *tmp == '_')
		*tmp = '\0';

	while (count < 100){
		// Append a count to the name
		char *buffer = NULL;
		estrStackCreate(&buffer);
		estrPrintf(&buffer, "%s_%d", pchDesiredName, count);
		pEvent->pchEventName = allocAddString(buffer);
		estrDestroy(&buffer);

		// Look for a duplicate name
		for (i = 0; i < eaSize(peaEvents); i++){
			if ((*peaEvents)[i] != pEvent && (*peaEvents)[i]->pchEventName == pEvent->pchEventName)
				break;
		}
		if (i == eaSize(peaEvents)){
			// done, exit loop
			break;
		}
		count++;
	}
	free(pchDesiredName);
}

static void MDEEventContextMenuPaste(UIMenuItem *pItem, MDEEventGroup *pGroup)
{
	bool bChanged = false;
	if (emDocIsEditable(&pGroup->pMissionGroup->pDoc->emDoc, true))
	{
		GameEvent *pNewEvent = gameevent_EventFromString(winCopyFromClipboard());
		GameEvent *pOldEvent = eaGet(pGroup->peaEvents, pGroup->index);

		if (pOldEvent && pNewEvent){
			if (StructCompare(parse_GameEvent, pOldEvent, pNewEvent, 0, 0, 0) != 0){
				StructCopy(parse_GameEvent, pNewEvent, pOldEvent, 0, 0, 0);
				// Add an autogenerated name if no name was set
				if (!pOldEvent->pchEventName){
					pOldEvent->pchEventName = allocAddString(StaticDefineIntRevLookup(EventTypeEnum, pNewEvent->type));
				}
				MDEMakeUniqueEventName(pGroup->peaEvents, pOldEvent);
				bChanged = true;
			}
		}
		StructDestroy(parse_GameEvent, pNewEvent);
	}
	if (bChanged)
		MDEMissionChanged(pGroup->pMissionGroup->pDoc, true);
}

static void MDEEventContextMenuCallback(UIExpander *widget_UNUSED, MDEEventGroup *pGroup)
{
	if (!pGroup->pMissionGroup->pContextMenu) {
		pGroup->pMissionGroup->pContextMenu = ui_MenuCreate(NULL);
	}else{
		ui_MenuClearAndFreeItems(pGroup->pMissionGroup->pContextMenu);
	}

	ui_MenuAppendItems(pGroup->pMissionGroup->pContextMenu,
		ui_MenuItemCreate("Copy",UIMenuCallback, MDEEventContextMenuCopy, pGroup, NULL),
		ui_MenuItemCreate("Paste",UIMenuCallback, MDEEventContextMenuPaste, pGroup, NULL),
		NULL);

	ui_MenuPopupAtCursor(pGroup->pMissionGroup->pContextMenu);
}


static F32 MDEUpdateEventGroup(MDEEventGroup *pGroup, GameEvent *pGameEvent, GameEvent *pOrigGameEvent, F32 y)
{
	if (!pGroup->pSeparator) {
		pGroup->pSeparator = ui_SeparatorCreate(UIHorizontal);
		ui_ExpanderAddChild(pGroup->pExpander, pGroup->pSeparator);
	}
	ui_WidgetSetPosition(UI_WIDGET(pGroup->pSeparator), 0, y);
	y += Y_OFFSET_SEPARATOR;

	// Update Name label

	// Update Remove button
	if (!pGroup->pRemoveButton) {
		pGroup->pRemoveButton = ui_ButtonCreate("Remove", 5, y, MDERemoveEventCB, pGroup);
		ui_WidgetSetWidth(UI_WIDGET(pGroup->pRemoveButton), 80);
		ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pRemoveButton), 5, y, 0, 0, UITopRight);
		ui_ExpanderAddChild(pGroup->pExpander, pGroup->pRemoveButton);
	} else {
		ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pRemoveButton), 5, y, 0, 0, UITopRight);
	}

	// If Event Editor is open, close it
	if (pGroup->pEventEditor){
		eventeditor_Destroy(pGroup->pEventEditor);
		pGroup->pEventEditor = NULL;
	}

	// Update Event Name label
	if (!pGroup->pEventNameLabel){
		pGroup->pEventNameLabel = ui_LabelCreate("", X_OFFSET_BASE, y);
		ui_ExpanderAddChild(pGroup->pExpander, UI_WIDGET(pGroup->pEventNameLabel));
		if (pGameEvent && pGameEvent->pchEventName){
			ui_LabelSetText(pGroup->pEventNameLabel, pGameEvent->pchEventName);
		}else{
			ui_LabelSetText(pGroup->pEventNameLabel, "<unnamed>");
		}
	} else {
		ui_WidgetSetPosition(UI_WIDGET(pGroup->pEventNameLabel), X_OFFSET_BASE, y);
		if (pGameEvent && pGameEvent->pchEventName){
			ui_LabelSetText(pGroup->pEventNameLabel, pGameEvent->pchEventName);
		}else{
			ui_LabelSetText(pGroup->pEventNameLabel, "<unnamed>");
		}
	}

	// Update Event button
	if (!pGroup->pEventButton){
		pGroup->pEventButton = ui_ButtonCreate("", X_OFFSET_CONTROL, y, MDEOpenEventEditor, pGroup);
		ui_ExpanderAddChild(pGroup->pExpander, pGroup->pEventButton);
		ui_WidgetSetWidthEx(UI_WIDGET(pGroup->pEventButton), 1.0, UIUnitPercentage);
		ui_WidgetSetPaddingEx(UI_WIDGET(pGroup->pEventButton), 0, 86, 0, 0);

		// I'm using a label to display the text since I don't want the text centered
		pGroup->pEventLabel = ui_LabelCreate("", 0, 0);
		ui_WidgetSetDimensionsEx(UI_WIDGET(pGroup->pEventLabel), 1.0, 1.0, UIUnitPercentage, UIUnitPercentage);
		ui_WidgetAddChild(UI_WIDGET(pGroup->pEventButton), UI_WIDGET(pGroup->pEventLabel));
		ui_WidgetSetContextCallback(UI_WIDGET(pGroup->pEventButton), MDEEventContextMenuCallback, pGroup);
	} else {
		ui_WidgetSetPosition(UI_WIDGET(pGroup->pEventButton), X_OFFSET_CONTROL, y);
	}
	// Update text on Event button
	if (pGameEvent && pGroup->pEventLabel){
		char *estr = NULL;
		const char *pchEventName = pGameEvent->pchEventName;
		pGameEvent->pchEventName = NULL;
		estrStackCreate(&estr);
		gameevent_WriteEventSingleLine(pGameEvent, &estr);
		pGameEvent->pchEventName = pchEventName;
		ui_LabelSetText(pGroup->pEventLabel, estr);
		ui_WidgetSetTooltipString(UI_WIDGET(pGroup->pEventButton), estr);
		estrDestroy(&estr);
	} else if (pGroup->pEventLabel){
		ui_LabelSetText(pGroup->pEventLabel, "Create Event");
	}

	// Change color of button if the Event is changed
	if (pGroup->pEventButton && ((!!pGameEvent != !!pOrigGameEvent) || (pGameEvent && pOrigGameEvent && StructCompare(parse_GameEvent, pGameEvent, pOrigGameEvent, 0, 0, 0) != 0))){
		ui_SetChanged(UI_WIDGET(pGroup->pEventButton), true);
	}else if(pGroup->pEventButton){
		ui_SetChanged(UI_WIDGET(pGroup->pEventButton), false);
	}

	y += Y_OFFSET_ROW;

	return y;
}

static void MDEUpdateEventExpander(MDEMissionGroup *pMissionGroup, bool bIsRoot)
{
	MissionDef *pDef = pMissionGroup->pMission;
	UIExpander *pExpander = pMissionGroup->pEventExpander;
	GameEvent*** peaEvents = &pMissionGroup->pMission->eaTrackedEvents;
	GameEvent*** peaOrigEvents = pMissionGroup->pOrigMission ? &pMissionGroup->pOrigMission->eaTrackedEvents : NULL;
	MDEEventGroup*** peaEventGroups = &pMissionGroup->eaEventGroups;
	F32 y = Y_OFFSET_ROW;
	int i;

	if (!pExpander)
		return;

	for(i=0; i<eaSize(peaEvents); ++i) {
		MDEEventGroup *pGroup;
		GameEvent *pEvent;
		GameEvent *pOrigEvent = NULL;

		if (i >= eaSize(peaEventGroups)) {
			pGroup = calloc(1, sizeof(MDEEventGroup));
			pGroup->pMissionGroup = pMissionGroup;
			pGroup->pExpander = pExpander;
			eaPush(peaEventGroups, pGroup);
		} else {
			pGroup = (*peaEventGroups)[i];
		}

		pGroup->peaEvents = peaEvents;
		pGroup->peaOrigEvents = peaOrigEvents;
		pGroup->index = i;
		pEvent = (*peaEvents)[i];
		if (peaOrigEvents && (i < eaSize(peaOrigEvents))) {
			pOrigEvent = (*peaOrigEvents)[i];
		}

		y = MDEUpdateEventGroup(pGroup, pEvent, pOrigEvent, y);
	}

	// Clean up extra groups
	while(i < eaSize(peaEventGroups)) {
		MDEFreeEventGroup((*peaEventGroups)[i]);
		eaRemove(peaEventGroups, i);
	}

	// Set the expander height
	ui_ExpanderSetHeight(pMissionGroup->pEventExpander, y);
}

static F32 MDEUpdateWaypointGroup(MDEWaypointGroup *pGroup, MissionWaypoint *pWaypoint, MissionWaypoint *pOrigWaypoint, F32 y)
{
	const char *pchWaypointLocationText = NULL;
	if (!pGroup->pSeparator) {
		pGroup->pSeparator = ui_SeparatorCreate(UIHorizontal);
		ui_ExpanderAddChild(pGroup->pExpander, pGroup->pSeparator);
	}
	ui_WidgetSetPosition(UI_WIDGET(pGroup->pSeparator), 0, y);
	y += Y_OFFSET_SEPARATOR;

	// Update Waypoint Type
	pGroup->pTypeLabel = MDERefreshLabel(pGroup->pTypeLabel, "Waypoint Type", "The waypoint type", X_OFFSET_BASE, 0, y, pGroup->pExpander);
	if (!pGroup->pTypeField) {
		pGroup->pTypeField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pOrigWaypoint, pWaypoint, parse_MissionWaypoint, "type", MissionWaypointTypeEnum);
		MDEAddFieldToParent(pGroup->pTypeField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 95, pGroup->pMissionGroup->pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pTypeField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pTypeField, pOrigWaypoint, pWaypoint);
	}

	// Update Remove button
	if (!pGroup->pRemoveButton) {
		pGroup->pRemoveButton = ui_ButtonCreate("Remove", 5, y, MDERemoveWaypointCB, pGroup);
		ui_WidgetSetWidth(UI_WIDGET(pGroup->pRemoveButton), 80);
		ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pRemoveButton), 5, y, 0, 0, UITopRight);
		ui_ExpanderAddChild(pGroup->pExpander, pGroup->pRemoveButton);
	} else {
		ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pRemoveButton), 5, y, 0, 0, UITopRight);
	}

	y += Y_OFFSET_ROW;

	if (pWaypoint->type != MissionWaypointType_None)
	{

		// Update Waypoint Location
		switch (pWaypoint->type)
		{
			xcase MissionWaypointType_Clicky:
				pchWaypointLocationText = "Waypt Clicky";
			xcase MissionWaypointType_Volume:
				pchWaypointLocationText = "Waypt Volume";
			xcase MissionWaypointType_AreaVolume:
				pchWaypointLocationText = "WP Area Volume";
			xcase MissionWaypointType_NamedPoint:
				pchWaypointLocationText = "WP Named Pnt";
			xcase MissionWaypointType_Encounter:
				pchWaypointLocationText = "WP Encounter";
			xcase MissionWaypointType_None:
				pchWaypointLocationText = "Waypt Disabled";
			xdefault:
				pchWaypointLocationText = "Waypt Unknown";
				Alertf("Mission editor encountered an unsupported waypoint type and needs to be updated.  Contact the mission team programmers.");
		}

		pGroup->pLocationLabel = MDERefreshLabel(pGroup->pLocationLabel, pchWaypointLocationText, pchWaypointLocationText, X_OFFSET_BASE, 0, y, pGroup->pExpander);
		if (!pGroup->pLocationField) {
			pGroup->pLocationField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigWaypoint, pWaypoint, parse_MissionWaypoint, "name");
			MDEAddFieldToParent(pGroup->pLocationField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 3, pGroup->pMissionGroup->pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pLocationField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pLocationField, pOrigWaypoint, pWaypoint);
		}

		y += Y_OFFSET_ROW;

		// Update Waypoint Map
		pGroup->pMapLabel = MDERefreshLabel(pGroup->pMapLabel, "Waypoint Map", "The waypoint map", X_OFFSET_BASE, 0, y, pGroup->pExpander);
		if (!pGroup->pMapField) {
			pGroup->pMapField = MEFieldCreateSimpleDataProvided(kMEFieldType_TextEntry, pOrigWaypoint, pWaypoint, parse_MissionWaypoint, "mapName", NULL, &g_GEMapDispNames, NULL);
			MDEAddFieldToParent(pGroup->pMapField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 200, UIUnitFixed, 3, pGroup->pMissionGroup->pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pMapField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pMapField, pOrigWaypoint, pWaypoint);
		}

		pGroup->pAnyMapLabel = MDERefreshLabel(pGroup->pAnyMapLabel, "Any", "Any map", X_OFFSET_CONTROL + 225, 0, y, pGroup->pExpander);
		if (!pGroup->pAnyMapField) {
			pGroup->pAnyMapField = MEFieldCreateSimple(kMEFieldType_Check, pOrigWaypoint, pWaypoint, parse_MissionWaypoint, "AnyMap");
			MDEAddFieldToParent(pGroup->pAnyMapField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL + 205, y, 0, 20, UIUnitFixed, 3, pGroup->pMissionGroup->pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pAnyMapField->pUIWidget, X_OFFSET_CONTROL + 205, y);
			MEFieldSetAndRefreshFromData(pGroup->pAnyMapField, pOrigWaypoint, pWaypoint);
		}

		y += Y_OFFSET_ROW;
	}

	return y;
}

static void MDEUpdateWaypointExpander(MDEMissionGroup *pMissionGroup, bool bIsRoot)
{
	MissionDef *pDef = pMissionGroup->pMission;
	UIExpander *pExpander = pMissionGroup->pWaypointExpander;
	MissionWaypoint*** peaWaypoints = &pMissionGroup->pMission->eaWaypoints;
	MissionWaypoint*** peaOrigWaypoints = pMissionGroup->pOrigMission ? &pMissionGroup->pOrigMission->eaWaypoints : NULL;
	MDEWaypointGroup*** peaWaypointGroups = &pMissionGroup->eaWaypointGroups;
	F32 y = Y_OFFSET_ROW;
	int i;

	if (!pExpander)
		return;

	for(i=0; i<eaSize(peaWaypoints); ++i) {
		MDEWaypointGroup *pGroup;
		MissionWaypoint *pWaypoint;
		MissionWaypoint *pOrigWaypoint = NULL;

		if (i >= eaSize(peaWaypointGroups)) {
			pGroup = calloc(1, sizeof(MDEWaypointGroup));
			pGroup->pMissionGroup = pMissionGroup;
			pGroup->pExpander = pExpander;
			eaPush(peaWaypointGroups, pGroup);
		} else {
			pGroup = (*peaWaypointGroups)[i];
		}

		pGroup->peaWaypoints = peaWaypoints;
		pGroup->peaOrigWaypoints = peaOrigWaypoints;
		pGroup->index = i;
		pWaypoint = (*peaWaypoints)[i];
		if (peaOrigWaypoints && (i < eaSize(peaOrigWaypoints))) {
			pOrigWaypoint = (*peaOrigWaypoints)[i];
		}

		y = MDEUpdateWaypointGroup(pGroup, pWaypoint, pOrigWaypoint, y);
	}

	// Clean up extra groups
	while(i < eaSize(peaWaypointGroups)) {
		MDEFreeWaypointGroup((*peaWaypointGroups)[i]);
		eaRemove(peaWaypointGroups, i);
	}

	// Set the expander height
	ui_ExpanderSetHeight(pMissionGroup->pWaypointExpander, y);
}

static F32 MDEUpdateMapGroup(MDEMapGroup *pGroup, SA_PARAM_NN_VALID MissionMap* pMap, MissionMap* pOrigMap, F32 y)
{
	ZoneMapInfo* pMapInfo = pMap && pMap->pchMapName ? zmapInfoGetByPublicName(pMap->pchMapName) : NULL;
	int i;

	pGroup->pMap = pMap;
	pGroup->pOrigMap = pOrigMap;

	if (!pGroup->pSeparator) {
		pGroup->pSeparator = ui_SeparatorCreate(UIHorizontal);
		ui_ExpanderAddChild(pGroup->pExpander, pGroup->pSeparator);
	}
	ui_WidgetSetPosition(UI_WIDGET(pGroup->pSeparator), 0, y);
	y += Y_OFFSET_SEPARATOR;

	// Update Map Field
	pGroup->pMapLabel = MDERefreshLabel(pGroup->pMapLabel, "Objective Map", "The map the mission is on", X_OFFSET_BASE, 0, y, pGroup->pExpander);
	if (!pGroup->pMapField) {
		pGroup->pMapField = MEFieldCreateSimpleDataProvided(kMEFieldType_ValidatedTextEntry, pOrigMap, pMap, parse_MissionMap, "MapName", NULL, &g_GEMapDispNames, NULL);
		MDEAddFieldToParent(pGroup->pMapField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 95, pGroup->pMissionGroup->pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pMapField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pMapField, pOrigMap, pMap);
	}
 
	// Update Remove button
	if (!pGroup->pRemoveMapButton) {
		pGroup->pRemoveMapButton = ui_ButtonCreate("Remove", 5, y, MDERemoveMapCB, pGroup);
		ui_WidgetSetWidth(UI_WIDGET(pGroup->pRemoveMapButton), 80);
		ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pRemoveMapButton), 5, y, 0, 0, UITopRight);
		ui_ExpanderAddChild(pGroup->pExpander, pGroup->pRemoveMapButton);
	} else {
		ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pRemoveMapButton), 5, y, 0, 0, UITopRight);
	}

	y += Y_OFFSET_ROW;
	
	// Update Hide Go to Field
	pGroup->pHideGotoLabel = MDERefreshLabel(pGroup->pHideGotoLabel, "Hide Goto String", "If true the \"Go to <mapname>\" string will not appear in the mission tracker UI for this objective map", X_OFFSET_BASE, 0, y, pGroup->pExpander);
	if (!pGroup->pHideGotoField) {
		pGroup->pHideGotoField = MEFieldCreateSimple(kMEFieldType_Check, pOrigMap, pMap, parse_MissionMap, "HideGotoString");
		MDEAddFieldToParent(pGroup->pHideGotoField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 95, pGroup->pMissionGroup->pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pHideGotoField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pHideGotoField, pOrigMap, pMap);
	}

	y += Y_OFFSET_ROW;

	if(!pGroup->pAddVarButton) {
		pGroup->pAddVarButton = ui_ButtonCreate("Add Variable", X_OFFSET_BASE, y, MDEAddMapVarCB, pGroup);
		ui_WidgetSetWidth(UI_WIDGET(pGroup->pAddVarButton), 120);
		ui_ExpanderAddChild(pGroup->pExpander, pGroup->pAddVarButton); 
	}
	ui_WidgetSetPosition(UI_WIDGET(pGroup->pAddVarButton), X_OFFSET_BASE, y);


	y += Y_OFFSET_ROW;

	// Collect variable defs and def names
	if (pMapInfo) {
		eaClear(&pGroup->eaVarDefs);
		eaClear(&pGroup->eaVarNames);
		for(i=0; i < zmapInfoGetVariableCount(pMapInfo); i++) {
			WorldVariableDef *pVarDef = zmapInfoGetVariableDef(pMapInfo, i);
			if(pVarDef) {
				eaPush(&pGroup->eaVarDefs, pVarDef);
				if(pVarDef->pcName)
					eaPush(&pGroup->eaVarNames, pVarDef->pcName);
			}
		}	
	}

	// Iterate the fields
	for(i=0; i<eaSize(&pMap->eaWorldVars); ++i) {
		GEVariableGroup *pVarGroup;
		WorldVariable *pVar, *pOrigVar;

		if (i<eaSize(&pGroup->eaVarGroups)) {
			pVarGroup = pGroup->eaVarGroups[i];
		} else {
			pVarGroup = calloc(1,sizeof(GEVariableGroup));
			pVarGroup->index = i;
			pVarGroup->pData = pGroup;
			eaPush(&pGroup->eaVarGroups, pVarGroup);
		}

		pVar = pMap->eaWorldVars[i];
		if (pOrigMap && pOrigMap->eaWorldVars && (i<eaSize(&pOrigMap->eaWorldVars))) {
			pOrigVar = pOrigMap->eaWorldVars[i];
		} else {
			pOrigVar = NULL;
		}

		y = GEUpdateVariableGroup(pVarGroup, UI_WIDGET(pGroup->pExpander), &pMap->eaWorldVars, pVar, pOrigVar, &pGroup->eaVarDefs, &pGroup->eaVarNames, i, y, MDEFieldChangedCB, MDEFieldPreChangeCB, pGroup->pMissionGroup->pDoc);
	}
	// Free unused variable groups
	if(pGroup->eaVarGroups) {
		for(i=eaSize(&pGroup->eaVarGroups)-1; i>=eaSize(&pMap->eaWorldVars); --i) {
			GEFreeVariableGroup(pGroup->eaVarGroups[i]);
			eaRemove(&pGroup->eaVarGroups, i);
		}
	}
	

	y += Y_OFFSET_ROW;

	return y;
}


static void MDEUpdateMapsExpander(MDEMissionGroup *pMissionGroup, bool bIsRoot)
{
	MissionDef *pDef = pMissionGroup->pMission;
	UIExpander *pExpander = pMissionGroup->pMapsExpander;
	MissionMap** eaMaps = pMissionGroup->pMission->eaObjectiveMaps;
	MissionMap** eaOrigMaps = pMissionGroup->pOrigMission ? pMissionGroup->pOrigMission->eaObjectiveMaps : NULL;
	MDEMapGroup*** peaMapGroups = &pMissionGroup->eaMapGroups;
	F32 y = Y_OFFSET_ROW;
	int i;

	if (!pExpander)
		return;

	for(i=0; i<eaSize(&eaMaps); i++) {
		MDEMapGroup *pGroup;
		MissionMap *pMap = eaMaps[i];
		MissionMap *pOrigMap = eaSize(&eaOrigMaps) > i ? eaOrigMaps[i] : NULL;
		
		if(i >= eaSize(peaMapGroups)) {
			pGroup = calloc(1, sizeof(MDEMapGroup));
			pGroup->pMissionGroup = pMissionGroup;
			pGroup->pExpander = pExpander;
			eaPush(peaMapGroups, pGroup);
		} else {
				pGroup = (*peaMapGroups)[i];
		}

		pGroup->index = i;
		
		y = MDEUpdateMapGroup(pGroup, pMap, pOrigMap, y);
	}

	// Clean up extra groups
	while(i < eaSize(peaMapGroups)) {
		MDEFreeMapGroup((*peaMapGroups)[i]);
		eaRemove(peaMapGroups, i);
	}

	// Set the expander height
	ui_ExpanderSetHeight(pMissionGroup->pMapsExpander, y);
}

static F32 MDEUpdateLevelGroup(MDELevelGroup *pGroup, F32 y)
{
	MDEMissionGroup *pMissionGroup = pGroup ? pGroup->pMissionGroup : NULL;
	MissionDef *pMission = pMissionGroup ? pMissionGroup->pMission : NULL;
	MissionDef *pOrigMission = pMissionGroup ? pMissionGroup->pOrigMission : NULL;
	UIExpander *pExpander = pGroup ? pGroup->pExpander : NULL;

	if(pGroup && pExpander && pMission && pMissionGroup) {
		if (!pGroup->pLevelDefGroup) {
			pGroup->pLevelDefGroup = calloc( 1, sizeof( *pGroup->pLevelDefGroup ));
		}

		y = GEUpdateMissionLevelDefGroup(pGroup->pLevelDefGroup, pExpander, &pMission->levelDef, SAFE_MEMBER_ADDR(pOrigMission, levelDef),
										 X_OFFSET_BASE, X_OFFSET_CONTROL, y, MDEFieldChangedCB, MDEFieldPreChangeCB, pMissionGroup->pDoc);
	} else if(pGroup) {
		MDEFreeLevelGroup(pGroup);
	}

	return y;
}

static void MDEUpdateLevelExpander(MDEMissionGroup *pMissionGroup)
{
	MissionDef *pDef = pMissionGroup->pMission;
	UIExpander *pExpander = pMissionGroup->pLevelExpander;
	MDELevelGroup *pGroup = pMissionGroup->pLevelGroup;
	F32 y = 0;

	if (!pExpander)
		return;

	if(!pGroup) {
		pGroup = calloc(1, sizeof(MDELevelGroup));
		pGroup->pMissionGroup = pMissionGroup;
		pGroup->pExpander = pExpander;
		pMissionGroup->pLevelGroup = pGroup;
	}

	y = MDEUpdateLevelGroup(pGroup, 0);

	// Set the expander height
	ui_ExpanderSetHeight(pMissionGroup->pLevelExpander, y);
}

static void MDEUpdateWarpExpander(MDEMissionGroup *pGroup)
{
	MissionDef* pOrigMission = pGroup->pOrigMission;
	MissionDef* pMission = pGroup->pMission;

	if (pGroup->pWarpMapField) {
		MEFieldSetAndRefreshFromData(pGroup->pWarpMapField, pOrigMission ? pOrigMission->pWarpToMissionDoor : NULL, pMission->pWarpToMissionDoor);
	}
	if (pGroup->pWarpSpawnField) {
		MEFieldSetAndRefreshFromData(pGroup->pWarpSpawnField, pOrigMission ? pOrigMission->pWarpToMissionDoor : NULL, pMission->pWarpToMissionDoor);
	}
	if (pGroup->pWarpCostTypeField) {
		MEFieldSetAndRefreshFromData(pGroup->pWarpCostTypeField, pOrigMission ? pOrigMission->pWarpToMissionDoor : NULL, pMission->pWarpToMissionDoor);
	}
	if (pGroup->pWarpLevelField) {
		MEFieldSetAndRefreshFromData(pGroup->pWarpLevelField, pOrigMission ? pOrigMission->pWarpToMissionDoor : NULL, pMission->pWarpToMissionDoor);
	}
	if (pGroup->pWarpTransitionField) {
		MEFieldSetAndRefreshFromData(pGroup->pWarpTransitionField, pOrigMission ? pOrigMission->pWarpToMissionDoor : NULL, pMission->pWarpToMissionDoor);
	}
}

static void MDEUpdateActivityNames(MDEMissionGroup *pGroup)
{
	S32 iIdx, iSize = eaSize(&g_ActivityDefs.ppDefs);

	eaClearEx(&pGroup->ppchActivityNames, StructFreeString);

	for(iIdx = 0; iIdx < iSize; iIdx++)
	{
		eaPush(&pGroup->ppchActivityNames, StructAllocString(g_ActivityDefs.ppDefs[iIdx]->pchActivityName));
	}
}

static void MDEUpdateMissionGroup(MDEMissionGroup *pGroup, MissionDef *pMission, MissionDef *pOrigMission, bool bIsRoot)
{
	int i;

	// Update info
	pGroup->pMission = pMission;
	pGroup->pOrigMission = pOrigMission;

	if (missiondef_GetType(pMission) != (MissionType)pGroup->eMissionType){
		pGroup->eMissionType = missiondef_GetType(pMission);
		MDEFullRefreshMissionGroup(pGroup, bIsRoot);
	}

	if (pMission->bIsTutorialPerk != pGroup->bOldIsTutorialPerk)
	{
		pGroup->bOldIsTutorialPerk = pMission->bIsTutorialPerk;
		MDEFullRefreshMissionGroup(pGroup, bIsRoot);
	}

	// Keep old name for handling renames
	if (!pGroup->pDoc->bSkipNameCapture) {
		if (pGroup->pcName && pMission->name && (stricmp(pGroup->pcName, pMission->name) != 0)) {
			free(pGroup->pcName);
			pGroup->pcName = strdup(pMission->name);
		} else if (pMission->name) {
			pGroup->pcName = strdup(pMission->name);
		}
	}

	// Refresh doc-level fields
	for(i=eaSize(&pGroup->eaDocFields)-1; i>=0; --i) {
		MEFieldSetAndRefreshFromData(pGroup->eaDocFields[i], pOrigMission, pMission);
	}

	// Refresh param-level fields
	for(i=eaSize(&pGroup->eaParamFields)-1; i>=0; --i) {
		MEFieldSetAndRefreshFromData(pGroup->eaParamFields[i], pOrigMission ? pOrigMission->params : NULL, pMission->params);
	}

	// Update activity names
	if (pGroup->pParamActivityName || pGroup->pInfoReqAnyActivities || pGroup->pInfoReqAllActivities) {
		MDEUpdateActivityNames(pGroup);
	}

	// Refresh required activities
	if (pGroup->pInfoReqAnyActivities) {
		MEFieldSetAndRefreshFromData(pGroup->pInfoReqAnyActivities, pOrigMission, pMission);
	}
	if (pGroup->pInfoReqAllActivities) {
		MEFieldSetAndRefreshFromData(pGroup->pInfoReqAllActivities, pOrigMission, pMission);
	}
	if (pGroup->pRelatedEvent)
	{
		MEFieldSetAndRefreshFromData(pGroup->pRelatedEvent, pOrigMission, pMission);
	}

	// Refresh the activity name field
	if (pGroup->pParamActivityName) {
		MEFieldSetAndRefreshFromData(pGroup->pParamActivityName, pOrigMission ? pOrigMission->params : NULL, pMission->params);
	}
	// Refresh display fields
	if (pGroup->pDisplayNameField) {
		MEFieldSetAndRefreshFromData(pGroup->pDisplayNameField, pOrigMission ? &pOrigMission->displayNameMsg : NULL, &pMission->displayNameMsg);
	}
	if (pGroup->pUIStringField) {
		MEFieldSetAndRefreshFromData(pGroup->pUIStringField, pOrigMission ? &pOrigMission->uiStringMsg : NULL, &pMission->uiStringMsg);
	}
	if (pGroup->pSummaryField) {
		MEFieldSetAndRefreshFromData(pGroup->pSummaryField, pOrigMission ? &pOrigMission->summaryMsg : NULL, &pMission->summaryMsg);
	}
	if (pGroup->pDetailField) {
		MEFieldSetAndRefreshFromData(pGroup->pDetailField, pOrigMission ? &pOrigMission->detailStringMsg : NULL, &pMission->detailStringMsg);
	}
	if (pGroup->pFailureTextField) {
		MEFieldSetAndRefreshFromData(pGroup->pFailureTextField, pOrigMission ? &pOrigMission->failureMsg : NULL, &pMission->failureMsg);
	}
	if (pGroup->pFailedReturnField) {
		MEFieldSetAndRefreshFromData(pGroup->pFailedReturnField, pOrigMission ? &pOrigMission->failReturnMsg : NULL, &pMission->failReturnMsg);
	}
	if (pGroup->pReturnField) {
		MEFieldSetAndRefreshFromData(pGroup->pReturnField, pOrigMission ? &pOrigMission->msgReturnStringMsg : NULL, &pMission->msgReturnStringMsg);
	}
	if (pGroup->pSplatTextField) {
		MEFieldSetAndRefreshFromData(pGroup->pSplatTextField, pOrigMission ? &pOrigMission->splatDisplayMsg : NULL, &pMission->splatDisplayMsg);
	}
	if (pGroup->pTeamUpTextField) {
		MEFieldSetAndRefreshFromData(pGroup->pTeamUpTextField, pOrigMission ? &pOrigMission->TeamUpDisplayName : NULL, &pMission->TeamUpDisplayName);
	}

	// Set size of display expander
	ui_ExpanderSetHeight(pGroup->pDisplayExpander, pGroup->fDisplayExpanderBaseSize);

	// Update reward data
	pGroup->rewardStartData.pDoc = pGroup->pDoc;
	pGroup->rewardStartData.pMission = pGroup->pMission;
	pGroup->rewardStartData.ppcRewardTableName = &pGroup->pMission->params->OnstartRewardTableName;
	pGroup->rewardSuccessData.pDoc = pGroup->pDoc;
	pGroup->rewardSuccessData.pMission = pGroup->pMission;
	pGroup->rewardSuccessData.ppcRewardTableName = &pGroup->pMission->params->OnsuccessRewardTableName;
	pGroup->rewardActivitySuccessData.pDoc = pGroup->pDoc;
	pGroup->rewardActivitySuccessData.pMission = pGroup->pMission;
	pGroup->rewardActivitySuccessData.ppcRewardTableName = &pGroup->pMission->params->ActivitySuccessRewardTableName;
	pGroup->rewardFailureData.pDoc = pGroup->pDoc;
	pGroup->rewardFailureData.pMission = pGroup->pMission;
	pGroup->rewardFailureData.ppcRewardTableName = &pGroup->pMission->params->OnfailureRewardTableName;
	pGroup->rewardReturnData.pDoc = pGroup->pDoc;
	pGroup->rewardReturnData.pMission = pGroup->pMission;
	pGroup->rewardReturnData.ppcRewardTableName = &pGroup->pMission->params->OnreturnRewardTableName;
	pGroup->rewardReplayReturnData.pDoc = pGroup->pDoc;
	pGroup->rewardReplayReturnData.pMission = pGroup->pMission;
	pGroup->rewardReplayReturnData.ppcRewardTableName = &pGroup->pMission->params->OnReplayReturnRewardTableName;
	pGroup->rewardActivityReturnData.pDoc = pGroup->pDoc;
	pGroup->rewardActivityReturnData.pMission = pGroup->pMission;
	pGroup->rewardActivityReturnData.ppcRewardTableName = &pGroup->pMission->params->ActivityReturnRewardTableName;

	pGroup->rewardOpenMissionGoldData.pDoc = pGroup->pDoc;
	pGroup->rewardOpenMissionGoldData.pMission = pGroup->pMission;
	pGroup->rewardOpenMissionGoldData.ppcRewardTableName = &pGroup->pMission->params->pchGoldRewardTable;
	pGroup->rewardOpenMissionSilverData.pDoc = pGroup->pDoc;
	pGroup->rewardOpenMissionSilverData.pMission = pGroup->pMission;
	pGroup->rewardOpenMissionSilverData.ppcRewardTableName = &pGroup->pMission->params->pchSilverRewardTable;
	pGroup->rewardOpenMissionBronzeData.pDoc = pGroup->pDoc;
	pGroup->rewardOpenMissionBronzeData.pMission = pGroup->pMission;
	pGroup->rewardOpenMissionBronzeData.ppcRewardTableName = &pGroup->pMission->params->pchBronzeRewardTable;
	pGroup->rewardOpenMissionDefaultData.pDoc = pGroup->pDoc;
	pGroup->rewardOpenMissionDefaultData.pMission = pGroup->pMission;
	pGroup->rewardOpenMissionDefaultData.ppcRewardTableName = &pGroup->pMission->params->pchDefaultRewardTable;

	pGroup->rewardOpenMissionFailureGoldData.pDoc = pGroup->pDoc;
	pGroup->rewardOpenMissionFailureGoldData.pMission = pGroup->pMission;
	pGroup->rewardOpenMissionFailureGoldData.ppcRewardTableName = &pGroup->pMission->params->pchFailureGoldRewardTable;
	pGroup->rewardOpenMissionFailureSilverData.pDoc = pGroup->pDoc;
	pGroup->rewardOpenMissionFailureSilverData.pMission = pGroup->pMission;
	pGroup->rewardOpenMissionFailureSilverData.ppcRewardTableName = &pGroup->pMission->params->pchFailureSilverRewardTable;
	pGroup->rewardOpenMissionFailureBronzeData.pDoc = pGroup->pDoc;
	pGroup->rewardOpenMissionFailureBronzeData.pMission = pGroup->pMission;
	pGroup->rewardOpenMissionFailureBronzeData.ppcRewardTableName = &pGroup->pMission->params->pchFailureBronzeRewardTable;
	pGroup->rewardOpenMissionFailureDefaultData.pDoc = pGroup->pDoc;
	pGroup->rewardOpenMissionFailureDefaultData.pMission = pGroup->pMission;
	pGroup->rewardOpenMissionFailureDefaultData.ppcRewardTableName = &pGroup->pMission->params->pchFailureDefaultRewardTable;

	// Update dynamic expanders
	if (bIsRoot) {
		MDEUpdateWarpExpander(pGroup);
	}
	MDEUpdateContactOverridesExpander(pGroup);
	MDEUpdateLevelExpander(pGroup);
	MDEUpdateWaypointExpander(pGroup, bIsRoot);
	MDEUpdateMapsExpander(pGroup, bIsRoot);
	MDEUpdateEventExpander(pGroup, bIsRoot);
	MDEUpdateCondExpander(pGroup, bIsRoot);
	MDEUpdateActionExpander(pGroup, bIsRoot);
	MDEUpdateNumericScalesExpander(pGroup, bIsRoot);
	MDEUpdateDropExpander(pGroup, bIsRoot);
	MDEUpdateInteractableOverrideExpander(pGroup, bIsRoot);
	MDEUpdateScoreboardExpander(pGroup, bIsRoot);
	MDEUpdateRequestsExpander(pGroup, bIsRoot);
	MDEUpdateVariablesExpander(pGroup, bIsRoot);
}


static void MDERefreshSubMissions(MissionEditDoc *pDoc, MissionDef *pMission)
{
	int i;

	for(i=0; i<eaSize(&pMission->subMissions); ++i) {
		eaPush(&pDoc->eaSubMissions, pMission->subMissions[i]);

		// Recurse
		MDERefreshSubMissions(pDoc, pMission->subMissions[i]);
	}
}

void MDEUpdateContactDisplay(MissionEditDoc *pDoc)
{
	// Prepare all contact groups for display update
	MDEBeginContactGroupsDisplayUpdate(pDoc);

	MDEUpdateSpecialDialogOverrideExpander(pDoc);
	MDEUpdateMissionOfferOverrideExpander(pDoc);
	MDEUpdateActionBlockOverrideExpander(pDoc);
	MDEUpdateImageMenuItemOverrideExpander(pDoc);

	// Finalize update for all contact groups
	MDEEndContactGroupsDisplayUpdate(pDoc);
}

void MDEUpdateDisplay(MissionEditDoc *pDoc)
{
	int i;

	// Ignore changes while UI refreshes
	pDoc->bIgnoreFieldChanges = true;

	// Update map variable names
	eaClear(&pDoc->eaVarNames);
	for(i=0; i<zmapInfoGetVariableCount(NULL); ++i) {
		WorldVariableDef *pDef = zmapInfoGetVariableDef(NULL, i);
		if (pDef) {
			eaPush(&pDoc->eaVarNames, (char*)pDef->pcName);
		}
	}

	// Refresh sub-missions list
	eaClear(&pDoc->eaSubMissions);
	MDERefreshSubMissions(pDoc, pDoc->pMission);

	// Update mission groups
	MDEUpdateMissionGroup(pDoc->pMainMissionGroup, pDoc->pMission, pDoc->pOrigMission, true);
	for(i=0; i<eaSize(&pDoc->pMission->subMissions); ++i) {
		MissionDef *pMission;
		MissionDef *pOrigMission = NULL;

		// Figure out the appropriate mission
		pMission= pDoc->pMission->subMissions[i];
		if (pDoc->pOrigMission && (i < eaSize(&pDoc->pOrigMission->subMissions))) {
			pOrigMission = pDoc->pOrigMission->subMissions[i];
		}

		// Create the group if not already present
		if (i >= eaSize(&pDoc->eaSubMissionGroups)) {
			eaPush(&pDoc->eaSubMissionGroups, MDEInitMissionGroup(pDoc, pMission, pOrigMission, false, i+1));
		}

		// Update the group
		MDEUpdateMissionGroup(pDoc->eaSubMissionGroups[i], pMission, pOrigMission, false);
	}
	while (i < eaSize(&pDoc->eaSubMissionGroups)) {
		MDEFreeMissionGroup(pDoc->eaSubMissionGroups[i]);
		eaRemove(&pDoc->eaSubMissionGroups, i);
	}

	// Clear mission group line counts;
	pDoc->pMainMissionGroup->iNumInBoxes = pDoc->pMainMissionGroup->iNumOutBoxes = 0;
	for(i=eaSize(&pDoc->eaSubMissionGroups)-1; i>=0; --i) {
		pDoc->eaSubMissionGroups[i]->iNumInBoxes = pDoc->eaSubMissionGroups[i]->iNumOutBoxes = 0;
	}
	if (gShowLines) {
		// Update inter-group lines
		MDEUpdateLines(pDoc->pMainMissionGroup, true);
		for(i=eaSize(&pDoc->eaSubMissionGroups)-1; i>=0; --i) {
			MDEUpdateLines(pDoc->eaSubMissionGroups[i], false);
		}
	}
	// Clean leftover inter-group lines
	MDECleanupLines(pDoc->pMainMissionGroup);
	for(i=eaSize(&pDoc->eaSubMissionGroups)-1; i>=0; --i) {
		MDECleanupLines(pDoc->eaSubMissionGroups[i]);
	}

	// Update the contacts
	MDEUpdateContactDisplay(pDoc);

	// Update non-field UI components
	ui_GimmeButtonSetName(pDoc->pFileButton, pDoc->pMission->name);
	ui_GimmeButtonSetReferent(pDoc->pFileButton, pDoc->pMission);
	ui_LabelSetText(pDoc->pFilenameLabel, pDoc->pMission->filename);

	// Update saved flag
	pDoc->emDoc.saved = pDoc->pOrigMission && (StructCompare(parse_MissionDef, pDoc->pOrigMission, pDoc->pMission, 0, 0, 0) == 0);

	// Start paying attention to changes again
	pDoc->bIgnoreFieldChanges = false;
}


//---------------------------------------------------------------------------------------------------
// UI Initialization
//---------------------------------------------------------------------------------------------------

static UIExpander *MDECreateExpander(UIExpanderGroup *pExGroup, const char *pcName, char *pcPrefName, bool bDefaultOpen)
{
	UIExpander *pExpander = ui_ExpanderCreate(pcName, 0);
	ui_WidgetSkin(UI_WIDGET(pExpander), gBoldExpanderSkin);
	ui_ExpanderGroupAddExpander(pExGroup, pExpander);
	ui_ExpanderSetOpened(pExpander, EditorPrefGetInt(MISSION_EDITOR, "Expander", pcPrefName, bDefaultOpen));
	ui_ExpanderSetExpandCallback(pExpander, MDEExpandChangeCB, pcPrefName);

	return pExpander;
}

static void MDEInitContactOverridesExpander(MissionEditDoc *pDoc, MDEMissionGroup *pGroup)
{
	pGroup->pContactOverridesExpander = MDECreateExpander(pGroup->pExGroup, "Contact Overrides", "ContactOverrides", true);
}

static void MDEInitInfoExpander(MissionEditDoc *pDoc, MDEMissionGroup *pGroup, bool bIsRoot)
{
	UIExpander *pExpander;
	UILabel *pLabel;
	MEField *pField;
	F32 y = 0;

	// Create the expander
	pExpander = MDECreateExpander(pGroup->pExGroup, "Information", bIsRoot ? "Information" : "Sub-Information", true);

	if (bIsRoot) {
		// Mission Type
		pLabel = MDECreateLabel("Mission Type", NULL, X_OFFSET_BASE, 0, y, pExpander);
		pField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pGroup->pOrigMission, pGroup->pMission, parse_MissionDef, "missionType", MissionTypeEnum);
		MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 130, UIUnitFixed, 3, pDoc);
		eaPush(&pGroup->eaDocFields, pField);
		y += Y_OFFSET_ROW;

		// Mission UI Type
		pLabel = MDECreateLabel("Mission UI Type", NULL, X_OFFSET_BASE, 0, y, pExpander);
		pField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pGroup->pOrigMission, pGroup->pMission, parse_MissionDef, "MissionUIType", MissionUITypeEnum);
		MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 130, UIUnitFixed, 3, pDoc);
		eaPush(&pGroup->eaDocFields, pField);
		y += Y_OFFSET_ROW;

		// Mission Tags
		pLabel = MDECreateLabel("Mission Tags", NULL, X_OFFSET_BASE, 0, y, pExpander);
		pField = MEFieldCreateSimpleEnum(kMEFieldType_FlagCombo, pGroup->pOrigMission, pGroup->pMission, parse_MissionDef, "MissionTag", MissionTagEnum);
		MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 130, UIUnitFixed, 3, pDoc);
		eaPush(&pGroup->eaDocFields, pField);
		y += Y_OFFSET_ROW;

		// Reward Scale
		pLabel = MDECreateLabel("Reward Scale", "The multiplier on numeric rewards for the mission.  1.0 is normal.", X_OFFSET_BASE, 0, y, pExpander);
		pField = MEFieldCreateSimple(kMEFieldType_TextEntry, pGroup->pOrigMission ? pGroup->pOrigMission->params : NULL, pGroup->pMission->params, parse_MissionDefParams, "NumericRewardScale");
		MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 70, UIUnitFixed, 3, pDoc);
		eaPush(&pGroup->eaParamFields, pField);
		y += Y_OFFSET_ROW;

		// Reward Scale for 'Ineligible' and 'AlreadyCompleted' credit types
		if (missiondef_GetType(pGroup->pMission) != MissionType_OpenMission) {
			pLabel = MDECreateLabel("Ineligible Scale", "The multiplier for numeric rewards if it's a secondary mission. If zero, default to the standard 'Reward Scale' value.", X_OFFSET_BASE, 0, y, pExpander);
			pField = MEFieldCreateSimple(kMEFieldType_TextEntry, pGroup->pOrigMission ? pGroup->pOrigMission->params : NULL, pGroup->pMission->params, parse_MissionDefParams, "NumericRewardScaleIneligible");
			MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 70, UIUnitFixed, 3, pDoc);
			eaPush(&pGroup->eaParamFields, pField);
			y += Y_OFFSET_ROW;
			pLabel = MDECreateLabel("Completed Scale", "The multiplier for numeric rewards if the mission has already been completed.  If zero, default to the standard 'Reward Scale' value.", X_OFFSET_BASE, 0, y, pExpander);
			pField = MEFieldCreateSimple(kMEFieldType_TextEntry, pGroup->pOrigMission ? pGroup->pOrigMission->params : NULL, pGroup->pMission->params, parse_MissionDefParams, "NumericRewardScaleAlreadyCompleted");
			MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 70, UIUnitFixed, 3, pDoc);
			eaPush(&pGroup->eaParamFields, pField);
			y += Y_OFFSET_ROW;
		} else if (pGroup->pMission->params) {
			pGroup->pMission->params->NumericRewardScaleIneligible = 0;
			pGroup->pMission->params->NumericRewardScaleAlreadyCompleted = 0;
		}

		// Scale Reward Over Time
		if (missiondef_GetType(pGroup->pMission) == MissionType_OpenMission){
			pLabel = MDECreateLabel("Scale Over Time", "Time (in minutes) in which the reward will be scaled linearly from 0 to 1. The final reward value will also be scaled by the 'Reward Scale'.", X_OFFSET_BASE, 0, y, pExpander);
			pField = MEFieldCreateSimple(kMEFieldType_TextEntry, pGroup->pOrigMission ? pGroup->pOrigMission->params : NULL, pGroup->pMission->params, parse_MissionDefParams, "ScaleRewardOverTime");
			MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 70, UIUnitFixed, 3, pDoc);
			eaPush(&pGroup->eaParamFields, pField);
			y += Y_OFFSET_ROW;
		} else if (pGroup->pMission->params) {
			//Clear iScaleRewardOverTime if this isn't an open mission
			pGroup->pMission->params->iScaleRewardOverTime = 0;
		}
		
		if (missiondef_GetType(pGroup->pMission) == MissionType_Perk){
			pLabel = MDECreateLabel("Perk Points", "The number of 'points' this Perk is worth", X_OFFSET_BASE, 0, y, pExpander);
			pField = MEFieldCreateSimple(kMEFieldType_TextEntry, pGroup->pOrigMission, pGroup->pMission, parse_MissionDef, "PerkPoints");
			MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 70, UIUnitFixed, 3, pDoc);
			eaPush(&pGroup->eaDocFields, pField);
			y += Y_OFFSET_ROW;
		}

		// Sort Priority
		pLabel = MDECreateLabel("Sort Priority", "Sort priority for the UI", X_OFFSET_BASE, 0, y, pExpander);
		pField = MEFieldCreateSimple(kMEFieldType_TextEntry, pGroup->pOrigMission, pGroup->pMission, parse_MissionDef, "SortPriority");
		MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 70, UIUnitFixed, 3, pDoc);
		eaPush(&pGroup->eaDocFields, pField);
		y += Y_OFFSET_ROW;

		// Required Allegiance
		pLabel = MDECreateLabel("Required Allegiance", "The player must be one of the following allegiances to be offered this mission", X_OFFSET_BASE, 0, y, pExpander);
		pField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_FlagCombo, pGroup->pOrigMission, pGroup->pMission, parse_MissionDef, "RequiredAllegiance", "Allegiance", "resourceName");
		MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 70, UIUnitFixed, 3, pDoc);
		eaPush(&pGroup->eaDocFields, pField);
		y += Y_OFFSET_ROW;

		// Require All Allegiances
		pLabel = MDECreateLabel("Require All Allegiances", "Set this to True if the player must have all of the required allegiances to be offered this mission", X_OFFSET_BASE, 0, y, pExpander);
		pField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_BooleanCombo, pGroup->pOrigMission, pGroup->pMission, parse_MissionDef, "RequireAllAllegiances", "Allegiance", "resourceName");
		MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 70, UIUnitFixed, 3, pDoc);
		eaPush(&pGroup->eaDocFields, pField);
		y += Y_OFFSET_ROW;

		// Min Level
		pLabel = MDECreateLabel("Min Level", "The player must be at least this level to be offered this mission", X_OFFSET_BASE, 0, y, pExpander);
		pField = MEFieldCreateSimple(kMEFieldType_TextEntry, pGroup->pOrigMission, pGroup->pMission, parse_MissionDef, "MinLevel");
		MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 70, UIUnitFixed, 3, pDoc);
		eaPush(&pGroup->eaDocFields, pField);
		y += Y_OFFSET_ROW;

		MDEUpdateActivityNames(pGroup);

		// Require Any Activities
		pLabel = MDECreateLabel("Require Any Activities", "Any of the following activities must be active in order for a player to see this mission", X_OFFSET_BASE, 0, y, pExpander);
		pGroup->pInfoReqAnyActivities = MEFieldCreateSimpleDataProvided(kMEFieldType_ValidatedTextEntry, pGroup->pOrigMission, pGroup->pMission, parse_MissionDef, "RequiresActivityOr", NULL, &pGroup->ppchActivityNames, NULL);
		MDEAddFieldToParent(pGroup->pInfoReqAnyActivities, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 70, UIUnitFixed, 3, pDoc);
		y += Y_OFFSET_ROW;

		// Require All Activities
		pLabel = MDECreateLabel("Require All Activities", "All of the following activities must be active in order for a player to see this mission", X_OFFSET_BASE, 0, y, pExpander);
		pGroup->pInfoReqAllActivities = MEFieldCreateSimpleDataProvided(kMEFieldType_ValidatedTextEntry, pGroup->pOrigMission, pGroup->pMission, parse_MissionDef, "RequiresActivityAnd", NULL, &pGroup->ppchActivityNames, NULL);
		MDEAddFieldToParent(pGroup->pInfoReqAllActivities, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 70, UIUnitFixed, 3, pDoc);
		y += Y_OFFSET_ROW;

		// Related Event
		pLabel = MDECreateLabel("Related Event", "The event that is connected to this open mission", X_OFFSET_BASE, 0, y, pExpander);
		pGroup->pRelatedEvent = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_ValidatedTextEntry, pGroup->pOrigMission, pGroup->pMission, parse_MissionDef, "RelatedEvent", "Event", "ResourceName");
		MDEAddFieldToParent(pGroup->pRelatedEvent, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 3, pDoc);
		y += Y_OFFSET_ROW;

		// Requires Expression
		pLabel = MDECreateLabel("Requires", "What must be true before the mission can be given to a player", X_OFFSET_BASE, 0, y, pExpander);
		pField = MEFieldCreateSimpleExpression(kMEFieldTypeEx_Expression, pGroup->pOrigMission, pGroup->pMission, parse_MissionDef, "RequiresBlock", MDEGetMissionRequiresExprContext());
		MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 3, pDoc);
		eaPush(&pGroup->eaDocFields, pField);
		y += Y_OFFSET_ROW;

		if (missiondef_GetType(pGroup->pMission) == MissionType_OpenMission) {
			// Map Requires Expression
			pLabel = MDECreateLabel("Map Requires", "An expression that is not related to any player which determines if the open mission runs.", X_OFFSET_BASE, 0, y, pExpander);
			pField = MEFieldCreateSimpleExpression(kMEFieldTypeEx_Expression, pGroup->pOrigMission, pGroup->pMission, parse_MissionDef, "MapRequiresBlock", MDEGetMissionRequiresExprContext());
			MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 3, pDoc);
			eaPush(&pGroup->eaDocFields, pField);
			y += Y_OFFSET_ROW;
		}
	}

	if (missiondef_GetType(pGroup->pMission) == MissionType_OpenMission) {
		// OnSuccess Expression
		pLabel = MDECreateLabel("Map Success", "An expression that is not related to any player which runs one time when the open mission succeeds.", X_OFFSET_BASE, 0, y, pExpander);
		pField = MEFieldCreateSimpleExpression(kMEFieldTypeEx_Expression, pGroup->pOrigMission, pGroup->pMission, parse_MissionDef, "MapSuccessBlock", MDEGetMissionRequiresExprContext());
		MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 3, pDoc);
		eaPush(&pGroup->eaDocFields, pField);
		y += Y_OFFSET_ROW;

		// OnFailure Expression
		pLabel = MDECreateLabel("Map Failure", "An expression that is not related to any player which runs one time when the open mission fails.", X_OFFSET_BASE, 0, y, pExpander);
		pField = MEFieldCreateSimpleExpression(kMEFieldTypeEx_Expression, pGroup->pOrigMission, pGroup->pMission, parse_MissionDef, "MapFailureBlock", MDEGetMissionRequiresExprContext());
		MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 3, pDoc);
		eaPush(&pGroup->eaDocFields, pField);
		y += Y_OFFSET_ROW;			
	}

	// Comments
	pLabel = MDECreateLabel("Comments", "Designer notes to themselves.  Never shown in game.", X_OFFSET_BASE, 0, y, pExpander);
	pField = MEFieldCreateSimple(kMEFieldType_MultiText, pGroup->pOrigMission, pGroup->pMission, parse_MissionDef, "Comments");
	MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 3, pDoc);
	eaPush(&pGroup->eaDocFields, pField);
	y += Y_OFFSET_ROW;

	ui_ExpanderSetHeight(pExpander, y);
}


static void MDEInitDisplayExpander(MissionEditDoc *pDoc, MDEMissionGroup *pGroup, bool bIsRoot)
{
	UIExpander *pExpander;
	UILabel *pLabel;
	MEField *pField;
	F32 y = 0;

	// Create the expander
	pExpander = MDECreateExpander(pGroup->pExGroup, "Mission Display", bIsRoot ? "Display" : "Sub-Display", true);
	pGroup->pDisplayExpander = pExpander;

	if (bIsRoot) {
		// Display Name
		pLabel = MDECreateLabel("Display Name", "Mission name shown in game.", X_OFFSET_BASE, 0, y, pExpander);
		pField = MEFieldCreateSimple(kMEFieldType_Message, pGroup->pOrigMission ? &pGroup->pOrigMission->displayNameMsg : NULL, &pGroup->pMission->displayNameMsg, parse_DisplayMessage, "EditorCopy");
		MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 3, pDoc);
		pGroup->pDisplayNameField = pField;
		y += Y_OFFSET_ROW;
	}

	// UI String
	pLabel = MDECreateLabel("UI String", bIsRoot ? "One liner shown in game." : "One liner shown in game.  If empty it won't show up as an objective line.", X_OFFSET_BASE, 0, y, pExpander);
	pField = MEFieldCreateSimple(kMEFieldType_Message, pGroup->pOrigMission ? &pGroup->pOrigMission->uiStringMsg : NULL, &pGroup->pMission->uiStringMsg, parse_DisplayMessage, "EditorCopy");
	MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 3, pDoc);
	pGroup->pUIStringField = pField;
	y += Y_OFFSET_ROW;

	// Summary
	pLabel = MDECreateLabel("Summary", "Short description shown in game.  Appears below Detail in contact dialog, above Detail in mission journal.", X_OFFSET_BASE, 0, y, pExpander);
	pField = MEFieldCreateSimple(kMEFieldType_Message, pGroup->pOrigMission ? &pGroup->pOrigMission->summaryMsg : NULL, &pGroup->pMission->summaryMsg, parse_DisplayMessage, "EditorCopy");
	MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 3, pDoc);
	pGroup->pSummaryField = pField;
	y += Y_OFFSET_ROW;

	if (bIsRoot) {
		// Detail
		pLabel = MDECreateLabel("Detail", "Long description shown in game.  Appears above summary in contact dialog, below summary in mission journal.", X_OFFSET_BASE, 0, y, pExpander);
		pField = MEFieldCreateSimple(kMEFieldType_Message, pGroup->pOrigMission ? &pGroup->pOrigMission->detailStringMsg : NULL, &pGroup->pMission->detailStringMsg, parse_DisplayMessage, "EditorCopy");
		MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 0, pDoc);
		pGroup->pDetailField = pField;

		// Detail sound
		pField = MEFieldCreateSimpleDataProvided(kMEFieldType_ValidatedTextEntry, pGroup->pOrigMission, pGroup->pMission, parse_MissionDef, "SoundOnContactOffer", NULL, sndGetEventListStatic(), NULL);
		MDEAddFieldToParent(pField, UI_WIDGET(pExpander), 3, y, 0, 150, UIUnitFixed, 0, pDoc);
		pField->pUIWidget->offsetFrom = UITopRight;
		eaPush(&pGroup->eaDocFields, pField);
		pLabel = MDECreateLabel("VO", "(optional) Sound that will play when a contact offers this mission", 153 , 0, y, pExpander);
		pLabel->widget.offsetFrom = UITopRight;
		pGroup->pDetailField->pUIWidget->rightPad = pLabel->widget.x + pLabel->widget.width + 3;
		y += Y_OFFSET_ROW;
	}

	if (bIsRoot) {
		// Failure String
		pLabel = MDECreateLabel("Failure Text", "Text to display in the Mission Journal if this objective was failed", X_OFFSET_BASE, 0, y, pExpander);
		pField = MEFieldCreateSimple(kMEFieldType_Message, pGroup->pOrigMission ? &pGroup->pOrigMission->failureMsg : NULL, &pGroup->pMission->failureMsg, parse_DisplayMessage, "EditorCopy");
		MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 3, pDoc);
		pGroup->pFailureTextField = pField;
		y += Y_OFFSET_ROW;
	}

	if (bIsRoot) {
		pLabel = MDECreateLabel("Return Text", "(optional) Text to show once the mission is complete", X_OFFSET_BASE, 0, y, pExpander);
		pField = MEFieldCreateSimple(kMEFieldType_Message, pGroup->pOrigMission ? &pGroup->pOrigMission->msgReturnStringMsg : NULL, &pGroup->pMission->msgReturnStringMsg, parse_DisplayMessage, "EditorCopy");
		MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 3, pDoc);
		pGroup->pReturnField = pField;
		y += Y_OFFSET_ROW;
	}

	if (bIsRoot) {
		pLabel = MDECreateLabel("Failed Return Text", "(optional) Text to show in the UI when the mission is failed", X_OFFSET_BASE, 0, y, pExpander);
		pField = MEFieldCreateSimple(kMEFieldType_Message, pGroup->pOrigMission ? &pGroup->pOrigMission->failReturnMsg : NULL, &pGroup->pMission->failReturnMsg, parse_DisplayMessage, "EditorCopy");
		MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 3, pDoc);
		pGroup->pFailedReturnField = pField;
		y += Y_OFFSET_ROW;
	}

	if (bIsRoot) {
		// Return Map
		pLabel = MDECreateLabel("Return Map", "The map the contact to return to is on", X_OFFSET_BASE, 0, y, pExpander);
		pField = MEFieldCreateSimpleDataProvided(kMEFieldType_TextEntry, pGroup->pOrigMission, pGroup->pMission, parse_MissionDef, "ReturnMap", NULL, &g_GEMapDispNames, NULL);
		MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 3, pDoc);
		eaPush(&pGroup->eaDocFields, pField);
		y += Y_OFFSET_ROW;

		// Category
		pLabel = MDECreateLabel("Journal Category", "(optional) If this is set it overrides the Obective Map in determining where in the mission journal to show the mission", X_OFFSET_BASE, 0, y, pExpander);
		pField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_TextEntry, pGroup->pOrigMission, pGroup->pMission, parse_MissionDef, "Category", "MissionCategory", "ResourceName");
		MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 3, pDoc);
		eaPush(&pGroup->eaDocFields, pField);
		y += Y_OFFSET_ROW;

		// Recommended Team size
		pLabel = MDECreateLabel("Team Size", "(optional) If this is set, the UI will indicate to the player that this is a group mission", X_OFFSET_BASE, 0, y, pExpander);
		pField = MEFieldCreateSimple(kMEFieldType_TextEntry, pGroup->pOrigMission, pGroup->pMission, parse_MissionDef, "SuggestedTeamSize");
		MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 70, UIUnitFixed, 3, pDoc);
		eaPush(&pGroup->eaDocFields, pField);

		pLabel = MDECreateLabel("Scales For Team", NULL, X_OFFSET_BASE + X_OFFSET_CONTROL + 70, 0, y, pExpander);
		pField = MEFieldCreateSimple(kMEFieldType_Check, pGroup->pOrigMission, pGroup->pMission, parse_MissionDef, "ScalesForTeamSize");
		MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL*2 + X_OFFSET_BASE + 70, y, 0, 70, UIUnitFixed, 3, pDoc);
		eaPush(&pGroup->eaDocFields, pField);
		y += Y_OFFSET_ROW;

		// Open Mission volume
		if (pGroup->pMission->missionType == MissionType_OpenMission)
		{
			pLabel = MDECreateLabel("Open Mis. Vol", "(optional) If this is set, the Open Mission will only be displayed to players in this volume", X_OFFSET_BASE, 0, y, pExpander);
			pField = MEFieldCreateSimple(kMEFieldType_TextEntry, pGroup->pOrigMission, pGroup->pMission, parse_MissionDef, "OpenMissionVolume");
			MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 3, pDoc);
			eaPush(&pGroup->eaDocFields, pField);
			y += Y_OFFSET_ROW;

			pLabel = MDECreateLabel("Open Team Up", "(Optional) If this is set, the user will be allowed to join a team up for this mission. Only allowed for open missions", X_OFFSET_BASE,0,y,pExpander);
			pField = MEFieldCreateSimple(kMEFieldType_TextEntry,pGroup->pOrigMission,pGroup->pMission, parse_MissionDef, "TeamUpName");
			MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 3, pDoc);
			eaPush(&pGroup->eaDocFields, pField);
			y += Y_OFFSET_ROW;

			pLabel = MDECreateLabel("Team Up Display Name", "(optional) Display name for the team up.", X_OFFSET_BASE, 0, y, pExpander);
			pField = MEFieldCreateSimple(kMEFieldType_Message, pGroup->pOrigMission ? &pGroup->pOrigMission->TeamUpDisplayName : NULL, &pGroup->pMission->TeamUpDisplayName, parse_DisplayMessage, "EditorCopy");
			MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 3, pDoc);
			pGroup->pTeamUpTextField = pField;
			y += Y_OFFSET_ROW;
		}
		else
		{
			pGroup->pTeamUpTextField = NULL;
		}

		// Perk icon
		if (pGroup->pMission->missionType == MissionType_Perk)
		{
			pLabel = MDECreateLabel("Perk Icon", "(optional) Icon to display for this Perk", X_OFFSET_BASE, 0, y, pExpander);
			pField = MEFieldCreateSimple(kMEFieldType_Texture, pGroup->pOrigMission, pGroup->pMission, parse_MissionDef, "IconName");
			MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 3, pDoc);
			if (pField && pField->pUIWidget){
				ui_WidgetSetHeight(pField->pUIWidget, Y_OFFSET_ROW*2-4);
			}
			eaPush(&pGroup->eaDocFields, pField);
			y += Y_OFFSET_ROW*2;

			pLabel = MDECreateLabel("Is Tutorial Perk", NULL, X_OFFSET_BASE, 0, y, pExpander);
			pField = MEFieldCreateSimple(kMEFieldType_Check, pGroup->pOrigMission, pGroup->pMission, parse_MissionDef, "IsTutorialPerk");
			MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 70, UIUnitFixed, 3, pDoc);
			eaPush(&pGroup->eaDocFields, pField);
			y += Y_OFFSET_ROW;

		}

		if (missiondef_GetType(pGroup->pMission) == MissionType_Perk && pGroup->pMission->bIsTutorialPerk)
		{
			// Tutorial Screen Region
			pLabel = MDECreateLabel("Tutorial Region", NULL, X_OFFSET_BASE, 0, y, pExpander);
			pField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pGroup->pOrigMission, pGroup->pMission, parse_MissionDef, "TutorialScreenRegion", TutorialScreenRegionEnum);
			MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 3, pDoc);
			eaPush(&pGroup->eaDocFields, pField);
			y += Y_OFFSET_ROW;
		}
	}

	pLabel = MDECreateLabel("Splat FX","(optional) A text words fx to use for mission splat and fx", X_OFFSET_BASE,0, y, pExpander);
	pField = MEFieldCreateSimpleDictionary(kMEFieldType_ValidatedTextEntry, pGroup->pOrigMission ? pGroup->pOrigMission : NULL, pGroup->pMission, parse_MissionDef, "SplatFX", "DynFxInfo", parse_DynFxInfo, "InternalName");
	
	if(pField && pField->pUIWidget)
	{
		MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 3, pDoc);
		eaPush(&pGroup->eaDocFields, pField);
	}
	y += Y_OFFSET_ROW;

	// Text words splat fields
	pLabel = MDECreateLabel("Splat FX String","(optional) Display text relating to the mission to be used in a text words fx", X_OFFSET_BASE,0, y, pExpander);
	pField = MEFieldCreateSimple(kMEFieldType_Message, pGroup->pOrigMission ? &pGroup->pOrigMission->splatDisplayMsg : NULL, &pGroup->pMission->splatDisplayMsg, parse_DisplayMessage, "EditorCopy");
	MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 3, pDoc);
	pGroup->pSplatTextField = pField;
	y += Y_OFFSET_ROW;

	// Related Open Mission
	pLabel = MDECreateLabel("Related Mission", "An Open Mission which this mission or submission is associated with.  If set, this is used to add an indicator in the UI that the two missions are related.", X_OFFSET_BASE, 0, y, pExpander);
	pField = MEFieldCreateSimpleDataProvided(kMEFieldType_ValidatedTextEntry, pGroup->pOrigMission, pGroup->pMission, parse_MissionDef, "RelatedMission", parse_MissionDef, &gOpenMissions, "name");
	MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 3, pDoc);
	eaPush(&pGroup->eaDocFields, pField);
	y += Y_OFFSET_ROW;

	pGroup->fDisplayExpanderBaseSize = y - 3;

	ui_ExpanderSetHeight(pExpander, pGroup->fDisplayExpanderBaseSize);
}

static void MDEInitLevelExpander(MissionEditDoc *pDoc, MDEMissionGroup *pGroup) 
{
	UIExpander *pExpander;
	F32 y = 0;

	pExpander = MDECreateExpander(pGroup->pExGroup, "Level", "Level", true);
	pGroup->pLevelExpander = pExpander;
}

static void MDEInitWarpExpander(MissionEditDoc* pDoc, MDEMissionGroup* pGroup)
{
	UIExpander* pExpander;
	MEField* pField;
	UILabel* pLabel;
	F32 y = 0;

	// Create the expander
	pExpander = MDECreateExpander(pGroup->pExGroup, "Warp", "Warp", false);
	pGroup->pWarpExpander = pExpander;

	// Warp Map
	pLabel = MDECreateLabel("Warp To Map", "The map to warp to in order to quickly play this mission", X_OFFSET_BASE, 0, y, pExpander);
	pField = MEFieldCreateSimpleDataProvided(kMEFieldType_ValidatedTextEntry, pGroup->pOrigMission ? pGroup->pOrigMission->pWarpToMissionDoor : NULL, pGroup->pMission->pWarpToMissionDoor, parse_MissionMapWarpData, "MapName", NULL, &g_GEMapDispNames, NULL);
	MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 3, pDoc);
	eaPush(&pGroup->eaDocFields, pField);
	pGroup->pWarpMapField = pField;
	y += Y_OFFSET_ROW;

	// Warp Spawn
	pLabel = MDECreateLabel("Warp To Spawn", "The spawn to warp to in order to quickly play this mission", X_OFFSET_BASE, 0, y, pExpander);
	pField = MEFieldCreateSimple(kMEFieldType_TextEntry, pGroup->pOrigMission ? pGroup->pOrigMission->pWarpToMissionDoor : NULL, pGroup->pMission->pWarpToMissionDoor, parse_MissionMapWarpData, "Spawn");
	MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 3, pDoc);
	eaPush(&pGroup->eaDocFields, pField);
	pGroup->pWarpSpawnField = pField;
	y += Y_OFFSET_ROW;

	// Warp Cost Type
	pLabel = MDECreateLabel("Warp Cost Type", "The cost to warp to this destination", X_OFFSET_BASE, 0, y, pExpander);
	pField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pGroup->pOrigMission ? pGroup->pOrigMission->pWarpToMissionDoor : NULL, pGroup->pMission->pWarpToMissionDoor, parse_MissionMapWarpData, "CostType", MissionWarpCostTypeEnum);
	MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 3, pDoc);
	eaPush(&pGroup->eaDocFields, pField);
	pGroup->pWarpCostTypeField = pField;
	y += Y_OFFSET_ROW;

	// Warp Level
	pLabel = MDECreateLabel("Warp Required Level", "The level required to warp to this destination", X_OFFSET_BASE, 0, y, pExpander);
	pField = MEFieldCreateSimple(kMEFieldType_TextEntry, pGroup->pOrigMission ? pGroup->pOrigMission->pWarpToMissionDoor : NULL, pGroup->pMission->pWarpToMissionDoor, parse_MissionMapWarpData, "RequiredLevel");
	MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 3, pDoc);
	eaPush(&pGroup->eaDocFields, pField);
	pGroup->pWarpLevelField = pField;
	y += Y_OFFSET_ROW;

	// Transition Sequence
	pLabel = MDECreateLabel("Transition Sequence", "The transition sequence to play", X_OFFSET_BASE, 0, y, pExpander);
	pField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_TextEntry, pGroup->pOrigMission ? pGroup->pOrigMission->pWarpToMissionDoor : NULL, pGroup->pMission->pWarpToMissionDoor, parse_MissionMapWarpData, "TransitionSequence", "DoorTransitionSequenceDef", "ResourceName");
	MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 3, pDoc);
	eaPush(&pGroup->eaDocFields, pField);
	pGroup->pWarpTransitionField = pField;
	y += Y_OFFSET_ROW;

	ui_ExpanderSetHeight(pExpander, y);
}

static void MDEInitNumericScalesExpander(MissionEditDoc *pDoc, MDEMissionGroup *pGroup, bool bIsRoot)
{
	UIExpander *pExpander;
	F32 y = 0;

	// Create the expander
	pExpander = MDECreateExpander(pGroup->pExGroup, "Per-Numeric Reward Scales", "Per-Numeric Reward Scales", false);
	pGroup->pNumericScalesExpander = pExpander;

	// Put in add button
	pGroup->pAddNumericScaleButton = ui_ButtonCreate("Add Numeric Scale", X_OFFSET_BASE, y, MDEAddNumericScaleCB, pGroup);
	ui_WidgetSetWidth(UI_WIDGET(pGroup->pAddNumericScaleButton), 120);
	ui_ExpanderAddChild(pExpander, pGroup->pAddNumericScaleButton);

	y += Y_OFFSET_ROW;

	ui_ExpanderSetHeight(pExpander, y);
}

static void MDEInitWaypointsExpander(MissionEditDoc *pDoc, MDEMissionGroup *pGroup, bool bIsRoot)
{
	UIExpander *pExpander;
	F32 y = 0;

	// Create the expander
	pExpander = MDECreateExpander(pGroup->pExGroup, "Waypoints", "Waypoints", false);
	pGroup->pWaypointExpander = pExpander;

	// Put in add button
	pGroup->pAddWaypointButton = ui_ButtonCreate("Add Waypoint", X_OFFSET_BASE, y, MDEAddWaypointCB, pGroup);
	ui_WidgetSetWidth(UI_WIDGET(pGroup->pAddWaypointButton), 110);
	ui_ExpanderAddChild(pExpander, pGroup->pAddWaypointButton);

	y += Y_OFFSET_ROW;

	ui_ExpanderSetHeight(pExpander, y);

}

static void MDEInitMapsExpander(MissionEditDoc *pDoc, MDEMissionGroup *pGroup, bool bIsRoot)
{
	UIExpander *pExpander;
	F32 y = 0;

	// Create the expander
	pExpander = MDECreateExpander(pGroup->pExGroup, "Objective Maps", "Objective Maps", true);
	pGroup->pMapsExpander = pExpander;

	// Put in add button
	pGroup->pAddMapButton = ui_ButtonCreate("Add Map", X_OFFSET_BASE, y, MDEAddMapCB, pGroup);
	ui_WidgetSetWidth(UI_WIDGET(pGroup->pAddMapButton), 80);
	ui_ExpanderAddChild(pExpander, pGroup->pAddMapButton);

	y += Y_OFFSET_ROW;

	ui_ExpanderSetHeight(pExpander, y);
}

static void MDEInitEventsExpander(MissionEditDoc *pDoc, MDEMissionGroup *pGroup, bool bIsRoot)
{
	UIExpander *pExpander;
	F32 y = 0;

	// Create the expander
	pExpander = MDECreateExpander(pGroup->pExGroup, "Events", "Events", false);
	pGroup->pEventExpander = pExpander;

	// Put in add button
	pGroup->pAddEventButton = ui_ButtonCreate("Add Event", X_OFFSET_BASE, y, MDEAddEventCB, pGroup);
	ui_WidgetSetWidth(UI_WIDGET(pGroup->pAddEventButton), 80);
	ui_ExpanderAddChild(pExpander, pGroup->pAddEventButton);

	y += Y_OFFSET_ROW;

	ui_ExpanderSetHeight(pExpander, y);

}

static void MDEInitConditionsExpander(MissionEditDoc *pDoc, MDEMissionGroup *pGroup, bool bIsRoot)
{
	UIExpander *pExpander;

	// Create the expander
	pExpander = MDECreateExpander(pGroup->pExGroup, "Conditions", bIsRoot ? "Condition" : "Sub-Condition", true);
	pGroup->pCondExpander = pExpander;
}


static void MDEInitActionsExpander(MissionEditDoc *pDoc, MDEMissionGroup *pGroup, bool bIsRoot)
{
	UIExpander *pExpander;
	UIButton *pButton;
	F32 y = 0;

	// Create the expander
	pExpander = MDECreateExpander(pGroup->pExGroup, "Actions", bIsRoot ? "Action" : "Sub-Actions", true);
	pGroup->pActionExpander = pExpander;

	pButton = ui_ButtonCreate("Add Start", 15, y, MDEAddStartActionCB, pGroup);
	ui_WidgetSetWidth(UI_WIDGET(pButton), 90);
	ui_ExpanderAddChild(pExpander, pButton);

	pButton = ui_ButtonCreate("Add Success", 110, y, MDEAddSuccessActionCB, pGroup);
	ui_WidgetSetWidth(UI_WIDGET(pButton), 90);
	ui_ExpanderAddChild(pExpander, pButton);

	pButton = ui_ButtonCreate("Add Failure", 205, y, MDEAddFailureActionCB, pGroup);
	ui_WidgetSetWidth(UI_WIDGET(pButton), 90);
	ui_ExpanderAddChild(pExpander, pButton);

	pButton = ui_ButtonCreate("Add Return", 300, y, MDEAddReturnActionCB, pGroup);
	ui_WidgetSetWidth(UI_WIDGET(pButton), 90);
	ui_ExpanderAddChild(pExpander, pButton);

	y += Y_OFFSET_ROW;

	ui_ExpanderSetHeight(pExpander, y);
}


static void MDEInitDropsExpander(MissionEditDoc *pDoc, MDEMissionGroup *pGroup, bool bIsRoot)
{
	UIExpander *pExpander;
	F32 y = 0;

	// Create the expander
	pExpander = MDECreateExpander(pGroup->pExGroup, "Mission Drops", bIsRoot ? "Drops" : "Sub-Drops", false);
	pGroup->pDropExpander = pExpander;

	// Put in add button
	pGroup->pAddDropButton = ui_ButtonCreate("Add Drop", X_OFFSET_BASE, y, MDEAddDropCB, pGroup);
	ui_WidgetSetWidth(UI_WIDGET(pGroup->pAddDropButton), 80);
	ui_ExpanderAddChild(pExpander, pGroup->pAddDropButton);

	y += Y_OFFSET_ROW;

	ui_ExpanderSetHeight(pExpander, y);
}

static void MDEInitRewardsExpander(MissionEditDoc *pDoc, MDEMissionGroup *pGroup, bool bIsRoot)
{
	UIExpander *pExpander;
	UILabel *pLabel;
	MEField *pField;
	F32 y = 0;

	// Create the expander
	pGroup->pRewardsExpander = pExpander = MDECreateExpander(pGroup->pExGroup, "Rewards", bIsRoot ? "Rewards" : "Sub-Rewards", false);

	// On Start
	pLabel = MDECreateLabel("On Start", "Reward granted when mission is granted", X_OFFSET_BASE, 0, y, pExpander);
	pField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_ValidatedTextEntry, pGroup->pOrigMission ? pGroup->pOrigMission->params : NULL, pGroup->pMission->params, parse_MissionDefParams, "OnstartRewardTableName", "RewardTable", "ResourceName");
	MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 75, pDoc);
	eaPush(&pGroup->eaParamFields, pField);
	strcpy(pGroup->rewardStartData.cNamePart, "OnStart");
	MDEAddRewardEditButton(pExpander, y, &pGroup->rewardStartData);
	y += Y_OFFSET_ROW;

	// On Success
	pLabel = MDECreateLabel("On Success", "Reward granted on success (prior to turning it in)", X_OFFSET_BASE, 0, y, pExpander);
	pField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_ValidatedTextEntry, pGroup->pOrigMission ? pGroup->pOrigMission->params : NULL, pGroup->pMission->params, parse_MissionDefParams, "OnsuccessRewardTableName", "RewardTable", "ResourceName");
	MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 75, pDoc);
	eaPush(&pGroup->eaParamFields, pField);
	strcpy(pGroup->rewardSuccessData.cNamePart, "OnSuccess");
	MDEAddRewardEditButton(pExpander, y, &pGroup->rewardSuccessData);
	y += Y_OFFSET_ROW;

	// On Failure
	pLabel = MDECreateLabel("On Failure", "Reward granted on failure", X_OFFSET_BASE, 0, y, pExpander);
	pField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_ValidatedTextEntry, pGroup->pOrigMission ? pGroup->pOrigMission->params : NULL, pGroup->pMission->params, parse_MissionDefParams, "OnfailureRewardTableName", "RewardTable", "ResourceName");
	MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 75, pDoc);
	eaPush(&pGroup->eaParamFields, pField);
	strcpy(pGroup->rewardFailureData.cNamePart, "OnFailure");
	MDEAddRewardEditButton(pExpander, y, &pGroup->rewardFailureData);
	y += Y_OFFSET_ROW;

	// On Return
	pLabel = MDECreateLabel("On Return", "Reward granted on return", X_OFFSET_BASE, 0, y, pExpander);
	pField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_ValidatedTextEntry, pGroup->pOrigMission ? pGroup->pOrigMission->params : NULL, pGroup->pMission->params, parse_MissionDefParams, "OnreturnRewardTableName", "RewardTable", "ResourceName");
	MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 75, pDoc);
	eaPush(&pGroup->eaParamFields, pField);
	strcpy(pGroup->rewardReturnData.cNamePart, "OnReturn");
	MDEAddRewardEditButton(pExpander, y, &pGroup->rewardReturnData);
	y += Y_OFFSET_ROW;

	// On Replay Return
	pLabel = MDECreateLabel("On Replay Return", "Reward granted on return during replay for secondary credit", X_OFFSET_BASE, 0, y, pExpander);
	pField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_ValidatedTextEntry, pGroup->pOrigMission ? pGroup->pOrigMission->params : NULL, pGroup->pMission->params, parse_MissionDefParams, "OnReplayReturnRewardTableName", "RewardTable", "ResourceName");
	MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 75, pDoc);
	eaPush(&pGroup->eaParamFields, pField);
	strcpy(pGroup->rewardReplayReturnData.cNamePart, "OnReplayReturn");
	MDEAddRewardEditButton(pExpander, y, &pGroup->rewardReplayReturnData);
	y += Y_OFFSET_ROW;

	// Activity Name
	MDEUpdateActivityNames(pGroup);
	pLabel = MDECreateLabel("Activity", "The activity to apply to ActivitySuccess or ActivityReturn rewards", X_OFFSET_BASE, 0, y, pExpander);
	pGroup->pParamActivityName = MEFieldCreateSimpleDataProvided(kMEFieldType_ValidatedTextEntry,  pGroup->pOrigMission ? pGroup->pOrigMission->params : NULL, pGroup->pMission->params, parse_MissionDefParams, "ActivityName", NULL, &pGroup->ppchActivityNames, NULL);
	MDEAddFieldToParent(pGroup->pParamActivityName, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 75, pDoc);
	y += Y_OFFSET_ROW;

	// On Success (Activity)
	pLabel = MDECreateLabel("Activity Success", "Reward granted on success (prior to turning it in) while an activity is active", X_OFFSET_BASE, 0, y, pExpander);
	pField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_ValidatedTextEntry, pGroup->pOrigMission ? pGroup->pOrigMission->params : NULL, pGroup->pMission->params, parse_MissionDefParams, "ActivitySuccessRewardTableName", "RewardTable", "ResourceName");
	MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 75, pDoc);
	eaPush(&pGroup->eaParamFields, pField);
	strcpy(pGroup->rewardActivitySuccessData.cNamePart, "ActivitySuccess");
	MDEAddRewardEditButton(pExpander, y, &pGroup->rewardActivitySuccessData);
	y += Y_OFFSET_ROW;

	// On Return (Activity)
	pLabel = MDECreateLabel("Activity Return", "Reward granted on return while an activity is active", X_OFFSET_BASE, 0, y, pExpander);
	pField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_ValidatedTextEntry, pGroup->pOrigMission ? pGroup->pOrigMission->params : NULL, pGroup->pMission->params, parse_MissionDefParams, "ActivityReturnRewardTableName", "RewardTable", "ResourceName");
	MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 75, pDoc);
	eaPush(&pGroup->eaParamFields, pField);
	strcpy(pGroup->rewardActivityReturnData.cNamePart, "ActivityReturn");
	MDEAddRewardEditButton(pExpander, y, &pGroup->rewardActivityReturnData);
	y += Y_OFFSET_ROW;

	ui_ExpanderSetHeight(pExpander, y);
}

static void MDEInitSoundExpander(MissionEditDoc *pDoc, MDEMissionGroup *pGroup, bool bIsRoot)
{
	UIExpander *pExpander;
	UILabel *pLabel;
	MEField *pField;
	F32 y = 0;

	if(bIsRoot)
	{
		// Create the expander
		pExpander = MDECreateExpander(pGroup->pExGroup, "Sound", "Sound", false);

		// Sound On Start
		pLabel = MDECreateLabel("On Start", "(optional) Sound that will play on start or when entering the map", X_OFFSET_BASE, 0, y, pExpander);
		pField = MEFieldCreateSimpleDataProvided(kMEFieldType_ValidatedTextEntry, pGroup->pOrigMission, pGroup->pMission, parse_MissionDef, "SoundOnStart", NULL, sndGetEventListStatic(), NULL);
		MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL2, y, 0, 1.0, UIUnitPercentage, 3, pDoc);
		eaPush(&pGroup->eaDocFields, pField);
		y += Y_OFFSET_ROW;

		// Sound On Complete
		pLabel = MDECreateLabel("On Complete", "(optional) Sound that will play when mission is complete", X_OFFSET_BASE, 0, y, pExpander);
		pField = MEFieldCreateSimpleDataProvided(kMEFieldType_ValidatedTextEntry, pGroup->pOrigMission, pGroup->pMission, parse_MissionDef, "SoundOnComplete", NULL, sndGetEventListStatic(), NULL);
		MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL2, y, 0, 1.0, UIUnitPercentage, 3, pDoc);
		eaPush(&pGroup->eaDocFields, pField);
		y += Y_OFFSET_ROW;

		// Sound for Ambient
		pLabel = MDECreateLabel("Ambient", "(optional) Sound that will play while mission is active", X_OFFSET_BASE, 0, y, pExpander);
		pField = MEFieldCreateSimpleDataProvided(kMEFieldType_ValidatedTextEntry, pGroup->pOrigMission, pGroup->pMission, parse_MissionDef, "SoundAmbient", NULL, sndGetEventListStatic(), NULL);
		MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL2, y, 0, 1.0, UIUnitPercentage, 3, pDoc);
		eaPush(&pGroup->eaDocFields, pField);
		y += Y_OFFSET_ROW;

		// Sound for Combat
		pLabel = MDECreateLabel("Combat", "(optional) Sound that will play while mission is active and player is in combat", X_OFFSET_BASE, 0, y, pExpander);
		pField = MEFieldCreateSimpleDataProvided(kMEFieldType_ValidatedTextEntry, pGroup->pOrigMission, pGroup->pMission, parse_MissionDef, "SoundCombat", NULL, sndGetEventListStatic(), NULL);
		MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL2, y, 0, 1.0, UIUnitPercentage, 3, pDoc);
		eaPush(&pGroup->eaDocFields, pField);
		y += Y_OFFSET_ROW;

		ui_ExpanderSetHeight(pExpander, y);
	}
}

static void MDEInitOptionsExpander(MissionEditDoc *pDoc, MDEMissionGroup *pGroup, bool bIsRoot)
{
	UIExpander *pExpander;
	UILabel *pLabel;
	MEField *pField;
	F32 y = 0;

	// Create the expander
	pExpander = MDECreateExpander(pGroup->pExGroup, "Options", bIsRoot ? "Options" : "Sub-Options", false);

	if (bIsRoot) {
		// Grant on Map
		pLabel = MDECreateLabel("Auto-grant on Map", "(optional) Mission will be granted on entering this map", X_OFFSET_BASE, 0, y, pExpander);
		pField = MEFieldCreateSimpleDataProvided(kMEFieldType_ValidatedTextEntry, pGroup->pOrigMission, pGroup->pMission, parse_MissionDef, "GrantOnMap", NULL, &g_GEMapDispNames, NULL);
		MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL2, y, 0, 1.0, UIUnitPercentage, 3, pDoc);
		eaPush(&pGroup->eaDocFields, pField);
		y += Y_OFFSET_ROW;
	}

	// Timeout
	pLabel = MDECreateLabel("Timeout (sec)", "If greater than zero, TimeExpired expression function will be true once this much time has passed (use TimeExpired in the Failure condition to have a mission time out)", X_OFFSET_BASE, 0, y, pExpander);
	pField = MEFieldCreateSimple(kMEFieldType_TextEntry, pGroup->pOrigMission, pGroup->pMission, parse_MissionDef, "TimeToComplete");
	MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL2, y, 0, 70, UIUnitFixed, 3, pDoc);
	eaPush(&pGroup->eaDocFields, pField);
	y += Y_OFFSET_ROW;

	if (bIsRoot) {
		// Lockout
		pLabel = MDECreateLabel("Lockout", "Whether or not the mission uses Mission Lockout to limit which players can accept the Mission", X_OFFSET_BASE, 0, y, pExpander);
		pField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pGroup->pOrigMission, pGroup->pMission, parse_MissionDef, "lockoutType", MissionLockoutTypeEnum);
		MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL2, y, 0, 100, UIUnitFixed, 3, pDoc);
		eaPush(&pGroup->eaDocFields, pField);
		y += Y_OFFSET_ROW;

		// Repeatable
		pLabel = MDECreateLabel("Repeatable", "Whether or not the mission can be repeated", X_OFFSET_BASE, 0, y, pExpander);
		pField = MEFieldCreateSimple(kMEFieldType_BooleanCombo, pGroup->pOrigMission, pGroup->pMission, parse_MissionDef, "CanRepeat");
		MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL2, y, 0, 100, UIUnitFixed, 3, pDoc);
		eaPush(&pGroup->eaDocFields, pField);
		y += Y_OFFSET_ROW;

		// Repeat Cooldown
		pLabel = MDECreateLabel("Repeat Cooldowns:", "How much time must pass before the mission can be repeated (hours)", X_OFFSET_BASE, 0, y, pExpander);
		y += Y_OFFSET_ROW;
		pLabel = MDECreateLabel("Since Completed", "How much time must pass since the last time the mission was completed before it can be repeated (hours)", X_OFFSET_BASE+15, 0, y, pExpander);
		pField = MEFieldCreateSimple(kMEFieldType_TextEntry, pGroup->pOrigMission, pGroup->pMission, parse_MissionDef, "RepeatCooldownHours");
		MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL2, y, 0, 70, UIUnitFixed, 3, pDoc);
		eaPush(&pGroup->eaDocFields, pField);
		y += Y_OFFSET_ROW;
		pLabel = MDECreateLabel("Since Started", "How much time must pass since the last time the mission was started before it can be repeated (hours)", X_OFFSET_BASE+15, 0, y, pExpander);
		pField = MEFieldCreateSimple(kMEFieldType_TextEntry, pGroup->pOrigMission, pGroup->pMission, parse_MissionDef, "RepeatCooldownHoursFromStart");
		MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL2, y, 0, 70, UIUnitFixed, 3, pDoc);
		eaPush(&pGroup->eaDocFields, pField);
		y += Y_OFFSET_ROW;
		pLabel = MDECreateLabel("Repeat Count Max", "How how many times can mission be completed in cooldown window.", X_OFFSET_BASE+15, 0, y, pExpander);
		pField = MEFieldCreateSimple(kMEFieldType_TextEntry, pGroup->pOrigMission, pGroup->pMission, parse_MissionDef, "RepeatCooldownCount");
		MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL2, y, 0, 70, UIUnitFixed, 3, pDoc);
		eaPush(&pGroup->eaDocFields, pField);
		y += Y_OFFSET_ROW;
		pLabel = MDECreateLabel("Repeat Blocktime", "If true cooldown blocks are in fixed blocks of time. Example 24 hour cooldown would always start at 12:00am.", X_OFFSET_BASE+15, 0, y, pExpander);
		pField = MEFieldCreateSimple(kMEFieldType_BooleanCombo, pGroup->pOrigMission, pGroup->pMission, parse_MissionDef, "RepeatCooldownBlockTime");
		MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL2, y, 0, 100, UIUnitFixed, 3, pDoc);
		eaPush(&pGroup->eaDocFields, pField);
		y += Y_OFFSET_ROW;
		pLabel = MDECreateLabel("Cooldown On Success", "If set, do not apply a cooldown if the player fails or drops the mission.", X_OFFSET_BASE+15, 0, y, pExpander);
		pField = MEFieldCreateSimple(kMEFieldType_BooleanCombo, pGroup->pOrigMission, pGroup->pMission, parse_MissionDef, "CooldownOnlyOnSuccess");
		MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL2, y, 0, 100, UIUnitFixed, 3, pDoc);
		eaPush(&pGroup->eaDocFields, pField);
		y += Y_OFFSET_ROW;

		// Needs Return
		pLabel = MDECreateLabel("Needs Return", "If false, the mission is 'returned' immediately upon success", X_OFFSET_BASE, 0, y, pExpander);
		pField = MEFieldCreateSimple(kMEFieldType_BooleanCombo, pGroup->pOrigMission, pGroup->pMission, parse_MissionDef, "NeedsReturn");
		MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL2, y, 0, 100, UIUnitFixed, 3, pDoc);
		eaPush(&pGroup->eaDocFields, pField);
		y += Y_OFFSET_ROW;

		// Is Handoff
		pLabel = MDECreateLabel("Is Handoff", NULL, X_OFFSET_BASE, 0, y, pExpander);
		pField = MEFieldCreateSimple(kMEFieldType_BooleanCombo, pGroup->pOrigMission, pGroup->pMission, parse_MissionDef, "IsHandoff");
		MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL2, y, 0, 100, UIUnitFixed, 3, pDoc);
		eaPush(&pGroup->eaDocFields, pField);
		y += Y_OFFSET_ROW;

		// Shareable
		pLabel = MDECreateLabel("Shareable", NULL, X_OFFSET_BASE, 0, y, pExpander);
		pField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pGroup->pOrigMission, pGroup->pMission, parse_MissionDef, "Shareable", MissionShareableTypeEnum);
		MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL2, y, 0, 100, UIUnitFixed, 3, pDoc);
		eaPush(&pGroup->eaDocFields, pField);
		y += Y_OFFSET_ROW;

		// Disallow Drop
		pLabel = MDECreateLabel("Disallow Drop", NULL, X_OFFSET_BASE, 0, y, pExpander);
		pField = MEFieldCreateSimple(kMEFieldType_BooleanCombo, pGroup->pOrigMission, pGroup->pMission, parse_MissionDef, "doNotAllowDrop");
		MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL2, y, 0, 100, UIUnitFixed, 3, pDoc);
		eaPush(&pGroup->eaDocFields, pField);
		y += Y_OFFSET_ROW;

		// Request Grant type
		pLabel = MDECreateLabel("Requested Grant", NULL, X_OFFSET_BASE, 0, y, pExpander);
		pField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pGroup->pOrigMission, pGroup->pMission, parse_MissionDef, "RequestGrantType", MissionRequestGrantTypeEnum);
		MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL2, y, 0, 100, UIUnitFixed, 3, pDoc);
		eaPush(&pGroup->eaDocFields, pField);
		y += Y_OFFSET_ROW;

		//Omit from mission helper.
		pLabel = MDECreateLabel("Omit from Mission Helper", "If true, this mission will appear in the journal but not in the mission tracker.", X_OFFSET_BASE, 0, y, pExpander);
		pField = MEFieldCreateSimple(kMEFieldType_BooleanCombo, pGroup->pOrigMission, pGroup->pMission, parse_MissionDef, "OmitFromMissionTracker");
		MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL2, y, 0, 100, UIUnitFixed, 3, pDoc);
		eaPush(&pGroup->eaDocFields, pField);
		y += Y_OFFSET_ROW;

		//Disable Completion Tracking
		pLabel = MDECreateLabel("Don't Track Completion", "If true, once the player completes this mission, the game will not keep any information on the completion of this mission.  It will be as if the player never played the mission at all.", X_OFFSET_BASE, 0, y, pExpander);
		pField = MEFieldCreateSimple(kMEFieldType_BooleanCombo, pGroup->pOrigMission, pGroup->pMission, parse_MissionDef, "DisableCompletionTracking");
		MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL2, y, 0, 100, UIUnitFixed, 3, pDoc);
		eaPush(&pGroup->eaDocFields, pField);
		y += Y_OFFSET_ROW;
	}

	// Never Uncomplete
	pLabel = MDECreateLabel("Never Uncomplete", "If true, the mission won't uncomplete if a count goes below the target number", X_OFFSET_BASE, 0, y, pExpander);
	pField = MEFieldCreateSimple(kMEFieldType_BooleanCombo, pGroup->pOrigMission, pGroup->pMission, parse_MissionDef, "doNotUncomplete");
	MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL2, y, 0, 100, UIUnitFixed, 3, pDoc);
	eaPush(&pGroup->eaDocFields, pField);
	y += Y_OFFSET_ROW;

	//Refresh autobuy powers.
	pLabel = MDECreateLabel("Refresh Autobuy Powers", "If true, completing this mission will cause the powers system to refresh the owner entity's power trees.", X_OFFSET_BASE, 0, y, pExpander);
	pField = MEFieldCreateSimple(kMEFieldType_BooleanCombo, pGroup->pOrigMission, pGroup->pMission, parse_MissionDef, "RefreshCharacterPowersOnComplete");
	MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL2, y, 0, 100, UIUnitFixed, 3, pDoc);
	eaPush(&pGroup->eaDocFields, pField);
	y += Y_OFFSET_ROW;

	//Only reward when scored on scoreboard
	pLabel = MDECreateLabel("Only Reward When Scored", "If true, scoreboard rewards will only be given to players who participated.", X_OFFSET_BASE, 0, y, pExpander);
	pField = MEFieldCreateSimple(kMEFieldType_BooleanCombo, pGroup->pOrigMission, pGroup->pMission, parse_MissionDef, "OnlyRewardIfScored");
	MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL2, y, 0, 100, UIUnitFixed, 3, pDoc);
	eaPush(&pGroup->eaDocFields, pField);
	y += Y_OFFSET_ROW;	

	ui_ExpanderSetHeight(pExpander, y);
}

static void MDEInitInteractableOverridesExpander(MissionEditDoc *pDoc, MDEMissionGroup *pGroup, bool bIsRoot)
{
	UIExpander *pExpander;
	F32 y = 0;

	// Create the expander
	pExpander = MDECreateExpander(pGroup->pExGroup, "Interactable Extra Properties", "Extra Properties", false);
	pGroup->pInteractableOverrideExpander = pExpander;

	// Put in add button
	pGroup->pAddInteractableOverrideButton = ui_ButtonCreate("Add Extra Properties", X_OFFSET_BASE, y, MDEAddInteractableOverrideCB, pGroup);
	ui_WidgetSetWidth(UI_WIDGET(pGroup->pAddInteractableOverrideButton), 140);
	ui_ExpanderAddChild(pExpander, pGroup->pAddInteractableOverrideButton);

	y += Y_OFFSET_ROW;

	ui_ExpanderSetHeight(pExpander, y);

}

static void MDEAddMissionOfferOverrideCB(UIButton *pButton, MDEContactGroup *pGroup)
{
	MissionOfferOverride *pOverride;

	devassert(pGroup);

	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	// Add a new override
	pOverride = StructCreate(parse_MissionOfferOverride);
	assert(pOverride);
	if (strcmp(MDE_UNASSIGNED_CONTACT_GROUP_NAME, pGroup->pchContactName) != 0)
	{
		pOverride->pcContactName = pGroup->pchContactName;
	}
	pOverride->pMissionOffer = CECreateBlankMissionOffer();
	assert(pOverride->pMissionOffer);
	langMakeEditorCopy(parse_MissionOfferOverride, pOverride, true);

	// Add to the list of mission offer overrides
	eaPush(&pGroup->pDoc->pMission->ppMissionOfferOverrides, pOverride);

	// Update the UI
	MDEMissionChanged(pGroup->pDoc, true);
}

static void MDEAddActionBlockOverrideCB(UIButton *pButton, MDEContactGroup *pGroup){
	ContactDef *pContactDef = (ContactDef *)RefSystem_ReferentFromString(g_ContactDictionary, pGroup->pchContactName);
	ActionBlockOverride *pOverride;
	static char nextName[512];
	S32 counter = 1;

	devassert(pGroup);

	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}


	// Add a new override
	pOverride = StructCreate(parse_ActionBlockOverride);
	assert(pOverride);
	if (strcmp(MDE_UNASSIGNED_CONTACT_GROUP_NAME, pGroup->pchContactName) != 0)
	{
		pOverride->pcContactName = pGroup->pchContactName;
	}	
	pOverride->pSpecialActionBlock = StructCreate(parse_SpecialActionBlock);
	assert(pOverride->pSpecialActionBlock);	

	strcpy(nextName, "SpecialActionBlock");
	while (CEDialogFlowGetSpecialActionBlockByName(pGroup->pDoc->pDialogFlowWindowInfo, nextName, NULL))
	{
		sprintf(nextName, "%s%i", "SpecialActionBlock", counter);
		counter++;
	}
	pOverride->pSpecialActionBlock->name = (char*)allocAddString(nextName);

	langMakeEditorCopy(parse_ActionBlockOverride, pOverride, true);

	////Must have at least 1 dialog block
	//eaCreate(&pOverride->pSpecialDialog->dialogBlock);
	//eaPush(&pOverride->pSpecialDialog->dialogBlock, StructCreate(parse_DialogBlock));
	eaCreate(&pOverride->pSpecialActionBlock->dialogActions);

	eaPush(&pOverride->pSpecialActionBlock->dialogActions, StructCreate(parse_SpecialDialogAction));

	eaPush(&pGroup->pDoc->pMission->ppSpecialActionBlockOverrides, pOverride);

	// Update the UI
	MDEMissionChanged(pGroup->pDoc, true);
}

static void MDEAddImageMenuItemOverrideCB(UIButton *pButton, MDEContactGroup *pGroup)
{
	ImageMenuItemOverride* pOverride;
	
	if( !emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	// Add the new override
	pOverride = StructCreate( parse_ImageMenuItemOverride );
	if( strcmp(MDE_UNASSIGNED_CONTACT_GROUP_NAME, pGroup->pchContactName) != 0)
	{
		pOverride->pcContactName = pGroup->pchContactName;
	}
	pOverride->pImageMenuItem = StructCreate( parse_ContactImageMenuItem );

	langMakeEditorCopy( parse_ImageMenuItemOverride, pOverride, true );
	eaPush(&pGroup->pDoc->pMission->ppImageMenuItemOverrides, pOverride);

	// Update the UI
	MDEMissionChanged( pGroup->pDoc, true );
}

static void MDEInitScoreboardExpander(MissionEditDoc *pDoc, MDEMissionGroup *pGroup, bool bIsRoot)
{
	UIExpander *pExpander;
	F32 y = 0;

	// Create the expander
	pExpander = MDECreateExpander(pGroup->pExGroup, "Scoreboard", "Scoreboard", false);
	pGroup->pScoreboardExpander = pExpander;

	// Put in add button
	pGroup->pAddScoreEventButton = ui_ButtonCreate("Add Event", X_OFFSET_BASE, y, MDEAddScoreEventCB, pGroup);
	ui_WidgetSetWidth(UI_WIDGET(pGroup->pAddScoreEventButton), 80);
	ui_ExpanderAddChild(pExpander, pGroup->pAddScoreEventButton);

	y += Y_OFFSET_ROW;

	ui_ExpanderSetHeight(pExpander, y);

}

static void MDEInitRequestsExpander(MissionEditDoc *pDoc, MDEMissionGroup *pGroup)
{
	UIExpander *pExpander;
	F32 y = 0;

	// Create the expander
	pExpander = MDECreateExpander(pGroup->pExGroup, "Mission Requests", "MissionRequests", false);
	pGroup->pRequestsExpander = pExpander;

	// Put in add button
	pGroup->pAddRequestButton = ui_ButtonCreate("Add Request", X_OFFSET_BASE, y, MDEAddRequestCB, pGroup);
	ui_WidgetSetWidth(UI_WIDGET(pGroup->pAddRequestButton), 80);
	ui_ExpanderAddChild(pExpander, pGroup->pAddRequestButton);

	y += Y_OFFSET_ROW;

	ui_ExpanderSetHeight(pExpander, y);

}

static void MDEInitOpenMissionSuccessRewardsExpander(MissionEditDoc *pDoc, MDEMissionGroup *pGroup, bool bIsRoot)
{
	UIExpander *pExpander;
	UILabel *pLabel;
	MEField *pField;
	F32 y = 0;

	pExpander = MDECreateExpander(pGroup->pExGroup, "Open Mission Success Rewards", "OMRewards", false);
	pGroup->pOpenMissionRewardsExpander = pExpander;

	// Gold rewards
	pLabel = MDECreateLabel("Gold Rewards", "Reward granted to the top player on open mission success", X_OFFSET_BASE, 0, y, pExpander);
	pField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_ValidatedTextEntry, pGroup->pOrigMission ? pGroup->pOrigMission->params : NULL, pGroup->pMission->params, parse_MissionDefParams, "GoldRewardTable", "RewardTable", "ResourceName");
	MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 75, pDoc);
	eaPush(&pGroup->eaParamFields, pField);
	strcpy(pGroup->rewardStartData.cNamePart, "GoldRewards");
	MDEAddRewardEditButton(pExpander, y, &pGroup->rewardOpenMissionGoldData);
	y += Y_OFFSET_ROW;

	// Silver rewards
	pLabel = MDECreateLabel("Silver Rewards", "Reward granted to top 20% of players on open mission success", X_OFFSET_BASE, 0, y, pExpander);
	pField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_ValidatedTextEntry, pGroup->pOrigMission ? pGroup->pOrigMission->params : NULL, pGroup->pMission->params, parse_MissionDefParams, "SilverRewardTable", "RewardTable", "ResourceName");
	MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 75, pDoc);
	eaPush(&pGroup->eaParamFields, pField);
	strcpy(pGroup->rewardSuccessData.cNamePart, "SilverRewards");
	MDEAddRewardEditButton(pExpander, y, &pGroup->rewardOpenMissionSilverData);
	y += Y_OFFSET_ROW;

	// Bronze rewards
	pLabel = MDECreateLabel("Bronze Rewards", "Reward granted to top 50% of players on open mission success", X_OFFSET_BASE, 0, y, pExpander);
	pField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_ValidatedTextEntry, pGroup->pOrigMission ? pGroup->pOrigMission->params : NULL, pGroup->pMission->params, parse_MissionDefParams, "BronzeRewardTable", "RewardTable", "ResourceName");
	MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 75, pDoc);
	eaPush(&pGroup->eaParamFields, pField);
	strcpy(pGroup->rewardFailureData.cNamePart, "BronzeRewards");
	MDEAddRewardEditButton(pExpander, y, &pGroup->rewardOpenMissionBronzeData);
	y += Y_OFFSET_ROW;

	// Default rewards
	pLabel = MDECreateLabel("Default Rewards", "Reward granted to all other players on open mission success", X_OFFSET_BASE, 0, y, pExpander);
	pField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_ValidatedTextEntry, pGroup->pOrigMission ? pGroup->pOrigMission->params : NULL, pGroup->pMission->params, parse_MissionDefParams, "DefaultRewardTable", "RewardTable", "ResourceName");
	MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 75, pDoc);
	eaPush(&pGroup->eaParamFields, pField);
	strcpy(pGroup->rewardReturnData.cNamePart, "DefaultRewards");
	MDEAddRewardEditButton(pExpander, y, &pGroup->rewardOpenMissionDefaultData);
	y += Y_OFFSET_ROW;

	// SuppressUnreliableOpenRewardErrors
	pLabel = MDECreateLabel("Ignore Reward Errors", "Makes this mission ignore error warnings that its reward are unreliable.  Don't use without programmer sign-off.", X_OFFSET_BASE, 0, y, pExpander);
	pField = MEFieldCreateSimple(kMEFieldType_Check, pGroup->pOrigMission, pGroup->pMission, parse_MissionDef, "SuppressUnreliableOpenRewardErrors");
	MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL3, y, 0, 1.0, UIUnitPercentage, 75, pDoc);
	eaPush(&pGroup->eaDocFields, pField);
	y += Y_OFFSET_ROW;

	ui_ExpanderSetHeight(pExpander, y);
}

static void MDEInitOpenMissionFailureRewardsExpander(MissionEditDoc *pDoc, MDEMissionGroup *pGroup, bool bIsRoot)
{
	UIExpander *pExpander;
	UILabel *pLabel;
	MEField *pField;
	F32 y = 0;

	pExpander = MDECreateExpander(pGroup->pExGroup, "Open Mission Failure Rewards", "OMFailureRewards", false);
	pGroup->pOpenMissionRewardsExpander = pExpander;

	// Gold failure rewards
	pLabel = MDECreateLabel("Gold Rewards", "Reward granted to the top player on open mission failure", X_OFFSET_BASE, 0, y, pExpander);
	pField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_ValidatedTextEntry, pGroup->pOrigMission ? pGroup->pOrigMission->params : NULL, pGroup->pMission->params, parse_MissionDefParams, "FailureGoldRewardTable", "RewardTable", "ResourceName");
	MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 75, pDoc);
	eaPush(&pGroup->eaParamFields, pField);
	strcpy(pGroup->rewardStartData.cNamePart, "GoldRewards");
	MDEAddRewardEditButton(pExpander, y, &pGroup->rewardOpenMissionFailureGoldData);
	y += Y_OFFSET_ROW;

	// Silver failure rewards
	pLabel = MDECreateLabel("Silver Rewards", "Reward granted to top 20% of players on open mission failure", X_OFFSET_BASE, 0, y, pExpander);
	pField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_ValidatedTextEntry, pGroup->pOrigMission ? pGroup->pOrigMission->params : NULL, pGroup->pMission->params, parse_MissionDefParams, "FailureSilverRewardTable", "RewardTable", "ResourceName");
	MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 75, pDoc);
	eaPush(&pGroup->eaParamFields, pField);
	strcpy(pGroup->rewardSuccessData.cNamePart, "SilverRewards");
	MDEAddRewardEditButton(pExpander, y, &pGroup->rewardOpenMissionFailureSilverData);
	y += Y_OFFSET_ROW;

	// Bronze failure rewards
	pLabel = MDECreateLabel("Bronze Rewards", "Reward granted to top 50% of players on open mission failure", X_OFFSET_BASE, 0, y, pExpander);
	pField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_ValidatedTextEntry, pGroup->pOrigMission ? pGroup->pOrigMission->params : NULL, pGroup->pMission->params, parse_MissionDefParams, "FailureBronzeRewardTable", "RewardTable", "ResourceName");
	MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 75, pDoc);
	eaPush(&pGroup->eaParamFields, pField);
	strcpy(pGroup->rewardFailureData.cNamePart, "BronzeRewards");
	MDEAddRewardEditButton(pExpander, y, &pGroup->rewardOpenMissionFailureBronzeData);
	y += Y_OFFSET_ROW;

	// Default failure rewards
	pLabel = MDECreateLabel("Default Rewards", "Reward granted to all other players on open mission failure", X_OFFSET_BASE, 0, y, pExpander);
	pField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_ValidatedTextEntry, pGroup->pOrigMission ? pGroup->pOrigMission->params : NULL, pGroup->pMission->params, parse_MissionDefParams, "FailureDefaultRewardTable", "RewardTable", "ResourceName");
	MDEAddFieldToParent(pField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 75, pDoc);
	eaPush(&pGroup->eaParamFields, pField);
	strcpy(pGroup->rewardReturnData.cNamePart, "DefaultRewards");
	MDEAddRewardEditButton(pExpander, y, &pGroup->rewardOpenMissionFailureDefaultData);
	y += Y_OFFSET_ROW;

	ui_ExpanderSetHeight(pExpander, y);
}

/********************
* VARIABLES
********************/
static GEVariableDefGroup *MDECreateVariableDefGroup(MDEMissionGroup *pGroup)
{
	GEVariableDefGroup *pVariableDefGroup = calloc(1, sizeof(*pVariableDefGroup));
	return pVariableDefGroup;
}

static void MDEAddVariableCB(UIButton *pButton, MDEMissionGroup *pGroup)
{
	WorldVariableDef *pWorldVariableDef;
	static int iVarNum = 1;

	// Make sure the resource is checked out of Gimme
	if (!emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	// Add a new group
	pWorldVariableDef = StructCreate(parse_WorldVariableDef);
	do 
	{
		char pcName[32];
		sprintf(pcName, "New_Variable_#%i", iVarNum++);
		pWorldVariableDef->pcName = allocAddString(pcName);
	} while (!eaPush(&pGroup->pMission->eaVariableDefs, pWorldVariableDef));

	// Update the UI
	MDEMissionChanged(pGroup->pDoc, true);
}

static void MDEUpdateVariablesExpander(MDEMissionGroup *pGroup, bool bIsRoot)
{
	F32 y = Y_OFFSET_ROW;
	int i;

	// Create necessary variable def groups
	while (eaSize(&pGroup->pMission->eaVariableDefs) > eaSize(&pGroup->eaVariableDefGroups))
		eaPush(&pGroup->eaVariableDefGroups, MDECreateVariableDefGroup(pGroup));

	// Update the variable def groups
	for (i = 0; i < eaSize(&pGroup->pMission->eaVariableDefs); i++)
	{
		WorldVariableDef ***peaVariableDefs = &pGroup->pMission->eaVariableDefs;
		WorldVariableDef ***peaOrigVariableDefs = (pGroup->pOrigMission ? &pGroup->pOrigMission->eaVariableDefs : NULL);
		WorldVariableDef *pOrigVariableDef = peaOrigVariableDefs ? eaGet(peaOrigVariableDefs, i) : NULL;

		assert(pGroup->eaVariableDefGroups);
		y = GEUpdateVariableDefGroupFromNames(pGroup->eaVariableDefGroups[i], UI_WIDGET(pGroup->pVariablesExpander), NULL, NULL, peaVariableDefs, pGroup->pMission->eaVariableDefs[i], pOrigVariableDef, NULL, i, X_OFFSET_BASE, X_OFFSET_CONTROL, y, MDEFieldChangedCB, MDEFieldPreChangeCB, pGroup->pDoc);
	}

	// Remove unused action groups
	while (i < eaSize(&pGroup->eaVariableDefGroups))
	{
		GEFreeVariableDefGroup(pGroup->eaVariableDefGroups[i]);
		eaRemove(&pGroup->eaVariableDefGroups, i);
	}

	ui_ExpanderSetHeight(pGroup->pVariablesExpander, y);
}

static void MDEInitVariablesExpander(MissionEditDoc *pDoc, MDEMissionGroup *pGroup, bool bIsRoot)
{
	GEVariableDefGroup *pVarDefGroup = calloc(1, sizeof(*pVarDefGroup));
	UIExpander *pExpander;
	UIButton *pButton;
	F32 y = 0;

	pExpander = MDECreateExpander(pGroup->pExGroup, "Variables", "Variables", false);
	pGroup->pVariablesExpander = pExpander;

	// Add variable button
	pButton = ui_ButtonCreate("Add Variable", X_OFFSET_BASE, y, MDEAddVariableCB, pGroup);
	ui_WidgetSetWidth(UI_WIDGET(pButton), 90);
	ui_ExpanderAddChild(pExpander, pButton);

	y += Y_OFFSET_ROW;
	ui_ExpanderSetHeight(pExpander, y);
}

static void MDEInitMissionGroupDisplay(MDEMissionGroup *pGroup, bool bIsRoot)
{
	UILabel *pLabel;
	UIExpanderGroup *pExGroup;
	MEField *pField;
	UISeparator *pSeparator = NULL;
	MissionDef *pMission = pGroup->pMission;
	MissionDef *pOrigMission = pGroup->pOrigMission;
	MissionEditDoc *pDoc = pGroup->pDoc;
	UIWindow *pWin = pGroup->pWindow;
	F32 y = 0.f;

	if (!pGroup || !pGroup->pWindow)
		return;

	// Mission Name
	pLabel = ui_LabelCreate("Mission Name", X_OFFSET_BASE, y);
	ui_WindowAddChild(pWin, pLabel);
	pField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigMission, pMission, parse_MissionDef, "Name");
	MDEAddFieldToParent(pField, UI_WIDGET(pWin), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 21, pDoc);
	MEFieldSetChangeCallback(pField, MDESetNameCB, pGroup);
	eaPush(&pGroup->eaDocFields, pField);

	y += Y_OFFSET_ROW;

	if (bIsRoot) {
		// Scope
		pLabel = ui_LabelCreate("Scope", X_OFFSET_BASE, y);
		ui_WindowAddChild(pWin, pLabel);
		pField = MEFieldCreateSimpleDataProvided(kMEFieldType_TextEntry, pOrigMission, pMission, parse_MissionDef, "Scope", NULL, &geaScopes, NULL);
		MDEAddFieldToParent(pField, UI_WIDGET(pWin), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 21, pDoc);
		MEFieldSetChangeCallback(pField, MDESetScopeCB, pDoc);
		eaPush(&pGroup->eaDocFields, pField);

		y += Y_OFFSET_ROW;

		// File Name
		pLabel = ui_LabelCreate("File Name", X_OFFSET_BASE, y);
		ui_WindowAddChild(pWin, pLabel);
		pDoc->pFileButton = ui_GimmeButtonCreate(X_OFFSET_CONTROL, y, "Mission", pMission->name, pMission);
		ui_WindowAddChild(pWin, pDoc->pFileButton);
		pLabel = ui_LabelCreate(pDoc->pMission->filename, X_OFFSET_CONTROL+20, y);
		ui_WindowAddChild(pWin, pLabel);
		ui_WidgetSetWidthEx(UI_WIDGET(pLabel), 1.0, UIUnitPercentage);
		ui_WidgetSetPaddingEx(UI_WIDGET(pLabel), 0, 21, 0, 0);
		pDoc->pFilenameLabel = pLabel;

		y += Y_OFFSET_ROW;

		// Version
		pLabel = ui_LabelCreate("Version", X_OFFSET_BASE, y);
		ui_WindowAddChild(pWin, pLabel);
		pField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigMission, pMission, parse_MissionDef, "version");
		MDEAddFieldToParent(pField, UI_WIDGET(pWin), X_OFFSET_CONTROL, y, 0, 70, UIUnitFixed, 3, pDoc);
		eaPush(&pGroup->eaDocFields, pField);

		y += Y_OFFSET_ROW;
	}

	// Put a separator above the expander group
	pSeparator = ui_SeparatorCreate(UIHorizontal);
	ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
	ui_WindowAddChild(pWin, pSeparator);
	y += 2;

	// Main expander group
	pExGroup = ui_ExpanderGroupCreate();
	ui_ExpanderGroupSetReflowCallback(pExGroup, MDEExpandReflowCB, pGroup);
	ui_WidgetSetPosition(UI_WIDGET(pExGroup), 0, y);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pExGroup), 1.0, 1.0, UIUnitPercentage, UIUnitPercentage);
	ui_WindowAddChild(pWin, pExGroup);
	pGroup->pExGroup = pExGroup;

	// Other expanders
	MDEInitInfoExpander(pDoc, pGroup, bIsRoot);
	if (bIsRoot)
	{
		MDEInitContactOverridesExpander(pDoc, pGroup);
	}
	MDEInitDisplayExpander(pDoc, pGroup, bIsRoot);
	if(bIsRoot) {
		MDEInitLevelExpander(pDoc, pGroup);
		MDEInitWarpExpander(pDoc, pGroup);
	}
	MDEInitWaypointsExpander(pDoc, pGroup, bIsRoot);
	MDEInitMapsExpander(pDoc, pGroup, bIsRoot);
	MDEInitEventsExpander(pDoc, pGroup, bIsRoot);
	MDEInitConditionsExpander(pDoc, pGroup, bIsRoot);
	MDEInitActionsExpander(pDoc, pGroup, bIsRoot);
	if (missiondef_GetType(pMission) != MissionType_OpenMission){
		MDEInitDropsExpander(pDoc, pGroup, bIsRoot);
		MDEInitRewardsExpander(pDoc, pGroup, bIsRoot);
	}else{
		MDEInitOpenMissionSuccessRewardsExpander(pDoc, pGroup, bIsRoot);
		MDEInitOpenMissionFailureRewardsExpander(pDoc, pGroup, bIsRoot);
	}
	if (bIsRoot) {
		MDEInitNumericScalesExpander(pDoc, pGroup, bIsRoot);
		MDEInitInteractableOverridesExpander(pDoc, pGroup, bIsRoot);		
	}
	if (missiondef_GetType(pMission) == MissionType_OpenMission)
		MDEInitScoreboardExpander(pDoc, pGroup, bIsRoot);
	MDEInitOptionsExpander(pDoc, pGroup, bIsRoot);
	MDEInitRequestsExpander(pDoc, pGroup);
	MDEInitSoundExpander(pDoc, pGroup, bIsRoot);
	MDEInitVariablesExpander(pDoc, pGroup, bIsRoot);

	// Add this mission's window to the display
	ui_ScrollAreaAddChild(pDoc->pScrollArea, UI_WIDGET(pWin));
}

static F32 MDEFindLowYForNewEntry(MissionEditDoc *pDoc)
{
	F32 fLowY = 15.f;

	if (pDoc)
	{
		if (pDoc->pMainMissionGroup) 
		{
			fLowY = pDoc->pMainMissionGroup->pWindow->widget.y + pDoc->pMainMissionGroup->pWindow->widget.height + Y_OFFSET_SPACING;
		}

		FOR_EACH_IN_EARRAY(pDoc->eaSubMissionGroups, MDEMissionGroup, pGroup)
		{
			if (pGroup && pGroup->pWindow)
			{
				MAX1(fLowY, pGroup->pWindow->widget.y + pGroup->pWindow->widget.height + Y_OFFSET_SPACING);
			}			
		}
		FOR_EACH_END

		FOR_EACH_IN_EARRAY(pDoc->eaContactGroups, MDEContactGroup, pGroup)
		{
			if (pGroup && pGroup->pWindow)
			{
				MAX1(fLowY, pGroup->pWindow->widget.y + pGroup->pWindow->widget.height + Y_OFFSET_SPACING);
			}			
		}
		FOR_EACH_END
	}

	return fLowY;
}


static MDEMissionGroup *MDEInitMissionGroup(MissionEditDoc *pDoc, MissionDef *pMission, MissionDef *pOrigMission, bool bIsRoot, int index)
{
	MDEMissionGroup *pGroup;
	UIWindow *pWin;
	F32 fLowY = 15;
	F32 fWidth;

	// Create the group
	pGroup = calloc(1, sizeof(MDEMissionGroup));
	pGroup->pDoc = pDoc;
	pGroup->pMission = pMission;
	pGroup->pOrigMission = pOrigMission;
	pGroup->pcName = strdup(pMission->name);

	// Find the proper position
	if (pDoc->pMainMissionGroup) {
		fWidth = pDoc->pMainMissionGroup->pWindow->widget.width;
	} else {
		fWidth = EditorPrefGetFloat(MISSION_EDITOR, "Options", "MissionWidth", 420);
	}

	fLowY = MDEFindLowYForNewEntry(pDoc);

	// Create the window
	pWin = ui_WindowCreate(pMission->name, 15, fLowY, fWidth, 600);
	ui_WidgetSetContextCallback((UIWidget*)pWin, MDEWindowRightClickCB, pGroup);
	ui_WindowSetClosable(pWin, false);
	pGroup->pWindow = pWin;

	// Init all other display elements
	MDEInitMissionGroupDisplay(pGroup, bIsRoot);

	return pGroup;
}

static void MDEScrollAreaTick(UIScrollArea *scrollarea, UI_PARENT_ARGS)
{
	MissionEditDoc *pDoc = (MissionEditDoc *)scrollarea->widget.userinfo;
	UI_GET_COORDINATES(scrollarea);

	if (pDoc && 
		pDoc->pDialogFlowWindowInfo &&
		pDoc->pDialogFlowWindowInfo->pHighlightedWindow &&
		pDoc->pDialogFlowWindowInfo->pHighlightedExpander &&
		pDoc->pDialogFlowWindowInfo->pWidgetToFocusOnDialogNodeClick)
	{
		ui_ScrollAreaScrollToPosition(scrollarea, scrollarea->widget.sb->scrollX, 
			pDoc->pDialogFlowWindowInfo->pHighlightedWindow->widget.y + 			
			pDoc->pDialogFlowWindowInfo->pHighlightedExpander->group->widget.y + 
			pDoc->pDialogFlowWindowInfo->pHighlightedExpander->widget.y +
			pDoc->pDialogFlowWindowInfo->pWidgetToFocusOnDialogNodeClick->y);
		pDoc->pDialogFlowWindowInfo->pHighlightedExpander = NULL;
		pDoc->pDialogFlowWindowInfo->pHighlightedWindow = NULL;
	}

	ui_ScrollAreaTick(scrollarea, UI_PARENT_VALUES);
}

static void MDEInitMainWindow(MissionEditDoc *pDoc)
{
	int i;

	// Create the main window
	pDoc->pMainWindow = ui_WindowCreate(pDoc->pMission->name, 15, 50, 350, 600);
	ui_WidgetSkin(UI_WIDGET(pDoc->pMainWindow), gMainWindowSkin);
	EditorPrefGetWindowPosition(MISSION_EDITOR, "Window Position", "Main", pDoc->pMainWindow);

	// Create the scroll area
	pDoc->pScrollArea = ui_ScrollAreaCreate(0, 0, 0, 0, MDE_SCROLLAREA_WIDTH, MDE_SCROLLAREA_WIDTH, true, true);
	pDoc->pScrollArea->widget.tickF = MDEScrollAreaTick;
	pDoc->pScrollArea->widget.userinfo = pDoc;
	ui_ScrollAreaSetDraggable(pDoc->pScrollArea, true);
	ui_ScrollAreaSetChildScale(pDoc->pScrollArea, EditorPrefGetFloat(MISSION_EDITOR, "Window Zoom", "Main", MDE_DEFAULT_ZOOM));
	ui_WidgetSetDimensionsEx(UI_WIDGET(pDoc->pScrollArea), 1.0, 1.0, UIUnitPercentage, UIUnitPercentage);
	pDoc->pScrollArea->autosize = true;
	ui_WindowAddChild(pDoc->pMainWindow, pDoc->pScrollArea);

	// Create window for main mission
	pDoc->pMainMissionGroup = MDEInitMissionGroup(pDoc, pDoc->pMission, pDoc->pOrigMission, true, 0);
	
	// Create windows for sub-missions
	for(i=0; i<eaSize(&pDoc->pMission->subMissions); ++i) {
		eaPush(&pDoc->eaSubMissionGroups, MDEInitMissionGroup(pDoc, pDoc->pMission->subMissions[i], pDoc->pOrigMission ? pDoc->pOrigMission->subMissions[i] : NULL, false, i+1));
	}

	// Update the contacts
	MDEUpdateContactDisplay(pDoc);

	// Make sure main mission has focus
	ui_SetFocus(UI_WIDGET(pDoc->pMainMissionGroup->pWindow));
}


static void MDEInitDisplay(EMEditor *pEditor, MissionEditDoc *pDoc)
{
	// Create the window (ignore field change callbacks during init)
	pDoc->bIgnoreFieldChanges = true;
	pDoc->bIgnoreFilenameChanges = true;
	MDEInitMainWindow(pDoc);
	pDoc->bIgnoreFieldChanges = false;
	pDoc->bIgnoreFilenameChanges = false;

	// Update dynamic data
	MDEUpdateDisplay(pDoc);
	MDELayoutMissions(pDoc);

	// Editor Manager needs to be told about the windows used
	ui_WindowPresent(pDoc->pMainWindow);
	pDoc->emDoc.primary_ui_window = pDoc->pMainWindow;
	eaPush(&pDoc->emDoc.ui_windows, pDoc->pMainWindow);
}

static void MDECreateContactOverride(UIButton *pButton, UITextEntry *pContactTextEntry)
{
	const char *pchContactName = ui_TextEntryGetText(pContactTextEntry);
	if (pchContactName && pchContactName[0])
	{
		MissionEditDoc *pDoc = MDEGetActiveDoc();
		assert(pDoc);

		// Make sure the resource is checked out of Gimme
		if (!emDocIsEditable(&pDoc->emDoc, true)) {
			return;
		}

		if (MDEFindContactGroup(pDoc, pchContactName))
		{
			ui_DialogPopup("Create Contact Override Failed", "There is already an override for this contact.");
		}

		// Create a contact group
		MDEGetOrCreateContactGroup(pDoc, pchContactName);

		// Update the display
		MDEUpdateDisplay(pDoc);
	}
	else
	{
		ui_DialogPopup("Create Contact Override Failed", "Please choose a contact.");
	}
}


static void MDEInitToolbarsAndMenus(EMEditor *pEditor)
{
	EMToolbar *pToolbar;
	UIButton *pButton;
	UITextEntry *pTextEntry;

	// Main Toolbar
	pToolbar = emToolbarCreateFileToolbar(EM_FILE_TOOLBAR_NEW|EM_FILE_TOOLBAR_OPEN|EM_FILE_TOOLBAR_SAVE);
	eaPush(&pEditor->toolbars, pToolbar);
	eaPush(&pEditor->toolbars, emToolbarCreateWindowToolbar());

	// Zoom Toolbar
	pToolbar = emToolbarCreate(0);
	pButton = ui_ButtonCreateImageOnly("32px_Zoom_Out", 0, 0, MDEZoomOutCB, NULL);
	ui_WidgetSetDimensions(UI_WIDGET(pButton), emToolbarGetHeight(pToolbar), emToolbarGetHeight(pToolbar));
	ui_ButtonSetImageStretch(pButton, true);
	emToolbarAddChild(pToolbar, pButton, false);
	pButton = ui_ButtonCreateImageOnly("32px_Zoom_In", 25, 0, MDEZoomInCB, NULL);
	ui_WidgetSetDimensions(UI_WIDGET(pButton), emToolbarGetHeight(pToolbar), emToolbarGetHeight(pToolbar));
	ui_ButtonSetImageStretch(pButton, true);
	emToolbarAddChild(pToolbar, pButton, true);
	eaPush(&pEditor->toolbars, pToolbar);

	// Mission Toolbar
	pToolbar = emToolbarCreate(0);
	
	pButton = ui_ButtonCreate("Create Sub-Mission", 0, 0, MDECreateSubMissionCB, NULL);
	ui_WidgetSetDimensions(UI_WIDGET(pButton), 140, emToolbarGetHeight(pToolbar));
	emToolbarAddChild(pToolbar, pButton, false);
	
	pButton = ui_ButtonCreate("Re-Layout", 145, 0, MDELayoutCB, NULL);
	ui_WidgetSetDimensions(UI_WIDGET(pButton), 100, emToolbarGetHeight(pToolbar));
	emToolbarAddChild(pToolbar, pButton, false);
	
	pButton = ui_ButtonCreate("Toggle Lines", 250, 0, MDEToggleLinesCB, NULL);
	ui_WidgetSetDimensions(UI_WIDGET(pButton), 100, emToolbarGetHeight(pToolbar));
	emToolbarAddChild(pToolbar, pButton, false);

	pButton = ui_ButtonCreate("Audio Export", 355, 0, MDEAudioExportCB, NULL);
	ui_WidgetSetDimensions(UI_WIDGET(pButton), 100, emToolbarGetHeight(pToolbar));
	emToolbarAddChild(pToolbar, pButton, true);

	eaPush(&pEditor->toolbars, pToolbar);

	// Contact Override Toolbar
	pToolbar = emToolbarCreate(0);

	pTextEntry = ui_TextEntryCreateWithDictionaryCombo("", 0, 0, g_ContactDictionary, parse_ContactDef, "name", true, true, true, true);
	ui_WidgetSetDimensions(UI_WIDGET(pTextEntry), 200, emToolbarGetHeight(pToolbar));
	emToolbarAddChild(pToolbar, pTextEntry, true);

	pButton = ui_ButtonCreate("Create Contact Override", 205, 0, MDECreateContactOverride, pTextEntry);
	ui_WidgetSetDimensions(UI_WIDGET(pButton), 165, emToolbarGetHeight(pToolbar));
	emToolbarAddChild(pToolbar, pButton, true);

	eaPush(&pEditor->toolbars, pToolbar);

	// File menu
	emMenuItemCreate(pEditor, "mde_revertmission", "Revert", NULL, NULL, "MDE_RevertMission");
	emMenuRegister(pEditor, emMenuCreate(pEditor, "File", "mde_revertmission", NULL));

	// Mission menu
	emMenuItemCreate(pEditor, "mde_createmission", "Create Sub-Mission", NULL, NULL, "MDE_CreateMission");
	emMenuItemCreate(pEditor, "mde_clonemission", "Clone Sub-Mission", MDESelectedIsSubMission, NULL, "MDE_CloneMission");
	emMenuItemCreate(pEditor, "mde_deletemission", "Delete Sub-Mission", MDESelectedIsSubMission, NULL, "MDE_DeleteMission");
	emMenuItemCreate(pEditor, "mde_pastemission", "Paste Mission", MDEIsMissionOnClipboard, NULL, "MDE_PasteMission");
	emMenuItemCreate(pEditor, "mde_deleteunused", "Delete Unused Sub-Missions", NULL, NULL, "MDE_DeleteUnused");
	emMenuItemCreate(pEditor, "mde_removetemplate", "Create Def from Template", MDEIsSelectedMissionTemplated, NULL, "MDE_RemoveTemplate");
	emMenuRegister(pEditor, emMenuCreate(pEditor, "Mission", "mde_copymission", "mde_clonemission", "mde_deletemission", "mde_pastemission", "mde_deleteunused", "mde_removetemplate", NULL));
}


//---------------------------------------------------------------------------------------------------
// Public Interface
//---------------------------------------------------------------------------------------------------

void MDEInitData(EMEditor *pEditor)
{
	if (!gInitializedEditor) {
		MDEInitToolbarsAndMenus(pEditor);

		// Have Editor Manager handle a lot of change tracking
		emAutoHandleDictionaryStateChange(pEditor, "Mission", true, NULL, NULL, NULL, NULL, NULL);

		// Register dictionary change listeners
		resDictRegisterEventCallback(g_MissionDictionary, MDEMissionDictChanged, NULL);

		resGetUniqueScopes(g_MissionDictionary, &geaScopes);

		gBoldExpanderSkin = ui_SkinCreate(NULL);
		SET_HANDLE_FROM_STRING(g_ui_FontDict, "Default_Bold", gBoldExpanderSkin->hNormal);

		gMainWindowSkin = ui_SkinCreate(NULL);
		gMainWindowSkin->background[0] = CreateColorRGB(146, 143, 135);

		gShowLines = EditorPrefGetInt(MISSION_EDITOR, "Options", "ShowLines", 1);

		// Call the below function so the dialog flow windows display fine
		InitializeContactEditorLook();

		// Set up allowed interaction class types
		eaPush(&s_eaClassNames, "Clickable");
		eaPush(&s_eaClassNames, "Contact");
		eaPush(&s_eaClassNames, "CraftingStation");
		//eaPush(&s_eaClassNames, "Destructible"); // Currently disabled since runtime doesn't handle this well
		eaPush(&s_eaClassNames, "Door");
		eaPush(&s_eaClassNames, "TeamCorral");

		eaPush(&geaWhenOptions, "All Conditions");
		eaPush(&geaWhenOptions, "Any One Condition");
		eaPush(&geaWhenOptions, "Count Conditions");

		resRequestAllResourcesInDictionary("MissionDef");
		
		// Request all guild stat defs from the server
		resRequestAllResourcesInDictionary("GuildStatDef");

		// Request all contact defs from the server
		resRequestAllResourcesInDictionary("Contact");

		// Request all item assignment defs from the server
		resRequestAllResourcesInDictionary("ItemAssignmentDef");

		MDERefreshOpenMissionList();

		gInitializedEditor = true;
	}
}

// Strips off any prepended mission name
static bool MDEMissionFixupDialogNameForEditing(MissionDef* pMission, char **pestrDialogName)
{
	if(pMission && pestrDialogName && (*pestrDialogName) && strStartsWith((*pestrDialogName), pMission->name))
	{
		estrRemoveUpToFirstOccurrence(pestrDialogName, '/');
		return true;
	}
	return false;
}

static void MDEMissionPrependMissionName(char **pestrDialogName, const char *pchMissionName)
{
	size_t iStrLen = strlen(pchMissionName);
	unsigned int uStrLen = iStrLen >= 0 ? (unsigned int) iStrLen : 0;
	estrInsert(pestrDialogName, 0, "/", 1);
	estrInsert(pestrDialogName, 0, pchMissionName, uStrLen);
}

// prepends mission name if necessary
bool MDEMissionFixupDialogNameForSaving(MissionDef* pMission, const char* pchContactName, char **pestrDialogName)
{
	if(pMission && (pMission->ppSpecialDialogOverrides || pMission->ppMissionOfferOverrides) && pchContactName && pestrDialogName && (*pestrDialogName))
	{
		int i;		
		for (i=0; i < eaSize(&pMission->ppSpecialDialogOverrides); i++) 
		{
			if(stricmp_safe(pMission->ppSpecialDialogOverrides[i]->pcContactName, pchContactName) == 0)
			{
				SpecialDialogBlock* pBlock = pMission->ppSpecialDialogOverrides[i]->pSpecialDialog;
				if(pBlock && stricmp_safe(pBlock->name, (*pestrDialogName)) == 0)
				{
					MDEMissionPrependMissionName(pestrDialogName, pMission->name);
					return true;
				}
			}
		}

		// Look in mission offers
		for (i = 0; i < eaSize(&pMission->ppMissionOfferOverrides); ++i)
		{
			// Does the contact name and the dialog name match?
			if(pMission->ppMissionOfferOverrides[i]->pMissionOffer &&  
				pMission->ppMissionOfferOverrides[i]->pMissionOffer->pchSpecialDialogName && 
				stricmp_safe(pMission->ppMissionOfferOverrides[i]->pcContactName, pchContactName) == 0 &&
				stricmp_safe(pMission->ppMissionOfferOverrides[i]->pMissionOffer->pchSpecialDialogName, *pestrDialogName) == 0)
			{
				MDEMissionPrependMissionName(pestrDialogName, pMission->name);
				return true;
			}
		}
	}
	return false;
}

static void MDEMissionPostOpenFixup(MissionDef* pRootMission, MissionDef *pMission)
{
	WorldGameActionBlock block;
	S32 i;
	char* estrTrimmedName = NULL;

	// Make sure optional structs are present
	MDEFillOptionalStructs(pMission);

	// Make sure ref names are filled in
	missiondef_CreateRefStringsRecursive(pMission, NULL);

	// Make editor copy
	langMakeEditorCopy(parse_MissionDef, pMission, true);

	// Fix up editor copies of messages
	MDEFixupMessages(pRootMission, pMission);

	// Clean expressions
	exprClean(pMission->missionReqs);
	exprClean(pMission->pMapRequirements);
	exprClean(pMission->pDiscoverCond);
	exprClean(pMission->pMapSuccess);
	exprClean(pMission->pMapFailure);
	block.eaActions = pMission->ppOnStartActions;
	gameactionblock_Clean(&block);
	block.eaActions = pMission->ppSuccessActions;
	gameactionblock_Clean(&block);
	block.eaActions = pMission->ppFailureActions;
	gameactionblock_Clean(&block);
	block.eaActions = pMission->ppOnReturnActions;
	gameactionblock_Clean(&block);
	for(i=0; i<eaSize(&pMission->ppInteractableOverrides); i++) {
		WorldInteractionPropertyEntry *pEntry = pMission->ppInteractableOverrides[i]->pPropertyEntry;
		interaction_CleanProperties(pEntry);
	}

	// Recurse on submissions
	for (i = 0; i < eaSize(&pMission->subMissions); i++)
		MDEMissionPostOpenFixup(pRootMission, pMission->subMissions[i]);

	for (i=0; i < eaSize(&pMission->ppSpecialDialogOverrides); i++) 
	{
		SpecialDialogBlock *pBlock = pMission->ppSpecialDialogOverrides[i]->pSpecialDialog;
		if(pBlock)
		{
			if (pBlock->pCondition)
			{
				exprClean(pBlock->pCondition);
			}
		}
	}

	for(i = 0 ; i < eaSize(&pMission->ppMissionOfferOverrides); ++i) 
	{
		MissionOfferOverride *pOverride = pMission->ppMissionOfferOverrides[i];

		if (pOverride && pOverride->pMissionOffer)
		{
			// Create dummy dialog blocks. The ones which are not used will be destroy in pre-save
			if (eaSize(&pOverride->pMissionOffer->greetingDialog) == 0) 
			{
				eaPush(&pOverride->pMissionOffer->greetingDialog, StructCreate(parse_DialogBlock));
			}
			if (eaSize(&pOverride->pMissionOffer->offerDialog) == 0) 
			{
				eaPush(&pOverride->pMissionOffer->offerDialog, StructCreate(parse_DialogBlock));
			}
			if (eaSize(&pOverride->pMissionOffer->inProgressDialog) == 0) 
			{
				eaPush(&pOverride->pMissionOffer->inProgressDialog, StructCreate(parse_DialogBlock));
			}
			if (eaSize(&pOverride->pMissionOffer->completedDialog) == 0) 
			{
				eaPush(&pOverride->pMissionOffer->completedDialog, StructCreate(parse_DialogBlock));
			}
			if (eaSize(&pOverride->pMissionOffer->failureDialog) == 0) 
			{
				eaPush(&pOverride->pMissionOffer->failureDialog, StructCreate(parse_DialogBlock));
			}

			if (pOverride->pMissionOffer->pchSpecialDialogName)
			{
				if(!estrTrimmedName)
					estrCreate(&estrTrimmedName);
				else
					estrClear(&estrTrimmedName);

				estrCopy2(&estrTrimmedName, pOverride->pMissionOffer->pchSpecialDialogName);

				if(MDEMissionFixupDialogNameForEditing(pMission, &estrTrimmedName))
					pOverride->pMissionOffer->pchSpecialDialogName = allocAddString(estrTrimmedName);
			}
		}
	}

	if(estrTrimmedName)
		estrDestroy(&estrTrimmedName);
}

static void MDEMissionPreSaveEventFixup(MissionDef* pRootMission, MissionDef* pMission)
{
	int i;

	// Recurse on submissions
	for (i = 0; i < eaSize(&pMission->subMissions); i++)
		MDEMissionPreSaveEventFixup(pRootMission, pMission->subMissions[i]);

}

static void MDEMissionPreSaveFixup(MissionDef *pMission)
{
	S32 i;
	char* estrBuf = NULL;
	StructFreeStringSafe(&pMission->genesisZonemap);
	
	// Remove unused optional structs
	MDERemoveUnusedOptionalStructs(pMission);

	// Fix up editor copies of messages
	MDEFixupMessages(pMission, pMission);

	// Set return type field
	if (pMission->msgReturnStringMsg.pEditorCopy->pcDefaultString) {
		pMission->eReturnType = MissionReturnType_Message;
	} else {
		pMission->eReturnType = MissionReturnType_None;
	}

	GEMissionLevelDefPreSaveFixup( &pMission->levelDef );

	estrCreate(&estrBuf);

	for(i = 0; i < eaSize(&pMission->ppMissionOfferOverrides); ++i) 
	{
		MissionOfferOverride *pOverride = pMission->ppMissionOfferOverrides[i];

		if (pOverride && pOverride->pMissionOffer)
		{
			// Strip unused dialogs in the mission offer
			CEStripEmptyDialogBlocks(&pOverride->pMissionOffer->greetingDialog);
			CEStripEmptyDialogBlocks(&pOverride->pMissionOffer->offerDialog);
			CEStripEmptyDialogBlocks(&pOverride->pMissionOffer->inProgressDialog);
			CEStripEmptyDialogBlocks(&pOverride->pMissionOffer->completedDialog);
			CEStripEmptyDialogBlocks(&pOverride->pMissionOffer->failureDialog);

			if (pOverride->pMissionOffer->pchSpecialDialogName)
			{
				estrClear(&estrBuf);
				estrCopy2(&estrBuf, pOverride->pMissionOffer->pchSpecialDialogName);
				if(MDEMissionFixupDialogNameForSaving(pMission, pOverride->pcContactName, &estrBuf))
					pOverride->pMissionOffer->pchSpecialDialogName = allocAddString(estrBuf);
			}
		}

	}

	MDEMissionPreSaveEventFixup(pMission, pMission);

	estrDestroy(&estrBuf);
}


void MDEOncePerFrame(EMEditorDoc *pDoc)
{
	MissionEditDoc *pMissionDoc = (MissionEditDoc*) pDoc;
	if (pMissionDoc->bNeedsUpdate)
	{
		MDEUpdateDisplay(pMissionDoc);
		pMissionDoc->bNeedsUpdate = false;
	}
}


static void MDEChoiceTableDictChanged(enumResourceEventType eType, const char *pDictName, const char *pcName, Referent pReferent, void *pUserData)
{
	if (eType == RESEVENT_RESOURCE_ADDED || eType == RESEVENT_RESOURCE_MODIFIED || eType == RESEVENT_RESOURCE_REMOVED)
	{
		int i;
		for (i = 0; i < eaSize(&s_MissionEditor.open_docs); i++)
		{
			MissionEditDoc *pDoc = (MissionEditDoc*) s_MissionEditor.open_docs[i];
			pDoc->bNeedsUpdate = true;
		}
	}
}

static MissionEditDoc *MDEInitDoc(MissionDef *pMission, bool bCreated)
{
	MissionEditDoc *pDoc;
	char nameBuf[260];

	// Initialize the structure
	pDoc = (MissionEditDoc*)calloc(1,sizeof(MissionEditDoc));

	// Fill in the mission data
	if (bCreated) {
		MissionMap* pMap = NULL;

		pDoc->pMission = StructCreate(parse_MissionDef);
		assert(pDoc->pMission);
		emMakeUniqueDocName(&pDoc->emDoc, "New_Mission", "mission", "Mission");
		pDoc->pMission->name = (char*)allocAddString(pDoc->emDoc.doc_name);
		sprintf(nameBuf, "defs/missions/%s.mission", pDoc->pMission->name);
		pDoc->pMission->filename = allocAddString(nameBuf);

/*		// Set the objective map to be the current map
		pMap = StructCreate(parse_MissionMap);
		pMap->pchMapName = allocAddString(zmapInfoGetPublicName(NULL));
		eaPush(&pDoc->pMission->eaObjectiveMaps, pMap);*/

		MDEMissionPostOpenFixup(pDoc->pMission, pDoc->pMission);
	} else {
		pDoc->pMission = StructClone(parse_MissionDef, pMission);
		assert(pDoc->pMission);
		MDEMissionPostOpenFixup(pDoc->pMission, pDoc->pMission);
		pDoc->pOrigMission = StructClone(parse_MissionDef, pDoc->pMission);
	}

	// Set up the undo stack
	pDoc->emDoc.edit_undo_stack = EditUndoStackCreate();
	EditUndoSetContext(pDoc->emDoc.edit_undo_stack, pDoc);
	pDoc->pNextUndoMission = StructClone(parse_MissionDef, pDoc->pMission);

	return pDoc;
}


MissionEditDoc *MDEOpenMission(EMEditor *pEditor, char *pcName)
{
	MissionEditDoc *pDoc = NULL;
	MissionDef *pMission = NULL;
	bool bCreated = false;

	if (pcName && resIsEditingVersionAvailable(g_MissionDictionary, pcName)) {
		// Simply open the object since it is in the dictionary
		pMission = RefSystem_ReferentFromString(g_MissionDictionary, pcName);
	} else if (pcName) {
		// Wait for object to show up so we can open it
		resSetDictionaryEditMode(g_MissionDictionary, true);
		resSetDictionaryEditMode(gMessageDict, true);
		resSetDictionaryEditMode("PetContactList", true);
		resSetDictionaryEditMode("ChoiceTable", true);
		resDictRegisterEventCallback("ChoiceTable", MDEChoiceTableDictChanged, NULL);
		emSetResourceState(pEditor, pcName, EMRES_STATE_OPENING);
		resRequestOpenResource(g_MissionDictionary, pcName);
	} else {
		// Create a new object since it is not in the dictionary
		bCreated = true;
	}

	if (pMission && pMission->genesisZonemap) {
		ZoneMapInfo* zmapInfo = zmapInfoGetByPublicName(pMission->genesisZonemap);
		if (zmapInfo && zmapInfoHasGenesisData(zmapInfo) && !stricmp(getUserName(), "jfinder")) {
			Alertf( "Editing is not allowed on unfrozen Missions.");
			return NULL;
		}
	}

	if (pMission || bCreated) {
		pDoc = MDEInitDoc(pMission, bCreated);
		MDEInitDisplay(pEditor, pDoc);
		resFixFilename(g_MissionDictionary, pDoc->pMission->name, pDoc->pMission);
	}

	return pDoc;
}


void MDERevertMission(MissionEditDoc *pDoc)
{
	MissionDef *pMission;

	if (!pDoc->emDoc.orig_doc_name[0]) {
		// Cannot revert if no original
		return;
	}

	pMission = RefSystem_ReferentFromString(g_MissionDictionary, pDoc->emDoc.orig_doc_name);
	if (pMission) {
		// Revert the mission
		StructDestroy(parse_MissionDef, pDoc->pMission);
		StructDestroy(parse_MissionDef, pDoc->pOrigMission);
		pDoc->pMission = StructClone(parse_MissionDef, pMission);
		MDEMissionPostOpenFixup(pDoc->pMission, pDoc->pMission);
		pDoc->pOrigMission = StructClone(parse_MissionDef, pDoc->pMission);

		// Clear the undo stack on revert
		EditUndoStackClear(pDoc->emDoc.edit_undo_stack);
		StructDestroy(parse_MissionDef, pDoc->pNextUndoMission);
		pDoc->pNextUndoMission = StructClone(parse_MissionDef, pDoc->pMission);

		// Refresh the UI
		pDoc->bIgnoreFieldChanges = true;
		pDoc->bIgnoreFilenameChanges = true;
		MDEUpdateDisplay(pDoc);
		pDoc->bIgnoreFieldChanges = false;
		pDoc->bIgnoreFilenameChanges = false;
	} 
}


void MDECloseMission(MissionEditDoc *pDoc)
{
	int i;

	MDESavePrefs(pDoc);

	// Free mission groups
	MDEFreeMissionGroup(pDoc->pMainMissionGroup);
	for(i=eaSize(&pDoc->eaSubMissionGroups)-1; i>=0; --i) {
		MDEFreeMissionGroup(pDoc->eaSubMissionGroups[i]);
	}
	eaDestroy(&pDoc->eaSubMissionGroups);

	// Free contact groups
	for (i = eaSize(&pDoc->eaContactGroups)-1; i>=0; --i) 
	{
		MDEFreeContactGroup(pDoc->eaContactGroups[i]);
	}

	// Free the variable list
	eaDestroy(&pDoc->eaVarNames);

	// Free the objects
	StructDestroy(parse_MissionDef, pDoc->pMission);
	if (pDoc->pOrigMission) {
		StructDestroy(parse_MissionDef, pDoc->pOrigMission);
	}

	// Close the window
	ui_WindowHide(pDoc->emDoc.primary_ui_window);
	ui_WidgetQueueFree((UIWidget*)pDoc->emDoc.primary_ui_window);

	CEFreeDialogFlowWindow(pDoc->pDialogFlowWindowInfo);
}


EMTaskStatus MDESaveMission(MissionEditDoc* pDoc, bool bSaveAsNew)
{
	EMTaskStatus status;
	const char *pcName;
	MissionDef *pMissionCopy;

	MDESavePrefs(pDoc);

	// Deal with state changes
	pcName = pDoc->pMission->name;
	if (emHandleSaveResourceState(pDoc->emDoc.editor, pcName, &status)) {
		return status;
	}

	// Do cleanup before validation
	pMissionCopy = StructClone(parse_MissionDef, pDoc->pMission);
	MDEMissionPreSaveFixup(pMissionCopy);

	// Perform validation
	if (!missiondef_Validate(pMissionCopy, pMissionCopy, false)) {
		StructDestroy(parse_MissionDef, pMissionCopy);
		return EM_TASK_FAILED;
	}

	// Do the save (which will free the copy)
	status = emSmartSaveDoc(&pDoc->emDoc, pMissionCopy, pDoc->pOrigMission, bSaveAsNew);

	return status;
}

const MDESpecialDialogOverrideGroup * const MDEFindSpecialDialogOverrideGroupByName(SA_PARAM_NN_VALID MDEContactGroup *pContactGroup, SA_PARAM_OP_STR const char *pchSpecialDialogName)
{
	if (pchSpecialDialogName && pchSpecialDialogName[0])
	{
		FOR_EACH_IN_CONST_EARRAY_FORWARDS(pContactGroup->eaSpecialDialogOverrideGroups, MDESpecialDialogOverrideGroup, pGroup)
		{
			SpecialDialogBlock *pSpecialDialogBlock = (*pGroup->peaOverrides)[pGroup->index]->pSpecialDialog;
			if (stricmp_safe(pSpecialDialogBlock->name, pchSpecialDialogName) == 0)
			{
				return pGroup;
			}
		}
		FOR_EACH_END
	}

	return NULL;
}

const MDEMissionOfferOverrideGroup * const MDEFindMissionOfferOverrideGroupByOffer(SA_PARAM_NN_VALID MDEContactGroup *pContactGroup, SA_PARAM_NN_VALID ContactMissionOffer *pMissionOffer)
{
	if (pMissionOffer)
	{
		FOR_EACH_IN_CONST_EARRAY_FORWARDS(pContactGroup->eaMissionOfferOverrideGroups, MDEMissionOfferOverrideGroup, pGroup)
		{
			if (pGroup->pMissionOfferGroup &&
				pGroup->pMissionOfferGroup->pMissionField &&
				pGroup->pMissionOfferGroup->pMissionField->pNew == pMissionOffer)
			{
				return pGroup;
			}
		}
		FOR_EACH_END
	}

	return NULL;
}

#endif 
