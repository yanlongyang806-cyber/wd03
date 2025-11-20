#ifndef NO_EDITORS

#include "MoveEditor.h"
#include "dynMove.h"

//----------------------------------------------------------------------------
// Everything below this point is from copy-paste
//----------------------------------------------------------------------------
#include "CostumeCommonLoad.h"
#include "CostumeDefEditor.h"
#include "cmdparse.h"
#include "dynFxInfo.h"
#include "EditorPrefs.h"
#include "GameClientLib.h"
#include "GfxClipper.h"
#include "AnimEditorCommon.h"
#include "GfxMaterials.h"
#include "GfxSprite.h"
#include "GfxSpriteText.h"
#include "GraphicsLib.h"
#include "StringCache.h"
#include "tokenstore.h"
#include "UnitSpec.h"
#include "EditLibGizmos.h"
#include "Quat.h"
#include "species_common.h"
#include "UIGimmeButton.h"
#include "WorldGrid.h"
#include "TimedCallback.h"
#include "GfxDebug.h"
#include "gimmeDLLWrapper.h"
#include "inputMouse.h"
#include "dynAnimTrack.h"

#include "AutoGen/CostumeCommon_h_ast.h"

static UIWindow *pGlobalWindow = NULL;

typedef struct MoveEditor_InfoBlock {
	MoveEditDoc *pDoc;
	UserData pData;
	UserData pSecondaryData;
	UIWidget *pWidget;
	union {
		S32 pint;		// used to store both, a possible value as well as a index of where the userData belongs such as in an earray
		F32 pfloat;
		const char *pString;
		bool pbool;
	} value;
	union {				// this union is exclusively used by the "redo" function
		S32 pint;
		F32 pfloat;
		const char *pString;
		bool pbool;
	} oldValue;
	union {
		const S32 *pint;
		const F32 *pfloat;
		const char **pString;
		UserData pStruct;
	} moveField;
	ParseTable *pTable;
	const char *parseField;
	bool safeToDelete;
	bool setupUndo;
}MoveEditor_InfoBlock;

typedef struct AnimTrackSliderInfo {
	UISliderTextEntry *pSlider;
	DynMoveAnimTrack *pAnimTrack;
}AnimTrackSliderInfo;

typedef struct TextEntryWindowData {
	const char* title;
	const char* textEntryInitalString;
	UserData userData;
	void (*fCallback)(UserData ignore, UserData userData);
}TextEntryWindowData;

static TextEntryWindowData *MELastTextWindowData = NULL;

static MoveEditDoc* focusedMove = NULL;

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

void MoveEditor_CreateUI(MoveEditDoc *pDoc);

// Setup the toolbar for the editor
void MoveEditor_SetupToolbar(EMEditor *pEditor);
static AnimEditor_CostumePickerData* MoveEditor_GetCostumePickerData(void);
static void MoveEditor_PostCostumeChangeCB( void );
EMPanel* MoveEditor_CreateMoveSeqPanel(MoveEditDoc *pDoc, DynMoveSeq *pMoveSeq);
void MoveEditor_SetSliderRange(S32 minValue, S32 maxValue, MoveEditDoc *pDoc);
void MoveEditor_SpawnTextEntryWindow(UIButton* ignore, TextEntryWindowData *pData);

//---------------------------------------------------------------------------------------------------
// Utility Functions
//---------------------------------------------------------------------------------------------------

static void* MoveEditor_GetData(UserData astruct, ParseTable *pti, const char* fieldname) {
	S32 ptIndex;

	if (pti) {
		for (ptIndex = 0; strlen(pti[ptIndex].name) != 0; ptIndex++) {
			if (strcmp(fieldname, pti[ptIndex].name) == 0) {
				return (void*)((char*)astruct + pti[ptIndex].storeoffset);
			}
		}
	}
	return NULL;
}

__forceinline static void MoveEditor_SetActiveMoveSeq(MoveEditDoc *pDoc, DynMoveSeq *pMoveSeq, UISliderTextEntry *pSlider) {
	pDoc->moveSeq = pMoveSeq;
	pDoc->currentFrame = pMoveSeq->dynMoveAnimTrack.uiFirst;
	pDoc->activeSlider = pSlider;
}

static void* MoveEditor_GetPanelData(MoveEditDoc *pDoc, S32 elementCount, size_t elementSize) {
	void *pData = calloc(elementCount, elementSize);
	eaPush(&pDoc->panelData, pData);
	return pData;
}

//---------------------------------------------------------------------------------------------------
// Generic Undo Functions
//---------------------------------------------------------------------------------------------------

static void MoveEditor_Undo_Free(MoveEditDoc* pDoc, MoveEditor_InfoBlock *userData) {
	if (userData->safeToDelete) {
		if (userData->pData) {
			if (userData->pTable) {
				StructDestroyVoid(userData->pTable,userData->pData);
			} else {
				free(userData->pData);
			}
		}
	}
	free(userData);
}

static void MoveEditor_Undo_FloatTextEntry(MoveEditDoc* pDoc, MoveEditor_InfoBlock *userData) {
	char strBuf[255];

	sprintf(strBuf,"%f",userData->oldValue.pfloat);
	*((F32*)userData->pData) = userData->oldValue.pfloat;
	ui_TextEntrySetText((UITextEntry*)userData->pSecondaryData, strBuf);
}

static void MoveEditor_Redo_FloatTextEntry(MoveEditDoc* pDoc, MoveEditor_InfoBlock *userData) {
	char strBuf[255];

	sprintf(strBuf,"%f",userData->value.pfloat);
	*((F32*)userData->pData) = userData->value.pfloat;
	ui_TextEntrySetText((UITextEntry*)userData->pSecondaryData, strBuf);
}

void MoveEditor_Undo_CheckToggle(MoveEditDoc *pDoc, MoveEditor_InfoBlock *pInfo) {
	*(bool*)pInfo->pData = pInfo->oldValue.pbool;
	ui_CheckButtonSetState(pInfo->pSecondaryData, pInfo->oldValue.pbool);
}

void MoveEditor_Redo_CheckToggle(MoveEditDoc *pDoc, MoveEditor_InfoBlock *pInfo) {
	*(bool*)pInfo->pData = pInfo->value.pbool;
	ui_CheckButtonSetState(pInfo->pSecondaryData, pInfo->value.pbool);
}

void MoveEditor_Undo_Slider(MoveEditDoc *pDoc, MoveEditor_InfoBlock *pData) {
	ui_SliderTextEntrySetValue((UISliderTextEntry*)pData->pSecondaryData, pData->oldValue.pfloat);
	*(F32*)pData->pData = pData->oldValue.pfloat;
}

void MoveEditor_Redo_Slider(MoveEditDoc *pDoc, MoveEditor_InfoBlock *pData) {
	ui_SliderTextEntrySetValue((UISliderTextEntry*)pData->pSecondaryData, pData->value.pfloat);
	*(F32*)pData->pData = pData->value.pfloat;
}

void MoveEditor_Undo_SliderFree(MoveEditDoc *pDoc, MoveEditor_InfoBlock *pData) {
	free(pData);
}

//---------------------------------------------------------------------------------------------------
// Stuff that needs to be filled in
//---------------------------------------------------------------------------------------------------



static void MoveEditor_TickCheckChanges(MoveEditDoc *pDoc) {
}

void MoveEditor_InitDisplay(EMEditor *pEditor, MoveEditDoc *pDoc) {
	// Create the side-panels

	// Setup the UI such as displaying panels

	MoveEditor_CreateUI(pDoc);

	// Add def windows to the editor
}

// Load global data structures
void MoveEditor_InitData(EMEditor *pEditor) {
	MoveEditor_SetupToolbar(pEditor);
}

//---------------------------------------------------------------------------------------------------
// Generic Functions and Callbacks
//---------------------------------------------------------------------------------------------------
void MoveEditor_CloseTextEntryWindow(UIButton *ignore, UIWindow *pWindow) {
	ui_WindowClose(pWindow);
}

typedef enum ME_TextType {
	ME_TEXTENTRYDEFAULT = 0,
	ME_TEXTENTRYSTRING = ME_TEXTENTRYDEFAULT,
	ME_TEXTENTRYFLOAT,
	ME_TEXTENTRYINT,
}ME_TextType;

