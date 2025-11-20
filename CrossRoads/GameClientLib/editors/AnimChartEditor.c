/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef NO_EDITORS

#include "AnimChartEditor.h"
#include "dynAnimChart.h"
#include "dynAnimGraph.h"
#include "dynMoveTransition.h"
#include "dynSkeletonMovement.h"
#include "FolderCache.h"

#include "error.h"
#include "EditLibUIUtil.h"
#include "EditorManagerUIToolbars.h"
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
#include "GfxFont.h"


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

#define DEFAULT_DOC_NAME "New_AnimChart"

#define MAX_CHAR_BUFFER 1024

//-----------------------------------------------------------------------------------
// Global Data
//-----------------------------------------------------------------------------------

static int aceNewNameCount = 0;

static bool gInitializedEditor = false;
static bool gInitializedEditorData = false;
static bool gIndexChanged = false;

static char** geaScopes = NULL;
static char** s_eaClassNames = NULL;
static char** s_eaClassNames2 = NULL;
static char** s_eaCategoryNames = NULL;

extern EMEditor* s_InteractionEditor;
static AnimChartDoc* gEmbeddedDoc = NULL;

static UISkin* gBoldExpanderSkin;

UISkin aceBadDataButton;
UISkin aceBadDataBackground;
UISkin aceUnsavedDataButton;

static UISkin aceHighlightSkin;
static const char *aceHighlightText;


#define ACE_INDENT_UI_LEFT 25
#define ACE_MATRIX_UI_LEFT 270
#define REF_COL_SIZE 270

#define ACE_NO_STANCE_STRING "Basic"

static const Color aceUnsavedDataColor = { 192, 128, 128, 255 };

static const Color aceBadDataColor[2] = {
	{ 255, 000, 000, 255 },	
	{ 255, 255, 000, 255 },
};

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

#define ACE_IDX_TO_PTR(idx1, idx2) U32_TO_PTR((U32)((idx1)*1000+(idx2)))
#define ACE_PTR_TO_IDX(idx1, idx2, ptr) (idx1) = ((U32)PTR_TO_U32(ptr)); (idx2) = (idx1)%1000; (idx1) = ((U32)(idx1)/1000);

#define ACE_IDX3_TO_PTR(idx1, idx2, idx3) U32_TO_PTR((U32)((idx1)+(idx2)*1000+(idx3)*1000000))
#define ACE_PTR_TO_IDX3(idx1, idx2, idx3, ptr)	(idx1) =  ((U32)PTR_TO_U32(ptr));				\
												(idx3) =  (U32)((idx1)/1000000);				\
												(idx1) -= (U32)((idx3)*1000000);				\
												(idx2) =  (U32)((idx1)/1000);					\
												(idx1) -= (U32)((idx2)*1000);					
												

static void ACEFieldChangedCB(MEField* pField, bool bFinished, AnimChartDoc* pDoc);
static bool ACEFieldPreChangeCB(MEField* pField, bool bFinished, AnimChartDoc* pDoc);
static void ACEAnimChartChanged(AnimChartDoc* pDoc, bool bUndoable);
static void ACEChartPreSaveFixup(DynAnimChartLoadTime* pChart);
static void ACEUpdateDisplay(AnimChartDoc* pDoc);
static void ACERefreshPropsExpander(AnimChartDoc* pDoc);
static void ACERefreshStanceWordsExpander(AnimChartDoc* pDoc);
static void ACERefreshGraphsExpander(AnimChartDoc* pDoc);
static void ACERefreshSubChartExpander(AnimChartDoc* pDoc);
static void ACERefreshStanceChartsExpander(AnimChartDoc* pDoc);
static void ACERefreshMovesExpander(AnimChartDoc* pDoc);
static void ACERefreshMoveTransitionsExpander(AnimChartDoc* pDoc);
static void ACEOpenRandomGraphWindow(UIButton *pUnused, void* pFakePtr);
static void ACEOpenRandomMoveWindow(UIButton *pUnused, void* pFakePtr);
static void ACEOpenMoveTransitionWindow(UIButton *pUnused, void* pFakePtr);
static void ACEOpenGraph(UIWidget* pWidget, void* pFakePtr);
static void ACEGraphAddAdditional(UIWidget* pWidget, void* pFakePtr);
static void ACECreateGraphEntry(F32 local_x, F32 local_y, const char *pcButtonText, const char *pcToolTip, bool bHighlight, bool bInvalid, UIActivationFunc graphF, UIActivationFunc openF, UIActivationFunc addF, UIActivationFunc removeF, void *pUserData, UIAddWidgetFunc addChildF, UIAnyWidget *pParent);
static void ACEMoveAddAdditional(UIWidget* pWidget, void* pFakePtr);
static void ACEClearRandMove(UIWidget* pWidget, void* pFakePtr);
static void ACEOpenRandMove(UIWidget* pWidget, void* pFakePtr);
static void ACEChooseMove(UIWidget* pWidget, void* pFakePtr);

void ACEGraphRefFieldDestroy(ACEGraphRefField* pField)
{
	ui_WidgetQueueFree(UI_WIDGET(pField->pEntry));
	free(pField);
}

//---------------------------------------------------------------------------------------------------
// Searching
//---------------------------------------------------------------------------------------------------

static EMPanel *ACEInitSearchPanel(AnimChartDoc*);

static void ACERefreshSearchPanel(UIButton *pButton, UserData uData)
{
	AnimChartDoc *pDoc = (AnimChartDoc*)emGetActiveEditorDoc();

	if (pDoc->pSearchPanel)
	{
		eaFindAndRemove(&pDoc->emDoc.em_panels, pDoc->pSearchPanel);
		emPanelFree(pDoc->pSearchPanel);
	}
	pDoc->pSearchPanel = ACEInitSearchPanel(pDoc);
	eaPush(&pDoc->emDoc.em_panels, pDoc->pSearchPanel);
	emRefreshDocumentUI();
}

static void ACESearchTextChanged(UITextEntry *pEntry, UserData uData)
{
	if (pEntry) {
		AnimChartDoc *pDoc = (AnimChartDoc*)emGetActiveEditorDoc();
		AnimEditor_SearchText = allocAddString(ui_TextEntryGetText(pEntry));
		if (!strlen(AnimEditor_SearchText))
			AnimEditor_SearchText = NULL;
		ACERefreshSearchPanel(NULL, uData);
	}
}


//---------------------------------------------------------------------------------------------------
// Data Manipulation
//---------------------------------------------------------------------------------------------------

static void ACEAnimChartUndoCB(AnimChartDoc* pDoc, ACEUndoData* pData)
{
	if (pDoc->pRandomGraphWindow ||
		pDoc->pRandomMoveWindow  ||
		pDoc->pMoveTransitionWindow) {
			return;
	}

	// Put the undo def into the editor
	StructDestroy(parse_DynAnimChartLoadTime, pDoc->pObject);
	pDoc->pObject = StructCloneFields(parse_DynAnimChartLoadTime, pData->pPreObject);
	if (pDoc->pNextUndoObject) {
		StructDestroy(parse_DynAnimChartLoadTime, pDoc->pNextUndoObject);
	}
	pDoc->pNextUndoObject = StructCloneFields(parse_DynAnimChartLoadTime, pDoc->pObject);

	// Update the UI
	ACEAnimChartChanged(pDoc, false);
}


static void ACEAnimChartRedoCB(AnimChartDoc* pDoc, ACEUndoData* pData)
{
	if (pDoc->pRandomGraphWindow ||
		pDoc->pRandomMoveWindow  ||
		pDoc->pMoveTransitionWindow) {
			return;
	}

	// Put the undo def into the editor
	StructDestroy(parse_DynAnimChartLoadTime, pDoc->pObject);
	pDoc->pObject = StructCloneFields(parse_DynAnimChartLoadTime, pData->pPostObject);
	if (pDoc->pNextUndoObject) {
		StructDestroy(parse_DynAnimChartLoadTime, pDoc->pNextUndoObject);
	}
	pDoc->pNextUndoObject= StructCloneFields(parse_DynAnimChartLoadTime, pDoc->pObject);

	// Update the UI
	ACEAnimChartChanged(pDoc, false);
}


static void ACEAnimChartUndoFreeCB(AnimChartDoc* pDoc, ACEUndoData* pData)
{
	// Free the memory
	StructDestroy(parse_DynAnimChartLoadTime, pData->pPreObject);
	StructDestroy(parse_DynAnimChartLoadTime, pData->pPostObject);
	free(pData);
}


static void ACEIndexChangedCB(void* unused)
{
	if (gIndexChanged) {
		gIndexChanged = false;
		resGetUniqueScopes(hAnimChartDictLoadTime, &geaScopes);
	}
}


static void ACEContentDictChanged(enumResourceEventType eType, const char* pDictName, const char* pcName, Referent pReferent, void* pUserData)
{
	if ((eType == RESEVENT_INDEX_MODIFIED) && !gIndexChanged) {
		gIndexChanged = true;
		emQueueFunctionCall(ACEIndexChangedCB, NULL);
	}
}


//---------------------------------------------------------------------------------------------------
// Interaction Property Editing UI
//---------------------------------------------------------------------------------------------------


//---------------------------------------------------------------------------------------------------
// UI Logic
//---------------------------------------------------------------------------------------------------

static void ACEAddFieldToParent(MEField* pField, UIWidget* pParent, F32 x, F32 y, F32 xPercent, F32 w, UIUnitType wUnit, F32 padRight, AnimChartDoc* pDoc)
{
	MEFieldAddToParent(pField, pParent, x, y);
	ui_WidgetSetPositionEx(pField->pUIWidget, x, y, xPercent, 0, UITopLeft);
	ui_WidgetSetWidthEx(pField->pUIWidget, w, wUnit);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, padRight, 0, 0);
	MEFieldSetChangeCallback(pField, ACEFieldChangedCB, pDoc);
	MEFieldSetPreChangeCallback(pField, ACEFieldPreChangeCB, pDoc);
}


static UIExpander* ACECreateExpander(UIExpanderGroup* pExGroup, const char* pcName)
{
	UIExpander* pExpander = ui_ExpanderCreate(pcName, 0);
	ui_WidgetSkin(UI_WIDGET(pExpander), gBoldExpanderSkin);
	ui_ExpanderGroupAddExpander(pExGroup, pExpander);
	ui_ExpanderSetOpened(pExpander, 1);

	return pExpander;
}

static void ACERandomGraphWindowClose(WleUIRandomGraphWin *pUI)
{
	StructDestroySafe(parse_DynAnimChartLoadTime, &pUI->pPreOpenBackup);
	ui_WidgetQueueFreeAndNull( &pUI->pWindow );
	free(pUI);
}

static void ACERandomMoveWindowClose(WleUIRandomGraphWin *pUI)
{
	StructDestroySafe(parse_DynAnimChartLoadTime, &pUI->pPreOpenBackup);
	ui_WidgetQueueFreeAndNull( &pUI->pWindow );
	free(pUI);
}

// This is called whenever any def data changes to do cleanup
static void ACEAnimChartChanged(AnimChartDoc* pDoc, bool bUndoable)
{
	if (pDoc->pRandomGraphWindow) {
		DynAnimChartLoadTime *pPreOpenBackup = pDoc->pRandomGraphWindow->pPreOpenBackup;
		void *pFakePtr = pDoc->pRandomGraphWindow->pFakePtr;
		pDoc->pRandomGraphWindow->pPreOpenBackup = NULL;
		ACERandomGraphWindowClose(pDoc->pRandomGraphWindow);
		pDoc->pRandomGraphWindow = NULL;
		ACEOpenRandomGraphWindow(NULL, pFakePtr);
		StructDestroySafe(parse_DynAnimChartLoadTime, &pDoc->pRandomGraphWindow->pPreOpenBackup);
		pDoc->pRandomGraphWindow->pPreOpenBackup = pPreOpenBackup;
		return;
	}
	if (pDoc->pRandomMoveWindow) {
		DynAnimChartLoadTime *pPreOpenBackup = pDoc->pRandomMoveWindow->pPreOpenBackup;
		void *pFakePtr = pDoc->pRandomMoveWindow->pFakePtr;
		pDoc->pRandomMoveWindow->pPreOpenBackup = NULL;
		ACERandomMoveWindowClose(pDoc->pRandomMoveWindow);
		pDoc->pRandomMoveWindow = NULL;
		ACEOpenRandomMoveWindow(NULL, pFakePtr);
		StructDestroySafe(parse_DynAnimChartLoadTime, &pDoc->pRandomMoveWindow->pPreOpenBackup);
		pDoc->pRandomMoveWindow->pPreOpenBackup = pPreOpenBackup;
		return;
	}
	if (pDoc->pMoveTransitionWindow) {
		DynAnimChartLoadTime *pPreOpenBackup = pDoc->pMoveTransitionWindow->pPreOpenBackup;
		void *pFakePtr = pDoc->pMoveTransitionWindow->pFakePtr;
		pDoc->pMoveTransitionWindow->pPreOpenBackup = NULL;
		ACERandomGraphWindowClose(pDoc->pMoveTransitionWindow);
		pDoc->pMoveTransitionWindow = NULL;
		ACEOpenMoveTransitionWindow(NULL, pFakePtr);
		StructDestroySafe(parse_DynAnimChartLoadTime, &pDoc->pMoveTransitionWindow->pPreOpenBackup);
		pDoc->pMoveTransitionWindow->pPreOpenBackup = pPreOpenBackup;
		return;
	}
	if (!pDoc->bIgnoreFieldChanges) {
		ACEUpdateDisplay(pDoc);

		if (bUndoable) {
			ACEUndoData* pData = calloc(1, sizeof(ACEUndoData));
			pData->pPreObject = pDoc->pNextUndoObject;
			pData->pPostObject = StructCloneFields(parse_DynAnimChartLoadTime, pDoc->pObject);
			EditCreateUndoCustom(pDoc->emDoc.edit_undo_stack, ACEAnimChartUndoCB, ACEAnimChartRedoCB, ACEAnimChartUndoFreeCB, pData);
			pDoc->pNextUndoObject = StructCloneFields(parse_DynAnimChartLoadTime, pDoc->pObject);
		}
	}
}


// This is called by MEField prior to allowing an edit
static bool ACEFieldPreChangeCB(MEField* pField, bool bFinished, AnimChartDoc* pDoc)
{
	return true;
}

static void ACEIsSubchartCB(MEField* pField, bool bFinished, AnimChartDoc* pDoc)
{
	if(pDoc->pObject->bIsSubChart) {
		DynAnimChartLoadTime *pObject = pDoc->pObject;
		REF_HANDLE_REMOVE(pObject->hBaseChart);
		eaDestroyStruct(&pObject->eaMoveRefs, parse_DynAnimChartMoveRefLoadTime);
	}
	ACEAnimChartChanged(pDoc, bFinished);
}


// This is called when an MEField is changed
static void ACEFieldChangedCB(MEField* pField, bool bFinished, AnimChartDoc* pDoc)
{
	if(bFinished)
		ACEAnimChartChanged(pDoc, bFinished);
}

static void ACESetScopeCB(MEField* pField, bool bFinished, AnimChartDoc* pDoc)
{
	if(!bFinished)
		return;

	if (!pDoc->bIgnoreFilenameChanges) {
		// Update the filename appropriately
		resFixFilename(hAnimChartDictLoadTime, pDoc->pObject->pcName, pDoc->pObject);
	}

	// Call on to do regular updates
	ACEFieldChangedCB(pField, bFinished, pDoc);
}

static void ACESetNameCB(MEField* pField, bool bFinished, AnimChartDoc* pDoc)
{
	if(!bFinished)
		return;

	MEFieldFixupNameString(pField, &pDoc->pObject->pcName);

	// When the name changes, change the title of the window
	ui_WindowSetTitle(pDoc->pMainWindow, pDoc->pObject->pcName);

	// Make sure the browser picks up the new def name if the name changed
	sprintf(pDoc->emDoc.doc_name, "%s", pDoc->pObject->pcName);
	sprintf(pDoc->emDoc.doc_display_name, "%s", pDoc->pObject->pcName);
	pDoc->emDoc.name_changed = 1;

	// Call the scope function to avoid duplicating logic
	ACESetScopeCB(pField, bFinished, pDoc);
}

static void ACEFormatValidKeywords(DynAnimChartLoadTime *pObject)
{
	int idx = eaFind(&pObject->eaValidKeywords, allocAddString("Default"));
	if(idx >= 0) {
		eaInsert(&pObject->eaValidKeywords, eaRemove(&pObject->eaValidKeywords, idx), 0);;
	}
}

static S32 ACEGetValidStanceIndexFromKey(	const DynAnimChartLoadTime *pObject,
											const char* keyParam)
{
	const char**	eaTempStanceWords = NULL;
	char			key[1024];

	eaStackCreate(&eaTempStanceWords, 100);

	// Double-convert the key, just to be safe.

	dynAnimStanceWordsFromKey(keyParam, &eaTempStanceWords);
	strcpy(key, dynAnimKeyFromStanceWords(eaTempStanceWords));

	EARRAY_CONST_FOREACH_BEGIN(pObject->eaValidStances, i, isize);
	{
		eaClear(&eaTempStanceWords);
		dynAnimStanceWordsFromKey(pObject->eaValidStances[i], &eaTempStanceWords);

		if(!stricmp(key, dynAnimKeyFromStanceWords(eaTempStanceWords))){
			eaDestroy(&eaTempStanceWords);
			return i;
		}
	}
	EARRAY_FOREACH_END;

	eaDestroy(&eaTempStanceWords);
	return -1;
}

static S32 ACEGetValidStanceIndex(	const DynAnimChartLoadTime* pObject,
									const char*const* eaStanceWords)
{
	char key[1024];

	strcpy(key, dynAnimKeyFromStanceWords(eaStanceWords));

	return ACEGetValidStanceIndexFromKey(pObject, key);
}

static EMPanel* ACEInitSearchPanel(AnimChartDoc* pDoc)
{
	EMPanel* pPanel;
	F32 y = 0.0;

	// Create the panel
	pPanel = emPanelCreate("Search", "Search", 0);

	y += AnimEditor_Search(	pDoc,
								pPanel,
								AnimEditor_SearchText,
								ACESearchTextChanged,
								ACERefreshSearchPanel
								);

	emPanelSetHeight(pPanel, y);

	return pPanel;
}

static void ACEUpdateDisplay(AnimChartDoc* pDoc)
{
	int i;

	// Ignore changes while UI refreshes
	pDoc->bIgnoreFieldChanges = true;

	ACERefreshPropsExpander(pDoc);
	ACERefreshStanceWordsExpander(pDoc);
	ACERefreshGraphsExpander(pDoc);
	ACERefreshMovesExpander(pDoc);
	ACERefreshMoveTransitionsExpander(pDoc);
	ACERefreshSubChartExpander(pDoc);
	ACERefreshStanceChartsExpander(pDoc);

	/*
	if(pDoc->pStanceChartsExpander) {
		ui_ExpanderGroupRemoveExpander(pDoc->pExpanderGroup, pDoc->pStanceChartsExpander);
		ui_ExpanderGroupAddExpander(pDoc->pExpanderGroup, pDoc->pStanceChartsExpander);
	}
	*/

	{
		int iStanceCount = eaSize(&pDoc->pObject->eaValidStances);
		ui_ScrollAreaSetSize(pDoc->pMatrixExteriorScroll, REF_COL_SIZE*(iStanceCount+1), 100);
	}

	// Refresh doc-level fields
	for(i=eaSize(&pDoc->eaDocFields)-1; i>=0; --i) {
		MEFieldSetAndRefreshFromData(pDoc->eaDocFields[i], pDoc->pOrigObject, pDoc->pObject);
	}

	FOR_EACH_IN_EARRAY(pDoc->eaGraphRefFields, ACEGraphRefField, pField)
	{
		const char *pcOrigKeyword = pDoc->pOrigObject?eaGet(&pDoc->pOrigObject->eaValidKeywords, pField->iIndex):NULL;
		const char *pcKeyword = pDoc->pObject?eaGet(&pDoc->pObject->eaValidKeywords, pField->iIndex):NULL;
		assert(pcKeyword);
		ui_TextEntrySetText(pField->pEntry, pcKeyword);
		ui_SetChanged(UI_WIDGET(pField->pEntry), (!pcOrigKeyword || pcOrigKeyword != pcKeyword));
	}
	FOR_EACH_END;

	// Update non-field UI components
	ui_GimmeButtonSetName(pDoc->pFileButton, pDoc->pObject->pcName);
	ui_GimmeButtonSetReferent(pDoc->pFileButton, pDoc->pObject);
	ui_LabelSetText(pDoc->pFilenameLabel, pDoc->pObject->pcFilename);

	// Update saved flag
	pDoc->emDoc.saved = pDoc->pOrigObject && (StructCompare(parse_DynAnimChartLoadTime, pDoc->pOrigObject, pDoc->pObject, 0, 0, 0) == 0);

	// Start paying attention to changes again
	pDoc->bIgnoreFieldChanges = false;
}

static void ACEChooseGraphClosedCallback(EMPicker *picker, void* pFakePtr)
{
	AnimChartDoc* pDoc = (AnimChartDoc*)emGetActiveEditorDoc();
	DynAnimChartGraphRefLoadTime* pRef;
	U32 uiRefIndex, uiRandIdx;

	ACE_PTR_TO_IDX(uiRefIndex, uiRandIdx, pFakePtr);
	pRef = eaGet(&pDoc->pObject->eaGraphRefs, uiRefIndex);
	assert(pRef);

	if (eaSize(&pRef->eaGraphChances)==0) {
		eaFindAndRemove(&pDoc->pObject->eaGraphRefs, pRef);
		StructDestroy(parse_DynAnimChartGraphRefLoadTime, pRef);
	}
	ACEAnimChartChanged(pDoc, false);
}

static void ACEChooseMoveClosedCallback(EMPicker *picker, void* pFakePtr)
{
	AnimChartDoc* pDoc = (AnimChartDoc*)emGetActiveEditorDoc();
	DynAnimChartMoveRefLoadTime* pRef;
	U32 uiRefIndex, uiRandIdx;

	ACE_PTR_TO_IDX(uiRefIndex, uiRandIdx, pFakePtr);
	pRef = eaGet(&pDoc->pObject->eaMoveRefs, uiRefIndex);
	assert(pRef);

	if (eaSize(&pRef->eaMoveChances)==0) {
		eaFindAndRemove(&pDoc->pObject->eaMoveRefs, pRef);
		StructDestroy(parse_DynAnimChartMoveRefLoadTime, pRef);
	}
	ACEAnimChartChanged(pDoc, false);
}

static void ACENormalizeGraphChances(DynAnimChartGraphRefLoadTime *pRef)
{
	int j;
	int cnt = eaSize(&pRef->eaGraphChances);
	if(cnt == 1) {
		pRef->eaGraphChances[0]->fChance = 1.0f;
	} else if (cnt > 1){
		F32 sum = 0;
		for ( j=0; j < cnt; j++ ) {
			DynAnimGraphChanceRef *pChance = pRef->eaGraphChances[j];
			sum += pChance->fChance;
		}
		for ( j=0; j < cnt; j++ ) {
			DynAnimGraphChanceRef *pChance = pRef->eaGraphChances[j];
			pChance->fChance /= sum;
		}
	}
}

