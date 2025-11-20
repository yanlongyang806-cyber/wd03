
#include "AnimList_Common.h"
#include "ClientTargeting.h"
#include "cutscene_common.h"
#include "EditLibUIUtil.h"
#include "EditLibGizmosToolbar.h"
#include "UIGimmeButton.h"
#include "EditorManager.h"
#include "EditorPrefs.h"
#include "Entity.h"
#include "oldencounter_common.h"
#include "CostumeCommonLoad.h"
#include "dynFxInfo.h"
#include "file.h"
#include "InputKeyBind.h"
#include "InputLib.h"
#include "gclCommandParse.h"
#include "gclCutscene.h"
#include "gclDemo.h"
#include "gclEntity.h"
#include "gfxPrimitive.h"
#include "GfxTexAtlas.h"
#include "GfxSprite.h"
#include "GraphicsLib.h"
#include "SplineEditUI.h"
#include "StringCache.h"
#include "tokenstore.h"
#include "WorldEditorUI.h"
#include "wlCurve.h"
#include "WorldGrid.h"
#include "soundLib.h"
#include "dynAnimChart.h"
#include "GfxDebug.h"
#include "gimmeDLLWrapper.h"

#include "CutsceneEditor.h"

#include "cutscene_common_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

#ifndef NO_EDITORS

#define CUTSCENE_EDITOR				"Cut Scene Editor"
#define CUTSCENE_PREF_EDITOR_NAME	"CutSceneEditor"
#define CUTSCENE_PREF_CAT_UI		"UI"

static EMPicker gCutscenePicker = {0};
EMEditor gCutsceneEditor = {0};

EditLibGizmosToolbar *gpGizmosToolbar = NULL;

static void cutEdFixupClientItemData(CutsceneDef *pDef)
{
	int i, j, k;
	for (i = 0; i < eaSize(&pDef->ppEntityLists); i++)
	{
		for (j = 0; j < eaSize(&pDef->ppEntityLists[i]->eaOverrideEquipment); j++)
		{
			CutsceneEntityOverrideEquipment* pEquip = pDef->ppEntityLists[i]->eaOverrideEquipment[j];
			ItemDef* pItemDef = GET_REF(pEquip->hItem);

			eaiClear(&pEquip->eaiCategories);
			eaDestroyStruct(&pEquip->eaCostumes, parse_CostumeRefWrapper);
			if (pItemDef)
			{

				eaiCopy(&pEquip->eaiCategories, &pItemDef->peCategories);
				for (k = 0; k < eaSize(&pItemDef->ppCostumes); k++)
				{
					CostumeRefWrapper* pWrapper = StructCreate(parse_CostumeRefWrapper);
					eaPush(&pEquip->eaCostumes, pWrapper);
					COPY_HANDLE(pWrapper->hCostume, pItemDef->ppCostumes[k]->hCostumeRef);
				}
				pEquip->eMode = pItemDef->eCostumeMode;
			}
		}
	}
}

static EMTaskStatus cutEdSaveDoc(CutsceneEditorDoc* pDoc, bool bSaveAsNew)
{
	EMTaskStatus status;

	// Deal with state changes
	if(!emHandleSaveResourceState(pDoc->emDoc.editor, pDoc->state.pCutsceneDef->name, &status))
	{
		// Attempt the save
		CutsceneDef *pCutsceneDefCopy = StructClone(parse_CutsceneDef, pDoc->state.pCutsceneDef);

		cutEdFixupMessages(pCutsceneDefCopy);

		//save deeper item info for client during demoplayback
		cutEdFixupClientItemData(pCutsceneDefCopy);

		// Clone will be freed
		status = emSmartSaveDoc(&pDoc->emDoc, pCutsceneDefCopy, pDoc->state.pOriginalCutsceneDef, bSaveAsNew);
	}

	if(status == EM_TASK_SUCCEEDED)
	{
		pDoc->savingAs = 0;

		// If actually saving, change file association in case it was a rename or a new file
		emDocRemoveAllFiles(&pDoc->emDoc, false);
		emDocAssocFile(&pDoc->emDoc, pDoc->state.pCutsceneDef->filename);
	}

	return status;
}