UITextEntry *MoveEditor_CreateTextEntryEx(const char* label, ME_TextType textType, UserData aStruct, ParseTable *pti, const char* dataField, void (*textEntryCallback)(UserData, UserData), MoveEditDoc *pDoc, EMPanel *pPanel, U32 x, U32 *y, UserData userData) {
	char strBuf[255];
	UILabel *pLabel;
	UITextEntry *pTextEntry;
	MoveEditor_InfoBlock *pData;
	UserData ptData = MoveEditor_GetData(aStruct, pti, dataField);

	assert(ptData);
	pData = calloc(1,sizeof(MoveEditor_InfoBlock));
	switch (textType) {
		case ME_TEXTENTRYFLOAT:
			sprintf(strBuf, "%f", *(F32*)ptData);
			pData->oldValue.pfloat = *(F32*)ptData;
			break;
		case ME_TEXTENTRYINT:
			sprintf(strBuf, "%d", *(S32*)ptData);
			pData->oldValue.pint = *(S32*)ptData;
			break;
		default:
			sprintf(strBuf, "%s", (char*)ptData);
			pData->oldValue.pString = (char*)ptData;
	}
	pLabel = ui_LabelCreate(label, x, *y);
	emPanelAddChild(pPanel, UI_WIDGET(pLabel), false);
	pTextEntry = ui_TextEntryCreate(strBuf, UI_WIDGET(pLabel)->width + x + 10, *y);
	switch (textType) {
		case ME_TEXTENTRYFLOAT:
			ui_TextEntrySetFloatOnly(pTextEntry);
			break;
		case ME_TEXTENTRYINT:
			ui_TextEntrySetIntegerOnly(pTextEntry);
			break;
	}
	pData->pDoc = pDoc;
	pData->pData = ptData;
	pData->pSecondaryData = userData;
	ui_TextEntrySetFinishedCallback(pTextEntry, textEntryCallback, pData);
	ui_WidgetSetWidthEx(UI_WIDGET(pTextEntry), 1.0, UIUnitPercentage);
	emPanelAddChild(pPanel, UI_WIDGET(pTextEntry), false);
	*y += 30;
	return pTextEntry;
}

#define MoveEditor_CreateTextEntry(label, textType, aStruct, parseTable, dataField, textEntryCallback, userData) MoveEditor_CreateTextEntryEx(label, textType, aStruct, parseTable, dataField, textEntryCallback, pDoc, pPanel, x, &y, userData)

UICheckButton* MoveEditor_CreateCheckBoxEx(const char* label, UserData aStruct, ParseTable *pti, const char* dataField, void (*checkboxCallback)(UserData, UserData), MoveEditDoc *pDoc, EMPanel *pPanel, U32 x, U32 *y) {
	UICheckButton *pCheck;
	MoveEditor_InfoBlock *pData;
	UserData ptData = MoveEditor_GetData(aStruct, pti, dataField);

	pData = calloc(1,sizeof(MoveEditor_InfoBlock));
	pData->pData = ptData;
	pData->pDoc = pDoc;
	pData->setupUndo = true;
	pCheck = ui_CheckButtonCreate(x, *y, label, *(bool*)ptData);
	ui_CheckButtonSetToggledCallback(pCheck, checkboxCallback, pData);
	emPanelAddChild(pPanel, UI_WIDGET(pCheck), false);
	*y += 25;
	return pCheck;
}

#define MoveEditor_CreateCheckBox(label, aStruct, parseTable, dataField, checkboxCallback) MoveEditor_CreateCheckBoxEx(label, aStruct, parseTable, dataField, checkboxCallback, pDoc, pPanel, x, &y)

UIButton* MoveEditor_CreateButtonEx(const char* label, UserData aStruct, ParseTable *pti, const char* dataField, void (*buttonCallback)(UserData, UserData), MoveEditDoc *pDoc, EMPanel *pPanel, U32 x, U32 *y, UserData userData) {
	UIButton *pButton;
	MoveEditor_InfoBlock *pData;
	UserData ptData = MoveEditor_GetData(aStruct, pti, dataField);

	pData = calloc(1,sizeof(MoveEditor_InfoBlock));
	pData->pData = ptData;
	pData->pSecondaryData = userData;
	pData->pDoc = pDoc;
	pData->setupUndo = true;
	pButton = ui_ButtonCreate(label, x, *y, buttonCallback, pData);
	ui_WidgetSetWidthEx(UI_WIDGET(pButton), 1.0, UIUnitPercentage);
	emPanelAddChild(pPanel, UI_WIDGET(pButton), false);
	*y += 30;
	return pButton;
}

#define MoveEditor_CreateButton(label, aStruct, parseTable, dataField, buttonCallback, userData) MoveEditor_CreateButtonEx(label, aStruct, parseTable, dataField, buttonCallback, pDoc, pPanel, x, &y, userData)

void MoveEditor_SpawnTextEntryWindow(UIButton* ignore, TextEntryWindowData *oData) {
	static UIWindow animWindow;
	static UIButton okayButton;
	static UIButton cancelButton;
	static MoveEditor_InfoBlock pData;	// never should be more than one of these in existence since window is modal
	static UITextEntry textEntry;
	static bool windowInitialized = false;
	static U32 height = 10;

	assert(oData);
	MELastTextWindowData = oData;
	pData.moveField.pStruct = oData->userData;
	pData.pData = &textEntry;
	pData.pSecondaryData = &animWindow;

	ui_WindowInitializeEx(&animWindow, oData->title, 500,height,200,height MEM_DBG_PARMS_INIT);

	ui_WidgetSetWidthEx(UI_WIDGET(&textEntry), 1.0, UIUnitPercentage);
	if (!windowInitialized) {
		windowInitialized = true;
		ui_TextEntryInitialize(&textEntry, oData->textEntryInitalString, 0, 10);
		ui_WindowAddChild(&animWindow, &textEntry);

		height += 30;
		ui_ButtonInitialize(&okayButton, "Okay", 0, height, oData->fCallback, &pData MEM_DBG_PARMS_INIT);
		okayButton.widget.widthUnit = UIUnitFitContents;
		ui_WindowAddChild(&animWindow, UI_WIDGET(&okayButton));

		ui_ButtonInitialize(&cancelButton, "Cancel", ui_WidgetGetWidth(UI_WIDGET(&okayButton)) + 10, height, MoveEditor_CloseTextEntryWindow, &animWindow MEM_DBG_PARMS_INIT);
		cancelButton.widget.widthUnit = UIUnitFitContents;
		ui_WindowAddChild(&animWindow,UI_WIDGET(&cancelButton));
		height += 30;

		ui_WindowSetDimensions(&animWindow, 200, height, 200, height);
		ui_WindowSetClosable(&animWindow, false);
		ui_WindowSetModal(&animWindow, true);
	} else {
		ui_TextEntrySetText(&textEntry, oData->textEntryInitalString);
		ui_ButtonSetCallback(&okayButton,oData->fCallback,&pData);
	}

	ui_WindowPresent(&animWindow);
}

void MoveEditor_CreateTextEntryWindowButtonEx(const char* title, const char* initString, void (*buttonCallback)(UserData, UserData), EMPanel *pPanel, U32 x, U32 *y, MoveEditor_InfoBlock *userData) {
	TextEntryWindowData *textData = calloc(1,sizeof(TextEntryWindowData));
	UIButton *pButton;

	assert(!userData->pSecondaryData);
	userData->pSecondaryData = textData;

	textData->textEntryInitalString = initString;
	textData->title = title;
	textData->userData = userData;
	textData->fCallback = buttonCallback;
	pButton = ui_ButtonCreate(title, x, *y, MoveEditor_SpawnTextEntryWindow, textData);
	emPanelAddChild(pPanel, UI_WIDGET(pButton), false);
	*y += 30;
}

#define MoveEditor_CreateTextEntryWindowButton(title, initString, buttonCallback, userData) MoveEditor_CreateTextEntryWindowButtonEx(title, initString, buttonCallback, pPanel, x, &y, userData)

//---------------------------------------------------------------------------------------------------
// Public Interface
//---------------------------------------------------------------------------------------------------

static void MoveEditor_SliderChangeCallback(UISliderTextEntry *pSlider, bool bFinished, MoveEditor_InfoBlock *pData) {
	*(F32*)pData->pData = ui_SliderTextEntryGetValue(pSlider);
	if (pData->setupUndo && !mouseIsDown(MS_LEFT)) {
		MoveEditor_InfoBlock *undoBlock = calloc(1,sizeof(MoveEditor_InfoBlock));
		undoBlock->pDoc = pData->pDoc;
		undoBlock->oldValue.pfloat = pData->oldValue.pfloat;
		undoBlock->value.pfloat = *(F32*)pData->pData;
		undoBlock->pData = pData->pData;
		undoBlock->pSecondaryData = pSlider;
		EditCreateUndoCustom(pData->pDoc->emDoc.edit_undo_stack, MoveEditor_Undo_Slider, MoveEditor_Redo_Slider, MoveEditor_Undo_SliderFree, undoBlock);
	}
}

void MoveEditor_CheckToggle(UICheckButton *pCheck, MoveEditor_InfoBlock* pInfo) {
	if (pInfo->setupUndo) {
		MoveEditor_InfoBlock *undoBlock = calloc(1, sizeof(MoveEditor_InfoBlock));

		undoBlock->oldValue.pbool = *(bool*)pInfo->pData;
		undoBlock->value.pbool = ui_CheckButtonGetState(pCheck);
		undoBlock->pData = pInfo->pData;
		undoBlock->pSecondaryData = pCheck;
		*(bool*)pInfo->pData = undoBlock->value.pbool;
		undoBlock->pDoc = pInfo->pDoc;
		EditCreateUndoCustom(pInfo->pDoc->emDoc.edit_undo_stack, MoveEditor_Undo_CheckToggle, MoveEditor_Redo_CheckToggle, MoveEditor_Undo_Free, undoBlock);
	} else {
		*(bool*)pInfo->pData = ui_CheckButtonGetState(pCheck);
	}
}

