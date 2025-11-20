//
// GroupProjectNumericEditorEM.c
//

#ifndef NO_EDITORS

#include "error.h"
#include "file.h"
#include "MultiEditTable.h"
#include "MultiEditWindow.h"
#include "GroupProjectNumericEditor.h"


// This magic line is required if you run in Full Debug to avoid assert
AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

EMEditor gGroupProjectNumericEditor;

EMPicker gGroupProjectNumericPicker;

static MultiEditEMDoc *gGroupProjectNumericEditorDoc = NULL;


//---------------------------------------------------------------------------------------------------
// Editor
//---------------------------------------------------------------------------------------------------


static EMEditorDoc *groupProjectNumericEditorEMNewDoc(const char *pcType, void *data)
{
	if (!gConf.bServerSaving) {
		Alertf("This editor only operates when server saving is enabled");
		return NULL;
	}
	groupProjectNumericEditor_createGroupProjectNumeric(NULL);

	return (EMEditorDoc*)gGroupProjectNumericEditorDoc;
}


static EMEditorDoc *groupProjectNumericEditorEMLoadDoc(const char *pcName, const char *pcType)
{
	if (!gConf.bServerSaving) {
		Alertf("This editor only operates when server saving is enabled");
		return NULL;
	}
	MEWindowOpenObject(gGroupProjectNumericEditorDoc->pWindow, (char*)pcName);

	return (EMEditorDoc*)gGroupProjectNumericEditorDoc;
}


static EMTaskStatus groupProjectNumericEditorEMSaveDoc(EMEditorDoc *pDoc)
{
	return MEWindowSaveAll(gGroupProjectNumericEditorDoc->pWindow);
}


static EMTaskStatus groupProjectNumericEditorEMSaveSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	return MEWindowSaveObject(gGroupProjectNumericEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void groupProjectNumericEditorEMCloseDoc(EMEditorDoc *pDoc)
{
	MEWindowExit(gGroupProjectNumericEditorDoc->pWindow);
}


static void groupProjectNumericEditorEMCloseSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	MEWindowCloseObject(gGroupProjectNumericEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void groupProjectNumericEditorEMReloadSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	MEWindowRevertObject(gGroupProjectNumericEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void groupProjectNumericEditorEMEnter(EMEditor *pEditor)
{

}

static void groupProjectNumericEditorEMExit(EMEditor *pEditor)
{

}

static void groupProjectNumericEditorEMLostFocus(EMEditorDoc *pDoc)
{
	MEWindowLostFocus(gGroupProjectNumericEditorDoc->pWindow);
}

static void groupProjectNumericEditorEMInit(EMEditor *pEditor)
{
	// Create the one global document
	if (!gGroupProjectNumericEditorDoc) {
		// Create the global document
		gGroupProjectNumericEditorDoc = calloc(1,sizeof(MultiEditEMDoc));
		gGroupProjectNumericEditorDoc->emDoc.editor = &gGroupProjectNumericEditor;
		gGroupProjectNumericEditorDoc->emDoc.saved = true;
		gGroupProjectNumericEditorDoc->pWindow = groupProjectNumericEditor_init(gGroupProjectNumericEditorDoc);
		sprintf(gGroupProjectNumericEditorDoc->emDoc.doc_name, "Group Project Numeric Editor");
		sprintf(gGroupProjectNumericEditorDoc->emDoc.doc_display_name, "Group Project Numeric Editor");
		gGroupProjectNumericEditorDoc->emDoc.primary_ui_window = MEWindowGetUIWindow(gGroupProjectNumericEditorDoc->pWindow);
	}
}

#endif

AUTO_RUN_LATE;
int groupProjectNumericEditorEMRegister(void)
{
#ifndef NO_EDITORS
	if (!areEditorsAllowed())
		return 0;

	// register editor
	strcpy(gGroupProjectNumericEditor.editor_name, "Group Project Numeric Editor");
	gGroupProjectNumericEditor.type = EM_TYPE_MULTIDOC;
	gGroupProjectNumericEditor.hide_world = 1;
	gGroupProjectNumericEditor.disable_single_doc_menus = 1;
	gGroupProjectNumericEditor.disable_auto_checkout = 1;
	gGroupProjectNumericEditor.default_type = "GroupProjectNumericDef";
	strcpy(gGroupProjectNumericEditor.default_workspace, "Game Design Editors");

	gGroupProjectNumericEditor.init_func = groupProjectNumericEditorEMInit;
	gGroupProjectNumericEditor.enter_editor_func = groupProjectNumericEditorEMEnter;
	gGroupProjectNumericEditor.exit_func = groupProjectNumericEditorEMExit;
	gGroupProjectNumericEditor.lost_focus_func = groupProjectNumericEditorEMLostFocus;
	gGroupProjectNumericEditor.new_func = groupProjectNumericEditorEMNewDoc;
	gGroupProjectNumericEditor.load_func = groupProjectNumericEditorEMLoadDoc;
	gGroupProjectNumericEditor.save_func = groupProjectNumericEditorEMSaveDoc;
	gGroupProjectNumericEditor.sub_save_func = groupProjectNumericEditorEMSaveSubDoc;
	gGroupProjectNumericEditor.close_func = groupProjectNumericEditorEMCloseDoc;
	gGroupProjectNumericEditor.sub_close_func = groupProjectNumericEditorEMCloseSubDoc;
	gGroupProjectNumericEditor.sub_reload_func = groupProjectNumericEditorEMReloadSubDoc;

	gGroupProjectNumericEditor.keybinds_name = "MultiEditor";

	// register picker
	gGroupProjectNumericPicker.allow_outsource = 1;
	strcpy(gGroupProjectNumericPicker.picker_name, "GroupProjectNumeric Library");
	strcpy(gGroupProjectNumericPicker.default_type, gGroupProjectNumericEditor.default_type);
	emPickerManage(&gGroupProjectNumericPicker);
	eaPush(&gGroupProjectNumericEditor.pickers, &gGroupProjectNumericPicker);

	emRegisterEditor(&gGroupProjectNumericEditor);
	emRegisterFileType(gGroupProjectNumericEditor.default_type, "GroupProjectNumeric", gGroupProjectNumericEditor.editor_name);
#endif

	return 1;
}