static EMTaskStatus cutEdSaveAs(EMEditorDoc *pDocIn)
{
	CutsceneEditorDoc *pDoc = (CutsceneEditorDoc*)pDocIn;

	if(!pDoc->savingAs)
	{
		char saveDir[ MAX_PATH ];
		char saveFile[ MAX_PATH ];
		char nameBuf[ MAX_PATH ];
		if( UIOk != ui_ModalFileBrowser( "Save Cut Scene As", "Save As", UIBrowseNewNoOverwrite, UIBrowseFiles, false, 
			"defs/cutscenes", "defs/cutscenes", ".cutscene", SAFESTR( saveDir ), SAFESTR( saveFile ), pDocIn->doc_name))
		{
				return EM_TASK_FAILED;
		}
		getFileNameNoExt( saveFile, saveFile );

		if(emGetEditorDoc(saveFile, "cutscene") || resGetInfo(CUTSCENE_DICTIONARY, saveFile))
			emMakeUniqueDocName(&pDoc->emDoc, saveFile, "cutscene", CUTSCENE_DICTIONARY);
		else
			strcpy(pDoc->emDoc.doc_name, saveFile);

		strcpy(pDoc->emDoc.doc_display_name, pDoc->emDoc.doc_name);
		pDoc->emDoc.name_changed = 1;
		pDoc->emDoc.saved = 0;

		pDoc->state.pCutsceneDef->name = allocAddString(pDoc->emDoc.doc_name);
		sprintf(nameBuf, "%s/%s.cutscene", saveDir, pDoc->state.pCutsceneDef->name);
		pDoc->state.pCutsceneDef->filename = allocAddFilename(nameBuf);

		pDoc->savingAs = true;
	}

	return cutEdSaveDoc(pDoc, true);
}

static EMTaskStatus cutEdSave(EMEditorDoc *pDocIn)
{
	CutsceneEditorDoc *pDoc = (CutsceneEditorDoc*)pDocIn;
	return cutEdSaveDoc(pDoc, false);
}

static void cutEdReloadDoc(EMEditorDoc *pDocIn)
{
	CutsceneEditorDoc *pDoc = (CutsceneEditorDoc*)pDocIn;
	CutsceneDef *pCutsceneDef;

	if (!pDoc->emDoc.orig_doc_name[0]) {
		// Cannot revert if no original
		return;
	}

	pCutsceneDef = RefSystem_ReferentFromString(CUTSCENE_DICTIONARY, pDoc->emDoc.orig_doc_name);
	if (pCutsceneDef) {
		int idx;
		//Find selected path index
		idx = eaFind(&pDoc->state.pCutsceneDef->pPathList->ppPaths, pDoc->state.pSelectedPath);
		pDoc->state.pSelectedPath = NULL;

		// Revert the cut scene
		StructDestroy(parse_CutsceneDef, pDoc->state.pCutsceneDef);
		StructDestroy(parse_CutsceneDef, pDoc->state.pOriginalCutsceneDef);
		pDoc->state.pCutsceneDef = StructClone(parse_CutsceneDef, pCutsceneDef);
		assert(pDoc->state.pCutsceneDef);
		if(pDoc->state.pCutsceneDef->pPathList)
			gclCutsceneLoadSplines(pDoc->state.pCutsceneDef->pPathList);
		pDoc->state.pOriginalCutsceneDef = StructClone(parse_CutsceneDef, pDoc->state.pCutsceneDef);

		// Clear the undo stack on revert
		EditUndoStackClear(pDoc->emDoc.edit_undo_stack);
		StructDestroy(parse_CutsceneDef, pDoc->state.pNextUndoCutsceneDef);
		pDoc->state.pNextUndoCutsceneDef = StructClone(parse_CutsceneDef, pDoc->state.pCutsceneDef);

		cutEdReInitCGTs(&pDoc->state);
		cutEdInitCommon(&pDoc->state, pDoc->state.pCutsceneDef);

		//Restore previously selected path
		if(idx >= 0 && idx < eaSize(&pDoc->state.pCutsceneDef->pPathList->ppPaths))
			pDoc->state.pSelectedPath = pDoc->state.pCutsceneDef->pPathList->ppPaths[idx];
		cutEdRefreshUICommon(&pDoc->state);
	} 
}