static void ACENormalizeMoveChances(DynAnimChartMoveRefLoadTime *pRef)
{
	int j;
	int cnt = eaSize(&pRef->eaMoveChances);
	if(cnt == 1) {
		pRef->eaMoveChances[0]->fChance = 1.0f;
	} else if (cnt > 1){
		F32 sum = 0;
		for ( j=0; j < cnt; j++ ) {
			DynAnimMoveChanceRef *pChance = pRef->eaMoveChances[j];
			sum += pChance->fChance;
		}
		for ( j=0; j < cnt; j++ ) {
			DynAnimMoveChanceRef *pChance = pRef->eaMoveChances[j];
			pChance->fChance /= sum;
		}
	}
}

static bool ACEChooseGraphCallback(EMPicker *picker, EMPickerSelection **selections, void* pFakePtr)
{
	AnimChartDoc* pDoc = (AnimChartDoc*)emGetActiveEditorDoc();
	DynAnimChartGraphRefLoadTime* pRef;
	DynAnimGraphChanceRef *pChance;
	U32 uiRefIndex, uiRandIdx;

	if (!eaSize(&selections))
		return false;

	ACE_PTR_TO_IDX(uiRefIndex, uiRandIdx, pFakePtr);
	pRef = eaGet(&pDoc->pObject->eaGraphRefs, uiRefIndex);
	assert(pRef);
	pChance = eaGet(&pRef->eaGraphChances, uiRandIdx);
	if(!pChance) {
		int cnt = eaSize(&pRef->eaGraphChances);
		pChance = StructCreate(parse_DynAnimGraphChanceRef);
		if(cnt>0)
			pChance->fChance = 1.0f/cnt;
		else 
			pChance->fChance = 1.0f;
		eaPush(&pRef->eaGraphChances, pChance);
		//We should only ever be requesting one more than the number we have
		pChance = eaGet(&pRef->eaGraphChances, uiRandIdx);
		ACENormalizeGraphChances(pRef);
	}
	assert(pChance);

	SET_HANDLE_FROM_STRING("AnimGraph", selections[0]->doc_name, pChance->hGraph);

	if (!pRef->pcKeyword)
	{
		int idx = 1;
		const char *pcNewKeyword = allocAddString(selections[0]->doc_name);
		while(eaFind(&pDoc->pObject->eaValidKeywords, pcNewKeyword) >= 0) {
			char pcNext[256];
			sprintf(pcNext, "%s_%d", selections[0]->doc_name, ++idx);
			pcNewKeyword = allocAddString(pcNext);
		}
		pRef->pcKeyword = pcNewKeyword;
		eaPush(&pDoc->pObject->eaValidKeywords, pcNewKeyword);
		ACEFormatValidKeywords(pDoc->pObject);
	}

	ACEAnimChartChanged(pDoc, true);
	return true;
}

static void ACEChooseGraph(UIWidget* pWidget, void* pFakePtr)
{
	AnimChartDoc* pDoc = (AnimChartDoc*)emGetActiveEditorDoc();
	if (pDoc) {
		EMPicker* pMovePicker = emPickerGetByName( "Animation Graph Library" );
		if (pMovePicker)
			emPickerShowEx(pMovePicker, NULL, false, ACEChooseGraphCallback, ACEChooseGraphClosedCallback, pFakePtr);
	}
}

static bool ACERandomGraphWindowCancel(UIWindow *pUnused, WleUIRandomGraphWin *pUI)
{
	if (pUI->pDoc->pRandomGraphWindow) {
		pUI->pDoc->pRandomGraphWindow = NULL;
	}
	else if (pUI->pDoc->pRandomMoveWindow) {
		pUI->pDoc->pRandomMoveWindow = NULL;
	}
	else if (pUI->pDoc->pMoveTransitionWindow) {
		pUI->pDoc->pMoveTransitionWindow = NULL;
	}

	StructDestroy(parse_DynAnimChartLoadTime, pUI->pDoc->pObject);
	pUI->pDoc->pObject = pUI->pPreOpenBackup;
	pUI->pPreOpenBackup = NULL;

	ACERandomGraphWindowClose(pUI);
	return true;
}

static bool ACERandomMoveWindowCancel(UIWindow *pUnused, WleUIRandomGraphWin *pUI)
{
	if (pUI->pDoc->pRandomGraphWindow) {
		pUI->pDoc->pRandomGraphWindow = NULL;
	}
	else if (pUI->pDoc->pRandomMoveWindow) {
		pUI->pDoc->pRandomMoveWindow = NULL;
	}
	else if (pUI->pDoc->pMoveTransitionWindow) {
		pUI->pDoc->pMoveTransitionWindow = NULL;
	}

	StructDestroy(parse_DynAnimChartLoadTime, pUI->pDoc->pObject);
	pUI->pDoc->pObject = pUI->pPreOpenBackup;
	pUI->pPreOpenBackup = NULL;

	ACERandomMoveWindowClose(pUI);
	return true;
}

static void ACERandomGraphWindowFinished(UIWindow *pUnused, WleUIRandomGraphWin *pUI)
{
	AnimChartDoc* pDoc = (AnimChartDoc*)emGetActiveEditorDoc();
	if (pDoc)
	{
		if (pUI->pDoc->pRandomGraphWindow) {
			U32 uiRefIndex, uiRandIdx;
			DynAnimChartGraphRefLoadTime *pRef;

			ACE_PTR_TO_IDX(uiRefIndex, uiRandIdx, pUI->pFakePtr);

			pRef = eaGet(&pDoc->pObject->eaGraphRefs, uiRefIndex);
			assert(pRef);

			pUI->pDoc->pRandomGraphWindow = NULL;
			ACENormalizeGraphChances(pRef);
			ACEAnimChartChanged(pDoc, true);
		}
		else if (pUI->pDoc->pRandomMoveWindow)
		{
			pUI->pDoc->pRandomMoveWindow = NULL;
		}
		else if (pUI->pDoc->pMoveTransitionWindow)
		{
			pUI->pDoc->pMoveTransitionWindow = NULL;
		}
		
		ACERandomGraphWindowClose(pUI);
	}
}

static void ACERandomMoveWindowFinished(UIWindow *pUnused, WleUIRandomGraphWin *pUI)
{
	AnimChartDoc* pDoc = (AnimChartDoc*)emGetActiveEditorDoc();
	if (pDoc)
	{
		if (pUI->pDoc->pRandomMoveWindow) {
			U32 uiRefIndex;
			DynAnimChartMoveRefLoadTime *pRef;

			uiRefIndex = PTR_TO_U32(pUI->pFakePtr);

			pRef = eaGet(&pDoc->pObject->eaMoveRefs, uiRefIndex);
			assert(pRef);

			pUI->pDoc->pRandomMoveWindow = NULL;
			ACENormalizeMoveChances(pRef);
			ACEAnimChartChanged(pDoc, true);
		}
		else if (pUI->pDoc->pRandomGraphWindow)
		{
			pUI->pDoc->pRandomGraphWindow = NULL;
		}
		else if (pUI->pDoc->pMoveTransitionWindow)
		{
			pUI->pDoc->pMoveTransitionWindow = NULL;
		}

		ACERandomMoveWindowClose(pUI);
	}
}

void ACEGotFocus(AnimChartDoc *pDoc)
{
	ACEUpdateDisplay(pDoc);
	ACERefreshSearchPanel(NULL, pDoc);
}

void ACELostFocus(AnimChartDoc* pDoc)
{
	if(pDoc->pRandomGraphWindow) {
		ACERandomGraphWindowCancel(NULL, pDoc->pRandomGraphWindow);
	}
	else if (pDoc->pRandomMoveWindow) {
		ACERandomMoveWindowCancel(NULL, pDoc->pRandomMoveWindow);
	}
	else if (pDoc->pMoveTransitionWindow) {
		ACERandomGraphWindowCancel(NULL, pDoc->pMoveTransitionWindow);
	}
}

static void ACERemoveRandGraph(UIWidget* pWidget, void* pFakePtr)
{
	AnimChartDoc* pDoc = (AnimChartDoc*)emGetActiveEditorDoc();
	if (pDoc) {
		U32 uiRefIndex, uiRandIdx;
		DynAnimChartGraphRefLoadTime *pRef;
		DynAnimGraphChanceRef *pChance;
		ACE_PTR_TO_IDX(uiRefIndex, uiRandIdx, pFakePtr);

		pRef = eaGet(&pDoc->pObject->eaGraphRefs, uiRefIndex);
		assert(pRef);
		pChance = eaRemove(&pRef->eaGraphChances, uiRandIdx);
		assert(pChance);
		StructDestroy(parse_DynAnimGraphChanceRef, pChance);

		ACEAnimChartChanged(pDoc, true);
	}
}

static void ACEGraphChanceChanged(UITextEntry* pEntry, void* pFakePtr)
{
	AnimChartDoc* pDoc = (AnimChartDoc*)emGetActiveEditorDoc();
	if (pDoc) {
		U32 uiRefIndex, uiRandIdx;
		DynAnimChartGraphRefLoadTime *pRef;
		DynAnimGraphChanceRef *pChance;
		ACE_PTR_TO_IDX(uiRefIndex, uiRandIdx, pFakePtr);

		pRef = eaGet(&pDoc->pObject->eaGraphRefs, uiRefIndex);
		assert(pRef);
		pChance = eaGet(&pRef->eaGraphChances, uiRandIdx);
		assert(pChance);
		pChance->fChance = atof(ui_TextEntryGetText(pEntry))/100.0f;

		ACEAnimChartChanged(pDoc, true);
	}
}

static void ACEMoveChanceChanged(UITextEntry* pEntry, void* pFakePtr)
{
	AnimChartDoc* pDoc = (AnimChartDoc*)emGetActiveEditorDoc();
	if (pDoc) {
		U32 uiRefIndex, uiRandIdx;
		DynAnimChartMoveRefLoadTime *pRef;
		DynAnimMoveChanceRef *pChance;
		ACE_PTR_TO_IDX(uiRefIndex, uiRandIdx, pFakePtr);

		pRef = eaGet(&pDoc->pObject->eaMoveRefs, uiRefIndex);
		assert(pRef);
		pChance = eaGet(&pRef->eaMoveChances, uiRandIdx);
		assert(pChance);
		pChance->fChance = atof(ui_TextEntryGetText(pEntry))/100.0f;

		ACEAnimChartChanged(pDoc, true);
	}
}

static void ACECreateGraphEntryRandWindow(DynAnimGraphChanceRef *pChance, F32 local_x, F32 local_y, bool bHighlight, void *pUserData, UIAddWidgetFunc addChildF, UIAnyWidget *pParent)
{
	const char *pcButtonText;
	bool bInvalid = false;

	if (pChance && GET_REF(pChance->hGraph)) {
		pcButtonText = REF_STRING_FROM_HANDLE(pChance->hGraph);
	} else {
		pcButtonText = "Invalid Graph";
		bInvalid = true;
	}
	
	ACECreateGraphEntry(local_x, local_y, pcButtonText, pcButtonText, bHighlight, bInvalid, 
		ACEChooseGraph, ACEOpenGraph, NULL, ACERemoveRandGraph, pUserData, addChildF, pParent);
}

static void ACECreateMoveEntryRandWindow(DynAnimMoveChanceRef *pChance, F32 local_x, F32 local_y, bool bHighlight, void *pUserData, UIAddWidgetFunc addChildF, UIAnyWidget *pParent, U32 bAllowDelete)
{
	const char *pcButtonText;
	bool bInvalid = false;
	UIButton *pButton;

	local_x += 10;

	if (pChance && GET_REF(pChance->hMove)) {
		pcButtonText = REF_STRING_FROM_HANDLE(pChance->hMove);
	} else {
		pcButtonText = "Invalid Move";
		bInvalid = true;
	}

	pButton = ui_ButtonCreate(pcButtonText, local_x, local_y, ACEChooseMove, pUserData);
	if (bHighlight)
		ui_WidgetSkin(UI_WIDGET(pButton), &aceHighlightSkin);
	else if (bInvalid)
		ui_WidgetSkin(UI_WIDGET(pButton), &aceBadDataButton);
	ui_WidgetSetWidth(UI_WIDGET(pButton), 190);
	local_x += 190+5;		
	addChildF(pParent, pButton);

	pButton = ui_ButtonCreate("Open", local_x, local_y, ACEOpenRandMove, pUserData); 
	ui_WidgetSetWidth(UI_WIDGET(pButton), 39);
	local_x += 39+5;
	addChildF(pParent, pButton);

	if (bAllowDelete) {
		pButton = ui_ButtonCreate("X", local_x, local_y, ACEClearRandMove, pUserData); 
		ui_WidgetSetWidth(UI_WIDGET(pButton), 16);
		local_x += 16+5;
		addChildF(pParent, pButton);
	}
}

static void ACEOpenRandomGraphWindow(UIButton *pUnused, void* pFakePtr)
{
	int i;
	U32 uiRefIndex, uiRandIdx;
	AnimChartDoc* pDoc = (AnimChartDoc*)emGetActiveEditorDoc();
	DynAnimChartGraphRefLoadTime* pRef;
	WleUIRandomGraphWin *pUI;
	UIWindow *pWindow;
	int y = 0;
	
	if (!pDoc->pRandomGraphWindow && !pDoc->pRandomMoveWindow && !pDoc->pMoveTransitionWindow)
	{
#define ACE_RAND_DATA_SIZE 100
		ACE_PTR_TO_IDX(uiRefIndex, uiRandIdx, pFakePtr);

		pRef = eaGet(&pDoc->pObject->eaGraphRefs, uiRefIndex);
		assert(pRef);

		pUI = calloc(1, sizeof(*pUI));
		pWindow = ui_WindowCreate("Random Move", 100, 100, REF_COL_SIZE+ACE_RAND_DATA_SIZE+5, 90);
		pUI->pWindow = pWindow;
		pUI->pDoc = pDoc;
		pUI->pPreOpenBackup = StructClone(parse_DynAnimChartLoadTime, pDoc->pObject);
		pDoc->pRandomGraphWindow = pUI;
		pUI->pFakePtr = pFakePtr;

		for ( i=0; i < eaSize(&pRef->eaGraphChances); i++ ) {
			DynAnimGraphChanceRef *pChance = pRef->eaGraphChances[i];
			bool bHighlight = false;

			if (GET_REF(pChance->hGraph) && aceHighlightText && strstri(REF_HANDLE_GET_STRING(pChance->hGraph),aceHighlightText))
				bHighlight = true;

			ACECreateGraphEntryRandWindow(pChance, 0, y, bHighlight, ACE_IDX_TO_PTR((U32)uiRefIndex, (U32)i), ui_WindowAddChild, pWindow);	

			{
				char buf[256];
				UITextEntry *pEntry;
				sprintf(buf, "%g%%", pChance->fChance*100.0f);
				pEntry = ui_TextEntryCreate(buf, REF_COL_SIZE+5, y);
				ui_TextEntrySetFinishedCallback(pEntry, ACEGraphChanceChanged, ACE_IDX_TO_PTR((U32)uiRefIndex, (U32)i));
				ui_WidgetSetWidth(UI_WIDGET(pEntry), ACE_RAND_DATA_SIZE);
				ui_WindowAddChild(pWindow, pEntry);
			}

			y += STANDARD_ROW_HEIGHT;
		}

		{
			UIButton *pButton = ui_ButtonCreate("Add", REF_COL_SIZE+5, y, ACEGraphAddAdditional, pFakePtr);
			ui_WidgetSetWidth(UI_WIDGET(pButton), ACE_RAND_DATA_SIZE);
			ui_WindowAddChild(pWindow, pButton);
			y += STANDARD_ROW_HEIGHT;
		}

		elUIAddCancelOkButtons(pWindow, ACERandomGraphWindowCancel, pUI, ACERandomGraphWindowFinished, pUI);
		ui_WindowSetCloseCallback(pWindow, ACERandomGraphWindowCancel, pUI);
		ui_WidgetSetHeight(UI_WIDGET(pWindow), y+STANDARD_ROW_HEIGHT*1.5f);
		elUICenterWindow(pWindow);
		ui_WindowSetModal(pWindow, true);
		ui_WindowSetMovable(pWindow, false);
		ui_WindowSetResizable(pWindow, false);
		ui_WindowShow(pWindow);
	}
}

static void ACEOpenRandomMoveWindow(UIButton *pUnused, void* pFakePtr)
{
	int i;
	U32 uiRefIndex;
	AnimChartDoc* pDoc = (AnimChartDoc*)emGetActiveEditorDoc();
	DynAnimChartMoveRefLoadTime* pRef;
	WleUIRandomGraphWin *pUI;
	UIWindow *pWindow;
	int y = 0;

	if (!pDoc->pRandomGraphWindow && !pDoc->pRandomMoveWindow && !pDoc->pMoveTransitionWindow)
	{
#define ACE_RAND_DATA_SIZE 100
		uiRefIndex = PTR_TO_U32(pFakePtr);

		pRef = eaGet(&pDoc->pObject->eaMoveRefs, uiRefIndex);
		assert(pRef);

		pUI = calloc(1, sizeof(*pUI));
		pWindow = ui_WindowCreate("Random Move", 100, 100, REF_COL_SIZE+ACE_RAND_DATA_SIZE+5, 90);
		pUI->pWindow = pWindow;
		pUI->pDoc = pDoc;
		pUI->pPreOpenBackup = StructClone(parse_DynAnimChartLoadTime, pDoc->pObject);
		pDoc->pRandomMoveWindow = pUI;
		pUI->pFakePtr = pFakePtr;

		for ( i=0; i < eaSize(&pRef->eaMoveChances); i++ ) {
			DynAnimMoveChanceRef *pChance = pRef->eaMoveChances[i];
			bool bHighlight = false;

			if (GET_REF(pChance->hMove) && aceHighlightText && strstri(REF_HANDLE_GET_STRING(pChance->hMove),aceHighlightText))
				bHighlight = true;

			ACECreateMoveEntryRandWindow(pChance, 0, y, bHighlight, ACE_IDX_TO_PTR((U32)uiRefIndex, (U32)i), ui_WindowAddChild, pWindow, eaSize(&pRef->eaMoveChances) > 1);	

			{
				char buf[256];
				UITextEntry *pEntry;
				sprintf(buf, "%g%%", pChance->fChance*100.0f);
				pEntry = ui_TextEntryCreate(buf, REF_COL_SIZE+5, y);
				ui_TextEntrySetFinishedCallback(pEntry, ACEMoveChanceChanged, ACE_IDX_TO_PTR((U32)uiRefIndex, (U32)i));
				ui_WidgetSetWidth(UI_WIDGET(pEntry), ACE_RAND_DATA_SIZE);
				ui_WindowAddChild(pWindow, pEntry);
			}

			y += STANDARD_ROW_HEIGHT;
		}

		{
			UIButton *pButton = ui_ButtonCreate("Add", REF_COL_SIZE+5, y, ACEMoveAddAdditional, pFakePtr);
			ui_WidgetSetWidth(UI_WIDGET(pButton), ACE_RAND_DATA_SIZE);
			ui_WindowAddChild(pWindow, pButton);
			y += STANDARD_ROW_HEIGHT;
		}

		elUIAddCancelOkButtons(pWindow, ACERandomMoveWindowCancel, pUI, ACERandomMoveWindowFinished, pUI);
		ui_WindowSetCloseCallback(pWindow, ACERandomMoveWindowCancel, pUI);
		ui_WidgetSetHeight(UI_WIDGET(pWindow), y+STANDARD_ROW_HEIGHT*1.5f);
		elUICenterWindow(pWindow);
		ui_WindowSetModal(pWindow, true);
		ui_WindowSetMovable(pWindow, false);
		ui_WindowSetResizable(pWindow, false);
		ui_WindowShow(pWindow);
	}
}

static void ACEOpenMoveByName(UIWidget *pWidget, void *pMoveName)
{
	emOpenFileEx(pMoveName, DYNMOVE_TYPENAME);
}

static void ACEOpenMoveTransition(UIWidget* pWidget, void* pMoveTransitionName)
{
	emOpenFileEx(pMoveTransitionName, MOVE_TRANSITION_EDITED_DICTIONARY);
}

static void ACEAddMoveTransitionWindowData(UIScrollArea *pScrollArea, DynMoveTransition *pMoveTrans, int x, int *y)
{
	char buf[MAX_CHAR_BUFFER];
	UILabel *pLabel;
	UIButton *pButton;
	
	pButton = ui_ButtonCreate(pMoveTrans->pcName, x, *y, ACEOpenMoveTransition, (void*)(pMoveTrans->pcName));
	ui_ScrollAreaAddChild(pScrollArea, pButton);
	*y += STANDARD_ROW_HEIGHT;

	//show data for the move types
	{
		pLabel = ui_LabelCreate("[Move Type]", x+25, *y);
		ui_ScrollAreaAddChild(pScrollArea, pLabel);
		*y += STANDARD_ROW_HEIGHT;

		if (!eaSize(&pMoveTrans->eaMovementTypesSource)) {
			pLabel = ui_LabelCreate("FROM Any", x+50, *y);
			ui_ScrollAreaAddChild(pScrollArea, pLabel);
			*y += STANDARD_ROW_HEIGHT;
		} else {
			FOR_EACH_IN_EARRAY_FORWARDS(pMoveTrans->eaMovementTypesSource, const char, pcMovementType)
			{
				sprintf(buf, "FROM %s", pcMovementType);
				pLabel = ui_LabelCreate(buf, x+50, *y);
				ui_ScrollAreaAddChild(pScrollArea, pLabel);
				*y += STANDARD_ROW_HEIGHT;
			}
			FOR_EACH_END;
		}

		if (!eaSize(&pMoveTrans->eaMovementTypesTarget)) {
			pLabel = ui_LabelCreate("TO Any", x+50, *y);
			ui_ScrollAreaAddChild(pScrollArea, pLabel);
			*y += STANDARD_ROW_HEIGHT;
		}
		FOR_EACH_IN_EARRAY_FORWARDS(pMoveTrans->eaMovementTypesTarget, const char, pcMovementType)
		{
			sprintf(buf, "TO %s", pcMovementType);
			pLabel = ui_LabelCreate(buf, x+50, *y);
			ui_ScrollAreaAddChild(pScrollArea, pLabel);
			*y += STANDARD_ROW_HEIGHT;
		}
		FOR_EACH_END;
	}

	//show data for the stance words
	{
		pLabel = ui_LabelCreate("[Stance Words]", x+25, *y);
		ui_ScrollAreaAddChild(pScrollArea, pLabel);
		*y += STANDARD_ROW_HEIGHT;

		if (!eaSize(&pMoveTrans->eaStanceWordsSource)) {
			pLabel = ui_LabelCreate("FROM Any", x+50, *y);
			ui_ScrollAreaAddChild(pScrollArea, pLabel);
			*y += STANDARD_ROW_HEIGHT;
		}
		FOR_EACH_IN_EARRAY_FORWARDS(pMoveTrans->eaStanceWordsSource, const char, pcStanceWord)
		{
			sprintf(buf, "FROM %s", pcStanceWord);
			pLabel = ui_LabelCreate(buf, x+50, *y);
			ui_ScrollAreaAddChild(pScrollArea, pLabel);
			*y += STANDARD_ROW_HEIGHT;
		}
		FOR_EACH_END;

		if (!eaSize(&pMoveTrans->eaStanceWordsTarget)) {
			pLabel = ui_LabelCreate("TO Any", x+50, *y);
			ui_ScrollAreaAddChild(pScrollArea, pLabel);
			*y += STANDARD_ROW_HEIGHT;
		}
		FOR_EACH_IN_EARRAY_FORWARDS(pMoveTrans->eaStanceWordsTarget, const char, pcStanceWord)
		{
			sprintf(buf, "TO %s", pcStanceWord);
			pLabel = ui_LabelCreate(buf, x+50, *y);
			ui_ScrollAreaAddChild(pScrollArea, pLabel);
			*y += STANDARD_ROW_HEIGHT;
		}
		FOR_EACH_END;
	}

	//show data for the transition
	{
		pLabel = ui_LabelCreate("[Animation]", x+25, *y);
		ui_ScrollAreaAddChild(pScrollArea, pLabel);
		*y += STANDARD_ROW_HEIGHT;

		pLabel = ui_LabelCreate(REF_HANDLE_GET_STRING(pMoveTrans->hMove), x+50, *y);
		ui_ScrollAreaAddChild(pScrollArea, pLabel);
		*y += STANDARD_ROW_HEIGHT;
	}
}

