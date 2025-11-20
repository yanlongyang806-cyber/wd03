#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/


#ifndef NO_EDITORS

#include "EditorManager.h"
#include "AnimEditorCommon.h"
#include "MultiEditField.h"

typedef struct MEField MEField;
typedef struct UIExpander UIExpander;
typedef struct UIExpanderGroup UIExpanderGroup;
typedef struct UIGimmeButton UIGimmeButton;
typedef struct UILabel UILabel;
typedef struct UIWindow UIWindow;


typedef struct DynAnimTemplate DynAnimTemplate;
typedef struct DynAnimTemplateNode DynAnimTemplateNode;


#define ANIMTEMPLATE_EDITOR "Animation Template Editor"
#define DEFAULT_ANIMTEMPLATE_NAME     "New_Animation_Template"

// ---- Animation Template Editor ----

typedef struct ATEUndoData 
{
	DynAnimTemplate* pPreObject;
	DynAnimTemplate* pPostObject;
} ATEUndoData;

typedef struct AnimTemplateDoc
{
	EMEditorDoc emDoc;

	DynAnimTemplate* pOrigObject;
	DynAnimTemplate* pObject;
	DynAnimTemplate* pNextUndoObject;

	bool bIgnoreFieldChanges;
	bool bIgnoreFilenameChanges;
	bool bNeedsDisplayUpdate;
	bool bReflowing;
	bool bNeedsFlowchartReflow;
	bool bRemovingNode;
	bool bNeedsNodePanelReflow;
	bool bNeedsPropPanelReflow;
	bool bNeedsGraphPanelReflow;

	int iFocusIndex;
	int iOldFocusIndex;

	DynAnimTemplateNode** eaNodesToFree;

	// Standalone main window controls
	/*
	UIExpanderGroup* pExpanderGroup;
	UIExpander* pInfoExpander;
	UIExpander* pPropsExpander;
	*/


	// Panel UI elements

	EMPanel* pPropsPanel;
	UILabel* pFilenameLabel;
	UIGimmeButton* pFileButton;
	UIWidget **eaInactivePropPanelWidgets;

	EMPanel* pNodesPanel;
	UIWidget** eaInactiveNodesPanelWidgets;

	EMPanel* pGraphsPanel;
	UIWidget **eaInactiveGraphsPanelWidgets;

	EMPanel *pSearchPanel;

	// Info Expander
	/*
	UILabel* pTypeLabel;
	UILabel* pCommentLabel;
	MEField* pTypeField;
	MEField* pCommentField;
	*/

	// Main display area
	UIPane* pViewPane;
	UIFlowchart* pFlowchart;
	UIButton* pFlowchartViewHideButton;
	bool bFlowchartViewIsResizing;
	int iFlowchartViewResizingCanvasHeight;

	// Props Expander
	//InteractionPropertiesGroup* pPropsGroup;

	// Simple fields
	MEField** eaDocFields;
	MEField** eaNodeFields;
	MEField **eaPropFields;
	MEField **eaGraphFields;
	MoveMEField** eaMoveFields;
	PathMEField **eaPathFields;
} AnimTemplateDoc;

AnimTemplateDoc* ATEOpenAnimTemplate(EMEditor* pEditor, char* pcName, DynAnimTemplate *pTemplate);
void ATERevertAnimTemplate(AnimTemplateDoc* pDoc);
void ATECloseAnimTemplate(AnimTemplateDoc* pDoc);
EMTaskStatus ATESaveAnimTemplate(AnimTemplateDoc* pDoc, bool bSaveAsNew);
void ATEInitData(EMEditor* pEditor);
void ATEOncePerFrame(AnimTemplateDoc* pDoc);
void ATEDuplicateDoc(UIButton *button, UserData uData);
void ATEGotFocus(AnimTemplateDoc *pDoc);

#endif
