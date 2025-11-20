//
// DonationTaskEditorEM.c
//

#ifndef NO_EDITORS

#include "error.h"
#include "file.h"
#include "MultiEditTable.h"
#include "MultiEditWindow.h"
#include "DonationTaskEditor.h"


// This magic line is required if you run in Full Debug to avoid assert
AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

EMEditor gDonationTaskEditor;

EMPicker gDonationTaskPicker;

static MultiEditEMDoc *gDonationTaskEditorDoc = NULL;


//---------------------------------------------------------------------------------------------------
// Editor
//---------------------------------------------------------------------------------------------------


static EMEditorDoc *donationTaskEditorEMNewDoc(const char *pcType, void *data)
{
	if (!gConf.bServerSaving) {
		Alertf("This editor only operates when server saving is enabled");
		return NULL;
	}
	donationTaskEditor_createDonationTask(NULL);

	return (EMEditorDoc*)gDonationTaskEditorDoc;
}


static EMEditorDoc *donationTaskEditorEMLoadDoc(const char *pcName, const char *pcType)
{
	if (!gConf.bServerSaving) {
		Alertf("This editor only operates when server saving is enabled");
		return NULL;
	}
	MEWindowOpenObject(gDonationTaskEditorDoc->pWindow, (char*)pcName);

	return (EMEditorDoc*)gDonationTaskEditorDoc;
}


static EMTaskStatus donationTaskEditorEMSaveDoc(EMEditorDoc *pDoc)
{
	return MEWindowSaveAll(gDonationTaskEditorDoc->pWindow);
}


static EMTaskStatus donationTaskEditorEMSaveSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	return MEWindowSaveObject(gDonationTaskEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void donationTaskEditorEMCloseDoc(EMEditorDoc *pDoc)
{
	MEWindowExit(gDonationTaskEditorDoc->pWindow);
}


static void donationTaskEditorEMCloseSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	MEWindowCloseObject(gDonationTaskEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void donationTaskEditorEMReloadSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	MEWindowRevertObject(gDonationTaskEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void donationTaskEditorEMEnter(EMEditor *pEditor)
{

}

static void donationTaskEditorEMExit(EMEditor *pEditor)
{

}

static void donationTaskEditorEMLostFocus(EMEditorDoc *pDoc)
{
	MEWindowLostFocus(gDonationTaskEditorDoc->pWindow);
}

static void donationTaskEditorEMInit(EMEditor *pEditor)
{
	// Create the one global document
	if (!gDonationTaskEditorDoc) {
		// Create the global document
		gDonationTaskEditorDoc = calloc(1,sizeof(MultiEditEMDoc));
		gDonationTaskEditorDoc->emDoc.editor = &gDonationTaskEditor;
		gDonationTaskEditorDoc->emDoc.saved = true;
		gDonationTaskEditorDoc->pWindow = donationTaskEditor_init(gDonationTaskEditorDoc);
		sprintf(gDonationTaskEditorDoc->emDoc.doc_name, "Donation Task Editor");
		sprintf(gDonationTaskEditorDoc->emDoc.doc_display_name, "Donation Task Editor");
		gDonationTaskEditorDoc->emDoc.primary_ui_window = MEWindowGetUIWindow(gDonationTaskEditorDoc->pWindow);
	}
}

#endif

AUTO_RUN_LATE;
int donationTaskEditorEMRegister(void)
{
#ifndef NO_EDITORS
	if (!areEditorsAllowed())
		return 0;

	// register editor
	strcpy(gDonationTaskEditor.editor_name, "Donation Task Editor");
	gDonationTaskEditor.type = EM_TYPE_MULTIDOC;
	gDonationTaskEditor.hide_world = 1;
	gDonationTaskEditor.disable_single_doc_menus = 1;
	gDonationTaskEditor.disable_auto_checkout = 1;
	gDonationTaskEditor.default_type = "DonationTaskDef";
	strcpy(gDonationTaskEditor.default_workspace, "Game Design Editors");

	gDonationTaskEditor.init_func = donationTaskEditorEMInit;
	gDonationTaskEditor.enter_editor_func = donationTaskEditorEMEnter;
	gDonationTaskEditor.exit_func = donationTaskEditorEMExit;
	gDonationTaskEditor.lost_focus_func = donationTaskEditorEMLostFocus;
	gDonationTaskEditor.new_func = donationTaskEditorEMNewDoc;
	gDonationTaskEditor.load_func = donationTaskEditorEMLoadDoc;
	gDonationTaskEditor.save_func = donationTaskEditorEMSaveDoc;
	gDonationTaskEditor.sub_save_func = donationTaskEditorEMSaveSubDoc;
	gDonationTaskEditor.close_func = donationTaskEditorEMCloseDoc;
	gDonationTaskEditor.sub_close_func = donationTaskEditorEMCloseSubDoc;
	gDonationTaskEditor.sub_reload_func = donationTaskEditorEMReloadSubDoc;

	gDonationTaskEditor.keybinds_name = "MultiEditor";

	// register picker
	gDonationTaskPicker.allow_outsource = 1;
	strcpy(gDonationTaskPicker.picker_name, "DonationTask Library");
	strcpy(gDonationTaskPicker.default_type, gDonationTaskEditor.default_type);
	emPickerManage(&gDonationTaskPicker);
	eaPush(&gDonationTaskEditor.pickers, &gDonationTaskPicker);

	emRegisterEditor(&gDonationTaskEditor);
	emRegisterFileType(gDonationTaskEditor.default_type, "DonationTask", gDonationTaskEditor.editor_name);
#endif

	return 1;
}

