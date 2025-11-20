/***************************************************************************
*     Copyright (c) 2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef NO_EDITORS

#include "encountereditor.h"
#include "file.h"
#include "EditorPrefs.h"
#include "oldencounter_common.h"

#include "AutoGen/oldencounter_common_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

// ----------------------------------------------------------------------------------
// Static Data
// ----------------------------------------------------------------------------------


static EMEditor s_EncounterEditor = {0};
EMPicker s_EncounterPicker = {0};

static EMMenuItemDef EEMenuItems[] = 
{
	{"placecritter", "Place Random Critter", NULL, NULL, "EncounterEditor.PlaceRandomCritter"},
};


// ----------------------------------------------------------------------------------
// Editor Manager Editor
// ----------------------------------------------------------------------------------

static bool EDESaveCallbackEM(EMEditor *editor, const char *name, EncounterEditDoc *editDoc, EMResourceState state, void *callback_data, bool success)
{
	if (success && (state == EMRES_STATE_SAVE_SUCCEEDED))
	{
		EncounterDef *encDef = editDoc->def;
		editDoc->newAndNeverSaved = 0;
		StructCopyAll(parse_EncounterDef, encDef, editDoc->origDef);
		editDoc->emDoc.saved = true;
	}
	return true;
}

static void EDEDocRefreshEM(EncounterEditDoc *pDoc)
{
	if (!pDoc)
		return;
	
	pDoc->bNeedsRefresh = false;

	if (pDoc->def)
	{
		EncounterDef *def = RefSystem_ReferentFromString(g_EncounterDictionary, pDoc->def->name);
		if (!def && pDoc->origDef)
			def = RefSystem_ReferentFromString(g_EncounterDictionary, pDoc->origDef->name);
		if (def)
		{
			EncounterDef *defCopy = StructClone(parse_EncounterDef, def);
			langMakeEditorCopy(parse_EncounterDef, defCopy, false);
			
			if (StructCompare(parse_EncounterDef, defCopy, pDoc->origDef, 0, 0, 0)
				&& StructCompare(parse_EncounterDef, defCopy, pDoc->def, 0, 0, 0))
			{
				if (pDoc->emDoc.saved)
				{
					// Reload latest version of the def
					emCloseDoc(&pDoc->emDoc);
					emOpenFileEx(def->name, "EncounterDef");
				}
				else
				{
					Alertf("File '%s' changed on disk, but is open in the editor.  Close and reopen editor to get changes.  DO NOT SAVE or you will overwrite any changes on disk!", def->name);
				}
			}

			StructDestroy(parse_EncounterDef, defCopy);
		}
		else if (!pDoc->newAndNeverSaved)
		{
			Errorf("Error: File '%s' tried to refresh, but it doesn't exist in the dictionary!  Please get Ben F or Stephen.", pDoc->def->name);
		}
	}
}



static void EDEDictChanged(enumResourceEventType eType, const char *pDictName, ConstReferenceData pRefData, Referent pReferent, EMEditor *pEditor)
{
	const char *pcName = (const char *)pRefData;

	if (!pcName) {
		return;
	}	

	// If an existing object was modified, it needs to be refreshed in the editor.
	if (gConf.bServerSaving && eType == RESEVENT_RESOURCE_MODIFIED)
	{
		EncounterDef *def = (EncounterDef*)pReferent;
		int i, n;

		// Queue Mission to be reloaded if the def is open in the editor
		n = eaSize(&s_EncounterEditor.open_docs);
		for (i = 0; i < n; i++)
		{
			EncounterEditDoc *doc = (EncounterEditDoc*)s_EncounterEditor.open_docs[i];
			devassert(doc->def->name);
			if (0 == stricmp(doc->def->name, def->name) && !doc->bNeedsRefresh)
			{
				doc->bNeedsRefresh = true;
				emQueueFunctionCall(EDEDocRefreshEM, doc);
			}
		}
	}

}

static void EDEMessageDictChanged(enumResourceEventType eType, const char *pDictName, ConstReferenceData pRefData, Referent pReferent, EMEditor *pEditor)
{
	int i, n;
	// Queue all EncounterDef Docs to be refreshed
	if (gConf.bServerSaving && eType == RESEVENT_RESOURCE_MODIFIED)
	{
		// Carefully handle reloading the def if it's open in the editor
		n = eaSize(&s_EncounterEditor.open_docs);
		for (i = 0; i < n; i++)
		{
			EncounterEditDoc *doc = (EncounterEditDoc*)s_EncounterEditor.open_docs[i];
			if (!doc->bNeedsRefresh)
			{
				doc->bNeedsRefresh = true;
				emQueueFunctionCall(EDEDocRefreshEM, doc);
			}
		}
	}
}

void EDENameChangedEM(const char* newName, EncounterEditDoc* encDoc)
{
	// Update the asset manager and ui related fields
	strcpy(encDoc->emDoc.doc_name, newName);
	strcpy(encDoc->emDoc.doc_display_name, newName);

	// If the doc didn't have a filename before, name it
	//if (encDoc->newAndNeverSaved)
	{
		resFixPooledFilename(&encDoc->def->filename, "defs/encounters", encDoc->def->scope, encDoc->def->name, "encounter");
		GESetDocFileEM(&encDoc->emDoc, encDoc->def->filename);
	}

	GESetCurrentDocUnsaved();
	encDoc->emDoc.name_changed = 1;
}

static void EDEInitEM(EMEditor *editor)
{
	emMenuItemCreateFromTable(editor, EEMenuItems, ARRAY_SIZE_CHECKED(EEMenuItems));
	emMenuRegister(editor, emMenuCreate(editor, "Place", "placecritter", NULL));

	emAddDictionaryStateChangeHandler(editor, "EncounterDef", NULL, NULL, EDESaveCallbackEM, NULL, NULL);
	resDictRegisterEventCallback(g_EncounterDictionary, EDEDictChanged, editor);
	resDictRegisterEventCallback(gMessageDict, EDEMessageDictChanged, editor);
	//resDictRegisterEventCallback(gFSMDict, EDERefreshAllDocs, editor);

	// Initialize the expression editor and the FSMs
	GEEditorInitExpressionsEM(editor);
}

void EDESetupUIEM(EncounterEditDoc* encDoc)
{
	EDESetupUI(encDoc);
	eaPush(&encDoc->emDoc.ui_windows, encDoc->uiInfo.propertiesWindow);
	EditorPrefGetWindowPosition("Encounter Editor", "Properties Window", "Position", encDoc->uiInfo.propertiesWindow);
	ui_WindowShow(encDoc->uiInfo.propertiesWindow);
}

void EDEDestroyUIEM(EncounterEditDoc* encDoc)
{
	EditorPrefStoreWindowPosition("Encounter Editor", "Properties Window", "Position", encDoc->uiInfo.propertiesWindow);
	GEDestroyUIGenericEM(&encDoc->emDoc);
}

static EMEditorDoc* EDELoadEncounterEM(const char* name, const char* type)
{
	EncounterDef* encDef;
	EncounterEditDoc* encDoc = NULL;

	devassert(0 == stricmp(type, "EncounterDef"));

	encDef = RefSystem_ReferentFromString(g_EncounterDictionary, name);

	if (encDef && resIsEditingVersionAvailable(g_EncounterDictionary, name))
	{
		encDoc = calloc(1, sizeof(EncounterEditDoc));
		EDEOpenEncounter(encDef, encDoc);
		emDocAssocFile(&encDoc->emDoc, encDef->filename);
		strcpy(encDoc->emDoc.doc_display_name, encDef->name);

		// Set up the undo/redo queue
		encDoc->emDoc.edit_undo_stack = EditUndoStackCreate();
		EditUndoSetContext(encDoc->emDoc.edit_undo_stack, encDoc);
		encDoc->previousState = StructCloneFields(parse_EncounterDef, encDef);

		GECheckoutDocEM(&encDoc->emDoc);

		// Create the UI from the contact def
		EDESetupUIEM(encDoc);
		return &encDoc->emDoc;
	}
	else if (name)
	{
		resSetDictionaryEditMode(g_EncounterDictionary, true);
		resSetDictionaryEditMode(gMessageDict, true);
		resSetDictionaryEditMode("CritterGroup", true);
		resSetDictionaryEditMode("CritterDef", true);
		resSetDictionaryEditMode("CritterFaction", true);
		emSetResourceState(&s_EncounterEditor, name, EMRES_STATE_OPENING);
		resRequestOpenResource(g_EncounterDictionary, name);
		return NULL;
	}
	else
	{
		// Should never happen
		Alertf("Error: Encounter %s not found!", name);
		return NULL;
	}
}
static EMEditorDoc* EDENewEncounterEM(const char* type, const char* unused)
{
	EncounterDef* encDef;
	EncounterEditDoc* encDoc = calloc(1, sizeof(EncounterEditDoc));

	devassert(0 == stricmp(type, "EncounterDef"));

	encDef = EDENewEncounter(NULL, encDoc);

	emDocAssocFile(&encDoc->emDoc, encDef->filename);
	strcpy(encDoc->emDoc.doc_display_name, encDef->name);

	// Set up the undo/redo queue
	encDoc->emDoc.edit_undo_stack = EditUndoStackCreate();
	EditUndoSetContext(encDoc->emDoc.edit_undo_stack, encDoc);
	encDoc->previousState = StructCloneFields(parse_EncounterDef, encDef);

	GECheckoutDocEM(&encDoc->emDoc);

	// Create the UI from the contact def
	EDESetupUIEM(encDoc);

	return &encDoc->emDoc;
}

static void EDECloseEncounterEM(EMEditorDoc* editorDoc)
{
	EncounterEditDoc* encDoc = (EncounterEditDoc*)editorDoc;

	EDECloseEncounter(encDoc);

	EDEDestroyUIEM(encDoc);
	EditUndoStackDestroy(editorDoc->edit_undo_stack);
	StructDestroy(parse_EncounterDef, encDoc->previousState);
	free(editorDoc);
}

static EMTaskStatus EDESaveEncounterEM(EMEditorDoc* editorDoc)
{
	EncounterEditDoc* encDoc = (EncounterEditDoc*)editorDoc;
	EMTaskStatus status = EDESaveEncounter(encDoc);

	if(status == EM_TASK_SUCCEEDED)
	{
		editorDoc->saved = true;
	}

	return status;
}

static EMTaskStatus EDESaveEncounterAsEM(EMEditorDoc* editorDoc)
{
	// For now do the same thing as Save
	return EDESaveEncounterEM(editorDoc);
}


static EMTaskStatus EDEAutoSaveBackupEM(EMEditorDoc* editorDoc)
{
	char* backupFilename = NULL;
	EncounterEditDoc* encDoc = (EncounterEditDoc*)editorDoc;
	EncounterDef* encounterDef = encDoc->def;
	bool saved;
	if (resExtractNameSpace_s(editorDoc->doc_name, NULL, 0, NULL, 0))
	{
		return EM_TASK_SUCCEEDED;
	}

	estrStackCreate(&backupFilename);
	estrPrintf(&backupFilename, "%s.autosave.bak", encounterDef->filename);
	saved = GESaveEncounterInternal(encounterDef, backupFilename);
	estrDestroy(&backupFilename);

	if(saved)
		return EM_TASK_SUCCEEDED;
	else
		return EM_TASK_FAILED;
}

static void EDEProcessInputEM(EMEditorDoc* editorDoc)
{
	EDEProcessInput((EncounterEditDoc*) editorDoc);
}

static void EDEDrawEncounterEM(EMEditorDoc* editorDoc)
{
	EDEDrawEncounter((EncounterEditDoc*) editorDoc);
}

void EDEPlaceObjectEM(EMEditorDoc* editorDoc, const char* name, const char* type)
{
	EDEPlaceObject((EncounterEditDoc*) editorDoc, name, type);
}

static void EDEEnterEM(EMEditor *pEditor)
{
	resSetDictionaryEditMode(g_EncounterDictionary, true);
	resSetDictionaryEditMode(gMessageDict, true);
	resSetDictionaryEditMode("CritterGroup", true);
	resSetDictionaryEditMode("CritterDef", true);
	resSetDictionaryEditMode("CritterFaction", true);
}

static void EDEExitEM(EMEditor *pEditor)
{

}

#endif // NO_EDITORS

AUTO_RUN_LATE;
int EDERegisterEditor(void)
{
#ifndef NO_EDITORS
	if (!areEditorsAllowed() || !gConf.bAllowOldEncounterData)
		return 0;

	// Setup and register the contact editor
	strcpy(s_EncounterEditor.editor_name, "Encounter Editor");
	s_EncounterEditor.autosave_interval = 10;
	s_EncounterEditor.allow_multiple_docs = 1;
	s_EncounterEditor.allow_save = 1;
	s_EncounterEditor.hide_world = 1;
	s_EncounterEditor.requires_server_data = 1;

	s_EncounterEditor.type = EM_TYPE_SINGLEDOC;

	/* JAMES TODO:
	These are options that EM has that AM didn't.  Ignoring them for now,
	may want to revisit them later
	do_not_reload
	reload_prompt
	enable_clipboard

	doc_type_name
	browsers
	keybinds_name
	default_workspace
	wiki_path

	*/

	s_EncounterEditor.init_func = EDEInitEM;
	s_EncounterEditor.enter_editor_func = EDEEnterEM;
	s_EncounterEditor.exit_func = EDEExitEM;
	s_EncounterEditor.new_func = EDENewEncounterEM;
	s_EncounterEditor.load_func = EDELoadEncounterEM;
	s_EncounterEditor.close_func = EDECloseEncounterEM;
	s_EncounterEditor.save_func = EDESaveEncounterEM;
	s_EncounterEditor.save_as_func = EDESaveEncounterAsEM;
	s_EncounterEditor.autosave_func = EDEAutoSaveBackupEM;
	s_EncounterEditor.draw_func = EDEProcessInputEM;
	s_EncounterEditor.ghost_draw_func = EDEDrawEncounterEM;
	s_EncounterEditor.object_dropped_func = EDEPlaceObjectEM;

	s_EncounterEditor.default_type = "EncounterDef";
	strcpy(s_EncounterEditor.default_workspace, "Game Design Editors");

	s_EncounterEditor.keybinds_name = "EncounterEditor";

	emRegisterEditor(&s_EncounterEditor);
	emRegisterFileType(s_EncounterEditor.default_type, "Encounter", s_EncounterEditor.editor_name);

	// Setup and register the encounter browser
	strcpy(s_EncounterPicker.picker_name, "Encounter Library");
	strcpy(s_EncounterPicker.default_type, s_EncounterEditor.default_type);
	emPickerManage(&s_EncounterPicker);
	emPickerRegister(&s_EncounterPicker);

	eaPush(&s_EncounterEditor.pickers, &s_EncounterPicker);
#endif

	return 1;
}
