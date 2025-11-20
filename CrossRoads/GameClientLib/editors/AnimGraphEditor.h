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
typedef struct UIProgressBar UIProgressBar;
typedef struct UIWindow UIWindow;


typedef struct DynAnimGraph DynAnimGraph;
typedef struct DynAnimGraphNode DynAnimGraphNode;


#define ANIMGRAPH_EDITOR "Animation Graph Editor"
#define DEFAULT_ANIMGRAPH_NAME     "New_Animation_Graph"



// ---- Animation Graph Editor ----


typedef struct AGENodeData
{
	DynAnimGraphNode* pNode;
	UILabel** eaMoveLabels;
	UICheckButton **eaBiasButtons;
	UILabel *pInterpByMovementLabel;
	UILabel *pPostIdleLabel;
	UILabel* pBlendLabel;
	UIProgressBar* pBlendBar;
} AGENodeData;

typedef struct AGEUndoData 
{
	DynAnimGraph* pPreObject;
	DynAnimGraph* pPostObject;
} AGEUndoData;

typedef struct FlagFrame
{
	F32 fTime;
	const char* pcFlag;
} FlagFrame;

typedef struct AnimGraphDoc
{
	EMEditorDoc emDoc;

	DynAnimGraph* pOrigObject;
	DynAnimGraph* pObject;
	DynAnimGraph* pNextUndoObject;

	AnimEditor_CostumePickerData costumeData;

	bool bIgnoreFieldChanges;
	bool bIgnoreFilenameChanges;
	bool bNeedsDisplayUpdate;
	bool bReflowing;
	bool bNeedsFlowchartReflow;
	bool bRemovingNode;
	bool bNeedsNodePanelReflow;
	bool bNeedsPropsPanelReflow;
	bool bNeedsChartsPanelReflow;
	bool bCostumeAnimating;
	bool bHasPickedCostume;
	bool bForceGraphChange;
	bool bScrubTime;
	bool bReflowAnimation;
	bool bFxPlaybackDetached;

	int iFocusIndex;
	int iOldFocusIndex;
	F32 fNewScrubTime;

	DynAnimGraphNode** eaNodesToFree;

	AnimEditorPlaybackInfo playbackInfo;
	MEField* pPlaybackRateField;

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

	EMPanel* pNodesPanel;
	UIWidget** eaInactiveNodesPanelWidgets;
	MEField** eaNodesPanelFieldsToAutoUpdate;

	UIWidget **eaInactivePropPanelWidgets;

	EMPanel* pChartsPanel;
	UIWidget **eaInactiveChartsPanelWidgets;

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

	// Timeline
	UIPane* pTimelinePane;
	UITimeline* pTimeline;
	FlagFrame** eaFlagFrames;

	// Simple fields
	MEField** eaDocFields;
	MEField** eaNodeFields;
	MEField** eaPropFields;
	MEField** eaChartFields;
	MoveMEField** eaMoveFields;
	MoveFxMEField **eaMoveFxFields;
	PathMEField **eaPathFields;
} AnimGraphDoc;

AnimGraphDoc* AGEOpenAnimGraph(EMEditor* pEditor, char* pcName, DynAnimGraph *pGraph);
void AGERevertAnimGraph(AnimGraphDoc* pDoc);
void AGECloseAnimGraph(AnimGraphDoc* pDoc);
EMTaskStatus AGESaveAnimGraph(AnimGraphDoc* pDoc, bool bSaveAsNew);
void AGEInitData(EMEditor* pEditor);
void AGEDrawGhosts(AnimGraphDoc* pDoc);
void AGEOncePerFrame(AnimGraphDoc* pDoc);
void AGEGotFocus(AnimGraphDoc *pDoc);
void AGELostFocus(void);
void AGEDuplicateDoc(UIButton* button, UserData uData);

#endif
