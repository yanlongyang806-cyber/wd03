/***************************************************************************
*     Copyright (c) 2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef NO_EDITORS

#include "gameeditorshared.h"


#include "EditorManagerUtils.h"
#include "ExpressionEditor.h"


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););


// Most GE editors need expressions, which need to be initialized when the editor loads
void GEEditorInitExpressionsEM(EMEditor* editor)
{
	exprEdInit();
}

// Other utility functions

void* GEGetActiveEditorDocEM(const char* docType)
{
	EMEditorDoc* emDoc = emGetActiveEditorDoc();

	// Make sure this is a GE doc of the type the user requested
	if(emDoc)
	{
		if(docType)
		{
			if(stricmp(emDoc->doc_type, docType))
				emDoc = NULL;
		}
		else
		{
			// Mission and contact docs are no longer "GE"-type docs
			if(	stricmp(emDoc->doc_type, "encounterlayer") && 
				stricmp(emDoc->doc_type, "Store") && 
				stricmp(emDoc->doc_type, "EncounterDef") && 
				stricmp(emDoc->doc_type, "MissionVarTable"))
				emDoc = NULL;
		}
	}

	return emDoc;
}

bool GECheckoutDocEM(EMEditorDoc* editorDoc)
{
	EMFile** files = NULL;
	int i, n;

	emDocGetFiles(editorDoc, &files, true);

	n = eaSize(&files);
	for(i=0; i<n; i++)
	{
		EMFile* file = files[i];

		if(!file->checked_out)
		{
			if(!emuCheckoutFile(editorDoc->all_files[i]->file))
			{
				Alertf("File could not be checked out, changes will not be saved!");
				return false;
			}
			file->checked_out = true;
		}
	}

	eaDestroy(&files);
	return true;
}

void GEPushUndoState(GEEditorDocPtr docPtr)
{
	GenericMissionEditDoc* geDoc = (GenericMissionEditDoc*) docPtr;
	if(geDoc->docDefinition->GetStateCB)
	{
		void* undoState = geDoc->docDefinition->GetStateCB(geDoc);
		EditCreateUndoCustom(geDoc->emDoc.edit_undo_stack, geDoc->docDefinition->UndoStateCB, geDoc->docDefinition->RedoStateCB, geDoc->docDefinition->FreeStateCB, undoState);
	}
}

void GESetDocUnsaved(GEEditorDocPtr docPtr)
{
	EMEditorDoc* editorDoc = (EMEditorDoc*) docPtr;
	if (editorDoc)
	{
		editorDoc->saved = false;

		GECheckoutDocEM(editorDoc);

		GEPushUndoState(docPtr);
	}
}

void GESetCurrentDocUnsaved(void)
{
	GESetDocUnsaved( GEGetActiveEditorDocEM(NULL) );
}

// UI destroy function for editors with no special cases
void GEDestroyUIGenericEM(EMEditorDoc* editorDoc)
{
	eaDestroyEx(&editorDoc->ui_windows, ui_WindowFreeInternal);
}

void GESetDocFileEM(EMEditorDoc* emDoc, const char* filename)
{
	emDocRemoveAllFiles(emDoc, false);
	emDocAssocFile(emDoc, filename);
}

#endif
