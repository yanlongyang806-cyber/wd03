/***************************************************************************
*     Copyright (c) 2006-2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef NO_EDITORS

#include "error.h"
#include "file.h"
#include "itemGenCommon.h"
#include "ItemGenEditor.h"
#include "MultiEditTable.h"
#include "MultiEditWindow.h"

// Magic code required by budgets
AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););


//-----------------------------------------------------------------------------------
// Global Data
//-----------------------------------------------------------------------------------


EMEditor gItemGenEditor;

EMPicker gItemGenPicker;

static MultiEditEMDoc *gItemGenEditorDoc = NULL;

//---------------------------------------------------------------------------------------------------
// Editor
//---------------------------------------------------------------------------------------------------


static EMEditorDoc *itemGenEditorEMNewDoc(const char *pcType, void *data)
{
	if (!gConf.bServerSaving) {
		Alertf("This editor only operates when server saving is enabled");
		return NULL;
	}
	itemGenEditor_createItemGen(NULL);

	return (EMEditorDoc*)gItemGenEditorDoc;
}


static EMEditorDoc *itemGenEditorEMLoadDoc(const char *pcName, const char *pcType)
{
	if (!gConf.bServerSaving) {
		Alertf("This editor only operates when server saving is enabled");
		return NULL;
	}
	MEWindowOpenObject(gItemGenEditorDoc->pWindow, (char*)pcName);

	return (EMEditorDoc*)gItemGenEditorDoc;
}


static EMTaskStatus itemGenEditorEMSaveDoc(EMEditorDoc *pDoc)
{
	return MEWindowSaveAll(gItemGenEditorDoc->pWindow);
}


static EMTaskStatus itemGenEditorEMSaveSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	return MEWindowSaveObject(gItemGenEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void itemGenEditorEMCloseDoc(EMEditorDoc *pDoc)
{
	MEWindowExit(gItemGenEditorDoc->pWindow);
}


static void itemGenEditorEMCloseSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	MEWindowCloseObject(gItemGenEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void itemGenEditorEMReloadSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	MEWindowRevertObject(gItemGenEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void itemGenEditorEMEnter(EMEditor *pEditor)
{

}

static void itemGenEditorEMExit(EMEditor *pEditor)
{

}

static void itemGenEditorEMLostFocus(EMEditorDoc *pDoc)
{
	MEWindowLostFocus(gItemGenEditorDoc->pWindow);
}

static void itemGenEditorEMInit(EMEditor *pEditor)
{
	// Create the one global document
	if (!gItemGenEditorDoc) {
		// Create the global document
		gItemGenEditorDoc = calloc(1,sizeof(MultiEditEMDoc));
		gItemGenEditorDoc->emDoc.editor = &gItemGenEditor;
		gItemGenEditorDoc->emDoc.saved = true;
		gItemGenEditorDoc->pWindow = itemGenEditor_init(gItemGenEditorDoc);
		sprintf(gItemGenEditorDoc->emDoc.doc_name, "Item Gen Editor");
		sprintf(gItemGenEditorDoc->emDoc.doc_display_name, "Item Gen Editor");
		gItemGenEditorDoc->emDoc.primary_ui_window = MEWindowGetUIWindow(gItemGenEditorDoc->pWindow);
	}
}

#endif

AUTO_RUN_LATE;
int itemGenEditorEMRegister(void)
{
#ifndef NO_EDITORS
	if (!areEditorsAllowed())
		return 0;

	// Register the editor
	strcpy(gItemGenEditor.editor_name, "Item Gen Editor");
	gItemGenEditor.type = EM_TYPE_MULTIDOC;
	gItemGenEditor.hide_world = 1;
	gItemGenEditor.disable_single_doc_menus = 1;
	gItemGenEditor.disable_auto_checkout = 1;
	gItemGenEditor.default_type = "itemGenData";
	strcpy(gItemGenEditor.default_workspace, "Game Design Editors");

	gItemGenEditor.init_func = itemGenEditorEMInit;
	gItemGenEditor.enter_editor_func = itemGenEditorEMEnter;
	gItemGenEditor.exit_func = itemGenEditorEMExit;
	gItemGenEditor.lost_focus_func = itemGenEditorEMLostFocus;
	gItemGenEditor.new_func = itemGenEditorEMNewDoc;
	gItemGenEditor.load_func = itemGenEditorEMLoadDoc;
	gItemGenEditor.save_func = itemGenEditorEMSaveDoc;
	gItemGenEditor.sub_save_func = itemGenEditorEMSaveSubDoc;
	gItemGenEditor.close_func = itemGenEditorEMCloseDoc;
	gItemGenEditor.sub_close_func = itemGenEditorEMCloseSubDoc;
	gItemGenEditor.sub_reload_func = itemGenEditorEMReloadSubDoc;

	// Register the picker
	gItemGenPicker.allow_outsource = 1;
	strcpy(gItemGenPicker.picker_name, "Item Gen Library");
	strcpy(gItemGenPicker.default_type, gItemGenEditor.default_type);
	emPickerManage(&gItemGenPicker);
	eaPush(&gItemGenEditor.pickers, &gItemGenPicker);

	emRegisterEditor(&gItemGenEditor);
	emRegisterFileType(gItemGenEditor.default_type, "itemGen", gItemGenEditor.editor_name);
#endif

	return 0;
}