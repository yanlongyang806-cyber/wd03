//
// MicroTransEditorEM.c
//

#ifndef NO_EDITORS

#include "error.h"
#include "file.h"
#include "MultiEditTable.h"
#include "MultiEditWindow.h"
#include "MicroTransactionEditor.h"

#include "MicroTransactions.h"

// This magic line is required if you run in Full Debug to avoid assert
AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

EMEditor gMicroTransEditor;

EMPicker gMicroTransPicker;

static MultiEditEMDoc *gMicroTransEditorDoc = NULL;


//---------------------------------------------------------------------------------------------------
// Editor
//---------------------------------------------------------------------------------------------------


static EMEditorDoc *MicroTransEditorEMNewDoc(const char *pcType, void *data)
{
	if (!gConf.bServerSaving) {
		Alertf("This editor only operates when server saving is enabled");
		return NULL;
	}
	MicroTransEditor_createMT(NULL);

	return (EMEditorDoc*)gMicroTransEditorDoc;
}


static EMEditorDoc *MicroTransEditorEMLoadDoc(const char *pcName, const char *pcType)
{
	if (!gConf.bServerSaving) {
		Alertf("This editor only operates when server saving is enabled");
		return NULL;
	}
	MEWindowOpenObject(gMicroTransEditorDoc->pWindow, (char*)pcName);

	return (EMEditorDoc*)gMicroTransEditorDoc;
}


static EMTaskStatus MicroTransEditorEMSaveDoc(EMEditorDoc *pDoc)
{
	return MEWindowSaveAll(gMicroTransEditorDoc->pWindow);
}


static EMTaskStatus MicroTransEditorEMSaveSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	return MEWindowSaveObject(gMicroTransEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void MicroTransEditorEMCloseDoc(EMEditorDoc *pDoc)
{
	MEWindowExit(gMicroTransEditorDoc->pWindow);
}


static void MicroTransEditorEMCloseSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	MEWindowCloseObject(gMicroTransEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void MicroTransEditorEMReloadSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	MEWindowRevertObject(gMicroTransEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void MicroTransEditorEMEnter(EMEditor *pEditor)
{
}

static void MicroTransEditorEMExit(EMEditor *pEditor)
{

}

static void MicroTransEditorEMLostFocus(EMEditorDoc *pDoc)
{
	MEWindowLostFocus(gMicroTransEditorDoc->pWindow);
}

static void MicroTransEditorEMInit(EMEditor *pEditor)
{
	// Create the one global document
	if (!gMicroTransEditorDoc) {
		// Create the global document
		gMicroTransEditorDoc = calloc(1,sizeof(MultiEditEMDoc));
		gMicroTransEditorDoc->emDoc.editor = &gMicroTransEditor;
		gMicroTransEditorDoc->emDoc.saved = true;
		gMicroTransEditorDoc->pWindow = MicroTransEditor_init(gMicroTransEditorDoc);
		sprintf(gMicroTransEditorDoc->emDoc.doc_name, "MicroTransaction Editor");
		sprintf(gMicroTransEditorDoc->emDoc.doc_display_name, "MicroTransaction Editor");
		gMicroTransEditorDoc->emDoc.primary_ui_window = MEWindowGetUIWindow(gMicroTransEditorDoc->pWindow);
	}
}

#endif

AUTO_RUN_LATE;
int MicroTransEditorEMRegister(void)
{
#ifndef NO_EDITORS
	if (!areEditorsAllowed())
		return 0;

	// register editor
	strcpy(gMicroTransEditor.editor_name, "MicroTransaction Editor");
	gMicroTransEditor.type = EM_TYPE_MULTIDOC;
	gMicroTransEditor.hide_world = 1;
	gMicroTransEditor.disable_single_doc_menus = 1;
	gMicroTransEditor.disable_auto_checkout = 1;
	gMicroTransEditor.default_type = "MicroTransactionDef";
	gMicroTransEditor.allow_multiple_docs = 1;
	strcpy(gMicroTransEditor.default_workspace, "Game Design Editors");

	gMicroTransEditor.init_func = MicroTransEditorEMInit;
	gMicroTransEditor.enter_editor_func = MicroTransEditorEMEnter;
	gMicroTransEditor.exit_func = MicroTransEditorEMExit;
	gMicroTransEditor.lost_focus_func = MicroTransEditorEMLostFocus;
	gMicroTransEditor.new_func = MicroTransEditorEMNewDoc;
	gMicroTransEditor.load_func = MicroTransEditorEMLoadDoc;
	gMicroTransEditor.save_func = MicroTransEditorEMSaveDoc;
	gMicroTransEditor.sub_save_func = MicroTransEditorEMSaveSubDoc;
	gMicroTransEditor.close_func = MicroTransEditorEMCloseDoc;
	gMicroTransEditor.sub_close_func = MicroTransEditorEMCloseSubDoc;
	gMicroTransEditor.sub_reload_func = MicroTransEditorEMReloadSubDoc;

	gMicroTransEditor.keybinds_name = "MultiEditor";

	// register picker
	gMicroTransPicker.allow_outsource = 1;
	strcpy(gMicroTransPicker.picker_name, "MicroTransaction Library");
	strcpy(gMicroTransPicker.default_type, gMicroTransEditor.default_type);
	emPickerManage(&gMicroTransPicker);
	eaPush(&gMicroTransEditor.pickers, &gMicroTransPicker);

	emRegisterEditor(&gMicroTransEditor);
	emRegisterFileType(gMicroTransEditor.default_type, "MicroTransaction", gMicroTransEditor.editor_name);
#endif

	return 1;
}