static void ACEAddMoveWindowData(UIScrollArea *pScrollArea, DynAnimChartMoveRefLoadTime *pMoveRef, DynAnimChartLoadTime *pChart, int x, int *y)
{
	char buf[MAX_CHAR_BUFFER];
	UILabel *pLabel;
	UIButton *pButton;
	
	FOR_EACH_IN_EARRAY(pMoveRef->eaMoveChances, DynAnimMoveChanceRef, pChanceRef) {
		pButton = ui_ButtonCreate(GET_REF(pChanceRef->hMove)->pcName, x, *y, ACEOpenMoveByName, (void*)(GET_REF(pChanceRef->hMove)->pcName));
		ui_ScrollAreaAddChild(pScrollArea, pButton);
		*y += STANDARD_ROW_HEIGHT;
	} FOR_EACH_END;

	pLabel = ui_LabelCreate("[Move Type]", x+25, *y);
	ui_ScrollAreaAddChild(pScrollArea, pLabel);
	*y += STANDARD_ROW_HEIGHT;

	sprintf(buf, "AT %s", pMoveRef->pcMovementType);
	pLabel = ui_LabelCreate(buf, x+50, *y);
	ui_ScrollAreaAddChild(pScrollArea, pLabel);
	*y += STANDARD_ROW_HEIGHT;

	pLabel = ui_LabelCreate("[Stance Words]", x+25, *y);
	ui_ScrollAreaAddChild(pScrollArea, pLabel);
	*y += STANDARD_ROW_HEIGHT;

	if (pMoveRef->pcMovementStance)
	{
		sprintf(buf, "MOVE STANCE %s", pMoveRef->pcMovementStance);
		pLabel = ui_LabelCreate(buf, x+50, *y);
		ui_ScrollAreaAddChild(pScrollArea, pLabel);
		*y += STANDARD_ROW_HEIGHT;
	}
	
	FOR_EACH_IN_EARRAY_FORWARDS(pMoveRef->eaStanceWords, const char, pcColumnWord)
	{
		sprintf(buf, "COLUMN STANCE %s", pcColumnWord);
		pLabel = ui_LabelCreate(buf, x+50, *y);
		ui_ScrollAreaAddChild(pScrollArea, pLabel);
		*y += STANDARD_ROW_HEIGHT;
	}
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY_FORWARDS(pChart->eaStanceWords, const char, pcChartWord)
	{
		sprintf(buf, "CHART STANCE %s", pcChartWord);
		pLabel = ui_LabelCreate(buf, x+50, *y);
		ui_ScrollAreaAddChild(pScrollArea, pLabel);
		*y += STANDARD_ROW_HEIGHT;
	}
	FOR_EACH_END;
}

static void ACEOpenMoveTransitionWindow(UIButton *pUnused, void* pFakePtr)
{
	AnimChartDoc* pDoc;
	U32 uiRefIndex;	
	DynAnimChartMoveRefLoadTime* pRef;
	UIWindow *pWindow;
	WleUIRandomGraphWin *pUI;
	UILabel *pLabel;
	UISeparator *pSeparator;
	UIScrollArea *pScrollArea;
	int yMov = 0, ySrc = 0, yTgt = 0;

	pDoc = (AnimChartDoc*)emGetActiveEditorDoc();
	
	if (!pDoc->pRandomGraphWindow && !pDoc->pRandomMoveWindow && !pDoc->pMoveTransitionWindow)
	{
		uiRefIndex = PTR_TO_U32(pFakePtr);
		pRef = eaGet(&pDoc->pObject->eaMoveRefs, uiRefIndex);
		assert(pRef);

		pWindow = ui_WindowCreate("Possible Transitions", 0, 0, 775, 500);

		pScrollArea = ui_ScrollAreaCreate(0, 1.5*STANDARD_ROW_HEIGHT, 775, 500-1.5*STANDARD_ROW_HEIGHT, 775, 500-1.5*STANDARD_ROW_HEIGHT, false, true);
		ui_WindowAddChild(pWindow, pScrollArea);
		
		pUI = calloc(1, sizeof(*pUI));
		pUI->pWindow = pWindow;
		pUI->pDoc = pDoc;
		pUI->pPreOpenBackup = StructClone(parse_DynAnimChartLoadTime, pDoc->pObject);
		pUI->pFakePtr = pFakePtr;

		pDoc->pMoveTransitionWindow = pUI;

		pLabel = ui_LabelCreate("Lead-ins", 0, 0);
		ui_WindowAddChild(pWindow, pLabel);

		pLabel = ui_LabelCreate("Move Entry", 250, 0);
		ui_WindowAddChild(pWindow, pLabel);

		pLabel = ui_LabelCreate("Lead-outs", 500, 0);
		ui_WindowAddChild(pWindow, pLabel);

		pSeparator = ui_SeparatorCreate(UIHorizontal);
		ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, STANDARD_ROW_HEIGHT);
		ui_WindowAddChild(pWindow, pSeparator);

		FOR_EACH_IN_REFDICT(hMoveTransitionDict, DynMoveTransition, pCurTrans)
		{
			bool validSrcMoveTrans = false;
			bool validTgtMoveTrans = false;

			if (REF_HANDLE_GET_STRING(pCurTrans->hChart) == pDoc->pObject->pcName || //the movement transition is on this chart
				REF_HANDLE_GET_STRING(pCurTrans->hChart) == REF_HANDLE_GET_STRING(pDoc->pObject->hBaseChart)) //the movement transition is on the base chart (for stance charts)
				//no case to check for sub-charts since they can't have movements
			{
				if (!eaSize(&pCurTrans->eaMovementTypesSource) || //any movement type is ok
					eaFind(&pCurTrans->eaMovementTypesSource, pRef->pcMovementType) >= 0) //one of the movement types matches the current move
				{
					validSrcMoveTrans = true; //found source movement type
					FOR_EACH_IN_EARRAY(pCurTrans->eaStanceWordsSource, const char, pSrcStanceWord)
					{
						if (eaFind(&pDoc->pObject->eaStanceWords, pSrcStanceWord) < 0 && //check the chart's stances
							eaFind(&pRef->eaStanceWords, pSrcStanceWord) < 0 && //check the column's stances
							pRef->pcMovementStance != pSrcStanceWord) //check the movement type's stance
						{
							validSrcMoveTrans = false; //failed to find source stance word
						}
					}
					FOR_EACH_END;
				}

				if (!eaSize(&pCurTrans->eaMovementTypesTarget) || //any movement type is ok
					eaFind(&pCurTrans->eaMovementTypesTarget, pRef->pcMovementType) >= 0) //one of the movement types matches the current move
				{
					validTgtMoveTrans = true; //found target movement type
					FOR_EACH_IN_EARRAY(pCurTrans->eaStanceWordsTarget, const char, pTgtStanceWord)
					{
						if (eaFind(&pDoc->pObject->eaStanceWords, pTgtStanceWord) < 0 && //check the chart's stances
							eaFind(&pRef->eaStanceWords, pTgtStanceWord) < 0 && //check the column's stances
							pRef->pcMovementStance != pTgtStanceWord) //check the movement type's stance
						{
							validTgtMoveTrans = false; //failed to find target stance word
						}
					}
					FOR_EACH_END;
				}
			}
			

			if (validSrcMoveTrans) {
				ACEAddMoveTransitionWindowData(pScrollArea, pCurTrans, 500, &ySrc);
				ySrc += STANDARD_ROW_HEIGHT;
			}

			if (validTgtMoveTrans) {
				ACEAddMoveTransitionWindowData(pScrollArea, pCurTrans, 0, &yTgt);
				yTgt += STANDARD_ROW_HEIGHT;
			}
		}
		FOR_EACH_END;

		ACEAddMoveWindowData(pScrollArea, pRef, pDoc->pObject, 250, &yMov);

		ui_ScrollAreaSetSize(pScrollArea, 775, max(max(ySrc, yTgt), yMov));

		ui_WindowSetCloseCallback(pWindow, ACERandomGraphWindowCancel, pUI);
		elUICenterWindow(pWindow);
		ui_WindowSetModal(pWindow, true);
		ui_WindowSetMovable(pWindow, true);
		ui_WindowSetResizable(pWindow, false);
		ui_WindowShow(pWindow);
	}
}

static bool ACEChooseMoveCallback(EMPicker *picker, EMPickerSelection **selections, void* pFakePtr)
{
	AnimChartDoc* pDoc = (AnimChartDoc*)emGetActiveEditorDoc();
	U32 uiRefIndex, uiRandIdx;
	DynAnimChartMoveRefLoadTime *pRef;
	DynAnimMoveChanceRef *pChance;

	if (!eaSize(&selections))
		return false;
	
	ACE_PTR_TO_IDX(uiRefIndex, uiRandIdx, pFakePtr);
	pRef = eaGet(&pDoc->pObject->eaMoveRefs, uiRefIndex);
	assert(pRef);
	pChance = eaGet(&pRef->eaMoveChances, uiRandIdx);
	if(!pChance) {
		int cnt = eaSize(&pRef->eaMoveChances);
		pChance = StructCreate(parse_DynAnimMoveChanceRef);
		if(cnt>0)
			pChance->fChance = 1.0f/cnt;
		else 
			pChance->fChance = 1.0f;
		eaPush(&pRef->eaMoveChances, pChance);
		//We should only ever be requesting one more than the number we have
		pChance = eaGet(&pRef->eaMoveChances, uiRandIdx);
		ACENormalizeMoveChances(pRef);
	}
	assert(pChance);

	SET_HANDLE_FROM_STRING(DYNMOVE_DICTNAME, selections[0]->doc_name, pChance->hMove);

	ACEAnimChartChanged(pDoc, true);
	return true;
}

static void ACEChooseMove(UIWidget* pWidget, void* pFakePtr)
{
	AnimChartDoc* pDoc = (AnimChartDoc*)emGetActiveEditorDoc();
	if (pDoc) {
		EMPicker* pMovePicker = emPickerGetByName( "Move Library" );

		if (pMovePicker)
			emPickerShow(pMovePicker, NULL, false, ACEChooseMoveCallback, pFakePtr);
	}
}

static void ACEClearMove(UIWidget* pWidget, void* pFakePtr)
{
	AnimChartDoc* pDoc = (AnimChartDoc*)emGetActiveEditorDoc();
	if (pDoc) {
		U32 uiRefIndex = PTR_TO_U32(pFakePtr);
		EMPicker* pMovePicker = emPickerGetByName( "Move Library" );
		DynAnimChartMoveRefLoadTime* pRef;

		assert(pDoc);
		pRef = eaGet(&pDoc->pObject->eaMoveRefs, uiRefIndex);
		assert(pRef);

		pRef->bBlank = false;
		//REMOVE_HANDLE(pRef->hMove);
		FOR_EACH_IN_EARRAY(pRef->eaMoveChances, DynAnimMoveChanceRef, pChanceRef) {
			REMOVE_HANDLE(pChanceRef->hMove);
		} FOR_EACH_END;
		eaDestroyStruct(&pRef->eaMoveChances, parse_DynAnimMoveChanceRef);
		ACEAnimChartChanged(pDoc, true);
	}
}

static void ACEClearRandMove(UIWidget* pWidget, void* pFakePtr)
{
	AnimChartDoc* pDoc = (AnimChartDoc*)emGetActiveEditorDoc();
	if (pDoc) {
		U32 uiRefIndex, uiRandIdx;
		EMPicker* pMovePicker = emPickerGetByName( "Move Library" );
		DynAnimChartMoveRefLoadTime* pRef;

		ACE_PTR_TO_IDX(uiRefIndex, uiRandIdx, pFakePtr);

		assert(pDoc);
		pRef = eaGet(&pDoc->pObject->eaMoveRefs, uiRefIndex);
		assert(pRef);

		if (eaUSize(&pRef->eaMoveChances) > uiRandIdx) {
			DynAnimMoveChanceRef *pChanceRef = eaRemove(&pRef->eaMoveChances, uiRandIdx);
			REMOVE_HANDLE(pChanceRef->hMove);
			StructDestroy(parse_DynAnimMoveChanceRef, pChanceRef);
			pRef->bBlank = false;
		}
		
		ACEAnimChartChanged(pDoc, true);
	}
}

static void ACEOpenMove(UIWidget* pWidget, void* pFakePtr)
{
	AnimChartDoc* pDoc = (AnimChartDoc*)emGetActiveEditorDoc();
	U32 uiRefIndex = PTR_TO_U32(pFakePtr);
	DynAnimChartMoveRefLoadTime* pRef;

	assert(pDoc);
	pRef = eaGet(&pDoc->pObject->eaMoveRefs, uiRefIndex);
	assert(pRef);

	FOR_EACH_IN_EARRAY(pRef->eaMoveChances, DynAnimMoveChanceRef, pChanceRef) {
		emOpenFileEx(GET_REF(pChanceRef->hMove)->pcName, DYNMOVE_TYPENAME);
	} FOR_EACH_END;
}

static void ACEOpenRandMove(UIWidget* pWidget, void* pFakePtr)
{
	AnimChartDoc* pDoc = (AnimChartDoc*)emGetActiveEditorDoc();
	U32 uiRefIndex, uiRandIdx;
	DynAnimChartMoveRefLoadTime* pRef;
	DynAnimMoveChanceRef *pChanceRef;

	ACE_PTR_TO_IDX(uiRefIndex, uiRandIdx, pFakePtr);

	assert(pDoc);
	pRef = eaGet(&pDoc->pObject->eaMoveRefs, uiRefIndex);
	assert(pRef);
	pChanceRef = eaGet(&pRef->eaMoveChances, uiRefIndex);
	assert(pChanceRef);
	
	emOpenFileEx(GET_REF(pChanceRef->hMove)->pcName, DYNMOVE_TYPENAME);
}

static bool ACEChooseChartCallback(EMPicker *picker, EMPickerSelection **selections, void* unused)
{
	AnimChartDoc* pDoc = (AnimChartDoc*)emGetActiveEditorDoc();

	if (!eaSize(&selections))
		return false;

	if (stricmp(selections[0]->doc_name, pDoc->pObject->pcName)==0)
		return false;

	SET_HANDLE_FROM_STRING(hAnimChartDictLoadTime, selections[0]->doc_name, pDoc->pObject->hBaseChart);
	REMOVE_HANDLE(pDoc->pObject->hMovementSet);

	dynAnimChartMovementSetChanged(pDoc->pObject);
	ACEAnimChartChanged(pDoc, true);
	return true;
}

static void ACEChooseChart(UIWidget* pWidget, void* unused)
{
	AnimChartDoc* pDoc = (AnimChartDoc*)emGetActiveEditorDoc();
	if (pDoc) {
		EMPicker* pChartPicker = emPickerGetByName( "Animation Chart Library" );
		assert(pDoc);

		if (pChartPicker)
			emPickerShow(pChartPicker, NULL, false, ACEChooseChartCallback, NULL);
	}
}

static bool ACEChooseMovementSetCallback(EMPicker *picker, EMPickerSelection **selections, DynAnimChartLoadTime* pChart)
{
	AnimChartDoc* pDoc = (AnimChartDoc*)emGetActiveEditorDoc();

	if (!eaSize(&selections))
		return false;

	SET_HANDLE_FROM_STRING("DynMovementSet", selections[0]->doc_name, pChart->hMovementSet);
	dynAnimChartMovementSetChanged(pDoc->pObject);
	ACEAnimChartChanged(pDoc, true);
	return true;
}

static void ACEChooseMovementSet(UIWidget* pWidget, void* pUnused)
{
	AnimChartDoc* pDoc = (AnimChartDoc*)emGetActiveEditorDoc();

	EMPicker* pMovePicker = emPickerGetByName( "DynMovementSet Library" );

	if (pMovePicker)
		emPickerShow(pMovePicker, NULL, false, ACEChooseMovementSetCallback, pDoc->pObject);
}

static void ACEAddKeyword(UIWidget* pWidget, SA_PARAM_NN_VALID AnimChartDoc* pDoc)
{
	if (pDoc) {
		DynAnimChartGraphRefLoadTime* pRef = StructCreate(parse_DynAnimChartGraphRefLoadTime);
		U32 uiRefIndex = eaPush(&pDoc->pObject->eaGraphRefs, pRef);

		ACEChooseGraph(pWidget, ACE_IDX_TO_PTR(uiRefIndex, 0));
	}
}

static void ACEAddBaseStanceWord(DynAnimChartLoadTime* pObject, const char ***eaStanceWords)
{
	int i;
	const char **ppNewStance;
	eaPush(eaStanceWords, allocAddString("Not Set"));
	ppNewStance = &((*eaStanceWords)[eaSize(eaStanceWords)-1]);
	for(i=2; ACEGetValidStanceIndex(pObject, *eaStanceWords) >= 0; i++) {
		char pcAttempt[1024];
		sprintf(pcAttempt, "Not Set %d", i);
		(*ppNewStance) = allocAddString(pcAttempt);
	}
}

static void ACEAddStanceKey(UIWidget* pWidget, SA_PARAM_NN_VALID AnimChartDoc* pDoc)
{
	if (pDoc) {
		const char **pcNewList = NULL;
		ACEAddBaseStanceWord(pDoc->pObject, &pcNewList);
		eaPush(&pDoc->pObject->eaValidStances, pcNewList[0]);
		eaDestroy(&pcNewList);
		ACEAnimChartChanged(pDoc, true);
	}
}

static void ACEAddDefaultKeyword(UIWidget* pWidget, SA_PARAM_NN_VALID AnimChartDoc* pDoc)
{
	if (pDoc) {
		DynAnimChartGraphRefLoadTime* pRef = StructCreate(parse_DynAnimChartGraphRefLoadTime);
		U32 uiRefIndex = eaPush(&pDoc->pObject->eaGraphRefs, pRef);
		pRef->pcKeyword = allocAddString("Default");
		eaPush(&pDoc->pObject->eaValidKeywords, pRef->pcKeyword);
		ACEFormatValidKeywords(pDoc->pObject);

		ACEChooseGraph(pWidget, ACE_IDX_TO_PTR(uiRefIndex, 0));
	}
}

static void ACERemoveGraphHelper(AnimChartDoc *pDoc, UIWidget *pWidget, void *pFakePtr)
{
	U32 uiRefIndex, uiRandIdx;
	DynAnimChartGraphRefLoadTime* pRef;
	ACE_PTR_TO_IDX(uiRefIndex, uiRandIdx, pFakePtr);

	pRef = eaRemove(&pDoc->pObject->eaGraphRefs, uiRefIndex);
	assert(pRef);
	if (pRef)
		StructDestroy(parse_DynAnimChartGraphRefLoadTime, pRef);
}

static void ACERemoveGraph(UIWidget* pWidget, void* pFakePtr)
{
	AnimChartDoc* pDoc = (AnimChartDoc*)emGetActiveEditorDoc();
	if (pDoc) {
		ACERemoveGraphHelper(pDoc, pWidget, pFakePtr);
		ACEAnimChartChanged(pDoc, true);
	}
}

static void ACEAddGraphHelper(AnimChartDoc *pDoc, UIWidget *pWidget, void *pFakePtr, bool bBlank)
{

	int keyword_idx, stance_idx;
	DynAnimChartGraphRefLoadTime* pRef = StructCreate(parse_DynAnimChartGraphRefLoadTime);
	U32 uiRefIndex = eaPush(&pDoc->pObject->eaGraphRefs, pRef);
	const char *pcDefaultString = allocAddString("Default");
	const char *pcStance;
	ACE_PTR_TO_IDX(keyword_idx, stance_idx, pFakePtr);
	stance_idx--;

	if (eaFind(&pDoc->pObject->eaValidKeywords, pcDefaultString) >= 0) {
		DynAnimChartLoadTime* pBaseChart = GET_REF(pDoc->pObject->hBaseChart);
		DynMovementSet* pMovementSet = pBaseChart?GET_REF(pBaseChart->hMovementSet):GET_REF(pDoc->pObject->hMovementSet);
		const char **eaMovementStances = NULL;
		if (pMovementSet)	
			eaMovementStances = pMovementSet->eaMovementStances;
		if(keyword_idx < eaSize(&eaMovementStances)+1) {
			pRef->pcKeyword = pcDefaultString;
			keyword_idx--;
			if(keyword_idx >= 0) {
				assert(keyword_idx < eaSize(&eaMovementStances));
				pRef->pcMovementStance = eaMovementStances[keyword_idx];
			}
		} else {
			keyword_idx -= eaSize(&eaMovementStances);
			assert(keyword_idx >= 0 && keyword_idx < eaSize(&pDoc->pObject->eaValidKeywords));
			pRef->pcKeyword = pDoc->pObject->eaValidKeywords[keyword_idx];
		}
	} else {
		assert(keyword_idx >= 0 && keyword_idx < eaSize(&pDoc->pObject->eaValidKeywords));
		pRef->pcKeyword = pDoc->pObject->eaValidKeywords[keyword_idx];
	}

	if(stance_idx >= 0) {
		assert(stance_idx < eaSize(&pDoc->pObject->eaValidStances));
		pcStance = pDoc->pObject->eaValidStances[stance_idx];
		dynAnimStanceWordsFromKey(pcStance, &pRef->eaStanceWords);
	}

	pRef->bBlank = bBlank;
	if (!bBlank)
		ACEChooseGraph(pWidget, ACE_IDX_TO_PTR(uiRefIndex, 0));
	else
		ACEAnimChartChanged(pDoc, true);
}

static void ACEAddBlankGraphRef(UIWidget *pWidget, void *pFakePtr)
{
	AnimChartDoc *pDoc = (AnimChartDoc*)emGetActiveEditorDoc();
	if (pDoc) {
		ACEAddGraphHelper(pDoc, pWidget, pFakePtr, true);
	}
}

static void ACEAddGraphRef(UIWidget* pWidget, void* pFakePtr)
{
	AnimChartDoc* pDoc = (AnimChartDoc*)emGetActiveEditorDoc();
	if (pDoc) {
		ACEAddGraphHelper(pDoc, pWidget, pFakePtr, false);
	}
}

static void ACERemoveAndAddGraphRef(UIWidget *pWidget, void *pFakePtr)
{
	AnimChartDoc *pDoc = (AnimChartDoc*)emGetActiveEditorDoc();
	if (pDoc) {
		U32 uiRefIdx, uiKeywordIdx, uiStanceIdx;
		ACE_PTR_TO_IDX3(uiRefIdx, uiKeywordIdx, uiStanceIdx, pFakePtr);
		ACERemoveGraphHelper(pDoc, pWidget, ACE_IDX_TO_PTR(uiRefIdx, 0));
		ACEAddGraphHelper(pDoc, pWidget, ACE_IDX_TO_PTR(uiKeywordIdx, uiStanceIdx), false);
	}
}

static bool ACESameStanceKey(const char *pcKey1, const char *pcKey2)
{
	if(!pcKey1 || !pcKey1[0]) {
		return (!pcKey2 || !pcKey2[0]);
	}
	return (pcKey1 == pcKey2);
}