void cutBaseDir(char* pString) {
	U32 initDirLength = 0;
	U32 strCounter = 0;
	U32 strLength = (U32)strlen(pString);

	while ((pString[initDirLength] != '/') && (initDirLength < strLength)) {
		initDirLength ++;
	}
	initDirLength ++;
	while ((initDirLength + strCounter) <= strLength) {
		pString[strCounter] = pString[strCounter + initDirLength];
		strCounter ++;
	}
}

static void MoveEditor_ChangeAnimTrack(UIButton *ignoreButton, MoveEditor_InfoBlock* pData) {
	char	saveFile[ MAX_PATH ];
	char	saveDir[ MAX_PATH ];
	char	fullPath[ MAX_PATH ];
	char	fileNoExt[ MAX_PATH ];
	DynMoveAnimTrack *pMoveAnimTrack = pData->moveField.pStruct;
	DynAnimTrackHeader *pAnimHeader;
	UISliderTextEntry *pSlider = (UISliderTextEntry*)pData->pSecondaryData;

	if( UIOk != ui_ModalFileBrowser( "Set Animation Track",
		"Set", UIBrowseExisting, UIBrowseFiles, false,
		"animation_library",
		"animation_library",
		".atrk",
		SAFESTR( saveDir ), SAFESTR( saveFile ), NULL))
	{
			return;
	}
	getFileNameNoExt(fileNoExt, saveFile);
	cutBaseDir(saveDir);
	sprintf( fullPath, "%s/%s", saveDir, fileNoExt );

	pAnimHeader = dynAnimTrackHeaderFind(fullPath);
	if (pAnimHeader) {
		pMoveAnimTrack->pcAnimTrackName = allocAddString(fullPath);
		pMoveAnimTrack->pAnimTrackHeader = pAnimHeader;
		pMoveAnimTrack->uiLast = pMoveAnimTrack->pAnimTrackHeader->uiTotalFrames;
		pMoveAnimTrack->uiLastFrame = pMoveAnimTrack->uiLast - 1;
		if (pMoveAnimTrack->uiFirst > pMoveAnimTrack->uiLast) {
			pMoveAnimTrack->uiFirst = pMoveAnimTrack->uiLast;
			pMoveAnimTrack->uiFirstFrame = pMoveAnimTrack->uiLastFrame;
		}
		ui_SliderTextEntrySetRange(pSlider,pMoveAnimTrack->uiFirst,pMoveAnimTrack->uiLast,0.1);
		ui_WidgetSetTextString(pData->pWidget, fullPath);
		emSetDocUnsaved(&pData->pDoc->emDoc, false);
	}
}

void MoveEditor_FloatTextEntryChanged(UITextEntry *pTextEntry, MoveEditor_InfoBlock *pInfoBlock) {
	F32 temp = atof(ui_TextEntryGetText(pTextEntry));
	MoveEditor_InfoBlock *undoData = calloc(1,sizeof(MoveEditor_InfoBlock));

	*(F32*)pInfoBlock->pData = temp;
	undoData->pDoc = pInfoBlock->pDoc;
	undoData->pData = pInfoBlock->pData;
	undoData->value.pfloat = temp;
	undoData->oldValue.pfloat = pInfoBlock->oldValue.pfloat;
	undoData->safeToDelete = false;
	EditCreateUndoCustom(pInfoBlock->pDoc->emDoc.edit_undo_stack, MoveEditor_Undo_FloatTextEntry, MoveEditor_Redo_FloatTextEntry, MoveEditor_Undo_Free, undoData);
	emSetDocUnsaved(&pInfoBlock->pDoc->emDoc, false);
}

static void MoveEditor_FrameChange(UISliderTextEntry *pSlider, bool bFinished, MoveEditor_InfoBlock *pData) {
	F32 frame = ui_SliderTextEntryGetValue(pSlider);

	if (pData->pDoc->moveSeq != pData->pData)
		MoveEditor_SetActiveMoveSeq(pData->pDoc, pData->pData, pSlider);

	pData->pDoc->currentFrame = frame;
	if (pData->pDoc->costumeData.pGraphics) {
		if (frame == pData->pDoc->moveSeq->dynMoveAnimTrack.uiLast) {
			dynSkeletonForceAnimation(pData->pDoc->costumeData.pGraphics->costume.pSkel, ((DynMoveAnimTrack*)pData->moveField.pStruct)->pcAnimTrackName, frame - 0.001);
		} else {
			dynSkeletonForceAnimation(pData->pDoc->costumeData.pGraphics->costume.pSkel, ((DynMoveAnimTrack*)pData->moveField.pStruct)->pcAnimTrackName, frame);
		}
	}
}

void MoveEditor_SimpleCheckCallback(UICheckButton *pCheck, bool* pBool) {
	*pBool = ui_CheckButtonGetState(pCheck);
}

void MoveEditor_SetSliderRange(S32 minValue, S32 maxValue, MoveEditDoc *pDoc) {
	UISliderTextEntry *pSlider = pDoc->activeSlider;
	DynMoveAnimTrack *pMoveAnimTrack = &pDoc->pMove->eaDynMoveSeqs[0]->dynMoveAnimTrack;

	ui_SliderTextEntrySetRange(pSlider, pMoveAnimTrack->uiFirst, pMoveAnimTrack->uiLast, 0.1);
}

void MoveEditor_MinFrameRangeChange(UITextEntry *pTextEntry, MoveEditor_InfoBlock *pData) {
	DynMoveAnimTrack *pMoveAnimTrack = ((AnimTrackSliderInfo*)pData->pSecondaryData)->pAnimTrack;
	UISliderTextEntry *pSlider = ((AnimTrackSliderInfo*)pData->pSecondaryData)->pSlider;
	S32 temp = atoi(ui_TextEntryGetText(pTextEntry));
	U32 pValue;
	U32 lastFrame;

	if (temp < 0) {
		char strBuf[255];
		sprintf(strBuf,"%d",pData->oldValue.pint);
		Alertf("Entry must be non-negative.");
		ui_TextEntrySetText(pTextEntry,strBuf);
		return;
	}
	if (pMoveAnimTrack->uiLast)
		lastFrame = pMoveAnimTrack->uiLast;
	else if (pMoveAnimTrack->pAnimTrackHeader->pAnimTrack && pMoveAnimTrack->pAnimTrackHeader->pAnimTrack->uiTotalFrames)
		lastFrame = pMoveAnimTrack->pAnimTrackHeader->pAnimTrack->uiTotalFrames;
	else
		lastFrame = pMoveAnimTrack->uiLastFrame + 1;
	pValue = temp;
	if (pValue > lastFrame) {
		char strBuf[255];
		sprintf(strBuf,"%d",pData->oldValue.pint);
		ui_TextEntrySetText(pTextEntry,strBuf);
		Alertf("First frame (%d) cannot be greater than the Last frame (%d)",pValue,(pMoveAnimTrack->uiLast ? (pMoveAnimTrack->uiLast + 1) : pMoveAnimTrack->pAnimTrackHeader->pAnimTrack->uiTotalFrames));
		return;
	}
	pData->oldValue.pint = pValue;
	pMoveAnimTrack->uiFirst = pValue;
	if (pMoveAnimTrack->uiFirst > pData->pDoc->currentFrame) {
		pData->pDoc->currentFrame = pMoveAnimTrack->uiFirst;
		ui_SliderTextEntrySetValue(pSlider, pMoveAnimTrack->uiFirst);
		if (pData->pDoc->costumeData.pGraphics)
			dynSkeletonForceAnimation(pData->pDoc->costumeData.pGraphics->costume.pSkel, pData->pDoc->moveSeq->dynMoveAnimTrack.pcAnimTrackName, pData->pDoc->currentFrame);
	}
	ui_SliderTextEntrySetRange(pSlider, pMoveAnimTrack->uiFirst, lastFrame, 0.1);
	emSetDocUnsaved(&pData->pDoc->emDoc, false);
}

