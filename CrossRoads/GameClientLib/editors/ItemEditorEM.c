//
// ItemEditorEM.c
//

#ifndef NO_EDITORS

#include "error.h"
#include "file.h"
#include "ItemEditor.h"
#include "itemCommon.h"
#include "MultiEditTable.h"
#include "MultiEditWindow.h"


// Magic code required by budgets
AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

//-----------------------------------------------------------------------------------
// Global Data
//-----------------------------------------------------------------------------------


EMEditor gItemEditor;

EMPicker gItemPicker;

static MultiEditEMDoc *gItemEditorDoc = NULL;


//---------------------------------------------------------------------------------------------------
// Editor
//---------------------------------------------------------------------------------------------------


static EMEditorDoc *itemEditorEMNewDoc(const char *pcType, void *data)
{
	if (!gConf.bServerSaving) {
		Alertf("This editor only operates when server saving is enabled");
		return NULL;
	}
	itemEditor_createItem(NULL);

	return (EMEditorDoc*)gItemEditorDoc;
}


static EMEditorDoc *itemEditorEMLoadDoc(const char *pcName, const char *pcType)
{
	if (!gConf.bServerSaving) {
		Alertf("This editor only operates when server saving is enabled");
		return NULL;
	}
	MEWindowOpenObject(gItemEditorDoc->pWindow, (char*)pcName);

	return (EMEditorDoc*)gItemEditorDoc;
}


static EMTaskStatus itemEditorEMSaveDoc(EMEditorDoc *pDoc)
{
	return MEWindowSaveAll(gItemEditorDoc->pWindow);
}


static EMTaskStatus itemEditorEMSaveSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	return MEWindowSaveObject(gItemEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void itemEditorEMCloseDoc(EMEditorDoc *pDoc)
{
	MEWindowExit(gItemEditorDoc->pWindow);
}


static void itemEditorEMCloseSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	MEWindowCloseObject(gItemEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void itemEditorEMReloadSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	MEWindowRevertObject(gItemEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void itemEditorEMEnter(EMEditor *pEditor)
{

}

static void itemEditorEMExit(EMEditor *pEditor)
{

}

static void itemEditorEMLostFocus(EMEditorDoc *pDoc)
{
	MEWindowLostFocus(gItemEditorDoc->pWindow);
}

static void itemEditorEMInit(EMEditor *pEditor)
{
	// Create the one global document
	if (!gItemEditorDoc) {
		// Create the global document
		gItemEditorDoc = calloc(1,sizeof(MultiEditEMDoc));
		gItemEditorDoc->emDoc.editor = &gItemEditor;
		gItemEditorDoc->emDoc.saved = true;
		gItemEditorDoc->pWindow = itemEditor_init(gItemEditorDoc);
		sprintf(gItemEditorDoc->emDoc.doc_name, "Item Editor");
		sprintf(gItemEditorDoc->emDoc.doc_display_name, "Item Editor");
		gItemEditorDoc->emDoc.primary_ui_window = MEWindowGetUIWindow(gItemEditorDoc->pWindow);
	}
}

#endif

AUTO_RUN_LATE;
int itemEditorEMRegister(void)
{
#ifndef NO_EDITORS
	if (!areEditorsAllowed())
		return 0;

	// Register the editor
	strcpy(gItemEditor.editor_name, "Item Editor");
	gItemEditor.type = EM_TYPE_MULTIDOC;
	gItemEditor.hide_world = 1;
	gItemEditor.disable_single_doc_menus = 1;
	gItemEditor.disable_auto_checkout = 1;
	gItemEditor.default_type = "ItemDef";
	strcpy(gItemEditor.default_workspace, "Game Design Editors");

	gItemEditor.init_func = itemEditorEMInit;
	gItemEditor.enter_editor_func = itemEditorEMEnter;
	gItemEditor.exit_func = itemEditorEMExit;
	gItemEditor.lost_focus_func = itemEditorEMLostFocus;
	gItemEditor.new_func = itemEditorEMNewDoc;
	gItemEditor.load_func = itemEditorEMLoadDoc;
	gItemEditor.save_func = itemEditorEMSaveDoc;
	gItemEditor.sub_save_func = itemEditorEMSaveSubDoc;
	gItemEditor.close_func = itemEditorEMCloseDoc;
	gItemEditor.sub_close_func = itemEditorEMCloseSubDoc;
	gItemEditor.sub_reload_func = itemEditorEMReloadSubDoc;

	// Register the picker
	gItemPicker.allow_outsource = 1;
	strcpy(gItemPicker.picker_name, "Item Library");
	strcpy(gItemPicker.default_type, gItemEditor.default_type);
	emPickerManage(&gItemPicker);
	eaPush(&gItemEditor.pickers, &gItemPicker);

	emRegisterEditor(&gItemEditor);
	emRegisterFileType(gItemEditor.default_type, "Item", gItemEditor.editor_name);
#endif

	return 0;
}

