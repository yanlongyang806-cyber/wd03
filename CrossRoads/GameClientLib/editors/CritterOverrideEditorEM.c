//
// CritterOverrideEditorEM.c
//

#ifndef NO_EDITORS

#include "CritterOverrideEditor.h"
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

EMEditor gCritterOverrideEditor;

EMPicker gCritterOverridePicker;

static MultiEditEMDoc *gCritterOverrideEditorDoc = NULL;


//---------------------------------------------------------------------------------------------------
// Editor
//---------------------------------------------------------------------------------------------------


static EMEditorDoc *CritterOverrideEditorEMNewDoc(const char *pcType, void *data)
{
	if (!gConf.bServerSaving) {
		Alertf("This editor only operates when server saving is enabled");
		return NULL;
	}
	CritterOverrideEditor_createCritterOverride(NULL);

	return (EMEditorDoc*)gCritterOverrideEditorDoc;
}


static EMEditorDoc *CritterOverrideEditorEMLoadDoc(const char *pcName, const char *pcType)
{
	if (!gConf.bServerSaving) {
		Alertf("This editor only operates when server saving is enabled");
		return NULL;
	}
	MEWindowOpenObject(gCritterOverrideEditorDoc->pWindow, (char*)pcName);

	return (EMEditorDoc*)gCritterOverrideEditorDoc;
}


static EMTaskStatus CritterOverrideEditorEMSaveDoc(EMEditorDoc *pDoc)
{
	return MEWindowSaveAll(gCritterOverrideEditorDoc->pWindow);
}


static EMTaskStatus CritterOverrideEditorEMSaveSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	return MEWindowSaveObject(gCritterOverrideEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void CritterOverrideEditorEMCloseDoc(EMEditorDoc *pDoc)
{
	MEWindowExit(gCritterOverrideEditorDoc->pWindow);
}


static void CritterOverrideEditorEMCloseSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	MEWindowCloseObject(gCritterOverrideEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void CritterOverrideEditorEMReloadSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	MEWindowRevertObject(gCritterOverrideEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void CritterOverrideEditorEMEnter(EMEditor *pEditor)
{

}

static void CritterOverrideEditorEMExit(EMEditor *pEditor)
{

}

static void CritterOverrideEditorEMLostFocus(EMEditorDoc *pDoc)
{
	MEWindowLostFocus(gCritterOverrideEditorDoc->pWindow);
}

static void CritterOverrideEditorEMInit(EMEditor *pEditor)
{
	// Create the one global document
	if (!gCritterOverrideEditorDoc) {
		// Create the global document
		gCritterOverrideEditorDoc = calloc(1,sizeof(MultiEditEMDoc));
		gCritterOverrideEditorDoc->emDoc.editor = &gCritterOverrideEditor;
		gCritterOverrideEditorDoc->emDoc.saved = true;
		gCritterOverrideEditorDoc->pWindow = CritterOverrideEditor_init(gCritterOverrideEditorDoc);
		sprintf(gCritterOverrideEditorDoc->emDoc.doc_name, "Critter Override Editor");
		sprintf(gCritterOverrideEditorDoc->emDoc.doc_display_name, "Critter Override Editor");
		gCritterOverrideEditorDoc->emDoc.primary_ui_window = MEWindowGetUIWindow(gCritterOverrideEditorDoc->pWindow);
	}
}

#endif

AUTO_RUN_LATE;
int CritterOverrideEditorEMRegister(void)
{
#ifndef NO_EDITORS
	if (!areEditorsAllowed())
		return 0;

	strcpy(gCritterOverrideEditor.editor_name, "Critter Override Editor");
	gCritterOverrideEditor.type = EM_TYPE_MULTIDOC;
	gCritterOverrideEditor.hide_world = 1;
	gCritterOverrideEditor.disable_single_doc_menus = 1;
	gCritterOverrideEditor.disable_auto_checkout = 1;
	gCritterOverrideEditor.default_type = "CritterOverrideDef";
	strcpy(gCritterOverrideEditor.default_workspace, "Game Design Editors");

	gCritterOverrideEditor.init_func = CritterOverrideEditorEMInit;
	gCritterOverrideEditor.enter_editor_func = CritterOverrideEditorEMEnter;
	gCritterOverrideEditor.exit_func = CritterOverrideEditorEMExit;
	gCritterOverrideEditor.lost_focus_func = CritterOverrideEditorEMLostFocus;
	gCritterOverrideEditor.new_func = CritterOverrideEditorEMNewDoc;
	gCritterOverrideEditor.load_func = CritterOverrideEditorEMLoadDoc;
	gCritterOverrideEditor.save_func = CritterOverrideEditorEMSaveDoc;
	gCritterOverrideEditor.sub_save_func = CritterOverrideEditorEMSaveSubDoc;
	gCritterOverrideEditor.close_func = CritterOverrideEditorEMCloseDoc;
	gCritterOverrideEditor.sub_close_func = CritterOverrideEditorEMCloseSubDoc;
	gCritterOverrideEditor.sub_reload_func = CritterOverrideEditorEMReloadSubDoc;

	// Register the picker
	gCritterOverridePicker.allow_outsource = 1;
	strcpy(gCritterOverridePicker.picker_name, "Critter Override Library");
	strcpy(gCritterOverridePicker.default_type, gCritterOverrideEditor.default_type);
	emPickerManage(&gCritterOverridePicker);
	eaPush(&gCritterOverrideEditor.pickers, &gCritterOverridePicker);

	emRegisterEditor(&gCritterOverrideEditor);
	emRegisterFileType(gCritterOverrideEditor.default_type, "Critter Override", gCritterOverrideEditor.editor_name);
#endif

	return 0;
}

