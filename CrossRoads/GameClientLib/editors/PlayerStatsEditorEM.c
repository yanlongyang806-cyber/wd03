//
// PlayerStatsEditorEM.c
//

#ifndef NO_EDITORS

#include "error.h"
#include "file.h"
#include "MultiEditTable.h"
#include "MultiEditWindow.h"
#include "PlayerStatsEditor.h"


// This magic line is required if you run in Full Debug to avoid assert
AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

EMEditor gPlayerStatsEditor;

EMPicker gPlayerStatsPicker;

static MultiEditEMDoc *gPlayerStatsEditorDoc = NULL;


//---------------------------------------------------------------------------------------------------
// Editor
//---------------------------------------------------------------------------------------------------


static EMEditorDoc *playerStatsEditorEMNewDoc(const char *pcType, void *data)
{
	if (!gConf.bServerSaving) {
		Alertf("This editor only operates when server saving is enabled");
		return NULL;
	}
	playerStatsEditor_createPlayerStat(NULL);

	return (EMEditorDoc*)gPlayerStatsEditorDoc;
}


static EMEditorDoc *playerStatsEditorEMLoadDoc(const char *pcName, const char *pcType)
{
	if (!gConf.bServerSaving) {
		Alertf("This editor only operates when server saving is enabled");
		return NULL;
	}
	MEWindowOpenObject(gPlayerStatsEditorDoc->pWindow, (char*)pcName);

	return (EMEditorDoc*)gPlayerStatsEditorDoc;
}


static EMTaskStatus playerStatsEditorEMSaveDoc(EMEditorDoc *pDoc)
{
	return MEWindowSaveAll(gPlayerStatsEditorDoc->pWindow);
}


static EMTaskStatus playerStatsEditorEMSaveSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	return MEWindowSaveObject(gPlayerStatsEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void playerStatsEditorEMCloseDoc(EMEditorDoc *pDoc)
{
	MEWindowExit(gPlayerStatsEditorDoc->pWindow);
}


static void playerStatsEditorEMCloseSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	MEWindowCloseObject(gPlayerStatsEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void playerStatsEditorEMReloadSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	MEWindowRevertObject(gPlayerStatsEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void playerStatsEditorEMEnter(EMEditor *pEditor)
{

}

static void playerStatsEditorEMExit(EMEditor *pEditor)
{

}

static void playerStatsEditorEMLostFocus(EMEditorDoc *pDoc)
{
	MEWindowLostFocus(gPlayerStatsEditorDoc->pWindow);
}

static void playerStatsEditorEMInit(EMEditor *pEditor)
{
	// Create the one global document
	if (!gPlayerStatsEditorDoc) {
		// Create the global document
		gPlayerStatsEditorDoc = calloc(1,sizeof(MultiEditEMDoc));
		gPlayerStatsEditorDoc->emDoc.editor = &gPlayerStatsEditor;
		gPlayerStatsEditorDoc->emDoc.saved = true;
		gPlayerStatsEditorDoc->pWindow = playerStatsEditor_init(gPlayerStatsEditorDoc);
		sprintf(gPlayerStatsEditorDoc->emDoc.doc_name, "PlayerStat Editor");
		sprintf(gPlayerStatsEditorDoc->emDoc.doc_display_name, "PlayerStat Editor");
		gPlayerStatsEditorDoc->emDoc.primary_ui_window = MEWindowGetUIWindow(gPlayerStatsEditorDoc->pWindow);
	}
}

#endif

AUTO_RUN_LATE;
int playerStatsEditorEMRegister(void)
{
#ifndef NO_EDITORS
	if (!areEditorsAllowed())
		return 0;

	// register editor
	strcpy(gPlayerStatsEditor.editor_name, "PlayerStat Editor");
	gPlayerStatsEditor.type = EM_TYPE_MULTIDOC;
	gPlayerStatsEditor.hide_world = 1;
	gPlayerStatsEditor.disable_single_doc_menus = 1;
	gPlayerStatsEditor.disable_auto_checkout = 1;
	gPlayerStatsEditor.default_type = "PlayerStatDef";
	strcpy(gPlayerStatsEditor.default_workspace, "Game Design Editors");

	gPlayerStatsEditor.init_func = playerStatsEditorEMInit;
	gPlayerStatsEditor.enter_editor_func = playerStatsEditorEMEnter;
	gPlayerStatsEditor.exit_func = playerStatsEditorEMExit;
	gPlayerStatsEditor.lost_focus_func = playerStatsEditorEMLostFocus;
	gPlayerStatsEditor.new_func = playerStatsEditorEMNewDoc;
	gPlayerStatsEditor.load_func = playerStatsEditorEMLoadDoc;
	gPlayerStatsEditor.save_func = playerStatsEditorEMSaveDoc;
	gPlayerStatsEditor.sub_save_func = playerStatsEditorEMSaveSubDoc;
	gPlayerStatsEditor.close_func = playerStatsEditorEMCloseDoc;
	gPlayerStatsEditor.sub_close_func = playerStatsEditorEMCloseSubDoc;
	gPlayerStatsEditor.sub_reload_func = playerStatsEditorEMReloadSubDoc;

	gPlayerStatsEditor.keybinds_name = "MultiEditor";

	// register picker
	gPlayerStatsPicker.allow_outsource = 1;
	strcpy(gPlayerStatsPicker.picker_name, "PlayerStatDef Library");
	strcpy(gPlayerStatsPicker.default_type, gPlayerStatsEditor.default_type);
	emPickerManage(&gPlayerStatsPicker);
	eaPush(&gPlayerStatsEditor.pickers, &gPlayerStatsPicker);

	emRegisterEditor(&gPlayerStatsEditor);
	emRegisterFileType(gPlayerStatsEditor.default_type, "PlayerStatDef", gPlayerStatsEditor.editor_name);
#endif

	return 1;
}

