//
// MoveMultiEditorEM.c
//

#ifndef NO_EDITORS

#include "GameEditorShared.h"
#include "MoveMultiEditor.h"
#include "EditorPrefs.h"
#include "error.h"
#include "file.h"
#include "StringCache.h"
#include "qsortG.h"
#include "MultiEditFieldContext.h"
#include "gclCostumeView.h"

#include "dynMove.h"
#include "dynAnimGraph.h"
#include "AnimEditorCommon.h"


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););


//-----------------------------------------------------------------------------------
// Global Data
//-----------------------------------------------------------------------------------

static EMPicker mmePicker = {0};
static EMEditor mmeEditor = {0};
static MoveMultiDoc *mmeDoc = NULL;


//---------------------------------------------------------------------------------------------------
// Editor
//---------------------------------------------------------------------------------------------------

static EMEditorDoc *MME_EMLoadDoc(const char *pcNameToOpen, const char *pcType)
{
	MoveDoc *pDoc;

	if (!gConf.bServerSaving) {
		Alertf("This editor only operates when server saving is enabled");
		return NULL;
	}

	FOR_EACH_IN_EARRAY(mmeDoc->eaMoveDocs, MoveDoc, pCheckDoc)
	{
		if (!strCmp(&(pCheckDoc->pObject->pcName), &pcNameToOpen)) {
			Alertf("File already open in move multi-editor");
			return NULL;
		}
	}
	FOR_EACH_END;

	// Open or create the object
	pDoc = MMEOpenMove(&mmeEditor, mmeDoc, pcNameToOpen);
	if (!pDoc) {
		return NULL;
	}

	return &mmeDoc->emDoc;
}

static EMEditorDoc *MME_EMNewDoc(const char *pcType, void *data)
{
	return MME_EMLoadDoc(NULL, pcType);
}

static void MME_EMReloadDoc(EMEditorDoc *pDoc)
{
	//FOR_EACH_IN_EARRAY(mmeDoc->eaMoveDocs, MoveDoc, pMoveDoc)
	//	MMERevertDoc(pMoveDoc);
	//FOR_EACH_END;
	//EditUndoStackClear(mmeDoc->emDoc.edit_undo_stack);
}

static void MME_EMCloseDoc(EMEditorDoc *pDoc)
{
	FOR_EACH_IN_EARRAY(mmeDoc->eaMoveDocs, MoveDoc, pMoveDoc)
		MMECloseMove(pMoveDoc);
	FOR_EACH_END;

	EditUndoStackClear(mmeDoc->emDoc.edit_undo_stack);

	ui_WindowHide(mmeDoc->pMainWindow);
	mmeDoc->pMainWindow->show = false;
	EditorPrefStoreInt(pDoc->editor->editor_name, "Window Show", "Primary Window", pDoc->primary_ui_window->show);

	//don't do this since the vars are setup by the editor's init as globals
	/*
	emPanelFree(mmeDoc->pAllFilesPanel	);
	emPanelFree(mmeDoc->pFiltersPanel	);
	emPanelFree(mmeDoc->pMovesPanel		);
	emPanelFree(mmeDoc->pFxClipboardPanel);
	emPanelFree(mmeDoc->pVisualizePanel	);
	emPanelFree(mmeDoc->pGraphsPanel		);
	emPanelFree(mmeDoc->pSearchPanel		);

	mmeDoc->pAllFilesPanel	= NULL;
	mmeDoc->pFiltersPanel	= NULL;
	mmeDoc->pMovesPanel		= NULL;
	mmeDoc->pFxClipboardPanel = NULL;
	mmeDoc->pVisualizePanel	= NULL;
	mmeDoc->pGraphsPanel		= NULL;
	mmeDoc->pSearchPanel		= NULL;
	*/
}

static bool MME_CloseWindow(UIWindow *pWindow, UserData pUserData)
{
	EMTaskStatus retval = emCloseDoc(&mmeDoc->emDoc);

	mmeDoc->pVisualizeMoveDoc = NULL;

	mmeDoc->pcVisualizeMove = NULL;
	mmeDoc->pVisualizeMove = NULL;
	mmeDoc->pVisualizeMoveOrig = NULL;
	
	mmeDoc->pcVisualizeMoveSeq = NULL;
	mmeDoc->pVisualizeMoveSeq = NULL;
	mmeDoc->pVisualizeMoveSeqOrig = NULL;

	return (retval == EM_TASK_SUCCEEDED) ? true : false;
}

static void MME_EMExit(EMEditor *pEditor){}
static void MME_EMEnter(EMEditor *pEditor)
{
	resSetDictionaryEditMode(hDynMoveDict, true);
}

