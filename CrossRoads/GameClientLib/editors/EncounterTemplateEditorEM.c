/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef NO_EDITORS

#include "encounter_common.h"
#include "EncounterTemplateEditor.h"
#include "error.h"
#include "file.h"
#include "EditorManager.h"
#include "EditorManagerUI.h"
#include "EditorObject.h"


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););


//-----------------------------------------------------------------------------------
// Global Data
//-----------------------------------------------------------------------------------

static EMPicker s_EncounterTemplatePicker = {0};

EMEditor s_EncounterTemplateEditor = {0};


//---------------------------------------------------------------------------------------------------
// Editor
//---------------------------------------------------------------------------------------------------

static EncounterTemplateEditDoc *ETEM_LoadDocTemplate(const char *pcNameToOpen, const char *pcType) 
{
	EncounterTemplateEditDoc *pDoc;
	const char *pcName;

	if (!gConf.bServerSaving) {
		Alertf("This editor only operates when server saving is enabled");
		return NULL;
	}

	// Open or create the object
	pDoc = ETOpenTemplate(&s_EncounterTemplateEditor, (char*)pcNameToOpen);
	if (!pDoc) {
		return NULL;
	}
	pcName = pDoc->pTemplate->pcName;

	// Set up the document
	strcpy(pDoc->emDoc.doc_name, pcName);
	strcpy(pDoc->emDoc.doc_type, "EncounterTemplate");
	strcpy(pDoc->emDoc.doc_display_name, pDoc->emDoc.doc_name);
	if (pcNameToOpen) {
		// If given a name to open, this is not a new document so set the original name
		strcpy(pDoc->emDoc.orig_doc_name, pcName);
	}

	return pDoc;
}


static EMEditorDoc *ETEM_LoadDoc(const char *pcNameToOpen, const char *pcType)
{
	EncounterTemplateEditDoc *pDoc = ETEM_LoadDocTemplate(pcNameToOpen, pcType);
	return &pDoc->emDoc;
}
static EMTaskStatus ETEM_Save(EMEditorDoc *pDoc)
{
	return ETSaveTemplate((EncounterTemplateEditDoc*)pDoc, false);
}


static EMEditorDoc *ETEM_NewDoc(const char *pcType, void *data)
{
	EncounterTemplateEditDoc *pDocTemplate = NULL;
	EMEditorDoc *pDoc = NULL;

	if(data) {
		ETNewTemplateData *pData = data;
		if(pData) {
			if(!pData->bCreateChild && pData->pchOldTemplate) {
				pDocTemplate = ETEM_LoadDocTemplate(pData->pchOldTemplate, pcType);
				if(pDocTemplate) {
					pDoc = &pDocTemplate->emDoc;
					pDoc->smart_save_overwrite = true;
				}
			}

			if(!pDoc) {
				pDocTemplate = ETEM_LoadDocTemplate(NULL, pcType);
				if (pData->bCreateChild)
				{
					SET_HANDLE_FROM_STRING(g_hEncounterTemplateDict, pData->pchOldTemplate, pDocTemplate->pTemplate->hParent);
					if (pDocTemplate->pTemplate->pActorSharedProperties)
						pDocTemplate->pTemplate->pActorSharedProperties->eCritterGroupType = EncounterSharedCritterGroupSource_FromParent;
				}
				pDoc = pDocTemplate ? &pDocTemplate->emDoc : NULL;
			}

			if(pDoc && pDocTemplate)
			{
				// Set name
				if(pData->pchName) {
					strcpy(pDoc->doc_name, pData->pchName);
					strcpy(pDoc->doc_display_name, pData->pchName);
					pDocTemplate->pTemplate->pcName = strdup(pData->pchName);
				}
				// Set scope
				if(pData->pchScope) {
					pDocTemplate->pTemplate->pcScope = strdup(pData->pchScope);
				}
				// Set map to pull variables from
				if(pData->pchMapForVars) {
					pDocTemplate->pchMapForVars = strdup(pData->pchMapForVars);
				}
				// Set the "One-Off" flag
				pDocTemplate->pTemplate->bOneOff = true;
			}

			// Set document for the caller
			pData->pDoc = pDoc;
		}
	} else {
		pDocTemplate = ETEM_LoadDocTemplate(NULL, pcType);
		pDoc = pDocTemplate ? &pDocTemplate->emDoc : NULL;
	}

	return pDoc;
}


