/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef NO_EDITORS

#include "AnimEditorCommon.h"
#include "MoveTransitionEditor.h"
#include "dynAnimChart.h"
#include "dynMoveTransition.h"
#include "dynSkeletonMovement.h"
#include "FolderCache.h"

#include "error.h"
#include "EditLibUIUtil.h"
#include "winutil.h"
#include "Color.h"
#include "StringCache.h"
#include "UIGimmeButton.h"
#include "EditorPrefs.h"
#include "UISeparator.h"
#include "GfxClipper.h"
#include "GfxTexAtlas.h"
#include "GfxSprite.h"
#include "inputMouse.h"
#include "qsortG.h"
#include "file.h"

#include "MultiEditFieldContext.h"
#include "EditorManagerUIToolbars.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););


#define DEFAULT_DOC_NAME "New_MoveTransition"
#define MOVE_TRANSITION_CONTEXT "MoveTransContext"
#define MOVE_TRANSITION_PREBLEND_CONTEXT "MoveTransPreBlendContext"
#define MOVE_TRANSITION_POSTBLEND_CONTEXT "MoveTransPostBlendContext"

static int mteNewNameCount = 0;

static bool gInitializedEditor = false;
static bool gInitializedEditorData = false;
static bool gIndexChanged = false;
static bool gUINeedsRefresh = false;

static char** geaScopes = NULL;

static UISkin* gBoldExpanderSkin;
UISkin mteBadDataButton;
UISkin mteBadDataBackground;
UISkin mteUnsavedDataButton;
static const Color mteUnsavedDataColor = { 192, 128, 128, 255 };
static const Color mteBadDataColor[2] = {
	{ 255, 000, 000, 255 },	
	{ 255, 255, 000, 255 },
};

static void RefreshUI(MoveTransitionDoc *pDoc, bool bUndoable);



//---------------------------------------------------------------------------------------------------
// Searching
//---------------------------------------------------------------------------------------------------

static EMPanel* MTEInitSearchPanel(MoveTransitionDoc* pDoc);

static void MTERefreshSearchPanel(UIButton *pButton, UserData uData)
{
	MoveTransitionDoc *pDoc = (MoveTransitionDoc*)emGetActiveEditorDoc();

	if (pDoc->pSearchPanel)
	{
		eaFindAndRemove(&pDoc->emDoc.em_panels, pDoc->pSearchPanel);
		emPanelFree(pDoc->pSearchPanel);
	}
	pDoc->pSearchPanel = MTEInitSearchPanel(pDoc);
	eaPush(&pDoc->emDoc.em_panels, pDoc->pSearchPanel);
	emRefreshDocumentUI();
}

static void MTESearchTextChanged(UITextEntry *pEntry, UserData uData)
{
	if (pEntry) {
		MoveTransitionDoc *pDoc = (MoveTransitionDoc*)emGetActiveEditorDoc();
		AnimEditor_SearchText = allocAddString(ui_TextEntryGetText(pEntry));
		if (!strlen(AnimEditor_SearchText))
			AnimEditor_SearchText = NULL;
		MTERefreshSearchPanel(NULL, uData);
	}
}

static EMPanel* MTEInitSearchPanel(MoveTransitionDoc* pDoc)
{
	EMPanel* pPanel;
	F32 y = 0.0;

	// Create the panel
	pPanel = emPanelCreate("Search", "Search", 0);

	y += AnimEditor_Search(	pDoc,
							pPanel,
							AnimEditor_SearchText,
							MTESearchTextChanged,
							MTERefreshSearchPanel
							);

	emPanelSetHeight(pPanel, y);

	emRefreshDocumentUI();

	return pPanel;
}


// +-------------+
// | HISTORY CBs |
// +-------------+

static void MTEUndoCB(MoveTransitionDoc *pDoc, MTEUndoData *pData)
{
	if (pDoc->pObject->pcName != pDoc->pNextUndoObject->pcName) {
		sprintf(pDoc->emDoc.doc_name, "%s", pDoc->pObject->pcName);
		sprintf(pDoc->emDoc.doc_display_name, "%s", pDoc->pObject->pcName);
		pDoc->emDoc.name_changed = 1;
	}

	StructDestroy(parse_DynMoveTransition, pDoc->pObject);
	pDoc->pObject = StructCloneFields(parse_DynMoveTransition, pData->pPreObject);
	if (pDoc->pNextUndoObject) StructDestroy(parse_DynMoveTransition, pDoc->pNextUndoObject);
	pDoc->pNextUndoObject = StructCloneFields(parse_DynMoveTransition, pDoc->pObject);

	RefreshUI(pDoc, false);
}

static void MTERedoCB(MoveTransitionDoc *pDoc, MTEUndoData *pData)
{
	StructDestroy(parse_DynMoveTransition, pDoc->pObject);
	pDoc->pObject = StructCloneFields(parse_DynMoveTransition, pData->pPostObject);
	if (pDoc->pNextUndoObject) StructDestroy(parse_DynMoveTransition, pDoc->pNextUndoObject);
	pDoc->pNextUndoObject= StructCloneFields(parse_DynMoveTransition, pDoc->pObject);

	if (pDoc->pObject->pcName != pDoc->pNextUndoObject->pcName) {
		sprintf(pDoc->emDoc.doc_name, "%s", pDoc->pObject->pcName);
		sprintf(pDoc->emDoc.doc_display_name, "%s", pDoc->pObject->pcName);
		pDoc->emDoc.name_changed = 1;
	}

	RefreshUI(pDoc, false);
}

static void MTEUndoFreeCB(MoveTransitionDoc *pDoc, MTEUndoData *pData)
{
	StructDestroy(parse_DynMoveTransition, pData->pPreObject);
	StructDestroy(parse_DynMoveTransition, pData->pPostObject);
	free(pData);
}

// +-----------------+
// | UI CB FUNCTIONS |
// +-----------------+

void MTEDuplicateDoc(UIButton* button, UserData uData)
{
	MoveTransitionDoc *pDoc = (MoveTransitionDoc *)emGetActiveEditorDoc();
	emNewDoc("MoveTransition", pDoc->pObject);
}

static void MTEContextChangedCB(MEField *pField, bool bFinished, UserData pData)
{
	MoveTransitionDoc *pDoc = (MoveTransitionDoc*)pData;
	bool nameChanged, scopeChanged;

	//check for changes in the data
	nameChanged  = pDoc->pObject->pcName  != pDoc->pNextUndoObject->pcName;
	scopeChanged = pDoc->pObject->pcScope != pDoc->pNextUndoObject->pcScope;

	//handle file name changes
	if (nameChanged || scopeChanged) {
		resFixFilename(hMoveTransitionDict, pDoc->pObject->pcName, pDoc->pObject);
	}

	//show the document with any updates
	RefreshUI(pDoc, true);
}

