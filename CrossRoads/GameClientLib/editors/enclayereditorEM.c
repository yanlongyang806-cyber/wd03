/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
 
#ifndef NO_EDITORS

#include "file.h"
#include "enclayereditor.h"
#include "oldencounter_common.h"
#include "StringCache.h"


#include "oldencounter_common_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

extern EMEditor s_EncounterLayerEditor;

static ELEWorldEditorInterfaceEM s_WEInterface = {0};

static EMMenuItemDef ELEMenuItems[] = 
{
	{"group", "Group", NULL, NULL, "MissionEditor.group"},
	{"delete", "Delete", NULL, NULL, "MissionEditor.delete"},
	{"setteamsize", "Set Team Size Override", NULL, NULL, "ELE.SetTeamSize"},
	{"freeze", "Freeze Selected", NULL, NULL, "MissionEditor.Freeze"},
	{"unfreeze", "Unfreeze All", NULL, NULL, "MissionEditor.Unfreeze"},
	{"aggroradius", "Show/Hide Aggro Radius", NULL, NULL, "ELE.ToggleAggroRadius"},
	{"encounterheight", "Check Encounter Height", NULL, NULL, "ELE.CheckEncounterHeight"},
	{"saveencasdef", "Save Selected Encounter as Def", NULL, NULL, "ELE.SaveEncAsDef"},
	{"layercut", "Cut From Layer", NULL, NULL, "ELE.LayerCut"},
	{"layerpaste", "Paste To Layer", NULL, NULL, "ELE.LayerPaste"},
//	{"logviewer", "Open Log Viewer", NULL, NULL, "ELE.OpenLogViewer"},	// Log viewer works but isn't very useful
};

static void ELEPlaceInEncounterLayerEM(EMEditorDoc* editorDoc, const char* name, const char* type)
{
	EncounterLayerEditDoc* encLayerDoc = (EncounterLayerEditDoc*) editorDoc;
	ELEPlaceInEncounterLayer(encLayerDoc, name, type);
}

static bool ELESaveCallback(EMEditor *editor, const char *name, EncounterLayerEditDoc *editDoc, EMResourceState state, void *callback_data, bool success)
{
	if (success && (state == EMRES_STATE_SAVE_SUCCEEDED))
	{
		EncounterLayer *encLayer = editDoc->layerDef;
		oldencounter_SafeLayerCopyAll(encLayer, editDoc->origLayerDef);
		editDoc->emDoc.saved = true;
	}
	return true;
}

static void ELERefreshDoc(EncounterLayerEditDoc* encLayerDoc)
{
	// This can crash in certain weird cases, but deselecting everything is too annoying
	// TODO - Validate all the selected objects
	//eaDestroyEx(&encLayerDoc->selectedObjects, GESelectedObjectDestroyCB);

	ELERefreshStaticEncounters(encLayerDoc, true);
	ELERefreshUI(encLayerDoc);

	// Clear the undo/redo queue
	EditUndoStackClear(encLayerDoc->emDoc.edit_undo_stack);
	if (encLayerDoc->previousState)
		StructDestroy(parse_EncounterLayer, encLayerDoc->previousState);
	encLayerDoc->previousState = oldencounter_SafeCloneLayer(encLayerDoc->layerDef);
}