static void ACEAddMoveRefHelper(UIWidget *pWidget, void *pFakePtr, bool bBlank)
{
	int i;
	AnimChartDoc* pDoc = (AnimChartDoc*)emGetActiveEditorDoc();
	if (pDoc) {
		int move_stance_idx, move_seq_idx, stance_idx;
		DynAnimChartLoadTime* pBaseChart = GET_REF(pDoc->pObject->hBaseChart);
		DynMovementSet* pMovementSet = pBaseChart?GET_REF(pBaseChart->hMovementSet):GET_REF(pDoc->pObject->hMovementSet);
		DynAnimChartMoveRefLoadTime* pRef = NULL;
		U32 uiRefIndex = 0;
		const char *pcMovementType = NULL;
		const char *pcMoveStance = NULL;
		const char *pcStance = NULL;
		ACE_PTR_TO_IDX3(move_seq_idx, move_stance_idx, stance_idx, pFakePtr);
		move_stance_idx--;
		stance_idx--;

		assert(	pMovementSet && 
				move_seq_idx >= 0 && 
				move_seq_idx < eaSize(&pMovementSet->eaMovementSequences) && 
				move_stance_idx < eaSize(&pMovementSet->eaMovementStances));
		
		pcMoveStance = (move_stance_idx<0 ? NULL : pMovementSet->eaMovementStances[move_stance_idx]);
		pcMovementType = pMovementSet->eaMovementSequences[move_seq_idx]->pcMovementType;

		if(stance_idx >= 0) {
			assert(stance_idx < eaSize(&pDoc->pObject->eaValidStances));
			pcStance = pDoc->pObject->eaValidStances[stance_idx];
		}

		for ( i=0; i < eaSize(&pDoc->pObject->eaMoveRefs); i++ ) {
			DynAnimChartMoveRefLoadTime* pTestRef = pDoc->pObject->eaMoveRefs[i];
			if(	pTestRef->pcMovementType == pcMovementType && pTestRef->pcMovementStance == pcMoveStance) {
				if(ACESameStanceKey(dynAnimKeyFromStanceWords(pTestRef->eaStanceWords), pcStance)) {
					pRef = pTestRef;
					uiRefIndex = i;
					break;
				}
			}
		}
		
		if(!pRef) {
			pRef = StructCreate(parse_DynAnimChartMoveRefLoadTime);
			uiRefIndex = eaPush(&pDoc->pObject->eaMoveRefs, pRef);
			pRef->pcMovementType = pcMovementType;
			pRef->pcMovementStance = pcMoveStance;
			if(pcStance)
				dynAnimStanceWordsFromKey(pcStance, &pRef->eaStanceWords);
		}
		
		if (bBlank) {
			pRef->bBlank = true;
			ACEAnimChartChanged(pDoc, true);
		} else {
			pRef->bBlank = false;
			ACEChooseMove(pWidget, ACE_IDX_TO_PTR(uiRefIndex, eaSize(&pRef->eaMoveChances)));
		}
	}
}

static void ACEAddMoveRef(UIWidget* pWidget, void *pFakePtr)
{
	ACEAddMoveRefHelper(pWidget, pFakePtr, false);
}

static void ACEAddBlankMoveRef(UIWidget *pWidget, void *pFakePtr)
{
	ACEAddMoveRefHelper(pWidget, pFakePtr, true);
}

static void ACEOpenGraph(UIWidget* pWidget, void* pFakePtr)
{
	U32 uiRefIndex, uiRandIdx;
	AnimChartDoc* pDoc = (AnimChartDoc*)emGetActiveEditorDoc();
	DynAnimChartGraphRefLoadTime* pRef;
	DynAnimGraphChanceRef *pChance;
	ACE_PTR_TO_IDX(uiRefIndex, uiRandIdx, pFakePtr);

	assert(pDoc);
	pRef = eaGet(&pDoc->pObject->eaGraphRefs, uiRefIndex);
	assert(pRef);
	pChance = eaGet(&pRef->eaGraphChances, uiRandIdx);
	if(pChance && GET_REF(pChance->hGraph))
		emOpenFileEx(GET_REF(pChance->hGraph)->pcName, "AnimGraph");
}

static void ACEMoveKeywordUp(UIWidget* pWidget, void* pFakePtr)
{
	AnimChartDoc* pDoc = (AnimChartDoc*)emGetActiveEditorDoc();
	if (pDoc) {
		int keyword_idx;
		const char *pcKeyword=NULL;
		keyword_idx = PTR_TO_U32(pFakePtr);

		assert(keyword_idx >= 0 && keyword_idx < eaSize(&pDoc->pObject->eaValidKeywords));
		if (keyword_idx > 0)
		{
			if (keyword_idx-1 != 0 || pDoc->pObject->eaValidKeywords[0] != ANIM_CHART_DEFAULT_KEY)
			{
				pcKeyword = eaRemove(&pDoc->pObject->eaValidKeywords, keyword_idx);
				eaInsert(&pDoc->pObject->eaValidKeywords, pcKeyword, keyword_idx-1);
				ACEAnimChartChanged(pDoc, true);
			}
		}
	}
}

static void ACEMoveKeywordDown(UIWidget* pWidget, void* pFakePtr)
{
	AnimChartDoc* pDoc = (AnimChartDoc*)emGetActiveEditorDoc();
	if (pDoc) {
		int keyword_idx;
		const char *pcKeyword=NULL;
		keyword_idx = PTR_TO_U32(pFakePtr);

		assert(keyword_idx >= 0 && keyword_idx < eaSize(&pDoc->pObject->eaValidKeywords));
		if (keyword_idx < eaSize(&pDoc->pObject->eaValidKeywords)-1)
		{
			pcKeyword = eaRemove(&pDoc->pObject->eaValidKeywords, keyword_idx);
			eaInsert(&pDoc->pObject->eaValidKeywords, pcKeyword, keyword_idx+1);
			ACEAnimChartChanged(pDoc, true);
		}
	}
}

static void ACEDeleteKeyword(UIWidget* pWidget, void* pFakePtr)
{
	int i;
	AnimChartDoc* pDoc = (AnimChartDoc*)emGetActiveEditorDoc();
	if (pDoc) {
		int keyword_idx;
		const char *pcKeyword=NULL;
		keyword_idx = PTR_TO_U32(pFakePtr);

		assert(keyword_idx >= 0 && keyword_idx < eaSize(&pDoc->pObject->eaValidKeywords));
		pcKeyword = eaRemove(&pDoc->pObject->eaValidKeywords, keyword_idx);
		
		for ( i=0; i < eaSize(&pDoc->pObject->eaGraphRefs); i++ ) {
			DynAnimChartGraphRefLoadTime *pRef = pDoc->pObject->eaGraphRefs[i];
			if(pRef->pcKeyword == pcKeyword) {
				eaRemove(&pDoc->pObject->eaGraphRefs, i);
				i--;
				StructDestroy(parse_DynAnimChartGraphRefLoadTime, pRef);
			}
		}

		ACEAnimChartChanged(pDoc, true);
	}
}

static void ACEGraphAddAdditional(UIWidget* pWidget, void* pFakePtr)
{
	AnimChartDoc* pDoc = (AnimChartDoc*)emGetActiveEditorDoc();
	if (pDoc) {
		EMPicker* pMovePicker = emPickerGetByName( "Animation Graph Library" );
		U32 uiRefIndex, uiRandIdx;
		DynAnimChartGraphRefLoadTime* pRef;
		ACE_PTR_TO_IDX(uiRefIndex, uiRandIdx, pFakePtr);

		pRef = eaGet(&pDoc->pObject->eaGraphRefs, uiRefIndex);
		assert(pRef);
		uiRandIdx = eaSize(&pRef->eaGraphChances);

		if (pMovePicker)
			emPickerShowEx(pMovePicker, NULL, false, ACEChooseGraphCallback, ACEChooseGraphClosedCallback, ACE_IDX_TO_PTR(uiRefIndex, uiRandIdx));
	}
}

static void ACEMoveAddAdditional(UIWidget* pWidget, void* pFakePtr)
{
	AnimChartDoc* pDoc = (AnimChartDoc*)emGetActiveEditorDoc();
	if (pDoc) {
		EMPicker* pMovePicker = emPickerGetByName( "Move Library" );
		U32 uiRefIndex, uiRandIdx;
		DynAnimChartMoveRefLoadTime* pRef;
		uiRefIndex = PTR_TO_U32(pFakePtr);

		pRef = eaGet(&pDoc->pObject->eaMoveRefs, uiRefIndex);
		assert(pRef);
		uiRandIdx = eaSize(&pRef->eaMoveChances);

		if (pMovePicker)
			emPickerShowEx(pMovePicker, NULL, false, ACEChooseMoveCallback, ACEChooseMoveClosedCallback, ACE_IDX_TO_PTR(uiRefIndex, uiRandIdx));
	}
}

static void ACEMoveStanceWordLeft(UIWidget *pWidget, void *pFakePtr)
{
	AnimChartDoc* pDoc = (AnimChartDoc*)emGetActiveEditorDoc();
	if (pDoc) {
		int stance_key_idx;
		const char *pcStanceKey=NULL;
		stance_key_idx = PTR_TO_U32(pFakePtr);
		stance_key_idx--;

		assert(stance_key_idx >= 0 && stance_key_idx < eaSize(&pDoc->pObject->eaValidStances));
		if (stance_key_idx > 0)
		{
			pcStanceKey = eaRemove(&pDoc->pObject->eaValidStances, stance_key_idx);
			eaInsert(&pDoc->pObject->eaValidStances, pcStanceKey, stance_key_idx-1);
			ACEAnimChartChanged(pDoc, true);
		}
	}
}

static void ACEMoveStanceWordRight(UIWidget *pWidget, void *pFakePtr)
{
	AnimChartDoc* pDoc = (AnimChartDoc*)emGetActiveEditorDoc();
	if (pDoc) {
		int stance_key_idx;
		const char *pcStanceKey=NULL;
		stance_key_idx = PTR_TO_U32(pFakePtr);
		stance_key_idx--;

		assert(stance_key_idx >= 0 && stance_key_idx < eaSize(&pDoc->pObject->eaValidStances));
		if (stance_key_idx < eaSize(&pDoc->pObject->eaValidStances) -1)
		{
			pcStanceKey = eaRemove(&pDoc->pObject->eaValidStances, stance_key_idx);
			eaInsert(&pDoc->pObject->eaValidStances, pcStanceKey, stance_key_idx+1);
			ACEAnimChartChanged(pDoc, true);
		}
	}
}

static void ACEDeleteStanceKey(UIWidget* pWidget, void* pFakePtr)
{
	int i;
	AnimChartDoc* pDoc = (AnimChartDoc*)emGetActiveEditorDoc();
	if (pDoc) {
		int stance_key_idx;
		const char *pcStanceKey=NULL;
		const char **eaStanceWords = NULL;
		const char *pcOrigStanceKey;
		stance_key_idx = PTR_TO_U32(pFakePtr);
		stance_key_idx--;

		assert(stance_key_idx >= 0 && stance_key_idx < eaSize(&pDoc->pObject->eaValidStances));
		pcStanceKey = eaRemove(&pDoc->pObject->eaValidStances, stance_key_idx);
		dynAnimStanceWordsFromKey(pcStanceKey, &eaStanceWords);
		pcOrigStanceKey = dynAnimKeyFromStanceWords(eaStanceWords);

		for ( i=0; i < eaSize(&pDoc->pObject->eaGraphRefs); i++ ) {
			DynAnimChartGraphRefLoadTime *pRef = pDoc->pObject->eaGraphRefs[i];
			if(dynAnimKeyFromStanceWords(pRef->eaStanceWords) == pcOrigStanceKey) {
				eaRemove(&pDoc->pObject->eaGraphRefs, i);
				i--;
				StructDestroy(parse_DynAnimChartGraphRefLoadTime, pRef);
			}
		}
		for ( i=0; i < eaSize(&pDoc->pObject->eaMoveRefs); i++ ) {
			DynAnimChartMoveRefLoadTime *pRef = pDoc->pObject->eaMoveRefs[i];
			if(dynAnimKeyFromStanceWords(pRef->eaStanceWords) == pcOrigStanceKey) {
				eaRemove(&pDoc->pObject->eaMoveRefs, i);
				i--;
				StructDestroy(parse_DynAnimChartMoveRefLoadTime, pRef);
			}
		}

		ACEAnimChartChanged(pDoc, true);
	}
}

static void ACERefAddStanceWord(UIWidget* pWidget, void* pFakePtr)
{
	int i;
	AnimChartDoc* pDoc = (AnimChartDoc*)emGetActiveEditorDoc();
	if (pDoc) {
		int stance_key_idx;
		char *pcSelected=NULL;
		const char **eaStanceWords = NULL;
		const char *pcOrigStanceKey = NULL;
		stance_key_idx = PTR_TO_U32(pFakePtr);
		stance_key_idx--;

		if(stance_key_idx < 0)
			eaCopy(&eaStanceWords, &pDoc->pObject->eaStanceWords);
		else if(stance_key_idx < eaSize(&pDoc->pObject->eaValidStances)) {
			dynAnimStanceWordsFromKey(pDoc->pObject->eaValidStances[stance_key_idx], &eaStanceWords);
			pcOrigStanceKey = dynAnimKeyFromStanceWords(eaStanceWords);
		} else 
			assert(false);

		ACEAddBaseStanceWord(pDoc->pObject, &eaStanceWords);

		if(stance_key_idx < 0) {
			eaCopy(&pDoc->pObject->eaStanceWords, &eaStanceWords);
		} else if(stance_key_idx < eaSize(&pDoc->pObject->eaValidStances)) {
			pDoc->pObject->eaValidStances[stance_key_idx] = dynAnimKeyFromStanceWords(eaStanceWords);
			for ( i=0; i < eaSize(&pDoc->pObject->eaGraphRefs); i++ ) {
				DynAnimChartGraphRefLoadTime *pRef = pDoc->pObject->eaGraphRefs[i];
				if(dynAnimKeyFromStanceWords(pRef->eaStanceWords) == pcOrigStanceKey) {
					eaCopy(&pRef->eaStanceWords, &eaStanceWords);
				}
			}
			for ( i=0; i < eaSize(&pDoc->pObject->eaMoveRefs); i++ ) {
				DynAnimChartMoveRefLoadTime *pRef = pDoc->pObject->eaMoveRefs[i];
				if(dynAnimKeyFromStanceWords(pRef->eaStanceWords) == pcOrigStanceKey) {
					eaCopy(&pRef->eaStanceWords, &eaStanceWords);
				}
			}
		}

		eaDestroy(&eaStanceWords);
		ACEAnimChartChanged(pDoc, true);
	}
}

static void ACERefStanceWordChanged(UIComboBox *pComboBox, void* pFakePtr)
{
	int i;
	AnimChartDoc* pDoc = (AnimChartDoc*)emGetActiveEditorDoc();
	if (pDoc) {
		int stance_key_idx, stance_idx;
		char *pcSelected=NULL;
		const char **eaStanceWords = NULL;
		const char *pcOrigStanceKey;
		const char *pcNewStanceKey;
		ACE_PTR_TO_IDX(stance_key_idx, stance_idx, pFakePtr);
		stance_key_idx--;

		if(stance_key_idx < 0)
			eaCopy(&eaStanceWords, &pDoc->pObject->eaStanceWords);
		else if(stance_key_idx < eaSize(&pDoc->pObject->eaValidStances))
			dynAnimStanceWordsFromKey(pDoc->pObject->eaValidStances[stance_key_idx], &eaStanceWords);
		else 
			assert(false);

		assert(stance_idx >=0 && stance_idx < eaSize(&eaStanceWords));
		ui_ComboBoxGetSelectedsAsString(pComboBox, &pcSelected);
		pcOrigStanceKey = dynAnimKeyFromStanceWords(eaStanceWords);
		if(pcSelected)
			eaStanceWords[stance_idx] = allocAddString(pcSelected);
		pcNewStanceKey = dynAnimKeyFromStanceWords(eaStanceWords);

		if(stance_key_idx >= 0 && ACEGetValidStanceIndexFromKey(pDoc->pObject, pcNewStanceKey) >= 0) {
			Alertf("Can not have columns with duplicate stance words.");
			eaRemove(&eaStanceWords, stance_idx);
			ACEAddBaseStanceWord(pDoc->pObject, &eaStanceWords);
			pcNewStanceKey = dynAnimKeyFromStanceWords(eaStanceWords);
		}
		
		if(pcOrigStanceKey == pcNewStanceKey) {
			eaDestroy(&eaStanceWords);
			return;
		}

		if(stance_key_idx < 0) {
			eaCopy(&pDoc->pObject->eaStanceWords, &eaStanceWords);
		} else if(stance_key_idx < eaSize(&pDoc->pObject->eaValidStances)) {
			pDoc->pObject->eaValidStances[stance_key_idx] = pcNewStanceKey;
			for ( i=0; i < eaSize(&pDoc->pObject->eaGraphRefs); i++ ) {
				DynAnimChartGraphRefLoadTime *pRef = pDoc->pObject->eaGraphRefs[i];
				if(dynAnimKeyFromStanceWords(pRef->eaStanceWords) == pcOrigStanceKey) {
					eaCopy(&pRef->eaStanceWords, &eaStanceWords);
				}
			}
			for ( i=0; i < eaSize(&pDoc->pObject->eaMoveRefs); i++ ) {
				DynAnimChartMoveRefLoadTime *pRef = pDoc->pObject->eaMoveRefs[i];
				if(dynAnimKeyFromStanceWords(pRef->eaStanceWords) == pcOrigStanceKey) {
					eaCopy(&pRef->eaStanceWords, &eaStanceWords);
				}
			}
		}

		eaDestroy(&eaStanceWords);
		ACEAnimChartChanged(pDoc, true);
	}
}

static void ACERefRemoveStanceWord(UIWidget* pWidget, void* pFakePtr)
{
	int i;
	AnimChartDoc* pDoc = (AnimChartDoc*)emGetActiveEditorDoc();
	if (pDoc) {
		int stance_key_idx, stance_idx;
		char *pcSelected=NULL;
		const char **eaStanceWords = NULL;
		const char *pcOrigStanceKey = NULL;
		ACE_PTR_TO_IDX(stance_key_idx, stance_idx, pFakePtr);
		stance_key_idx--;

		if(stance_key_idx < 0)
			eaCopy(&eaStanceWords, &pDoc->pObject->eaStanceWords);
		else if(stance_key_idx < eaSize(&pDoc->pObject->eaValidStances)) {
			dynAnimStanceWordsFromKey(pDoc->pObject->eaValidStances[stance_key_idx], &eaStanceWords);
			pcOrigStanceKey = dynAnimKeyFromStanceWords(eaStanceWords);
		} else 
			assert(false);

		assert(stance_idx >=0 && stance_idx < eaSize(&eaStanceWords));
		eaRemove(&eaStanceWords, stance_idx);

		if(ACEGetValidStanceIndex(pDoc->pObject, eaStanceWords) >= 0) {
			Alertf("Can not have columns with duplicate stance words.");
			eaDestroy(&eaStanceWords);
			return;
		}

		if(stance_key_idx < 0) {
			eaCopy(&pDoc->pObject->eaStanceWords, &eaStanceWords);
		} else if(stance_key_idx < eaSize(&pDoc->pObject->eaValidStances)) {
			pDoc->pObject->eaValidStances[stance_key_idx] = dynAnimKeyFromStanceWords(eaStanceWords);
			for ( i=0; i < eaSize(&pDoc->pObject->eaGraphRefs); i++ ) {
				DynAnimChartGraphRefLoadTime *pRef = pDoc->pObject->eaGraphRefs[i];
				if(dynAnimKeyFromStanceWords(pRef->eaStanceWords) == pcOrigStanceKey) {
					eaCopy(&pRef->eaStanceWords, &eaStanceWords);
				}
			}
			for ( i=0; i < eaSize(&pDoc->pObject->eaMoveRefs); i++ ) {
				DynAnimChartMoveRefLoadTime *pRef = pDoc->pObject->eaMoveRefs[i];
				if(dynAnimKeyFromStanceWords(pRef->eaStanceWords) == pcOrigStanceKey) {
					eaCopy(&pRef->eaStanceWords, &eaStanceWords);
				}
			}
		}

		eaDestroy(&eaStanceWords);
		ACEAnimChartChanged(pDoc, true);
	}
}

static bool ACEChooseSubChartCallback(EMPicker *picker, EMPickerSelection **selections, DynAnimSubChartRef* pRef)
{
	AnimChartDoc* pDoc = (AnimChartDoc*)emGetActiveEditorDoc();

	if (!eaSize(&selections))
		return false;

	SET_HANDLE_FROM_STRING(hAnimChartDictLoadTime, selections[0]->doc_name, pRef->hSubChart);

	{
		DynAnimChartLoadTime* pSubChart = GET_REF(pRef->hSubChart);
		if (pSubChart && !pSubChart->bIsSubChart)
		{
			Errorf("Tried to add non subchart %s as a subchart!", pSubChart->pcName);
			REMOVE_HANDLE(pRef->hSubChart);
			return false;
		}
	}

	ACEAnimChartChanged(pDoc, true);
	return true;
}

static void ACEChooseSubChart(UIWidget* pWidget, void* pFakePtr)
{
	U32 uiRefIndex = PTR_TO_U32(pFakePtr);
	EMPicker* pMovePicker = emPickerGetByName( "Animation Chart Library" );
	AnimChartDoc* pDoc = (AnimChartDoc*)emGetActiveEditorDoc();
	DynAnimSubChartRef* pRef;

	assert(pDoc);

	pRef = eaGet(&pDoc->pObject->eaSubCharts, uiRefIndex);
	assert(pRef);


	if (pMovePicker && pRef)
		emPickerShow(pMovePicker, NULL, false, ACEChooseSubChartCallback, pRef);
}

static void ACEAddSubChart(UIWidget* pWidget, SA_PARAM_NN_VALID AnimChartDoc* pDoc)
{
	if (pDoc) {
		DynAnimSubChartRef* pRef = StructCreate(parse_DynAnimSubChartRef);
		U32 uiRefIndex = eaPush(&pDoc->pObject->eaSubCharts, pRef);

		ACEChooseSubChart(pWidget, U32_TO_PTR(uiRefIndex));
		ACEAnimChartChanged(pDoc, false);
	}
}

static void ACERemoveSubChart(UIWidget* pWidget, void* pFakePtr)
{
	AnimChartDoc* pDoc = (AnimChartDoc*)emGetActiveEditorDoc();
	if (pDoc) {
		U32 uiRefIndex = PTR_TO_U32(pFakePtr);
		DynAnimSubChartRef* pRef;

		assert(pDoc);

		pRef = eaRemove(&pDoc->pObject->eaSubCharts, uiRefIndex);
		assert(pRef);
		if (pRef)
			StructDestroy(parse_DynAnimSubChartRef, pRef);
		ACEAnimChartChanged(pDoc, true);
	}
}

static void ACEOpenStanceChart(UIWidget* pWidget, void* pChartName)
{
	emOpenFileEx(pChartName, ANIM_CHART_EDITED_DICTIONARY);
}

static void ACEOpenSubChart(UIWidget* pWidget, void* pFakePtr)
{
	U32 uiRefIndex = PTR_TO_U32(pFakePtr);
	AnimChartDoc* pDoc = (AnimChartDoc*)emGetActiveEditorDoc();
	DynAnimSubChartRef* pRef;

	assert(pDoc);
	pRef = eaGet(&pDoc->pObject->eaSubCharts, uiRefIndex);
	assert(pRef);

	if (GET_REF(pRef->hSubChart))
		emOpenFileEx(GET_REF(pRef->hSubChart)->pcName, ANIM_CHART_EDITED_DICTIONARY);
}