static void cutEdCloseDoc(EMEditorDoc *pDocIn)
{
	CutsceneEditorDoc *pDoc = (CutsceneEditorDoc*)pDocIn;
	
	if (pDoc->state.pCutsceneDef)
		StructDestroy(parse_CutsceneDef, pDoc->state.pCutsceneDef);
	if (pDoc->state.pOriginalCutsceneDef)
		StructDestroy(parse_CutsceneDef, pDoc->state.pOriginalCutsceneDef);
	if (pDoc->state.pNextUndoCutsceneDef)
		StructDestroy(parse_CutsceneDef, pDoc->state.pNextUndoCutsceneDef);

	SAFE_FREE(pDoc);
}

static void cutEdGotFocus(EMEditorDoc *pDocIn)
{
	CutsceneEditorDoc *pDoc = (CutsceneEditorDoc*)pDocIn;
	ui_WidgetAddToDevice( UI_WIDGET( pDoc->state.pTimelinePane ), NULL );

	RotateGizmoSetCallbackContext(cutEdRotateGizmo(), &pDoc->state);
	TranslateGizmoSetCallbackContext(cutEdTranslateGizmo(), &pDoc->state);
}

static void cutEdLostFocus(EMEditorDoc *pDocIn)
{
	CutsceneEditorDoc *pDoc = (CutsceneEditorDoc*)pDocIn;
	cutEdStop(&pDoc->state);
	ui_WidgetRemoveFromGroup( UI_WIDGET( pDoc->state.pTimelinePane ));
}

static void cutEdEMAddToCont(EMPanel *pPanel, UIAnyWidget *pWidget)
{
	emPanelAddChild(pPanel, pWidget, true);
}

static void cutEdEMAddCont(CutsceneEditorState *pState, EMPanel *pPanel)
{
	eaPush(&pState->pParentDoc->emDoc.em_panels, pPanel);
}

static void cutEdEMRemoveCont(CutsceneEditorState *pState, EMPanel *pPanel)
{
	eaFindAndRemove(&pState->pParentDoc->emDoc.em_panels, pPanel);
}

static void cutEdSetEditMode(CutsceneEditMode mode)
{
	if(gpGizmosToolbar)
	{
		if(mode == CutsceneEditMode_Translate)
			elGizmosToolbarSetActiveGizmo(gpGizmosToolbar, cutEdTranslateGizmo());
		else
			elGizmosToolbarSetActiveGizmo(gpGizmosToolbar, cutEdRotateGizmo());
	}
}

static void cutEdInitEMData(CutsceneEditorDoc *pDoc)
{
	pDoc->state.pParentDoc = pDoc;
	pDoc->state.pInsertIntoContFunc = cutEdEMAddToCont;
	pDoc->state.pAddContFunc = cutEdEMAddCont;
	pDoc->state.pRemoveContFunc = cutEdEMRemoveCont;
	pDoc->state.pFileCont = emPanelCreate("Cut Scene", "File", 0);
	pDoc->state.pPreviewCont = emPanelCreate("Cut Scene", "Preview", 0);
	pDoc->state.pPathListCont = emPanelCreate("Cut Scene", "Path List", 0);
	pDoc->state.pPointListCont = emPanelCreate("Cut Scene", "Point List", 0);
	pDoc->state.pBasicPathCont = emPanelCreate("Cut Scene", "Basic Path", 0);
	pDoc->state.pCirclePathCont = emPanelCreate("Cut Scene", "Circle Path", 0);
	pDoc->state.pWatchPathCont = emPanelCreate("Cut Scene", "Entity Path", 0);
	pDoc->state.pPointCont = emPanelCreate("Cut Scene", "Selected Point", 0);
	pDoc->state.pRelativePosCont = emPanelCreate("Cut Scene", "Positioning", 0);
	pDoc->state.pGenPntCont = emPanelCreate("Cut Scene", "Point Common", 0);
	pDoc->state.pGenCGTCont = emPanelCreate("Cut Scene", "Path Common", 0);
	pDoc->state.pUISetEditModeFunc = cutEdSetEditMode;
}

