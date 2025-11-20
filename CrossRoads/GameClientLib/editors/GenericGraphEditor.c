/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "EditorManager.h"
#include "Color.h"
#include "GenericGraphEditor.h"
#include "StringCache.h"
#include "file.h"

#include "AutoGen/GenericGraphEditor_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););


/////////////////////////////////////////////////////////////////////////////////////////
// Example Graph Object
/////////////////////////////////////////////////////////////////////////////////////////

DictionaryHandle g_SampleGraphDictionary = NULL;


static int SampleGraph_ValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, SampleGraph *pSampleGraph, U32 userID)
{
	switch(eType)
	{

	// Fix filename: called during saving.
	xcase RESVALIDATE_FIX_FILENAME:
		resFixPooledFilename(&pSampleGraph->pchFilename, "samplegraphs", pSampleGraph->pchScope, pSampleGraph->pchName, "samplegraph");
		return VALIDATE_HANDLED;
	}
	return VALIDATE_NOT_HANDLED;
}


AUTO_RUN;
void initSampleGraph(void)
{
	g_SampleGraphDictionary = RefSystem_RegisterSelfDefiningDictionary("SampleGraph", false, parse_SampleGraph, true, true, NULL);

	resDictManageValidation(g_SampleGraphDictionary, SampleGraph_ValidateCB);	

	if (isDevelopmentMode() || isProductionEditMode()) {
		resDictMaintainInfoIndex(g_SampleGraphDictionary, ".name", ".scope", ".tags", NULL, NULL);
	}
}

AUTO_STARTUP(SampleGraphs);
void SampleGraphs_Load(void)
{
//	resLoadResourcesFromDisk(g_SampleGraphDictionary, "samplegraphs", ".samplegraph", NULL,  PARSER_OPTIONALFLAG);
}

// Example editor

#ifndef NO_EDITORS

static EMPicker s_SampleGraphPicker = {0};

EMEditor s_SampleGraphEditor = {0};

static UISkin *gMainWindowSkin;


typedef struct SampleGraphEditDoc
{
	EMEditorDoc emDoc;
	void *unusedPointerNeededToAvoidCrashesWithOldKeybindFiles;

	SampleGraph *pGraph;
	SampleGraph *pOrigGraph;

	UIWindow *pMainWindow;
	UIFlowchart *pFlow;
} SampleGraphEditDoc;


static void SampleGraphEditorRefreshDisplay(SampleGraphEditDoc *pDoc)
{
	GraphIterator iter;
	const char *nodeName;
	SampleGraph *pGraph = pDoc->pGraph;	
	UIFlowchartButton **incoming = NULL, **outgoing = NULL;
	F32 x = 0, y = 0;

	ui_FlowchartClear(pDoc->pFlow);

	graphIterInitAllNodes(&iter, &pGraph->graph);

	while (nodeName = graphIterGetNext(&iter))
	{
		UIFlowchartNode *pNode;
		eaPush(&incoming, ui_FlowchartButtonCreate(pDoc->pFlow, UIFlowchartNormal, NULL, NULL));
		eaPush(&outgoing, ui_FlowchartButtonCreate(pDoc->pFlow, UIFlowchartNormal, NULL, NULL));

		pNode = ui_FlowchartNodeCreate(nodeName, x, y, 100, 100, &incoming, &outgoing, (void *)nodeName);
		ui_FlowchartAddNode(pDoc->pFlow, pNode);
		eaClear(&incoming); eaClear(&outgoing);
		x+= 120;
	}

	eaDestroy(&incoming); eaDestroy(&outgoing);

	graphIterInitAllNodes(&iter, &pGraph->graph);

	while (nodeName = graphIterGetNext(&iter))
	{
		GraphIterator linkIter;
		const char *destName;
		UIFlowchartNode *pNode = ui_FlowchartFindNode(pDoc->pFlow, (void *)nodeName);

		graphIterInitOutgoingLinks(&linkIter, &pGraph->graph, nodeName);

		while (destName = graphIterGetNext(&linkIter))
		{
			UIFlowchartNode *pDestNode = ui_FlowchartFindNode(pDoc->pFlow, (void *)destName);
			if (pNode && pDestNode)
			{				
				ui_FlowchartLink(pNode->outputButtons[0], pDestNode->inputButtons[0]);
			}
		}
	}

	pDoc->emDoc.saved = 0;
}

