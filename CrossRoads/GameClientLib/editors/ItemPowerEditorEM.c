//
// ItemEditorEM.c
//

#ifndef NO_EDITORS

#include "error.h"
#include "file.h"
#include "itemCommon.h"
#include "ItemPowerEditor.h"
#include "MultiEditTable.h"
#include "MultiEditWindow.h"
#include "PowersEditor.h"


// Magic code required by budgets
AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

//-----------------------------------------------------------------------------------
// Global Data
//-----------------------------------------------------------------------------------


EMEditor gItemPowerEditor;

EMPicker gItemPowerPicker;

static MultiEditEMDoc *gItemPowerEditorDoc = NULL;


//---------------------------------------------------------------------------------------------------
// Editor
//---------------------------------------------------------------------------------------------------



void itemPowerEditorEMNewDocFromPowerDef(const char *pcType, PowerDef* pPowerDef, char* pchName, const char* pScope)
{
	newItemPowerData newData;
	if (!gConf.bServerSaving) {
		Alertf("This editor only operates when server saving is enabled");
		return;
	}
	newData.pchName = pchName;
	newData.pPowerDef = pPowerDef;
	newData.pScope = pScope;
	emNewDoc("ItemPowerDef", &newData);
}

static EMEditorDoc *itemPowerEditorEMNewDoc(const char *pcType, void *data)
{
	if (!gConf.bServerSaving) {
		Alertf("This editor only operates when server saving is enabled");
		return NULL;
	}

	if (data)
	{
		newItemPowerData* pItemPowerData = (newItemPowerData*)data;
		itemPowerEditor_createItemPowerFromPowerDef(pItemPowerData->pchName, pItemPowerData->pPowerDef, pItemPowerData->pScope);
	}
	else
		itemPowerEditor_createItemPower(NULL);

	return (EMEditorDoc*)gItemPowerEditorDoc;
}

static EMEditorDoc *itemPowerEditorEMLoadDoc(const char *pcName, const char *pcType)
{
	if (!gConf.bServerSaving) {
		Alertf("This editor only operates when server saving is enabled");
		return NULL;
	}
	MEWindowOpenObject(gItemPowerEditorDoc->pWindow, (char*)pcName);

	return (EMEditorDoc*)gItemPowerEditorDoc;
}


static EMTaskStatus itemPowerEditorEMSaveDoc(EMEditorDoc *pDoc)
{
	return MEWindowSaveAll(gItemPowerEditorDoc->pWindow);
}


static EMTaskStatus itemPowerEditorEMSaveSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	return MEWindowSaveObject(gItemPowerEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void itemPowerEditorEMCloseDoc(EMEditorDoc *pDoc)
{
	MEWindowExit(gItemPowerEditorDoc->pWindow);
}


static void itemPowerEditorEMCloseSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	MEWindowCloseObject(gItemPowerEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void itemPowerEditorEMReloadSubDoc(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	MEWindowRevertObject(gItemPowerEditorDoc->pWindow, ((MultiEditEMSubDoc*)pSubDoc)->pObject);
}


static void itemPowerEditorEMEnter(EMEditor *pEditor)
{

}

static void itemPowerEditorEMExit(EMEditor *pEditor)
{

}

static void itemPowerEditorEMLostFocus(EMEditorDoc *pDoc)
{
	MEWindowLostFocus(gItemPowerEditorDoc->pWindow);
}

static void itemPowerEditorEMInit(EMEditor *pEditor)
{
	// Create the one global document
	if (!gItemPowerEditorDoc) {
		// Create the global document
		gItemPowerEditorDoc = calloc(1,sizeof(MultiEditEMDoc));
		gItemPowerEditorDoc->emDoc.editor = &gItemPowerEditor;
		gItemPowerEditorDoc->emDoc.saved = true;
		gItemPowerEditorDoc->pWindow = itemPowerEditor_init(gItemPowerEditorDoc);
		sprintf(gItemPowerEditorDoc->emDoc.doc_name, "Item Power Editor");
		sprintf(gItemPowerEditorDoc->emDoc.doc_display_name, "Item Power Editor");
		gItemPowerEditorDoc->emDoc.primary_ui_window = MEWindowGetUIWindow(gItemPowerEditorDoc->pWindow);
	}
}

#endif

AUTO_RUN_LATE;
int itemPowerEditorEMRegister(void)
{
#ifndef NO_EDITORS
	if (!areEditorsAllowed())
		return 0;

	strcpy(gItemPowerEditor.editor_name, "Item Power Editor");
	gItemPowerEditor.type = EM_TYPE_MULTIDOC;
	gItemPowerEditor.hide_world = 1;
	gItemPowerEditor.disable_single_doc_menus = 1;
	gItemPowerEditor.disable_auto_checkout = 1;
	gItemPowerEditor.default_type = "ItemPowerDef";
	strcpy(gItemPowerEditor.default_workspace, "Game Design Editors");

	gItemPowerEditor.init_func = itemPowerEditorEMInit;
	gItemPowerEditor.enter_editor_func = itemPowerEditorEMEnter;
	gItemPowerEditor.exit_func = itemPowerEditorEMExit;
	gItemPowerEditor.lost_focus_func = itemPowerEditorEMLostFocus;
	gItemPowerEditor.new_func = itemPowerEditorEMNewDoc;
	gItemPowerEditor.load_func = itemPowerEditorEMLoadDoc;
	gItemPowerEditor.save_func = itemPowerEditorEMSaveDoc;
	gItemPowerEditor.sub_save_func = itemPowerEditorEMSaveSubDoc;
	gItemPowerEditor.close_func = itemPowerEditorEMCloseDoc;
	gItemPowerEditor.sub_close_func = itemPowerEditorEMCloseSubDoc;
	gItemPowerEditor.sub_reload_func = itemPowerEditorEMReloadSubDoc;

	// Register the picker
	gItemPowerPicker.allow_outsource = 1;
	strcpy(gItemPowerPicker.picker_name, "Item Power Library");
	strcpy(gItemPowerPicker.default_type, gItemPowerEditor.default_type);
	emPickerManage(&gItemPowerPicker);
	eaPush(&gItemPowerEditor.pickers, &gItemPowerPicker);

	emRegisterEditor(&gItemPowerEditor);
	emRegisterFileType(gItemPowerEditor.default_type, "Item Power", gItemPowerEditor.editor_name);
#endif

	return 0;
}

