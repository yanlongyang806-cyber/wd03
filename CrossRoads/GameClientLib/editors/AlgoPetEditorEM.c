/***************************************************************************
*     Copyright (c) 2006-2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef NO_EDITORS

#include "error.h"
#include "file.h"
#include "AlgoPet.h"
#include "AlgoPetEditor.h"
#include "MultiEditTable.h"
#include "MultiEditWindow.h"

// Magic code required by budgets
AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););


//-----------------------------------------------------------------------------------
// Global Data
//-----------------------------------------------------------------------------------


EMEditor gAlgoPetEditor;

EMPicker gAlgoPetPicker;

static MultiEditEMDoc *gAlgoPetEditorDoc = NULL;

//---------------------------------------------------------------------------------------------------
// Editor
//---------------------------------------------------------------------------------------------------


static EMEditorDoc *algoPetEditorEMNewDoc(const char *pcType, void *data)
{
	if (!gConf.bServerSaving) {
		Alertf("This editor only operates when server saving is enabled");
		return NULL;
	}
	algoPetEditor_createAlgoPet(NULL);

	return (EMEditorDoc*)gAlgoPetEditorDoc;
}


static EMEditorDoc *algoPetEditorEMLoadDoc(const char *pcName, const char *pcType)
{
	if (!gConf.bServerSaving) {
		Alertf("This editor only operates when server saving is enabled");
		return NULL;
	}
	MEWindowOpenObject(gAlgoPetEditorDoc->pWindow, (char*)pcName);

	return (EMEditorDoc*)gAlgoPetEditorDoc;
}


static EMTaskStatus algoPetEditorEMSaveDoc(EMEditorDoc *pDoc)
{
	return MEWindowSaveAll(gAlgoPetEditorDoc->pWindow);
}


static EMTaskStatus algoPetEditorEMSaveSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	return MEWindowSaveObject(gAlgoPetEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void algoPetEditorEMCloseDoc(EMEditorDoc *pDoc)
{
	MEWindowExit(gAlgoPetEditorDoc->pWindow);
}


static void algoPetEditorEMCloseSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	MEWindowCloseObject(gAlgoPetEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void algoPetEditorEMReloadSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	MEWindowRevertObject(gAlgoPetEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void algoPetEditorEMEnter(EMEditor *pEditor)
{
	resRequestAllResourcesInDictionary("PowerTreeNodeDef");
}

static void algoPetEditorEMExit(EMEditor *pEditor)
{

}

static void algoPetEditorEMLostFocus(EMEditorDoc *pDoc)
{
	MEWindowLostFocus(gAlgoPetEditorDoc->pWindow);
}

static void algoPetEditorEMInit(EMEditor *pEditor)
{
	// Create the one global document
	if (!gAlgoPetEditorDoc) {
		// Create the global document
		gAlgoPetEditorDoc = calloc(1,sizeof(MultiEditEMDoc));
		gAlgoPetEditorDoc->emDoc.editor = &gAlgoPetEditor;
		gAlgoPetEditorDoc->emDoc.saved = true;
		gAlgoPetEditorDoc->pWindow = algoPetEditor_init(gAlgoPetEditorDoc);
		sprintf(gAlgoPetEditorDoc->emDoc.doc_name, "Algo Pet Editor");
		sprintf(gAlgoPetEditorDoc->emDoc.doc_display_name, "Algo Pet Editor");
		gAlgoPetEditorDoc->emDoc.primary_ui_window = MEWindowGetUIWindow(gAlgoPetEditorDoc->pWindow);
	}
}

#endif

AUTO_RUN_LATE;
int algoEditorEMRegister(void)
{
#ifndef NO_EDITORS
	if (!areEditorsAllowed())
		return 0;

	// Register the editor
	strcpy(gAlgoPetEditor.editor_name, "Algo Pet Editor");
	gAlgoPetEditor.type = EM_TYPE_MULTIDOC;
	gAlgoPetEditor.hide_world = 1;
	gAlgoPetEditor.disable_single_doc_menus = 1;
	gAlgoPetEditor.disable_auto_checkout = 1;
	gAlgoPetEditor.default_type = "AlgoPetDef";
	strcpy(gAlgoPetEditor.default_workspace, "Game Design Editors");

	gAlgoPetEditor.init_func = algoPetEditorEMInit;
	gAlgoPetEditor.enter_editor_func = algoPetEditorEMEnter;
	gAlgoPetEditor.exit_func = algoPetEditorEMExit;
	gAlgoPetEditor.lost_focus_func = algoPetEditorEMLostFocus;
	gAlgoPetEditor.new_func = algoPetEditorEMNewDoc;
	gAlgoPetEditor.load_func = algoPetEditorEMLoadDoc;
	gAlgoPetEditor.save_func = algoPetEditorEMSaveDoc;
	gAlgoPetEditor.sub_save_func = algoPetEditorEMSaveSubDoc;
	gAlgoPetEditor.close_func = algoPetEditorEMCloseDoc;
	gAlgoPetEditor.sub_close_func = algoPetEditorEMCloseSubDoc;
	gAlgoPetEditor.sub_reload_func = algoPetEditorEMReloadSubDoc;

	// Register the picker
	gAlgoPetPicker.allow_outsource = 1;
	strcpy(gAlgoPetPicker.picker_name, "Algo Pet Library");
	strcpy(gAlgoPetPicker.default_type, gAlgoPetEditor.default_type);
	emPickerManage(&gAlgoPetPicker);
	eaPush(&gAlgoPetEditor.pickers, &gAlgoPetPicker);

	emRegisterEditor(&gAlgoPetEditor);
	emRegisterFileType(gAlgoPetEditor.default_type, "AlgoPet", gAlgoPetEditor.editor_name);
#endif

	return 0;
}