static CutsceneEditorDoc *cutEdInitDoc(CutsceneDef *pCutsceneDef, bool bCreated, char *pcCreatedName, char *pcCreatedDir)
{
	CutsceneEditorDoc *pDoc;
	char nameBuf[260];

	// Initialize the structure
	pDoc = (CutsceneEditorDoc*)calloc(1,sizeof(CutsceneEditorDoc));

	// Fill in the cut scene data
	if (bCreated) {
		pDoc->state.pCutsceneDef = StructCreate(parse_CutsceneDef);
		assert(pDoc->state.pCutsceneDef);
		pDoc->state.pCutsceneDef->iVersion = CUTSCENE_DEF_VERSION;
		pDoc->state.pCutsceneDef->pPathList = StructCreate(parse_CutscenePathList);
		pDoc->state.pCutsceneDef->fBlendRate = CSE_DEFAULT_BLEND_RATE;
		pDoc->state.pCutsceneDef->fMinCutSceneSendRange = CSE_DEFAULT_SEND_RANGE;
		pDoc->state.pCutsceneDef->bPlayersAreUntargetable = gConf.bCutsceneDefault_bPlayersAreUntargetable;

		if(emGetEditorDoc(pcCreatedName, "cutscene") || resGetInfo(CUTSCENE_DICTIONARY, pcCreatedName))
			emMakeUniqueDocName(&pDoc->emDoc, pcCreatedName, "cutscene", CUTSCENE_DICTIONARY);
		else
			strcpy(pDoc->emDoc.doc_name, pcCreatedName);

		pDoc->state.pCutsceneDef->name = allocAddString(pDoc->emDoc.doc_name);
		sprintf(nameBuf, "%s/%s.cutscene", pcCreatedDir, pDoc->state.pCutsceneDef->name);
		pDoc->state.pCutsceneDef->filename = allocAddFilename(nameBuf);
	} else {
		pDoc->state.pCutsceneDef = StructClone(parse_CutsceneDef, pCutsceneDef);
		assert(pDoc->state.pCutsceneDef);
		if(pDoc->state.pCutsceneDef->pPathList)
		{
			gclCutsceneLoadSplines(pDoc->state.pCutsceneDef->pPathList);
		}
		else
		{
			pDoc->state.pCutsceneDef->pPathList = StructCreate(parse_CutscenePathList);
			pDoc->state.pCutsceneDef->fBlendRate = CSE_DEFAULT_BLEND_RATE;
		}
		pDoc->state.pOriginalCutsceneDef = StructClone(parse_CutsceneDef, pDoc->state.pCutsceneDef);

		emDocAssocFile(&pDoc->emDoc, pCutsceneDef->filename);
	}

	// Set up the undo stack
	pDoc->emDoc.edit_undo_stack = EditUndoStackCreate();
	EditUndoSetContext(pDoc->emDoc.edit_undo_stack, &pDoc->state);
	pDoc->state.pNextUndoCutsceneDef = StructClone(parse_CutsceneDef, pDoc->state.pCutsceneDef);

	cutEdInitEMData(pDoc);
	cutEdInitCommon(&pDoc->state, pDoc->state.pCutsceneDef);
	cutEdInitUICommon(&pDoc->state, pDoc->state.pCutsceneDef);

	return pDoc;
}

