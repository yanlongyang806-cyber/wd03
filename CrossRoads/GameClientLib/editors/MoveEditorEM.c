#ifndef NO_EDITORS

#include "MoveEditor.h"
#include "CostumeCommonLoad.h"
#include "dynMove.h"
#include "error.h"
#include "file.h"
#include "GameClientLib.h"
#include "dynAnimGraph.h"

#include "AutoGen/CostumeCommonEnums_h_ast.h"
#include "AutoGen/CostumeCommon_h_ast.h"

// Magic code required by budgets
AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););


//-----------------------------------------------------------------------------------
// Global Data
//-----------------------------------------------------------------------------------

static EMEditor gMoveEdit;

static EMPicker gSequencePicker;

// Really defined in MoveEdit.c
extern CmdList MoveEditCmdList;

static ResourceGroup movePickerTree;
static bool movePickerRefreshRequested = false;


//---------------------------------------------------------------------------------------------------
// General Access
//---------------------------------------------------------------------------------------------------



EMEditor *MoveEditEMGetEditor(void) {
	return &gMoveEdit;
}


void MoveEditEMGetAllOpenDocs(MoveEditDoc ***peaDocs) {
	int i;

	for(i=eaSize(&gMoveEdit.open_docs)-1; i>=0; --i) {
		eaPush(peaDocs, (MoveEditDoc*)gMoveEdit.open_docs[i]);
	}
}


bool MoveEditorEMIsDocOpen(char *pcName) {
	int i;

	for(i=eaSize(&gMoveEdit.open_docs)-1; i>=0; --i) {
		if (stricmp(pcName, gMoveEdit.open_docs[i]->doc_name) == 0) {
			return true;
		}
	}
	return false;
}


MoveEditDoc *MoveEditEMGetOpenDoc(const char *pcName) {
	int i;

	for(i=eaSize(&gMoveEdit.open_docs)-1; i>=0; --i) {
		if (stricmp(pcName, gMoveEdit.open_docs[i]->doc_name) == 0) {
			return (MoveEditDoc*)gMoveEdit.open_docs[i];
		}
	}
	return NULL;
}


//---------------------------------------------------------------------------------------------------
// Editor
//---------------------------------------------------------------------------------------------------

extern bool gCEColorLinkAll;
extern bool gCEMatLinkAll;
static S32 newMoveCounter = 0;

bool MoveEditorEM_RegisterNewMove(const char *pName) {
	DynMove *pMove;

	pMove = StructClone(parse_DynMove,RefSystem_ReferentFromString(DYNMOVE_DICTNAME, "Core_Defaultanim"));
	if (pMove) {
		pMove->pcName = pName;
		pMove->pcFilename = NULL;
		eaPush(&pMove->eaDynMoveSeqs, StructCreate(parse_DynMoveSeq));
		//Adding the new sky's data to the dictionary
		RefSystem_AddReferent(DYNMOVE_DICTNAME, pName, pMove);

		return true;
	}
	return false;
}

static EMEditorDoc *MoveEditorEMLoadDoc(const char *pcName, const char *pcType) {
	MoveEditDoc *newDoc = calloc(1, sizeof(MoveEditDoc));
	DynMove *moveDef;
	char moveName[255];

	// Set up the graphics
	costumeView_SetCamera(gMoveEdit.camera);
	costumeView_InitGraphics();

	if (pcName) {
		strcpy(moveName,pcName);
		strcpy(newDoc->moveName,moveName);
	} else {
		newMoveCounter++;
		sprintf(moveName,"NewMove_%02d",newMoveCounter);
		newDoc->bNewMove = true;
		strcpy(newDoc->moveName,moveName);
		MoveEditorEM_RegisterNewMove(newDoc->moveName);
	}
	sprintf(newDoc->emDoc.doc_name, "%s", moveName);
	sprintf(newDoc->emDoc.doc_display_name, "%s", moveName);
	newDoc->emDoc.name_changed = 1;

	moveDef = RefSystem_ReferentFromString(DYNMOVE_DICTNAME, moveName);
	assert(moveDef);
	newDoc->pMove = StructClone(parse_DynMove, moveDef);
	assert(newDoc->pMove);
	newDoc->pOrigFileAssociation = newDoc->pMove->pcFilename;
	if (pcName)
		emDocAssocFile(&newDoc->emDoc, newDoc->pMove->pcFilename);
	newDoc->pOrigRefName = newDoc->pMove->pcName;

	// Set up the undo/redo queue
	newDoc->emDoc.edit_undo_stack = EditUndoStackCreate();
	EditUndoSetContext(newDoc->emDoc.edit_undo_stack, newDoc);

	newDoc->loop = newDoc->animate = true;

	// document and UI initialization...
	MoveEditor_CreateUI(newDoc);

	return &newDoc->emDoc;
}


static EMEditorDoc *MoveEditorEMNewDoc(const char *pcType, void *data) {
	return MoveEditorEMLoadDoc(NULL, pcType);
}


