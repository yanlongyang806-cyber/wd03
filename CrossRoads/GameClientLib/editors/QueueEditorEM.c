//
// QueueEditorEM.c
//

#ifndef NO_EDITORS

#include "error.h"
#include "file.h"
#include "gameeditorshared.h"
#include "MultiEditTable.h"
#include "MultiEditWindow.h"
#include "QueueEditor.h"

#include "queue_common.h"

// This magic line is required if you run in Full Debug to avoid assert
AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

EMEditor gQueueEditor;

EMPicker gQueuePicker;

static MultiEditEMDoc *gQueueEditorDoc = NULL;


//---------------------------------------------------------------------------------------------------
// Editor
//---------------------------------------------------------------------------------------------------


static EMEditorDoc *queueEditorEMNewDoc(const char *pcType, void *data)
{
	if (!gConf.bServerSaving) {
		Alertf("This editor only operates when server saving is enabled");
		return NULL;
	}
	queueEditor_createQueue(NULL);

	return (EMEditorDoc*)gQueueEditorDoc;
}


static EMEditorDoc *queueEditorEMLoadDoc(const char *pcName, const char *pcType)
{
	if (!gConf.bServerSaving) {
		Alertf("This editor only operates when server saving is enabled");
		return NULL;
	}
	MEWindowOpenObject(gQueueEditorDoc->pWindow, (char*)pcName);

	return (EMEditorDoc*)gQueueEditorDoc;
}


static EMTaskStatus queueEditorEMSaveDoc(EMEditorDoc *pDoc)
{
	return MEWindowSaveAll(gQueueEditorDoc->pWindow);
}


static EMTaskStatus queueEditorEMSaveSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	return MEWindowSaveObject(gQueueEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void queueEditorEMCloseDoc(EMEditorDoc *pDoc)
{
	MEWindowExit(gQueueEditorDoc->pWindow);
}


static void queueEditorEMCloseSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	MEWindowCloseObject(gQueueEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void queueEditorEMReloadSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	MEWindowRevertObject(gQueueEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void queueEditorEMEnter(EMEditor *pEditor)
{
	GERefreshMapNamesList();
}

static void queueEditorEMExit(EMEditor *pEditor)
{

}

static void queueEditorEMLostFocus(EMEditorDoc *pDoc)
{
	MEWindowLostFocus(gQueueEditorDoc->pWindow);
}

static void queueEditorEMInit(EMEditor *pEditor)
{
	// Create the one global document
	if (!gQueueEditorDoc) {
		// Create the global document
		gQueueEditorDoc = calloc(1,sizeof(MultiEditEMDoc));
		gQueueEditorDoc->emDoc.editor = &gQueueEditor;
		gQueueEditorDoc->emDoc.saved = true;
		gQueueEditorDoc->pWindow = queueEditor_init(gQueueEditorDoc);
		sprintf(gQueueEditorDoc->emDoc.doc_name, "Queue Editor");
		sprintf(gQueueEditorDoc->emDoc.doc_display_name, "Queue Editor");
		gQueueEditorDoc->emDoc.primary_ui_window = MEWindowGetUIWindow(gQueueEditorDoc->pWindow);
	}
}

#endif

AUTO_RUN_LATE;
int queueEditorEMRegister(void)
{
#ifndef NO_EDITORS
	if (!areEditorsAllowed())
		return 0;

	// register editor
	strcpy(gQueueEditor.editor_name, "Queue Editor");
	gQueueEditor.type = EM_TYPE_MULTIDOC;
	gQueueEditor.hide_world = 1;
	gQueueEditor.disable_single_doc_menus = 1;
	gQueueEditor.disable_auto_checkout = 1;
	gQueueEditor.default_type = "QueueDef";
	gQueueEditor.allow_multiple_docs = 1;
	strcpy(gQueueEditor.default_workspace, "Game Design Editors");

	gQueueEditor.init_func = queueEditorEMInit;
	gQueueEditor.enter_editor_func = queueEditorEMEnter;
	gQueueEditor.exit_func = queueEditorEMExit;
	gQueueEditor.lost_focus_func = queueEditorEMLostFocus;
	gQueueEditor.new_func = queueEditorEMNewDoc;
	gQueueEditor.load_func = queueEditorEMLoadDoc;
	gQueueEditor.save_func = queueEditorEMSaveDoc;
	gQueueEditor.sub_save_func = queueEditorEMSaveSubDoc;
	gQueueEditor.close_func = queueEditorEMCloseDoc;
	gQueueEditor.sub_close_func = queueEditorEMCloseSubDoc;
	gQueueEditor.sub_reload_func = queueEditorEMReloadSubDoc;

	gQueueEditor.keybinds_name = "MultiEditor";

	// register picker
	gQueuePicker.allow_outsource = 1;
	strcpy(gQueuePicker.picker_name, "Queue Library");
	strcpy(gQueuePicker.default_type, gQueueEditor.default_type);
	emPickerManage(&gQueuePicker);
	eaPush(&gQueueEditor.pickers, &gQueuePicker);

	emRegisterEditor(&gQueueEditor);
	emRegisterFileType(gQueueEditor.default_type, "Queue", gQueueEditor.editor_name);
#endif

	return 1;
}

