/***************************************************************************
*     Copyright (c) 2006-2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef NO_EDITORS

#include "error.h"
#include "file.h"
#include "ItemAssignments.h"
#include "ItemAssignmentEditor.h"
#include "MultiEditTable.h"
#include "MultiEditWindow.h"

// Magic code required by budgets
AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););


//-----------------------------------------------------------------------------------
// Global Data
//-----------------------------------------------------------------------------------


EMEditor gItemAssignmentEditor;

EMPicker gItemAssignmentPicker;

static MultiEditEMDoc *gItemAssignmentEditorDoc = NULL;

//---------------------------------------------------------------------------------------------------
// Editor
//---------------------------------------------------------------------------------------------------


static EMEditorDoc *itemAssignmentEditorEMNewDoc(const char *pcType, void *data)
{
	if (!gConf.bServerSaving) {
		Alertf("This editor only operates when server saving is enabled");
		return NULL;
	}
	itemAssignmentEditor_CreateItemAssignment(NULL);

	return (EMEditorDoc*)gItemAssignmentEditorDoc;
}


static EMEditorDoc *itemAssignmentEditorEMLoadDoc(const char *pcName, const char *pcType)
{
	if (!gConf.bServerSaving) {
		Alertf("This editor only operates when server saving is enabled");
		return NULL;
	}
	MEWindowOpenObject(gItemAssignmentEditorDoc->pWindow, (char*)pcName);

	return (EMEditorDoc*)gItemAssignmentEditorDoc;
}


static EMTaskStatus itemAssignmentEditorEMSaveDoc(EMEditorDoc *pDoc)
{
	return MEWindowSaveAll(gItemAssignmentEditorDoc->pWindow);
}


static EMTaskStatus itemAssignmentEditorEMSaveSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	return MEWindowSaveObject(gItemAssignmentEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void itemAssignmentEditorEMCloseDoc(EMEditorDoc *pDoc)
{
	MEWindowExit(gItemAssignmentEditorDoc->pWindow);
}


static void itemAssignmentEditorEMCloseSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	MEWindowCloseObject(gItemAssignmentEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void itemAssignmentEditorEMReloadSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	MEWindowRevertObject(gItemAssignmentEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void itemAssignmentEditorEMEnter(EMEditor *pEditor)
{

}

static void itemAssignmentEditorEMExit(EMEditor *pEditor)
{

}

static void itemAssignmentEditorEMLostFocus(EMEditorDoc *pDoc)
{
	MEWindowLostFocus(gItemAssignmentEditorDoc->pWindow);
}

static void itemAssignmentEditorEMInit(EMEditor *pEditor)
{
	// Create the one global document
	if (!gItemAssignmentEditorDoc) {
		// Create the global document
		gItemAssignmentEditorDoc = calloc(1,sizeof(MultiEditEMDoc));
		gItemAssignmentEditorDoc->emDoc.editor = &gItemAssignmentEditor;
		gItemAssignmentEditorDoc->emDoc.saved = true;
		gItemAssignmentEditorDoc->pWindow = itemAssignmentEditor_Init(gItemAssignmentEditorDoc);
		sprintf(gItemAssignmentEditorDoc->emDoc.doc_name, "Item Assignment Editor");
		sprintf(gItemAssignmentEditorDoc->emDoc.doc_display_name, "Item Assignment Editor");
		gItemAssignmentEditorDoc->emDoc.primary_ui_window = MEWindowGetUIWindow(gItemAssignmentEditorDoc->pWindow);
	}
}

#endif

AUTO_RUN_LATE;
int itemAssignmentEditorEMRegister(void)
{
#ifndef NO_EDITORS
	if (!areEditorsAllowed())
		return 0;

	// Register the editor
	strcpy(gItemAssignmentEditor.editor_name, "Item Assignment Editor");
	gItemAssignmentEditor.type = EM_TYPE_MULTIDOC;
	gItemAssignmentEditor.hide_world = 1;
	gItemAssignmentEditor.disable_single_doc_menus = 1;
	gItemAssignmentEditor.disable_auto_checkout = 1;
	gItemAssignmentEditor.default_type = "ItemAssignmentDef";
	strcpy(gItemAssignmentEditor.default_workspace, "Game Design Editors");

	gItemAssignmentEditor.init_func = itemAssignmentEditorEMInit;
	gItemAssignmentEditor.enter_editor_func = itemAssignmentEditorEMEnter;
	gItemAssignmentEditor.exit_func = itemAssignmentEditorEMExit;
	gItemAssignmentEditor.lost_focus_func = itemAssignmentEditorEMLostFocus;
	gItemAssignmentEditor.new_func = itemAssignmentEditorEMNewDoc;
	gItemAssignmentEditor.load_func = itemAssignmentEditorEMLoadDoc;
	gItemAssignmentEditor.save_func = itemAssignmentEditorEMSaveDoc;
	gItemAssignmentEditor.sub_save_func = itemAssignmentEditorEMSaveSubDoc;
	gItemAssignmentEditor.close_func = itemAssignmentEditorEMCloseDoc;
	gItemAssignmentEditor.sub_close_func = itemAssignmentEditorEMCloseSubDoc;
	gItemAssignmentEditor.sub_reload_func = itemAssignmentEditorEMReloadSubDoc;

	// Register the picker
	gItemAssignmentPicker.allow_outsource = 1;
	strcpy(gItemAssignmentPicker.picker_name, "Item Assignment Library");
	strcpy(gItemAssignmentPicker.default_type, gItemAssignmentEditor.default_type);
	emPickerManage(&gItemAssignmentPicker);
	eaPush(&gItemAssignmentEditor.pickers, &gItemAssignmentPicker);

	emRegisterEditor(&gItemAssignmentEditor);
	emRegisterFileType(gItemAssignmentEditor.default_type, "ItemAssignment", gItemAssignmentEditor.editor_name);
#endif

	return 0;
}