static CutsceneEditorDoc *cutEdOpenDoc(EMEditor *pEditor, char *pcName)
{
	CutsceneEditorDoc *pDoc = NULL;
	CutsceneDef *pCutsceneDef = NULL;
	bool bCreated = false;

	if (pcName && resIsEditingVersionAvailable(CUTSCENE_DICTIONARY, pcName)) {
		// Simply open the object since it is in the dictionary
		pCutsceneDef = RefSystem_ReferentFromString(CUTSCENE_DICTIONARY, pcName);
	} else if (pcName) {
		// Wait for object to show up so we can open it
		resSetDictionaryEditMode(CUTSCENE_DICTIONARY, true);
		emSetResourceState(pEditor, pcName, EMRES_STATE_OPENING);
		resRequestOpenResource(CUTSCENE_DICTIONARY, pcName);
	} else {
		// Create a new object since it is not in the dictionary
		bCreated = true;
	}

	if (pCutsceneDef || bCreated) {
		char saveDir[ MAX_PATH ];
		char saveFile[ MAX_PATH ];
		if (bCreated) {
			if( UIOk != ui_ModalFileBrowser( "New Cutscene", "New", UIBrowseNewNoOverwrite, UIBrowseFiles, false,
				"defs/cutscenes", "defs/cutscenes", ".cutscene", SAFESTR( saveDir ), SAFESTR( saveFile ), NULL))
			{
				return NULL;
			}
			getFileNameNoExt( saveFile, saveFile );
		}

		pDoc = cutEdInitDoc(pCutsceneDef, bCreated, saveFile, saveDir);
		resFixFilename(CUTSCENE_DICTIONARY, pDoc->state.pCutsceneDef->name, pDoc->state.pCutsceneDef);
	}

	return pDoc;
}

static EMEditorDoc *cutEdLoadDoc(const char *pcNameToOpen, const char *pcType)
{
	CutsceneEditorDoc *pDoc;
	const char *pcName;

	if (!gConf.bServerSaving) {
		Alertf("This editor only operates when server saving is enabled");
		return NULL;
	}

	// Open or create the object
	pDoc = cutEdOpenDoc(&gCutsceneEditor, (char*)pcNameToOpen);
	if (!pDoc) {
		return NULL;
	}
	pcName = pDoc->state.pCutsceneDef->name;

	// Set up the document
	strcpy(pDoc->emDoc.doc_name, pcName);
	strcpy(pDoc->emDoc.doc_type, "cutscene");
	strcpy(pDoc->emDoc.doc_display_name, pDoc->emDoc.doc_name);
	if (pcNameToOpen) {
		// If given a name to open, this is not a new document so set the original name
		strcpy(pDoc->emDoc.orig_doc_name, pcName);
	}

	return &pDoc->emDoc;
}

static EMEditorDoc *cutEdNewDoc(const char *pcType, void *data)
{
	return cutEdLoadDoc(NULL, pcType);
}

static void cutEdEnter(EMEditor *pEditor)
{
	resSetDictionaryEditMode(CUTSCENE_DICTIONARY, true);
}

static void cutEdGizmosUIGizmoChanged(void *gizmo)
{
	if (gizmo == cutEdTranslateGizmo())
		cutEdSetCutsceneEditMode(CutsceneEditMode_Translate);
	else if (gizmo == cutEdRotateGizmo())
		cutEdSetCutsceneEditMode(CutsceneEditMode_Rotate);
}

static void cutEdGizmosUITransGizmoChanged(TranslateGizmo *transGizmo)
{
	RotateGizmoSetAlignedToWorld(cutEdRotateGizmo(), TranslateGizmoGetAlignedToWorld(transGizmo));

	// update preferences
	EditorPrefStoreInt(CUTSCENE_PREF_EDITOR_NAME, CUTSCENE_PREF_CAT_UI, "GizmosWorldAligned", TranslateGizmoGetAlignedToWorld(transGizmo));
	EditorPrefStoreInt(CUTSCENE_PREF_EDITOR_NAME, CUTSCENE_PREF_CAT_UI, "GizmosTransSnap", TranslateGizmoGetSpecSnap(transGizmo));
	EditorPrefStoreInt(CUTSCENE_PREF_EDITOR_NAME, CUTSCENE_PREF_CAT_UI, "GizmosTransSnapRes", TranslateGizmoGetSnapResolution(transGizmo));
	EditorPrefStoreInt(CUTSCENE_PREF_EDITOR_NAME, CUTSCENE_PREF_CAT_UI, "GizmosTransSnapNormal", TranslateGizmoGetSnapNormal(transGizmo));
	EditorPrefStoreInt(CUTSCENE_PREF_EDITOR_NAME, CUTSCENE_PREF_CAT_UI, "GizmosTransSnapNormalAxis", TranslateGizmoGetSnapNormalAxis(transGizmo));
	EditorPrefStoreInt(CUTSCENE_PREF_EDITOR_NAME, CUTSCENE_PREF_CAT_UI, "GizmosTransSnapNormalInv", TranslateGizmoGetSnapNormalInverse(transGizmo));
}

