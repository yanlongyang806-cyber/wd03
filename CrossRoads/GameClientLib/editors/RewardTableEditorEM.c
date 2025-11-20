//
// RewardTableEditorEM.c
//

#ifndef NO_EDITORS

#include "error.h"
#include "file.h"
#include "MultiEditWindow.h"
#include "MultiEditTable.h"
#include "RewardTableEditor.h"



// Magic code required by budgets
AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

//-----------------------------------------------------------------------------------
// Global Data
//-----------------------------------------------------------------------------------


EMEditor gRewardTableEditor;

EMPicker gRewardTablePicker;

static MultiEditEMDoc *gRewardTableEditorDoc = NULL;


//---------------------------------------------------------------------------------------------------
// Editor
//---------------------------------------------------------------------------------------------------

static EMEditorDoc *rewardTableEditorEMNewDoc(const char *pcType, void *data)
{
	if (!gConf.bServerSaving) {
		Alertf("This editor only operates when server saving is enabled");
		return NULL;
	}
	rewardTableEditor_createRewardTable((rewardTableEditor_InitData*)data);

	return (EMEditorDoc*)gRewardTableEditorDoc;
}


static EMEditorDoc *rewardTableEditorEMLoadDoc(const char *pcName, const char *pcType)
{
	if (!gConf.bServerSaving) {
		Alertf("This editor only operates when server saving is enabled");
		return NULL;
	}
	MEWindowOpenObject(gRewardTableEditorDoc->pWindow, (char*)pcName);

	return (EMEditorDoc*)gRewardTableEditorDoc;
}


static EMTaskStatus rewardTableEditorEMSaveDoc(EMEditorDoc *pDoc)
{
	return MEWindowSaveAll(gRewardTableEditorDoc->pWindow);
}


static EMTaskStatus rewardTableEditorEMSaveSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	return MEWindowSaveObject(gRewardTableEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void rewardTableEditorEMCloseDoc(EMEditorDoc *pDoc)
{
	MEWindowExit(gRewardTableEditorDoc->pWindow);
}


static void rewardTableEditorEMCloseSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	MEWindowCloseObject(gRewardTableEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void rewardTableEditorEMReloadSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	MEWindowRevertObject(gRewardTableEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void rewardTableEditorEMEnter(EMEditor *pEditor)
{

}

static void rewardTableEditorEMExit(EMEditor *pEditor)
{

}

static void rewardTableEditorEMLostFocus(EMEditorDoc *pDoc)
{
	MEWindowLostFocus(gRewardTableEditorDoc->pWindow);
}

static void rewardTableEditorEMInit(EMEditor *pEditor)
{
	// Create the one global document
	if (!gRewardTableEditorDoc) {
		// Create the global document
		gRewardTableEditorDoc = calloc(1,sizeof(MultiEditEMDoc));
		gRewardTableEditorDoc->emDoc.editor = &gRewardTableEditor;
		gRewardTableEditorDoc->emDoc.saved = true;
		gRewardTableEditorDoc->pWindow = rewardTableEditor_init(gRewardTableEditorDoc);
		sprintf(gRewardTableEditorDoc->emDoc.doc_name, "Reward Table Editor");
		sprintf(gRewardTableEditorDoc->emDoc.doc_display_name, "Reward Table Editor");
		gRewardTableEditorDoc->emDoc.primary_ui_window = MEWindowGetUIWindow(gRewardTableEditorDoc->pWindow);
	}
}

#endif

AUTO_RUN_LATE;
int rewardTableEditorEMRegister(void)
{
#ifndef NO_EDITORS
	if (!areEditorsAllowed())
		return 0;

	// Register the editor
	strcpy(gRewardTableEditor.editor_name, "Reward Table Editor");
	gRewardTableEditor.type = EM_TYPE_MULTIDOC;
	gRewardTableEditor.hide_world = 1;
	gRewardTableEditor.disable_single_doc_menus = 1;
	gRewardTableEditor.disable_auto_checkout = 1;
	gRewardTableEditor.default_type = "RewardTable";
	strcpy(gRewardTableEditor.default_workspace, "Game Design Editors");

	gRewardTableEditor.init_func = rewardTableEditorEMInit;
	gRewardTableEditor.enter_editor_func = rewardTableEditorEMEnter;
	gRewardTableEditor.exit_func = rewardTableEditorEMExit;
	gRewardTableEditor.lost_focus_func = rewardTableEditorEMLostFocus;
	gRewardTableEditor.new_func = rewardTableEditorEMNewDoc;
	gRewardTableEditor.load_func = rewardTableEditorEMLoadDoc;
	gRewardTableEditor.save_func = rewardTableEditorEMSaveDoc;
	gRewardTableEditor.sub_save_func = rewardTableEditorEMSaveSubDoc;
	gRewardTableEditor.close_func = rewardTableEditorEMCloseDoc;
	gRewardTableEditor.sub_close_func = rewardTableEditorEMCloseSubDoc;
	gRewardTableEditor.sub_reload_func = rewardTableEditorEMReloadSubDoc;

	// Register the picker
	gRewardTablePicker.allow_outsource = 1;
	strcpy(gRewardTablePicker.picker_name, "Reward Table Library");
	strcpy(gRewardTablePicker.default_type, gRewardTableEditor.default_type);
	emPickerManage(&gRewardTablePicker);
	eaPush(&gRewardTableEditor.pickers, &gRewardTablePicker);

	emRegisterEditor(&gRewardTableEditor);
	emRegisterFileType(gRewardTableEditor.default_type, "Reward Table", gRewardTableEditor.editor_name);
#endif

	return 0;
}