static void SampleGraphEditorPreOpenFixup(SampleGraph *pGraph)
{

}

AUTO_COMMAND ACMD_NAME("gge_addnode");
void CmdAddNode(void) 
{
#ifndef NO_EDITORS
	int randnum = rand() % 1024;
	char nodeName[128];
	SampleGraphEditDoc *pDoc = (SampleGraphEditDoc*)emGetActiveEditorDoc();

	if (pDoc)
	{	
		sprintf(nodeName, "Node %d", randnum);
		graphAddNode(&pDoc->pGraph->graph, nodeName);

		SampleGraphEditorRefreshDisplay(pDoc);
	}
#endif
}


bool GraphEditorFlowchartLinkRequest(UIFlowchart* flowchart, UIFlowchartButton* source,
	UIFlowchartButton* dest, bool force, SampleGraphEditDoc* doc )
{
	const char *pSourceName = (const char *)source->node->userData;
	const char *pDestName = (const char *)dest->node->userData;

	if( !force ) 
	{
		if( source->node == dest->node ) 
		{
			return false;
		}
	}

	graphAddLink(&doc->pGraph->graph, pSourceName, pDestName);
	return true;
}

bool GraphEditorFlowchartUnlinkRequest(UIFlowchart* flowchart, UIFlowchartButton* source,
									 UIFlowchartButton* dest, bool force, SampleGraphEditDoc* doc )
{
	const char *pSourceName = (const char *)source->node->userData;
	const char *pDestName = (const char *)dest->node->userData;
	
	graphRemoveLink(&doc->pGraph->graph, pSourceName, pDestName);
	return true;
}

bool GraphEditorFlowchartNodeRemoveRequest(UIFlowchart* flowchart, UIFlowchartNode* uiNode, SampleGraphEditDoc* doc )
{
	const char *pSourceName = (const char *)uiNode->userData;

	graphRemoveNode(&doc->pGraph->graph, pSourceName);
	return true;
}


static SampleGraphEditDoc *SampleGraphEditorInitDoc(SampleGraph *pSampleGraph)
{
	UIPane *pPane;
	SampleGraphEditDoc *pDoc;

	// Initialize the structure
	pDoc = (SampleGraphEditDoc*)calloc(1,sizeof(SampleGraphEditDoc));

	// Fill in the SampleGraph data
	if (!pSampleGraph) {
		pDoc->pGraph = StructCreate(parse_SampleGraph);
		assert(pDoc->pGraph);
		emMakeUniqueDocName(&pDoc->emDoc, "New_SampleGraph", "SampleGraph", "SampleGraph");
		pDoc->pGraph->pchName = (char*)allocAddString(pDoc->emDoc.doc_name);
	} else {
		pDoc->pGraph = StructClone(parse_SampleGraph, pSampleGraph);
		assert(pDoc->pGraph);		
		pDoc->pOrigGraph = StructClone(parse_SampleGraph, pDoc->pGraph);		
		strcpy(pDoc->emDoc.doc_name, pDoc->pGraph->pchName);
		// If given a name to open, this is not a new document so set the original name
		strcpy(pDoc->emDoc.orig_doc_name, pDoc->pGraph->pchName);
	}
	
	strcpy(pDoc->emDoc.doc_type, "SampleGraph");
	strcpy(pDoc->emDoc.doc_display_name, pDoc->emDoc.doc_name);

	resFixFilename(g_SampleGraphDictionary, pDoc->pGraph->pchName, pDoc->pGraph);

	// Create the main window
	pDoc->pMainWindow = ui_WindowCreate(pDoc->pGraph->pchName, 15, 50, 800, 600);

	pPane = ui_PaneCreate(0,0,0.5,1.0, UIUnitPercentage, UIUnitPercentage, 0);
	ui_WidgetSkin(UI_WIDGET(pPane), gMainWindowSkin);
	ui_WindowAddChild(pDoc->pMainWindow, UI_WIDGET(pPane));

	pDoc->pFlow = ui_FlowchartCreate(NULL, NULL, NULL, NULL);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pDoc->pFlow), 1.0, 1.0, UIUnitPercentage, UIUnitPercentage);
	ui_PaneAddChild(pPane, UI_WIDGET(pDoc->pFlow));

	ui_FlowchartSetLinkedCallback( pDoc->pFlow, GraphEditorFlowchartLinkRequest, pDoc );
	ui_FlowchartSetUnlinkedCallback( pDoc->pFlow, GraphEditorFlowchartUnlinkRequest, pDoc );
	ui_FlowchartSetNodeRemovedCallback( pDoc->pFlow, GraphEditorFlowchartNodeRemoveRequest, pDoc );

	SampleGraphEditorPreOpenFixup(pDoc->pGraph);
	SampleGraphEditorRefreshDisplay(pDoc);

	// Editor Manager needs to be told about the windows used
	ui_WindowPresent(pDoc->pMainWindow);
	pDoc->emDoc.primary_ui_window = pDoc->pMainWindow;
	eaPush(&pDoc->emDoc.ui_windows, pDoc->pMainWindow);

	return pDoc;
}


