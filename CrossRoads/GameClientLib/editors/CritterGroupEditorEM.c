//
// crittergroupEditorEM.c
//

#ifndef NO_EDITORS

#include "CritterGroupEditor.h"
#include "entCritter.h"
#include "error.h"
#include "file.h"
#include "MultiEditTable.h"
#include "MultiEditWindow.h"

// Magic code required by budgets
AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

//-----------------------------------------------------------------------------------
// Global Data
//-----------------------------------------------------------------------------------


EMEditor gcrittergroupEditor;

EMPicker gcrittergroupPicker;

static MultiEditEMDoc *gcrittergroupEditorDoc = NULL;


//---------------------------------------------------------------------------------------------------
// Editor
//---------------------------------------------------------------------------------------------------


static EMEditorDoc *crittergroupEditorEMNewDoc(const char *pcType, void *data)
{
	if (!gConf.bServerSaving) {
		Alertf("This editor only operates when server saving is enabled");
		return NULL;
	}
	crittergroupEditor_createGroup(NULL);

	return (EMEditorDoc*)gcrittergroupEditorDoc;
}


static EMEditorDoc *crittergroupEditorEMLoadDoc(const char *pcName, const char *pcType)
{
	if (!gConf.bServerSaving) {
		Alertf("This editor only operates when server saving is enabled");
		return NULL;
	}
	MEWindowOpenObject(gcrittergroupEditorDoc->pWindow, (char*)pcName);

	return (EMEditorDoc*)gcrittergroupEditorDoc;
}


static EMTaskStatus crittergroupEditorEMSaveDoc(EMEditorDoc *pDoc)
{
	return MEWindowSaveAll(gcrittergroupEditorDoc->pWindow);
}


static EMTaskStatus crittergroupEditorEMSaveSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	return MEWindowSaveObject(gcrittergroupEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void crittergroupEditorEMCloseDoc(EMEditorDoc *pDoc)
{
	MEWindowExit(gcrittergroupEditorDoc->pWindow);
}


static void crittergroupEditorEMCloseSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	MEWindowCloseObject(gcrittergroupEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void crittergroupEditorEMReloadSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	MEWindowRevertObject(gcrittergroupEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void crittergroupEditorEMEnter(EMEditor *pEditor)
{

}

static void crittergroupEditorEMExit(EMEditor *pEditor)
{

}

static void crittergroupEditorEMLostFocus(EMEditorDoc *pDoc)
{
	MEWindowLostFocus(gcrittergroupEditorDoc->pWindow);
}

static void crittergroupEditorEMInit(EMEditor *pEditor)
{
	// Create the one global document
	if (!gcrittergroupEditorDoc) {
		// Create the global document
		gcrittergroupEditorDoc = calloc(1,sizeof(MultiEditEMDoc));
		gcrittergroupEditorDoc->emDoc.editor = &gcrittergroupEditor;
		gcrittergroupEditorDoc->emDoc.saved = true;
		gcrittergroupEditorDoc->pWindow = crittergroupEditor_init(gcrittergroupEditorDoc);
		sprintf(gcrittergroupEditorDoc->emDoc.doc_name, "CritterGroup Editor");
		sprintf(gcrittergroupEditorDoc->emDoc.doc_display_name, "CritterGroup Editor");
		gcrittergroupEditorDoc->emDoc.primary_ui_window = MEWindowGetUIWindow(gcrittergroupEditorDoc->pWindow);
	}
}

#endif

AUTO_RUN_LATE;
int crittergroupEditorEMRegister(void)
{
#ifndef NO_EDITORS
	if (!areEditorsAllowed())
		return 0;

	// Register the editor
	strcpy(gcrittergroupEditor.editor_name, "CritterGroup Editor");
	gcrittergroupEditor.type = EM_TYPE_MULTIDOC;
	gcrittergroupEditor.hide_world = 1;
	gcrittergroupEditor.disable_single_doc_menus = 1;
	gcrittergroupEditor.disable_auto_checkout = 1;
	gcrittergroupEditor.default_type = "CritterGroup";
	strcpy(gcrittergroupEditor.default_workspace, "Game Design Editors");

	gcrittergroupEditor.init_func = crittergroupEditorEMInit;
	gcrittergroupEditor.enter_editor_func = crittergroupEditorEMEnter;
	gcrittergroupEditor.exit_func = crittergroupEditorEMExit;
	gcrittergroupEditor.lost_focus_func = crittergroupEditorEMLostFocus;
	gcrittergroupEditor.new_func = crittergroupEditorEMNewDoc;
	gcrittergroupEditor.load_func = crittergroupEditorEMLoadDoc;
	gcrittergroupEditor.save_func = crittergroupEditorEMSaveDoc;
	gcrittergroupEditor.sub_save_func = crittergroupEditorEMSaveSubDoc;
	gcrittergroupEditor.close_func = crittergroupEditorEMCloseDoc;
	gcrittergroupEditor.sub_close_func = crittergroupEditorEMCloseSubDoc;
	gcrittergroupEditor.sub_reload_func = crittergroupEditorEMReloadSubDoc;

	// Register the picker
	gcrittergroupPicker.allow_outsource = 1;
	strcpy(gcrittergroupPicker.picker_name, "CritterGroup Library");
	strcpy(gcrittergroupPicker.default_type, gcrittergroupEditor.default_type);
	emPickerManage(&gcrittergroupPicker);
	eaPush(&gcrittergroupEditor.pickers, &gcrittergroupPicker);

	emRegisterEditor(&gcrittergroupEditor);
	emRegisterFileType(gcrittergroupEditor.default_type, "CritterGroup", gcrittergroupEditor.editor_name);
#endif

	return 0;
}

