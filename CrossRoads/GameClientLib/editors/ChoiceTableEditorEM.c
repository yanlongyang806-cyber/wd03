#ifndef NO_EDITORS

#include "ChoiceTableEditor.h"

#include "ChoiceTable_common.h"
#include "error.h"
#include "file.h"

// Magic code required by budgets
AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

//-----------------------------------------------------------------------------------
// Global Data
//-----------------------------------------------------------------------------------
static EMPicker gChoiceTablePicker = { 0 };
EMEditor gChoiceTableEditor = { 0 };


//---------------------------------------------------------------------------------------------------
// Editor
//---------------------------------------------------------------------------------------------------

static EMEditorDoc *choiceEdEM_LoadDoc( const char *pcNameToOpen, const char *pcType )
{
	ChoiceEd_Doc *pDoc;
	const char* pcName;
	
	if( !gConf.bServerSaving ) {
		Alertf( "This editor only operates when server saving is enabled" );
		return NULL;
	}

	// Open or create the object
	pDoc = choiceEd_Open(&gChoiceTableEditor, (char*)pcNameToOpen);
	if (!pDoc) {
		return NULL;
	}

	pcName = pDoc->pChoiceTable->pchName;

	// Set up the document
	strcpy(pDoc->emDoc.doc_name, pcName);
	strcpy(pDoc->emDoc.doc_type, "ChoiceTable");
	strcpy(pDoc->emDoc.doc_display_name, pDoc->emDoc.doc_name);
	if( pcNameToOpen ) {
		// If given a name to open, this is not a new document so set the original name
		strcpy(pDoc->emDoc.orig_doc_name, pcName);
	}

	return &pDoc->emDoc;
}

static EMEditorDoc *choiceEdEM_NewDoc( const char *pcType, void *data )
{
	return choiceEdEM_LoadDoc(NULL, pcType);
}

static void choiceEdEM_ReloadDoc(EMEditorDoc *pDoc)
{
	choiceEd_Revert((ChoiceEd_Doc*)pDoc);
}

static void choiceEdEM_CloseDoc( EMEditorDoc *pDoc )
{
	choiceEd_Close((ChoiceEd_Doc*)pDoc);
	SAFE_FREE(pDoc);
}

static EMTaskStatus choiceEdEM_SaveDoc(EMEditorDoc *pDoc)
{
	return choiceEd_Save((ChoiceEd_Doc*)pDoc, false);
}

static EMTaskStatus choiceEdEM_SaveAsDoc(EMEditorDoc *pDoc)
{
	return choiceEd_Save((ChoiceEd_Doc*)pDoc, true);
}

static void choiceEdEM_Enter(EMEditor *pEditor)
{
}

static void choiceEdEM_Exit(EMEditor *pEditor)
{
}

static void choiceEdEM_LostFocus(EMEditorDoc *pDoc)
{
}

static void choiceEdEM_Init(EMEditor *pEditor)
{
	choiceEd_Init(pEditor);
}

#endif

AUTO_RUN_LATE;
int choiceEdEM_Register( void )
{
#ifndef NO_EDITORS
	if (!areEditorsAllowed())
		return 0;

	// Register the editor
	strcpy( gChoiceTableEditor.editor_name, "Choice Table Editor" );
	gChoiceTableEditor.type = EM_TYPE_MULTIDOC;
	gChoiceTableEditor.allow_save = 1;
	gChoiceTableEditor.allow_multiple_docs = 1;
	gChoiceTableEditor.hide_world = 1;
	gChoiceTableEditor.disable_auto_checkout = 1;
	gChoiceTableEditor.default_type = "ChoiceTable";
	strcpy( gChoiceTableEditor.default_workspace, "Game Design Editors" );

	gChoiceTableEditor.init_func = choiceEdEM_Init;
	gChoiceTableEditor.enter_editor_func = choiceEdEM_Enter;
	gChoiceTableEditor.exit_func = choiceEdEM_Exit;
	gChoiceTableEditor.lost_focus_func = choiceEdEM_LostFocus;
	gChoiceTableEditor.new_func = choiceEdEM_NewDoc;
	gChoiceTableEditor.load_func = choiceEdEM_LoadDoc;
	gChoiceTableEditor.reload_func = choiceEdEM_ReloadDoc;
	gChoiceTableEditor.save_func = choiceEdEM_SaveDoc;
	gChoiceTableEditor.save_as_func = choiceEdEM_SaveAsDoc;
	gChoiceTableEditor.close_func = choiceEdEM_CloseDoc;

	// Register the picker
	gChoiceTablePicker.allow_outsource = 1;
	strcpy( gChoiceTablePicker.picker_name, "Choice Table Library" );
	strcpy( gChoiceTablePicker.default_type, gChoiceTableEditor.default_type );
	emPickerManage( &gChoiceTablePicker );
	eaPush( &gChoiceTableEditor.pickers, &gChoiceTablePicker );

	emRegisterEditor( &gChoiceTableEditor );
	emRegisterFileType( gChoiceTableEditor.default_type, "Choice Table", gChoiceTableEditor.editor_name );
#endif

	return 0;
}
