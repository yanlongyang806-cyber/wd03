/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef NO_EDITORS

#include "GameEditorShared.h"
#include "interaction_common.h"
#include "InteractionEditor.h"
#include "error.h"
#include "file.h"


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););


//-----------------------------------------------------------------------------------
// Global Data
//-----------------------------------------------------------------------------------

static EMPicker s_InteractionPicker = {0};

EMEditor s_InteractionEditor = {0};


//---------------------------------------------------------------------------------------------------
// Editor
//---------------------------------------------------------------------------------------------------


static EMEditorDoc *IEEM_LoadDoc(const char *pcNameToOpen, const char *pcType)
{
	InteractionEditDoc *pDoc;
	const char *pcName;

	if (!gConf.bServerSaving) {
		Alertf("This editor only operates when server saving is enabled");
		return NULL;
	}

	// Open or create the object
	pDoc = IEOpenInteractionDef(&s_InteractionEditor, (char*)pcNameToOpen);
	if (!pDoc) {
		return NULL;
	}
	pcName = pDoc->pDef->pcName;

	// Set up the document
	strcpy(pDoc->emDoc.doc_name, pcName);
	strcpy(pDoc->emDoc.doc_type, "InteractionDef");
	strcpy(pDoc->emDoc.doc_display_name, pDoc->emDoc.doc_name);
	if (pcNameToOpen) {
		// If given a name to open, this is not a new document so set the original name
		strcpy(pDoc->emDoc.orig_doc_name, pcName);
	}

	return &pDoc->emDoc;
}


static EMEditorDoc *IEEM_NewDoc(const char *pcType, void *data)
{
	return IEEM_LoadDoc(NULL, pcType);
}


static void IEEM_ReloadDoc(EMEditorDoc *pDoc)
{
	IERevertInteractionDef((InteractionEditDoc*)pDoc);
}


static void IEEM_CloseDoc(EMEditorDoc *pDoc)
{
	IECloseInteractionDef((InteractionEditDoc*)pDoc);

	SAFE_FREE(pDoc);
}


static EMTaskStatus IEEM_Save(EMEditorDoc *pDoc)
{
	return IESaveInteractionDef((InteractionEditDoc*)pDoc, false);
}


static EMTaskStatus IEEM_SaveAs(EMEditorDoc *pDoc)
{
	return IESaveInteractionDef((InteractionEditDoc*)pDoc, true);
}


static void IEEM_Enter(EMEditor *pEditor)
{
	resSetDictionaryEditMode(g_InteractionDefDictionary, true);
	GERefreshMapNamesList();
}

static void IEEM_Exit(EMEditor *pEditor)
{
}


static void IEEM_Init(EMEditor *pEditor)
{
	IEInitData(pEditor);
}

#endif

AUTO_RUN_LATE;
int IEEM_Register(void)
{
#ifndef NO_EDITORS
	if (!areEditorsAllowed())
		return 0;

	// Register the editor
	strcpy(s_InteractionEditor.editor_name, INTERACTION_EDITOR);
	s_InteractionEditor.type = EM_TYPE_MULTIDOC;
	s_InteractionEditor.allow_save = 1;
	s_InteractionEditor.allow_multiple_docs = 1;
	s_InteractionEditor.hide_world = 1;
	s_InteractionEditor.disable_auto_checkout = 1;
	s_InteractionEditor.default_type = "InteractionDef";
	strcpy(s_InteractionEditor.default_workspace, "Game Design Editors");

	s_InteractionEditor.init_func = IEEM_Init;
	s_InteractionEditor.enter_editor_func = IEEM_Enter;
	s_InteractionEditor.exit_func = IEEM_Exit;
	s_InteractionEditor.new_func = IEEM_NewDoc;
	s_InteractionEditor.load_func = IEEM_LoadDoc;
	s_InteractionEditor.reload_func = IEEM_ReloadDoc;
	s_InteractionEditor.close_func = IEEM_CloseDoc;
	s_InteractionEditor.save_func = IEEM_Save;
	s_InteractionEditor.save_as_func = IEEM_SaveAs;

	// Register the picker
	s_InteractionPicker.allow_outsource = 1;
	strcpy(s_InteractionPicker.picker_name, "Interaction Def Library");
	strcpy(s_InteractionPicker.default_type, s_InteractionEditor.default_type);
	emPickerManage(&s_InteractionPicker);
	eaPush(&s_InteractionEditor.pickers, &s_InteractionPicker);
	emRegisterEditor(&s_InteractionEditor);

	emRegisterFileType(s_InteractionEditor.default_type, "InteractionDef", s_InteractionEditor.editor_name);
#endif

	return 0;
}