static void ELELoadToMasterLayerEM(const char *pchLayerName)
{
	EncounterLayer *layer = RefSystem_ReferentFromString(g_EncounterLayerDictionary, pchLayerName);
	int i, n;

	if (!layer)
		return;

	// Check whether the encounter layer is open in the editor first.
	n = eaSize(&s_EncounterLayerEditor.open_docs);
	for (i = 0; i < n; i++)
	{
		EncounterLayerEditDoc *pDoc = (EncounterLayerEditDoc*)s_EncounterLayerEditor.open_docs[i];
		devassert(pchLayerName && pDoc->layerDef->name);
		if (0 == stricmp(pDoc->layerDef->name, pchLayerName))
		{
			if (layer->bNeedsRefresh)
			{
				EncounterLayer *layerCopy = oldencounter_SafeCloneLayer(layer);
				langMakeEditorCopy(parse_EncounterLayer, layerCopy, false);
				
				if (StructCompare(parse_EncounterLayer, layerCopy, pDoc->layerDef, 0, 0, 0)
					&& StructCompare(parse_EncounterLayer, layerCopy, pDoc->origLayerDef, 0, 0, 0))
				{
					if (pDoc->emDoc.saved)
					{
						// Reload latest version of the layer
						oldencounter_LoadToMasterLayer(layer);
						StructDestroy(parse_EncounterLayer, pDoc->origLayerDef);
						pDoc->origLayerDef = oldencounter_SafeCloneLayer(pDoc->layerDef);
						langMakeEditorCopy(parse_EncounterLayer, pDoc->layerDef, false);
						langMakeEditorCopy(parse_EncounterLayer, pDoc->origLayerDef, false);
						ELERefreshDoc(pDoc);
					}
					else
					{
						Alertf("File '%s' changed on disk, but is open in the editor.  Close and reopen editor to get changes.  DO NOT SAVE or you will overwrite any changes on disk!", layer->name);
					}
				}
				layer->bNeedsRefresh = false;
				StructDestroy(parse_EncounterLayer, layerCopy);
			}
		}
	}

	// Layer was not open in the editor.
	// Load (or reload) the encounter layer into the master layer
	if (layer->bNeedsRefresh && oldencounter_MatchesMasterLayer(layer, g_EncounterMasterLayer))
	{
		oldencounter_LoadToMasterLayer(layer);
	}

	// Refresh the World Editor interface
	ELERefreshWorldEditorInterfaceEM();
}

static void ELEDictChanged(enumResourceEventType eType, const char *pDictName, ConstReferenceData pRefData, Referent pReferent, EMEditor *pEditor)
{
	const char *pcName = (const char *)pRefData;
	bool openInEditor = false;

	if (!pcName) {
		return;
	}

	if (eType == RESEVENT_RESOURCE_ADDED || eType == RESEVENT_RESOURCE_MODIFIED)
	{
		EncounterLayer *newLayer = (EncounterLayer*)pReferent;
		if (!newLayer->bNeedsRefresh)
		{
			newLayer->bNeedsRefresh = true;
			emQueueFunctionCall(ELELoadToMasterLayerEM, (char*)allocAddString(newLayer->name));
		}
	}
}

static void ELEMessageDictChanged(enumResourceEventType eType, const char *pDictName, ConstReferenceData pRefData, Referent pReferent, EMEditor *pEditor)
{
	int i, n;
	// Queue all EncounterLayer Docs to be refreshed
	if (gConf.bServerSaving && eType == RESEVENT_RESOURCE_MODIFIED)
	{
		// Carefully handle reloading the def if it's open in the editor
		n = eaSize(&s_EncounterLayerEditor.open_docs);
		for (i = 0; i < n; i++)
		{
			EncounterLayerEditDoc *doc = (EncounterLayerEditDoc*)s_EncounterLayerEditor.open_docs[i];
			EncounterLayer *pDictLayer = RefSystem_ReferentFromString(g_EncounterLayerDictionary, doc->layerDef->name);
			if (pDictLayer && !pDictLayer->bNeedsRefresh)
			{
				pDictLayer->bNeedsRefresh = true;
				emQueueFunctionCall(ELELoadToMasterLayerEM, (char*)allocAddString(pDictLayer->name));
			}
		}
	}
}

static void ELEInitEM(EMEditor *editor)
{
	emMenuItemCreateFromTable(editor, ELEMenuItems, ARRAY_SIZE_CHECKED(ELEMenuItems));
	emMenuRegister(editor, emMenuCreate(editor, "Layer", "setteamsize", "startspawn", NULL));
	emMenuRegister(editor, emMenuCreate(editor, "Editing", "delete", "group", "freeze", "unfreeze", "aggroradius", "encounterheight", "saveencasdef", "layercut", "layerpaste", "logviewer", NULL));

	GEEditorInitExpressionsEM(editor);
}

void ELERefreshWorldEditorInterfaceEM(void)
{
	ELESetupWorldEditorInterface(&s_WEInterface, false);
}