static void MTEOpenChartButtonCB(UIButton *button, void *pInfo)
{
	MoveTransitionDoc *pDoc = (MoveTransitionDoc*)emGetActiveEditorDoc();
	if (pDoc)
	{
		if (GET_REF(pDoc->pObject->hMove))
			emOpenFileEx(GET_REF(pDoc->pObject->hChart)->pcName, ANIM_CHART_EDITED_DICTIONARY);
	}
}

static bool MTEChooseChartCallback(EMPicker *picker, EMPickerSelection **selections, DynMoveTransition *pData)
{
	MoveTransitionDoc *pDoc = (MoveTransitionDoc*)emGetActiveEditorDoc();

	if (!eaSize(&selections))
		return false;

	SET_HANDLE_FROM_STRING(hAnimChartDictLoadTime, selections[0]->doc_name, pData->hChart);

	RefreshUI(pDoc, true);

	return true;
}

static void MTEChooseChartButtonCB(UIButton *button, void *pInfo)
{
	MoveTransitionDoc *pDoc = (MoveTransitionDoc*)emGetActiveEditorDoc();
	if (pDoc) {
		EMPicker *pMovePicker = emPickerGetByName("Animation Chart Library");
		if (pMovePicker)
			emPickerShow(pMovePicker, NULL, false, MTEChooseChartCallback, pDoc->pObject);
	}
}

static void MTERemoveChartButtonCB(UIButton *button, void *pInfo)
{
	MoveTransitionDoc *pDoc = (MoveTransitionDoc*)emGetActiveEditorDoc();
	if (pDoc) {
		REMOVE_HANDLE(pDoc->pObject->hChart);
		RefreshUI(pDoc, true);
	}
}

static void MTEOpenMoveButtonCB(UIButton *button, void *pInfo)
{
	MoveTransitionDoc *pDoc = (MoveTransitionDoc*)emGetActiveEditorDoc();
	if (pDoc)
	{
		if (GET_REF(pDoc->pObject->hMove))
			emOpenFileEx(GET_REF(pDoc->pObject->hMove)->pcName, DYNMOVE_TYPENAME);
	}
}

static bool MTEChooseMoveCallback(EMPicker *picker, EMPickerSelection **selections, DynMoveTransition *pData)
{
	MoveTransitionDoc *pDoc = (MoveTransitionDoc*)emGetActiveEditorDoc();

	if (!eaSize(&selections))
		return false;

	SET_HANDLE_FROM_STRING(DYNMOVE_DICTNAME, selections[0]->doc_name, pData->hMove);

	RefreshUI(pDoc, true);

	return true;
}

static void MTEChooseMoveButtonCB(UIButton *button, void *pInfo)
{
	MoveTransitionDoc *pDoc = (MoveTransitionDoc*)emGetActiveEditorDoc();
	if (pDoc) {
		EMPicker *pMovePicker = emPickerGetByName("Move Library");
		if (pMovePicker)
			emPickerShow(pMovePicker, NULL, false, MTEChooseMoveCallback, pDoc->pObject);
	}
}

static void MTERemoveMoveButtonCB(UIButton *button, void *pInfo)
{
	MoveTransitionDoc *pDoc = (MoveTransitionDoc*)emGetActiveEditorDoc();
	if (pDoc) {
		REMOVE_HANDLE(pDoc->pObject->hMove);
		RefreshUI(pDoc, true);
	}
}

static void MTEAddSrcMovementTypeCB(UIButton *button, void *pInfo)
{
	MoveTransitionDoc *pDoc = (MoveTransitionDoc*)emGetActiveEditorDoc();
	if (pDoc && !MEContextExists())
	{
		eaPush(&pDoc->pObject->eaMovementTypesSource, "");
		RefreshUI(pDoc, true);
	}
}

static void MTERemoveSrcMovementTypeCB(UIButton *button, void *pInfo)
{
	MoveTransitionDoc *pDoc = (MoveTransitionDoc*)emGetActiveEditorDoc();
	if (pDoc && !MEContextExists())
	{
		eaRemove(&pDoc->pObject->eaMovementTypesSource, PTR_TO_U32(pInfo));
		RefreshUI(pDoc, true);
	}
}

static void MTEAddSrcStanceCB(UIButton *button, void *pInfo)
{
	MoveTransitionDoc *pDoc = (MoveTransitionDoc*)emGetActiveEditorDoc();
	if (pDoc && !MEContextExists())
	{
		eaPush(&pDoc->pObject->eaStanceWordsSource, "");
		RefreshUI(pDoc, true);
	}
}

static void MTERemoveSrcStanceCB(UIButton *button, void *pInfo)
{
	MoveTransitionDoc *pDoc = (MoveTransitionDoc*)emGetActiveEditorDoc();
	if (pDoc && !MEContextExists())
	{
		eaRemove(&pDoc->pObject->eaStanceWordsSource, PTR_TO_U32(pInfo));
		RefreshUI(pDoc, true);
	}
}

static void MTEAddSrcTimedStanceCB(UIButton *button, void *pInfo)
{
	MoveTransitionDoc *pDoc = (MoveTransitionDoc*)emGetActiveEditorDoc();
	if (pDoc && !MEContextExists())
	{
		DynAnimTimedStance *pTimedStance = StructCreate(parse_DynAnimTimedStance);
		pTimedStance->pcName = StructAllocString("");
		pTimedStance->fTime = 0.0f;
		eaPush(&pDoc->pObject->eaTimedStancesSource, pTimedStance);
		RefreshUI(pDoc, true);
	}
}

static void MTERemoveSrcTimedStanceCB(UIButton *button, void *pInfo)
{
	MoveTransitionDoc *pDoc = (MoveTransitionDoc*)emGetActiveEditorDoc();
	if (pDoc && !MEContextExists())
	{
		DynAnimTimedStance *pTimedStance = eaRemove(&pDoc->pObject->eaTimedStancesSource, PTR_TO_U32(pInfo));
		StructDestroy(parse_DynAnimTimedStance, pTimedStance);
		RefreshUI(pDoc, true);
	}
}

static void MTEAddTgtMovementTypeCB(UIButton *button, void *pInfo)
{
	MoveTransitionDoc *pDoc = (MoveTransitionDoc*)emGetActiveEditorDoc();
	if (pDoc && !MEContextExists())
	{
		eaPush(&pDoc->pObject->eaMovementTypesTarget, "");
		RefreshUI(pDoc, true);
	}
}