void MoveEditor_MaxFrameRangeChange(UITextEntry *pTextEntry, MoveEditor_InfoBlock *pData) {
	DynMoveAnimTrack *pMoveAnimTrack = ((AnimTrackSliderInfo*)pData->pSecondaryData)->pAnimTrack;
	UISliderTextEntry *pSlider = ((AnimTrackSliderInfo*)pData->pSecondaryData)->pSlider;
	DynAnimTrackHeader *pAnimTrackHeader;
	S32 temp = atoi(ui_TextEntryGetText(pTextEntry));
	U32 pValue;
	char strBuf[255];
	sprintf(strBuf,"%d",pData->oldValue.pint);

	if (temp < 0) {
		Alertf("Entry must be non-negative.");
		ui_TextEntrySetText(pTextEntry,strBuf);
		return;
	}
	pValue = temp;
	if (pValue < pMoveAnimTrack->uiFirst) {
		Alertf("Last frame (%d) cannot be smaller than the First frame (%d)",pValue,pMoveAnimTrack->uiFirst);
		ui_TextEntrySetText(pTextEntry,strBuf);
		return;
	}
	pAnimTrackHeader = dynAnimTrackHeaderFind(pMoveAnimTrack->pcAnimTrackName);
	if (!dynAnimTrackHeaderRequest(pAnimTrackHeader)) {
		Alertf("AnimTrack (%s) is not ready or does not exist so this field cannot be modified.", pMoveAnimTrack->pcAnimTrackName);
		ui_TextEntrySetText(pTextEntry, strBuf);
		return;
	}
	if (pValue > pAnimTrackHeader->pAnimTrack->uiTotalFrames) {
		Alertf("AnimTrack does not have %d frames.", pValue);
		ui_TextEntrySetText(pTextEntry, strBuf);
		return;
	} else {
		if (ui_SliderTextEntryGetValue(pSlider) > pValue) {
			pData->pDoc->currentFrame = pValue;
			ui_SliderTextEntrySetValue(pSlider, pValue);
			if (pData->pDoc->costumeData.pGraphics)
				dynSkeletonForceAnimation(pData->pDoc->costumeData.pGraphics->costume.pSkel, pData->pDoc->moveSeq->dynMoveAnimTrack.pcAnimTrackName, pData->pDoc->currentFrame);
		}
		pData->oldValue.pint = pValue;
		pMoveAnimTrack->uiLast = pValue;
	}
	ui_SliderTextEntrySetRange(pSlider, pMoveAnimTrack->uiFirst, pMoveAnimTrack->uiLast, 0.1);
	emSetDocUnsaved(&pData->pDoc->emDoc, false);
}

void MoveEditor_PlayAnimation(UIButton *pButton, MoveEditor_InfoBlock *pData) {
	DynMoveSeq *pMoveSeq = (DynMoveSeq*)pData->pData;
	S32 lastFrame;

	if (pMoveSeq->dynMoveAnimTrack.uiLast) {
		lastFrame = pMoveSeq->dynMoveAnimTrack.uiLast;
	} else {
		pMoveSeq->dynMoveAnimTrack.uiLast = pMoveSeq->dynMoveAnimTrack.uiLastFrame + 1;
		MoveEditor_SetSliderRange(pMoveSeq->dynMoveAnimTrack.uiFirst, pMoveSeq->dynMoveAnimTrack.uiLast, pData->pDoc);
	}

	if (pData->pDoc->moveSeq != pMoveSeq) {
		MoveEditor_SetActiveMoveSeq(pData->pDoc, pMoveSeq, pData->pSecondaryData);
	}
	pData->pDoc->animate = true;
}

void MoveEditor_StopAnimation(UIButton *pButton, MoveEditDoc *pDoc) {
	pDoc->animate = false;
}

void MoveEditor_DisplayAnimTrack(MoveEditDoc *pDoc, EMPanel *pPanel, DynMoveSeq *pMoveSeq, U32 x, U32 *pY) {
	S32 y = *pY;
	DynMoveAnimTrack *dynMoveAnimTrack = &pMoveSeq->dynMoveAnimTrack;
	UILabel *pLabel;
	UISliderTextEntry *pSlider;
	MoveEditor_InfoBlock *pChangeAnimData;
	MoveEditor_InfoBlock *pData;
	AnimTrackSliderInfo *pFirstData = calloc(1, sizeof(AnimTrackSliderInfo));
	AnimTrackSliderInfo *pLastData = calloc(1, sizeof(AnimTrackSliderInfo));
	UIButton *pButton;
	UICheckButton *pCheck;
	char strBuf[255];

	pFirstData->pAnimTrack = dynMoveAnimTrack;
	pLastData->pAnimTrack = dynMoveAnimTrack;

	pLabel = ui_LabelCreate(dynMoveAnimTrack->pcAnimTrackName, x, y);
	emPanelAddChild(pPanel,UI_WIDGET(pLabel), false);
	y += 25;
	x += 10;

	pChangeAnimData = calloc(1,sizeof(MoveEditor_InfoBlock));
	pChangeAnimData->pDoc = pDoc;
	pChangeAnimData->pWidget = UI_WIDGET(pLabel);
	pChangeAnimData->moveField.pStruct = dynMoveAnimTrack;
	pChangeAnimData->parseField = "AnimTrackName";
	pChangeAnimData->pTable = parse_DynMoveAnimTrack;
	pButton = ui_ButtonCreate("Change Anim Track", x, y, MoveEditor_ChangeAnimTrack, pChangeAnimData);
	emPanelAddChild(pPanel,UI_WIDGET(pButton), false);
	y += 30;

	if (dynMoveAnimTrack->uiLast == 0) {
		if (dynMoveAnimTrack->pAnimTrackHeader->pAnimTrack && dynMoveAnimTrack->pAnimTrackHeader->pAnimTrack->uiTotalFrames)
			dynMoveAnimTrack->uiLast = dynMoveAnimTrack->pAnimTrackHeader->pAnimTrack->uiTotalFrames;
		else
			dynMoveAnimTrack->uiLast = dynMoveAnimTrack->uiLastFrame + 1;
	}
	MoveEditor_CreateTextEntry("First Frame", ME_TEXTENTRYINT, dynMoveAnimTrack, parse_DynMoveAnimTrack, "First", MoveEditor_MinFrameRangeChange, pFirstData);
	MoveEditor_CreateTextEntry("Last Frame", ME_TEXTENTRYINT, dynMoveAnimTrack, parse_DynMoveAnimTrack, "Last", MoveEditor_MaxFrameRangeChange, pLastData);

	{
		// Purely a UI element (not stored in the data structure) for showing the animation of the move file.
		// As such, winds up being a very specific use and isn't setup in a generic fashion as to use a repeatable macro/function.
		pData = calloc(1,sizeof(MoveEditor_InfoBlock));
		pData->pDoc = pDoc;
		pData->moveField.pStruct = dynMoveAnimTrack;
		pData->pData = pMoveSeq;
		pLabel = ui_LabelCreate("Frame", x, y);
		emPanelAddChild(pPanel,UI_WIDGET(pLabel), false);
		sprintf(strBuf,"%d.0",dynMoveAnimTrack->uiFirst);
		pSlider = ui_SliderTextEntryCreate(strBuf,dynMoveAnimTrack->uiFirst,(dynMoveAnimTrack->uiLast?dynMoveAnimTrack->uiLast:dynMoveAnimTrack->uiFirst), x + UI_WIDGET(pLabel)->width + 5, y, 0);
		ui_SliderTextEntrySetRange(pSlider,dynMoveAnimTrack->uiFirst,dynMoveAnimTrack->uiLast,0.10);
		ui_SliderTextEntrySetChangedCallback(pSlider, MoveEditor_FrameChange, pData);
		ui_SliderTextEntrySetPolicy(pSlider, UISliderContinuous);
		ui_WidgetSetWidthEx(UI_WIDGET(pSlider), 1.0, UIUnitPercentage);
		emPanelAddChild(pPanel,UI_WIDGET(pSlider), false);
		dynSkeletonForceAnimationPrepare(strBuf);
		pFirstData->pSlider = pSlider;
		pLastData->pSlider = pSlider;
		pChangeAnimData->pSecondaryData = pSlider;
		y += 30;

		pDoc->currentFrame = dynMoveAnimTrack->uiFirst;
		pData = calloc(1,sizeof(MoveEditor_InfoBlock));
		pData->pDoc = pDoc;
		pData->pData = pMoveSeq;
		pData->pSecondaryData = pSlider;
		pButton = ui_ButtonCreate("Play", x, y, MoveEditor_PlayAnimation, pData);
		emPanelAddChild(pPanel,UI_WIDGET(pButton), false);
		pButton = ui_ButtonCreate("Stop", UI_WIDGET(pButton)->width + UI_WIDGET(pButton)->x + 10, y, MoveEditor_StopAnimation, pDoc);
		emPanelAddChild(pPanel,UI_WIDGET(pButton), false);
		pCheck = ui_CheckButtonCreate(UI_WIDGET(pButton)->width + UI_WIDGET(pButton)->x + 10, y, "Loop", pDoc->loop);
		ui_CheckButtonSetToggledCallback(pCheck, MoveEditor_SimpleCheckCallback, &pDoc->loop);
		emPanelAddChild(pPanel,UI_WIDGET(pCheck), false);
		y += 30;
	}

	// Pose the skeleton
	pDoc->moveSeq = pMoveSeq;
	pDoc->currentFrame = dynMoveAnimTrack->uiFirst;
	pDoc->activeSlider = pSlider;
	if (pDoc->costumeData.pGraphics) {
		dynSkeletonForceAnimation(pDoc->costumeData.pGraphics->costume.pSkel, pDoc->moveSeq->dynMoveAnimTrack.pcAnimTrackName, pDoc->currentFrame);
	}
	*pY = y;
}