void ELESetupUIEM(EncounterLayerEditDoc* encLayerDoc)
{
	EncounterLayerEditorUI* uiInfo = &encLayerDoc->uiInfo;
	ELESetupUI(encLayerDoc);

	// Add all the windows to the document
	eaPush(&encLayerDoc->emDoc.ui_windows, uiInfo->createToolbar);
	eaPush(&encLayerDoc->emDoc.ui_windows, uiInfo->propertiesWindow);
	eaPush(&encLayerDoc->emDoc.ui_windows, uiInfo->layerTreeWindow);
	eaPush(&encLayerDoc->emDoc.ui_windows, uiInfo->mouseoverInfoWindow);

	if(uiInfo->eventEditorWindow)
		eaPush(&encLayerDoc->emDoc.ui_windows, uiInfo->eventEditorWindow);
	if(uiInfo->invalidSpawnWindow)
		eaPush(&encLayerDoc->emDoc.ui_windows, uiInfo->invalidSpawnWindow);
}

void ELEDestroyUIEM(EncounterLayerEditDoc* encLayerDoc)
{
	EMEditorDoc* emDoc = &encLayerDoc->emDoc;

	// Don't destroy the event editor window
	if(encLayerDoc->uiInfo.eventEditorWindow)
		eaFindAndRemove(&emDoc->ui_windows, encLayerDoc->uiInfo.eventEditorWindow);

	eaDestroyEx(&emDoc->ui_windows, ui_WidgetQueueFree);

	// Free all modal windows
	ui_WidgetQueueFree(UI_WIDGET(encLayerDoc->uiInfo.renameWindow));
	ui_WidgetQueueFree(UI_WIDGET(encLayerDoc->uiInfo.groupPropWindow));
}

static EMEditorDoc* ELENewEncLayerEM(const char* type, const char* unused);

static EMEditorDoc* ELELoadEncLayerEM(const char* name, const char* type)
{
	const char* dispName;
	const char* lastSlash;
	EncounterLayer* layerDef = NULL;
	EncounterLayer *dictLayer = NULL;
	EncounterLayerEditDoc* encLayerDoc = NULL;
	char layerName[MAX_PATH];
	layerName[0] = '\0';

	devassert(0 == stricmp(type, "encounterlayer"));

	if (name)
	{
		fileRelativePath(name, layerName);
		if (!strEndsWith(layerName, ".encounterlayer"))
			strcat(layerName, ".encounterlayer");
	}

	layerDef = oldencounter_FindSubLayer(g_EncounterMasterLayer, layerName);

	if (layerDef)
	{
		// Make sure the version in the master layer is up-to-date
		dictLayer = RefSystem_ReferentFromString(g_EncounterLayerDictionary, layerDef->name);
		if (dictLayer && oldencounter_MatchesMasterLayer(dictLayer, g_EncounterMasterLayer))
			oldencounter_LoadToMasterLayer(dictLayer);
	}

	if (layerDef)
	{
		encLayerDoc = calloc(1, sizeof(EncounterLayerEditDoc));
		ELEOpenEncounterLayer(layerDef, encLayerDoc);

		emDocAssocFile(&encLayerDoc->emDoc, layerDef->pchFilename);
		lastSlash = strrchr(layerDef->pchFilename, '/');
		dispName = (lastSlash && lastSlash[0]) ? (lastSlash + 1) : layerName;
		strcpy(encLayerDoc->emDoc.doc_display_name, dispName);

		// Set up the undo/redo queue
		encLayerDoc->emDoc.edit_undo_stack = EditUndoStackCreate();
		EditUndoSetContext(encLayerDoc->emDoc.edit_undo_stack, encLayerDoc);
		encLayerDoc->previousState = oldencounter_SafeCloneLayer(layerDef);

		GECheckoutDocEM(&encLayerDoc->emDoc);

		// Create the UI from the contact def
		ELESetupUIEM(encLayerDoc);
		return &encLayerDoc->emDoc;
	}
	else if (layerName[0] && g_EncounterMasterLayer)
	{
		/*  All layers should be loaded as soon as the editor is open for drawing.
		 *  So, the only way this can happen is if this is a new map and the user 
		 *  clicked on the default layer, or if the layer hasn't loaded yet.
		*/
		char *dictName = ELELayerNameFromFilename(layerName);

		if (!resGetInfo("EncounterLayer", dictName)){
			free(dictName);
			return ELENewEncLayerEM(type, NULL);
		}
		free(dictName);
	}
	else
	{
		Alertf("Encounter system got unloaded; try to open your layer again in a second or two.");
		oldencounter_LoadLayers(NULL);
		return NULL;
	}

	return NULL;
}
static EMEditorDoc* ELENewEncLayerEM(const char* type, const char* unused)
{
	EncounterLayer* layer;
	EncounterLayerEditDoc* encLayerDoc = calloc(1, sizeof(EncounterLayerEditDoc));
	const char* dispName;
	const char* lastSlash;

	devassert(0 == stricmp(type, "encounterlayer"));

	layer = ELENewEncounterLayer(NULL, encLayerDoc);
	if (!layer)
	{
		free(encLayerDoc);
		return NULL;
	}

	emDocAssocFile(&encLayerDoc->emDoc, layer->pchFilename);
	lastSlash = strrchr(layer->pchFilename, '/');
	dispName = (lastSlash && lastSlash[0]) ? (lastSlash + 1) : layer->pchFilename;
	strcpy(encLayerDoc->emDoc.doc_display_name, dispName);

	// The doc starts "saved"
	encLayerDoc->emDoc.saved = true;

	// Set up the undo/redo queue
	encLayerDoc->emDoc.edit_undo_stack = EditUndoStackCreate();
	EditUndoSetContext(encLayerDoc->emDoc.edit_undo_stack, encLayerDoc);
	encLayerDoc->previousState = oldencounter_SafeCloneLayer(layer);

	GECheckoutDocEM(&encLayerDoc->emDoc);

	// Create the UI from the contact def
	ELESetupUIEM(encLayerDoc);

	return &encLayerDoc->emDoc;
}