static void MTERemoveTgtMovementTypeCB(UIButton *button, void *pInfo)
{
	MoveTransitionDoc *pDoc = (MoveTransitionDoc*)emGetActiveEditorDoc();
	if (pDoc && !MEContextExists())
	{
		eaRemove(&pDoc->pObject->eaMovementTypesTarget, PTR_TO_U32(pInfo));
		RefreshUI(pDoc, true);
	}
}

static void MTEAddTgtTimedStanceCB(UIButton *button, void *pInfo)
{
	MoveTransitionDoc *pDoc = (MoveTransitionDoc*)emGetActiveEditorDoc();
	if (pDoc && !MEContextExists())
	{
		DynAnimTimedStance *pTimedStance = StructCreate(parse_DynAnimTimedStance);
		pTimedStance->pcName = StructAllocString("");
		pTimedStance->fTime = 0.0f;
		eaPush(&pDoc->pObject->eaTimedStancesTarget, pTimedStance);
		RefreshUI(pDoc, true);
	}
}

static void MTERemoveTgtTimedStanceCB(UIButton *button, void *pInfo)
{
	MoveTransitionDoc *pDoc = (MoveTransitionDoc*)emGetActiveEditorDoc();
	if (pDoc && !MEContextExists())
	{
		DynAnimTimedStance *pTimedStance = eaRemove(&pDoc->pObject->eaTimedStancesTarget, PTR_TO_U32(pInfo));
		StructDestroy(parse_DynAnimTimedStance, pTimedStance);
		RefreshUI(pDoc, true);
	}
}

static void MTEAddTgtStanceCB(UIButton *button, void *pInfo)
{
	MoveTransitionDoc *pDoc = (MoveTransitionDoc*)emGetActiveEditorDoc();
	if (pDoc && !MEContextExists())
	{
		eaPush(&pDoc->pObject->eaStanceWordsTarget, "");
		RefreshUI(pDoc, true);
	}
}

static void MTERemoveTgtStanceCB(UIButton *button, void *pInfo)
{
	MoveTransitionDoc *pDoc = (MoveTransitionDoc*)emGetActiveEditorDoc();
	if (pDoc && !MEContextExists())
	{
		eaRemove(&pDoc->pObject->eaStanceWordsTarget, PTR_TO_U32(pInfo));
		RefreshUI(pDoc, true);
	}
}


// +----------------------+
// | UI REFRESH FUNCTIONS |
// +----------------------+