static void MoveEditorEMCloseDoc(EMEditorDoc *pDoc) {
	MoveEditor_CloseMove((MoveEditDoc*)pDoc);

	SAFE_FREE(pDoc);
}


static EMTaskStatus MoveEditorEMSave(EMEditorDoc *pDoc) {

	Alertf("Warning: Save disabled in Move Editor to prevent overwriting hand edited .move files!");
	/*
	DISABLED TO PREVENT OVERWRITING HAND EDITED FILES
	return MoveEditor_SaveMove((MoveEditDoc*)pDoc, false);
	*/
	return EM_TASK_FAILED;
}


static void MoveEditorEMDraw(EMEditorDoc *pDoc) {
	dynAnimCheckDataReload();
	MoveEditor_Draw((MoveEditDoc*)pDoc);
}


static void MoveEditorEMDrawGhosts(EMEditorDoc *pDoc) {
	MoveEditor_DrawGhosts((MoveEditDoc*)pDoc);
}


static void MoveEditorEMGotFocus(EMEditorDoc *pDoc) {
	MoveEditor_GotFocus((MoveEditDoc*)pDoc);
}


static void MoveEditorEMLostFocus(EMEditorDoc *pDoc) {
	MoveEditor_LostFocus((MoveEditDoc*)pDoc);
}


static void MoveEditEMEnter(EMEditor *pEditor) {
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

static void MoveEditorEMExit(EMEditor *pEditor) {
}

static void MoveEditorEMInit(EMEditor *pEditor) {
	// Set up the data
	MoveEditor_InitData(pEditor);
}

//-----------------------------------------------------------------------------------
//                          Picker Functions
//-----------------------------------------------------------------------------------

static void MoveEditorEMRefreshPicker(void *data)
{
	resBuildGroupTree(DYNMOVE_DICTNAME, &movePickerTree);
	emPickerRefresh(&gSequencePicker);
	movePickerRefreshRequested = false;
}


static void MoveEditorEMPickerReferenceCallback(enumResourceEventType eType, const char *pDictName, const void *pData2, void *pData1, void *pUserData)
{
	if (!movePickerRefreshRequested) {	
		movePickerRefreshRequested = true;
		emQueueFunctionCall(MoveEditorEMRefreshPicker, NULL);
	}
}

static bool MoveEditorEMPickerSelected(EMPicker *pPicker, EMPickerSelection *pSelection)
{
	assert(pSelection->table == parse_ResourceInfo);

	sprintf(pSelection->doc_name, "%s", ((ResourceInfo*)pSelection->data)->resourceName);
	strcpy(pSelection->doc_type, DYNMOVE_TYPENAME);

	return true;
}

//---------------------------------------------------------------------------

#endif

AUTO_RUN_LATE;
int MoveEditorEMRegister(void) {
#ifndef NO_EDITORS
	if (!areEditorsAllowed())
		return 0;

#ifndef USE_NEW_MOVE_EDITOR
	// Register the editor
	strcpy(gMoveEdit.editor_name, "Animation Move Editor");
	gMoveEdit.type = EM_TYPE_SINGLEDOC;
	gMoveEdit.hide_world = 1;
	gMoveEdit.region_type = WRT_CharacterCreator;
	gMoveEdit.allow_save = 1;
	gMoveEdit.allow_multiple_docs = 1;
	gMoveEdit.disable_auto_checkout = 1;
	gMoveEdit.default_type = DYNMOVE_TYPENAME;
	strcpy(gMoveEdit.default_workspace, "Animation Editors");

	gMoveEdit.init_func = MoveEditorEMInit;
	gMoveEdit.enter_editor_func = MoveEditEMEnter;
	gMoveEdit.exit_func = MoveEditorEMExit;
	gMoveEdit.new_func = MoveEditorEMNewDoc;
	gMoveEdit.load_func = MoveEditorEMLoadDoc;
	gMoveEdit.close_func = MoveEditorEMCloseDoc;
	gMoveEdit.save_func = MoveEditorEMSave;
	gMoveEdit.save_as_func = NULL;
	gMoveEdit.draw_func = MoveEditorEMDraw;
	gMoveEdit.ghost_draw_func = MoveEditorEMDrawGhosts;
	gMoveEdit.lost_focus_func = MoveEditorEMLostFocus;
	gMoveEdit.got_focus_func = MoveEditorEMGotFocus;

	gMoveEdit.keybinds_name = "MoveEditor";
	gMoveEdit.keybind_version = 5;

	// Register the move picker
	gSequencePicker.allow_outsource = 1;
	strcpy(gSequencePicker.picker_name, "Move Library");
	strcpy(gSequencePicker.default_type, gMoveEdit.default_type);
	emPickerManage(&gSequencePicker);
	eaPush(&gMoveEdit.pickers, &gSequencePicker);
	emPickerRegister(&gSequencePicker);

	emRegisterEditor(&gMoveEdit);

	emRegisterFileType(gMoveEdit.default_type, "Move", gMoveEdit.editor_name);
#endif
	
#endif

	return 0;
}