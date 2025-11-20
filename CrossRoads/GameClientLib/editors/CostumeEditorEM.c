//
// CostumeEditorEM.c
//

#ifndef NO_EDITORS

#include "CostumeCommonLoad.h"
#include "CostumeEditor.h"
#include "CostumeDefEditor.h"
#include "error.h"
#include "file.h"

#include "AutoGen/CostumeCommonEnums_h_ast.h"
#include "AutoGen/CostumeCommon_h_ast.h"

// Magic code required by budgets
AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););


//-----------------------------------------------------------------------------------
// Global Data
//-----------------------------------------------------------------------------------

static EMEditor gCostumeEditor;

static EMPicker gCostumePicker;

// Really defined in CostumeEditor.c
extern CmdList CostumeEditorCmdList;


//---------------------------------------------------------------------------------------------------
// General Access
//---------------------------------------------------------------------------------------------------



EMEditor *costumeEditorEMGetEditor(void)
{
	return &gCostumeEditor;
}


void costumeEditorEMGetAllOpenDocs(CostumeEditDoc ***peaDocs)
{
	int i;

	for(i=eaSize(&gCostumeEditor.open_docs)-1; i>=0; --i) {
		eaPush(peaDocs, (CostumeEditDoc*)gCostumeEditor.open_docs[i]);
	}
}


bool costumeEditorEMIsDocOpen(char *pcName)
{
	int i;

	for(i=eaSize(&gCostumeEditor.open_docs)-1; i>=0; --i) {
		if (stricmp(pcName, gCostumeEditor.open_docs[i]->doc_name) == 0) {
			return true;
		}
	}
	return false;
}


CostumeEditDoc *costumeEditorEMGetOpenDoc(const char *pcName)
{
	int i;

	for(i=eaSize(&gCostumeEditor.open_docs)-1; i>=0; --i) {
		if (stricmp(pcName, gCostumeEditor.open_docs[i]->doc_name) == 0) {
			return (CostumeEditDoc*)gCostumeEditor.open_docs[i];
		}
	}
	return NULL;
}


//---------------------------------------------------------------------------------------------------
// Editor
//---------------------------------------------------------------------------------------------------

extern bool gCEColorLinkAll;
extern bool gCEMatLinkAll;

static EMEditorDoc *costumeEditorEMLoadDoc(const char *pcName, const char *pcType)
{
	CostumeEditDoc *pDoc;

	if (!gConf.bServerSaving) {
		Alertf("This editor only operates when server saving is enabled");
		return NULL;
	}

	// Set up the graphics
	costumeView_SetCamera(gCostumeEditor.camera);
	gCostumeEditor.camera->pan_speed = 0.1f;
	costumeView_InitGraphics();

	// Open or create the costume
	if (stricmp(pcType, "_costume") == 0) {
		pDoc = costumeEdit_OpenCloneOfCostume(&gCostumeEditor, "__AUTO_CLONE__");
	} else {
		pDoc = costumeEdit_OpenCostume(&gCostumeEditor, (char*)pcName);
	}
	if (!pDoc) {
		return NULL;
	}

	if (!pcName)
	{
		pDoc->pCostume->eDefaultColorLinkAll = gCEColorLinkAll;
		pDoc->pCostume->eDefaultMaterialLinkAll = gCEMatLinkAll;
	}

	pcName = pDoc->pCostume->pcName;

	// Set up the document
	strcpy(pDoc->emDoc.doc_name, pcName);
	strcpy(pDoc->emDoc.doc_type, "PlayerCostume");
	strcpy(pDoc->emDoc.doc_display_name, pDoc->emDoc.doc_name);

	return &pDoc->emDoc;
}


static EMEditorDoc *costumeEditorEMNewDoc(const char *pcType, void *data)
{
	return costumeEditorEMLoadDoc(NULL, pcType);
}


static void costumeEditorEMCloseDoc(EMEditorDoc *pDoc)
{
	costumeEdit_CloseCostume((CostumeEditDoc*)pDoc);

	SAFE_FREE(pDoc);
}


static EMTaskStatus costumeEditorEMSave(EMEditorDoc *pDoc)
{
	return costumeEdit_SaveCostume((CostumeEditDoc*)pDoc, false);
}


static EMTaskStatus costumeEditorEMSaveAs(EMEditorDoc *pDoc)
{
	return costumeEdit_SaveCostume((CostumeEditDoc*)pDoc, true);
}


static EMTaskStatus costumeEditorEMSaveDef(EMEditorDoc *pDoc, EMEditorSubDoc *pSubDoc)
{
	return costumeDefEdit_SaveDef((CostumeEditDoc*)pDoc, (CostumeEditDefDoc*)pSubDoc);
}


static void costumeEditorEMDraw(EMEditorDoc *pDoc)
{
	costumeEdit_Draw((CostumeEditDoc*)pDoc);
}