static void RefreshMEContext(MoveTransitionDoc *pDoc)
{
	static const char **eaMovementTypes = NULL;
	MEFieldContext *pContext = MEContextPush(MOVE_TRANSITION_CONTEXT, pDoc->pOrigObject, pDoc->pObject, parse_DynMoveTransition);
	int prevStart = pContext->iXDataStart;
	pContext->pUIContainer = emPanelGetUIContainer(pDoc->pMTPanel);
	pContext->cbChanged = MTEContextChangedCB;
	pContext->pChangedData = pDoc;

	//buildup the movement types
	eaClear(&eaMovementTypes);
	{
		DynAnimChartLoadTime *pChart = GET_REF(pDoc->pObject->hChart);
		if (pChart) {
			DynMovementSet *pMovementSet = GET_REF(pChart->hMovementSet);
			if (pMovementSet) {
				EARRAY_CONST_FOREACH_BEGIN(pMovementSet->eaMovementSequences, i, iSize);
				{
					eaPush(&eaMovementTypes, pMovementSet->eaMovementSequences[i]->pcMovementType);
				}
				EARRAY_FOREACH_END;
			}
		}
	}

	//line 1
	MEContextAddSpacer();
	MEContextAddSeparator("Line1");
	MEContextAddSpacer();

	//file info
	MEContextAddLabel("FileInfoLabel", "File Info:", "Information about the file");
	MEContextAddSimple(kMEFieldType_TextEntry, "Name",     "Name",     "Name of the transition");
	MEContextAddSimple(kMEFieldType_TextEntry, "Scope",    "Scope",    "Storage location of the transition");
	MEContextAddSimple(kMEFieldType_MultiText, "Comments", "Comments", "Notes about the transition");
	MEContextAddSpacer();

	//line 2
	MEContextAddSpacer();
	MEContextAddSeparator("Line2");
	MEContextAddSpacer();

	//chart label
	MEContextAddLabel("ChartLabel", "Chart:", "Chart the transition belongs to");

	//chart data
	if (GET_REF(pDoc->pObject->hChart)) {
		MEFieldContextEntry *mefChooseButton;
		pContext->iXDataStart = 0;
		mefChooseButton = MEContextAddButton(REF_HANDLE_GET_STRING(pDoc->pObject->hChart), NULL, MTEChooseChartButtonCB, NULL, "ChooseChartButton", NULL, "Choose the chart the transition belongs to");
		MEContextEntryAddActionButton(mefChooseButton, "X",    NULL, MTERemoveChartButtonCB, NULL, -1, "Unlink the chart");
		MEContextEntryAddActionButton(mefChooseButton, "Open", NULL, MTEOpenChartButtonCB,   NULL, -1, "Open the linked chart");
		pContext->iXDataStart = prevStart;
	}else {
		MEFieldContextEntry *mefAddButton;
		mefAddButton = MEContextAddButton("Add", NULL, MTEChooseChartButtonCB, NULL, "ChooseChartButton", NULL, "Choose the chart the transition belongs to");
		MEContextEntryAddActionButton(mefAddButton, "X", NULL, MTERemoveChartButtonCB, NULL, -1, "Unlink the move");
	}

	//line 3
	MEContextAddSpacer();
	MEContextAddSeparator("Line3");
	MEContextAddSpacer();

	//forced data
	pContext->iXDataStart = 60;
	MEContextAddSimple(kMEFieldType_Check, "Forced", "Forced: ", "When enabled the transition occurs solely due to stance change & will not end until its move is over");

	//line 4
	MEContextAddSpacer();
	MEContextAddSeparator("Line4");
	MEContextAddSpacer();

	//src label
	MEContextAddLabel("SrcLabel", "Source:", "Required before the transition");

	//src move types
	pContext->iXDataStart = 100;
	if (!eaSize(&pDoc->pObject->eaMovementTypesSource)) MEContextAddButton("Any", NULL, NULL, NULL, "SrcMoveTypeData", "Movement Type", "Movement type required to trigger transition");
	FOR_EACH_IN_EARRAY_FORWARDS(pDoc->pObject->eaMovementTypesSource, const char, pchSrcMoveType)
	{
		MEFieldContextEntry *mefSelectButton;
		mefSelectButton = MEContextAddListIdx(kMEFieldType_Combo, &eaMovementTypes, "MovementTypeSource", ipchSrcMoveTypeIndex, "Movement Type", "Movement type required to trigger transition");
		MEContextEntryAddActionButton(mefSelectButton, "X", NULL, MTERemoveSrcMovementTypeCB, U32_TO_PTR(ipchSrcMoveTypeIndex), -1, "Remove movement type requirement");
	}
	FOR_EACH_END;
	MEContextAddButton("Add", NULL, MTEAddSrcMovementTypeCB, NULL, "AddSrcMovementTypeButton", NULL, "Add a movement type required to trigger transition");
	pContext->iXDataStart = prevStart;
	MEContextAddSpacer();
	
	//src stances
	pContext->iXDataStart = 100;
	if (!eaSize(&pDoc->pObject->eaStanceWordsSource)) MEContextAddButton("Any", NULL, NULL, NULL, "SrcStanceData", "Stance", "Stance words required to trigger transition");
	FOR_EACH_IN_EARRAY_FORWARDS(pDoc->pObject->eaStanceWordsSource, const char, pchSrcStance)
	{
		MEFieldContextEntry *mefSelectButton;
		mefSelectButton = MEContextAddDictIdx(kMEFieldType_ValidatedTextEntry, STANCE_DICTIONARY, "StanceWordSource", ipchSrcStanceIndex, "Stance", "Stance words required to trigger transition");
		MEContextEntryAddActionButton(mefSelectButton, "X", NULL, MTERemoveSrcStanceCB, U32_TO_PTR(ipchSrcStanceIndex), -1, "Remove stance word requirement");
	}
	FOR_EACH_END;
	MEContextAddButton("Add", NULL, MTEAddSrcStanceCB, NULL, "AddSrcStanceButton", NULL, "Add a stance word required to trigger transition");
	pContext->iXDataStart = prevStart;

	MEContextAddSpacer();

	//src timed stances
	pContext->iXDataStart = 100;
	if (!eaSize(&pDoc->pObject->eaTimedStancesSource)) MEContextAddButton("Any", NULL, NULL, NULL, "SrcTimedStanceData", "Timed Stance", "Timed stance words required to trigger transition (time specifies minimum requirement)");
	FOR_EACH_IN_EARRAY_FORWARDS(pDoc->pObject->eaTimedStancesSource, DynAnimTimedStance, pSrcTimedStance)
	{
		MEFieldContext *tsContext;
		char tsLoopUID[MAX_PATH], tsXButtonUID[MAX_PATH], tsLoopId[5];
		MEFieldContextEntry *pContextEntry;

		itoa(ipSrcTimedStanceIndex, tsLoopId, 10);
		strcpy(tsLoopUID, MOVE_TRANSITION_CONTEXT);
		strcat(tsLoopUID, "TimedSrcStance");
		strcat(tsLoopUID, tsLoopId);
		strcpy(tsXButtonUID, tsLoopUID);
		strcat(tsXButtonUID, "X");

		tsContext = MEContextPush(tsLoopUID,
			pDoc->pOrigObject && eaSize(&pDoc->pOrigObject->eaTimedStancesSource) > ipSrcTimedStanceIndex ? pDoc->pOrigObject->eaTimedStancesSource[ipSrcTimedStanceIndex] : NULL,
			pDoc->pObject->eaTimedStancesSource[ipSrcTimedStanceIndex],
			parse_DynAnimTimedStance);
		
		pContextEntry = MEContextAddButton("X", NULL, MTERemoveSrcTimedStanceCB, U32_TO_PTR(ipSrcTimedStanceIndex), tsXButtonUID, "", "Remove timed stance word requirement");
		ui_WidgetSetWidthEx(UI_WIDGET(pContextEntry->pButton), 17, UIUnitFixed);
		ui_WidgetSetPositionEx(UI_WIDGET(pContextEntry->pButton), 0, UI_WIDGET(pContextEntry->pButton)->y, 0, 0, UITopRight);

		MEContextStepBackUp();
		pContextEntry = MEContextAddSimple(kMEFieldType_TextEntry, "Time", NULL, "Minimum time requirement for stance to have been currently active on the character's skeleton");
		ui_WidgetSetWidthEx(pContextEntry->ppFields[0]->pUIWidget, 180, UIUnitFixed);
		ui_WidgetSetPositionEx(pContextEntry->ppFields[0]->pUIWidget, -85, pContextEntry->ppFields[0]->pUIWidget->y, 0, 0, UITopRight);

		MEContextStepBackUp();
		pContextEntry = MEContextAddDict(kMEFieldType_ValidatedTextEntry, STANCE_DICTIONARY, "Name", "Timed Stance", "Timed stance words required to trigger transition (time specifies minimum requirement)");
		ui_WidgetSetWidthEx(pContextEntry->ppFields[0]->pUIWidget, 0.7, UIUnitPercentage);
		ui_WidgetSetPositionEx(pContextEntry->ppFields[0]->pUIWidget, 0, pContextEntry->ppFields[0]->pUIWidget->y, 0, 0, UITopLeft);

		MEContextPop(tsLoopUID);
	}
	FOR_EACH_END;
	MEContextAddButton("Add", NULL, MTEAddSrcTimedStanceCB, NULL, "AddSrcTimedStanceButton", NULL, "Add a timed stance word required to trigger transition");
	pContext->iXDataStart = prevStart;

	//line 5
	MEContextAddSpacer();
	MEContextAddSeparator("Line5");
	MEContextAddSpacer();

	//target label
	MEContextAddLabel("TgtLabel", "Target:", "Required data after transition ends");

	//target move types
	pContext->iXDataStart = 100;
	if (!eaSize(&pDoc->pObject->eaMovementTypesTarget)) MEContextAddButton("Any", NULL, NULL, NULL, "TgtMoveTypeData", "Movement Type", "Movement type required after transition ends");
	FOR_EACH_IN_EARRAY_FORWARDS(pDoc->pObject->eaMovementTypesTarget, const char, pchTgtMoveType)
	{
		MEFieldContextEntry *mefSelectButton;
		mefSelectButton = MEContextAddListIdx(kMEFieldType_Combo, &eaMovementTypes, "MovementTypeTarget", ipchTgtMoveTypeIndex, "Movement Type", "Movement type after transition ends");
		MEContextEntryAddActionButton(mefSelectButton, "X", NULL, MTERemoveTgtMovementTypeCB, U32_TO_PTR(ipchTgtMoveTypeIndex), -1, "Remove stance word requirement");
	}
	FOR_EACH_END;
	MEContextAddButton("Add", NULL, MTEAddTgtMovementTypeCB, NULL, "AddTgtMovementTypeButton", NULL, "Add a movement type required after transition ends");
	pContext->iXDataStart = prevStart;

	MEContextAddSpacer();

	//target stances
	pContext->iXDataStart = 100;
	if (!eaSize(&pDoc->pObject->eaStanceWordsTarget)) MEContextAddButton("Any", NULL, NULL, NULL, "TgtStanceData", "Stance", "Stance words required after transition ends");
	FOR_EACH_IN_EARRAY_FORWARDS(pDoc->pObject->eaStanceWordsTarget, const char, pchTgtStance)
	{
		MEFieldContextEntry *mefSelectButton;
		mefSelectButton = MEContextAddDictIdx(kMEFieldType_ValidatedTextEntry, STANCE_DICTIONARY, "StanceWordTarget", ipchTgtStanceIndex, "Stance", "Stance words required after transition ends");
		MEContextEntryAddActionButton(mefSelectButton, "X", NULL, MTERemoveTgtStanceCB, U32_TO_PTR(ipchTgtStanceIndex), -1, "Remove stance word requirement");
	}
	FOR_EACH_END;
	MEContextAddButton("Add", NULL, MTEAddTgtStanceCB, NULL, "AddTgtStanceButton", NULL, "Add a stance word required after transition ends");
	pContext->iXDataStart = prevStart;

	MEContextAddSpacer();

	//src timed stances
	pContext->iXDataStart = 100;
	if (!eaSize(&pDoc->pObject->eaTimedStancesTarget)) MEContextAddButton("Any", NULL, NULL, NULL, "TgtTimedStanceData", "Timed Stance", "Timed stance words required after transition ends (time specifies minimum requirement)");
	FOR_EACH_IN_EARRAY_FORWARDS(pDoc->pObject->eaTimedStancesTarget, DynAnimTimedStance, pTgtTimedStance)
	{
		MEFieldContext *ttContext;
		char ttLoopUID[MAX_PATH], ttXButtonUID[MAX_PATH], ttLoopId[5];
		MEFieldContextEntry *pContextEntry;

		itoa(ipTgtTimedStanceIndex, ttLoopId, 10);
		strcpy(ttLoopUID, MOVE_TRANSITION_CONTEXT);
		strcat(ttLoopUID, "TimedTgtStance");
		strcat(ttLoopUID, ttLoopId);
		strcpy(ttXButtonUID, ttLoopUID);
		strcat(ttXButtonUID, "X");

		ttContext = MEContextPush(ttLoopUID,
			pDoc->pOrigObject && eaSize(&pDoc->pOrigObject->eaTimedStancesTarget) > ipTgtTimedStanceIndex ? pDoc->pOrigObject->eaTimedStancesTarget[ipTgtTimedStanceIndex] : NULL,
			pDoc->pObject->eaTimedStancesTarget[ipTgtTimedStanceIndex],
			parse_DynAnimTimedStance);

		pContextEntry = MEContextAddButton("X", NULL, MTERemoveTgtTimedStanceCB, U32_TO_PTR(ipTgtTimedStanceIndex), ttXButtonUID, "", "Remove timed stance word requirement");
		ui_WidgetSetWidthEx(UI_WIDGET(pContextEntry->pButton), 17, UIUnitFixed);
		ui_WidgetSetPositionEx(UI_WIDGET(pContextEntry->pButton), 0, UI_WIDGET(pContextEntry->pButton)->y, 0, 0, UITopRight);

		MEContextStepBackUp();
		pContextEntry = MEContextAddSimple(kMEFieldType_TextEntry, "Time", NULL, "Minimum time requirement for stance to have been currently active on the character's skeleton");
		ui_WidgetSetWidthEx(pContextEntry->ppFields[0]->pUIWidget, 180, UIUnitFixed);
		ui_WidgetSetPositionEx(pContextEntry->ppFields[0]->pUIWidget, -85, pContextEntry->ppFields[0]->pUIWidget->y, 0, 0, UITopRight);

		MEContextStepBackUp();
		pContextEntry = MEContextAddDict(kMEFieldType_ValidatedTextEntry, STANCE_DICTIONARY, "Name", "Timed Stance", "Timed stance words required after the transition ends (time specifies minimum requirement)");
		ui_WidgetSetWidthEx(pContextEntry->ppFields[0]->pUIWidget, 0.7, UIUnitPercentage);
		ui_WidgetSetPositionEx(pContextEntry->ppFields[0]->pUIWidget, 0, pContextEntry->ppFields[0]->pUIWidget->y, 0, 0, UITopLeft);

		MEContextPop(ttLoopUID);
	}
	FOR_EACH_END;
	MEContextAddButton("Add", NULL, MTEAddTgtTimedStanceCB, NULL, "AddTgtTimedStanceButton", NULL, "Add a timed stance word required after transition ends");
	pContext->iXDataStart = prevStart;

	//line 6
	MEContextAddSpacer();
	MEContextAddSeparator("Line6");
	MEContextAddSpacer();

	//move label
	MEContextAddLabel("MoveLabel", "Move: ", "Animation to play during the transition");

	//move data
	if (GET_REF(pDoc->pObject->hMove)) {
		MEFieldContextEntry *mefChooseButton;
		pContext->iXDataStart = 0;
		mefChooseButton = MEContextAddButton(REF_HANDLE_GET_STRING(pDoc->pObject->hMove), NULL, MTEChooseMoveButtonCB, NULL, "ChooseMoveButton", NULL, "Choose the transition's move");
		MEContextEntryAddActionButton(mefChooseButton, "X",    NULL, MTERemoveMoveButtonCB, NULL, -1, "Unlink the move");
		MEContextEntryAddActionButton(mefChooseButton, "Open", NULL, MTEOpenMoveButtonCB,   NULL, -1, "Open the linked move");
		pContext->iXDataStart = prevStart;
	}else {
		MEFieldContextEntry *mefAddButton;
		mefAddButton = MEContextAddButton("Add", NULL, MTEChooseMoveButtonCB, NULL, "ChooseMoveButton", NULL, "Choose the transition's move");
		MEContextEntryAddActionButton(mefAddButton, "X", NULL, MTERemoveMoveButtonCB, NULL, -1, "Unlink the move");
	}
	
	//line 7
	MEContextAddSpacer();
	MEContextAddSeparator("Line7");
	MEContextAddSpacer();

	//pre interpolation label
	MEContextAddLabel("InterpolationLabelPre", "Enter Blend: ", "Interpolation data used when entering the transition");

	//interpolation data
	pContext->iXDataStart = 100;
	MEContextPush(MOVE_TRANSITION_PREBLEND_CONTEXT, pDoc->pOrigObject ? &pDoc->pOrigObject->interpBlockPre : NULL, &pDoc->pObject->interpBlockPre, parse_DynAnimInterpolation);
		MEContextAddSimple(kMEFieldType_TextEntry, "interpolation", "Frames", "Number of frames to blend over");//MEContextAddMinMax(kMEFieldType_SliderText, 0, 10, 1, "interpolation", "Frames", "Number of frames to blend over");
		MEContextAddEnum(kMEFieldType_Combo, eEaseTypeEnum, "EaseIn", "Curve-In", "Function to smooth ease in blending");
		MEContextAddEnum(kMEFieldType_Combo, eEaseTypeEnum, "EaseOut", "Curve-Out", "Function to smooth ease out blending");
	MEContextPop(MOVE_TRANSITION_PREBLEND_CONTEXT);
	pContext->iXDataStart = 145;
	{
		MEFieldContextEntry *mefEntry;
		mefEntry = MEContextAddSimple(kMEFieldType_Check, "BlendLowerBodyFromGraph", "Lower Body from Graph", "Sets the blend data to also control the relevant graph/movement blend factor(s)");
		FOR_EACH_IN_CONST_EARRAY(mefEntry->ppFields, MEField, pField) {
			ui_SetActive(UI_WIDGET(pField->pUICheck), false);
		} FOR_EACH_END;
		mefEntry = MEContextAddSimple(kMEFieldType_Check, "BlendWholeBodyFromGraph", "Whole Body from Graph", "Sets the blend data to also control the relevant graph/movement blend factor(s)");
		FOR_EACH_IN_CONST_EARRAY(mefEntry->ppFields, MEField, pField) {
			ui_SetActive(UI_WIDGET(pField->pUICheck), false);
		} FOR_EACH_END;
	}

	MEContextAddSpacer();

	//post interpolation label
	MEContextAddLabel("InterpolationLabelPost", "Exit Blend: ", "Interpolation data used when entering the transition");

	//interpolation data
	pContext->iXDataStart = 100;
	MEContextPush(MOVE_TRANSITION_POSTBLEND_CONTEXT, pDoc->pOrigObject ? &pDoc->pOrigObject->interpBlockPost : NULL, &pDoc->pObject->interpBlockPost, parse_DynAnimInterpolation);
		MEContextAddSimple(kMEFieldType_TextEntry, "interpolation", "Frames", "Number of frames to blend over");//MEContextAddMinMax(kMEFieldType_SliderText, 0, 10, 1, "interpolation", "Frames", "Number of frames to blend over");
		MEContextAddEnum(kMEFieldType_Combo, eEaseTypeEnum, "EaseIn", "Ease In", "Function to smooth ease in blending");
		MEContextAddEnum(kMEFieldType_Combo, eEaseTypeEnum, "EaseOut", "Ease Out", "Function to smooth ease out blending");
	MEContextPop(MOVE_TRANSITION_POSTBLEND_CONTEXT);
	pContext->iXDataStart = 145;
	{
		MEFieldContextEntry *mefEntry;
		mefEntry = MEContextAddSimple(kMEFieldType_Check, "BlendLowerBodyToGraph", "Lower Body To Graph", "Sets the blend data to also control the relevant graph/movement blend factor(s)");
		FOR_EACH_IN_CONST_EARRAY(mefEntry->ppFields, MEField, pField) {
			ui_SetActive(UI_WIDGET(pField->pUICheck), false);
		} FOR_EACH_END;
		mefEntry = MEContextAddSimple(kMEFieldType_Check, "BlendWholeBodyToGraph", "Whole Body To Graph", "Sets the blend data to also control the relevant graph/movement blend factor(s)");
		FOR_EACH_IN_CONST_EARRAY(mefEntry->ppFields, MEField, pField) {
			ui_SetActive(UI_WIDGET(pField->pUICheck), false);
		} FOR_EACH_END;
	}
	
	//line 8
	MEContextAddSpacer();
	MEContextAddSeparator("Line8");
	MEContextAddSpacer();

	MEContextPop(MOVE_TRANSITION_CONTEXT);

	emPanelSetHeight(pDoc->pMTPanel, pContext->iYPos);
}

