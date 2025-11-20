/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef NO_EDITORS

#include "GameEditorShared.h"
#include "AnimGraphEditor.h"
#include "error.h"
#include "file.h"

#include "gclCostumeView.h"
#include "dynAnimGraph.h"


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););


//-----------------------------------------------------------------------------------
// Global Data
//-----------------------------------------------------------------------------------

static EMPicker s_AnimGraphPicker = {0};

EMEditor s_AnimGraphEditor = {0};


//---------------------------------------------------------------------------------------------------
// Editor
//---------------------------------------------------------------------------------------------------

static EMEditorDoc *AGEM_LoadDocHelper(const char *pcNameToOpen, const char *pcType, void *pData)
{
	DynAnimGraph *pGraphIn  = (DynAnimGraph*)pData;
	AnimGraphDoc *pDoc;
	const char *pcName;

	if (!gConf.bServerSaving) {
		Alertf("This editor only operates when server saving is enabled");
		return NULL;
	}

	costumeView_SetCamera(s_AnimGraphEditor.camera);
	costumeView_InitGraphics();

	// Open or create the object
	pDoc = AGEOpenAnimGraph(&s_AnimGraphEditor, (char*)pcNameToOpen, pGraphIn);
	if (!pDoc) {
		return NULL;
	}
	pcName = pDoc->pObject->pcName;

	// Set up the document
	strcpy(pDoc->emDoc.doc_name, pcName);
	strcpy(pDoc->emDoc.doc_type, "AnimGraph");
	strcpy(pDoc->emDoc.doc_display_name, pDoc->emDoc.doc_name);
	if (pcNameToOpen) {
		// If given a name to open, this is not a new document so set the original name
		strcpy(pDoc->emDoc.orig_doc_name, pcName);
	}

	return &pDoc->emDoc;
}

static EMEditorDoc *AGEM_LoadDoc(const char *pcNameToOpen, const char *pcType)
{
	return AGEM_LoadDocHelper(pcNameToOpen, pcType, NULL);
}

static EMEditorDoc *AGEM_NewDoc(const char *pcType, void *data)
{
	return AGEM_LoadDocHelper(NULL, pcType, data);
}


static void AGEM_ReloadDoc(EMEditorDoc *pDoc)
{
	AGERevertAnimGraph((AnimGraphDoc*)pDoc);
}


static void AGEM_CloseDoc(EMEditorDoc *pDoc)
{
	AGECloseAnimGraph((AnimGraphDoc*)pDoc);

	SAFE_FREE(pDoc);
}


static EMTaskStatus AGEM_Save(EMEditorDoc *pDoc)
{
	if (emDocIsEditable(pDoc, false))
		return AGESaveAnimGraph((AnimGraphDoc*)pDoc, false);
	else {
		AnimEditor_AskToCheckout(pDoc, NULL);
		return EM_TASK_FAILED;
	}
}


static EMTaskStatus AGEM_SaveAs(EMEditorDoc *pDoc)
{
	if (emDocIsEditable(pDoc, false))
		return AGESaveAnimGraph((AnimGraphDoc*)pDoc, true);
	else {
		AnimEditor_AskToCheckout(pDoc, NULL);
		return EM_TASK_FAILED;
	}
}


static void AGEM_Enter(EMEditor *pEditor)
{
	resSetDictionaryEditMode(hAnimGraphDict, true);
	GERefreshMapNamesList();
}

static void AGEM_Exit(EMEditor *pEditor)
{
}


static void AGEM_Init(EMEditor *pEditor)
{
	AGEInitData(pEditor);
}

static void AGEMDocGotFocus( EMEditorDoc *pDoc )
{
	AnimGraphDoc *pGraphDoc = (AnimGraphDoc*)pDoc;
	ui_WidgetAddToDevice( UI_WIDGET( ((AnimGraphDoc*)pDoc)->pViewPane ), NULL );
	ui_WidgetAddToDevice( UI_WIDGET( ((AnimGraphDoc*)pDoc)->pTimelinePane ), NULL );
	costumeView_SetCamera(s_AnimGraphEditor.camera);
	AGEGotFocus(pGraphDoc);
}

