//
// GroupProjectUnlockEditorEM.c
//

#ifndef NO_EDITORS

#include "error.h"
#include "file.h"
#include "MultiEditTable.h"
#include "MultiEditWindow.h"
#include "GroupProjectUnlockEditor.h"


// This magic line is required if you run in Full Debug to avoid assert
AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

EMEditor gGroupProjectUnlockEditor;
EMPicker gGroupProjectUnlockPicker;

static MultiEditEMDoc *gGroupProjectUnlockEditorDoc = NULL;


//---------------------------------------------------------------------------------------------------
// Editor
//---------------------------------------------------------------------------------------------------


static EMEditorDoc *groupProjectUnlockEditorEMNewDoc(const char *pcType, void *data)
{
	if (!gConf.bServerSaving) {
		Alertf("This editor only operates when server saving is enabled");
		return NULL;
	}
	groupProjectUnlockEditor_createGroupProjectUnlock(NULL);

	return (EMEditorDoc*)gGroupProjectUnlockEditorDoc;
}


static EMEditorDoc *groupProjectUnlockEditorEMLoadDoc(const char *pcName, const char *pcType)
{
	if (!gConf.bServerSaving) {
		Alertf("This editor only operates when server saving is enabled");
		return NULL;
	}
	MEWindowOpenObject(gGroupProjectUnlockEditorDoc->pWindow, (char*)pcName);

	return (EMEditorDoc*)gGroupProjectUnlockEditorDoc;
}


static EMTaskStatus groupProjectUnlockEditorEMSaveDoc(EMEditorDoc *pDoc)
{
	return MEWindowSaveAll(gGroupProjectUnlockEditorDoc->pWindow);
}


static EMTaskStatus groupProjectUnlockEditorEMSaveSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	return MEWindowSaveObject(gGroupProjectUnlockEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void groupProjectUnlockEditorEMCloseDoc(EMEditorDoc *pDoc)
{
	MEWindowExit(gGroupProjectUnlockEditorDoc->pWindow);
}


static void groupProjectUnlockEditorEMCloseSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	MEWindowCloseObject(gGroupProjectUnlockEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void groupProjectUnlockEditorEMReloadSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	MEWindowRevertObject(gGroupProjectUnlockEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void groupProjectUnlockEditorEMEnter(EMEditor *pEditor)
{

}

static void groupProjectUnlockEditorEMExit(EMEditor *pEditor)
{

}

static void groupProjectUnlockEditorEMLostFocus(EMEditorDoc *pDoc)
{
	MEWindowLostFocus(gGroupProjectUnlockEditorDoc->pWindow);
}

static void groupProjectUnlockEditorEMInit(EMEditor *pEditor)
{
	// Create the one global document
	if (!gGroupProjectUnlockEditorDoc) {
		// Create the global document
		gGroupProjectUnlockEditorDoc = calloc(1,sizeof(MultiEditEMDoc));
		gGroupProjectUnlockEditorDoc->emDoc.editor = &gGroupProjectUnlockEditor;
		gGroupProjectUnlockEditorDoc->emDoc.saved = true;
		gGroupProjectUnlockEditorDoc->pWindow = groupProjectUnlockEditor_init(gGroupProjectUnlockEditorDoc);
		sprintf(gGroupProjectUnlockEditorDoc->emDoc.doc_name, "Group Project Unlock Editor");
		sprintf(gGroupProjectUnlockEditorDoc->emDoc.doc_display_name, "Group Project Unlock Editor");
		gGroupProjectUnlockEditorDoc->emDoc.primary_ui_window = MEWindowGetUIWindow(gGroupProjectUnlockEditorDoc->pWindow);
	}
}

#endif

AUTO_RUN_LATE;
int groupProjectUnlockEditorEMRegister(void)
{
#ifndef NO_EDITORS
	if (!areEditorsAllowed())
		return 0;

	// register editor
	strcpy(gGroupProjectUnlockEditor.editor_name, "Group Project Unlock Editor");
	gGroupProjectUnlockEditor.type = EM_TYPE_MULTIDOC;
	gGroupProjectUnlockEditor.hide_world = 1;
	gGroupProjectUnlockEditor.disable_single_doc_menus = 1;
	gGroupProjectUnlockEditor.disable_auto_checkout = 1;
	gGroupProjectUnlockEditor.default_type = "GroupProjectUnlockDef";
	strcpy(gGroupProjectUnlockEditor.default_workspace, "Game Design Editors");

	gGroupProjectUnlockEditor.init_func = groupProjectUnlockEditorEMInit;
	gGroupProjectUnlockEditor.enter_editor_func = groupProjectUnlockEditorEMEnter;
	gGroupProjectUnlockEditor.exit_func = groupProjectUnlockEditorEMExit;
	gGroupProjectUnlockEditor.lost_focus_func = groupProjectUnlockEditorEMLostFocus;
	gGroupProjectUnlockEditor.new_func = groupProjectUnlockEditorEMNewDoc;
	gGroupProjectUnlockEditor.load_func = groupProjectUnlockEditorEMLoadDoc;
	gGroupProjectUnlockEditor.save_func = groupProjectUnlockEditorEMSaveDoc;
	gGroupProjectUnlockEditor.sub_save_func = groupProjectUnlockEditorEMSaveSubDoc;
	gGroupProjectUnlockEditor.close_func = groupProjectUnlockEditorEMCloseDoc;
	gGroupProjectUnlockEditor.sub_close_func = groupProjectUnlockEditorEMCloseSubDoc;
	gGroupProjectUnlockEditor.sub_reload_func = groupProjectUnlockEditorEMReloadSubDoc;

	gGroupProjectUnlockEditor.keybinds_name = "MultiEditor";

	// register picker
	gGroupProjectUnlockPicker.allow_outsource = 1;
	strcpy(gGroupProjectUnlockPicker.picker_name, "GroupProjectUnlock Library");
	strcpy(gGroupProjectUnlockPicker.default_type, gGroupProjectUnlockEditor.default_type);
	emPickerManage(&gGroupProjectUnlockPicker);
	eaPush(&gGroupProjectUnlockEditor.pickers, &gGroupProjectUnlockPicker);

	emRegisterEditor(&gGroupProjectUnlockEditor);
	emRegisterFileType(gGroupProjectUnlockEditor.default_type, "GroupProjectUnlock", gGroupProjectUnlockEditor.editor_name);
#endif

	return 1;
}