static void RefreshUI(MoveTransitionDoc *pDoc, bool bUndoable)
{
	gUINeedsRefresh = true;

	if (bUndoable && StructCompare(parse_DynMoveTransition, pDoc->pObject, pDoc->pNextUndoObject, 0, 0, 0)) {
		MTEUndoData* pData = calloc(1, sizeof(MTEUndoData));
		pData->pPreObject = pDoc->pNextUndoObject;
		pData->pPostObject = StructCloneFields(parse_DynMoveTransition, pDoc->pObject);
		EditCreateUndoCustom(pDoc->emDoc.edit_undo_stack, MTEUndoCB, MTERedoCB, MTEUndoFreeCB, pData);
		pDoc->pNextUndoObject = StructCloneFields(parse_DynMoveTransition, pDoc->pObject);
	}
}

// +--------------------+
// | EM FUNC: GOT FOCUS |
// +--------------------+

void MTEGotFocus(MoveTransitionDoc *pDoc)
{
	RefreshUI(pDoc, false);
}

// +---------------------+
// | EM FUNC: LOST FOCUS |
// +---------------------+

void MTELostFocus(MoveTransitionDoc *pDoc)
{
	;
}

// +-------------------------+
// | EM FUNC: ONCE PER FRAME |
// +-------------------------+

