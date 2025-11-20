// Sky File Editor 2
// Sky Editor 2 EM Setup & Registration

#include "SkyEditor2.h"
#include "EditorManager.h"
#include "GfxEditorIncludes.h"
#include "GraphicsLib.h"
#include "Color.h"
#include "File.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

//-----------------------------------------------------------------------------------
//                                Global Data
//-----------------------------------------------------------------------------------

static EMEditor gSkyEditor;
static EMPicker gSkyFilePicker;
static ResourceGroup s_SkyPickerTree;
static U16 gNumNewSkies = 0;
static bool bPickerRefreshRequested = false;

//---------------------------------------------------------------------------------------------------
//                          Editor Callback Definitions
//---------------------------------------------------------------------------------------------------

static EMEditorDoc *skyEditorEMLoadDoc(const char *pcName, const char *pcType)
{
	SkyEditorDoc *pDoc;

	// Open or create the costume
	pDoc = skyEdNewDoc(pcName);
	if (!pDoc) {
		return NULL;
	}

	// Set up the document
	strcpy(pDoc->emDoc.doc_name, pDoc->pSkyInfo->filename_no_path);
	strcpy(pDoc->emDoc.doc_type, GFX_SKY_DICTIONARY);
	strcpy(pDoc->emDoc.doc_display_name, pDoc->emDoc.doc_name);

	return &pDoc->emDoc;
}

static void skyEditorEMReloadDoc(EMEditorDoc *pDoc)
{
	skyEdReloadSky((SkyEditorDoc*)pDoc);
}

static EMEditorDoc *skyEditorEMNewDoc(const char *pcType, void *data)
{
	return skyEditorEMLoadDoc(NULL, pcType);
}

static void skyEditorEMCloseDoc(EMEditorDoc *pDoc)
{
	skyEdCloseSky((SkyEditorDoc*)pDoc);
	SAFE_FREE(pDoc);
}


static EMTaskStatus skyEditorEMSave(EMEditorDoc *pDoc)
{
	return skyEdSaveSky((SkyEditorDoc *)pDoc, false);
}


static EMTaskStatus skyEditorEMSaveAs(EMEditorDoc *pDoc)
{
	return skyEdSaveAsSky((SkyEditorDoc *)pDoc);
}


static void skyEditorEMDraw(EMEditorDoc *pDoc)
{
	skyEdDraw((SkyEditorDoc*)pDoc);
}

static void skyEditorEMDrawGhosts(EMEditorDoc *pDoc)
{
	skyEdDrawGhosts((SkyEditorDoc*)pDoc);
}

static void skyEditorEMGotFocus(EMEditorDoc *pDoc)
{
	skyEdGotFocus((SkyEditorDoc*)pDoc);
}

static void skyEditorEMEnterEditor(EMEditor *pEditor)
{
	resSetDictionaryEditMode(GFX_SKY_DICTIONARY, true);
}

static void skyEditorEMLostFocus(EMEditorDoc *pDoc)
{
	skyEdLostFocus((SkyEditorDoc*)pDoc);
}


static void skyEditorEMInit(EMEditor *pEditor)
{
	skyEdInit(pEditor);
}

//-----------------------------------------------------------------------------------
//                          Registering Editor
//-----------------------------------------------------------------------------------

AUTO_RUN_LATE;
void skyEditor2Register(void)
{
	if (!areEditorsAllowed())
		return;
	
	// Editor Setup
	strcpy(gSkyEditor.editor_name, "Sky Editor");
	gSkyEditor.type = EM_TYPE_SINGLEDOC;
	gSkyEditor.hide_world = 0;
	gSkyEditor.hide_info_window = 1;
	gSkyEditor.allow_save = 1;
	gSkyEditor.allow_multiple_docs = 1;
	gSkyEditor.disable_auto_checkout = 1;
	gSkyEditor.default_type = GFX_SKY_DICTIONARY;

	// Registering Callbacks
	gSkyEditor.init_func = skyEditorEMInit;
	gSkyEditor.new_func = skyEditorEMNewDoc;
	gSkyEditor.load_func = skyEditorEMLoadDoc;
	gSkyEditor.reload_func = skyEditorEMReloadDoc;
	gSkyEditor.close_func = skyEditorEMCloseDoc;
	gSkyEditor.save_func = skyEditorEMSave;
	gSkyEditor.save_as_func = skyEditorEMSaveAs;
	gSkyEditor.draw_func = skyEditorEMDraw;
	gSkyEditor.ghost_draw_func = skyEditorEMDrawGhosts;
	gSkyEditor.lost_focus_func = skyEditorEMLostFocus;
	gSkyEditor.got_focus_func = skyEditorEMGotFocus;
	gSkyEditor.enter_editor_func = skyEditorEMEnterEditor;

	// Registering Keybinds
	gSkyEditor.keybinds_name = "skyEditor2";
	gSkyEditor.keybind_version = 2;

	// Register the picker
	gSkyFilePicker.allow_outsource = 0;
	strcpy(gSkyFilePicker.picker_name, "Sky");
	strcpy(gSkyFilePicker.default_type, gSkyEditor.default_type);
	emPickerManage(&gSkyFilePicker);
	eaPush(&gSkyEditor.pickers, &gSkyFilePicker);

	// Registering Editor
	emRegisterEditor(&gSkyEditor);
	emRegisterFileType(gSkyEditor.default_type, "Sky", gSkyEditor.editor_name);

	return;
}