void MoveEditor_SetMoveSequenceName(UserData ignore, MoveEditor_InfoBlock *pData) {
	MoveEditor_InfoBlock *ptData = pData->moveField.pStruct;
	*ptData->moveField.pString = allocAddString(ui_TextEntryGetText((UITextEntry*)pData->pData));
	((TextEntryWindowData*)ptData->pSecondaryData)->textEntryInitalString = *ptData->moveField.pString;
	emPanelSetName(ptData->pData, *ptData->moveField.pString);
	ui_WindowClose(pData->pSecondaryData);
	emSetDocUnsaved(&ptData->pDoc->emDoc, false);
	return;
}

EMPanel* MoveEditor_CreateMoveSeqPanel(MoveEditDoc *pDoc, DynMoveSeq *pMoveSeq) {
	EMPanel *pPanel = emPanelCreate("Move", pMoveSeq->pcDynMoveSeq,0);
	U32 x = 10;
	U32 y = 0;

	{
		MoveEditor_InfoBlock *pData = calloc(1, sizeof(MoveEditor_InfoBlock));
		pData->pDoc = pDoc;
		pData->pData = pPanel;
		pData->moveField.pString = &pMoveSeq->pcDynMoveSeq;
		MoveEditor_CreateTextEntryWindowButton("Set Move Sequence Name", NULL, MoveEditor_SetMoveSequenceName, pData);
	}
	MoveEditor_CreateTextEntry("Frame Speed", ME_TEXTENTRYFLOAT, pMoveSeq, parse_DynMoveSeq, "Speed", MoveEditor_FloatTextEntryChanged, NULL);
	MoveEditor_CreateTextEntry("Distance", ME_TEXTENTRYFLOAT, pMoveSeq, parse_DynMoveSeq, "Distance", MoveEditor_FloatTextEntryChanged, NULL);
	MoveEditor_CreateTextEntry("Min Rate", ME_TEXTENTRYFLOAT, pMoveSeq, parse_DynMoveSeq, "MinRate", MoveEditor_FloatTextEntryChanged, NULL);
	MoveEditor_CreateTextEntry("Max Rate", ME_TEXTENTRYFLOAT, pMoveSeq, parse_DynMoveSeq, "MaxRate", MoveEditor_FloatTextEntryChanged, NULL);
	MoveEditor_CreateTextEntry("IK Target", ME_TEXTENTRYSTRING, pMoveSeq, parse_DynMoveSeq, "IKTarget", MoveEditor_FloatTextEntryChanged, NULL);
	MoveEditor_CreateCheckBox("Ragdoll", pMoveSeq, parse_DynMoveSeq, "Ragdoll", MoveEditor_CheckToggle);
	MoveEditor_CreateCheckBox("Register Weapon", pMoveSeq, parse_DynMoveSeq, "RegisterWep", MoveEditor_CheckToggle);

	MoveEditor_DisplayAnimTrack(pDoc, pPanel, pMoveSeq, x, &y);

	emPanelSetHeight(pPanel, y);

	pDoc->moveSeq = pMoveSeq;

	return pPanel;
}

typedef struct FileAssocationData {
	MoveEditDoc *pDoc;
	UILabel *pLabel;
}FileAssocationData;

void MoveEditor_AddMoveSequence(UserData ignore, MoveEditor_InfoBlock *pData) {
	MoveEditor_InfoBlock *ptData = pData->moveField.pStruct;
	MoveEditDoc *pDoc = ptData->pDoc;
	const char *moveSeqName = allocAddString(ui_TextEntryGetText((UITextEntry*)pData->pData));
	DynMoveSeq *pMoveSeq;

	if (!strlen(moveSeqName)) {
		Alertf("Move Sequence Requires a name.");
		ui_WindowClose(pData->pSecondaryData);
		MoveEditor_SpawnTextEntryWindow(NULL, MELastTextWindowData);
		return;
	}
	assert(pDoc->pMove->eaDynMoveSeqs);
	FOR_EACH_IN_EARRAY(pDoc->pMove->eaDynMoveSeqs, DynMoveSeq, moveSeq) {
		if (!strcmpi(moveSeq->pcDynMoveSeq,moveSeqName)) {
			Alertf("A duplicate name was entered.");
			ui_WindowClose(pData->pSecondaryData);
			MoveEditor_SpawnTextEntryWindow(NULL, MELastTextWindowData);
			return;
		}
	} FOR_EACH_END;
	pMoveSeq = StructClone(parse_DynMoveSeq,pDoc->pMove->eaDynMoveSeqs[eaSize(&pDoc->pMove->eaDynMoveSeqs) - 1]);
	assert(pMoveSeq);
	pMoveSeq->pcDynMoveSeq = moveSeqName;
	eaPush(&pDoc->pMove->eaDynMoveSeqs,pMoveSeq);
	eaPush(&pDoc->emDoc.em_panels,MoveEditor_CreateMoveSeqPanel(pDoc, pMoveSeq));
	ui_WindowClose(pData->pSecondaryData);
	emSetDocUnsaved(&pDoc->emDoc, false);
	return;
}

void MoveEditor_SetFileAssociation(UIButton *ignore, FileAssocationData *pFAData) {
	char			saveFile[ MAX_PATH ];
	char			saveDir[ MAX_PATH ];
	char			fullPath[ MAX_PATH ];

	if( UIOk != ui_ModalFileBrowser( "Set File Association",
		"Set", UIBrowseNewOrExisting, UIBrowseFiles, false,
		"dyn/move",
		"dyn/move",
		".move",
		SAFESTR( saveDir ), SAFESTR( saveFile ), NULL))
	{
			return;
	}
	sprintf( fullPath, "%s/%s", saveDir, saveFile );
	ui_LabelSetText(pFAData->pLabel, fullPath);
	pFAData->pDoc->pMove->pcFilename = allocAddString(fullPath);
	emSetDocUnsaved(&pFAData->pDoc->emDoc, false);

	return;
}

void MoveEditor_SetMoveName(UserData ignore, MoveEditor_InfoBlock *pData) {
	MoveEditor_InfoBlock *ptData = pData->moveField.pStruct;
	ptData->pDoc->pMove->pcName = allocAddString(ui_TextEntryGetText((UITextEntry*)pData->pData));
	((TextEntryWindowData*)ptData->pSecondaryData)->textEntryInitalString = ptData->pDoc->pMove->pcName;
	emPanelSetName((EMPanel*)ptData->pData, ptData->pDoc->pMove->pcName);
	strcpy(ptData->pDoc->emDoc.doc_display_name, ptData->pDoc->pMove->pcName);
	ui_WindowClose(pData->pSecondaryData);
	emSetDocUnsaved(&ptData->pDoc->emDoc, false);
	return;
}

static void MoveEditor_CreateMovePanel(MoveEditDoc *pDoc) {
	// --------------------------------------
	// DynAction Panel function calls go below
	// --------------------------------------
	if (pDoc) {
		if (pDoc->pMove) {
			EMPanel *pPanel = emPanelCreate("Move",pDoc->pMove->pcName,0);
			UILabel *pLabel;
			UIExpanderGroup *pExpanderGroup = ui_ExpanderGroupCreate();
			UIButton *pButton;
			S32 y = 0;
			S32 x = 10;
			FileAssocationData *pFAData = calloc(1, sizeof(FileAssocationData));

			{
				MoveEditor_InfoBlock *pData = calloc(1, sizeof(MoveEditor_InfoBlock));
				pData->pDoc = pDoc;
				pData->pData = pPanel;
				MoveEditor_CreateTextEntryWindowButton("Set Move Name", NULL, MoveEditor_SetMoveName, pData);
			}
			pLabel = ui_LabelCreate(pDoc->pMove->pcFilename, 0, y);
			emPanelAddChild(pPanel, UI_WIDGET(pLabel), false);
			y += 25;
			pFAData->pDoc = pDoc;
			pFAData->pLabel = pLabel;
			pButton = ui_ButtonCreate("Set file association", 10, y, MoveEditor_SetFileAssociation, pFAData);
			pButton->widget.widthUnit = UIUnitFitContents;
			emPanelAddChild(pPanel, UI_WIDGET(pButton), false);
			y += 30;
			{
				MoveEditor_InfoBlock *pData = calloc(1, sizeof(MoveEditor_InfoBlock));
				pData->pDoc = pDoc;
				pData->pData = pPanel;
				MoveEditor_CreateTextEntryWindowButton("Add Move Sequence", NULL, MoveEditor_AddMoveSequence, pData);
			}
			emPanelSetHeight(pPanel, y);
			eaPush(&pDoc->emDoc.em_panels, pPanel);
			FOR_EACH_IN_EARRAY_FORWARDS(pDoc->pMove->eaDynMoveSeqs, DynMoveSeq, pMoveSeq) {
				if (pMoveSeq->pcDynMoveSeq)
					eaPush(&pDoc->emDoc.em_panels,MoveEditor_CreateMoveSeqPanel(pDoc,pMoveSeq));
				else {
					// move sequence is corrupt
					eaFindAndRemove(&pDoc->pMove->eaDynMoveSeqs,pMoveSeq);
					StructDestroy(parse_DynMoveSeq,pMoveSeq);
				}
			} FOR_EACH_END;
			return;
		}
	}
	return;
}