void MTEOncePerFrame(MoveTransitionDoc *pDoc)
{
	//make bad data colors flash
	{
		float fR = sin( timeGetTime() / 250.0 ) / 2 + 0.5;
		ui_SkinSetBackground( &mteBadDataBackground, ColorLerp(mteBadDataColor[ 0 ], mteBadDataColor[ 1 ], fR));
		ui_SkinSetButton( &mteBadDataButton, ColorLerp(mteBadDataColor[ 1 ], mteBadDataColor[ 0 ], fR));
		ui_SkinSetEntry( &mteBadDataButton, ColorLerp(mteBadDataColor[ 1 ], mteBadDataColor[ 0 ], fR));
	}

	if (!ui_ExpanderIsOpened(emPanelGetExpander(pDoc->pMTPanel)))
		emPanelSetOpened(pDoc->pMTPanel, true);

	if (!ui_ExpanderIsOpened(emPanelGetExpander(pDoc->pSearchPanel)))
		emPanelSetOpened(pDoc->pSearchPanel, true);

	//refresh the UI
	if (gUINeedsRefresh)
	{
		if (pDoc->pSearchPanel)
		{
			eaFindAndRemove(&pDoc->emDoc.em_panels, pDoc->pSearchPanel);
			emPanelFree(pDoc->pSearchPanel);
		}
		pDoc->pSearchPanel = MTEInitSearchPanel(pDoc);
		eaPush(&pDoc->emDoc.em_panels, pDoc->pSearchPanel);

		RefreshMEContext(pDoc);
		pDoc->emDoc.saved = pDoc->pOrigObject && (StructCompare(parse_DynMoveTransition, pDoc->pOrigObject, pDoc->pObject, 0, 0, 0) == 0);
		sprintf(pDoc->emDoc.doc_name, "%s", pDoc->pObject->pcName);
		sprintf(pDoc->emDoc.doc_display_name, "%s", pDoc->pObject->pcName);
		pDoc->emDoc.name_changed = 1;
		gUINeedsRefresh = false;
	}
}

