/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef NO_EDITORS

#include "GameEditorShared.h"
#include "AnimTemplateEditor.h"
#include "error.h"
#include "file.h"

#include "dynAnimGraph.h"
#include "dynAnimTemplate.h"


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););


//-----------------------------------------------------------------------------------
// Global Data
//-----------------------------------------------------------------------------------

static EMPicker s_AnimTemplatePicker = {0};

EMEditor s_AnimTemplateEditor = {0};


//---------------------------------------------------------------------------------------------------
// Editor
//---------------------------------------------------------------------------------------------------

static EMEditorDoc *ATEM_LoadDocHelper(const char *pcNameToOpen, const char *pcType, void *pData)
{
	DynAnimTemplate *pTemplateIn = (DynAnimTemplate*)pData;
	AnimTemplateDoc *pDoc;
	const char *pcName;

	if (!gConf.bServerSaving) {
		Alertf("This editor only operates when server saving is enabled");
		return NULL;
	}

	// Open or create the object
	pDoc = ATEOpenAnimTemplate(&s_AnimTemplateEditor, (char*)pcNameToOpen, pTemplateIn);
	if (!pDoc) {
		return NULL;
	}
	pcName = pDoc->pObject->pcName;

	// Set up the document
	strcpy(pDoc->emDoc.doc_name, pcName);
	strcpy(pDoc->emDoc.doc_type, "AnimTemplate");
	strcpy(pDoc->emDoc.doc_display_name, pDoc->emDoc.doc_name);
	if (pcNameToOpen) {
		// If given a name to open, this is not a new document so set the original name
		strcpy(pDoc->emDoc.orig_doc_name, pcName);
	}

	return &pDoc->emDoc;
}

static EMEditorDoc *ATEM_LoadDoc(const char *pcNameToOpen, const char *pcType)
{
	return ATEM_LoadDocHelper(pcNameToOpen, pcType, NULL);
}

static EMEditorDoc *ATEM_NewDoc(const char *pcType, void *data)
{
	return ATEM_LoadDocHelper(NULL, pcType, data);
}


static void ATEM_ReloadDoc(EMEditorDoc *pDoc)
{
	ATERevertAnimTemplate((AnimTemplateDoc*)pDoc);
}


static void ATEM_CloseDoc(EMEditorDoc *pDoc)
{
	ATECloseAnimTemplate((AnimTemplateDoc*)pDoc);

	SAFE_FREE(pDoc);
}


static EMTaskStatus ATEM_Save(EMEditorDoc *pDoc)
{
	if (emDocIsEditable(pDoc, false))
		return ATESaveAnimTemplate((AnimTemplateDoc*)pDoc, false);
	else {
		AnimEditor_AskToCheckout(pDoc, NULL);
		return EM_TASK_FAILED;
	}
}


static EMTaskStatus ATEM_SaveAs(EMEditorDoc *pDoc)
{
	if (emDocIsEditable(pDoc, false))
		return ATESaveAnimTemplate((AnimTemplateDoc*)pDoc, true);
	else {
		AnimEditor_AskToCheckout(pDoc, NULL);
		return EM_TASK_FAILED;
	}
}


static void ATEM_Enter(EMEditor *pEditor)
{
	resSetDictionaryEditMode(hAnimTemplateDict, true);
	GERefreshMapNamesList();
}

static void ATEM_Exit(EMEditor *pEditor)
{
}


static void ATEM_Init(EMEditor *pEditor)
{
	ATEInitData(pEditor);
}

static void ATEMDocGotFocus( EMEditorDoc *pDoc )
{
	AnimTemplateDoc *pTemplateDoc = (AnimTemplateDoc*)pDoc;
	ui_WidgetAddToDevice( UI_WIDGET( ((AnimTemplateDoc*)pDoc)->pViewPane ), NULL );
	ATEGotFocus(pTemplateDoc);
}

static void ATEMDocLostFocus( EMEditorDoc *pDoc )
{
	ui_WidgetRemoveFromGroup( UI_WIDGET( ((AnimTemplateDoc*)pDoc)->pViewPane ));
}

static void ATEMDocOncePerFrame( EMEditorDoc *pDoc )
{
	dynAnimCheckDataReload();
	ATEOncePerFrame((AnimTemplateDoc*)pDoc);
}

#endif

static EMPicker* s_ATEPicker;

AUTO_RUN_LATE;
int ATEM_Register(void)
{
#ifndef NO_EDITORS
	if (!areEditorsAllowed())
		return 0;

	if (!gConf.bNewAnimationSystem)
		return 0;


	// Register template picker
	s_ATEPicker = emEasyPickerCreate( "AnimTemplate", ".atemp", "dyn/animtemplate/", NULL );
	emPickerRegister(s_ATEPicker);

	// Register the editor
	strcpy(s_AnimTemplateEditor.editor_name, ANIMTEMPLATE_EDITOR);
	s_AnimTemplateEditor.type = EM_TYPE_SINGLEDOC;
	s_AnimTemplateEditor.allow_save = 1;
	s_AnimTemplateEditor.allow_multiple_docs = 1;
	s_AnimTemplateEditor.hide_world = 1;
	s_AnimTemplateEditor.disable_auto_checkout = 1;
	s_AnimTemplateEditor.default_type = "AnimTemplate";
	strcpy(s_AnimTemplateEditor.default_workspace, "Animation Editors");

	s_AnimTemplateEditor.init_func = ATEM_Init;
	s_AnimTemplateEditor.enter_editor_func = ATEM_Enter;
	s_AnimTemplateEditor.exit_func = ATEM_Exit;
	s_AnimTemplateEditor.new_func = ATEM_NewDoc;
	s_AnimTemplateEditor.load_func = ATEM_LoadDoc;
	s_AnimTemplateEditor.reload_func = ATEM_ReloadDoc;
	s_AnimTemplateEditor.close_func = ATEM_CloseDoc;
	s_AnimTemplateEditor.save_func = ATEM_Save;
	s_AnimTemplateEditor.save_as_func = ATEM_SaveAs;
	s_AnimTemplateEditor.got_focus_func = ATEMDocGotFocus;
	s_AnimTemplateEditor.lost_focus_func = ATEMDocLostFocus;
	s_AnimTemplateEditor.draw_func = ATEMDocOncePerFrame;

	// Register the picker
	s_AnimTemplatePicker.allow_outsource = 1;
	strcpy(s_AnimTemplatePicker.picker_name, "Animation Template Library");
	strcpy(s_AnimTemplatePicker.default_type, s_AnimTemplateEditor.default_type);
	emPickerManage(&s_AnimTemplatePicker);
	eaPush(&s_AnimTemplateEditor.pickers, &s_AnimTemplatePicker);

	emRegisterEditor(&s_AnimTemplateEditor);
	emRegisterFileType(s_AnimTemplateEditor.default_type, "AnimTemplate", s_AnimTemplateEditor.editor_name);
#endif

	return 0;
}
