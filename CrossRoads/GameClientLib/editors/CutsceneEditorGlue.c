#include "cutscene_common.h"
#include "CutsceneEditor.h"
#include "CutsceneDemoPlayEditor.h"

#include "cutscene_common_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

CutsceneEditorState *cutEdCutsceneEditorState()
{
	CutsceneEditorDoc *pDoc = (CutsceneEditorDoc *)emGetActiveEditorDoc();
	CutsceneDemoPlayEditor *pCutsceneDemoPlayEditor = cutEdDemoPlayEditor();
	if(pDoc)
		return &pDoc->state;
	else if(pCutsceneDemoPlayEditor)
		return &cutEdDemoPlayEditor()->state;
	return NULL;
}

bool cutEdIsEditable(CutsceneEditorState *pState)
{
	// Make sure the resource is checked out of Gimme
	if(pState->pParentDoc && !emDocIsEditable(&pState->pParentDoc->emDoc, true))
	{
		cutEdRefreshUICommon(pState);
		return false;
	}
	return true;
}

static void cutEdUndoFree(CutsceneEditorState *pState, CSEUndoData *pData)
{
	// Free the memory
	StructDestroy(parse_CutsceneDef, pData->pPreCutsceneDef);
	StructDestroy(parse_CutsceneDef, pData->pPostCutsceneDef);
	free(pData);
}

void cutEdSetUnsaved(CutsceneEditorState *pState)
{
	CSEUndoData *pData = calloc(1, sizeof(CSEUndoData));
	pData->pPreCutsceneDef = pState->pNextUndoCutsceneDef;
	pData->pPostCutsceneDef = StructClone(parse_CutsceneDef, pState->pCutsceneDef);

	if(pState->pParentDoc)
	{
		EMFile *emFile = NULL;

		EditCreateUndoCustom(pState->pParentDoc->emDoc.edit_undo_stack, cutEdUndo, cutEdRedo, cutEdUndoFree, pData);
		emSetDocUnsaved(&pState->pParentDoc->emDoc, false);
	}
	else
	{
		EditCreateUndoCustom(pState->edit_undo_stack, cutEdUndo, cutEdRedo, cutEdUndoFree, pData);
		ui_ButtonSetText(pState->pUIButtonSaveDemo, "Save *");
		pState->bUnsaved = 1;
	}

	pState->pNextUndoCutsceneDef = StructClone(parse_CutsceneDef, pState->pCutsceneDef);
}

bool cutEdIsUnsaved(CutsceneEditorState *pState)
{
	if(pState->pParentDoc)
		return !emGetDocSaved(&pState->pParentDoc->emDoc);
	return pState->bUnsaved;
}