// +--------------------+
// | EM FUNC: INIT DATA |
// +--------------------+

#define BUTTON_SPACING 3.0f
#define ADD_BUTTON( text, callback, callbackdata ) \
	pButton = ui_ButtonCreate(text, fX, 0, callback, callbackdata); \
	pButton->widget.widthUnit = UIUnitFitContents; \
	emToolbarAddChild(pToolbar, pButton, false); \
	fX += ui_WidgetGetWidth(UI_WIDGET(pButton)) + BUTTON_SPACING; \

static void MTEInitToolbarsAndMenus(EMEditor* pEditor)
{
	EMToolbar *pToolbar;
	UIButton *pButton;
	F32 fX;

	// Toolbar
	pToolbar = emToolbarCreateFileToolbar(EM_FILE_TOOLBAR_NEW|EM_FILE_TOOLBAR_OPEN|EM_FILE_TOOLBAR_SAVE);
	fX = emToolbarGetPaneWidget(pToolbar)->width;
	ADD_BUTTON("Duplicate", MTEDuplicateDoc, NULL);
	emToolbarSetWidth(pToolbar, fX);
	eaPush(&pEditor->toolbars, pToolbar);
	eaPush(&pEditor->toolbars, emToolbarCreateWindowToolbar());

	// File menu
	emMenuItemCreate(pEditor, "mte_reverttransition", "Revert", NULL, NULL, "MTE_RevertTransition");
	emMenuRegister(pEditor, emMenuCreate(pEditor, "File", "mte_reverttransition", NULL));
}

static void MTEIndexChangedCB(void* unused)
{
	if (gIndexChanged) {
		gIndexChanged = false;
		resGetUniqueScopes(hMoveTransitionDict, &geaScopes);
	}
}

static void MTEContentDictChanged(enumResourceEventType eType, const char* pDictName, const char* pcName, Referent pReferent, void* pUserData)
{
	if ((eType == RESEVENT_INDEX_MODIFIED) && !gIndexChanged) {
		gIndexChanged = true;
		emQueueFunctionCall(MTEIndexChangedCB, NULL);
	}
}

static void MTEStanceChanged(const char *relpath, int when)
{
	EMEditorDoc *pEditorDoc = emGetActiveEditorDoc();
	if (pEditorDoc && stricmp(pEditorDoc->doc_type, MOVE_TRANSITION_EDITED_DICTIONARY)==0) {
		RefreshUI((MoveTransitionDoc*)pEditorDoc, false);
	}
}

void MTEInitData(EMEditor *pEditor)
{
	if (pEditor && !gInitializedEditor)
	{
		gBoldExpanderSkin = ui_SkinCreate(NULL);
		SET_HANDLE_FROM_STRING(g_ui_FontDict, "Default_Bold", gBoldExpanderSkin->hNormal);

		ui_SkinCopy(&mteBadDataBackground,	NULL);
		ui_SkinCopy(&mteBadDataButton,		NULL);
		ui_SkinCopy(&mteUnsavedDataButton,	NULL);
		ui_SkinSetButton(&mteUnsavedDataButton, mteUnsavedDataColor);

		MTEInitToolbarsAndMenus(pEditor);

		emAutoHandleDictionaryStateChange(pEditor, MOVE_TRANSITION_EDITED_DICTIONARY, true, NULL, NULL, NULL, NULL, NULL);

		resGetUniqueScopes(hMoveTransitionDict, &geaScopes);

		gInitializedEditor = true;
	}

	if (!gInitializedEditorData)
	{
		resDictRegisterEventCallback(hMoveTransitionDict, MTEContentDictChanged, NULL);
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "dyn/AnimStance/*.AStance", MTEStanceChanged);

		gInitializedEditorData = true;
	}
}

// +---------------+
// | EM FUNC: OPEN |
// +---------------+

static MoveTransitionDoc* MTEInitDoc(DynMoveTransition *pMoveTransition, bool bCreated, bool bEmbedded)
{
	MoveTransitionDoc *pDoc;
	char nameBuf[260];

	// Initialize the structure
	pDoc = (MoveTransitionDoc*)calloc(1,sizeof(MoveTransitionDoc));

	// Fill in the def data
	if (bCreated && pMoveTransition)
	{
		pDoc->pObject = StructClone(parse_DynMoveTransition, pMoveTransition);
		assert(pDoc->pObject);
		sprintf(pDoc->emDoc.doc_name,"%s_Dup%d",pMoveTransition->pcName,++mteNewNameCount);
		pDoc->pObject->pcName = StructAllocString(pDoc->emDoc.doc_name);
		sprintf(nameBuf, "dyn/movetransition/%s/%s.movetrans", pDoc->pObject->pcScope, pDoc->pObject->pcName);
		pDoc->pObject->pcFilename = allocAddFilename(nameBuf);
	}
	else if (bCreated)
	{
		pDoc->pObject = StructCreate(parse_DynMoveTransition);
		assert(pDoc->pObject);
		emMakeUniqueDocName(&pDoc->emDoc, DEFAULT_DOC_NAME, MOVE_TRANSITION_EDITED_DICTIONARY, MOVE_TRANSITION_EDITED_DICTIONARY);
		pDoc->pObject->pcName = StructAllocString(pDoc->emDoc.doc_name);
		sprintf(nameBuf, "dyn/movetransition/%s.movetrans", pDoc->pObject->pcName);
		pDoc->pObject->pcFilename = allocAddFilename(nameBuf);
	}
	else
	{
		pDoc->pObject = StructCloneFields(parse_DynMoveTransition, pMoveTransition);
		assert(pDoc->pObject);
		pDoc->pOrigObject = StructCloneFields(parse_DynMoveTransition, pDoc->pObject);
		emDocAssocFile(&pDoc->emDoc, pDoc->pObject->pcFilename);
	}

	// Set up the undo stack
	pDoc->emDoc.edit_undo_stack = EditUndoStackCreate();
	EditUndoSetContext(pDoc->emDoc.edit_undo_stack, pDoc);
	pDoc->pNextUndoObject = StructCloneFields(parse_DynMoveTransition, pDoc->pObject);

	return pDoc;
}

