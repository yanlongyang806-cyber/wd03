/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef NO_EDITORS

#include "file.h"
#include "gameeditorshared.h"
#include "mission_common.h"
#include "missioneditor.h"
#include "contacteditor.h"
#include "GameClientLib.h"


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););


//-----------------------------------------------------------------------------------
// Global Data
//-----------------------------------------------------------------------------------

static EMPicker s_MissionPicker = {0};

EMEditor s_MissionEditor = {0};


//---------------------------------------------------------------------------------------------------
// Editor
//---------------------------------------------------------------------------------------------------


static EMEditorDoc *missionEditorEMLoadDoc(const char *pcNameToOpen, const char *pcType)
{
	MissionEditDoc *pDoc;
	const char *pcName;

	if (!gConf.bServerSaving) {
		Alertf("This editor only operates when server saving is enabled");
		return NULL;
	}

	// Open or create the contact
	pDoc = MDEOpenMission(&s_MissionEditor, (char*)pcNameToOpen);
	if (!pDoc) {
		return NULL;
	}
	pcName = pDoc->pMission->name;

	// Set up the document
	strcpy(pDoc->emDoc.doc_name, pcName);
	strcpy(pDoc->emDoc.doc_type, "mission");
	strcpy(pDoc->emDoc.doc_display_name, pDoc->emDoc.doc_name);
	if (pcNameToOpen) {
		// If given a name to open, this is not a new document so set the original name
		strcpy(pDoc->emDoc.orig_doc_name, pcName);
	}

	// Initialize the dialog flow window
	pDoc->pDialogFlowWindowInfo = CEInitDialogFlowWindowWithMissionDoc(pDoc);
	CERefreshDialogFlowWindow(pDoc->pDialogFlowWindowInfo);

	return &pDoc->emDoc;
}

static void missionEditorEMDraw(EMEditorDoc *pDoc)
{
	MDEOncePerFrame(pDoc);
}


static EMEditorDoc *missionEditorEMNewDoc(const char *pcType, void *data)
{
	return missionEditorEMLoadDoc(NULL, pcType);
}


static void missionEditorEMReloadDoc(EMEditorDoc *pDoc)
{
	MDERevertMission((MissionEditDoc*)pDoc);
}


static void missionEditorEMCloseDoc(EMEditorDoc *pDoc)
{
	MDECloseMission((MissionEditDoc*)pDoc);

	SAFE_FREE(pDoc);
}


static EMTaskStatus missionEditorEMSave(EMEditorDoc *pDoc)
{
	return MDESaveMission((MissionEditDoc*)pDoc, false);
}


static EMTaskStatus missionEditorEMSaveAs(EMEditorDoc *pDoc)
{
	return MDESaveMission((MissionEditDoc*)pDoc, true);
}


static void missionEditorEMEnter(EMEditor *pEditor)
{
	resSetDictionaryEditMode(g_MissionDictionary, true);
	resSetDictionaryEditMode(gMessageDict, true);

	GERefreshMapNamesList();
}

static void missionEditorEMExit(EMEditor *pEditor)
{

}


static void missionEditorEMInit(EMEditor *pEditor)
{
	MDEInitData(pEditor);
}


#endif

AUTO_RUN_LATE;
int missionEditorEMRegister(void)
{
#ifndef NO_EDITORS
	static bool bInited = false;
	if (!areEditorsAllowed() && areEditorsPossible())
	{
		if (!gGCLState.bRunning)
			gclRegisterEditChangeCallback(missionEditorEMRegister);

		return 0;
	}

	if (bInited)
	{
		return 0;
	}
	bInited = true;

	// Register the editor
	strcpy(s_MissionEditor.editor_name, MISSION_EDITOR);
	s_MissionEditor.type = EM_TYPE_MULTIDOC;
	s_MissionEditor.allow_save = 1;
	s_MissionEditor.allow_multiple_docs = 1;
	s_MissionEditor.hide_world = 1;
	s_MissionEditor.disable_auto_checkout = 1;
	s_MissionEditor.default_type = "Mission";
	strcpy(s_MissionEditor.default_workspace, "Game Design Editors");

	s_MissionEditor.init_func = missionEditorEMInit;
	s_MissionEditor.enter_editor_func = missionEditorEMEnter;
	s_MissionEditor.exit_func = missionEditorEMExit;
	s_MissionEditor.new_func = missionEditorEMNewDoc;
	s_MissionEditor.load_func = missionEditorEMLoadDoc;
	s_MissionEditor.reload_func = missionEditorEMReloadDoc;
	s_MissionEditor.close_func = missionEditorEMCloseDoc;
	s_MissionEditor.save_func = missionEditorEMSave;
	s_MissionEditor.save_as_func = missionEditorEMSaveAs;
	s_MissionEditor.draw_func = missionEditorEMDraw;

	// Register the picker
	s_MissionPicker.allow_outsource = 1;
	strcpy(s_MissionPicker.picker_name, "Mission Library");
	strcpy(s_MissionPicker.default_type, s_MissionEditor.default_type);
	emPickerManage(&s_MissionPicker);
	eaPush(&s_MissionEditor.pickers, &s_MissionPicker);
	emRegisterEditor(&s_MissionEditor);

	emRegisterFileType(s_MissionEditor.default_type, "Mission", s_MissionEditor.editor_name);

#endif 
	return 0;
}

