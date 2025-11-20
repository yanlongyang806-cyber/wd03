/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#define GENESIS_ALLOW_OLD_HEADERS
#ifndef NO_EDITORS

#include "GenesisMapDescriptionEditor.h"

#include "wlGenesis.h"
#include "gameeditorshared.h"
#include "error.h"
#include "file.h"


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););


//-----------------------------------------------------------------------------------
// Global Data
//-----------------------------------------------------------------------------------

static EMPicker s_MapDescPicker = {0};

EMEditor s_MapDescEditor = {0};


//---------------------------------------------------------------------------------------------------
// Editor
//---------------------------------------------------------------------------------------------------


static EMEditorDoc *GMDEM_LoadDoc(const char *pcNameToOpen, const char *pcType)
{
	MapDescEditDoc *pDoc;
	const char *pcName;

	if (!gConf.bServerSaving) {
		Alertf("This editor only operates when server saving is enabled");
		return NULL;
	}

	// Open or create the object
	pDoc = GMDOpenMapDesc(&s_MapDescEditor, (char*)pcNameToOpen);
	if (!pDoc) {
		return NULL;
	}
	pcName = pDoc->pMapDesc->name;

	// Set up the document
	strcpy(pDoc->emDoc.doc_name, pcName);
	strcpy(pDoc->emDoc.doc_type, "MapDescription");
	strcpy(pDoc->emDoc.doc_display_name, pDoc->emDoc.doc_name);
	if (pcNameToOpen) {
		// If given a name to open, this is not a new document so set the original name
		strcpy(pDoc->emDoc.orig_doc_name, pcName);
	}

	return &pDoc->emDoc;
}


static EMEditorDoc *GMDEM_NewDoc(const char *pcType, void *data)
{
	return GMDEM_LoadDoc(NULL, pcType);
}


static void GMDEM_ReloadDoc(EMEditorDoc *pDoc)
{
	GMDRevertMapDesc((MapDescEditDoc*)pDoc);
}


static void GMDEM_CloseDoc(EMEditorDoc *pDoc)
{
	GMDCloseMapDesc((MapDescEditDoc*)pDoc);

	SAFE_FREE(pDoc);
}


static EMTaskStatus GMDEM_Save(EMEditorDoc *pDoc)
{
	return GMDSaveMapDesc((MapDescEditDoc*)pDoc, false);
}


static EMTaskStatus GMDEM_SaveAs(EMEditorDoc *pDoc)
{
	return GMDSaveMapDesc((MapDescEditDoc*)pDoc, true);
}


static void GMDEM_Enter(EMEditor *pEditor)
{
	resSetDictionaryEditMode(g_MapDescDictionary, true);
	resRequestAllResourcesInDictionary("NameTemplateList");
	resRequestAllResourcesInDictionary("PhonemeSet");
	GERefreshMapNamesList();
}

static void GMDEM_Exit(EMEditor *pEditor)
{
}


static void GMDEM_Init(EMEditor *pEditor)
{
	GMDInitData(pEditor);
}

#endif

AUTO_RUN_LATE;
int GMDEM_Register(void)
{
#ifndef NO_EDITORS
	if (!areEditorsAllowed())
		return 0;

	// Register the editor
	strcpy(s_MapDescEditor.editor_name, MAPDESC_EDITOR);
	s_MapDescEditor.type = EM_TYPE_MULTIDOC;
	s_MapDescEditor.allow_save = 1;
	s_MapDescEditor.allow_multiple_docs = 1;
	s_MapDescEditor.hide_world = 1;
	s_MapDescEditor.disable_auto_checkout = 1;
	s_MapDescEditor.default_type = "MapDescription";
	strcpy(s_MapDescEditor.default_workspace, "Genesis Editors");

	s_MapDescEditor.init_func = GMDEM_Init;
	s_MapDescEditor.enter_editor_func = GMDEM_Enter;
	s_MapDescEditor.exit_func = GMDEM_Exit;
	s_MapDescEditor.new_func = GMDEM_NewDoc;
	s_MapDescEditor.load_func = GMDEM_LoadDoc;
	s_MapDescEditor.reload_func = GMDEM_ReloadDoc;
	s_MapDescEditor.close_func = GMDEM_CloseDoc;
	s_MapDescEditor.save_func = GMDEM_Save;
	s_MapDescEditor.save_as_func = GMDEM_SaveAs;

	// Register the picker
	s_MapDescPicker.allow_outsource = 1;
	strcpy(s_MapDescPicker.picker_name, "Map Description Library");
	strcpy(s_MapDescPicker.default_type, s_MapDescEditor.default_type);
	emPickerManage(&s_MapDescPicker);
	eaPush(&s_MapDescEditor.pickers, &s_MapDescPicker);
	emRegisterEditor(&s_MapDescEditor);

	emRegisterFileType(s_MapDescEditor.default_type, "MapDescription", s_MapDescEditor.editor_name);
#endif

	return 0;
}
