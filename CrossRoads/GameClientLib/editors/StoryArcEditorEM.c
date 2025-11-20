/***************************************************************************
*     Copyright (c) 2006-2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef NO_EDITORS

#include "error.h"
#include "file.h"
#include "mission_common.h"
#include "StoryArcEditor.h"
#include "MultiEditTable.h"
#include "MultiEditWindow.h"

// Magic code required by budgets
AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););


//-----------------------------------------------------------------------------------
// Global Data
//-----------------------------------------------------------------------------------


EMEditor gStoryArcEditor;

EMPicker gStoryArcPicker;

static MultiEditEMDoc *gStoryArcEditorDoc = NULL;

//---------------------------------------------------------------------------------------------------
// Editor
//---------------------------------------------------------------------------------------------------


static EMEditorDoc *storyArcEditorEMNewDoc(const char *pcType, void *data)
{
	if (!gConf.bServerSaving) {
		Alertf("This editor only operates when server saving is enabled");
		return NULL;
	}
	storyArcEditor_CreateStoryArc(NULL);

	return (EMEditorDoc*)gStoryArcEditorDoc;
}


static EMEditorDoc *storyArcEditorEMLoadDoc(const char *pcName, const char *pcType)
{
	if (!gConf.bServerSaving) {
		Alertf("This editor only operates when server saving is enabled");
		return NULL;
	}
	MEWindowOpenObject(gStoryArcEditorDoc->pWindow, (char*)pcName);

	return (EMEditorDoc*)gStoryArcEditorDoc;
}


static EMTaskStatus storyArcEditorEMSaveDoc(EMEditorDoc *pDoc)
{
	return MEWindowSaveAll(gStoryArcEditorDoc->pWindow);
}


static EMTaskStatus storyArcEditorEMSaveSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	return MEWindowSaveObject(gStoryArcEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void storyArcEditorEMCloseDoc(EMEditorDoc *pDoc)
{
	MEWindowExit(gStoryArcEditorDoc->pWindow);
}


static void storyArcEditorEMCloseSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	MEWindowCloseObject(gStoryArcEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void storyArcEditorEMReloadSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	MEWindowRevertObject(gStoryArcEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void storyArcEditorEMEnter(EMEditor *pEditor)
{

}

static void storyArcEditorEMExit(EMEditor *pEditor)
{

}

static void storyArcEditorEMLostFocus(EMEditorDoc *pDoc)
{
	MEWindowLostFocus(gStoryArcEditorDoc->pWindow);
}

static void storyArcEditorEMInit(EMEditor *pEditor)
{
	// Create the one global document
	if (!gStoryArcEditorDoc) {
		// Create the global document
		gStoryArcEditorDoc = calloc(1,sizeof(MultiEditEMDoc));
		gStoryArcEditorDoc->emDoc.editor = &gStoryArcEditor;
		gStoryArcEditorDoc->emDoc.saved = true;
		gStoryArcEditorDoc->pWindow = storyArcEditor_Init(gStoryArcEditorDoc);
		sprintf(gStoryArcEditorDoc->emDoc.doc_name, "Game Progression Node Editor");
		sprintf(gStoryArcEditorDoc->emDoc.doc_display_name, "Game Progression Node Editor");
		gStoryArcEditorDoc->emDoc.primary_ui_window = MEWindowGetUIWindow(gStoryArcEditorDoc->pWindow);
	}
}

#endif

AUTO_RUN_LATE;
int storyArcEditorEMRegister(void)
{
#ifndef NO_EDITORS
	if (!areEditorsAllowed())
		return 0;

	// Register the editor
	strcpy(gStoryArcEditor.editor_name, "Game Progression Node Editor");
	gStoryArcEditor.type = EM_TYPE_MULTIDOC;
	gStoryArcEditor.hide_world = 1;
	gStoryArcEditor.disable_single_doc_menus = 1;
	gStoryArcEditor.disable_auto_checkout = 1;
	gStoryArcEditor.default_type = "GameProgressionNodeDef";
	strcpy(gStoryArcEditor.default_workspace, "Game Design Editors");

	gStoryArcEditor.init_func = storyArcEditorEMInit;
	gStoryArcEditor.enter_editor_func = storyArcEditorEMEnter;
	gStoryArcEditor.exit_func = storyArcEditorEMExit;
	gStoryArcEditor.lost_focus_func = storyArcEditorEMLostFocus;
	gStoryArcEditor.new_func = storyArcEditorEMNewDoc;
	gStoryArcEditor.load_func = storyArcEditorEMLoadDoc;
	gStoryArcEditor.save_func = storyArcEditorEMSaveDoc;
	gStoryArcEditor.sub_save_func = storyArcEditorEMSaveSubDoc;
	gStoryArcEditor.close_func = storyArcEditorEMCloseDoc;
	gStoryArcEditor.sub_close_func = storyArcEditorEMCloseSubDoc;
	gStoryArcEditor.sub_reload_func = storyArcEditorEMReloadSubDoc;

	// Register the picker
	gStoryArcPicker.allow_outsource = 1;
	strcpy(gStoryArcPicker.picker_name, "Game Progression Node Library");
	strcpy(gStoryArcPicker.default_type, gStoryArcEditor.default_type);
	emPickerManage(&gStoryArcPicker);
	eaPush(&gStoryArcEditor.pickers, &gStoryArcPicker);

	emRegisterEditor(&gStoryArcEditor);
	emRegisterFileType(gStoryArcEditor.default_type, "GameProgressionNode", gStoryArcEditor.editor_name);
#endif

	return 0;
}