static EMEditorDoc *SampleGraphEditorLoadDoc(const char *pcNameToOpen, const char *pcType)
{
	SampleGraph *pGraph = NULL;
	SampleGraphEditDoc *pDoc;
	
	if (pcNameToOpen && resIsEditingVersionAvailable(g_SampleGraphDictionary, pcNameToOpen)) {
		// Simply open the object since it is in the dictionary
		pGraph = RefSystem_ReferentFromString(g_SampleGraphDictionary, pcNameToOpen);
	} else if (pcNameToOpen) {
		// Wait for object to show up so we can open it
		resSetDictionaryEditMode(g_SampleGraphDictionary, true);
		emSetResourceState(&s_SampleGraphEditor, pcNameToOpen, EMRES_STATE_OPENING);
		resRequestOpenResource(g_SampleGraphDictionary, pcNameToOpen);
		return NULL;
	}
	pDoc = SampleGraphEditorInitDoc(pGraph);

	if (pDoc) 
	{
		return &pDoc->emDoc;
	}
	else
	{
		return NULL;
	}
}


static EMEditorDoc *SampleGraphEditorNewDoc(const char *pcType, void *data)
{
	return SampleGraphEditorLoadDoc(NULL, pcType);
}

static void SampleGraphEditorReloadDoc(EMEditorDoc *pEditorDoc)
{
	SampleGraphEditDoc *pDoc = (SampleGraphEditDoc *)pEditorDoc;
	SampleGraph *pGraph;

	if (!pDoc->emDoc.orig_doc_name[0]) {
		// Cannot revert if no original
		return;
	}

	pGraph = RefSystem_ReferentFromString(pDoc->emDoc.doc_type, pDoc->emDoc.orig_doc_name);
	if (pGraph) {
		// Revert the mission
		StructDestroy(parse_SampleGraph, pDoc->pGraph);
		StructDestroy(parse_SampleGraph, pDoc->pOrigGraph);
		pDoc->pGraph = StructClone(parse_SampleGraph, pGraph);		
		pDoc->pOrigGraph = StructClone(parse_SampleGraph, pGraph);

		SampleGraphEditorPreOpenFixup(pDoc->pGraph);
		SampleGraphEditorRefreshDisplay(pDoc);
	} 
}


static void SampleGraphEditorCloseDoc(EMEditorDoc *pEditorDoc)
{
	SampleGraphEditDoc *pDoc = (SampleGraphEditDoc *)pEditorDoc;
	
	// Free the objects
	StructDestroy(parse_SampleGraph, pDoc->pGraph);
	if (pDoc->pOrigGraph)
		StructDestroy(parse_SampleGraph, pDoc->pOrigGraph);

	// Close the window
	ui_WindowHide(pDoc->emDoc.primary_ui_window);
	ui_WidgetQueueFree((UIWidget*)pDoc->emDoc.primary_ui_window);

	SAFE_FREE(pDoc);
}


