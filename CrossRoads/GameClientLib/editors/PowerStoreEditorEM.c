//
// PowerStoreEditorEM.c
//

#ifndef NO_EDITORS

#include "error.h"
#include "file.h"
#include "MultiEditTable.h"
#include "MultiEditWindow.h"
#include "PowerStoreEditor.h"


// This magic line is required if you run in Full Debug to avoid assert
AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

EMEditor gPowerStoreEditor;

EMPicker gPowerStorePicker;

static MultiEditEMDoc *gPowerStoreEditorDoc = NULL;


//---------------------------------------------------------------------------------------------------
// Editor
//---------------------------------------------------------------------------------------------------


static EMEditorDoc *powerStoreEditorEMNewDoc(const char *pcType, void *data)
{
	if (!gConf.bServerSaving) {
		Alertf("This editor only operates when server saving is enabled");
		return NULL;
	}
	powerStoreEditor_createPowerStore(NULL);

	return (EMEditorDoc*)gPowerStoreEditorDoc;
}


static EMEditorDoc *powerStoreEditorEMLoadDoc(const char *pcName, const char *pcType)
{
	if (!gConf.bServerSaving) {
		Alertf("This editor only operates when server saving is enabled");
		return NULL;
	}
	MEWindowOpenObject(gPowerStoreEditorDoc->pWindow, (char*)pcName);

	return (EMEditorDoc*)gPowerStoreEditorDoc;
}


static EMTaskStatus powerStoreEditorEMSaveDoc(EMEditorDoc *pDoc)
{
	return MEWindowSaveAll(gPowerStoreEditorDoc->pWindow);
}


static EMTaskStatus powerStoreEditorEMSaveSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	return MEWindowSaveObject(gPowerStoreEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void powerStoreEditorEMCloseDoc(EMEditorDoc *pDoc)
{
	MEWindowExit(gPowerStoreEditorDoc->pWindow);
}


static void powerStoreEditorEMCloseSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	MEWindowCloseObject(gPowerStoreEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void powerStoreEditorEMReloadSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	MEWindowRevertObject(gPowerStoreEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void powerStoreEditorEMEnter(EMEditor *pEditor)
{

}

static void powerStoreEditorEMExit(EMEditor *pEditor)
{

}

static void powerStoreEditorEMLostFocus(EMEditorDoc *pDoc)
{
	MEWindowLostFocus(gPowerStoreEditorDoc->pWindow);
}

static void powerStoreEditorEMInit(EMEditor *pEditor)
{
	// Create the one global document
	if (!gPowerStoreEditorDoc) {
		// Create the global document
		gPowerStoreEditorDoc = calloc(1,sizeof(MultiEditEMDoc));
		gPowerStoreEditorDoc->emDoc.editor = &gPowerStoreEditor;
		gPowerStoreEditorDoc->emDoc.saved = true;
		gPowerStoreEditorDoc->pWindow = powerStoreEditor_init(gPowerStoreEditorDoc);
		sprintf(gPowerStoreEditorDoc->emDoc.doc_name, "Power Store Editor");
		sprintf(gPowerStoreEditorDoc->emDoc.doc_display_name, "Power Store Editor");
		gPowerStoreEditorDoc->emDoc.primary_ui_window = MEWindowGetUIWindow(gPowerStoreEditorDoc->pWindow);
	}
}

#endif

AUTO_RUN_LATE;
int powerStoreEditorEMRegister(void)
{
#ifndef NO_EDITORS
	if (!areEditorsAllowed())
		return 0;

	// register editor
	strcpy(gPowerStoreEditor.editor_name, "Power Store Editor");
	gPowerStoreEditor.type = EM_TYPE_MULTIDOC;
	gPowerStoreEditor.hide_world = 1;
	gPowerStoreEditor.disable_single_doc_menus = 1;
	gPowerStoreEditor.disable_auto_checkout = 1;
	gPowerStoreEditor.default_type = "PowerStore";
	strcpy(gPowerStoreEditor.default_workspace, "Game Design Editors");

	gPowerStoreEditor.init_func = powerStoreEditorEMInit;
	gPowerStoreEditor.enter_editor_func = powerStoreEditorEMEnter;
	gPowerStoreEditor.exit_func = powerStoreEditorEMExit;
	gPowerStoreEditor.lost_focus_func = powerStoreEditorEMLostFocus;
	gPowerStoreEditor.new_func = powerStoreEditorEMNewDoc;
	gPowerStoreEditor.load_func = powerStoreEditorEMLoadDoc;
	gPowerStoreEditor.save_func = powerStoreEditorEMSaveDoc;
	gPowerStoreEditor.sub_save_func = powerStoreEditorEMSaveSubDoc;
	gPowerStoreEditor.close_func = powerStoreEditorEMCloseDoc;
	gPowerStoreEditor.sub_close_func = powerStoreEditorEMCloseSubDoc;
	gPowerStoreEditor.sub_reload_func = powerStoreEditorEMReloadSubDoc;

	gPowerStoreEditor.keybinds_name = "MultiEditor";

	// register picker
	gPowerStorePicker.allow_outsource = 1;
	strcpy(gPowerStorePicker.picker_name, "Power Store Library");
	strcpy(gPowerStorePicker.default_type, gPowerStoreEditor.default_type);
	emPickerManage(&gPowerStorePicker);
	eaPush(&gPowerStoreEditor.pickers, &gPowerStorePicker);

	emRegisterEditor(&gPowerStoreEditor);
	emRegisterFileType(gPowerStoreEditor.default_type, "Power Store", gPowerStoreEditor.editor_name);
#endif

	return 1;
}

