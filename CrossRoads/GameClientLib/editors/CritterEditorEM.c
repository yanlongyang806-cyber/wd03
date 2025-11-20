//
// CritterEditorEM.c
//

#ifndef NO_EDITORS

#include "CritterEditor.h"
#include "entCritter.h"
#include "error.h"
#include "file.h"
#include "MultiEditTable.h"
#include "MultiEditWindow.h"
#include "GameClientLib.h"

// This magic line is required if you run in Full Debug to avoid assert
AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

EMEditor gCritterEditor;
EMPicker gCritterPicker;

static MultiEditEMDoc *gCritterEditorDoc = NULL;


//---------------------------------------------------------------------------------------------------
// Editor
//---------------------------------------------------------------------------------------------------


static EMEditorDoc *critterEditorEMNewDoc(const char *pcType, void *data)
{
	if (!gConf.bServerSaving) {
		Alertf("This editor only operates when server saving is enabled");
		return NULL;
	}
	critterEditor_createCritter(NULL);

	return (EMEditorDoc*)gCritterEditorDoc;
}


static EMEditorDoc *critterEditorEMLoadDoc(const char *pcName, const char *pcType)
{
	if (!gConf.bServerSaving) {
		Alertf("This editor only operates when server saving is enabled");
		return NULL;
	}
	MEWindowOpenObject(gCritterEditorDoc->pWindow, (char*)pcName);

	return (EMEditorDoc*)gCritterEditorDoc;
}


static EMTaskStatus critterEditorEMSaveDoc(EMEditorDoc *pDoc)
{
	return MEWindowSaveAll(gCritterEditorDoc->pWindow);
}


static EMTaskStatus critterEditorEMSaveSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	return MEWindowSaveObject(gCritterEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void critterEditorEMCloseDoc(EMEditorDoc *pDoc)
{
	MEWindowExit(gCritterEditorDoc->pWindow);
}


static void critterEditorEMCloseSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	MEWindowCloseObject(gCritterEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void critterEditorEMReloadSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	MEWindowRevertObject(gCritterEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void critterEditorEMEnter(EMEditor *pEditor)
{

}

static void critterEditorEMExit(EMEditor *pEditor)
{

}

static void critterEditorEMLostFocus(EMEditorDoc *pDoc)
{
	MEWindowLostFocus(gCritterEditorDoc->pWindow);
}

static void critterEditorEMInit(EMEditor *pEditor)
{
	// Create the one global document
	if (!gCritterEditorDoc) {
		// Create the global document
		gCritterEditorDoc = calloc(1,sizeof(MultiEditEMDoc));
		gCritterEditorDoc->emDoc.editor = &gCritterEditor;
		gCritterEditorDoc->emDoc.saved = true;
		gCritterEditorDoc->pWindow = critterEditor_init(gCritterEditorDoc);
		sprintf(gCritterEditorDoc->emDoc.doc_name, "Critter Editor");
		sprintf(gCritterEditorDoc->emDoc.doc_display_name, "Critter Editor");
		gCritterEditorDoc->emDoc.primary_ui_window = MEWindowGetUIWindow(gCritterEditorDoc->pWindow);

		// These weird lines are here to force this dictionary to actually have values in it
		resDictSetMaxUnreferencedResources("DynFxInfo", RES_DICT_KEEP_ALL);
		resRequestAllResourcesInDictionary("DynFxInfo");
	}
}

#endif

AUTO_RUN_LATE;
int critterEditorEMRegister(void)
{
#ifndef NO_EDITORS
	static bool bInited = false;
	if (!areEditorsAllowed() && areEditorsPossible())
	{
		if (!gGCLState.bRunning)
			gclRegisterEditChangeCallback(critterEditorEMRegister);
		return 0;
	}

	if (bInited)
	{
		return 0;
	}
	bInited = true;

	// register editor
	strcpy(gCritterEditor.editor_name, "Critter Editor");
	gCritterEditor.type = EM_TYPE_MULTIDOC;
	gCritterEditor.hide_world = 1;
	gCritterEditor.disable_single_doc_menus = 1;
	gCritterEditor.disable_auto_checkout = 1;
	gCritterEditor.default_type = "CritterDef";
	strcpy(gCritterEditor.default_workspace, "Game Design Editors");

	gCritterEditor.init_func = critterEditorEMInit;
	gCritterEditor.enter_editor_func = critterEditorEMEnter;
	gCritterEditor.exit_func = critterEditorEMExit;
	gCritterEditor.lost_focus_func = critterEditorEMLostFocus;
	gCritterEditor.new_func = critterEditorEMNewDoc;
	gCritterEditor.load_func = critterEditorEMLoadDoc;
	gCritterEditor.save_func = critterEditorEMSaveDoc;
	gCritterEditor.sub_save_func = critterEditorEMSaveSubDoc;
	gCritterEditor.close_func = critterEditorEMCloseDoc;
	gCritterEditor.sub_close_func = critterEditorEMCloseSubDoc;
	gCritterEditor.sub_reload_func = critterEditorEMReloadSubDoc;

	gCritterEditor.keybinds_name = "MultiEditor";

	// register picker
	gCritterPicker.allow_outsource = 1;
	strcpy(gCritterPicker.picker_name, "Critter Library");
	strcpy(gCritterPicker.default_type, gCritterEditor.default_type);
	emPickerManage(&gCritterPicker);
	eaPush(&gCritterEditor.pickers, &gCritterPicker);

	emRegisterEditor(&gCritterEditor);
	emRegisterFileType(gCritterEditor.default_type, "Critter", gCritterEditor.editor_name);
#endif

	return 1;
}