static void MoveEditor_UpdateCostume(MoveEditDoc *pDoc) {
	if (pDoc->currentFrame == pDoc->moveSeq->dynMoveAnimTrack.uiLast) {
		dynSkeletonForceAnimation(pDoc->costumeData.pGraphics->costume.pSkel, pDoc->moveSeq->dynMoveAnimTrack.pcAnimTrackName, pDoc->currentFrame - 0.001);
	} else {
		dynSkeletonForceAnimation(pDoc->costumeData.pGraphics->costume.pSkel, pDoc->moveSeq->dynMoveAnimTrack.pcAnimTrackName, pDoc->currentFrame);
	}
}

void MoveEditor_UIFieldFinishChanged(MEField *pField, bool bFinished, MoveEditDoc *pDoc) {
	if (bFinished) {
		MoveEditor_UpdateCostume(pDoc);
	}
}


void MoveEditor_CreateDisplaySettingsPanel(MoveEditDoc *pDoc) {
	EMPanel *pPanel = emPanelCreate("Misc", "Display Settings", 0);
	UICheckButton *pCheck;
	UISliderTextEntry *pSlider;
	UILabel *pLabel;
	MoveEditor_InfoBlock *pData;
	U32 x = 10;
	U32 y = 0;

	eaPush(&pDoc->emDoc.em_panels, pPanel);

	pCheck = ui_CheckButtonCreate(x, y, "Display Grid", pDoc->bGrid);
	ui_CheckButtonSetToggledCallback(pCheck, MoveEditor_SimpleCheckCallback, &pDoc->bGrid);
	emPanelAddChild(pPanel, UI_WIDGET(pCheck), false);
	y += 30;

	pData = calloc(1, sizeof(MoveEditor_InfoBlock));
	pData->pDoc = pDoc;
	pData->pData = &pDoc->gridDirectionAngle;
	pLabel = ui_LabelCreate("Grid Direction", x, y);
	emPanelAddChild(pPanel, UI_WIDGET(pLabel), false);
	pSlider = ui_SliderTextEntryCreate("0.0", 0.0, 360.0, x + UI_WIDGET(pLabel)->width + 10, y, 200);
	ui_SliderTextEntrySetRange(pSlider, 0.0, 360.0, 0.1);
	ui_SliderTextEntrySetPolicy(pSlider, UISliderContinuous);
	ui_SliderTextEntrySetChangedCallback(pSlider, MoveEditor_SliderChangeCallback, pData);
	ui_WidgetSetWidthEx(UI_WIDGET(pSlider), 1.0, UIUnitPercentage);
	emPanelAddChild(pPanel, UI_WIDGET(pSlider), false);
	y += 30;

	emPanelSetHeight(pPanel, y);
}

void MoveEditor_CreateUI(MoveEditDoc *pDoc)
{
	//UI Building is broken up into functions to make it easier to sort through
	//the mass blocks of code that is the UI

	eaDestroyEx(&pDoc->emDoc.em_panels, emPanelFree); // Free panel array for rebuilding
	pDoc->emDoc.em_panels = NULL;

	// --------------------------------------
	// Init Panel function calls go below
	// --------------------------------------

	MoveEditor_CreateMovePanel(pDoc);
	MoveEditor_CreateDisplaySettingsPanel(pDoc);

	// --------------------------------------
	emRefreshDocumentUI();
}

DynMove *MoveEditor_CreateMove(const char *pcName, DynMove *pMoveToClone) {
	DynMove *pMove;
	PCSkeletonDef *pSkel = NULL;
	const char *pcBaseName;
	char nameBuf[260];
	char resultBuf[260];
	int count = 0;

	if (pMoveToClone) {
		pMove = StructClone(parse_DynMove, pMoveToClone);
		assert(pMove);
		pcBaseName = pMove->pcName;
	} else {
		pMove = StructCreate(parse_DynMove);
		pcBaseName = DEFAULT_MOVE_NAME;
	}

	// Strip off trailing digits and underbar
	strcpy(nameBuf, pcBaseName);
	while(nameBuf[0] && (nameBuf[strlen(nameBuf)-1] >= '0') && (nameBuf[strlen(nameBuf)-1] <= '9')) {
		nameBuf[strlen(nameBuf)-1] = '\0';
	}
	if (nameBuf[0] && nameBuf[strlen(nameBuf)-1] == '_') {
		nameBuf[strlen(nameBuf)-1] = '\0';
	}

	// Generate new name
	do {
		++count;
		sprintf(resultBuf,"%s_%d",nameBuf,count);
	} while (MoveEditorEMIsDocOpen(resultBuf) || RefSystem_ReferentFromString(g_hPlayerCostumeDict,resultBuf));

	pMove->pcName = allocAddString(resultBuf);

	sprintf(nameBuf, "defs/Move/%s.Move", pMove->pcName);
	pMove->pcFilename = allocAddString(nameBuf);

	return pMove;
}

static void MoveEditor_UIDismissWindow(UIButton *pButton, MoveEditDoc *pDoc) {
	if (pGlobalWindow) {
		EditorPrefStoreWindowPosition(MOVE_EDITOR, "Window Position", "Save Confirm", pGlobalWindow);

		// Free the window
		ui_WindowHide(pGlobalWindow);
		ui_WidgetQueueFreeAndNull(&pGlobalWindow);
	}

	// Clear window flags
	pDoc->bSaveOverwrite = false;
	pDoc->bSaveRename = false;
}


static void MoveEditUISaveOverwrite(UIButton *pButton, MoveEditDoc *pDoc) {
	MoveEditor_UIDismissWindow(pButton, pDoc);

	pDoc->bSaveOverwrite = true;
	emSaveDocAs(&pDoc->emDoc);
}


static void MoveEditor_UISaveRename(UIButton *pButton, MoveEditDoc *pDoc) {
	MoveEditor_UIDismissWindow(pButton, pDoc);

	pDoc->bSaveRename = true;
	emSaveDoc(&pDoc->emDoc);
}


static void MoveEditor_PromptForSave(MoveEditDoc *pDoc, bool bNameCollision, bool bNameChanged) {
	UILabel *pLabel;
	UIButton *pButton;
	char buf[1024];
	int y = 0;
	int width = 0;
	int x = 0;

	assert(!pGlobalWindow); // Global window is always supposed to be null'd when done with it

	pGlobalWindow = ui_WindowCreate("Confirm Save?", 200, 200, 300, 60);

	EditorPrefGetWindowPosition(MOVE_EDITOR, "Window Position", "Save Confirm", pGlobalWindow);

	if (bNameCollision) {
		sprintf(buf, "The Move name '%s' is already in use.  Did you want to overwrite it?", pDoc->pMove->pcName);
		pLabel = ui_LabelCreate(buf, 0, y);
		ui_WindowAddChild(pGlobalWindow, pLabel);
		width = MAX(width, pLabel->widget.width + 20);
		y += 28;

		// Spawn Overwrite button
		pButton = ui_ButtonCreate("Overwrite", 0, 0, MoveEditUISaveOverwrite, pDoc);
		ui_WidgetSetWidth(UI_WIDGET(pButton), 100);
		ui_WidgetSetPositionEx(UI_WIDGET(pButton), -105, y, 0.5, 0, UITopLeft);
		ui_WindowAddChild(pGlobalWindow, pButton);

		x = 5;
		width = MAX(width, 230);
	}

	pButton = ui_ButtonCreate("Cancel", 0, 0, MoveEditor_UIDismissWindow, pDoc);
	ui_WidgetSetWidth(UI_WIDGET(pButton), 100);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), x, y, 0.5, 0, UITopLeft);
	ui_WindowAddChild(pGlobalWindow, pButton);

	y += 28;

	pGlobalWindow->widget.width = width;
	pGlobalWindow->widget.height = y;

	ui_WindowSetClosable(pGlobalWindow, false);
	ui_WindowSetModal(pGlobalWindow, true);

	ui_WindowPresent(pGlobalWindow);
}


static bool MoveEditor_DeleteMoveContinue(EMEditor *pEdit, const char *pcName, void *pMove, EMResourceState eState, void *pData, bool bSuccess) {
	if (bSuccess && (eState == EMRES_STATE_LOCK_SUCCEEDED)) {
		// Since we got the lock, continue by doing the delete save
		emSetResourceStateWithData(pEdit, pcName, EMRES_STATE_DELETING, pMove);
		resRequestSaveResource(g_hPlayerCostumeDict, pcName, NULL);
	}

	return true;
}