static void ELECloseEncLayerEM(EMEditorDoc* editorDoc)
{
	EncounterLayerEditDoc* encLayerDoc = (EncounterLayerEditDoc*) editorDoc;

	ELECloseEncounterLayer(encLayerDoc);

	ELEDestroyUIEM(encLayerDoc);
	EditUndoStackDestroy(editorDoc->edit_undo_stack);
	StructDestroy(parse_EncounterLayer, encLayerDoc->previousState);
	free(editorDoc);

	ELERefreshWorldEditorInterfaceEM();
}

void ELESetDocFileEM(EncounterLayerEditDoc* encLayerDoc, const char* filename)
{
	emDocAssocFile(&encLayerDoc->emDoc, filename);
}

static EMTaskStatus ELESaveEncounterLayerEM(EMEditorDoc* editorDoc)
{
	EncounterLayerEditDoc* encLayerDoc = (EncounterLayerEditDoc*) editorDoc;
	EMTaskStatus status = ELESaveEncounterLayer(encLayerDoc);

	if(status == EM_TASK_SUCCEEDED)
	{
		editorDoc->saved = true;
	}

	return status;
}

static EMTaskStatus ELEAutoSaveBackupEM(EMEditorDoc* editorDoc)
{
	char* backupFilename = NULL;
	EncounterLayerEditDoc* encLayerDoc = (EncounterLayerEditDoc*) editorDoc;
	EncounterLayer* layerDef = encLayerDoc->layerDef;
	bool saved;
	estrStackCreate(&backupFilename);
	estrPrintf(&backupFilename, "%s.autosave.bak", layerDef->pchFilename);
	saved = ele_SaveEncounterLayer(layerDef, backupFilename);
	estrDestroy(&backupFilename);

	if(saved)
		return EM_TASK_SUCCEEDED;
	else
		return EM_TASK_FAILED;
}

static void ELEProcessInputEM(EMEditorDoc* editorDoc)
{
	EncounterLayerEditDoc* encLayerDoc = (EncounterLayerEditDoc*) editorDoc;
	ELEProcessInput(encLayerDoc);
}
static void ELEDrawEncounterLayerEM(EMEditorDoc* editorDoc)
{
	EncounterLayerEditDoc* encLayerDoc = (EncounterLayerEditDoc*) editorDoc;
	ELEDrawActiveLayer(encLayerDoc);
}
// JAMES TODO: have get/lost focus create/destroy tracker tree?
static void ELEGotFocusEM(EMEditorDoc* editorDoc)
{
	EncounterLayerEditDoc* encLayerDoc = (EncounterLayerEditDoc*) editorDoc;
	ELEGotFocus(encLayerDoc);
}

static bool ELEPlaceEncounterFromPickerEM(EMPicker* encounterPicker, EMPickerSelection** selections, void* unused)
{
	if(selections)
		ELEPlaceInEncounterLayerEM(GEGetActiveEditorDocEM("encounterlayer"), selections[0]->doc_name, selections[0]->doc_type);

	return true;
}