void ACEOncePerFrame(AnimChartDoc* pDoc)
{
	float fR = sin( timeGetTime() / 250.0 ) / 2 + 0.5;
	ui_SkinSetBackground( &aceBadDataBackground, ColorLerp(aceBadDataColor[ 0 ], aceBadDataColor[ 1 ], fR));
	ui_SkinSetButton( &aceBadDataButton, ColorLerp(aceBadDataColor[ 1 ], aceBadDataColor[ 0 ], fR));
	ui_SkinSetEntry( &aceBadDataButton, ColorLerp(aceBadDataColor[ 1 ], aceBadDataColor[ 0 ], fR));

	if (!ui_ExpanderIsOpened(emPanelGetExpander(pDoc->pSearchPanel)))
		emPanelSetOpened(pDoc->pSearchPanel, true);
}


//---------------------------------------------------------------------------------------------------
// UI Initialization
//---------------------------------------------------------------------------------------------------

#define GRAPHS_LEFT_ADJUST 10

static void ACECreatePropsExpander(AnimChartDoc* pDoc)
{
	F32 x = GRAPHS_LEFT_ADJUST;
	F32 y = 0;
	UILabel* pLabel;
	UIButton* pButton;
	MEField* pField;
	bool bIsBaseChart = !REF_HANDLE_IS_ACTIVE(pDoc->pObject->hBaseChart);

	assert(!pDoc->pPropsExpander);
	pDoc->pPropsExpander = ACECreateExpander(pDoc->pExpanderGroup, "Properties");

	// Name
	pLabel = ui_LabelCreate("Name", 0, y);
	ui_ExpanderAddChild(pDoc->pPropsExpander, pLabel);
	pField = MEFieldCreateSimple(kMEFieldType_TextEntry, pDoc->pOrigObject, pDoc->pObject, parse_DynAnimChartLoadTime, "Name");
	ACEAddFieldToParent(pField, UI_WIDGET(pDoc->pPropsExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 21, pDoc);
	if (pDoc->pObject->pcName && aceHighlightText && strstri(pDoc->pObject->pcName,aceHighlightText))
		ui_WidgetSkin(pField->pUIWidget,&aceHighlightSkin);
	MEFieldSetChangeCallback(pField, ACESetNameCB, pDoc);
	eaPush(&pDoc->eaDocFields, pField);

	y += STANDARD_ROW_HEIGHT;

	// Scope
	pLabel = ui_LabelCreate("Scope", 0, y);
	ui_ExpanderAddChild(pDoc->pPropsExpander, pLabel);
	pField = MEFieldCreateSimpleDataProvided(kMEFieldType_TextEntry, pDoc->pOrigObject, pDoc->pObject, parse_DynAnimChartLoadTime, "Scope", NULL, &geaScopes, NULL);
	ACEAddFieldToParent(pField, UI_WIDGET(pDoc->pPropsExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 21, pDoc);
	if (pDoc->pObject->pcScope && aceHighlightText && strstri(pDoc->pObject->pcScope,aceHighlightText))
		ui_WidgetSkin(pField->pUIWidget,&aceHighlightSkin);
	MEFieldSetChangeCallback(pField, ACESetScopeCB, pDoc);
	eaPush(&pDoc->eaDocFields, pField);

	y += STANDARD_ROW_HEIGHT;

	// File Name
	pLabel = ui_LabelCreate("File Name", 0, y);
	ui_ExpanderAddChild(pDoc->pPropsExpander, pLabel);
	pDoc->pFileButton = ui_GimmeButtonCreate(X_OFFSET_CONTROL, y, ANIM_CHART_EDITED_DICTIONARY, pDoc->pObject->pcName, pDoc->pObject);
	ui_ExpanderAddChild(pDoc->pPropsExpander, pDoc->pFileButton);
	pLabel = ui_LabelCreate(pDoc->pObject->pcFilename, X_OFFSET_CONTROL+20, y);
	if (pDoc->pObject->pcFilename && aceHighlightText && strstri(pDoc->pObject->pcFilename,aceHighlightText))
		ui_LabelSetFont(pLabel, RefSystem_ReferentFromString(g_ui_FontDict,"AnimChartEditor_Highlighted"));
	ui_ExpanderAddChild(pDoc->pPropsExpander, pLabel);
	ui_WidgetSetWidthEx(UI_WIDGET(pLabel), 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(UI_WIDGET(pLabel), 0, 21, 0, 0);
	pDoc->pFilenameLabel = pLabel;

	y += STANDARD_ROW_HEIGHT;

	// Comments
	pLabel = ui_LabelCreate("Comments", 0, y);
	ui_ExpanderAddChild(pDoc->pPropsExpander, pLabel);
	pField = MEFieldCreateSimple(kMEFieldType_MultiText, pDoc->pOrigObject, pDoc->pObject, parse_DynAnimChartLoadTime, "Comments");
	ACEAddFieldToParent(pField, UI_WIDGET(pDoc->pPropsExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 21, pDoc);
	if (pDoc->pObject->pcComments && aceHighlightText && strstri(pDoc->pObject->pcComments,aceHighlightText))
		ui_WidgetSkin(pField->pUIWidget,&aceHighlightSkin);
	eaPush(&pDoc->eaDocFields, pField);

	y += STANDARD_ROW_HEIGHT;

	{
		UISeparator* pSeparator = ui_SeparatorCreate(UIHorizontal);
		ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
		ui_ExpanderAddChild(pDoc->pPropsExpander, pSeparator);
		y += SEPARATOR_HEIGHT;
	}

	// Subchart?
	x = GRAPHS_LEFT_ADJUST;
	pLabel = ui_LabelCreate("IsSubchart", x, y);
	ui_ExpanderAddChild(pDoc->pPropsExpander, pLabel);
	x += ui_WidgetGetWidth(UI_WIDGET(pLabel)) + 10.0f;
	pField = MEFieldCreateSimple(kMEFieldType_Check, pDoc->pOrigObject, pDoc->pObject, parse_DynAnimChartLoadTime, "IsSubChart");
	ACEAddFieldToParent(pField, UI_WIDGET(pDoc->pPropsExpander), x, y, 0, 20.0f, UIUnitFixed, 0, pDoc);
	MEFieldSetChangeCallback(pField, ACEIsSubchartCB, pDoc);
	eaPush(&pDoc->eaDocFields, pField);
	y += STANDARD_ROW_HEIGHT;


	if (!pDoc->pObject->bIsSubChart)
	{
		const char *pcButtonText;
		x = GRAPHS_LEFT_ADJUST;
		pLabel = ui_LabelCreate("Base Chart", x, y);
		ui_ExpanderAddChild(pDoc->pPropsExpander, pLabel);
		x += ui_WidgetGetWidth(UI_WIDGET(pLabel)) + 10;
		pcButtonText = GET_REF(pDoc->pObject->hBaseChart) ? REF_STRING_FROM_HANDLE(pDoc->pObject->hBaseChart) : "Root Chart";
		pButton = ui_ButtonCreate(pcButtonText, x, y, ACEChooseChart, NULL);
		if (pcButtonText && aceHighlightText && strstri(pcButtonText,aceHighlightText))
			ui_WidgetSkin(UI_WIDGET(pButton),&aceHighlightSkin);
		if (!pDoc->pOrigObject || (GET_REF(pDoc->pOrigObject->hBaseChart) != GET_REF(pDoc->pObject->hBaseChart)))
			ui_SetChanged(UI_WIDGET(pButton), true);
		ui_ExpanderAddChild(pDoc->pPropsExpander, pButton);

		y += STANDARD_ROW_HEIGHT;

		{
			UISeparator* pSeparator = ui_SeparatorCreate(UIHorizontal);
			ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
			ui_ExpanderAddChild(pDoc->pPropsExpander, pSeparator);
			y += SEPARATOR_HEIGHT;
		}

		//Bone Vis Sets
		x = 0;
		pLabel = ui_LabelCreate("Default Bone Visibility Set", x, y);
		ui_ExpanderAddChild(pDoc->pPropsExpander, pLabel);
		x += ui_WidgetGetWidth(UI_WIDGET(pLabel)) + 10.0f;
		pField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pDoc->pOrigObject, pDoc->pObject, parse_DynAnimChartLoadTime, "BoneVisSet", SkelBoneVisibilitySetEnum);
		ACEAddFieldToParent(pField, UI_WIDGET(pDoc->pPropsExpander), x, y, 0, 200.0f, UIUnitFixed, 0, pDoc);
		eaPush(&pDoc->eaDocFields, pField);
		y += STANDARD_ROW_HEIGHT;
	}

	ui_ExpanderSetHeight(pDoc->pPropsExpander, y);
}

// static int cmpGraphRef(const void *pa, const void *pb)
// {
// 	const DynAnimChartGraphRefLoadTime *a = *(const DynAnimChartGraphRefLoadTime **)pa;
// 	const DynAnimChartGraphRefLoadTime *b = *(const DynAnimChartGraphRefLoadTime **)pb;
// 	if (stricmp(a->pcKeyword, "Default")==0)
// 		return -1;
// 	if (stricmp(b->pcKeyword, "Default")==0)
// 		return 1;
// 	return stricmp(a->pcKeyword, b->pcKeyword);
// }

static void ACEHighlightChangedCB(UITextEntry *pEntry, UserData *pData)
{
	if (pEntry) {
		AnimChartDoc *pDoc = (AnimChartDoc*)emGetActiveEditorDoc();
		aceHighlightText = allocAddString(ui_TextEntryGetText(pEntry));
		if (!strlen(aceHighlightText))
			aceHighlightText = NULL;
		ACEUpdateDisplay(pDoc);
	}
}

static void ACEKeywordChangedCB(UITextEntry *pEntry, ACEGraphRefField *pField)
{
	if (pField->pDoc) {
		int i;
		DynAnimChartLoadTime *pObject = pField->pDoc->pObject;
		const char *pcDefaultString = allocAddString("Default");
		const char *pcNewString = allocAddString(ui_TextEntryGetText(pEntry));
		const char *pcOrigString;

		assert(pField->iIndex >= 0 && pField->iIndex < eaSize(&pObject->eaValidKeywords));
		pcOrigString = pObject->eaValidKeywords[pField->iIndex];

		if(pcNewString == pcDefaultString) {
			Alertf("Default is a reserved name.");
			ui_TextEntrySetText(pEntry, pcOrigString);
			return;
		}

		if(!pcNewString || !pcNewString[0]) {
			Alertf("Keyword can not be blank.");
			ui_TextEntrySetText(pEntry, pcOrigString);
			return;
		}

		if(pcOrigString == pcNewString)
			return;

		if(eaFind(&pObject->eaValidKeywords, pcNewString) >= 0) {
			Alertf("Can not have duplicate Keywords.");
			ui_TextEntrySetText(pEntry, pcOrigString);
			return;
		}

		for ( i=0; i < eaSize(&pObject->eaGraphRefs); i++ ) {
			DynAnimChartGraphRefLoadTime *pRef = pObject->eaGraphRefs[i];
			if(pRef->pcKeyword == pcOrigString)
				pRef->pcKeyword = pcNewString;
		}

		pObject->eaValidKeywords[pField->iIndex] = pcNewString;

		ACEFormatValidKeywords(pObject);
		ACEAnimChartChanged(pField->pDoc, true);
	}
}

static DynAnimChartGraphRefLoadTime* ACEFindGraphRef(DynAnimChartLoadTime *pChart, const char *pcKeyword, const char *pcStanceKey)
{
	int i;
	for ( i=0; i < eaSize(&pChart->eaGraphRefs); i++ ) {
		DynAnimChartGraphRefLoadTime *pRef = pChart->eaGraphRefs[i];
		if(pRef->pcKeyword == pcKeyword) {
			if(!pcStanceKey || !pcStanceKey[0]) {
				if(eaSize(&pRef->eaStanceWords) == 0)
					return pRef;
			} else {
				if(pcStanceKey == dynAnimKeyFromStanceWords(pRef->eaStanceWords))
					return pRef;
			}
		}
	}
	return NULL;
}

static int ACEFindMovementSeqIdx(DynMovementSequence **eaMoveList, const char *pcMoveType)
{
	int i;
	for ( i=0; i < eaSize(&eaMoveList); i++ ) {
		if(eaMoveList[i]->pcMovementType == pcMoveType)
			return i;
	}
	return -1;
}

static int ACEFindMovementStanceIdx(const char **eaMoveStanceList, const char *pcStance)
{
	int i;
	for ( i=0; i < eaSize(&eaMoveStanceList); i++ ) {
		if(eaMoveStanceList[i] == pcStance)
			return i;
	}
	return -1;
}

void ACEPaneTick(UIPane *pane, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(pane);
	EMEditorDoc *pEditor = emGetActiveEditorDoc();
	AnimChartDoc* pDoc = (AnimChartDoc*)pEditor;
	if(!pDoc || strcmp(pEditor->doc_type, ANIM_CHART_EDITED_DICTIONARY)!=0 || !pDoc->pMatrixExteriorScroll)
		return;

	mouseClipPushRestrict(&box);
	x -= pDoc->pMatrixExteriorScroll->widget.sb->xpos;
	w += pDoc->pMatrixExteriorScroll->widget.sb->xpos;
	ui_WidgetGroupTick(&pane->widget.children, UI_MY_VALUES);
	mouseClipPop();
}

void ACEPaneDraw(UIPane *pane, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(pane);
	EMEditorDoc *pEditor = emGetActiveEditorDoc();
	AnimChartDoc* pDoc = (AnimChartDoc*)pEditor;
	F32 col_x;
	if(!pDoc || strcmp(pEditor->doc_type, ANIM_CHART_EDITED_DICTIONARY)!=0 || !pDoc->pMatrixExteriorScroll)
		return;

	UI_DRAW_EARLY(pane);
	x -= pDoc->pMatrixExteriorScroll->widget.sb->xpos;
	w += pDoc->pMatrixExteriorScroll->widget.sb->xpos;
	for ( col_x=0; col_x < w; col_x+=REF_COL_SIZE*2 )
		display_sprite(white_tex_atlas, x+col_x, y, z, REF_COL_SIZE*scale / white_tex_atlas->width, h*scale / white_tex_atlas->height, 0x00000022);

	UI_DRAW_LATE(pane);
}

static void ACECreateStanceWordsExpander(AnimChartDoc* pDoc)
{
	int i, j;
	F32 x = GRAPHS_LEFT_ADJUST;
	F32 y = 0;
	F32 y_max = 0;
	UIButton* pButton;
	UIPane *pScrollingPane;
	UIFilteredComboBox *pFilterCombo;
	int iStanceCount = eaSize(&pDoc->pObject->eaValidStances);

	assert(!pDoc->pStanceWordsExpander);
	pDoc->pStanceWordsExpander = ACECreateExpander(pDoc->pExpanderGroup, "Stance Words");

	{
		UISeparator* pSeparator = ui_SeparatorCreate(UIHorizontal);
		ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
		ui_ExpanderAddChild(pDoc->pStanceWordsExpander, pSeparator);
		y += SEPARATOR_HEIGHT;
	}

	{
		pScrollingPane = ui_PaneCreate(ACE_MATRIX_UI_LEFT+REF_COL_SIZE, 0, REF_COL_SIZE*(iStanceCount+2), 1, UIUnitFixed, UIUnitFixed, 0);
		pScrollingPane->widget.tickF = ACEPaneTick;
		pScrollingPane->widget.drawF = ACEPaneDraw;
		pScrollingPane->invisible = true;
		ui_ExpanderAddChild(pDoc->pStanceWordsExpander, pScrollingPane);
	}

	{
		int size = eaSize(&pDoc->pObject->eaValidStances);
		x = GRAPHS_LEFT_ADJUST + (REF_COL_SIZE*size);
		pButton = ui_ButtonCreate("Add Column", x, y, ACEAddStanceKey, pDoc);
		ui_PaneAddChild(pScrollingPane, pButton);
	}

	for ( i=-1; i < eaSize(&pDoc->pObject->eaValidStances); i++ ) {
		F32 local_y = 0;
		F32 local_x = REF_COL_SIZE*(i+1);
		const char **eaStanceWords = NULL;

		if(i < 0)
			eaCopy(&eaStanceWords, &pDoc->pObject->eaStanceWords);
		else
			dynAnimStanceWordsFromKey(pDoc->pObject->eaValidStances[i], &eaStanceWords);

		local_y += STANDARD_ROW_HEIGHT/2;
		for ( j=0; j < eaSize(&eaStanceWords); j++ ) {
			const char *pcStanceWord = eaStanceWords[j];
			if(i<0)
				local_x = GRAPHS_LEFT_ADJUST;
			else
				local_x = GRAPHS_LEFT_ADJUST+(REF_COL_SIZE*i);
			pFilterCombo = ui_FilteredComboBoxCreate(local_x, local_y, 229, parse_DynAnimStanceData, &stance_list.eaStances, "Name");
			if(i<0) {
				if (!pDoc->pOrigObject || eaFind(&pDoc->pOrigObject->eaStanceWords, pcStanceWord) < 0)
					ui_SetChanged(UI_WIDGET(pFilterCombo), true);
			} else {
				if (!pDoc->pOrigObject || ACEGetValidStanceIndexFromKey(pDoc->pOrigObject, pDoc->pObject->eaValidStances[i]) < 0)
					ui_SetChanged(UI_WIDGET(pFilterCombo), true);
			}
			ui_ComboBoxSetSelectedCallback((UIComboBox*)pFilterCombo, ACERefStanceWordChanged, ACE_IDX_TO_PTR(i+1, j));
			ui_ComboBoxSetSelected((UIComboBox*)pFilterCombo, dynAnimStanceIndex(pcStanceWord));
			if (pcStanceWord && aceHighlightText && strstri(pcStanceWord,aceHighlightText))
				ui_WidgetSkin(UI_WIDGET(pFilterCombo),&aceHighlightSkin);
			ui_WidgetSetTooltipString(UI_WIDGET(pFilterCombo), pcStanceWord);
			if(i<0)
				ui_ExpanderAddChild(pDoc->pStanceWordsExpander, pFilterCombo);
			else
				ui_PaneAddChild(pScrollingPane, pFilterCombo);
			local_x += ui_WidgetGetWidth(UI_WIDGET(pFilterCombo)) + 5;

			pButton = ui_ButtonCreate("X", local_x, local_y, ACERefRemoveStanceWord, ACE_IDX_TO_PTR(i+1, j));
			if(i>=0 && eaSize(&eaStanceWords) <= 1) {
				ui_SetActive(UI_WIDGET(pButton), false);
				ui_WidgetSetTooltipString(UI_WIDGET(pButton), "Must have at least one stance word");	
			}
			if(i<0)
				ui_ExpanderAddChild(pDoc->pStanceWordsExpander, pButton);
			else
				ui_PaneAddChild(pScrollingPane, pButton);

			local_y += STANDARD_ROW_HEIGHT;
		}
		eaDestroy(&eaStanceWords);

		if (i>=0)
		{
			local_x = GRAPHS_LEFT_ADJUST+(REF_COL_SIZE*i);
			pButton = ui_ButtonCreate("<", local_x, local_y, ACEMoveStanceWordLeft, U32_TO_PTR((U32)(i+1)));
			ui_WidgetSetWidth(UI_WIDGET(pButton), 16);
			ui_PaneAddChild(pScrollingPane, pButton);
		}

		if(i<0)
			local_x = GRAPHS_LEFT_ADJUST;
		else
			local_x += 18;
		pButton = ui_ButtonCreate("Add Stance Word", local_x, local_y, ACERefAddStanceWord, U32_TO_PTR((U32)(i+1)));
		ui_WidgetSetWidth(UI_WIDGET(pButton), 111);
		if(i<0)
			ui_ExpanderAddChild(pDoc->pStanceWordsExpander, pButton);
		else
			ui_PaneAddChild(pScrollingPane, pButton);

		if(i>=0)
		{
			local_x += 113;
			pButton = ui_ButtonCreate("Delete Column", local_x, local_y, ACEDeleteStanceKey, U32_TO_PTR((U32)(i+1)));
			ui_WidgetSetWidth(UI_WIDGET(pButton), 101);
			if(i<0)
				ui_ExpanderAddChild(pDoc->pStanceWordsExpander, pButton);
			else
				ui_PaneAddChild(pScrollingPane, pButton);
		}

		if (i>=0)
		{
			local_x += 103;
			pButton = ui_ButtonCreate(">", local_x, local_y, ACEMoveStanceWordRight, U32_TO_PTR((U32)(i+1)));
			ui_WidgetSetWidth(UI_WIDGET(pButton), 16);
			ui_PaneAddChild(pScrollingPane, pButton);
		}

		local_y += STANDARD_ROW_HEIGHT;

		y_max = MAX(y_max, local_y);
	}
	y += y_max + STANDARD_ROW_HEIGHT/2;
	ui_WidgetSetHeight(UI_WIDGET(pScrollingPane), y - UI_WIDGET(pScrollingPane)->y);

	ui_ExpanderSetHeight(pDoc->pStanceWordsExpander, y);
}

static void ACECreateStanceWordsHeader(AnimChartDoc* pDoc, UIExpander *pExpander, UIPane *pScrollingPane, F32 y, const char *pcLabelText)
{
	int i;
	UITextEntry* pEntry;
	UILabel *pLabel;
	for ( i=-1; i < eaSize(&pDoc->pObject->eaValidStances); i++ ) {
		const char *pcStanceKey;
		F32 local_x, local_y;
		if(i<0) {
			pcStanceKey = "Base";
			local_x = ACE_MATRIX_UI_LEFT;
			local_y = UI_WIDGET(pScrollingPane)->y + y;
		} else {
			pcStanceKey = pDoc->pObject->eaValidStances[i];
			local_x = REF_COL_SIZE*i;
			local_y = y;
		}
		pEntry = ui_TextEntryCreate(pcStanceKey, GRAPHS_LEFT_ADJUST+local_x, local_y);
		if (pcStanceKey && aceHighlightText && strstri(pcStanceKey,aceHighlightText))
			ui_WidgetSkin(UI_WIDGET(pEntry),&aceHighlightSkin);
		ui_WidgetSetWidth(UI_WIDGET(pEntry), REF_COL_SIZE-GRAPHS_LEFT_ADJUST*2);
		ui_WidgetSetTooltipString(UI_WIDGET(pEntry), pcStanceKey);
		ui_SetActive(UI_WIDGET(pEntry), false);
		if(i<0)
			ui_ExpanderAddChild(pExpander, pEntry);
		else
			ui_PaneAddChild(pScrollingPane, pEntry);

		pLabel = ui_LabelCreate(pcLabelText, GRAPHS_LEFT_ADJUST+local_x, local_y+STANDARD_ROW_HEIGHT);
		if(i<0)
			ui_ExpanderAddChild(pExpander, pLabel);
		else
			ui_PaneAddChild(pScrollingPane, pLabel);
	}
}

static void ACECreateGraphEntry(F32 local_x, F32 local_y, const char *pcButtonText, const char *pcToolTip, bool bHighlight, bool bInvalid,
								UIActivationFunc graphF, UIActivationFunc openF, 
								UIActivationFunc addF, UIActivationFunc removeF, void *pUserData, 
								UIAddWidgetFunc addChildF, UIAnyWidget *pParent)
{
	UIButton *pButton;
	//assert(openF || addF);
	local_x += 10;

	pButton = ui_ButtonCreate(pcButtonText, local_x, local_y, graphF, pUserData);
	if (bHighlight)
		ui_WidgetSkin(UI_WIDGET(pButton), &aceHighlightSkin);
	if(openF && addF) {
		ui_WidgetSetWidth(UI_WIDGET(pButton), 146);
		local_x += 146+5;
	} else if ((openF || addF)) {
		ui_WidgetSetWidth(UI_WIDGET(pButton), 190);
		local_x += 190+5;
	} else {
		ui_WidgetSetWidth(UI_WIDGET(pButton), 146);
		local_x += 239;//146+5+39+5+39+5;
	}

	ui_WidgetSetTooltipString(UI_WIDGET(pButton), pcToolTip); 
	if (bInvalid)
		ui_WidgetSkin(UI_WIDGET(pButton), &aceBadDataButton);
	addChildF(pParent, pButton);

	if(openF) {
		pButton = ui_ButtonCreate("Open", local_x, local_y, openF, pUserData); 
		ui_WidgetSetWidth(UI_WIDGET(pButton), 39);
		local_x += 39+5;
		addChildF(pParent, pButton);
	}

	if(addF) {
		pButton = ui_ButtonCreate("Add", local_x, local_y, addF, pUserData); 
		ui_WidgetSetWidth(UI_WIDGET(pButton), 39);
		local_x += 39+5;
		addChildF(pParent, pButton);
	}

	if (removeF) {
		pButton = ui_ButtonCreate("X", local_x, local_y, removeF, pUserData); 
		ui_WidgetSetWidth(UI_WIDGET(pButton), 16);
		local_x += 16+5;
		addChildF(pParent, pButton);
	}
}

static void ACECreateGraphEntryBlank(DynAnimChartGraphRefLoadTime *pRef, F32 local_x, F32 local_y, void *pUserData, UIAddWidgetFunc addChildF, UIAnyWidget *pParent)
{
	UIButton *pButton;
	U32 uiRefIdx, uiKeywordIdx, uiStanceIdx;
	ACE_PTR_TO_IDX3(uiRefIdx, uiKeywordIdx, uiStanceIdx, pUserData);

	pButton = ui_ButtonCreate("SKIP", local_x+10, local_y, ACERemoveGraph, ACE_IDX_TO_PTR(uiRefIdx, 0));
	if (aceHighlightText && strstri("SKIP",aceHighlightText))
		ui_WidgetSkin(UI_WIDGET(pButton), &aceHighlightSkin);
	ui_WidgetSetWidth(UI_WIDGET(pButton), 146);
	ui_WidgetSetTooltipString(UI_WIDGET(pButton), "Do not apply any animation / Click to toggle state"); 
	addChildF(pParent, pButton);

	pButton = ui_ButtonCreate("Add", local_x+205, local_y, ACERemoveAndAddGraphRef, pUserData); 
	ui_WidgetSetWidth(UI_WIDGET(pButton), 39);
	ui_WidgetSetTooltipString(UI_WIDGET(pButton), "Assign an animation graph"); 
	addChildF(pParent, pButton);
}

static void ACECreateGraphEntryRandom(DynAnimChartGraphRefLoadTime *pRef, F32 local_x, F32 local_y, bool bHighlight, void *pUserData, UIAddWidgetFunc addChildF, UIAnyWidget *pParent)
{
	int j;
	char pcButtonText[256];
	char pcToolTip[256];
	bool bInvalid = false;

	pcButtonText[0] = 0;
	pcToolTip[0] = 0;

	sprintf(pcButtonText, "Random (x%d)...", eaSize(&pRef->eaGraphChances));
	for ( j=0; j < eaSize(&pRef->eaGraphChances); j++ ) {
		DynAnimGraphChanceRef *pChance = pRef->eaGraphChances[j];
		const char* pcGraphName = GET_REF(pChance->hGraph)?REF_HANDLE_GET_STRING(pChance->hGraph):NULL;
		if(j!=0)
			strcat(pcToolTip, " / ");
		if(pcGraphName) {
			strcat(pcToolTip, pcGraphName);
		} else {
			strcat(pcToolTip, "Invalid");
			bInvalid = true;
		}
	}

	ACECreateGraphEntry(local_x, local_y, pcButtonText, pcToolTip, bHighlight, bInvalid, 
						ACEOpenRandomGraphWindow, NULL, ACEGraphAddAdditional, ACERemoveGraph, pUserData, addChildF, pParent);
}

static void ACECreateGraphEntryNormal(DynAnimChartGraphRefLoadTime *pRef, F32 local_x, F32 local_y, bool bHighlight, void *pUserData, UIAddWidgetFunc addChildF, UIAnyWidget *pParent)
{
	DynAnimGraphChanceRef *pChance = NULL;
	const char *pcButtonText;
	bool bInvalid = false;

	if(eaSize(&pRef->eaGraphChances)==1)
		pChance = pRef->eaGraphChances[0];

	if (pChance && GET_REF(pChance->hGraph)) {
		pcButtonText = REF_STRING_FROM_HANDLE(pChance->hGraph);
	} else {
		pcButtonText = "Invalid Graph";
		bInvalid = true;
	}

	ACECreateGraphEntry(local_x, local_y, pcButtonText, pcButtonText, bHighlight, bInvalid, 
						ACEChooseGraph, ACEOpenGraph, ACEGraphAddAdditional, ACERemoveGraph, pUserData, addChildF, pParent);
}

static void ACECreateGraphsExpander(AnimChartDoc* pDoc)
{
	int i, j;
	F32 x = GRAPHS_LEFT_ADJUST;
	F32 y = 0;
	F32 y_top = 0;
	F32 y_default_offset = 0;
	UILabel* pLabel;
	UITextEntry *pEntry;
	UIButton* pButton;
	UIPane *pScrollingPane;
	bool bIsBaseChart = !REF_HANDLE_IS_ACTIVE(pDoc->pObject->hBaseChart);
	bool *pMatrix = NULL;
	int iKeywordCount = eaSize(&pDoc->pObject->eaValidKeywords);
	int iStanceCount = eaSize(&pDoc->pObject->eaValidStances);
	const char *pcDefaultString = allocAddString("Default");
	const char **eaMovementStances = NULL;

	if(pDoc->pObject->bIsSubChart)
		bIsBaseChart = false;

	assert(!pDoc->pGraphsExpander);
	if(bIsBaseChart)
		pDoc->pGraphsExpander = ACECreateExpander(pDoc->pExpanderGroup, "Default");
	else
		pDoc->pGraphsExpander = ACECreateExpander(pDoc->pExpanderGroup, "Graphs");
		
	{
		UISeparator* pSeparator = ui_SeparatorCreate(UIHorizontal);
		ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
		ui_ExpanderAddChild(pDoc->pGraphsExpander, pSeparator);
		y += SEPARATOR_HEIGHT;
	}

	{
		pScrollingPane = ui_PaneCreate(ACE_MATRIX_UI_LEFT+REF_COL_SIZE, 0, REF_COL_SIZE*(iStanceCount+2), 1, UIUnitFixed, UIUnitFixed, 0);
		pScrollingPane->widget.tickF = ACEPaneTick;
		pScrollingPane->widget.drawF = ACEPaneDraw;
		pScrollingPane->invisible = true;
		ui_ExpanderAddChild(pDoc->pGraphsExpander, pScrollingPane);
	}

	{
		ACECreateStanceWordsHeader(pDoc, pDoc->pGraphsExpander, pScrollingPane, y, "Graph");
		y += STANDARD_ROW_HEIGHT;
	}

	{
		UISeparator *pSeparator;

		pLabel = ui_LabelCreate("Keyword", GRAPHS_LEFT_ADJUST, y);
		ui_ExpanderAddChild(pDoc->pGraphsExpander, pLabel);
		y += STANDARD_ROW_HEIGHT;

		pSeparator = ui_SeparatorCreate(UIHorizontal);
		ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y+SEPARATOR_HEIGHT/2.0f);
		ui_ExpanderAddChild(pDoc->pGraphsExpander, pSeparator);
		y += SEPARATOR_HEIGHT;
	}
	y_top = y;

	if (eaFind(&pDoc->pObject->eaValidKeywords, pcDefaultString) >= 0) {
		DynAnimChartLoadTime* pBaseChart = GET_REF(pDoc->pObject->hBaseChart);
		DynMovementSet* pMovementSet = pBaseChart?GET_REF(pBaseChart->hMovementSet):GET_REF(pDoc->pObject->hMovementSet);
		if (pMovementSet)
			eaMovementStances = pMovementSet->eaMovementStances;
		iKeywordCount += eaSize(&eaMovementStances);
		for ( i=-1; i < eaSize(&eaMovementStances); i++ ) {
			F32 local_y = y + y_default_offset;
			UISeparator *pSeparator;
			char buf[256];
			sprintf(buf, "Default: %s", (i<0 ? ACE_NO_STANCE_STRING : eaMovementStances[i]));

			if(i<0) {
				pButton = ui_ButtonCreate("X", GRAPHS_LEFT_ADJUST, local_y, ACEDeleteKeyword, U32_TO_PTR((U32)0)); 
				ui_WidgetSetWidth(UI_WIDGET(pButton), 16);
				ui_ExpanderAddChild(pDoc->pGraphsExpander, pButton);
			}

			pEntry = ui_TextEntryCreate(buf, GRAPHS_LEFT_ADJUST+21, local_y);
			if (buf && aceHighlightText && strstri(buf,aceHighlightText))
				ui_WidgetSkin(UI_WIDGET(pEntry),&aceHighlightSkin);
			ui_WidgetSetWidth(UI_WIDGET(pEntry), 179);
			ui_SetActive(UI_WIDGET(pEntry), false);
			ui_ExpanderAddChild(pDoc->pGraphsExpander, pEntry);

			pSeparator = ui_SeparatorCreate(UIHorizontal);
			ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, local_y+STANDARD_ROW_HEIGHT+SEPARATOR_HEIGHT/2.0f);
			ui_ExpanderAddChild(pDoc->pGraphsExpander, pSeparator);

			y_default_offset += STANDARD_ROW_HEIGHT+SEPARATOR_HEIGHT;
		}
	}
	y += y_default_offset;

	for ( i=0; i < eaSize(&pDoc->pObject->eaValidKeywords); i++ ) {
		const char *pcKeyword = pDoc->pObject->eaValidKeywords[i];
		const char *pcOrigKeyword = pDoc->pOrigObject?eaGet(&pDoc->pOrigObject->eaValidKeywords, i):NULL;
		UISeparator *pSeparator;

		pSeparator = ui_SeparatorCreate(UIHorizontal);
		ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y+STANDARD_ROW_HEIGHT+SEPARATOR_HEIGHT/2.0f);
		ui_ExpanderAddChild(pDoc->pGraphsExpander, pSeparator);

		if(pcDefaultString == pcKeyword) {
			assert(i==0);
			continue;
		}

		{
			pButton = ui_ButtonCreate("X", GRAPHS_LEFT_ADJUST, y, ACEDeleteKeyword, U32_TO_PTR((U32)i)); 
			ui_WidgetSetWidth(UI_WIDGET(pButton), 16);
			ui_ExpanderAddChild(pDoc->pGraphsExpander, pButton);

			pButton = ui_ButtonCreate("^", GRAPHS_LEFT_ADJUST+21, y, ACEMoveKeywordUp, U32_TO_PTR((U32)i));
			ui_WidgetSetWidth(UI_WIDGET(pButton), 16);
			ui_ExpanderAddChild(pDoc->pGraphsExpander, pButton);

			pButton = ui_ButtonCreate("v", GRAPHS_LEFT_ADJUST+42, y, ACEMoveKeywordDown, U32_TO_PTR((U32)i));
			ui_WidgetSetWidth(UI_WIDGET(pButton), 16);
			ui_ExpanderAddChild(pDoc->pGraphsExpander, pButton);
		}

		{
			ACEGraphRefField* pGraphRefField = calloc(sizeof(ACEGraphRefField), 1);
			pEntry = ui_TextEntryCreate(pcKeyword, GRAPHS_LEFT_ADJUST+63, y);
			if (pcKeyword && aceHighlightText && strstri(pcKeyword,aceHighlightText))
				ui_WidgetSkin(UI_WIDGET(pEntry),&aceHighlightSkin);
			ui_TextEntrySetFinishedCallback(pEntry, ACEKeywordChangedCB, pGraphRefField);
			x = GRAPHS_LEFT_ADJUST;
			ui_ExpanderAddChild(pDoc->pGraphsExpander, pEntry);
			ui_WidgetSetWidth(UI_WIDGET(pEntry), 179);
			ui_SetChanged(UI_WIDGET(pEntry), (!pcOrigKeyword || pcOrigKeyword != pcKeyword));
			pGraphRefField->pDoc = pDoc;
			pGraphRefField->pEntry = pEntry;
			pGraphRefField->iIndex = i;
			eaPush(&pDoc->eaGraphRefFields, pGraphRefField);
		}

		y += STANDARD_ROW_HEIGHT+SEPARATOR_HEIGHT;
	}
	y += SEPARATOR_HEIGHT/2.0f;
	ui_WidgetSetHeight(UI_WIDGET(pScrollingPane), y - UI_WIDGET(pScrollingPane)->y);


	pMatrix = calloc(iKeywordCount * (iStanceCount+1), sizeof(bool));
	FOR_EACH_IN_EARRAY_FORWARDS(pDoc->pObject->eaGraphRefs, DynAnimChartGraphRefLoadTime, pRef)
	{
		F32 local_x, local_y;
		int iKeywordIdx = eaFind(&pDoc->pObject->eaValidKeywords, pRef->pcKeyword);
		int iStanceIdx = ACEGetValidStanceIndex(pDoc->pObject, pRef->eaStanceWords);
		int iMoveStanceIdx = eaFind(&eaMovementStances, pRef->pcMovementStance) + 1;
		UIAddWidgetFunc addF;
		UIAnyWidget *pParent;
		assert(iKeywordIdx >= 0 && (iStanceIdx >= 0 || eaSize(&pRef->eaStanceWords)==0));
		if(eaSize(&pRef->eaStanceWords)==0)
			iStanceIdx = 0;
		else
			iStanceIdx++;

		if(iStanceIdx == 0) {
			local_x = ACE_MATRIX_UI_LEFT;
			local_y = UI_WIDGET(pScrollingPane)->y + y_top + (STANDARD_ROW_HEIGHT+SEPARATOR_HEIGHT)*iKeywordIdx;
			addF = ui_ExpanderAddChild;
			pParent = pDoc->pGraphsExpander;
		} else {
			local_x = REF_COL_SIZE*(iStanceIdx-1);
			local_y = y_top + (STANDARD_ROW_HEIGHT+SEPARATOR_HEIGHT)*iKeywordIdx;
			addF = ui_PaneAddChild;
			pParent = pScrollingPane;
		}

		if(pRef->pcKeyword == pcDefaultString) {
			assert(iKeywordIdx == 0);
			local_y += (STANDARD_ROW_HEIGHT+SEPARATOR_HEIGHT)*iMoveStanceIdx;
			iKeywordIdx += iMoveStanceIdx;
		} else {
			if(y_default_offset)
				local_y += y_default_offset - (STANDARD_ROW_HEIGHT+SEPARATOR_HEIGHT);
			iKeywordIdx += eaSize(&eaMovementStances);
		}
		pMatrix[iKeywordIdx+(iStanceIdx*iKeywordCount)] = true;

		{
			bool bHighlight = false;
			if (pRef->bBlank) {
				ACECreateGraphEntryBlank(pRef, local_x, local_y, ACE_IDX3_TO_PTR((U32)ipRefIndex, (U32)iKeywordIdx, (U32)iStanceIdx), addF, pParent);
			} else if(eaSize(&pRef->eaGraphChances) > 1) {
				FOR_EACH_IN_EARRAY(pRef->eaGraphChances, DynAnimGraphChanceRef, pChanceRef)
					if (GET_REF(pChanceRef->hGraph) && aceHighlightText && strstri(REF_HANDLE_GET_STRING(pChanceRef->hGraph),aceHighlightText))
						bHighlight = true;
				FOR_EACH_END;
				ACECreateGraphEntryRandom(pRef, local_x, local_y, bHighlight, ACE_IDX_TO_PTR((U32)ipRefIndex, 0), addF, pParent);
			} else {
				if (eaSize(&pRef->eaGraphChances) == 1 &&
					GET_REF(pRef->eaGraphChances[0]->hGraph) &&
					aceHighlightText &&
					strstri(REF_HANDLE_GET_STRING(pRef->eaGraphChances[0]->hGraph),aceHighlightText))
					bHighlight = true;
				ACECreateGraphEntryNormal(pRef, local_x, local_y, bHighlight, ACE_IDX_TO_PTR((U32)ipRefIndex, 0), addF, pParent);
			}
		}
	}
	FOR_EACH_END;

	for ( j=0; j < iStanceCount+1; j++ ) {
		for ( i=0; i < iKeywordCount; i++ ) {
			if(!pMatrix[i+(j*iKeywordCount)]) {
				const char *pcKeywordCheck = NULL;
				if (eaFind(&pDoc->pObject->eaValidKeywords, pcDefaultString) >= 0) {
					DynAnimChartLoadTime* pBaseChart = GET_REF(pDoc->pObject->hBaseChart);
					DynMovementSet* pMovementSet = pBaseChart?GET_REF(pBaseChart->hMovementSet):GET_REF(pDoc->pObject->hMovementSet);
					const char **eaChkMovementStances = NULL;
					if (pMovementSet)	
						eaChkMovementStances = pMovementSet->eaMovementStances;
					if(i < eaSize(&eaChkMovementStances)+1) {
						pcKeywordCheck = pcDefaultString;
					} else {
						pcKeywordCheck = pDoc->pObject->eaValidKeywords[i - eaSize(&eaChkMovementStances)];
					}
				} else {
					pcKeywordCheck = pDoc->pObject->eaValidKeywords[i];
				}
				if (strstr(pcKeywordCheck,"***") == NULL)
				{
					F32 local_x, local_y;
					if(j == 0) {
						local_x = ACE_MATRIX_UI_LEFT;
						local_y = UI_WIDGET(pScrollingPane)->y + y_top + (STANDARD_ROW_HEIGHT+SEPARATOR_HEIGHT)*i;
					} else {
						local_x = REF_COL_SIZE*(j-1);
						local_y = y_top + (STANDARD_ROW_HEIGHT+SEPARATOR_HEIGHT)*i;				
					}

					pButton = ui_ButtonCreate("FALLBACK", local_x+10, local_y, ACEAddBlankGraphRef, ACE_IDX_TO_PTR(i,j));
					if (aceHighlightText && strstri("FALLBACK",aceHighlightText))
						ui_WidgetSkin(UI_WIDGET(pButton), &aceHighlightSkin);
					ui_WidgetSetWidth(UI_WIDGET(pButton), 146);
					ui_WidgetSetTooltipString(UI_WIDGET(pButton), "Use the graph with next highest stance priority instead / Click to toggle state"); 
					if (j == 0)
						ui_ExpanderAddChild(pDoc->pGraphsExpander, pButton);
					else
						ui_PaneAddChild(pScrollingPane, pButton);

					pButton = ui_ButtonCreate("Add", local_x+205, local_y, ACEAddGraphRef, ACE_IDX_TO_PTR(i, j)); 
					ui_WidgetSetWidth(UI_WIDGET(pButton), 39);
					ui_WidgetSetTooltipString(UI_WIDGET(pButton), "Assign an animation graph"); 
					if(j == 0)
						ui_ExpanderAddChild(pDoc->pGraphsExpander, pButton);
					else
						ui_PaneAddChild(pScrollingPane, pButton);
				}
			}
		}
	}
	free(pMatrix);
	pMatrix = NULL;

	{
		bool bHasDefault=false;
		x = GRAPHS_LEFT_ADJUST;

		if(!bIsBaseChart) {
			pButton = ui_ButtonCreate("Add Keyword", x, y, ACEAddKeyword, pDoc);
			ui_ExpanderAddChild(pDoc->pGraphsExpander, pButton);
			x += ui_WidgetGetWidth(UI_WIDGET(pButton)) + 5;
		}

		bHasDefault = (eaFind(&pDoc->pObject->eaValidKeywords, pcDefaultString) >= 0);
		if (!bHasDefault && !pDoc->pObject->bIsSubChart)
		{
			pButton = ui_ButtonCreate("Add Default", x, y, ACEAddDefaultKeyword, pDoc);
			ui_ExpanderAddChild(pDoc->pGraphsExpander, pButton);
			x += ui_WidgetGetWidth(UI_WIDGET(pButton)) + 5;
		}
		if(x != GRAPHS_LEFT_ADJUST)
			y += STANDARD_ROW_HEIGHT;
	}

	ui_ExpanderSetHeight(pDoc->pGraphsExpander, y);
}