static void cutEdGizmosUIRotGizmoChanged(RotateGizmo *rotGizmo)
{
	TranslateGizmoSetAlignedToWorld(cutEdTranslateGizmo(), RotateGizmoGetAlignedToWorld(rotGizmo));

	// update preferences
	EditorPrefStoreInt(CUTSCENE_PREF_EDITOR_NAME, CUTSCENE_PREF_CAT_UI, "GizmosWorldAligned", RotateGizmoGetAlignedToWorld(rotGizmo));
	EditorPrefStoreInt(CUTSCENE_PREF_EDITOR_NAME, CUTSCENE_PREF_CAT_UI, "GizmosRotSnap", RotateGizmoIsSnapEnabled(rotGizmo));
	EditorPrefStoreInt(CUTSCENE_PREF_EDITOR_NAME, CUTSCENE_PREF_CAT_UI, "GizmosRotSnapRes", RotateGizmoGetSnapResolution(rotGizmo));
}

static void cutEdInit(EMEditor *pEditor)
{
	EMToolbar *toolbar = NULL;
	int buttonWidth;

	// Have Editor Manager handle a lot of change tracking
	emAutoHandleDictionaryStateChange(pEditor, CUTSCENE_DICTIONARY, true, NULL, NULL, NULL, NULL, NULL);

	eaPush(&pEditor->toolbars, emToolbarCreateDefaultFileToolbar());
	eaPush(&pEditor->toolbars, emToolbarCreateWindowToolbar());

	cutEdInitGizmos(NULL);

	// gizmos toolbar
	toolbar = emToolbarCreateEx("Gizmos", 0);
	buttonWidth = emToolbarGetHeight(toolbar);
	eaPush(&pEditor->toolbars, toolbar);
	gpGizmosToolbar = elGizmosToolbarCreate(cutEdGizmosUIGizmoChanged, buttonWidth);
	elGizmosToolbarAddTranslateGizmo(gpGizmosToolbar, cutEdTranslateGizmo(), "Translate", cutEdGizmosUITransGizmoChanged);
	elGizmosToolbarAddRotateGizmo(gpGizmosToolbar, cutEdRotateGizmo(), "Rotate", cutEdGizmosUIRotGizmoChanged);
	emToolbarAddChild(toolbar, elGizmosToolbarGetWidget(gpGizmosToolbar), true);

	cutEdSetEditMode(CutsceneEditMode_Translate);

	// initialize toolbar preferences
	TranslateGizmoSetAlignedToWorld(cutEdTranslateGizmo(), EditorPrefGetInt(CUTSCENE_PREF_EDITOR_NAME, CUTSCENE_PREF_CAT_UI, "GizmosWorldAligned", 0));
	TranslateGizmoSetSpecSnap(cutEdTranslateGizmo(), EditorPrefGetInt(CUTSCENE_PREF_EDITOR_NAME, CUTSCENE_PREF_CAT_UI, "GizmosTransSnap", EditSnapGrid));
	TranslateGizmoSetSnapResolution(cutEdTranslateGizmo(), EditorPrefGetInt(CUTSCENE_PREF_EDITOR_NAME, CUTSCENE_PREF_CAT_UI, "GizmosTransSnapRes", 2));
	TranslateGizmoSetSnapNormal(cutEdTranslateGizmo(), EditorPrefGetInt(CUTSCENE_PREF_EDITOR_NAME, CUTSCENE_PREF_CAT_UI, "GizmosTransSnapNormal", 0));
	TranslateGizmoSetSnapNormalAxis(cutEdTranslateGizmo(), EditorPrefGetInt(CUTSCENE_PREF_EDITOR_NAME, CUTSCENE_PREF_CAT_UI, "GizmosTransSnapNormalAxis", 1));
	TranslateGizmoSetSnapNormalInverse(cutEdTranslateGizmo(), EditorPrefGetInt(CUTSCENE_PREF_EDITOR_NAME, CUTSCENE_PREF_CAT_UI, "GizmosTransSnapNormalInv", 0));
	RotateGizmoSetAlignedToWorld(cutEdRotateGizmo(), TranslateGizmoGetAlignedToWorld(cutEdTranslateGizmo()));
	RotateGizmoEnableSnap(cutEdRotateGizmo(), EditorPrefGetInt(CUTSCENE_PREF_EDITOR_NAME, CUTSCENE_PREF_CAT_UI, "GizmosRotSnap", 1));
	RotateGizmoSetSnapResolution(cutEdRotateGizmo(), EditorPrefGetInt(CUTSCENE_PREF_EDITOR_NAME, CUTSCENE_PREF_CAT_UI, "GizmosRotSnapRes", 4));

	cutEdGizmosUITransGizmoChanged(cutEdTranslateGizmo());
	cutEdGizmosUIRotGizmoChanged(cutEdRotateGizmo());
}