static void AGEMDocLostFocus( EMEditorDoc *pDoc )
{
	ui_WidgetRemoveFromGroup( UI_WIDGET( ((AnimGraphDoc*)pDoc)->pViewPane ));
	ui_WidgetRemoveFromGroup( UI_WIDGET( ((AnimGraphDoc*)pDoc)->pTimelinePane ));
	AGELostFocus();
}

static void AGEMDocOncePerFrame( EMEditorDoc *pDoc )
{
	dynAnimCheckDataReload();
	AGEOncePerFrame((AnimGraphDoc*)pDoc);
}

static void AGEMDocDrawGhosts( EMEditorDoc *pDoc )
{
	AGEDrawGhosts((AnimGraphDoc*)pDoc);
}

static bool AGEMShouldRevert(EMEditorDoc *pDoc)
{
	return emGetResourceState(pDoc->editor, ((AnimGraphDoc*)pDoc)->pObject->pcName) != EMRES_STATE_SAVING;
}

#endif

AUTO_RUN_LATE;
int AGEM_Register(void)
{
#ifndef NO_EDITORS
	if (!areEditorsAllowed())
		return 0;

	if (!gConf.bNewAnimationSystem)
		return 0;


	// Register the editor
	strcpy(s_AnimGraphEditor.editor_name, ANIMGRAPH_EDITOR);
	s_AnimGraphEditor.type = EM_TYPE_SINGLEDOC;
	s_AnimGraphEditor.allow_save = 1;
	s_AnimGraphEditor.allow_multiple_docs = 1;
	s_AnimGraphEditor.hide_world = 1;
	s_AnimGraphEditor.disable_auto_checkout = 1;
	s_AnimGraphEditor.region_type = WRT_CharacterCreator;

	s_AnimGraphEditor.keybinds_name = "MaterialEditor2";
	s_AnimGraphEditor.keybind_version = 1;

	s_AnimGraphEditor.default_type = "AnimGraph";
	strcpy(s_AnimGraphEditor.default_workspace, "Animation Editors");

	s_AnimGraphEditor.init_func = AGEM_Init;
	s_AnimGraphEditor.enter_editor_func = AGEM_Enter;
	s_AnimGraphEditor.exit_func = AGEM_Exit;
	s_AnimGraphEditor.new_func = AGEM_NewDoc;
	s_AnimGraphEditor.load_func = AGEM_LoadDoc;
	s_AnimGraphEditor.reload_func = AGEM_ReloadDoc;
	s_AnimGraphEditor.close_func = AGEM_CloseDoc;
	s_AnimGraphEditor.save_func = AGEM_Save;
	s_AnimGraphEditor.save_as_func = AGEM_SaveAs;
	s_AnimGraphEditor.got_focus_func = AGEMDocGotFocus;
	s_AnimGraphEditor.lost_focus_func = AGEMDocLostFocus;
	s_AnimGraphEditor.draw_func = AGEMDocOncePerFrame;
	s_AnimGraphEditor.ghost_draw_func = AGEMDocDrawGhosts;
	s_AnimGraphEditor.should_revert_func = AGEMShouldRevert;

	// Register the picker
	s_AnimGraphPicker.allow_outsource = 1;
	strcpy(s_AnimGraphPicker.picker_name, "Animation Graph Library");
	strcpy(s_AnimGraphPicker.default_type, s_AnimGraphEditor.default_type);
	emPickerManage(&s_AnimGraphPicker);
	eaPush(&s_AnimGraphEditor.pickers, &s_AnimGraphPicker);
	emPickerRegister(&s_AnimGraphPicker);


	emRegisterEditor(&s_AnimGraphEditor);
	emRegisterFileType(s_AnimGraphEditor.default_type, "AnimGraph", s_AnimGraphEditor.editor_name);
#endif

	return 0;
}