static void ACEChartGetAllStanceWords(DynAnimChartLoadTime *pChart, char pcList[MAX_CHAR_BUFFER])
{
	int i, j;
	const char **eaWords = NULL;
	for ( i=0; i < eaSize(&pChart->eaStanceWords); i++ ) {
		eaPushUnique(&eaWords, pChart->eaStanceWords[i]);
	}
	for ( i=0; i < eaSize(&pChart->eaValidStances); i++ ) {
		const char **eaValidWords = NULL;
		dynAnimStanceWordsFromKey(pChart->eaValidStances[i], &eaValidWords);
		for ( j=0; j < eaSize(&eaValidWords); j++ ) {
			eaPushUnique(&eaWords, eaValidWords[j]);
		}
	}
	strcpy_s(pcList, MAX_CHAR_BUFFER, "Uses Stance Words: ");
	if(eaSize(&eaWords) == 0) {
		strcat_s(pcList, MAX_CHAR_BUFFER, "None");
	} else {
		for ( i=0; i < eaSize(&eaWords); i++ ) {
			if(i>0) 
				strcat_s(pcList, MAX_CHAR_BUFFER, ", ");
			strcat_s(pcList, MAX_CHAR_BUFFER, eaWords[i]);
		}
	}
}

static void ACECreateSubChartExpander(AnimChartDoc* pDoc)
{
	F32 x = GRAPHS_LEFT_ADJUST;
	F32 y = 0;
	UILabel *pLabel;
	UIButton *pButton;

	assert(!pDoc->pSubChartsExpander);
	pDoc->pSubChartsExpander = ACECreateExpander(pDoc->pExpanderGroup, "Sub Charts");

	{
		UISeparator* pSeparator = ui_SeparatorCreate(UIHorizontal);
		ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
		ui_ExpanderAddChild(pDoc->pSubChartsExpander, pSeparator);
		y += SEPARATOR_HEIGHT;
	}

	FOR_EACH_IN_EARRAY_FORWARDS(pDoc->pObject->eaSubCharts, DynAnimSubChartRef, pSubChartRef)
	{
		DynAnimChartLoadTime *pChart = GET_REF(pSubChartRef->hSubChart);
		const char* pcSubchartName = REF_HANDLE_GET_STRING(pSubChartRef->hSubChart);
		x = GRAPHS_LEFT_ADJUST;
		pButton = ui_ButtonCreate("X", x, y, ACERemoveSubChart, U32_TO_PTR((U32)ipSubChartRefIndex));
		ui_ExpanderAddChild(pDoc->pSubChartsExpander, pButton);
		x += ui_WidgetGetWidth(UI_WIDGET(pButton)) + 5;
		pButton = ui_ButtonCreate("Open", x, y, ACEOpenSubChart, U32_TO_PTR((U32)ipSubChartRefIndex));
		ui_ExpanderAddChild(pDoc->pSubChartsExpander, pButton);
		x += ui_WidgetGetWidth(UI_WIDGET(pButton)) + 5;
		pLabel = ui_LabelCreate(pcSubchartName?pcSubchartName:"NOTFOUND", x, y);
		if (aceHighlightText && strstri(pcSubchartName?pcSubchartName:"NOTFOUND",aceHighlightText))
			ui_LabelSetFont(pLabel, RefSystem_ReferentFromString(g_ui_FontDict,"AnimChartEditor_Highlighted"));
		ui_ExpanderAddChild(pDoc->pSubChartsExpander, pLabel);
		y += STANDARD_ROW_HEIGHT;

		if(pChart) {
			int i, j;
			char buf[MAX_CHAR_BUFFER];

			ACEChartGetAllStanceWords(pChart, buf);
			pLabel = ui_LabelCreate(buf, GRAPHS_LEFT_ADJUST+21, y);
			if (buf && aceHighlightText && strstri(buf,aceHighlightText))
				ui_LabelSetFont(pLabel, RefSystem_ReferentFromString(g_ui_FontDict,"AnimChartEditor_Highlighted"));
			ui_ExpanderAddChild(pDoc->pSubChartsExpander, pLabel);
			y += STANDARD_ROW_HEIGHT;

			for ( i=0; i < eaSize(&pChart->eaValidKeywords); i++ ) {
				DynAnimChartGraphRefLoadTime *pRef = ACEFindGraphRef(pChart, pChart->eaValidKeywords[i], NULL);

				pLabel = ui_LabelCreate(pChart->eaValidKeywords[i], GRAPHS_LEFT_ADJUST+21, y);
				if (pChart->eaValidKeywords[i] && aceHighlightText && strstri(pChart->eaValidKeywords[i],aceHighlightText))
					ui_LabelSetFont(pLabel, RefSystem_ReferentFromString(g_ui_FontDict,"AnimChartEditor_Highlighted"));
				ui_ExpanderAddChild(pDoc->pSubChartsExpander, pLabel);

				if(pRef && eaSize(&pRef->eaGraphChances) > 0) {
					sprintf(buf, "-    ");
					for ( j=0; j < eaSize(&pRef->eaGraphChances); j++ ) {
						const char* pcGraphName = pRef?REF_HANDLE_GET_STRING(pRef->eaGraphChances[j]->hGraph):NULL;
						if(j!=0)
							strcat(buf, " / ");
						strcat(buf, pcGraphName?pcGraphName:"Missing");
					}
				} else {
					sprintf(buf, "-    Missing");
				}
				pLabel = ui_LabelCreate(buf, ACE_MATRIX_UI_LEFT, y);
				if (buf && aceHighlightText && strstri(buf,aceHighlightText))
					ui_LabelSetFont(pLabel, RefSystem_ReferentFromString(g_ui_FontDict,"AnimChartEditor_Highlighted"));
				ui_ExpanderAddChild(pDoc->pSubChartsExpander, pLabel);

				y += STANDARD_ROW_HEIGHT;
			}
		}
	}
	FOR_EACH_END;

	{
		x = GRAPHS_LEFT_ADJUST;
		pButton = ui_ButtonCreate("Add SubChart", x, y, ACEAddSubChart, pDoc);
		ui_ExpanderAddChild(pDoc->pSubChartsExpander, pButton);
		y += STANDARD_ROW_HEIGHT;
	}

	ui_ExpanderSetHeight(pDoc->pSubChartsExpander, y);
}

