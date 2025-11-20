/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef NO_EDITORS

#include "GameEditorShared.h"
#include "AnimChartEditor.h"
#include "error.h"
#include "file.h"
#include "EditorPrefs.h"

#include "dynAnimChart.h"
#include "dynAnimGraph.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););


//-----------------------------------------------------------------------------------
// Global Data
//-----------------------------------------------------------------------------------

static EMPicker s_AnimChartPicker = {0};
static EMPicker s_MovementSetPicker = {0};

EMEditor s_AnimChartEditor = {0};


//---------------------------------------------------------------------------------------------------
// Editor
//---------------------------------------------------------------------------------------------------


static EMEditorDoc *ACEM_LoadDocHelper(const char *pcNameToOpen, const char *pcType, void *pData)
{
	DynAnimChartLoadTime *pChartIn  = (DynAnimChartLoadTime*)pData;
	AnimChartDoc *pDoc;
	const char *pcName;

	if (!gConf.bServerSaving) {
		Alertf("This editor only operates when server saving is enabled");
		return NULL;
	}

	// Open or create the object
	pDoc = ACEOpenAnimChart(&s_AnimChartEditor, (char*)pcNameToOpen, pChartIn);
	if (!pDoc) {
		return NULL;
	}
	pcName = pDoc->pObject->pcName;

	// Set up the document
	strcpy(pDoc->emDoc.doc_name, pcName);
	strcpy(pDoc->emDoc.doc_type, ANIM_CHART_EDITED_DICTIONARY);
	strcpy(pDoc->emDoc.doc_display_name, pDoc->emDoc.doc_name);
	if (pcNameToOpen) {
		// If given a name to open, this is not a new document so set the original name
		strcpy(pDoc->emDoc.orig_doc_name, pcName);
	}

	return &pDoc->emDoc;
}

static EMEditorDoc *ACEM_LoadDoc(const char *pcNameToOpen, const char *pcType)
{
	return ACEM_LoadDocHelper(pcNameToOpen, pcType, NULL);
}

static EMEditorDoc *ACEM_NewDoc(const char *pcType, void *data)
{
	return ACEM_LoadDocHelper(NULL, pcType, data);
}


static void ACEM_ReloadDoc(EMEditorDoc *pDoc)
{
	ACERevertAnimChart((AnimChartDoc*)pDoc);
}


static void ACEM_CloseDoc(EMEditorDoc *pDoc)
{
	ACECloseAnimChart((AnimChartDoc*)pDoc);

	SAFE_FREE(pDoc);
}


static EMTaskStatus ACEM_Save(EMEditorDoc *pDoc)
{
	if (emDocIsEditable(pDoc, false))
		return ACESaveAnimChart((AnimChartDoc*)pDoc, false);
	else {
		AnimEditor_AskToCheckout(pDoc, NULL);
		return EM_TASK_FAILED;
	}
}


static EMTaskStatus ACEM_SaveAs(EMEditorDoc *pDoc)
{
	if (emDocIsEditable(pDoc, false))
		return ACESaveAnimChart((AnimChartDoc*)pDoc, true);
	else {
		AnimEditor_AskToCheckout(pDoc, NULL);
		return EM_TASK_FAILED;
	}
}


static void ACEM_Enter(EMEditor *pEditor)
{
	resSetDictionaryEditMode(hAnimChartDictLoadTime, true);
	GERefreshMapNamesList();
}

static void ACEM_Exit(EMEditor *pEditor)
{
}


static void ACEM_Init(EMEditor *pEditor)
{
	ACEInitData(pEditor);
}

static void ACEM_OncePerFrame( EMEditorDoc *pDoc )
{
	dynAnimCheckDataReload();
	ACEOncePerFrame((AnimChartDoc*)pDoc);
}

static void ACEM_GotFocus(EMEditorDoc *pDoc)
{
	AnimChartDoc *pChartDoc = (AnimChartDoc*)pDoc;
	ui_WindowShow(pChartDoc->pMainWindow);
	pChartDoc->pMainWindow->show = true;
	EditorPrefStoreInt(pDoc->editor->editor_name, "Window Show", "Primary Window", pDoc->primary_ui_window->show);
	ACEGotFocus(pChartDoc);
}

static void ACEM_LostFocus( EMEditorDoc *pDoc )
{
	AnimChartDoc *pChartDoc = (AnimChartDoc*)pDoc;
	ui_WindowHide(pChartDoc->pMainWindow);
	pChartDoc->pMainWindow->show = false;
	EditorPrefStoreInt(pDoc->editor->editor_name, "Window Show", "Primary Window", pDoc->primary_ui_window->show);
	ACELostFocus(pChartDoc);
}

#endif

AUTO_RUN_LATE;
int ACEM_Register(void)
{
#ifndef NO_EDITORS
	if (!areEditorsAllowed())
		return 0;

	if (!gConf.bNewAnimationSystem)
		return 0;

	// Register the editor
	strcpy(s_AnimChartEditor.editor_name, ANIMCHART_EDITOR);
	s_AnimChartEditor.type = EM_TYPE_MULTIDOC;
	s_AnimChartEditor.allow_save = 1;
	s_AnimChartEditor.allow_multiple_docs = 1;
	s_AnimChartEditor.hide_world = 1;
	s_AnimChartEditor.disable_auto_checkout = 1;
	s_AnimChartEditor.default_type = ANIM_CHART_EDITED_DICTIONARY;
	strcpy(s_AnimChartEditor.default_workspace, "Animation Editors");

	s_AnimChartEditor.init_func = ACEM_Init;
	s_AnimChartEditor.enter_editor_func = ACEM_Enter;
	s_AnimChartEditor.exit_func = ACEM_Exit;
	s_AnimChartEditor.new_func = ACEM_NewDoc;
	s_AnimChartEditor.load_func = ACEM_LoadDoc;
	s_AnimChartEditor.reload_func = ACEM_ReloadDoc;
	s_AnimChartEditor.close_func = ACEM_CloseDoc;
	s_AnimChartEditor.save_func = ACEM_Save;
	s_AnimChartEditor.save_as_func = ACEM_SaveAs;
	s_AnimChartEditor.draw_func = ACEM_OncePerFrame;
	s_AnimChartEditor.got_focus_func = ACEM_GotFocus;
	s_AnimChartEditor.lost_focus_func = ACEM_LostFocus;

	// Register the picker
	s_AnimChartPicker.allow_outsource = 1;
	strcpy(s_AnimChartPicker.picker_name, "Animation Chart Library");
	strcpy(s_AnimChartPicker.default_type, s_AnimChartEditor.default_type);
	emPickerManage(&s_AnimChartPicker);
	eaPush(&s_AnimChartEditor.pickers, &s_AnimChartPicker);
	emPickerRegister(&s_AnimChartPicker);
	emRegisterEditor(&s_AnimChartEditor);

	// Register the movement set 
	s_MovementSetPicker.allow_outsource = 1;
	strcpy(s_MovementSetPicker.picker_name, "DynMovementSet Library");
	strcpy(s_MovementSetPicker.default_type, "DynMovementSet");
	emPickerManage(&s_MovementSetPicker);
	emPickerRegister(&s_MovementSetPicker);

	emRegisterFileType(s_AnimChartEditor.default_type, "AnimChart", s_AnimChartEditor.editor_name);
#endif

	return 0;
}