static void MME_EMInit(EMEditor *pEditor)
{
	if (!mmeDoc)
	{
		EMToolbar		*pToolbar;
		UIWindow		*pWindow;
		UIExpanderGroup	*pExpanderGroup;
		EMPanel			*pPanel;

		// Rendering setup
		costumeView_SetCamera(mmeEditor.camera);
		costumeView_InitGraphics();

		// Toolbar
		pToolbar = emToolbarCreateFileToolbar(EM_FILE_TOOLBAR_NEW|EM_FILE_TOOLBAR_OPEN);
		eaPush(&pEditor->toolbars, pToolbar);
		eaPush(&pEditor->toolbars, emToolbarCreateWindowToolbar());

		// Init custom toolbars
		MMEInitCustomToolbars(pEditor);

		// Create the global document
		mmeDoc = (MoveMultiDoc*)calloc(1,sizeof(MoveMultiDoc));
		mmeDoc->emDoc.editor = pEditor;
		mmeDoc->emDoc.saved = true;
		sprintf(mmeDoc->emDoc.doc_name, "Move Editor");
		sprintf(mmeDoc->emDoc.doc_display_name, "Move Editor");
		mmeDoc->costumeData.eaAddedFx = NULL;
		mmeDoc->costumeData.bMoveMultiEditor = 1;

		// Fx Clipboard
		mmeDoc->eaFxClipBoard = NULL;

		// Main window
		pWindow = ui_WindowCreate("Move Editor", 15, 50, 450, 600);
		ui_WindowSetCloseCallback(pWindow, MME_CloseWindow, NULL);
		EditorPrefGetWindowPosition(MME_MOVEEDITOR, "Window Position", "Main", pWindow);
		mmeDoc->emDoc.primary_ui_window = mmeDoc->pMainWindow = pWindow;
		eaPush(&mmeDoc->emDoc.ui_windows, pWindow);
		ui_WindowShow(mmeDoc->pMainWindow);
		mmeDoc->pMainWindow->show = true;
		EditorPrefStoreInt(mmeDoc->emDoc.editor->editor_name, "Window Show", "Primary Window", mmeDoc->emDoc.primary_ui_window->show);

		// Window expander group
		pExpanderGroup = ui_ExpanderGroupCreate();
		ui_WidgetSetDimensionsEx(UI_WIDGET(pExpanderGroup), 1.0, 1.0, UIUnitPercentage, UIUnitPercentage);
		ui_WindowAddChild(pWindow, pExpanderGroup);
		mmeDoc->pExpanderGroup = pExpanderGroup;

		// All Files Panel
		pPanel = emPanelCreate("Move Editor", "All Files", 0);
		eaPush(&mmeDoc->emDoc.em_panels, pPanel);
		emPanelSetOpened(pPanel, true);
		mmeDoc->pAllFilesPanel = pPanel;
		
		// Filters Panel
		pPanel = emPanelCreate("Move Editor", "Filters", 0);
		eaPush(&mmeDoc->emDoc.em_panels, pPanel);
		emPanelSetOpened(pPanel, true);
		mmeDoc->pFiltersPanel = pPanel;

		// Move Files Panel
		pPanel = emPanelCreate("Move Editor", "Move Files", 0);
		eaPush(&mmeDoc->emDoc.em_panels, pPanel);
		emPanelSetOpened(pPanel, true);
		mmeDoc->pMovesPanel = pPanel;

		// Fx Clipboard Panel
		pPanel = emPanelCreate("Move Editor", "Fx Clipboard", 0);
		eaPush(&mmeDoc->emDoc.em_panels, pPanel);
		emPanelSetOpened(pPanel, true);
		mmeDoc->pFxClipboardPanel = pPanel;

		// Visualize Panel
		pPanel = emPanelCreate("Visualize", "Visualize", 0);
		eaPush(&mmeDoc->emDoc.em_panels, pPanel);
		emPanelSetOpened(pPanel, true);
		mmeDoc->pVisualizePanel = pPanel;
		
		// Graphs Panel
		pPanel = emPanelCreate("Used by", "Used by", 0);
		eaPush(&mmeDoc->emDoc.em_panels, pPanel);
		emPanelSetOpened(pPanel, true);
		mmeDoc->pGraphsPanel = pPanel;

		// Search Panel
		pPanel = MMEInitSearchPanel(mmeDoc);
		eaPush(&mmeDoc->emDoc.em_panels, pPanel);
		emPanelSetOpened(pPanel, true);
		mmeDoc->pSearchPanel = pPanel;

		//sub-docs
		mmeDoc->eaMoveDocs = NULL;
		mmeDoc->eaSortedMoveDocs = NULL;
		mmeDoc->eaFileNames	= NULL;
		mmeDoc->eaMoveNames = NULL;
		mmeDoc->eaMoveSeqNames = NULL;

		//auto-sort setup
		mmeDoc->bOneTimeSortWindow = false;

		//visualize setup
		mmeDoc->pcVisualizeMove = NULL;
		mmeDoc->pVisualizeMove  = NULL;
		mmeDoc->pcVisualizeMoveSeq = NULL;
		mmeDoc->pVisualizeMoveSeq  = NULL;
		mmeDoc->bVisualizeCostumePicked = false;
		mmeDoc->bVisualizePlaying = false;
		mmeDoc->bVisualizeLoop = true;

		//filter setup
		mmeDoc->eMoveFilterPresentOp = mmeFilterOp_Add;
		mmeDoc->eMoveFilterAbsentOp  = mmeFilterOp_Remove;
		mmeDoc->eaFilters = NULL;
		
		// Set up the undo stack
		mmeDoc->emDoc.edit_undo_stack = EditUndoStackCreate();
		EditUndoSetContext(mmeDoc->emDoc.edit_undo_stack, mmeDoc);

		//Have Editor Manager handle a lot of change tracking
		emAutoHandleDictionaryStateChange(pEditor, DYNMOVE_DICTNAME, true, NULL, NULL, NULL, NULL, NULL);

		// Make sure lists refresh if dictionary changes
		resDictRegisterEventCallback(hDynMoveDict, MMEContentDictChanged, NULL);
	}
}