static void ACECreateMovesExpander(AnimChartDoc* pDoc)
{
	int i, j, k;
	F32 x = GRAPHS_LEFT_ADJUST;
	F32 y = 0;
	F32 y_top = 0;
	UILabel* pLabel;
	UIButton* pButton = NULL;
	bool bIsBaseChart = !REF_HANDLE_IS_ACTIVE(pDoc->pObject->hBaseChart);
	int iStanceCount = eaSize(&pDoc->pObject->eaValidStances) + 1;
	const char **eaMoveStanceList = NULL;
	DynMovementSequence **eaMoveSeqList = NULL;
	int iMoveStanceCnt = 0;
	int iMoveSeqCnt = 0;
	UIPane *pScrollingPane;
	bool *pMatrix = NULL;

	if (pDoc->pObject->bIsSubChart)
		return;

	assert(!pDoc->pMovesExpander);
	pDoc->pMovesExpander = ACECreateExpander(pDoc->pExpanderGroup, "Moves");

	if (bIsBaseChart)
	{
		const char *pcButtonText;
		pLabel = ui_LabelCreate("Movement Set:", x, y);
		ui_ExpanderAddChild(pDoc->pMovesExpander, pLabel);
		x += ui_WidgetGetWidth(UI_WIDGET(pLabel)) + 10;
		pcButtonText = GET_REF(pDoc->pObject->hMovementSet) ? REF_STRING_FROM_HANDLE(pDoc->pObject->hMovementSet) : "No Movement Set";
		pButton = ui_ButtonCreate(pcButtonText, x, y, ACEChooseMovementSet, NULL);
		if (pcButtonText && aceHighlightText && strstri(pcButtonText,aceHighlightText))
			ui_WidgetSkin(UI_WIDGET(pButton), &aceHighlightSkin);
		if (!pDoc->pOrigObject || (GET_REF(pDoc->pOrigObject->hMovementSet) != GET_REF(pDoc->pObject->hMovementSet)))
			ui_SetChanged(UI_WIDGET(pButton), true);
		ui_ExpanderAddChild(pDoc->pMovesExpander, pButton);

		y += STANDARD_ROW_HEIGHT;
	}
	y_top = y;

	{
		UISeparator* pSeparator = ui_SeparatorCreate(UIHorizontal);
		ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
		ui_ExpanderAddChild(pDoc->pMovesExpander, pSeparator);
		y += SEPARATOR_HEIGHT;
	}

	{
		pScrollingPane = ui_PaneCreate(ACE_MATRIX_UI_LEFT+REF_COL_SIZE, y_top, REF_COL_SIZE*(iStanceCount+1), 1, UIUnitFixed, UIUnitFixed, 0);
		pScrollingPane->widget.tickF = ACEPaneTick;
		pScrollingPane->widget.drawF = ACEPaneDraw;
		pScrollingPane->invisible = true;
		ui_ExpanderAddChild(pDoc->pMovesExpander, pScrollingPane);
	}

	{
		ACECreateStanceWordsHeader(pDoc, pDoc->pMovesExpander, pScrollingPane, y-y_top, "Move");
		y += STANDARD_ROW_HEIGHT;
	}

	{
		x = GRAPHS_LEFT_ADJUST;
		pLabel = ui_LabelCreate("Movement Type", x, y);
		ui_ExpanderAddChild(pDoc->pMovesExpander, pLabel);
		y += STANDARD_ROW_HEIGHT;
	}
	y_top = y-y_top;

	{
		DynAnimChartLoadTime* pBaseChart = GET_REF(pDoc->pObject->hBaseChart);
		DynMovementSet* pMovementSet = pBaseChart?GET_REF(pBaseChart->hMovementSet):GET_REF(pDoc->pObject->hMovementSet);
		if (pMovementSet) {
			eaMoveSeqList = pMovementSet->eaMovementSequences;
			eaMoveStanceList = pMovementSet->eaMovementStances;
			iMoveSeqCnt = eaSize(&eaMoveSeqList);
			iMoveStanceCnt = eaSize(&eaMoveStanceList) + 1;

			for ( j=-1; j < eaSize(&eaMoveStanceList); j++ ) {
				
				UISeparator* pSeparator;

				x = GRAPHS_LEFT_ADJUST;

				pLabel = ui_LabelCreate(j<0 ? ACE_NO_STANCE_STRING : eaMoveStanceList[j], x, y);
				if (aceHighlightText && strstri(j<0?ACE_NO_STANCE_STRING:eaMoveStanceList[j],aceHighlightText))
					ui_LabelSetFont(pLabel, RefSystem_ReferentFromString(g_ui_FontDict,"AnimChartEditor_Highlighted"));
				ui_ExpanderAddChild(pDoc->pMovesExpander, pLabel);
				y += STANDARD_ROW_HEIGHT;

				pSeparator = ui_SeparatorCreate(UIHorizontal);
				ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
				ui_ExpanderAddChild(pDoc->pMovesExpander, pSeparator);
				y += SEPARATOR_HEIGHT/4.0f;

				for ( i=0; i < eaSize(&eaMoveSeqList); i++ ) {
					
					pSeparator = ui_SeparatorCreate(UIHorizontal);
					ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
					ui_ExpanderAddChild(pDoc->pMovesExpander, pSeparator);
					y += SEPARATOR_HEIGHT/2.0f;

					x = ACE_INDENT_UI_LEFT;
					pLabel = ui_LabelCreate(eaMoveSeqList[i]->pcMovementType, x, y);
					if (eaMoveSeqList[i]->pcMovementType && aceHighlightText && strstri(eaMoveSeqList[i]->pcMovementType,aceHighlightText))
						ui_LabelSetFont(pLabel, RefSystem_ReferentFromString(g_ui_FontDict,"AnimChartEditor_Highlighted"));
					ui_ExpanderAddChild(pDoc->pMovesExpander, pLabel);
					y += STANDARD_ROW_HEIGHT + SEPARATOR_HEIGHT/2.0f;

				}

				pSeparator = ui_SeparatorCreate(UIHorizontal);
				ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
				ui_ExpanderAddChild(pDoc->pMovesExpander, pSeparator);
				y += STANDARD_ROW_HEIGHT;

			}
		}
	}
	y += SEPARATOR_HEIGHT/2.0f;
	ui_WidgetSetHeight(UI_WIDGET(pScrollingPane), y - UI_WIDGET(pScrollingPane)->y);

	if(iMoveSeqCnt > 0)
		pMatrix = calloc(iMoveStanceCnt * iMoveSeqCnt * iStanceCount, sizeof(bool));
	FOR_EACH_IN_EARRAY_FORWARDS(pDoc->pObject->eaMoveRefs, DynAnimChartMoveRefLoadTime, pRef)
	{
		F32 local_x, local_y;
		int iMoveStanceIdx = ACEFindMovementStanceIdx(eaMoveStanceList, pRef->pcMovementStance) + 1;
		int iMoveSeqIdx = ACEFindMovementSeqIdx(eaMoveSeqList, pRef->pcMovementType);
		int iStanceColIdx = ACEGetValidStanceIndex(pDoc->pObject, pRef->eaStanceWords);
		DynAnimChartMoveRefLoadTime* pOldRef = pDoc->pOrigObject?eaGet(&pDoc->pOrigObject->eaMoveRefs, ipRefIndex):NULL;
		const char *pcButtonText;
		bool bMissingRef = false;

		assert(iMoveSeqIdx >= 0 && (iStanceColIdx >= 0 || eaSize(&pRef->eaStanceWords)==0));
		if(eaSize(&pRef->eaStanceWords)==0)
			iStanceColIdx = 0;
		else
			iStanceColIdx++;

		if (!eaSize(&pRef->eaMoveChances)) {
			bMissingRef = true;
		} else {
			FOR_EACH_IN_EARRAY(pRef->eaMoveChances, DynAnimMoveChanceRef, pChanceRef) {
				if (!GET_REF(pChanceRef->hMove))
					bMissingRef = true;
			} FOR_EACH_END;
		}

		if (!pRef->bBlank && bMissingRef) {
			if(!bIsBaseChart)
				continue;
			if(iStanceColIdx > 0)
				continue;
			if(iMoveStanceIdx > 0)
				continue;
		}

		pMatrix[iMoveSeqIdx + (iMoveStanceIdx*iMoveSeqCnt) + (iStanceColIdx*iMoveStanceCnt*iMoveSeqCnt)] = true;

		if(iStanceColIdx == 0) {
			local_x = ACE_MATRIX_UI_LEFT;
			local_y = UI_WIDGET(pScrollingPane)->y + y_top + (STANDARD_ROW_HEIGHT+SEPARATOR_HEIGHT) + (2*STANDARD_ROW_HEIGHT+SEPARATOR_HEIGHT/4.0f)*iMoveStanceIdx + (STANDARD_ROW_HEIGHT+SEPARATOR_HEIGHT)*(iMoveSeqIdx + iMoveStanceIdx*iMoveSeqCnt);
		} else {
			local_x = REF_COL_SIZE*(iStanceColIdx-1);
			local_y = y_top + (STANDARD_ROW_HEIGHT+SEPARATOR_HEIGHT) + (2*STANDARD_ROW_HEIGHT+SEPARATOR_HEIGHT/4.0f)*iMoveStanceIdx + (STANDARD_ROW_HEIGHT+SEPARATOR_HEIGHT)*(iMoveSeqIdx + iMoveStanceIdx*iMoveSeqCnt);
		}

		if (!pRef->bBlank) {
			pButton = ui_ButtonCreate("T", local_x+10, local_y, ACEOpenMoveTransitionWindow, U32_TO_PTR((U32)ipRefIndex));
			ui_WidgetSetWidth(UI_WIDGET(pButton), 16);
			if (iStanceColIdx == 0) {
				ui_ExpanderAddChild(pDoc->pMovesExpander, pButton);
			}
			else {
				ui_PaneAddChild(pScrollingPane, pButton);
			}
		}

		if (pRef->bBlank)
		{
			pcButtonText = "SKIP";
			pButton = ui_ButtonCreate(pcButtonText, local_x+32, local_y, ACEClearMove, U32_TO_PTR((U32)ipRefIndex));
			if (pcButtonText && aceHighlightText && strstri(pcButtonText,aceHighlightText))
				ui_WidgetSkin(UI_WIDGET(pButton), &aceHighlightSkin);
			ui_WidgetSetTooltipString(UI_WIDGET(pButton), "Animation for this direction is re-assigned to nearest neighboring direction based on angle / click to toggle state");
			ui_WidgetSetWidth(UI_WIDGET(pButton), 125);
		}
		else if (eaSize(&pRef->eaMoveChances) > 1)
		{
			pcButtonText = "Random...";
			pButton = ui_ButtonCreate(pcButtonText, local_x+31, local_y, ACEOpenRandomMoveWindow, U32_TO_PTR((U32)ipRefIndex));
			FOR_EACH_IN_EARRAY(pRef->eaMoveChances, DynAnimMoveChanceRef, pChanceRef)
				if (GET_REF(pChanceRef->hMove) && aceHighlightText && strstri(REF_HANDLE_GET_STRING(pChanceRef->hMove),aceHighlightText))
					ui_WidgetSkin(UI_WIDGET(pButton), &aceHighlightSkin);
			FOR_EACH_END;
			ui_WidgetSetTooltipString(UI_WIDGET(pButton), pcButtonText);
			ui_WidgetSetWidth(UI_WIDGET(pButton), 125);
		}
		else if (eaSize(&pRef->eaMoveChances) == 1)
		{
			pcButtonText = GET_REF(pRef->eaMoveChances[0]->hMove)?REF_STRING_FROM_HANDLE(pRef->eaMoveChances[0]->hMove):(bIsBaseChart?"Invalid Move":"Not overriden");
			pButton = ui_ButtonCreate(pcButtonText, local_x+31, local_y, ACEChooseMove, ACE_IDX_TO_PTR(ipRefIndex,0));
			if (pcButtonText && aceHighlightText && strstri(pcButtonText,aceHighlightText))
				ui_WidgetSkin(UI_WIDGET(pButton), &aceHighlightSkin);
			ui_WidgetSetTooltipString(UI_WIDGET(pButton), pcButtonText);
			ui_WidgetSetWidth(UI_WIDGET(pButton), 125);
		}

		if (!pRef->bBlank && !eaSize(&pRef->eaMoveChances) && bIsBaseChart)
			ui_WidgetSkin(UI_WIDGET(pButton), &aceBadDataButton);
		else if (!pOldRef || pOldRef->bBlank != pRef->bBlank || eaSize(&pOldRef->eaMoveChances) != eaSize(&pRef->eaMoveChances))
			ui_SetChanged(UI_WIDGET(pButton), true);
		else if (eaSize(&pOldRef->eaMoveChances) && eaSize(&pOldRef->eaMoveChances) == eaSize(&pRef->eaMoveChances))
		{
			FOR_EACH_IN_EARRAY(pRef->eaMoveChances, DynAnimMoveChanceRef, pMoveChance) {
				if (GET_REF(pRef->eaMoveChances[ipMoveChanceIndex]->hMove) != GET_REF(pOldRef->eaMoveChances[ipMoveChanceIndex]->hMove) ||
					pRef->eaMoveChances[ipMoveChanceIndex]->fChance != pOldRef->eaMoveChances[ipMoveChanceIndex]->fChance)
				{
						ui_SetChanged(UI_WIDGET(pButton), true);
						break;
				}
			} FOR_EACH_END;
		}

		if(iStanceColIdx == 0) {
			ui_ExpanderAddChild(pDoc->pMovesExpander, pButton);
		} else {
			ui_PaneAddChild(pScrollingPane, pButton);
		}

		{
			pButton = ui_ButtonCreate("Add", local_x+205, local_y, ACEAddMoveRef, ACE_IDX3_TO_PTR(iMoveSeqIdx, iMoveStanceIdx, iStanceColIdx));
			ui_WidgetSetWidth(UI_WIDGET(pButton), 39);
			if (iStanceColIdx == 0) {
				ui_ExpanderAddChild(pDoc->pMovesExpander, pButton);
			} else {
				ui_PaneAddChild(pScrollingPane, pButton);
			}
		}

		if (!pRef->bBlank)
		{
			pButton = ui_ButtonCreate("Open", local_x+161, local_y, ACEOpenMove, U32_TO_PTR((U32)ipRefIndex)); 
			ui_WidgetSetWidth(UI_WIDGET(pButton), 39);
			if(iStanceColIdx == 0) {
				ui_ExpanderAddChild(pDoc->pMovesExpander, pButton);
			} else {
				ui_PaneAddChild(pScrollingPane, pButton);
			}

			if (!bIsBaseChart || iStanceColIdx>0 || iMoveStanceIdx > 0) {
				pButton = ui_ButtonCreate("X", local_x+249, local_y, ACEClearMove, U32_TO_PTR((U32)ipRefIndex));
				ui_WidgetSetWidth(UI_WIDGET(pButton), 16);
				if(iStanceColIdx == 0) {
					ui_ExpanderAddChild(pDoc->pMovesExpander, pButton);
				} else {
					ui_PaneAddChild(pScrollingPane, pButton);
				}
			}
		}
	}
	FOR_EACH_END;
	
	if(pMatrix) {
		for ( k=0; k < iStanceCount; k++ ) {
			for ( j=0; j < iMoveStanceCnt; j++ ) {
				for ( i=0; i < iMoveSeqCnt; i++ ) {
					if(!pMatrix[i + (j*iMoveSeqCnt) + (k*iMoveSeqCnt*iMoveStanceCnt)]) {
						F32 local_x, local_y;

						if (k==0) {
							local_x = ACE_MATRIX_UI_LEFT;
							local_y = UI_WIDGET(pScrollingPane)->y + y_top + (STANDARD_ROW_HEIGHT+SEPARATOR_HEIGHT) + (2*STANDARD_ROW_HEIGHT+SEPARATOR_HEIGHT/4.0f)*j + (STANDARD_ROW_HEIGHT+SEPARATOR_HEIGHT)*(i+j*iMoveSeqCnt);
						} else {
							local_x = REF_COL_SIZE*(k-1);
							local_y = y_top + (STANDARD_ROW_HEIGHT+SEPARATOR_HEIGHT) + (2*STANDARD_ROW_HEIGHT+SEPARATOR_HEIGHT/4.0f)*j + (STANDARD_ROW_HEIGHT+SEPARATOR_HEIGHT)*(i+j*iMoveSeqCnt);
						}

						pButton = ui_ButtonCreate("FALLBACK", local_x+32, local_y, ACEAddBlankMoveRef, ACE_IDX3_TO_PTR(i,j,k));
						ui_WidgetSetTooltipString(UI_WIDGET(pButton), "Animation for this direction is re-assigned based on the next highest priority stance / click to toggle state");
						ui_WidgetSetWidth(UI_WIDGET(pButton),125);
						if(k == 0) {
							ui_ExpanderAddChild(pDoc->pMovesExpander, pButton);
						} else {
							ui_PaneAddChild(pScrollingPane, pButton);
						}

						pButton = ui_ButtonCreate("Add", local_x+205, local_y, ACEAddMoveRef, ACE_IDX3_TO_PTR(i, j, k)); 
						ui_WidgetSetWidth(UI_WIDGET(pButton), 39);
						if(k == 0) {
							ui_ExpanderAddChild(pDoc->pMovesExpander, pButton);
						} else {
							ui_PaneAddChild(pScrollingPane, pButton);
						}

					}
				}
			}
		}
		free(pMatrix);
		pMatrix = NULL;
	}

	ui_ExpanderSetHeight(pDoc->pMovesExpander, y+STANDARD_ROW_HEIGHT);
}

static void ACECreateMoveTransitionsExpander(AnimChartDoc *pDoc)
{
	F32 x = GRAPHS_LEFT_ADJUST;
	F32 y = 0;
	UIButton *pButton;
	const char **eaNames=NULL;

	FOR_EACH_IN_REFDICT(hMoveTransitionDict, DynMoveTransition, pMoveTransition)
	{
		if (REF_HANDLE_GET_STRING(pMoveTransition->hChart) == pDoc->pObject->pcName) {
			eaPush(&eaNames, pMoveTransition->pcName);
		}
	}
	FOR_EACH_END;

	if (eaSize(&eaNames) > 0)
	{
		assert(!pDoc->pMoveTransitionsExpander);
		pDoc->pMoveTransitionsExpander = ACECreateExpander(pDoc->pExpanderGroup, "Move Transitions");

		{
			UISeparator* pSeparator = ui_SeparatorCreate(UIHorizontal);
			ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
			ui_ExpanderAddChild(pDoc->pMoveTransitionsExpander, pSeparator);
			y += SEPARATOR_HEIGHT;
		}

		if (eaSize(&eaNames))
		{
			eaQSortG(eaNames, strCmp);
			FOR_EACH_IN_EARRAY_FORWARDS(eaNames, const char, pcName)
			{
				pButton = ui_ButtonCreate(pcName, x, y, ACEOpenMoveTransition, (void*)pcName);
				if (pcName && aceHighlightText && strstri(pcName,aceHighlightText))
					ui_WidgetSkin(UI_WIDGET(pButton),&aceHighlightSkin);
				ui_ExpanderAddChild(pDoc->pMoveTransitionsExpander, pButton);

				y += STANDARD_ROW_HEIGHT;
			}
			FOR_EACH_END;
		}

		ui_ExpanderSetHeight(pDoc->pMoveTransitionsExpander, y);
	}
	
	eaDestroy(&eaNames);
}