void ELEPlaceEncounterEM(void)
{
	emPickerShow(&s_EncounterPicker, "Place", false, ELEPlaceEncounterFromPickerEM, NULL);
}

static void ELEEnterEM(EMEditor *pEditor)
{
//	resSetDictionaryEditMode(g_EncounterLayerDictionary, true);
//	resSetDictionaryEditMode(gMessageDict, true);	
}

static void ELEExitEM(EMEditor *pEditor)
{

}

static void ELEEditorEntryCallback(void* unused)
{
	// Load all EncounterLayers	
	resSetDictionaryEditMode(g_EncounterLayerDictionary, true);
	resSetDictionaryEditMode(g_EncounterDictionary, true);
	resSetDictionaryEditMode("FSM", true);
	resSetDictionaryEditMode(gMessageDict, true);
	resSetDictionaryEditMode("CritterGroup", true);
	resSetDictionaryEditMode("CritterDef", true);
	resSetDictionaryEditMode("CritterFaction", true);
	emAddDictionaryStateChangeHandler(&s_EncounterLayerEditor, "encounterlayer", NULL, NULL, ELESaveCallback, NULL, NULL);
	resDictRegisterEventCallback(g_EncounterLayerDictionary, ELEDictChanged, &s_EncounterLayerEditor);
	resDictRegisterEventCallback(gMessageDict, ELEMessageDictChanged, &s_EncounterLayerEditor);
	oldencounter_LoadLayers(NULL);
	ELESetupWorldEditorInterface(&s_WEInterface, false);
	GELoadDisplayDefs(NULL, false);
}

static void ELEMapChangeCallback(void* unused, bool bMapReset)
{
	if (!emIsEditorActive()) {
		// Queue function to refresh client-side EncounterLayer data
		// once the editors are entered
		emAddEditorEntryCallback(ELEEditorEntryCallback, NULL);
	} else {
		// Reload the layer now!
		ELEEditorEntryCallback(NULL);
	}
}

#endif // NO_EDITORS

AUTO_RUN;
int EncounterLayerEditorRegisterEditor(void)
{
#ifndef NO_EDITORS
	if (!areEditorsAllowed() || !gConf.bAllowOldEncounterData)
		return 0;

	// Setup and register the encounter layer editor
	strcpy(s_EncounterLayerEditor.editor_name, "Encounter Layer Editor");
	s_EncounterLayerEditor.autosave_interval = 10;
	s_EncounterLayerEditor.allow_multiple_docs = 1;
	s_EncounterLayerEditor.allow_save = 1;
	s_EncounterLayerEditor.hide_world = 0;
	s_EncounterLayerEditor.requires_server_data = 1;

	s_EncounterLayerEditor.type = EM_TYPE_SINGLEDOC;

	s_EncounterLayerEditor.init_func = ELEInitEM;
	s_EncounterLayerEditor.new_func = ELENewEncLayerEM;
	s_EncounterLayerEditor.load_func = ELELoadEncLayerEM;
	s_EncounterLayerEditor.close_func = ELECloseEncLayerEM;
	s_EncounterLayerEditor.enter_editor_func = ELEEnterEM;
	s_EncounterLayerEditor.exit_func = ELEExitEM;

	s_EncounterLayerEditor.save_func = ELESaveEncounterLayerEM;
	s_EncounterLayerEditor.autosave_func = ELEAutoSaveBackupEM;
	s_EncounterLayerEditor.draw_func = ELEProcessInputEM;
	s_EncounterLayerEditor.ghost_draw_func = ELEDrawEncounterLayerEM;
	s_EncounterLayerEditor.object_dropped_func = ELEPlaceInEncounterLayerEM;
	s_EncounterLayerEditor.got_focus_func = ELEGotFocusEM;

	s_EncounterLayerEditor.default_type = "encounterlayer";
	strcpy(s_EncounterLayerEditor.default_workspace, "Environment Editors");
	s_EncounterLayerEditor.keybinds_name = "MissionEditor";

	emRegisterEditor(&s_EncounterLayerEditor);
	emRegisterFileType("encounterlayer", "Encounter Layer", "Encounter Layer Editor");

	emAddMapChangeCallback(ELEMapChangeCallback, NULL);
#endif

	return 1;
}
