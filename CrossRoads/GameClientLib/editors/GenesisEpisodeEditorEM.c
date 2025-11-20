/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#define GENESIS_ALLOW_OLD_HEADERS

#ifndef NO_EDITORS

#include "GenesisEpisodeEditor.h"

#include "wlGenesis.h"
#include "wlGenesisMissions.h"
#include "gameeditorshared.h"
#include "error.h"
#include "file.h"


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););


//-----------------------------------------------------------------------------------
// Global Data
//-----------------------------------------------------------------------------------

static EMPicker s_EpisodePicker = {0};

EMEditor s_EpisodeEditor = {0};


//---------------------------------------------------------------------------------------------------
// Editor
//---------------------------------------------------------------------------------------------------


static EMEditorDoc *GEPEM_LoadDoc(const char *pcNameToOpen, const char *pcType)
{
	EpisodeEditDoc *pDoc;
	const char *pcName;

	if (!gConf.bServerSaving) {
		Alertf("This editor only operates when server saving is enabled");
		return NULL;
	}

	// Open or create the object
	pDoc = GEPOpenEpisode(&s_EpisodeEditor, (char*)pcNameToOpen);
	if (!pDoc) {
		return NULL;
	}

	pcName = pDoc->pEpisode->name;

	// Set up the document
	strcpy(pDoc->emDoc.doc_name, pcName);
	strcpy(pDoc->emDoc.doc_type, "Episode");
	strcpy(pDoc->emDoc.doc_display_name, pDoc->emDoc.doc_name);
	if (pcNameToOpen) {
		// If given a name to open, this is not a new document so set the original name
		strcpy(pDoc->emDoc.orig_doc_name, pcName);
	}

	return &pDoc->emDoc;
}


static EMEditorDoc *GEPEM_NewDoc(const char *pcType, void *data)
{
	return GEPEM_LoadDoc(NULL, pcType);
}


static void GEPEM_ReloadDoc(EMEditorDoc *pDoc)
{
	GEPRevertEpisode((EpisodeEditDoc*)pDoc);
}


static void GEPEM_CloseDoc(EMEditorDoc *pDoc)
{
	GEPCloseEpisode((EpisodeEditDoc*)pDoc);

	SAFE_FREE(pDoc);
}


static EMTaskStatus GEPEM_Save(EMEditorDoc *pDoc)
{
	return GEPSaveEpisode((EpisodeEditDoc*)pDoc, false);
}


static EMTaskStatus GEPEM_SaveAs(EMEditorDoc *pDoc)
{
	return GEPSaveEpisode((EpisodeEditDoc*)pDoc, true);
}


static void GEPEM_Enter(EMEditor *pEditor)
{
	resSetDictionaryEditMode(g_EpisodeDictionary, true);
	resSetDictionaryEditMode(g_MapDescDictionary, true);
}

static void GEPEM_Exit(EMEditor *pEditor)
{
}


static void GEPEM_Init(EMEditor *pEditor)
{
	GEPInitData(pEditor);
}

#endif

AUTO_RUN_LATE;
int GEPEM_Register(void)
{
#ifndef NO_EDITORS
	if (!areEditorsAllowed())
		return 0;

	// Register the editor
	strcpy(s_EpisodeEditor.editor_name, EPISODE_EDITOR);
	s_EpisodeEditor.type = EM_TYPE_MULTIDOC;
	s_EpisodeEditor.allow_save = 1;
	s_EpisodeEditor.allow_multiple_docs = 1;
	s_EpisodeEditor.hide_world = 1;
	s_EpisodeEditor.disable_auto_checkout = 1;
	s_EpisodeEditor.default_type = "Episode";
	strcpy(s_EpisodeEditor.default_workspace, "Genesis Editors");

	s_EpisodeEditor.init_func = GEPEM_Init;
	s_EpisodeEditor.enter_editor_func = GEPEM_Enter;
	s_EpisodeEditor.exit_func = GEPEM_Exit;
	s_EpisodeEditor.new_func = GEPEM_NewDoc;
	s_EpisodeEditor.load_func = GEPEM_LoadDoc;
	s_EpisodeEditor.reload_func = GEPEM_ReloadDoc;
	s_EpisodeEditor.close_func = GEPEM_CloseDoc;
	s_EpisodeEditor.save_func = GEPEM_Save;
	s_EpisodeEditor.save_as_func = GEPEM_SaveAs;

	// Register the picker
	s_EpisodePicker.allow_outsource = 1;
	strcpy(s_EpisodePicker.picker_name, "Episode Library");
	strcpy(s_EpisodePicker.default_type, s_EpisodeEditor.default_type);
	emPickerManage(&s_EpisodePicker);
	eaPush(&s_EpisodeEditor.pickers, &s_EpisodePicker);
	emRegisterEditor(&s_EpisodeEditor);

	emRegisterFileType(s_EpisodeEditor.default_type, "Episode", s_EpisodeEditor.editor_name);
#endif

	return 0;
}
