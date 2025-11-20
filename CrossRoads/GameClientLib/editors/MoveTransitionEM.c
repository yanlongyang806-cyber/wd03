/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef NO_EDITORS

#include "GameEditorShared.h"
#include "MoveTransitionEditor.h"
#include "error.h"
#include "file.h"

#include "dynMoveTransition.h"
#include "dynAnimGraph.h"
#include "AnimEditorCommon.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););


//-----------------------------------------------------------------------------------
// Global Data
//-----------------------------------------------------------------------------------

static EMPicker s_MoveTransitionPicker = {0};

EMEditor s_MoveTransitionEditor = {0};


//---------------------------------------------------------------------------------------------------
// Editor
//---------------------------------------------------------------------------------------------------

static EMEditorDoc *MTEM_LoadDocHelper(const char *pcNameToOpen, const char *pcType, const void *pData)
{
	DynMoveTransition *pMoveTransitionIn  = (DynMoveTransition *)pData;
	MoveTransitionDoc *pDoc;
	const char *pcName;

	if (!gConf.bServerSaving) {
		Alertf("This editor only operates when server saving is enabled");
		return NULL;
	}

	// Open or create the object
	pDoc = MTEOpenMoveTransition(&s_MoveTransitionEditor, (char*)pcNameToOpen, pMoveTransitionIn);
	if (!pDoc) {
		return NULL;
	}
	pcName = pDoc->pObject->pcName;

	// Set up the document
	strcpy(pDoc->emDoc.doc_name, pcName);
	strcpy(pDoc->emDoc.doc_type, MOVE_TRANSITION_EDITED_DICTIONARY);
	strcpy(pDoc->emDoc.doc_display_name, pDoc->emDoc.doc_name);
	if (pcNameToOpen) {
		// If given a name to open, this is not a new document so set the original name
		strcpy(pDoc->emDoc.orig_doc_name, pcName);
	}

	return &pDoc->emDoc;
}

static EMEditorDoc *MTEM_LoadDoc(const char *pcNameToOpen, const char *pcType)
{
	return MTEM_LoadDocHelper(pcNameToOpen, pcType, NULL);
}

static EMEditorDoc *MTEM_NewDoc(const char *pcType, void *data)
{
	return MTEM_LoadDocHelper(NULL, pcType, data);
}


static void MTEM_ReloadDoc(EMEditorDoc *pDoc)
{
	MTERevertMoveTransition((MoveTransitionDoc*)pDoc);
}


static void MTEM_CloseDoc(EMEditorDoc *pDoc)
{
	MTECloseMoveTransition((MoveTransitionDoc*)pDoc);

	SAFE_FREE(pDoc);
}


static EMTaskStatus MTEM_Save(EMEditorDoc *pDoc)
{
	if (emDocIsEditable(pDoc, false))
		return MTESaveMoveTransition((MoveTransitionDoc*)pDoc, false);
	else {
		AnimEditor_AskToCheckout(pDoc, NULL);
		return EM_TASK_FAILED;
	}
}


static EMTaskStatus MTEM_SaveAs(EMEditorDoc *pDoc)
{
	if (emDocIsEditable(pDoc, false))
		return MTESaveMoveTransition((MoveTransitionDoc*)pDoc, true);
	else {
		AnimEditor_AskToCheckout(pDoc, NULL);
		return EM_TASK_FAILED;
	}
}


static void MTEM_Enter(EMEditor *pEditor)
{
	resSetDictionaryEditMode(hMoveTransitionDict, true);
	GERefreshMapNamesList();
}

static void MTEM_Exit(EMEditor *pEditor)
{
}


static void MTEM_Init(EMEditor *pEditor)
{
	MTEInitData(pEditor);
}

static void MTEM_OncePerFrame(EMEditorDoc *pDoc)
{
	dynAnimCheckDataReload();
	MTEOncePerFrame((MoveTransitionDoc*)pDoc);
}

static void MTEM_LostFocus(EMEditorDoc *pDoc)
{
	MTELostFocus((MoveTransitionDoc*)pDoc);
}

static void MTEM_GotFocus(EMEditorDoc *pDoc)
{
	MTEGotFocus((MoveTransitionDoc*)pDoc);
}

#endif

AUTO_RUN_LATE;
int MTEM_Register(void)
{
#ifndef NO_EDITORS
	if (!areEditorsAllowed())
		return 0;

	if (!gConf.bNewAnimationSystem)
		return 0;

	// Register the editor
	strcpy(s_MoveTransitionEditor.editor_name, MOVE_TRANSITION_EDITOR);
	s_MoveTransitionEditor.type = EM_TYPE_SINGLEDOC;
	s_MoveTransitionEditor.allow_save = 1;
	s_MoveTransitionEditor.allow_multiple_docs = 1;
	s_MoveTransitionEditor.hide_world = 1;
	s_MoveTransitionEditor.disable_auto_checkout = 1;
	s_MoveTransitionEditor.default_type = MOVE_TRANSITION_EDITED_DICTIONARY;
	strcpy(s_MoveTransitionEditor.default_workspace, "Animation Editors");

	s_MoveTransitionEditor.init_func			= MTEM_Init;
	s_MoveTransitionEditor.enter_editor_func	= MTEM_Enter;
	s_MoveTransitionEditor.exit_func			= MTEM_Exit;
	s_MoveTransitionEditor.new_func				= MTEM_NewDoc;
	s_MoveTransitionEditor.load_func			= MTEM_LoadDoc;
	s_MoveTransitionEditor.reload_func			= MTEM_ReloadDoc;
	s_MoveTransitionEditor.close_func			= MTEM_CloseDoc;
	s_MoveTransitionEditor.save_func			= MTEM_Save;
	s_MoveTransitionEditor.save_as_func			= MTEM_SaveAs;
	s_MoveTransitionEditor.draw_func			= MTEM_OncePerFrame;
	s_MoveTransitionEditor.lost_focus_func		= MTEM_LostFocus;
	s_MoveTransitionEditor.got_focus_func		= MTEM_GotFocus;

	// Register the picker
	s_MoveTransitionPicker.allow_outsource = 1;
	strcpy(s_MoveTransitionPicker.picker_name, "Move Transition Library");
	strcpy(s_MoveTransitionPicker.default_type, s_MoveTransitionEditor.default_type);
	emPickerManage(&s_MoveTransitionPicker);
	eaPush(&s_MoveTransitionEditor.pickers, &s_MoveTransitionPicker);
	emPickerRegister(&s_MoveTransitionPicker);
	emRegisterEditor(&s_MoveTransitionEditor);

	emRegisterFileType(s_MoveTransitionEditor.default_type, MOVE_TRANSITION_EDITED_DICTIONARY, s_MoveTransitionEditor.editor_name);
#endif

	return 0;
}
