//
// PowersEditorEM.c
//

#ifndef NO_EDITORS

#include "error.h"
#include "file.h"
#include "MultiEditWindow.h"
#include "MultiEditTable.h"
#include "Powers.h"
#include "PowersEditor.h"

// This magic line is required if you run in Full Debug to avoid assert
AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););


//---------------------------------------------------------------------------------------------------
// Global Data
//---------------------------------------------------------------------------------------------------

EMEditor gPowersEditor = {0};

EMPicker gPowersPicker = {0};

static MultiEditEMDoc *gPowersEditorDoc = NULL;


//---------------------------------------------------------------------------------------------------
// Editor
//---------------------------------------------------------------------------------------------------

static void powersEditorEMCopy(EMEditorDoc *doc, bool cut)
{
	//cut ignored, can't cut powers

	pe_CopyToClipboard();
}

static EMEditorDoc *powersEditorEMNewDoc(const char *pcType, void *data)
{
	if (!gConf.bServerSaving) {
		Alertf("This editor only operates when server saving is enabled");
		return NULL;
	}
	powersEditor_createPower(NULL);

	return (EMEditorDoc*)gPowersEditorDoc;
}


static EMEditorDoc *powersEditorEMLoadDoc(const char *pcName, const char *pcType)
{
	if (!gConf.bServerSaving) {
		Alertf("This editor only operates when server saving is enabled");
		return NULL;
	}
	MEWindowOpenObject(gPowersEditorDoc->pWindow, (char*)pcName);

	return (EMEditorDoc*)gPowersEditorDoc;
}


static EMTaskStatus powersEditorEMSaveDoc(EMEditorDoc *pDoc)
{
	return MEWindowSaveAll(gPowersEditorDoc->pWindow);
}


static EMTaskStatus powersEditorEMSaveSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	return MEWindowSaveObject(gPowersEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void powersEditorEMCloseDoc(EMEditorDoc *pDoc)
{
	MEWindowExit(gPowersEditorDoc->pWindow);
}


static void powersEditorEMCloseSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	MEWindowCloseObject(gPowersEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}

static void powersEditorEMReloadSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	MEWindowRevertObject(gPowersEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}

static void powersEditorEMEnter(EMEditor *pEditor)
{

}

static void powersEditorEMExit(EMEditor *pEditor)
{

}

static void powersEditorEMLostFocus(EMEditorDoc *pDoc)
{
	MEWindowLostFocus(gPowersEditorDoc->pWindow);
}

static void powersEditorEMInit(EMEditor *pEditor)
{
	// Create the one global document
	if (!gPowersEditorDoc) {
		// Create the global document
		gPowersEditorDoc = calloc(1,sizeof(MultiEditEMDoc));
		gPowersEditorDoc->emDoc.editor = &gPowersEditor;
		gPowersEditorDoc->emDoc.saved = true;
		gPowersEditorDoc->pWindow = powersEditor_init(gPowersEditorDoc);
		sprintf(gPowersEditorDoc->emDoc.doc_name, "Powers Editor");
		sprintf(gPowersEditorDoc->emDoc.doc_display_name, "Powers Editor");
		gPowersEditorDoc->emDoc.primary_ui_window = MEWindowGetUIWindow(gPowersEditorDoc->pWindow);

		// These weird lines are here to force this dictionary to actually have values in it
		resDictSetMaxUnreferencedResources("DynFxInfo", RES_DICT_KEEP_ALL);
		resRequestAllResourcesInDictionary("DynFxInfo");
	}
}

#endif

AUTO_RUN_LATE;
int powersEditorEMRegister(void)
{
#ifndef NO_EDITORS
	if (!areEditorsAllowed())
		return 0;

	// Register the editor
	strcpy(gPowersEditor.editor_name, "Powers Editor");
	gPowersEditor.type = EM_TYPE_MULTIDOC;
	gPowersEditor.hide_world = 1;
	gPowersEditor.disable_single_doc_menus = 1;
	gPowersEditor.disable_auto_checkout = 1;
	gPowersEditor.default_type = "PowerDef";
	strcpy(gPowersEditor.default_workspace, "Game Design Editors");

	gPowersEditor.init_func = powersEditorEMInit;
	gPowersEditor.enter_editor_func = powersEditorEMEnter;
	gPowersEditor.exit_func = powersEditorEMExit;
	gPowersEditor.lost_focus_func = powersEditorEMLostFocus;
	gPowersEditor.new_func = powersEditorEMNewDoc;
	gPowersEditor.load_func = powersEditorEMLoadDoc;
	gPowersEditor.save_func = powersEditorEMSaveDoc;
	gPowersEditor.sub_save_func = powersEditorEMSaveSubDoc;
	gPowersEditor.close_func = powersEditorEMCloseDoc;
	gPowersEditor.sub_close_func = powersEditorEMCloseSubDoc;
	gPowersEditor.sub_reload_func = powersEditorEMReloadSubDoc;
	gPowersEditor.copy_func = powersEditorEMCopy;

	// Register the picker
	gPowersPicker.allow_outsource = 1;
	strcpy(gPowersPicker.picker_name, "Powers Library");
	strcpy(gPowersPicker.default_type, gPowersEditor.default_type);
	emPickerManage(&gPowersPicker);
	eaPush(&gPowersEditor.pickers, &gPowersPicker);

	emRegisterEditor(&gPowersEditor);
	emRegisterFileType(gPowersEditor.default_type, "Power", gPowersEditor.editor_name);
#endif

	return 0;
}
