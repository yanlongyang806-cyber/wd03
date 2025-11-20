//
// StoreEditorEM.c
//

#ifndef NO_EDITORS

#include "error.h"
#include "file.h"
#include "MultiEditTable.h"
#include "MultiEditWindow.h"
#include "StoreEditor.h"


// This magic line is required if you run in Full Debug to avoid assert
AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

EMEditor gStoreEditor;

EMPicker gStorePicker;

static MultiEditEMDoc *gStoreEditorDoc = NULL;


//---------------------------------------------------------------------------------------------------
// Editor
//---------------------------------------------------------------------------------------------------


static EMEditorDoc *storeEditorEMNewDoc(const char *pcType, void *data)
{
	if (!gConf.bServerSaving) {
		Alertf("This editor only operates when server saving is enabled");
		return NULL;
	}
	storeEditor_createStore(NULL);

	return (EMEditorDoc*)gStoreEditorDoc;
}


static EMEditorDoc *storeEditorEMLoadDoc(const char *pcName, const char *pcType)
{
	if (!gConf.bServerSaving) {
		Alertf("This editor only operates when server saving is enabled");
		return NULL;
	}
	MEWindowOpenObject(gStoreEditorDoc->pWindow, (char*)pcName);

	return (EMEditorDoc*)gStoreEditorDoc;
}


static EMTaskStatus storeEditorEMSaveDoc(EMEditorDoc *pDoc)
{
	return MEWindowSaveAll(gStoreEditorDoc->pWindow);
}


static EMTaskStatus storeEditorEMSaveSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	return MEWindowSaveObject(gStoreEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void storeEditorEMCloseDoc(EMEditorDoc *pDoc)
{
	MEWindowExit(gStoreEditorDoc->pWindow);
}


static void storeEditorEMCloseSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	MEWindowCloseObject(gStoreEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void storeEditorEMReloadSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	MEWindowRevertObject(gStoreEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void storeEditorEMEnter(EMEditor *pEditor)
{

}

static void storeEditorEMExit(EMEditor *pEditor)
{

}

static void storeEditorEMLostFocus(EMEditorDoc *pDoc)
{
	MEWindowLostFocus(gStoreEditorDoc->pWindow);
}

static void storeEditorEMInit(EMEditor *pEditor)
{
	// Create the one global document
	if (!gStoreEditorDoc) {
		// Create the global document
		gStoreEditorDoc = calloc(1,sizeof(MultiEditEMDoc));
		gStoreEditorDoc->emDoc.editor = &gStoreEditor;
		gStoreEditorDoc->emDoc.saved = true;
		gStoreEditorDoc->pWindow = storeEditor_init(gStoreEditorDoc);
		sprintf(gStoreEditorDoc->emDoc.doc_name, "Store Editor");
		sprintf(gStoreEditorDoc->emDoc.doc_display_name, "Store Editor");
		gStoreEditorDoc->emDoc.primary_ui_window = MEWindowGetUIWindow(gStoreEditorDoc->pWindow);
	}
}

#endif

AUTO_RUN_LATE;
int storeEditorEMRegister(void)
{
#ifndef NO_EDITORS
	if (!areEditorsAllowed())
		return 0;

	// register editor
	strcpy(gStoreEditor.editor_name, "Store Editor");
	gStoreEditor.type = EM_TYPE_MULTIDOC;
	gStoreEditor.hide_world = 1;
	gStoreEditor.disable_single_doc_menus = 1;
	gStoreEditor.disable_auto_checkout = 1;
	gStoreEditor.default_type = "Store";
	strcpy(gStoreEditor.default_workspace, "Game Design Editors");

	gStoreEditor.init_func = storeEditorEMInit;
	gStoreEditor.enter_editor_func = storeEditorEMEnter;
	gStoreEditor.exit_func = storeEditorEMExit;
	gStoreEditor.lost_focus_func = storeEditorEMLostFocus;
	gStoreEditor.new_func = storeEditorEMNewDoc;
	gStoreEditor.load_func = storeEditorEMLoadDoc;
	gStoreEditor.save_func = storeEditorEMSaveDoc;
	gStoreEditor.sub_save_func = storeEditorEMSaveSubDoc;
	gStoreEditor.close_func = storeEditorEMCloseDoc;
	gStoreEditor.sub_close_func = storeEditorEMCloseSubDoc;
	gStoreEditor.sub_reload_func = storeEditorEMReloadSubDoc;

	gStoreEditor.keybinds_name = "MultiEditor";

	// register picker
	gStorePicker.allow_outsource = 1;
	strcpy(gStorePicker.picker_name, "Store Library");
	strcpy(gStorePicker.default_type, gStoreEditor.default_type);
	emPickerManage(&gStorePicker);
	eaPush(&gStoreEditor.pickers, &gStorePicker);

	emRegisterEditor(&gStoreEditor);
	emRegisterFileType(gStoreEditor.default_type, "Store", gStoreEditor.editor_name);
#endif

	return 1;
}