bool MoveEditor_SaveMoveContinue(EMEditor *pEdit, const char *pcName, MoveEditDoc *pDoc, EMResourceState eState, void *pData, bool bSuccess) {
	if (bSuccess && (eState == EMRES_STATE_SAVE_SUCCEEDED)) {
		pDoc->bSaved = true;
	}
	return true;
}

static void MoveEditor_UIDismissErrorWindow(UIButton *pButton, MoveEditDoc *pDoc) {
	if (pGlobalWindow) {
		EditorPrefStoreWindowPosition(MOVE_EDITOR, "Window Position", "Not Checked Out", pGlobalWindow);

		// Free the window
		ui_WindowHide(pGlobalWindow);
		ui_WidgetQueueFreeAndNull(&pGlobalWindow);
	}
}

static void MoveEditor_NotCheckedOutError(MoveEditDoc *pDoc) {
	UIButton *pButton;
	int y = 0;
	int width = 250;
	int x = 0;

	assert(!pGlobalWindow); // Global window is always supposed to be null'd when done with it

	pGlobalWindow = ui_WindowCreate("You can't save this. It is not checked out.", 200, 200, 300, 60);

	EditorPrefGetWindowPosition(MOVE_EDITOR, "Window Position", "Not Checked Out", pGlobalWindow);

	pButton = ui_ButtonCreate("Cancel", 0, 0, MoveEditor_UIDismissErrorWindow, pDoc);
	ui_WidgetSetWidth(UI_WIDGET(pButton), 100);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), x, y, 0.5, 0, UITopLeft);
	ui_WindowAddChild(pGlobalWindow, pButton);

	y += 28;

	pGlobalWindow->widget.width = width;
	pGlobalWindow->widget.height = y;

	ui_WindowSetClosable(pGlobalWindow, false);
	ui_WindowSetModal(pGlobalWindow, true);

	ui_WindowPresent(pGlobalWindow);
}

bool MoveEditor_GimmeCheckout(const char* filename) {
	S32 ret;

	gfxShowPleaseWaitMessage("Please wait, checking with gimme...");

	if (!gimmeDLLQueryIsFileLatest(filename)) {
		Alertf("Error: file (%s) unable to be checked out, someone else has changed it since you last got latest.  Exit, get latest and reload the file.", filename);
		return false;
	}
	ret = gimmeDLLDoOperation(filename, GIMME_CHECKOUT, 0);
	if (ret != GIMME_NO_ERROR && ret != GIMME_ERROR_NOT_IN_DB && ret != GIMME_ERROR_NO_DLL) {
		Alertf("Error checking out file \"%s\" (see console for details).", filename);
		gfxStatusPrintf("Check out FAILED on %s", filename);
		return false;
	}
	gfxStatusPrintf("You have checked out: %s", filename);
	return true;
}

// This is called to save the Move being edited
EMTaskStatus MoveEditor_SaveMove(MoveEditDoc *pDoc, bool bSaveAsNew) {
	S32	success;
	DynMove* pMove;
	DynMove* pOriginal;

	FOR_EACH_IN_EARRAY(pDoc->pMove->eaDynMoveSeqs, DynMoveSeq, pMoveSeq) {
		if (&pMoveSeq->dynMoveAnimTrack.frameSnapshot) {
			eaDestroyEx(&pMoveSeq->dynMoveAnimTrack.frameSnapshot.eaBones,NULL);
			eaDestroyEx(&pMoveSeq->dynMoveAnimTrack.frameSnapshot.eaBonesRotOnly,NULL);
		}
	} FOR_EACH_END;
	if (pDoc->bNewMove || bSaveAsNew) {
		return false;// MoveEditor_SaveMoveAs(pDoc);
	}
	if (!pDoc->pMove->pcFilename) {
		Alertf("This move (%s) is not associated with any file.", pDoc->pMove->pcName);
		return false;
	}
	pMove = RefSystem_ReferentFromString(DYNMOVE_DICTNAME, pDoc->moveName);
	assert(pMove);

	if (pDoc->pOrigRefName && (pDoc->pOrigRefName != pDoc->pMove->pcName)) {
		pOriginal = StructClone(parse_DynMove, pMove);
		RefSystem_RemoveReferent(pMove, true);
		pMove = StructClone(parse_DynMove, pDoc->pMove);
		RefSystem_AddReferent(DYNMOVE_DICTNAME, pMove->pcName, pMove);
		pDoc->pOrigRefName = pMove->pcName;
		strcpy(pDoc->moveName, pDoc->pOrigRefName);
	} else {
		pOriginal = StructClone(parse_DynMove, pMove);
		StructCopyAll(parse_DynMove,pDoc->pMove,pMove);
	}

	//gimme...

	// if move is associated with a new file from the original, save the original file without the old move.
	if (pDoc->pOrigFileAssociation && (pDoc->pOrigFileAssociation != pMove->pcFilename)) {
		if (!MoveEditor_GimmeCheckout(pDoc->pOrigFileAssociation)) {
			return false;
		}
		success = ParserWriteTextFileFromDictionary(pDoc->pOrigFileAssociation, DYNMOVE_DICTNAME, 0, TOK_USEROPTIONBIT_1);
		if (!success) {
			Alertf("Error saving Original file: %s", pDoc->pOrigFileAssociation);
			StructCopyAll(parse_DynMove, pOriginal, pMove);
			StructDestroy(parse_DynMove, pOriginal);
			return false;
		}
	}
	if (!MoveEditor_GimmeCheckout(pMove->pcFilename)) {
		return false;
	}

	success = ParserWriteTextFileFromDictionary(pMove->pcFilename, DYNMOVE_DICTNAME, 0, TOK_USEROPTIONBIT_1);

	if (!success) {
		Alertf("Error saving file: %s", pMove->pcFilename);
		StructCopyAll(parse_DynMove, pOriginal, pMove);
		StructDestroy(parse_DynMove, pOriginal);
		return false;
	} else {
		pDoc->pOrigFileAssociation = pMove->pcFilename;
		gfxStatusPrintf("Successfully saved %s", pMove->pcFilename);
		StructDestroy(parse_DynMove, pOriginal);
		emDocRemoveAllFiles(&pDoc->emDoc, false);
		emDocAssocFile(&pDoc->emDoc, pMove->pcFilename);
		return true;
	}
	return false;
}

// This is called to close the Move being edited
void MoveEditor_CloseMove(MoveEditDoc *pDoc)
{
	costumeView_FreeGraphics(pDoc->costumeData.pGraphics);
	REF_HANDLE_REMOVE(pDoc->costumeData.hCostume);
}


void MoveEditor_GotFocus(MoveEditDoc *pDoc) {
	focusedMove = pDoc;
}


void MoveEditor_LostFocus(MoveEditDoc *pDoc) {
	if (focusedMove == pDoc) {
		focusedMove = NULL;
	}
	return;
}


//---------------------------------------------------------------------------------------------------
// Graphics Logic
//---------------------------------------------------------------------------------------------------


void MoveEditor_DrawGrid(S32 uid, MoveEditDoc* pDoc, F32 distance) {
	GroupDef *ground_plane = objectLibraryGetGroupDef(uid, true);
	SingleModelParams params = {0};
	Vec2 offset;
	offset[0] = (ground_plane->bounds.min[0] + ground_plane->bounds.max[0]) / 2;
	offset[1] = (ground_plane->bounds.min[2] + ground_plane->bounds.max[2]) / 2;
	pDoc->gridPos[0] += sin(RAD(pDoc->gridDirectionAngle)) * distance;
	pDoc->gridPos[1] -= cos(RAD(pDoc->gridDirectionAngle)) * distance;
	pDoc->gridPos[0] = (pDoc->gridPos[0] > 10.0 ? pDoc->gridPos[0] - 10 : pDoc->gridPos[0]);
	pDoc->gridPos[0] = (pDoc->gridPos[0] < -10.0 ? pDoc->gridPos[0] + 10 : pDoc->gridPos[0]);
	pDoc->gridPos[1] = (pDoc->gridPos[1] > 10.0 ? pDoc->gridPos[1] - 10 : pDoc->gridPos[1]);
	pDoc->gridPos[1] = (pDoc->gridPos[1] < -10.0 ? pDoc->gridPos[1] + 10 : pDoc->gridPos[1]);
	params.model = ground_plane->model;
	params.alpha = 255;
	scaleMat3(unitmat, params.world_mat, 1.0f);
	zeroVec3(params.world_mat[3]);
	moveinX(params.world_mat, pDoc->gridPos[0] - offset[0]);
	moveinZ(params.world_mat, pDoc->gridPos[1] - offset[1]);
	params.unlit = true;
	setVec3same(params.color, 1.0f);
	params.dist_offset = -6.0f;
	params.double_sided = true;
	gfxQueueSingleModelTinted(&params, -1);
}