static void costumeEditorEMDrawGhosts(EMEditorDoc *pDoc)
{
	costumeEdit_DrawGhosts((CostumeEditDoc*)pDoc);
}


static void costumeEditorEMGotFocus(EMEditorDoc *pDoc)
{
	costumeEdit_GotFocus((CostumeEditDoc*)pDoc);
}


static void costumeEditorEMLostFocus(EMEditorDoc *pDoc)
{
	costumeEdit_LostFocus((CostumeEditDoc*)pDoc);
}


static void costumeEditorEMEnter(EMEditor *pEditor)
{
	resSetDictionaryEditMode(g_hCostumeTextureDict, true);
	resSetDictionaryEditMode(g_hCostumeMaterialDict, true);
	resSetDictionaryEditMode(g_hCostumeMaterialAddDict, true);
	resSetDictionaryEditMode(g_hCostumeGeometryDict, true);
	resSetDictionaryEditMode(g_hCostumeGeometryAddDict, true);
	resSetDictionaryEditMode(g_hPlayerCostumeDict, true);
	resSetDictionaryEditMode(gMessageDict, true);

	// Force all assets to be present for editor choices
	resRequestAllResourcesInDictionary(g_hCostumeSkeletonDict);
	resRequestAllResourcesInDictionary(g_hCostumeBoneDict);
	resRequestAllResourcesInDictionary(g_hCostumeRegionDict);
	resRequestAllResourcesInDictionary(g_hCostumeCategoryDict);
	resRequestAllResourcesInDictionary(g_hCostumeGeometryDict);
	resRequestAllResourcesInDictionary(g_hCostumeGeometryAddDict);
	resRequestAllResourcesInDictionary(g_hCostumeMaterialDict);
	resRequestAllResourcesInDictionary(g_hCostumeMaterialAddDict);
	resRequestAllResourcesInDictionary(g_hCostumeTextureDict);
	resRequestAllResourcesInDictionary(g_hCostumeLayerDict);
}

static void costumeEditorEMExit(EMEditor *pEditor)
{

}

static void costumeEditorEMInit(EMEditor *pEditor)
{
	// Set up the data
	costumeEdit_InitData(pEditor);
}

#endif

AUTO_RUN_LATE;
int costumeEditorEMRegister(void)
{
#ifndef NO_EDITORS
	if (!areEditorsAllowed())
		return 0;

	// Register the editor
	strcpy(gCostumeEditor.editor_name, "Costume Editor");
	gCostumeEditor.type = EM_TYPE_SINGLEDOC;
	gCostumeEditor.hide_world = 1;
	gCostumeEditor.region_type = WRT_CharacterCreator;
	gCostumeEditor.allow_save = 1;
	gCostumeEditor.allow_multiple_docs = 1;
	gCostumeEditor.disable_auto_checkout = 1;
	//gCostumeEditor.use_em_cam_keybinds = 1; // DON'T USE EM CAMERA!
	gCostumeEditor.default_type = "PlayerCostume";

	gCostumeEditor.init_func = costumeEditorEMInit;
	gCostumeEditor.enter_editor_func = costumeEditorEMEnter;
	gCostumeEditor.exit_func = costumeEditorEMExit;
	gCostumeEditor.new_func = costumeEditorEMNewDoc;
	gCostumeEditor.load_func = costumeEditorEMLoadDoc;
	gCostumeEditor.close_func = costumeEditorEMCloseDoc;
	gCostumeEditor.save_func = costumeEditorEMSave;
	gCostumeEditor.save_as_func = costumeEditorEMSaveAs;
	gCostumeEditor.sub_save_func = costumeEditorEMSaveDef;
	gCostumeEditor.draw_func = costumeEditorEMDraw;
	gCostumeEditor.ghost_draw_func = costumeEditorEMDrawGhosts;
	gCostumeEditor.lost_focus_func = costumeEditorEMLostFocus;
	gCostumeEditor.got_focus_func = costumeEditorEMGotFocus;

	gCostumeEditor.keybinds_name = "CostumeEditor";
	gCostumeEditor.keybind_version = 3;

	// Register the picker
	gCostumePicker.allow_outsource = 1;
	strcpy(gCostumePicker.picker_name, "Costume Library");
	strcpy(gCostumePicker.default_type, gCostumeEditor.default_type);
	emPickerManage(&gCostumePicker);
	emPickerRegister(&gCostumePicker);
	eaPush(&gCostumeEditor.pickers, &gCostumePicker);
	emRegisterEditor(&gCostumeEditor);

	emRegisterFileType(gCostumeEditor.default_type, "Costume", gCostumeEditor.editor_name);
	emRegisterFileType("_costume", NULL, gCostumeEditor.editor_name);
#endif

	return 0;
}