static void cutEdDrawDoc(EMEditorDoc *pDocIn)
{
	CutsceneEditorDoc *pDoc = (CutsceneEditorDoc*)pDocIn;
	cutEdTick(&pDoc->state);
	cutEdDraw(&pDoc->state);
}

#endif

AUTO_RUN;
int cutEdRegister(void)
{
#ifndef NO_EDITORS

	if (!areEditorsAllowed())
		return 0;

	// Register the editor
	strcpy(gCutsceneEditor.editor_name, CUTSCENE_EDITOR);
	gCutsceneEditor.type = EM_TYPE_SINGLEDOC;
	gCutsceneEditor.allow_save = 1;
	gCutsceneEditor.allow_multiple_docs = 1;
	gCutsceneEditor.hide_world = 0;
	gCutsceneEditor.disable_auto_checkout = 1;
	gCutsceneEditor.default_type = CUTSCENE_DICTIONARY;
	strcpy(gCutsceneEditor.default_workspace, "Cut Scene Editor");
	gCutsceneEditor.keybinds_name = "CutSceneEditor";
	gCutsceneEditor.keybind_version = 2;

	gCutsceneEditor.init_func = cutEdInit;
	gCutsceneEditor.enter_editor_func = cutEdEnter;
	gCutsceneEditor.new_func = cutEdNewDoc;
	gCutsceneEditor.load_func = cutEdLoadDoc;
	gCutsceneEditor.reload_func = cutEdReloadDoc;
	gCutsceneEditor.close_func = cutEdCloseDoc;
	gCutsceneEditor.save_func = cutEdSave;
	gCutsceneEditor.save_as_func = cutEdSaveAs;
	gCutsceneEditor.draw_func = cutEdDrawDoc;
	gCutsceneEditor.got_focus_func = cutEdGotFocus;
	gCutsceneEditor.lost_focus_func = cutEdLostFocus;

	// Register the picker
	gCutscenePicker.allow_outsource = 1;
	strcpy(gCutscenePicker.picker_name, "Cut Scene Library");
	strcpy(gCutscenePicker.default_type, gCutsceneEditor.default_type);
	emPickerManage(&gCutscenePicker);
	eaPush(&gCutsceneEditor.pickers, &gCutscenePicker);
	emRegisterEditor(&gCutsceneEditor);

	emRegisterFileType(gCutsceneEditor.default_type, "Cutscene", gCutsceneEditor.editor_name);
#endif

	return 0;
}