// This is run once per tick for drawing ghosts
void MoveEditor_DrawGhosts(MoveEditDoc *pDoc) {
	U32 iModelMemory, iMaterialMemory;
	S32 uid;

	// Check if assets changed and UI needs updating
	if (!pDoc->costumeData.pGraphics)
		return;


	if (pDoc->costumeData.uiFrameCreated < dynDebugState.uiAnimDataResetFrame)
		AnimEditor_InitCostume(&pDoc->costumeData);


	MoveEditor_TickCheckChanges(pDoc);

	if (pDoc->animate) {
		const DynMoveAnimTrack* t = &pDoc->moveSeq->dynMoveAnimTrack;
		pDoc->currentFrame += gGCLState.frameElapsedTime * 30 * pDoc->moveSeq->fSpeed;
		while (pDoc->currentFrame > t->uiLast) {
			if (pDoc->loop &&
				t->uiLast > t->uiFirst)
			{
				pDoc->currentFrame = (pDoc->currentFrame - t->uiLast) + t->uiFirst;
			} else {
				pDoc->currentFrame = t->uiLast;
				pDoc->animate = false;
				break;
			}
		}
		ui_SliderTextEntrySetValue(pDoc->activeSlider, pDoc->currentFrame);
		if (pDoc->costumeData.pGraphics) {
			if (pDoc->currentFrame == t->uiLast) {
				dynSkeletonForceAnimation(pDoc->costumeData.pGraphics->costume.pSkel, t->pcAnimTrackName, pDoc->currentFrame - 0.001);
			} else {
				dynSkeletonForceAnimation(pDoc->costumeData.pGraphics->costume.pSkel, t->pcAnimTrackName, pDoc->currentFrame);
			}
		}
	}
	AnimEditor_DrawCostumeGhosts(pDoc->costumeData.pGraphics, gGCLState.frameElapsedTime);

	gfxGetSkeletonMemoryUsage(pDoc->costumeData.pGraphics->costume.pDrawSkel, &iModelMemory, &iMaterialMemory);
	if (iModelMemory != pDoc->costumeData.pGraphics->costume.iModelMemory || iMaterialMemory != pDoc->costumeData.pGraphics->costume.iMaterialMemory) {
		char buf1[100], buf2[100];
		pDoc->costumeData.pGraphics->costume.iModelMemory = iModelMemory;
		pDoc->costumeData.pGraphics->costume.iMaterialMemory = iMaterialMemory;
		emStatusPrintf("Total geometry memory: %s    Total material memory: %s", friendlyBytesBuf(iModelMemory, buf1), friendlyBytesBuf(iMaterialMemory, buf2));
	}
	if ((pDoc->bGrid) && (uid = objectLibraryUIDFromObjName("Plane_Doublesided_500ft"))) {
		MoveEditor_DrawGrid(uid, pDoc, gGCLState.frameElapsedTime * pDoc->moveSeq->fDistance * pDoc->animate);
	}
}

void MoveEditor_Draw(MoveEditDoc *pDoc)
{
	if (!pDoc->bHasPickedCostume && (MoveEditDoc*)emGetActiveEditorDoc() == pDoc)
	{
		AnimEditor_LastCostume(NULL, MoveEditor_GetCostumePickerData);
		pDoc->bHasPickedCostume = true;
	}

	AnimEditor_DrawCostume(&pDoc->costumeData, gGCLState.frameElapsedTime);
}

//----------------------------------------------------------------
// Toolbar
//----------------------------------------------------------------

static AnimEditor_CostumePickerData* MoveEditor_GetCostumePickerData(void)
{
	MoveEditDoc* pDoc = (MoveEditDoc*)emGetActiveEditorDoc();
	assert(pDoc);
	pDoc->costumeData.getCostumePickerData = MoveEditor_GetCostumePickerData;
	pDoc->costumeData.postCostumeChange = MoveEditor_PostCostumeChangeCB;
	return &pDoc->costumeData;
}

static void MoveEditor_PostCostumeChangeCB( void ) 
{
	MoveEditDoc* pDoc = (MoveEditDoc*)emGetActiveEditorDoc();
	assert(pDoc);

	if (pDoc->moveSeq) {
		if (pDoc->currentFrame == pDoc->moveSeq->dynMoveAnimTrack.uiLast) {
			dynSkeletonForceAnimation(pDoc->costumeData.pGraphics->costume.pSkel, pDoc->moveSeq->dynMoveAnimTrack.pcAnimTrackName, pDoc->currentFrame - 0.001);
		} else {
			dynSkeletonForceAnimation(pDoc->costumeData.pGraphics->costume.pSkel, pDoc->moveSeq->dynMoveAnimTrack.pcAnimTrackName, pDoc->currentFrame);
		}
	}
}

#define BUTTON_SPACING 3.0f

#define ADD_BUTTON( text, callback, callbackdata ) \
		pButton = ui_ButtonCreate(text, fX, 0, callback, callbackdata); \
		pButton->widget.widthUnit = UIUnitFitContents; \
		emToolbarAddChild(pToolbar, pButton, false); \
		fX += ui_WidgetGetWidth(UI_WIDGET(pButton)) + BUTTON_SPACING; \

void MoveEditor_SetupToolbar(EMEditor *pEditor)
{
	EMToolbar *pToolbar;
	UIButton *pButton;

	pToolbar = emToolbarCreateFileToolbar(EM_FILE_TOOLBAR_NEW|EM_FILE_TOOLBAR_OPEN|EM_FILE_TOOLBAR_SAVE);
	eaPush(&pEditor->toolbars, pToolbar);
	eaPush(&pEditor->toolbars, emToolbarCreateWindowToolbar());

	// Custom Buttons Bar
	{
		F32 fX = 0.0f;
		pToolbar = emToolbarCreate(50);

		ADD_BUTTON("Pick Costume", AnimEditor_CostumePicker, MoveEditor_GetCostumePickerData);

		//ADD_BUTTON("Last Costume", AnimEditor_LastCostume, MoveEditor_GetCostumePickerData);

		ADD_BUTTON("Fit to Pane", AnimEditor_UIFitCameraToPane, MoveEditor_GetCostumePickerData);

		ADD_BUTTON("Fit to Screen", AnimEditor_UICenterCamera, MoveEditor_GetCostumePickerData);

		emToolbarSetWidth(pToolbar, fX);
		eaPush(&pEditor->toolbars, pToolbar);
	}
	{
		F32 fX = 0.0f;
		pToolbar = emToolbarCreate(50);

		ADD_BUTTON("Add Test FX", AnimEditor_UIAddTestFX, MoveEditor_GetCostumePickerData);

		ADD_BUTTON("Clear Test FX", AnimEditor_UIClearTestFX, MoveEditor_GetCostumePickerData);

		emToolbarSetWidth(pToolbar, fX);
		eaPush(&pEditor->toolbars, pToolbar);	
	}

}

AUTO_COMMAND ACMD_NAME("MoveEditor.ForwardFrame");
void MoveEditor_ForwardFrame(void) {
#ifndef NO_EDITORS
	if (focusedMove) {
		focusedMove->currentFrame += 1.0;
		if (focusedMove->currentFrame > focusedMove->moveSeq->dynMoveAnimTrack.uiLast) {
			focusedMove->currentFrame = focusedMove->moveSeq->dynMoveAnimTrack.uiLast;
		}
		ui_SliderTextEntrySetValue(focusedMove->activeSlider, focusedMove->currentFrame);
		if (focusedMove->currentFrame == focusedMove->moveSeq->dynMoveAnimTrack.uiLast) {
			dynSkeletonForceAnimation(focusedMove->costumeData.pGraphics->costume.pSkel, focusedMove->moveSeq->dynMoveAnimTrack.pcAnimTrackName, focusedMove->currentFrame - 0.001);
		} else {
			dynSkeletonForceAnimation(focusedMove->costumeData.pGraphics->costume.pSkel, focusedMove->moveSeq->dynMoveAnimTrack.pcAnimTrackName, focusedMove->currentFrame);
		}
	}
#endif
}

AUTO_COMMAND ACMD_NAME("MoveEditor.BackFrame");
void MoveEditor_BackFrame(void) {
#ifndef NO_EDITORS
	if (focusedMove) {
		focusedMove->currentFrame -= 1.0;
		if (focusedMove->currentFrame < focusedMove->moveSeq->dynMoveAnimTrack.uiFirst) {
			focusedMove->currentFrame = focusedMove->moveSeq->dynMoveAnimTrack.uiFirst;
		}
		ui_SliderTextEntrySetValue(focusedMove->activeSlider, focusedMove->currentFrame);
		dynSkeletonForceAnimation(focusedMove->costumeData.pGraphics->costume.pSkel, focusedMove->moveSeq->dynMoveAnimTrack.pcAnimTrackName, focusedMove->currentFrame);
	}
#endif
}

#endif