EMTaskStatus SampleGraphEditorSaveFunction(SampleGraphEditDoc* pDoc, bool bSaveAsNew)
{
	EMTaskStatus status;
	SampleGraph *pGraphCopy;

	// Deal with state changes
	if (emHandleSaveResourceState(pDoc->emDoc.editor, pDoc->pGraph->pchName, &status)) {
		return status;
	}

	// Do cleanup before validation
	pGraphCopy = StructCloneFields(parse_SampleGraph, pDoc->pGraph);

	// Do the save (which will free the copy)
	status = emSmartSaveDoc(&pDoc->emDoc, pGraphCopy, pDoc->pOrigGraph, bSaveAsNew);

	return status;
}


static EMTaskStatus SampleGraphEditorSave(EMEditorDoc *pDoc)
{
	return SampleGraphEditorSaveFunction((SampleGraphEditDoc*)pDoc, false);
}

static EMTaskStatus SampleGraphEditorSaveAs(EMEditorDoc *pDoc)
{
	return SampleGraphEditorSaveFunction((SampleGraphEditDoc*)pDoc, true);
}

void SampleGraphEditorInit(EMEditor *pEditor)
{
	emMenuItemCreate(pEditor, "gge_addnode", "Add Node", NULL, NULL, "gge_addnode");
	emMenuRegister(pEditor, emMenuCreate(pEditor, "Node", "gge_addnode", NULL));

	gMainWindowSkin = ui_SkinCreate(NULL);
	gMainWindowSkin->background[0] = CreateColorRGB(146, 143, 135);

	// Have Editor Manager handle a lot of change tracking
	emAutoHandleDictionaryStateChange(pEditor, "SampleGraph", true, NULL, NULL, NULL, NULL, NULL);
}


static void SampleGraphEditorEnter(EMEditor *pEditor)
{
	resSetDictionaryEditMode(g_SampleGraphDictionary, true);
}

#endif 

AUTO_RUN_LATE;
int SampleGraphEditorEMRegister(void)
{
#ifndef NO_EDITORS
	if (!areEditorsAllowed())
		return 0;

/*	// Register the editor
	strcpy(s_SampleGraphEditor.editor_name, "SampleGraph Editor");
	s_SampleGraphEditor.type = EM_TYPE_MULTIDOC;
	s_SampleGraphEditor.allow_save = 1;
	s_SampleGraphEditor.allow_multiple_docs = 1;
	s_SampleGraphEditor.hide_world = 1;
	s_SampleGraphEditor.disable_auto_checkout = 1;
	s_SampleGraphEditor.default_type = "SampleGraph";
	strcpy(s_SampleGraphEditor.default_workspace, "Game Design Editors");

	s_SampleGraphEditor.init_func = SampleGraphEditorInit;
	s_SampleGraphEditor.enter_editor_func = SampleGraphEditorEnter;
	s_SampleGraphEditor.new_func = SampleGraphEditorNewDoc;
	s_SampleGraphEditor.load_func = SampleGraphEditorLoadDoc;
	s_SampleGraphEditor.reload_func = SampleGraphEditorReloadDoc;
	s_SampleGraphEditor.close_func = SampleGraphEditorCloseDoc;
	s_SampleGraphEditor.save_func = SampleGraphEditorSave;
	s_SampleGraphEditor.save_as_func = SampleGraphEditorSaveAs;

	// Register the picker
	s_SampleGraphPicker.allow_outsource = 1;
	strcpy(s_SampleGraphPicker.picker_name, "SampleGraph Library");
	strcpy(s_SampleGraphPicker.default_type, s_SampleGraphEditor.default_type);
	emPickerManage(&s_SampleGraphPicker);
	eaPush(&s_SampleGraphEditor.pickers, &s_SampleGraphPicker);
	emRegisterEditor(&s_SampleGraphEditor);

	emRegisterFileType(s_SampleGraphEditor.default_type, "SampleGraph", s_SampleGraphEditor.editor_name);*/

#endif 
	return 0;
}




#include "AutoGen/GenericGraphEditor_h_ast.c"
