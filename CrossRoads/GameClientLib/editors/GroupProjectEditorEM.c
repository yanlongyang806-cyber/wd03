//
// GroupProjectEditorEM.c
//

#ifndef NO_EDITORS

#include "error.h"
#include "file.h"
#include "MultiEditTable.h"
#include "MultiEditWindow.h"
#include "GroupProjectEditor.h"


// This magic line is required if you run in Full Debug to avoid assert
AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

EMEditor gGroupProjectEditor;

EMPicker gGroupProjectPicker;

static MultiEditEMDoc *gGroupProjectEditorDoc = NULL;


//---------------------------------------------------------------------------------------------------
// Editor
//---------------------------------------------------------------------------------------------------


static EMEditorDoc *groupProjectEditorEMNewDoc(const char *pcType, void *data)
{
	if (!gConf.bServerSaving) {
		Alertf("This editor only operates when server saving is enabled");
		return NULL;
	}
	groupProjectEditor_createGroupProject(NULL);

	return (EMEditorDoc*)gGroupProjectEditorDoc;
}


static EMEditorDoc *groupProjectEditorEMLoadDoc(const char *pcName, const char *pcType)
{
	if (!gConf.bServerSaving) {
		Alertf("This editor only operates when server saving is enabled");
		return NULL;
	}
	MEWindowOpenObject(gGroupProjectEditorDoc->pWindow, (char*)pcName);

	return (EMEditorDoc*)gGroupProjectEditorDoc;
}


static EMTaskStatus groupProjectEditorEMSaveDoc(EMEditorDoc *pDoc)
{
	return MEWindowSaveAll(gGroupProjectEditorDoc->pWindow);
}


static EMTaskStatus groupProjectEditorEMSaveSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	return MEWindowSaveObject(gGroupProjectEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void groupProjectEditorEMCloseDoc(EMEditorDoc *pDoc)
{
	MEWindowExit(gGroupProjectEditorDoc->pWindow);
}


static void groupProjectEditorEMCloseSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	MEWindowCloseObject(gGroupProjectEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void groupProjectEditorEMReloadSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	MEWindowRevertObject(gGroupProjectEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void groupProjectEditorEMEnter(EMEditor *pEditor)
{

}

static void groupProjectEditorEMExit(EMEditor *pEditor)
{

}

static void groupProjectEditorEMLostFocus(EMEditorDoc *pDoc)
{
	MEWindowLostFocus(gGroupProjectEditorDoc->pWindow);
}

static void groupProjectEditorEMInit(EMEditor *pEditor)
{
	// Create the one global document
	if (!gGroupProjectEditorDoc) {
		// Create the global document
		gGroupProjectEditorDoc = calloc(1,sizeof(MultiEditEMDoc));
		gGroupProjectEditorDoc->emDoc.editor = &gGroupProjectEditor;
		gGroupProjectEditorDoc->emDoc.saved = true;
		gGroupProjectEditorDoc->pWindow = groupProjectEditor_init(gGroupProjectEditorDoc);
		sprintf(gGroupProjectEditorDoc->emDoc.doc_name, "Group Project Editor");
		sprintf(gGroupProjectEditorDoc->emDoc.doc_display_name, "Group Project Editor");
		gGroupProjectEditorDoc->emDoc.primary_ui_window = MEWindowGetUIWindow(gGroupProjectEditorDoc->pWindow);
	}
}

#endif

AUTO_RUN_LATE;
int groupProjectEditorEMRegister(void)
{
#ifndef NO_EDITORS
	if (!areEditorsAllowed())
		return 0;

	// register editor
	strcpy(gGroupProjectEditor.editor_name, "Group Project Editor");
	gGroupProjectEditor.type = EM_TYPE_MULTIDOC;
	gGroupProjectEditor.hide_world = 1;
	gGroupProjectEditor.disable_single_doc_menus = 1;
	gGroupProjectEditor.disable_auto_checkout = 1;
	gGroupProjectEditor.default_type = "GroupProjectDef";
	strcpy(gGroupProjectEditor.default_workspace, "Game Design Editors");

	gGroupProjectEditor.init_func = groupProjectEditorEMInit;
	gGroupProjectEditor.enter_editor_func = groupProjectEditorEMEnter;
	gGroupProjectEditor.exit_func = groupProjectEditorEMExit;
	gGroupProjectEditor.lost_focus_func = groupProjectEditorEMLostFocus;
	gGroupProjectEditor.new_func = groupProjectEditorEMNewDoc;
	gGroupProjectEditor.load_func = groupProjectEditorEMLoadDoc;
	gGroupProjectEditor.save_func = groupProjectEditorEMSaveDoc;
	gGroupProjectEditor.sub_save_func = groupProjectEditorEMSaveSubDoc;
	gGroupProjectEditor.close_func = groupProjectEditorEMCloseDoc;
	gGroupProjectEditor.sub_close_func = groupProjectEditorEMCloseSubDoc;
	gGroupProjectEditor.sub_reload_func = groupProjectEditorEMReloadSubDoc;

	gGroupProjectEditor.keybinds_name = "MultiEditor";

	// register picker
	gGroupProjectPicker.allow_outsource = 1;
	strcpy(gGroupProjectPicker.picker_name, "GroupProject Library");
	strcpy(gGroupProjectPicker.default_type, gGroupProjectEditor.default_type);
	emPickerManage(&gGroupProjectPicker);
	eaPush(&gGroupProjectEditor.pickers, &gGroupProjectPicker);

	emRegisterEditor(&gGroupProjectEditor);
	emRegisterFileType(gGroupProjectEditor.default_type, "GroupProject", gGroupProjectEditor.editor_name);
#endif

	return 1;
}