static void ACECreateStanceChartsExpander(AnimChartDoc* pDoc)
{
	F32 x = GRAPHS_LEFT_ADJUST;
	F32 y = 0;
	UIButton* pButton;
	bool bIsBaseChart = !REF_HANDLE_IS_ACTIVE(pDoc->pObject->hBaseChart);
	const char **eaNames=NULL;

	if (pDoc->pObject->bIsSubChart || !bIsBaseChart)
		return;

	assert(!pDoc->pStanceChartsExpander);
	pDoc->pStanceChartsExpander = ACECreateExpander(pDoc->pExpanderGroup, "Stance Charts");

	{
		UISeparator* pSeparator = ui_SeparatorCreate(UIHorizontal);
		ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
		ui_ExpanderAddChild(pDoc->pStanceChartsExpander, pSeparator);
		y += SEPARATOR_HEIGHT;
	}

	FOR_EACH_IN_REFDICT(hAnimChartDictLoadTime, DynAnimChartLoadTime, pOtherChart)
	{
		if (GET_REF(pOtherChart->hBaseChart) && GET_REF(pOtherChart->hBaseChart)->pcName == pDoc->pObject->pcName)
		{
			eaPush(&eaNames, pOtherChart->pcName);
		}
	}
	FOR_EACH_END;

	if (eaSize(&eaNames))
	{
		eaQSortG(eaNames, strCmp);
		FOR_EACH_IN_EARRAY_FORWARDS(eaNames, const char, pcName)
		{
			pButton = ui_ButtonCreate(pcName, x, y, ACEOpenStanceChart, (void*)pcName);
			if (pcName && aceHighlightText && strstri(pcName,aceHighlightText))
				ui_WidgetSkin(UI_WIDGET(pButton),&aceHighlightSkin);
			ui_ExpanderAddChild(pDoc->pStanceChartsExpander, pButton);

			y += STANDARD_ROW_HEIGHT;
		}
		FOR_EACH_END;
	}
	eaDestroy(&eaNames);

	ui_ExpanderSetHeight(pDoc->pStanceChartsExpander, y);
}

static void ACERefreshPropsExpander(AnimChartDoc* pDoc)
{
	bool bWasOpen = ui_ExpanderIsOpened(pDoc->pPropsExpander);
	eaDestroyEx(&pDoc->eaDocFields, MEFieldDestroy);
	ui_ExpanderGroupRemoveExpander(pDoc->pExpanderGroup, pDoc->pPropsExpander);
	ui_WidgetForceQueueFree(UI_WIDGET(pDoc->pPropsExpander));
	pDoc->pPropsExpander = NULL;
	ACECreatePropsExpander(pDoc);
	ANALYSIS_ASSUME(pDoc->pPropsExpander != NULL);
	ui_ExpanderSetOpened(pDoc->pPropsExpander, bWasOpen);
}

static void ACERefreshStanceWordsExpander(AnimChartDoc* pDoc)
{
	bool bWasOpen = pDoc->pStanceWordsExpander ? ui_ExpanderIsOpened(pDoc->pStanceWordsExpander) : true;
	ui_ExpanderGroupRemoveExpander(pDoc->pExpanderGroup, pDoc->pStanceWordsExpander);
	ui_WidgetForceQueueFree(UI_WIDGET(pDoc->pStanceWordsExpander));
	pDoc->pStanceWordsExpander = NULL;
	ACECreateStanceWordsExpander(pDoc);
	if(pDoc->pStanceWordsExpander)
		ui_ExpanderSetOpened(pDoc->pStanceWordsExpander, bWasOpen);
}

static void ACERefreshGraphsExpander(AnimChartDoc* pDoc)
{
	bool bWasOpen = ui_ExpanderIsOpened(pDoc->pGraphsExpander);
	eaDestroyEx(&pDoc->eaGraphRefFields, ACEGraphRefFieldDestroy);
	ui_ExpanderGroupRemoveExpander(pDoc->pExpanderGroup, pDoc->pGraphsExpander);
	ui_WidgetForceQueueFree(UI_WIDGET(pDoc->pGraphsExpander));
	pDoc->pGraphsExpander = NULL;
	ACECreateGraphsExpander(pDoc);
	ANALYSIS_ASSUME(pDoc->pGraphsExpander != NULL);
	ui_ExpanderSetOpened(pDoc->pGraphsExpander, bWasOpen);
}

static void ACERefreshSubChartExpander(AnimChartDoc* pDoc)
{
	bool bWasOpen = ui_ExpanderIsOpened(pDoc->pSubChartsExpander);
	ui_ExpanderGroupRemoveExpander(pDoc->pExpanderGroup, pDoc->pSubChartsExpander);
	ui_WidgetForceQueueFree(UI_WIDGET(pDoc->pSubChartsExpander));
	pDoc->pSubChartsExpander = NULL;
	ACECreateSubChartExpander(pDoc);
	ANALYSIS_ASSUME(pDoc->pSubChartsExpander != NULL);
	ui_ExpanderSetOpened(pDoc->pSubChartsExpander, bWasOpen);
}

static void ACERefreshStanceChartsExpander(AnimChartDoc* pDoc)
{
	bool bWasOpen =  pDoc->pStanceChartsExpander ? ui_ExpanderIsOpened(pDoc->pStanceChartsExpander) : true;
	ui_ExpanderGroupRemoveExpander(pDoc->pExpanderGroup, pDoc->pStanceChartsExpander);
	ui_WidgetForceQueueFree(UI_WIDGET(pDoc->pStanceChartsExpander));
	pDoc->pStanceChartsExpander = NULL;
	ACECreateStanceChartsExpander(pDoc);
	ANALYSIS_ASSUME(pDoc->pStanceChartsExpander != NULL);
	if (pDoc->pStanceChartsExpander) ui_ExpanderSetOpened(pDoc->pStanceChartsExpander, bWasOpen);
}

static void ACERefreshMovesExpander(AnimChartDoc* pDoc)
{
	bool bWasOpen = pDoc->pMovesExpander ? ui_ExpanderIsOpened(pDoc->pMovesExpander) : true;
	ui_ExpanderGroupRemoveExpander(pDoc->pExpanderGroup, pDoc->pMovesExpander);
	ui_WidgetForceQueueFree(UI_WIDGET(pDoc->pMovesExpander));
	pDoc->pMovesExpander = NULL;
	ACECreateMovesExpander(pDoc);
	if(pDoc->pMovesExpander)
	{
		ANALYSIS_ASSUME(pDoc->pMovesExpander != NULL);
		ui_ExpanderSetOpened(pDoc->pMovesExpander, bWasOpen);
	}
}

static void ACERefreshMoveTransitionsExpander(AnimChartDoc* pDoc)
{
	bool bWasOpen = pDoc->pMoveTransitionsExpander ? ui_ExpanderIsOpened(pDoc->pMoveTransitionsExpander) : true;
	ui_ExpanderGroupRemoveExpander(pDoc->pExpanderGroup, pDoc->pMoveTransitionsExpander);
	ui_WidgetForceQueueFree(UI_WIDGET(pDoc->pMoveTransitionsExpander));
	pDoc->pMoveTransitionsExpander = NULL;
	ACECreateMoveTransitionsExpander(pDoc);
	if (pDoc->pMoveTransitionsExpander)
	{
		ANALYSIS_ASSUME(pDoc->pMoveTransitionsExpander != NULL);
		ui_ExpanderSetOpened(pDoc->pMoveTransitionsExpander, bWasOpen);
	}
}

static UIWindow* ACEInitMainWindow(AnimChartDoc* pDoc)
{
	UIWindow* pWin;
	F32 y = 0;
	F32 fBottomY = 0;
	F32 fTopY = 0;
	UIScrollArea *pScrollArea;
	// Create the window
	pWin = ui_WindowCreate(pDoc->pObject->pcName, 15, 50, 450, 600);
	EditorPrefGetWindowPosition(ANIMCHART_EDITOR, "Window Position", "Main", pWin);

	// Main expander group
	pDoc->pExpanderGroup = ui_ExpanderGroupCreate();
	ui_WidgetSetPosition(UI_WIDGET(pDoc->pExpanderGroup), 0, y);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pDoc->pExpanderGroup), 1.0, 1.0, UIUnitPercentage, UIUnitPercentage);
	ui_WindowAddChild(pWin, pDoc->pExpanderGroup);

	pScrollArea = ui_ScrollAreaCreate(0,0,100,100,200,200, true, false);
	ui_WidgetSetPositionEx(UI_WIDGET(pScrollArea), ACE_MATRIX_UI_LEFT+REF_COL_SIZE, 0, 0, 0, UIBottomLeft);
	UI_WIDGET(pScrollArea)->rightPad = ui_ScrollbarWidth(UI_WIDGET(pScrollArea)->sb);
	UI_WIDGET(pDoc->pExpanderGroup)->bottomPad = ui_ScrollbarHeight(UI_WIDGET(pScrollArea)->sb);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pScrollArea), 1.0, 1.0, UIUnitPercentage, UIUnitPercentage);
	pDoc->pMatrixExteriorScroll = pScrollArea;
	ui_WindowAddChild(pWin, pScrollArea);

	ACECreatePropsExpander(pDoc);
	ACECreateStanceWordsExpander(pDoc);
	ACECreateGraphsExpander(pDoc);
	ACECreateMovesExpander(pDoc);
	ACECreateMoveTransitionsExpander(pDoc);
	ACECreateSubChartExpander(pDoc);
	ACECreateStanceChartsExpander(pDoc);

	/*
	// Define expanders
	pDoc->pInfoExpander = IECreateExpander(pDoc->pExpanderGroup, "Information", 0);
	pDoc->pPropsExpander = IECreateExpander(pDoc->pExpanderGroup, "Properties", 1);

	// Refresh the dynamic expanders
	IERefreshInfoExpander(pDoc);
	IERefreshPropsExpander(pDoc);
	*/

	return pWin;
}



static void ACEInitDisplay(EMEditor* pEditor, AnimChartDoc* pDoc)
{
	// Create the window (ignore field change callbacks during init)
	pDoc->bIgnoreFieldChanges = true;
	pDoc->bIgnoreFilenameChanges = true;
	pDoc->pMainWindow = ACEInitMainWindow(pDoc);
	pDoc->pSearchPanel = ACEInitSearchPanel(pDoc);
	pDoc->bIgnoreFieldChanges = false;
	pDoc->bIgnoreFilenameChanges = false;

	// Show the window
	ui_WindowPresent(pDoc->pMainWindow);

	// Editor Manager needs to be told about the windows used
	pDoc->emDoc.primary_ui_window = pDoc->pMainWindow;
	eaPush(&pDoc->emDoc.ui_windows, pDoc->pMainWindow);

	eaPush(&pDoc->emDoc.em_panels, pDoc->pSearchPanel);

	// Update the rest of the UI
	ACEUpdateDisplay(pDoc);
}

#define BUTTON_SPACING 3.0f

#define ADD_BUTTON( text, callback, callbackdata ) \
	pButton = ui_ButtonCreate(text, fX, 0, callback, callbackdata); \
	pButton->widget.widthUnit = UIUnitFitContents; \
	emToolbarAddChild(pToolbar, pButton, false); \
	fX += ui_WidgetGetWidth(UI_WIDGET(pButton)) + BUTTON_SPACING; \

static void ACEInitToolbarsAndMenus(EMEditor* pEditor)
{
	EMToolbar* pToolbar;
	UIButton *pButton;
	UILabel *pLabel;
	UITextEntry *pEntry;
	F32 fX;

	// Toolbar - basic options
	pToolbar = emToolbarCreateFileToolbar(EM_FILE_TOOLBAR_NEW|EM_FILE_TOOLBAR_OPEN|EM_FILE_TOOLBAR_SAVE);
	fX = emToolbarGetPaneWidget(pToolbar)->width;
	ADD_BUTTON("Duplicate", ACEDuplicateDoc, NULL);
	emToolbarSetWidth(pToolbar, fX);
	eaPush(&pEditor->toolbars, pToolbar);

	eaPush(&pEditor->toolbars, emToolbarCreateWindowToolbar());

	// Toolbar - highlight options
	fX = 0.0;
	pToolbar = emToolbarCreate(50);
	pLabel = ui_LabelCreate("Highlight", fX, 0);
	emToolbarAddChild(pToolbar, pLabel, true);
	fX += ui_WidgetGetWidth(UI_WIDGET(pLabel)) + 5.0;
	pEntry = ui_TextEntryCreate(NULL, fX, 0);
	ui_WidgetSetWidth(UI_WIDGET(pEntry), 179);
	ui_TextEntrySetFinishedCallback(pEntry, ACEHighlightChangedCB, NULL);
	emToolbarAddChild(pToolbar, pEntry, true);
	fX += ui_WidgetGetWidth(UI_WIDGET(pEntry)) + 5.0;
	emToolbarSetWidth(pToolbar, fX);
	eaPush(&pEditor->toolbars, pToolbar);

	// File menu
	emMenuItemCreate(pEditor, "ate_revertchart", "Revert", NULL, NULL, "ACE_RevertChart");
	emMenuRegister(pEditor, emMenuCreate(pEditor, "File", "ate_revertchart", NULL));
}

static void ACEStanceChanged(const char *relpath, int when)
{
	EMEditorDoc *pEditorDoc = emGetActiveEditorDoc();
	if(pEditorDoc && stricmp(pEditorDoc->doc_type, ANIM_CHART_EDITED_DICTIONARY)==0) {
		ACEUpdateDisplay((AnimChartDoc*)pEditorDoc);		
	}
}

//---------------------------------------------------------------------------------------------------
// Public Interface
//---------------------------------------------------------------------------------------------------

void ACEInitData(EMEditor* pEditor)
{
	if (pEditor && !gInitializedEditor) {
		gBoldExpanderSkin = ui_SkinCreate(NULL);
		SET_HANDLE_FROM_STRING(g_ui_FontDict, "Default_Bold", gBoldExpanderSkin->hNormal);

		ui_SkinCopy( &aceBadDataBackground, NULL);
		ui_SkinCopy( &aceBadDataButton, NULL );
		ui_SkinCopy( &aceUnsavedDataButton, NULL );
		ui_SkinSetButton( &aceUnsavedDataButton, aceUnsavedDataColor);

		{
			Color highlightColor = {225, 225, 0, 255};
			ui_SkinCopy(&aceHighlightSkin, NULL);
			ui_SkinSetBackground(&aceHighlightSkin, highlightColor);
			ui_SkinSetButton(&aceHighlightSkin, highlightColor);
			ui_SkinSetEntryEx(&aceHighlightSkin, highlightColor, ColorLighten(highlightColor, 32), ColorDarken(highlightColor, 32), highlightColor, highlightColor);
		}

		ACEInitToolbarsAndMenus(pEditor);

		// Have Editor Manager handle a lot of change tracking
		emAutoHandleDictionaryStateChange(pEditor, ANIM_CHART_EDITED_DICTIONARY, true, NULL, NULL, NULL, NULL, NULL);

		resGetUniqueScopes(hAnimChartDictLoadTime, &geaScopes);

		aceHighlightText = NULL;

		gInitializedEditor = true;
	}

	if (!gInitializedEditorData) {
		// Make sure lists refresh if dictionary changes
		resDictRegisterEventCallback(hAnimChartDictLoadTime, ACEContentDictChanged, NULL);
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "dyn/AnimStance/*.AStance", ACEStanceChanged);

		gInitializedEditorData = true;
	}
}

static void ACEChartPostOpenFixup(DynAnimChartLoadTime* pChart)
{
}


static void ACEChartPreSaveFixup(DynAnimChartLoadTime* pChart)
{
	int i;
	for ( i=0; i < eaSize(&pChart->eaMoveRefs); i++ ) {
		DynAnimChartMoveRefLoadTime *pRef = pChart->eaMoveRefs[i];
		if(!pRef->bBlank && !GET_REF(pRef->hMove) && !eaSize(&pRef->eaMoveChances)) {
			eaRemove(&pChart->eaMoveRefs, i);
			i--;
			StructDestroy(parse_DynAnimChartMoveRefLoadTime, pRef);
		}
	}

	dynAnimChartLoadTimeFixup(pChart);
}

static void	ACEChartPostSave(AnimChartDoc* pDoc)
{
	//ACEAnimChartChanged(pDoc, false, true);
}



static AnimChartDoc* ACEInitDoc(DynAnimChartLoadTime* pChart, bool bCreated, bool bEmbedded)
{
	AnimChartDoc* pDoc;
	char nameBuf[260];

	// Initialize the structure
	pDoc = (AnimChartDoc*)calloc(1,sizeof(AnimChartDoc));

	// Fill in the def data
	if (bCreated && pChart)
	{
		pDoc->pObject = StructClone(parse_DynAnimChartLoadTime, pChart);
		assert(pDoc->pObject);
		sprintf(pDoc->emDoc.doc_name,"%s_Dup%d",pChart->pcName,++aceNewNameCount);
		pDoc->pObject->pcName = StructAllocString(pDoc->emDoc.doc_name);
		sprintf(nameBuf, "dyn/animchart/%s/%s.achart", pDoc->pObject->pcScope, pDoc->pObject->pcName);
		pDoc->pObject->pcFilename = allocAddFilename(nameBuf);
	}
	else if (bCreated)
	{
		pDoc->pObject = StructCreate(parse_DynAnimChartLoadTime);
		assert(pDoc->pObject);
		emMakeUniqueDocName(&pDoc->emDoc, DEFAULT_DOC_NAME, "DynAnimChartLoadTime", "DynAnimChartLoadTime");
		pDoc->pObject->pcName = StructAllocString(pDoc->emDoc.doc_name);
		sprintf(nameBuf, "dyn/animchart/%s.achart", pDoc->pObject->pcName);
		pDoc->pObject->pcFilename = allocAddFilename(nameBuf);
		ACEChartPostOpenFixup(pDoc->pObject);
	}
	else
	{
		pDoc->pObject = StructCloneFields(parse_DynAnimChartLoadTime, pChart);
		assert(pDoc->pObject);
		ACEChartPostOpenFixup(pDoc->pObject);
		pDoc->pOrigObject = StructCloneFields(parse_DynAnimChartLoadTime, pDoc->pObject);
		emDocAssocFile(&pDoc->emDoc, pDoc->pObject->pcFilename);
	}

	// Set up the undo stack
	pDoc->emDoc.edit_undo_stack = EditUndoStackCreate();
	EditUndoSetContext(pDoc->emDoc.edit_undo_stack, pDoc);
	pDoc->pNextUndoObject = StructCloneFields(parse_DynAnimChartLoadTime, pDoc->pObject);

	return pDoc;
}


AnimChartDoc* ACEOpenAnimChart(EMEditor* pEditor, char* pcName, DynAnimChartLoadTime *pChartIn)
{
	AnimChartDoc* pDoc = NULL;
	DynAnimChartLoadTime* pChart = NULL;
	bool bCreated = false;

	if (pChartIn)
	{
		pChart = pChartIn;
		bCreated = true;
	}
	else if (pcName && resIsEditingVersionAvailable(hAnimChartDictLoadTime, pcName))
	{
		// Simply open the object since it is in the dictionary
		pChart = RefSystem_ReferentFromString(hAnimChartDictLoadTime, pcName);
	}
	else if (pcName)
	{
		// Wait for object to show up so we can open it
		resSetDictionaryEditMode(hAnimChartDictLoadTime, true);
		emSetResourceState(pEditor, pcName, EMRES_STATE_OPENING);
		resRequestOpenResource(hAnimChartDictLoadTime, pcName);
	}
	else
	{
		// Create a new object since it is not in the dictionary
		bCreated = true;
	}

	if (pChart || bCreated) {
		pDoc = ACEInitDoc(pChart, bCreated, false);
		ACEInitDisplay(pEditor, pDoc);
		resFixFilename(hAnimChartDictLoadTime, pDoc->pObject->pcName, pDoc->pObject);
	}

	return pDoc;
}

static void ACEDeleteOldDirectoryIfEmpty(AnimChartDoc *pDoc)
{
	char dir[MAX_PATH], out_dir[MAX_PATH];
	char cmd[MAX_PATH];

	sprintf(dir, "/dyn/Animchart/%s", NULL_TO_EMPTY(pDoc->pOrigObject->pcScope));
	fileLocateWrite(dir, out_dir);
	if (dirExists(out_dir))
	{
		backSlashes(out_dir);
		sprintf(cmd, "rd %s", out_dir);
		system(cmd);
	}
}

void ACERevertAnimChart(AnimChartDoc* pDoc)
{
	DynAnimChartLoadTime* pChart;

	if (!pDoc->emDoc.orig_doc_name[0]) {
		// Cannot revert if no original
		return;
	}

	//if we're reverting due to save, remove the old directory if it's empty post scope change
	if (pDoc->pOrigObject && pDoc->pObject->pcScope != pDoc->pOrigObject->pcScope)
		ACEDeleteOldDirectoryIfEmpty(pDoc);

	pChart = RefSystem_ReferentFromString(hAnimChartDictLoadTime, pDoc->emDoc.orig_doc_name);
	if (pChart) {
		// Revert the def
		StructDestroy(parse_DynAnimChartLoadTime, pDoc->pObject);
		StructDestroy(parse_DynAnimChartLoadTime, pDoc->pOrigObject);
		pDoc->pObject = StructCloneFields(parse_DynAnimChartLoadTime, pChart);
		ACEChartPostOpenFixup(pDoc->pObject);
		pDoc->pOrigObject = StructCloneFields(parse_DynAnimChartLoadTime, pDoc->pObject);

		// Clear the undo stack on revert
		EditUndoStackClear(pDoc->emDoc.edit_undo_stack);
		StructDestroy(parse_DynAnimChartLoadTime, pDoc->pNextUndoObject);
		pDoc->pNextUndoObject = StructCloneFields(parse_DynAnimChartLoadTime, pDoc->pObject);

		// Refresh the UI
		pDoc->bIgnoreFieldChanges = true;
		pDoc->bIgnoreFilenameChanges = true;
		ACEUpdateDisplay(pDoc);
		pDoc->bIgnoreFieldChanges = false;
		pDoc->bIgnoreFilenameChanges = false;
	} 
}


void ACECloseAnimChart(AnimChartDoc* pDoc)
{
	// Free doc fields
	eaDestroyEx(&pDoc->eaDocFields, MEFieldDestroy);
	eaDestroyEx(&pDoc->eaGraphRefFields, ACEGraphRefFieldDestroy);

	// Free the groups
	//FreeInteractionPropertiesGroup(pDoc->pPropsGroup);
	
	// Free the objects
	StructDestroy(parse_DynAnimChartLoadTime, pDoc->pObject);
	if (pDoc->pOrigObject) {
		StructDestroy(parse_DynAnimChartLoadTime, pDoc->pOrigObject);
	}
	StructDestroy(parse_DynAnimChartLoadTime, pDoc->pNextUndoObject);

	emPanelFree(pDoc->pSearchPanel);
	pDoc->pSearchPanel = NULL;

	// Close the window
	ui_WindowHide(pDoc->pMainWindow);
	ui_WidgetQueueFree(UI_WIDGET(pDoc->pMainWindow));
}


EMTaskStatus ACESaveAnimChart(AnimChartDoc* pDoc, bool bSaveAsNew)
{
	EMTaskStatus status;
	const char* pcName;
	DynAnimChartLoadTime* pChartCopy;

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
	pChartCopy = StructCloneFields(parse_DynAnimChartLoadTime, pDoc->pObject);
	ACEChartPreSaveFixup(pChartCopy);

	// Perform validation
	if (!dynAnimChartLoadTimeVerify(pChartCopy) || !dynAnimChartVerifyReferences(pChartCopy)) {
		StructDestroy(parse_DynAnimChartLoadTime, pChartCopy);
		return EM_TASK_FAILED;
	}

	// Do the save (which will free the copy)
	status = emSmartSaveDoc(&pDoc->emDoc, pChartCopy, pDoc->pOrigObject, bSaveAsNew);
	emDocRemoveAllFiles(&pDoc->emDoc, false);
	emDocAssocFile(&pDoc->emDoc, pDoc->pObject->pcFilename);

	ACEChartPostSave(pDoc);

	return status;
}

void ACEDuplicateDoc(UIButton *button, UserData uData)
{
	AnimChartDoc *pDoc = (AnimChartDoc*)emGetActiveEditorDoc();
	emNewDoc("AnimChart", pDoc->pObject);
}

#endif
