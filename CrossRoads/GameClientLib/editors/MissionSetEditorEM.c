//
// MissionSetEditorEM.c
//

#ifndef NO_EDITORS

#include "error.h"
#include "file.h"
#include "MultiEditTable.h"
#include "MultiEditWindow.h"
#include "MissionSetEditor.h"


// This magic line is required if you run in Full Debug to avoid assert
AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

EMEditor gMissionSetEditor;

EMPicker gMissionSetPicker;

static MultiEditEMDoc *gMissionSetEditorDoc = NULL;


//---------------------------------------------------------------------------------------------------
// Editor
//---------------------------------------------------------------------------------------------------


static EMEditorDoc *missionSetEditorEMNewDoc(const char *pcType, void *data)
{
	if (!gConf.bServerSaving) {
		Alertf("This editor only operates when server saving is enabled");
		return NULL;
	}
	missionSetEditor_createMissionSet(NULL);

	return (EMEditorDoc*)gMissionSetEditorDoc;
}


static EMEditorDoc *missionSetEditorEMLoadDoc(const char *pcName, const char *pcType)
{
	if (!gConf.bServerSaving) {
		Alertf("This editor only operates when server saving is enabled");
		return NULL;
	}
	MEWindowOpenObject(gMissionSetEditorDoc->pWindow, (char*)pcName);

	return (EMEditorDoc*)gMissionSetEditorDoc;
}


static EMTaskStatus missionSetEditorEMSaveDoc(EMEditorDoc *pDoc)
{
	return MEWindowSaveAll(gMissionSetEditorDoc->pWindow);
}


static EMTaskStatus missionSetEditorEMSaveSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	return MEWindowSaveObject(gMissionSetEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void missionSetEditorEMCloseDoc(EMEditorDoc *pDoc)
{
	MEWindowExit(gMissionSetEditorDoc->pWindow);
}


static void missionSetEditorEMCloseSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	MEWindowCloseObject(gMissionSetEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void missionSetEditorEMReloadSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	MEWindowRevertObject(gMissionSetEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void missionSetEditorEMEnter(EMEditor *pEditor)
{

}

static void missionSetEditorEMExit(EMEditor *pEditor)
{

}

static void missionSetEditorEMLostFocus(EMEditorDoc *pDoc)
{
	MEWindowLostFocus(gMissionSetEditorDoc->pWindow);
}

static void missionSetEditorEMInit(EMEditor *pEditor)
{
	// Create the one global document
	if (!gMissionSetEditorDoc) {
		// Create the global document
		gMissionSetEditorDoc = calloc(1,sizeof(MultiEditEMDoc));
		gMissionSetEditorDoc->emDoc.editor = &gMissionSetEditor;
		gMissionSetEditorDoc->emDoc.saved = true;
		gMissionSetEditorDoc->pWindow = missionSetEditor_init(gMissionSetEditorDoc);
		sprintf(gMissionSetEditorDoc->emDoc.doc_name, "MissionSet Editor");
		sprintf(gMissionSetEditorDoc->emDoc.doc_display_name, "MissionSet Editor");
		gMissionSetEditorDoc->emDoc.primary_ui_window = MEWindowGetUIWindow(gMissionSetEditorDoc->pWindow);
	}
}

#endif

AUTO_RUN_LATE;
int missionSetEditorEMRegister(void)
{
#ifndef NO_EDITORS
	if (!areEditorsAllowed())
		return 0;

	// register editor
	strcpy(gMissionSetEditor.editor_name, "MissionSet Editor");
	gMissionSetEditor.type = EM_TYPE_MULTIDOC;
	gMissionSetEditor.hide_world = 1;
	gMissionSetEditor.disable_single_doc_menus = 1;
	gMissionSetEditor.disable_auto_checkout = 1;
	gMissionSetEditor.default_type = "MissionSet";
	strcpy(gMissionSetEditor.default_workspace, "Game Design Editors");

	gMissionSetEditor.init_func = missionSetEditorEMInit;
	gMissionSetEditor.enter_editor_func = missionSetEditorEMEnter;
	gMissionSetEditor.exit_func = missionSetEditorEMExit;
	gMissionSetEditor.lost_focus_func = missionSetEditorEMLostFocus;
	gMissionSetEditor.new_func = missionSetEditorEMNewDoc;
	gMissionSetEditor.load_func = missionSetEditorEMLoadDoc;
	gMissionSetEditor.save_func = missionSetEditorEMSaveDoc;
	gMissionSetEditor.sub_save_func = missionSetEditorEMSaveSubDoc;
	gMissionSetEditor.close_func = missionSetEditorEMCloseDoc;
	gMissionSetEditor.sub_close_func = missionSetEditorEMCloseSubDoc;
	gMissionSetEditor.sub_reload_func = missionSetEditorEMReloadSubDoc;

	gMissionSetEditor.keybinds_name = "MultiEditor";

	// register picker
	gMissionSetPicker.allow_outsource = 1;
	strcpy(gMissionSetPicker.picker_name, "MissionSet Library");
	strcpy(gMissionSetPicker.default_type, gMissionSetEditor.default_type);
	emPickerManage(&gMissionSetPicker);
	eaPush(&gMissionSetEditor.pickers, &gMissionSetPicker);

	emRegisterEditor(&gMissionSetEditor);
	emRegisterFileType(gMissionSetEditor.default_type, "MissionSet", gMissionSetEditor.editor_name);
#endif

	return 1;
}

