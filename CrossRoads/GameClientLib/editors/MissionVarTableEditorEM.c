/***************************************************************************
*     Copyright (c) 2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef NO_EDITORS

#include "file.h"
#include "gameeditorshared.h"
#include "MissionVarTableEditor.h"
#include "mission_common.h"
#include "AutoGen/mission_common_h_ast.h"
#include "StringCache.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););


static EMPicker s_MissionVarTablePicker = {0};
static EMEditor s_MissionVarTableEditor = {0};

static const char* MVEGetLogicalName(MissionVarTable* varTable) { return varTable->pchName; }
static const char* MVEGetFileName(MissionVarTable* varTable) { return varTable->filename; }

void MVENameChangedEM(const char* newName, MissionVarTableEditDoc* editDoc)
{
	// Update the asset manager and ui related fields
	strcpy(editDoc->emDoc.doc_name, newName);
	strcpy(editDoc->emDoc.doc_display_name, newName);

	// If the doc didn't have a filename before, name it
	//if (editDoc->newAndNeverSaved)
	{
		char outFile[MAX_PATH];
		sprintf(outFile, "defs/MissionVars/%s.missionvars", editDoc->varTable->pchName);
		//StructFreeString(editDoc->varTable->filename);
		editDoc->varTable->filename = allocAddString(outFile);
		GESetDocFileEM(&editDoc->emDoc, editDoc->varTable->filename);
	}

	GESetCurrentDocUnsaved();
	editDoc->emDoc.name_changed = 1;
}

void MVESetupUIEM(MissionVarTableEditDoc* editDoc)
{
	MVESetupUI(editDoc);
	eaPush(&editDoc->emDoc.ui_windows, editDoc->window);
	editDoc->emDoc.primary_ui_window = editDoc->window;
}

static EMEditorDoc* MVELoadMissionVarTableEM(const char* name, const char* type)
{
	MissionVarTable* varTable;
	MissionVarTableEditDoc* editDoc = calloc(1, sizeof(MissionVarTableEditDoc));
	editDoc->emDoc.editor = &s_MissionVarTableEditor;

	devassert(0 == stricmp(type, "MissionVarTable"));

	varTable = MVEOpenMissionVarTable(name, editDoc);
	if (varTable)
	{
		emDocAssocFile(&editDoc->emDoc, varTable->filename);
		strcpy(editDoc->emDoc.doc_display_name, varTable->pchName);

		// Set up the undo/redo queue
		editDoc->emDoc.edit_undo_stack = EditUndoStackCreate();

		GECheckoutDocEM(&editDoc->emDoc);

		// Create the UI from the contact def
		MVESetupUIEM(editDoc);
	}
	else
	{
		free(editDoc);
		return NULL;
	}

	return &editDoc->emDoc;
}

static EMEditorDoc* MVENewMissionVarTableEM(const char* type, const char* unused)
{
	MissionVarTable* varTable;
	MissionVarTableEditDoc* editDoc = calloc(1, sizeof(MissionVarTableEditDoc));
	editDoc->emDoc.editor = &s_MissionVarTableEditor;

	devassert(0 == stricmp(type, "MissionVarTable"));

	varTable = MVENewMissionVarTable(NULL, editDoc);

	emDocAssocFile(&editDoc->emDoc, varTable->filename);
	strcpy(editDoc->emDoc.doc_display_name, varTable->pchName);

	// Set up the undo/redo queue
	editDoc->emDoc.edit_undo_stack = EditUndoStackCreate();

	GECheckoutDocEM(&editDoc->emDoc);

	// Create the UI from the contact def
	MVESetupUIEM(editDoc);

	return &editDoc->emDoc;
}


static void MVECloseMissionVarTableEM(EMEditorDoc* editorDoc)
{
	MissionVarTableEditDoc* editDoc = (MissionVarTableEditDoc*)editorDoc;

	MVECloseMissionVarTable(editDoc);

	GEDestroyUIGenericEM(editorDoc);
	EditUndoStackDestroy(editorDoc->edit_undo_stack);
	free(editorDoc);
}

static EMTaskStatus MVESaveMissionVarTableEM(EMEditorDoc* editorDoc)
{
	MissionVarTableEditDoc* editDoc = (MissionVarTableEditDoc*)editorDoc;
	EMTaskStatus status = MVESaveMissionVarTable(editDoc, false);

	return status;
}
static EMTaskStatus MVESaveMissionVarTableAsEM(EMEditorDoc* editorDoc)
{
	MissionVarTableEditDoc* editDoc = (MissionVarTableEditDoc*)editorDoc;
	EMTaskStatus status = MVESaveMissionVarTable(editDoc, true);

	return status;
}

static EMTaskStatus MVEAutoSaveBackupEM(EMEditorDoc* editorDoc)
{
	char* backupFilename = NULL;
	MissionVarTableEditDoc* editDoc = (MissionVarTableEditDoc*)editorDoc;
	MissionVarTable* varTable = editDoc->varTable;
	bool saved;
	estrStackCreate(&backupFilename);
	estrPrintf(&backupFilename, "%s.autosave.bak", varTable->filename);
	saved = MVESaveMissionVarTableFile(varTable, backupFilename);
	estrDestroy(&backupFilename);
	if(saved)
		return EM_TASK_SUCCEEDED;
	else
		return EM_TASK_FAILED;
}

static bool MVESaveCallback(EMEditor *editor, const char *name, MissionVarTableEditDoc *editDoc, EMResourceState state, void *callback_data, bool success)
{
	if (success && (state == EMRES_STATE_SAVE_SUCCEEDED))
	{
		MissionVarTable *varTable = editDoc->varTable;
		editDoc->newAndNeverSaved = 0;
		StructCopyAll(parse_MissionVarTable, editDoc->varTable, editDoc->varTableOrig);
		ui_WidgetSetTextString(UI_WIDGET(editDoc->window), varTable->pchName);
		editDoc->emDoc.saved = true;
	}
	return true;
}

static void MVEDocRefreshEM(MissionVarTableEditDoc *pDoc)
{
	if (!pDoc)
		return;
	
	pDoc->bNeedsRefresh = false;

	if (pDoc->varTable)
	{
		MissionVarTable *varTable = RefSystem_ReferentFromString(g_MissionVarTableDict, pDoc->varTable->pchName);
		if (varTable)
		{
			MissionVarTable *varTableCopy = StructClone(parse_MissionVarTable, varTable);
			langMakeEditorCopy(parse_MissionVarTable, varTableCopy, false);
			
			if (StructCompare(parse_MissionVarTable, varTableCopy, pDoc->varTable, 0, 0, 0)
				&& StructCompare(parse_MissionVarTable, varTableCopy, pDoc->varTableOrig, 0, 0, 0))
			{
				if (pDoc->emDoc.saved)
				{
					// Reload latest version of the def
					emCloseDoc(&pDoc->emDoc);
					emOpenFileEx(varTable->pchName, "MissionVarTable");
				}
				else
				{
					Alertf("File '%s' changed on disk, but is open in the editor.  Close and reopen editor to get changes.  DO NOT SAVE or you will overwrite any changes on disk!", varTable->pchName);
				}
			}

			StructDestroy(parse_MissionVarTable, varTableCopy);
		}
		else if (!pDoc->newAndNeverSaved)
		{
			Errorf("Error: File '%s' tried to refresh, but it doesn't exist in the dictionary!  Please get Ben F or Stephen.", pDoc->varTable->pchName);
		}
	}
}



static void MVEDictChanged(enumResourceEventType eType, const char *pDictName, ConstReferenceData pRefData, Referent pReferent, EMEditor *pEditor)
{
	const char *pcName = (const char *)pRefData;

	if (!pcName) {
		return;
	}	

	// If an existing object was modified, it needs to be refreshed in the editor.
	if (gConf.bServerSaving && eType == RESEVENT_RESOURCE_MODIFIED)
	{
		MissionVarTable *varTable = (MissionVarTable*)pReferent;
		int i, n;

		// Queue Mission to be reloaded if the def is open in the editor
		n = eaSize(&s_MissionVarTableEditor.open_docs);
		for (i = 0; i < n; i++)
		{
			MissionVarTableEditDoc *doc = (MissionVarTableEditDoc*)s_MissionVarTableEditor.open_docs[i];
			devassert(doc->varTable->pchName);
			if (0 == stricmp(doc->varTable->pchName, varTable->pchName) && !doc->bNeedsRefresh)
			{
				doc->bNeedsRefresh = true;
				emQueueFunctionCall(MVEDocRefreshEM, doc);
			}
		}
	}
}

static void MVEMessageDictChanged(enumResourceEventType eType, const char *pDictName, ConstReferenceData pRefData, Referent pReferent, EMEditor *pEditor)
{
	int i, n;
	// Queue all Mission Docs to be refreshed
	if (gConf.bServerSaving && eType == RESEVENT_RESOURCE_MODIFIED)
	{
		// Carefully handle reloading the def if it's open in the editor
		n = eaSize(&s_MissionVarTableEditor.open_docs);
		for (i = 0; i < n; i++)
		{
			MissionVarTableEditDoc *doc = (MissionVarTableEditDoc*)s_MissionVarTableEditor.open_docs[i];
			if (!doc->bNeedsRefresh)
			{
				doc->bNeedsRefresh = true;
				emQueueFunctionCall(MVEDocRefreshEM, doc);
			}
		}
	}
}


static void MVEEditorEMEnter(EMEditor *pEditor)
{
    resSetDictionaryEditMode(g_MissionVarTableDict, true);
	resSetDictionaryEditMode(gMessageDict, true);
}

static void MVEEditorEMExit(EMEditor *pEditor)
{

}

static void MVEEditorInit(EMEditor* pEditor)
{
	emAddDictionaryStateChangeHandler(pEditor, "MissionVarTable", NULL, NULL, MVESaveCallback, NULL, NULL);
	resDictRegisterEventCallback(g_MissionVarTableDict, MVEDictChanged, pEditor);
	resDictRegisterEventCallback(gMessageDict, MVEMessageDictChanged, pEditor);

	GEEditorInitExpressionsEM(pEditor);
}

#endif // NO_EDITORS

AUTO_RUN_LATE;
int MissionVarTableEditorRegisterEditor(void)
{
#ifndef NO_EDITORS
	if (!areEditorsAllowed() || !gConf.bAllowOldEncounterData)
		return 0;

	// Setup and register the editor
	strcpy(s_MissionVarTableEditor.editor_name, "MissionVarTable Editor");
	s_MissionVarTableEditor.autosave_interval = 10;
	s_MissionVarTableEditor.allow_multiple_docs = 1;
	s_MissionVarTableEditor.allow_save = 1;
	s_MissionVarTableEditor.hide_world = 1;
	s_MissionVarTableEditor.requires_server_data = 1;

	s_MissionVarTableEditor.type = EM_TYPE_MULTIDOC;

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

	s_MissionVarTableEditor.init_func = MVEEditorInit;
	s_MissionVarTableEditor.new_func = MVENewMissionVarTableEM;
	s_MissionVarTableEditor.load_func = MVELoadMissionVarTableEM;
	s_MissionVarTableEditor.close_func = MVECloseMissionVarTableEM;
	s_MissionVarTableEditor.save_func = MVESaveMissionVarTableEM;
	s_MissionVarTableEditor.save_as_func = MVESaveMissionVarTableAsEM;
	s_MissionVarTableEditor.autosave_func = MVEAutoSaveBackupEM;
	s_MissionVarTableEditor.enter_editor_func = MVEEditorEMEnter;
	s_MissionVarTableEditor.exit_func = MVEEditorEMExit;

	s_MissionVarTableEditor.default_type = "MissionVarTable";
	strcpy(s_MissionVarTableEditor.default_workspace, "Game Design Editors");

	s_MissionVarTableEditor.keybinds_name = "MissionEditor";

	// Setup and register the store picker
	s_MissionVarTablePicker.allow_outsource = 1;
	s_MissionVarTablePicker.requires_server_data = 1;
	strcpy(s_MissionVarTablePicker.picker_name, "MissionVarTable Library");
	strcpy(s_MissionVarTablePicker.default_type, s_MissionVarTableEditor.default_type);
	emPickerManage(&s_MissionVarTablePicker);
	eaPush(&s_MissionVarTableEditor.pickers, &s_MissionVarTablePicker);

	emRegisterEditor(&s_MissionVarTableEditor);
	emRegisterFileType(s_MissionVarTableEditor.default_type, "MissionVarTable", s_MissionVarTableEditor.editor_name);
#endif

	return 1;
}