static void ETEM_ReloadDoc(EMEditorDoc *pDoc)
{
	ETRevertTemplate((EncounterTemplateEditDoc*)pDoc);
}


static void ETEM_CloseDoc(EMEditorDoc *pDoc)
{
	ETCloseTemplate((EncounterTemplateEditDoc*)pDoc);

	SAFE_FREE(pDoc);
}

static EMTaskStatus ETEM_SaveAs(EMEditorDoc *pDoc)
{
	return ETSaveTemplate((EncounterTemplateEditDoc*)pDoc, true);
}


static void ETEM_Enter(EMEditor *pEditor)
{
	resSetDictionaryEditMode(g_hEncounterTemplateDict, true);
	resSetDictionaryEditMode("FSM", true);
}

static void ETEM_Exit(EMEditor *pEditor)
{
}


static void ETEM_Init(EMEditor *pEditor)
{
	ETInitData(pEditor);
}


static void ETEM_OncePerFrame(EMEditorDoc *pDoc)
{
	edObjHarnessOncePerFrame();
}

#endif


AUTO_RUN_LATE;
int ETEM_Register(void)
{
#ifndef NO_EDITORS
	if (!areEditorsAllowed())
		return 0;

	// Register the editor
	strcpy(s_EncounterTemplateEditor.editor_name, ENCOUNTER_TEMPLATE_EDITOR);
	s_EncounterTemplateEditor.type = EM_TYPE_SINGLEDOC;
	s_EncounterTemplateEditor.allow_save = 1;
	s_EncounterTemplateEditor.allow_multiple_docs = 1;
	s_EncounterTemplateEditor.show_without_focus = 0;
	s_EncounterTemplateEditor.hide_world = 0;
	s_EncounterTemplateEditor.disable_auto_checkout = 1;
	s_EncounterTemplateEditor.default_type = "EncounterTemplate";
	strcpy(s_EncounterTemplateEditor.default_workspace, "Environment Editors");

	s_EncounterTemplateEditor.init_func = ETEM_Init;
	s_EncounterTemplateEditor.enter_editor_func = ETEM_Enter;
	s_EncounterTemplateEditor.exit_func = ETEM_Exit;
	s_EncounterTemplateEditor.new_func = ETEM_NewDoc;
	s_EncounterTemplateEditor.load_func = ETEM_LoadDoc;
	s_EncounterTemplateEditor.reload_func = ETEM_ReloadDoc;
	s_EncounterTemplateEditor.close_func = ETEM_CloseDoc;
	s_EncounterTemplateEditor.save_func = ETEM_Save;
	s_EncounterTemplateEditor.save_as_func = ETEM_SaveAs;
	s_EncounterTemplateEditor.draw_func = ETEM_OncePerFrame;

	// Register the picker
	strcpy(s_EncounterTemplatePicker.picker_name, "Encounter Template");
	strcpy(s_EncounterTemplatePicker.default_type, s_EncounterTemplateEditor.default_type);
	emPickerManage(&s_EncounterTemplatePicker);
	eaPush(&s_EncounterTemplateEditor.pickers, &s_EncounterTemplatePicker);
	emRegisterEditor(&s_EncounterTemplateEditor);

	emRegisterFileType(s_EncounterTemplateEditor.default_type, "Encounter Template", s_EncounterTemplateEditor.editor_name);

#endif

	return 0;
}
