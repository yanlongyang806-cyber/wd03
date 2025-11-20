//
// MessageStoreEditorEM.c
//

#ifndef NO_EDITORS

#include "error.h"
#include "file.h"
#include "MultiEditWindow.h"
#include "MultiEditTable.h"
#include "Message.h"
#include "MessageStoreEditor.h"



// This magic line is required if you run in Full Debug to avoid assert
AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););


//---------------------------------------------------------------------------------------------------
// Global Data
//---------------------------------------------------------------------------------------------------

EMEditor gMessageStoreEditor = {0};

EMPicker gMessageStorePicker = {0};

static MultiEditEMDoc *gMessageStoreEditorDoc = NULL;


//---------------------------------------------------------------------------------------------------
// Editor
//---------------------------------------------------------------------------------------------------


static EMEditorDoc *messageStoreEditorEMNewDoc(const char *pcType, void *data)
{
	if (!gConf.bServerSaving) {
		Alertf("This editor only operates when server saving is enabled");
		return NULL;
	}
	messageStoreEditor_createMessage(NULL);

	return (EMEditorDoc*)gMessageStoreEditorDoc;
}


static EMEditorDoc *messageStoreEditorEMLoadDoc(const char *pcName, const char *pcType)
{
	if (!gConf.bServerSaving) {
		Alertf("This editor only operates when server saving is enabled");
		return NULL;
	}
	MEWindowOpenObject(gMessageStoreEditorDoc->pWindow, (char*)pcName);

	return (EMEditorDoc*)gMessageStoreEditorDoc;
}


static EMTaskStatus messageStoreEditorEMSaveDoc(EMEditorDoc *pDoc)
{
	return MEWindowSaveAll(gMessageStoreEditorDoc->pWindow);
}


static EMTaskStatus messageStoreEditorEMSaveSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	return MEWindowSaveObject(gMessageStoreEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void messageStoreEditorEMCloseDoc(EMEditorDoc *pDoc)
{
	MEWindowExit(gMessageStoreEditorDoc->pWindow);
}


static void messageStoreEditorEMCloseSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	MEWindowCloseObject(gMessageStoreEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void messageStoreEditorEMReloadSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	MEWindowRevertObject(gMessageStoreEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void messageStoreEditorEMEnter(EMEditor *pEditor)
{
	resSetDictionaryEditMode(gMessageDict, true);
}

static void messageStoreEditorEMExit(EMEditor *pEditor)
{

}

static void messageStoreEditorEMLostFocus(EMEditorDoc *pDoc)
{
	MEWindowLostFocus(gMessageStoreEditorDoc->pWindow);
}

static void messageStoreEditorEMInit(EMEditor *pEditor)
{
	// Create the one global document
	if (!gMessageStoreEditorDoc) {
		// Create the global document
		gMessageStoreEditorDoc = calloc(1,sizeof(MultiEditEMDoc));
		gMessageStoreEditorDoc->emDoc.editor = &gMessageStoreEditor;
		gMessageStoreEditorDoc->emDoc.saved = true;
		gMessageStoreEditorDoc->pWindow = messageStoreEditor_init(gMessageStoreEditorDoc);
		sprintf(gMessageStoreEditorDoc->emDoc.doc_name, "Message Store Editor");
		sprintf(gMessageStoreEditorDoc->emDoc.doc_display_name, "Message Store Editor");
		gMessageStoreEditorDoc->emDoc.primary_ui_window = MEWindowGetUIWindow(gMessageStoreEditorDoc->pWindow);
	}
}

#endif

AUTO_RUN_LATE;
int messageStoreEditorEMRegister(void)
{
#ifndef NO_EDITORS
	if (!areEditorsAllowed())
		return 0;

	// Register the editor
	strcpy(gMessageStoreEditor.editor_name, "Message Store Editor");
	gMessageStoreEditor.type = EM_TYPE_MULTIDOC;
	gMessageStoreEditor.hide_world = 1;
	gMessageStoreEditor.disable_single_doc_menus = 1;
	gMessageStoreEditor.disable_auto_checkout = 1;
	gMessageStoreEditor.default_type = "Message";
	strcpy(gMessageStoreEditor.default_workspace, "Message Store Editor");

	gMessageStoreEditor.init_func = messageStoreEditorEMInit;
	gMessageStoreEditor.enter_editor_func = messageStoreEditorEMEnter;
	gMessageStoreEditor.exit_func = messageStoreEditorEMExit;
	gMessageStoreEditor.lost_focus_func = messageStoreEditorEMLostFocus;
	gMessageStoreEditor.new_func = messageStoreEditorEMNewDoc;
	gMessageStoreEditor.load_func = messageStoreEditorEMLoadDoc;
	gMessageStoreEditor.save_func = messageStoreEditorEMSaveDoc;
	gMessageStoreEditor.sub_save_func = messageStoreEditorEMSaveSubDoc;
	gMessageStoreEditor.close_func = messageStoreEditorEMCloseDoc;
	gMessageStoreEditor.sub_close_func = messageStoreEditorEMCloseSubDoc;
	gMessageStoreEditor.sub_reload_func = messageStoreEditorEMReloadSubDoc;

	// Register the picker
	gMessageStorePicker.allow_outsource = 1;
	strcpy(gMessageStorePicker.picker_name, "Message Library");
	strcpy(gMessageStorePicker.default_type, gMessageStoreEditor.default_type );
	emPickerManage(&gMessageStorePicker);
	eaPush(&gMessageStoreEditor.pickers, &gMessageStorePicker);

	emRegisterEditor(&gMessageStoreEditor);
	emRegisterFileType(gMessageStoreEditor.default_type, "Message", gMessageStoreEditor.editor_name);
#endif

	return 0;
}