static void MME_EMOncePerFrame(EMEditorDoc *pDoc)
{
	dynAnimCheckDataReload();

	MMEOncePerFrameStart(mmeDoc);
	EARRAY_FOREACH_BEGIN(mmeDoc->eaMoveDocs, i);
	{
		//modify the counter if the doc gets deleted during it's per frame update
		if (MMEOncePerFramePerDoc(mmeDoc->eaMoveDocs[i])) i--;
	}
	EARRAY_FOREACH_END;
	MMEOncePerFrameEnd(mmeDoc);
}

static void MME_EMDrawGhost(EMEditorDoc *pDoc)
{
	MMEDrawGhost(mmeDoc);
}

static void MME_EMGotFocus(EMEditorDoc *pDoc)
{
	MoveMultiDoc *pMMDoc = (MoveMultiDoc*)pDoc;
	ui_WindowShow(pMMDoc->pMainWindow);
	pMMDoc->pMainWindow->show = true;
	EditorPrefStoreInt(pDoc->editor->editor_name, "Window Show", "Primary Window", pDoc->primary_ui_window->show);
	costumeView_SetCamera(mmeEditor.camera);
	MMEGotFocus();
}

static void MME_EMLostFocus(EMEditorDoc *pDoc)
{
	MoveMultiDoc *pMMDoc = (MoveMultiDoc*)pDoc;
	ui_WindowHide(pMMDoc->pMainWindow);
	pMMDoc->pMainWindow->show = false;
	EditorPrefStoreInt(pDoc->editor->editor_name, "Window Show", "Primary Window", pDoc->primary_ui_window->show);
	MMELostFocus();
}

#endif

AUTO_RUN_LATE;
int MME_EMRegister(void)
{

#ifndef NO_EDITORS

	if (!areEditorsAllowed())
		return 0;

#ifdef USE_NEW_MOVE_EDITOR

	// Register the editor
	strcpy(mmeEditor.editor_name, MME_MOVEEDITOR);
	mmeEditor.type = EM_TYPE_MULTIDOC;
	mmeEditor.allow_save = 0;
	mmeEditor.disable_single_doc_menus = 1;
	mmeEditor.allow_multiple_docs = 1;
	mmeEditor.hide_world = 1;
	mmeEditor.region_type = WRT_CharacterCreator;
	mmeEditor.disable_auto_checkout = 1;
	mmeEditor.default_type = DYNMOVE_TYPENAME;
	strcpy(mmeEditor.default_workspace, "Animation Editors");

	mmeEditor.keybinds_name = "MoveEditor";
	mmeEditor.keybind_version = 5;

	mmeEditor.init_func = MME_EMInit;
	mmeEditor.enter_editor_func = MME_EMEnter;
	mmeEditor.exit_func = MME_EMExit;
	mmeEditor.new_func = MME_EMNewDoc;
	mmeEditor.load_func = MME_EMLoadDoc;
	mmeEditor.reload_func = MME_EMReloadDoc;
	mmeEditor.close_func = MME_EMCloseDoc;
	mmeEditor.draw_func = MME_EMOncePerFrame;
	mmeEditor.ghost_draw_func = MME_EMDrawGhost;
	mmeEditor.got_focus_func = MME_EMGotFocus;
	mmeEditor.lost_focus_func = MME_EMLostFocus;

	// Register the picker
	mmePicker.allow_outsource = 1;
	strcpy(mmePicker.picker_name, "Move Library");
	strcpy(mmePicker.default_type, mmeEditor.default_type);
	emPickerManage(&mmePicker);
	eaPush(&mmeEditor.pickers, &mmePicker);
	emPickerRegister(&mmePicker);

	//finish editor registration
	emRegisterEditor(&mmeEditor);
	emRegisterFileType(mmeEditor.default_type, DYNMOVE_TYPENAME, mmeEditor.editor_name);

#endif
#endif

	return 0;
}