static void MTEInitDisplay(EMEditor *pEditor, MoveTransitionDoc *pDoc)
{
	eaDestroyEx(&pDoc->emDoc.em_panels, emPanelFree); // Free panel array for rebuilding
	pDoc->emDoc.em_panels = NULL;

	{
		EMPanel *pPanel = emPanelCreate("Move Transition", "Move Transition", 0);
		emPanelSetOpened(pPanel, true);
		eaPush(&pDoc->emDoc.em_panels, pPanel);
		pDoc->pMTPanel = pPanel;
	}

	{
		EMPanel *pPanel = MTEInitSearchPanel(pDoc);
		emPanelSetOpened(pPanel, true);
		eaPush(&pDoc->emDoc.em_panels, pPanel);
		pDoc->pSearchPanel = pPanel;
	}

	RefreshUI(pDoc, false);
	emRefreshDocumentUI();
}

MoveTransitionDoc* MTEOpenMoveTransition(EMEditor *pEditor, char *pcName, DynMoveTransition *pMoveTransitionIn)
{
	MoveTransitionDoc *pDoc = NULL;
	DynMoveTransition *pMoveTransition = NULL;
	bool bCreated = false;

	if (pMoveTransitionIn)
	{
		pMoveTransition = pMoveTransitionIn;
		bCreated = true;
	}
	else if (pcName && resIsEditingVersionAvailable(hMoveTransitionDict, pcName)){
		pMoveTransition = RefSystem_ReferentFromString(hMoveTransitionDict, pcName);
	} else if (pcName) {
		resSetDictionaryEditMode(hMoveTransitionDict, true);
		emSetResourceState(pEditor, pcName, EMRES_STATE_OPENING);
		resRequestOpenResource(hMoveTransitionDict, pcName);
	} else {
		bCreated = true;
	}

	if (pMoveTransition || bCreated) {
		pDoc = MTEInitDoc(pMoveTransition, bCreated, false);
		MTEInitDisplay(pEditor, pDoc);
		resFixFilename(hMoveTransitionDict, pDoc->pObject->pcName, pDoc->pObject);
	}

	return pDoc;
}

// +-----------------+
// | EM FUNC: REVERT |
// +-----------------+

static void MTEDeleteOldDirectoryIfEmpty(MoveTransitionDoc *pDoc)
{
	char dir[MAX_PATH], out_dir[MAX_PATH];
	char cmd[MAX_PATH];

	sprintf(dir, "/dyn/movetransition/%s", NULL_TO_EMPTY(pDoc->pOrigObject->pcScope));
	fileLocateWrite(dir, out_dir);
	if (dirExists(out_dir))
	{
		backSlashes(out_dir);
		sprintf(cmd, "rd %s", out_dir);
		system(cmd);
	}
}

void MTERevertMoveTransition(MoveTransitionDoc *pDoc)
{
	DynMoveTransition *pMoveTransition;

	if (!pDoc->emDoc.orig_doc_name[0]) {
		return;
	}

	//if we're reverting due to save, remove the old directory if it's empty post scope change
	if (pDoc->pOrigObject && pDoc->pObject->pcScope != pDoc->pOrigObject->pcScope) {
		MTEDeleteOldDirectoryIfEmpty(pDoc);
	}

	pMoveTransition = RefSystem_ReferentFromString(hMoveTransitionDict, pDoc->emDoc.orig_doc_name);
	if (pMoveTransition) {
		// Revert the def
		StructDestroy(parse_DynMoveTransition, pDoc->pObject);
		StructDestroy(parse_DynMoveTransition, pDoc->pOrigObject);
		pDoc->pObject = StructCloneFields(parse_DynMoveTransition, pMoveTransition);
		pDoc->pOrigObject = StructCloneFields(parse_DynMoveTransition, pDoc->pObject);

		// Clear the undo stack on revert
		EditUndoStackClear(pDoc->emDoc.edit_undo_stack);
		StructDestroy(parse_DynMoveTransition, pDoc->pNextUndoObject);
		pDoc->pNextUndoObject = StructCloneFields(parse_DynMoveTransition, pDoc->pObject);

		// Refresh the UI
		RefreshUI(pDoc, false);
	} 
}

// +----------------+
// | EM FUNC: CLOSE |
// +----------------+

void MTECloseMoveTransition(MoveTransitionDoc *pDoc)
{	
	//Free the objects
	StructDestroy(parse_DynMoveTransition, pDoc->pObject);
	if (pDoc->pOrigObject) StructDestroy(parse_DynMoveTransition, pDoc->pOrigObject);
	StructDestroy(parse_DynMoveTransition, pDoc->pNextUndoObject);

	//free the ui elements
	MEContextDestroyByName(MOVE_TRANSITION_CONTEXT);
	eaDestroyEx(&pDoc->emDoc.em_panels, emPanelFree); // Free panel array for rebuilding
	pDoc->emDoc.em_panels = NULL;
	pDoc->pMTPanel = NULL;
}

// +---------------+
// | EM FUNC: SAVE |
// +---------------+

EMTaskStatus MTESaveMoveTransition(MoveTransitionDoc *pDoc, bool bSaveAsNew)
{
	EMTaskStatus status;
	const char *pcName;
	DynMoveTransition *pMoveTransitionCopy;

	// Deal with state changes
	pcName = pDoc->pObject->pcName;
	if (emHandleSaveResourceState(pDoc->emDoc.editor, pcName, &status)) {
		return status;
	}

	if (strnicmp(pcName, DEFAULT_DOC_NAME, strlen(DEFAULT_DOC_NAME))==0)
	{
		Errorf("Must choose a name besides %s", DEFAULT_DOC_NAME);
		return EM_TASK_FAILED;
	}

	// Do cleanup before validation
	pMoveTransitionCopy = StructCloneFields(parse_DynMoveTransition, pDoc->pObject);

	// Perform validation
	if (!dynMoveTransitionVerify(pMoveTransitionCopy)) {
		StructDestroy(parse_DynMoveTransition, pMoveTransitionCopy);
		return EM_TASK_FAILED;
	}

	// Do the save (which will free the copy)
	status = emSmartSaveDoc(&pDoc->emDoc, pMoveTransitionCopy, pDoc->pOrigObject, bSaveAsNew);
	emDocRemoveAllFiles(&pDoc->emDoc, false);
	emDocAssocFile(&pDoc->emDoc, pDoc->pObject->pcFilename);

	return status;
}

#endif
