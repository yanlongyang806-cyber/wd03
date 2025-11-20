/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef NO_EDITORS

#include "AnimTemplateEditor.h"
#include "dynAnimTemplate.h"
#include "dynAnimGraph.h"
#include "dynFxDebug.h"


#include "winutil.h"
#include "Color.h"
#include "Error.h"
#include "StringCache.h"
#include "UIGimmeButton.h"
#include "EditorPrefs.h"
#include "inputMouse.h"
#include "UISeparator.h"
#include "GfxTexAtlas.h"
#include "file.h"
#include "EditorManagerUtils.h"
#include "EditorManagerUIToolbars.h"
#include "gimmeDLLWrapper.h"
#include "dynFxManager.h"
#include "dynFxInfo.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

#define DEFAULT_DOC_NAME "New_AnimTemplate"

#define EPSILON 0.0001f

#define DEFAULT_NODE_WIDTH 300
#define DEFAULT_START_NODE_WIDTH 100
#define NODE_MARGINS 5.0f

//-----------------------------------------------------------------------------------
// Global Data
//-----------------------------------------------------------------------------------

static int ateNewNameCount = 0;

static bool gInitializedEditor = false;
static bool gInitializedEditorData = false;
static bool gIndexChanged = false;

static char** geaScopes = NULL;

static UISkin* gBoldExpanderSkin;

static UISkin ateViewPaneSkin;

typedef enum eATESkinType
{
	eATENormal,
	eATERandomizer,
	eATEStart,
	eATEEnd,
	eATEUnconnected,
	eATEUnconnected1,
	eATECount,
} eATESkinType;

UISkin ateNodeSkin[ eATECount ];

// Node skin colors based on info
static const Color ateNodeTypeColor[eATECount] = {
	{ 192, 192, 192, 255 },		// eATENormal
	{ 240, 240, 128, 255 },		// eATERandomizer
	{ 128, 240, 128, 255 },		// eATEStart
	{ 128, 128, 240, 255 },		// eATEEnd
	{ 255, 000, 000, 255 },		// eATEUnconnected
	{ 255, 255, 000, 255 },		// eATEUnconnected1
};


//---------------------------------------------------------------------------------------------------
// Function Prototypes and type definitions
//---------------------------------------------------------------------------------------------------

#define X_OFFSET_BASE    15
#define X_OFFSET_CONTROL 125

#define X_OFFSET_INTERACT_BASE2    20
#define X_OFFSET_INTERACT_CONTROL  135
#define X_OFFSET_INTERACT_CONTROL2 155
#define X_OFFSET_INTERACT_CONTROL3 175

#define STANDARD_ROW_HEIGHT	26
#define LABEL_ROW_HEIGHT	20
#define SEPARATOR_HEIGHT	11

#define ATE_TABBAR_HEIGHT 32

static void ATEFieldChangedCB(MEField* pField, bool bFinished, AnimTemplateDoc* pDoc);
static bool ATEFieldPreChangeCB(MEField* pField, bool bFinished, AnimTemplateDoc* pDoc);
static void ATEAnimTemplateChanged(AnimTemplateDoc* pDoc, bool bUndoable, bool bFlowchart, bool bPropsPanel, bool bNodesPanel, bool bGraphsPanel);
static void ATETemplatePreSaveFixup(DynAnimTemplate* pTemplate);
static void ATEUpdateDisplay(AnimTemplateDoc* pDoc, bool bImmediate, bool bFlowchart, bool bPropsPanel, bool bNodesPanel, bool bGraphsPanel);
static EMPanel* ATEInitNodesPanel(AnimTemplateDoc* pDoc);
static EMPanel* ATEInitPropertiesPanel(AnimTemplateDoc* pDoc);
static EMPanel* ATEInitGraphsPanel(AnimTemplateDoc *pDoc);
static EMPanel* ATEInitSearchPanel(AnimTemplateDoc *pDoc);

//---------------------------------------------------------------------------------------------------
// Searching
//---------------------------------------------------------------------------------------------------

static void ATERefreshSearchPanel(UIButton *pButton, UserData uData)
{
	AnimTemplateDoc *pDoc = (AnimTemplateDoc*)emGetActiveEditorDoc();

	if (pDoc->pSearchPanel)
	{
		eaFindAndRemove(&pDoc->emDoc.em_panels, pDoc->pSearchPanel);
		emPanelFree(pDoc->pSearchPanel);
	}
	pDoc->pSearchPanel = ATEInitSearchPanel(pDoc);
	eaPush(&pDoc->emDoc.em_panels, pDoc->pSearchPanel);
	emRefreshDocumentUI();
}

static void ATESearchTextChanged(UITextEntry *pEntry, UserData uData)
{
	if (pEntry) {
		AnimTemplateDoc *pDoc = (AnimTemplateDoc*)emGetActiveEditorDoc();
		AnimEditor_SearchText = allocAddString(ui_TextEntryGetText(pEntry));
		if (!strlen(AnimEditor_SearchText))
			AnimEditor_SearchText = NULL;
		ATERefreshSearchPanel(NULL, uData);
	}
}


//---------------------------------------------------------------------------------------------------
// Data Manipulation
//---------------------------------------------------------------------------------------------------

static void ATEAnimTemplateUndoCB(AnimTemplateDoc* pDoc, ATEUndoData* pData)
{
	// Put the undo def into the editor
	StructDestroy(parse_DynAnimTemplate, pDoc->pObject);
	pDoc->pObject = StructClone(parse_DynAnimTemplate, pData->pPreObject);
	if (pDoc->pNextUndoObject)
		StructDestroy(parse_DynAnimTemplate, pDoc->pNextUndoObject);
	pDoc->pNextUndoObject = StructClone(parse_DynAnimTemplate, pDoc->pObject);

	// Update the UI
	ATEAnimTemplateChanged(pDoc, false, true, true, true, true);
}


static void ATEAnimTemplateRedoCB(AnimTemplateDoc* pDoc, ATEUndoData* pData)
{
	// Put the undo def into the editor
	StructDestroy(parse_DynAnimTemplate, pDoc->pObject);
	pDoc->pObject = StructClone(parse_DynAnimTemplate, pData->pPostObject);
	if (pDoc->pNextUndoObject)
		StructDestroy(parse_DynAnimTemplate, pDoc->pNextUndoObject);
	pDoc->pNextUndoObject= StructClone(parse_DynAnimTemplate, pDoc->pObject);

	// Update the UI
	ATEAnimTemplateChanged(pDoc, false, true, true, true, true);
}


static void ATEAnimTemplateUndoFreeCB(AnimTemplateDoc* pDoc, ATEUndoData* pData)
{
	// Free the memory
	StructDestroy(parse_DynAnimTemplate, pData->pPreObject);
	StructDestroy(parse_DynAnimTemplate, pData->pPostObject);
	free(pData);
}


static void ATEIndexChangedCB(void* unused)
{
	if (gIndexChanged)
	{
		gIndexChanged = false;
		resGetUniqueScopes(hAnimTemplateDict, &geaScopes);
	}
}


static void ATEContentDictChanged(enumResourceEventType eType, const char* pDictName, const char* pcName, Referent pReferent, void* pUserData)
{
	if ((eType == RESEVENT_INDEX_MODIFIED) && !gIndexChanged)
	{
		gIndexChanged = true;
		emQueueFunctionCall(ATEIndexChangedCB, NULL);
	}
}


//---------------------------------------------------------------------------------------------------
// FlowChart Editing UI
//---------------------------------------------------------------------------------------------------

// Called by the flowchart widget every time a link is added --
// returns if the link is allowed.
bool ATEFlowchartLinkRequest( UIFlowchart* pFlowchart, UIFlowchartButton* pSource, UIFlowchartButton* pDest, bool bForce, AnimTemplateDoc* pDoc )
{
	//if (pDoc && emDocIsEditable(&pDoc->emDoc, true))
	{

		DynAnimTemplateNode* pSourceNode = (DynAnimTemplateNode*)pSource->node->userData;
		DynAnimTemplateNode* pDestNode = (DynAnimTemplateNode*)pDest->node->userData;

		if (pDest->output)
		{
			return false;
		}
		/*
		Mated2Node* sourceNode = (Mated2Node*)source->node->userData;
		Mated2Node* destNode = (Mated2Node*)dest->node->userData;

		if( !force )
		{
		if( source->node == dest->node )
		{
		return false;
		}
		}

		// Point of NO RETURN!	The link has been accepted!
		destNode->needsReflow = true;
		if( !mated2IsLoading( doc ))
		{
		ShaderInputEdge* edge = StructCreate( parse_ShaderInputEdge );
		mated2SetEdgeFlowchartLink( edge, source, dest );
		eaPush( &mated2NodeShaderOp(destNode)->inputs, edge );

		{
		Mated2FlowchartLinkAction* accum = calloc( 1, sizeof( *accum ));
		accum->sourceNodeName = strdup( mated2NodeName( sourceNode ));
		accum->sourceName = strdup( edge->input_source_output_name );
		accum->destNodeName = strdup( mated2NodeName( destNode ));
		accum->destName = strdup( edge->input_name );
		memcpy( accum->swizzle, edge->input_swizzle, sizeof( accum->swizzle ));

		mated2UndoRecord( doc, mated2FlowchartLinkActionUnlink,
		mated2FlowchartLinkActionLink, mated2FlowchartLinkActionFree,
		accum );
		}

		mated2SetDirty( doc );
		}


		*/
		return true;
	}
	//return false;
}

bool ATEFlowchartUnlinkRequest( UIFlowchart* pFlowchart, UIFlowchartButton* pSource, UIFlowchartButton* pDest, bool bForce, AnimTemplateDoc* pDoc )
{
	if (pDoc) {
		DynAnimTemplateNode* pSourceNode = pSource->node->userData;
		DynAnimTemplateNode* pDestNode = pDest->node->userData;
		DynAnimTemplateNodeRef* pRef = pSource->userData;

		if (pRef->p == pDestNode)
			pRef->p = NULL;
		else
			Errorf("Trying to unlink nodes that aren't linked?");

		if (!pDoc->bRemovingNode)
			ATEAnimTemplateChanged(pDoc, true, true, false, false, false);
		return true;
	}

	return false;
}

// Called by the flowchart widget ONCE every time a link add is
// requested.  This will be the first call.
bool ATEFlowchartLinkBegin( UIFlowchart* pFlowchart, UIFlowchartButton* pSource, UIFlowchartButton* pDest, bool bForce, AnimTemplateDoc* pDoc )
{
	return true;
}

// Called by the flowchart widget ONCE every time a link add is
// requested.  This will be the last call.
bool ATEFlowchartLinkEnd( UIFlowchart* pFlowchart, UIFlowchartButton* pSource, UIFlowchartButton* pDest, bool bLinked, AnimTemplateDoc* pDoc )
{
	if (pDoc) {
		if (!pDoc->bReflowing)
		{
			DynAnimTemplateNode* pSourceNode = pSource->node->userData;
			DynAnimTemplateNode* pDestNode = pDest->node->userData;
			DynAnimTemplateNodeRef* pRef = pSource->userData;

			pRef->p = pDestNode;

			ATEAnimTemplateChanged(pDoc, true, true, false, false, false);
		}
		return true;
	}
	return false;
}

// Called by the flowchart widget every time a node is requested to
// be removed, before its links are broken.
static bool ATEFlowchartNodeRemoveRequest( UIFlowchart* pFlowchart, UIFlowchartNode* pUINode, AnimTemplateDoc* pDoc )
{
	if (pDoc) {
		DynAnimTemplateNode* pNode = pUINode->userData;

		if (pNode->eType == eAnimTemplateNodeType_Start)
			return false;

		pDoc->bRemovingNode = true;
		return true;
	}
	else
	{
		return false;
	}
}

static void ATERemoveNode(DynAnimTemplateNode* pNode, AnimTemplateDoc* pDoc)
{
	int iNodeIndex = eaFind(&pDoc->pObject->eaNodes, pNode);
	assert(iNodeIndex >= 0);

	dynAnimTemplateFreeNode(pDoc->pObject, pDoc->pObject->eaNodes[iNodeIndex]);
	eaRemove(&pDoc->pObject->eaNodes, iNodeIndex);
	//eaRemove(&pDoc->pObject->pDefaultsGraph->eaNodes, iNodeIndex); Already handled in the free node routine
	pDoc->bRemovingNode = false;
	ATEAnimTemplateChanged(pDoc, true, true, false, true, false);
}

// Called by the flowchart widget every time a node is requested to
// be removed, after its links are broken.
static bool ATEFlowchartNodeRemoved( UIFlowchart* pFlowchart, UIFlowchartNode* pUINode, AnimTemplateDoc* pDoc )
{
	DynAnimTemplateNode* pNode = pUINode->userData;
	eaPush(&pDoc->eaNodesToFree, pNode);

	ui_WidgetForceQueueFree( UI_WIDGET( pUINode ));
	return true;
}

// Hide or show the node view window.
static void ATEFlowchartViewHideShowClicked( UIButton* ignored, AnimTemplateDoc* pDoc )
{
	F32 canvasX, canvasY, canvasWidth, canvasHeight;
	emGetCanvasSize( &canvasX, &canvasY, &canvasWidth, &canvasHeight );

	ui_WidgetRemoveChild( UI_WIDGET( pDoc->pViewPane ), UI_WIDGET( pDoc->pFlowchartViewHideButton ));
	ui_WidgetRemoveChild( UI_WIDGET( pDoc->pViewPane ), UI_WIDGET( pDoc->pFlowchartViewHideButton ));

	if( pDoc->pViewPane->widget.height < canvasHeight - ATE_TABBAR_HEIGHT )
	{
		ui_WidgetSetHeightEx( UI_WIDGET( pDoc->pViewPane ), canvasHeight - ATE_TABBAR_HEIGHT, UIUnitFixed );
		ui_WidgetAddChild( UI_WIDGET( pDoc->pViewPane ), UI_WIDGET( pDoc->pFlowchartViewHideButton ));
	}
	else
	{
		ui_WidgetSetHeightEx( UI_WIDGET( pDoc->pViewPane ), (canvasHeight - ATE_TABBAR_HEIGHT) / 2, UIUnitFixed );
		ui_WidgetAddChild( UI_WIDGET( pDoc->pViewPane ), UI_WIDGET( pDoc->pFlowchartViewHideButton ));
	}
}

static void ATEFlowchartNodeFocusCB(UIFlowchartNode* pUINode, AnimTemplateDoc* pDoc)
{
	DynAnimTemplateNode* pNode = pUINode->userData;
	pDoc->iOldFocusIndex = pDoc->iFocusIndex;
	pDoc->iFocusIndex = eaFind(&pDoc->pObject->eaNodes, pNode);

	if (!pDoc->bReflowing && pDoc->iFocusIndex != pDoc->iOldFocusIndex)
		ATEUpdateDisplay(pDoc, false, false, false, true, false);
}

static void ATEFlowchartNodeUnfocusCB(UIFlowchartNode* pUINode, AnimTemplateDoc* pDoc)
{
	/*
	DynAnimTemplateNode* pNode = pUINode->userData;
	if (pDoc->iFocusIndex == eaFind(&pDoc->pObject->eaNodes, pNode))
		pDoc->iFocusIndex = -1;
	if (!pDoc->bReflowing)
		ATEUpdateDisplay(pDoc, false);
		*/
}

static void ATEFlowchartNodeMovedCB(UIFlowchartNode* pUINode, AnimTemplateDoc* pDoc)
{
	if (pDoc)
	{
		DynAnimTemplateNode* pNode = pUINode->userData;
		if (pNode)
		{
			F32 fX = ui_WidgetGetX( UI_WIDGET(pUINode) );
			F32 fY = ui_WidgetGetY( UI_WIDGET(pUINode) );
			if (fabsf(pNode->fX - fX) > 5.0f || fabsf(pNode->fY - fY) > 5.0f)
			{
				pNode->fX = fX;
				pNode->fY = fY;
				ATEAnimTemplateChanged(pDoc, true, true, false, false, false);
			} else {
				ui_WidgetSetPosition(UI_WIDGET(pUINode), pNode->fX, pNode->fY);
			}
		}
	}
	else
	{
		ATEAnimTemplateChanged(pDoc, false, true, false, false, false);
	}
}



// Expand the node view window.
static void ATEFlowchartViewExpandDrag( UIButton* button, AnimTemplateDoc* pDoc )
{
	int x, y;
	F32 canvasX, canvasY, canvasWidth, canvasHeight;
	mousePos( &x, &y );
	emGetCanvasSize( &canvasX, &canvasY, &canvasWidth, &canvasHeight );

	ui_DragStart( UI_WIDGET( button ), "ate_flowchart_view_expand", NULL, atlasLoadTexture( "eui_pointer_arrows_vert" ));
	pDoc->bFlowchartViewIsResizing = true;
	pDoc->iFlowchartViewResizingCanvasHeight = canvasHeight;
}

// Overriding Tick function for the editor pane.
static void ATEFlowchartPaneTick( UIPane* pane, UI_PARENT_ARGS )
{
	AnimTemplateDoc* pDoc = (AnimTemplateDoc*)emGetActiveEditorDoc();
	bool bConnectingShouldFinish = false;
	// deal with resizing
	if( pDoc && pDoc->bFlowchartViewIsResizing )
	{
		if( mouseIsDown( MS_LEFT ))
		{
			int mouseX, mouseY;
			F32 canvasX, canvasY, canvasWidth, canvasHeight;
			int maxHeight = pDoc->iFlowchartViewResizingCanvasHeight - ATE_TABBAR_HEIGHT;
			int newHeight;
			mousePos( &mouseX, &mouseY );
			emGetCanvasSize( &canvasX, &canvasY, &canvasWidth, &canvasHeight );

			newHeight = MIN( MAX( pDoc->iFlowchartViewResizingCanvasHeight - (mouseY - canvasY),
				100 ),
				pDoc->iFlowchartViewResizingCanvasHeight - ATE_TABBAR_HEIGHT );

			ui_WidgetSetHeight( UI_WIDGET( pDoc->pViewPane ), newHeight );

			ui_WidgetRemoveChild( UI_WIDGET( pDoc->pViewPane ), UI_WIDGET( pDoc->pFlowchartViewHideButton ));
			ui_WidgetRemoveChild( UI_WIDGET( pDoc->pViewPane ), UI_WIDGET( pDoc->pFlowchartViewHideButton ));
			if( newHeight == maxHeight )
				ui_WidgetAddChild( UI_WIDGET( pDoc->pViewPane ), UI_WIDGET( pDoc->pFlowchartViewHideButton ));
			else
				ui_WidgetAddChild( UI_WIDGET( pDoc->pViewPane ), UI_WIDGET( pDoc->pFlowchartViewHideButton ));


		}
		else
			pDoc->bFlowchartViewIsResizing = false;
	}

	if( pDoc && pDoc->pViewPane == pane && pDoc->pFlowchart->connecting && (mouseDown( MS_LEFT ) || mouseDown( MS_MID ) || mouseDown( MS_RIGHT )) )
		bConnectingShouldFinish = true;

	ui_PaneTick( pane, UI_PARENT_VALUES );

	if( bConnectingShouldFinish && pDoc && pDoc->pViewPane == pane)
	{
		pDoc->pFlowchart->connecting = NULL;
	}

	ui_PaneTick( pane, UI_PARENT_VALUES );
}

static UIFlowchartNode* ATEFlowchartNodeCreateFromDynAnimTemplateNode(DynAnimTemplateNode* pNode, AnimTemplateDoc* pDoc, int iNodeIndex)
{
	UIFlowchartButton** eaInputButtons = NULL;
	UIFlowchartButton** eaOutputButtons = NULL;
	UIFlowchartNode* pUINode;


	if (pNode->eType != eAnimTemplateNodeType_Start)
	{
		UIFlowchartButton* inputButton = ui_FlowchartButtonCreate( pDoc->pFlowchart, UIFlowchartNormal, ui_LabelCreate( pNode->eType == eAnimTemplateNodeType_End?"Exit Animation":"In", 0, 0 ), NULL );
		ui_WidgetSetTooltipString( UI_WIDGET( inputButton ), "Input" );

		ui_FlowchartButtonSetSingleConnection( inputButton, false );
		eaPush( &eaInputButtons, inputButton );
	}

	if (pNode->eType != eAnimTemplateNodeType_End)
	{
		UIFlowchartButton* outputButton;

		if (pNode->eType == eAnimTemplateNodeType_Start ||
			pNode->eType == eAnimTemplateNodeType_Normal)
		{
			outputButton = ui_FlowchartButtonCreate( pDoc->pFlowchart, UIFlowchartNormal, ui_LabelCreate( "Default", 0, 0 ), &pNode->defaultNext );
			ui_FlowchartButtonSetSingleConnection( outputButton, true );
			eaPush( &eaOutputButtons, outputButton );

			FOR_EACH_IN_EARRAY_FORWARDS(pNode->eaSwitch, DynAnimTemplateSwitch, pSwitch)
			{
				outputButton = ui_FlowchartButtonCreate( pDoc->pFlowchart, UIFlowchartNormal, ui_LabelCreate( pSwitch->pcFlag, 0, 0 ), &pSwitch->next );
				ui_FlowchartButtonSetSingleConnection( outputButton, true );
				eaPush( &eaOutputButtons, outputButton );
			}
			FOR_EACH_END;
		}

		if (pNode->eType == eAnimTemplateNodeType_Randomizer)
		{
			FOR_EACH_IN_EARRAY_FORWARDS(pNode->eaPath, DynAnimTemplatePath, pPath)
			{
				char cPathName[16];
				sprintf(cPathName, "Path %d", ipPathIndex+1);
				outputButton = ui_FlowchartButtonCreate( pDoc->pFlowchart, UIFlowchartNormal, ui_LabelCreate( cPathName, 0, 0 ), &pPath->next );
				ui_FlowchartButtonSetSingleConnection( outputButton, true );
				eaPush( &eaOutputButtons, outputButton );
			}
			FOR_EACH_END;
		}
	}

	{
		eATESkinType eType = eATENormal;
		switch (pNode->eType)
		{
			xcase eAnimTemplateNodeType_Start:
			{
				eType = eATEStart;
			}
			xcase eAnimTemplateNodeType_End:
			{
				eType = eATEEnd;
			}
			xcase eAnimTemplateNodeType_Normal:
			{
				eType = eATENormal;
			}
			xcase eAnimTemplateNodeType_Randomizer:
			{
				eType = eATERandomizer;
			}
		}

		pUINode = ui_FlowchartNodeCreate( pNode->pcName, pNode->fX, pNode->fY, pNode->eType == eAnimTemplateNodeType_Normal?DEFAULT_NODE_WIDTH:DEFAULT_START_NODE_WIDTH, 100, &eaInputButtons, &eaOutputButtons, pNode );

		if (!dynAnimTemplateNodeConnected(pDoc->pObject, pNode))
			eType = eATEUnconnected;

		ui_WidgetSkin( UI_WIDGET( pUINode ), &ateNodeSkin[eType] );
		FOR_EACH_IN_EARRAY(pUINode->outputButtons, UIFlowchartButton, pButton)
		{
			DynAnimTemplateNodeRef* pRef = pButton->userData;
			if (!pRef->p)
				ui_WidgetSkin( UI_WIDGET( pButton ), &ateNodeSkin[eATEUnconnected1] );
		}
		FOR_EACH_END;
	}
	//pUINode->widget.tickF = mated2FlowchartNodeTick;
	//ui_WindowSetClickedCallback( UI_WINDOW( pUINode ), (UIActivationFunc)mated2FlowchartNodeClicked, pDoc );
	//ui_WindowSetShadedCallback( UI_WINDOW( pUINode ), mated2FlowchartNodeShaded, doc );
	//ui_WidgetSetFreeCallback( UI_WIDGET( pUINode ), ATENodeFree );
	ui_WidgetSetFocusCallback( UI_WIDGET( pUINode ), ATEFlowchartNodeFocusCB, pDoc);
	ui_WindowSetRaisedCallback( (UIWindow*)pUINode, ATEFlowchartNodeFocusCB, pDoc);
	ui_WidgetSetUnfocusCallback( UI_WIDGET( pUINode ), ATEFlowchartNodeUnfocusCB, pDoc);
	ui_WindowSetMovedCallback( UI_WINDOW( pUINode ), ATEFlowchartNodeMovedCB, pDoc);
	ui_WindowSetResizable( UI_WINDOW( pUINode ), false );
	if (pNode->eType == eAnimTemplateNodeType_Start)
		ui_WindowSetClosable( UI_WINDOW( pUINode), false );
	ui_FlowchartNodeSetAutoResize( pUINode, true );

	{
		int iY = 0;
		int iX = NODE_MARGINS;
		int iMaxX = 0;
		
		if (SAFE_MEMBER(pNode,bInterruptible))
		{
			UILabel* pLabel = ui_LabelCreate("Interruptible by Movement", iX, iY);
			pLabel->widget.widthUnit = UIUnitFitContents;
			ui_FlowchartNodeAddChild(pUINode, UI_WIDGET(pLabel), false );
			iY += ui_WidgetGetHeight(UI_WIDGET(pLabel));
			MAX1(iMaxX, ui_WidgetGetWidth(UI_WIDGET(pLabel)));
		}

		ui_WidgetSetHeight( UI_WIDGET( pUINode->beforePane ), iY );
		iMaxX += NODE_MARGINS;
		if (iMaxX > DEFAULT_NODE_WIDTH) {
			ui_WidgetSetWidth( UI_WIDGET( pUINode ), iMaxX );
		}
	}

	ui_FlowchartAddNode(pDoc->pFlowchart, pUINode);
	return pUINode;
}

static void ATEAddNodeByTypeCallback(AnimTemplateDoc *pDoc, eAnimTemplateNodeType newNodeType)
{
	if (pDoc) {
		//variables
		DynAnimTemplateNode *pTemplateNode;
		DynAnimGraphNode *pGraphNode;
		int iNodeIndex;

		//create and add the node
		pTemplateNode = StructCreate(parse_DynAnimTemplateNode);
		pGraphNode    = StructCreate(parse_DynAnimGraphNode);
		iNodeIndex = eaPush(&pDoc->pObject->eaNodes, pTemplateNode);
		eaPush(&pDoc->pObject->pDefaultsGraph->eaNodes, pGraphNode);

		//set the basic node data
		pTemplateNode->eType = newNodeType;
		pGraphNode->fX = pTemplateNode->fX = pDoc->pObject->eaNodes[0]->fX + 50.0f;
		pGraphNode->fY = pTemplateNode->fY = pDoc->pObject->eaNodes[0]->fY + 50.0f;
		pGraphNode->pTemplateNode = pTemplateNode;

		//set the node name
		{
			char cTempName[32];
			sprintf(cTempName, "New Node %d", eaSize(&pDoc->pObject->eaNodes) - 2);
			pGraphNode->pcName = pTemplateNode->pcName = allocAddString(cTempName);
		}

		//randomizer specific setup
		if (newNodeType == eAnimTemplateNodeType_Randomizer)
		{
			//vars to add paths
			DynAnimTemplatePath *pTemplatePath1, *pTemplatePath2;
			DynAnimGraphPath *pGraphPath1, *pGraphPath2;

			//create and add two paths
			pTemplatePath1 = StructCreate(parse_DynAnimTemplatePath);
			pTemplatePath2 = StructCreate(parse_DynAnimTemplatePath);
			pGraphPath1 = StructCreate(parse_DynAnimGraphPath);
			pGraphPath2 = StructCreate(parse_DynAnimGraphPath);
			eaPush(&pTemplateNode->eaPath, pTemplatePath1);
			eaPush(&pTemplateNode->eaPath, pTemplatePath2);
			eaPush(&pGraphNode->eaPath, pGraphPath1);
			eaPush(&pGraphNode->eaPath, pGraphPath2);

			//set the basic path data
			pGraphPath1->fChance = 0.5f;
			pGraphPath2->fChance = 0.5f;
		}

		//update the UI
		{
			UIFlowchartNode* pUINode = ATEFlowchartNodeCreateFromDynAnimTemplateNode(pTemplateNode, pDoc, iNodeIndex);
			ui_SetFocus(pUINode);
		}
		ATEAnimTemplateChanged(pDoc, true, true, false, true, false);
	}
}

static void ATEAddExitNodeCallback( UIButton *ignored, AnimTemplateDoc *pDoc )
{
	ATEAddNodeByTypeCallback(pDoc, eAnimTemplateNodeType_End);
}

static void ATEAddNormalNodeCallback( UIButton* ignored, AnimTemplateDoc* pDoc )
{
	ATEAddNodeByTypeCallback(pDoc, eAnimTemplateNodeType_Normal);
}

static void ATEAddRandomizerNodeCallback( UIButton *ignored, AnimTemplateDoc *pDoc)
{
	ATEAddNodeByTypeCallback(pDoc, eAnimTemplateNodeType_Randomizer);
}

static void ATEReconnectNodes( AnimTemplateDoc* pDoc, DynAnimTemplateNode* pNode, DynAnimTemplateNodeRef* pRef ) 
{
	if (pNode && pRef->p)
	{
		// Find source and dest nodes
		UIFlowchartButton* pSource = NULL;
		UIFlowchartButton* pDest = NULL;
		FOR_EACH_IN_EARRAY(pDoc->pFlowchart->nodeWindows, UIFlowchartNode, pFlowNode)
		{
			if ((DynAnimTemplateNode*)pFlowNode->userData == pNode)
			{
				FOR_EACH_IN_EARRAY(pFlowNode->outputButtons, UIFlowchartButton, pButton)
				{
					if (pButton->userData == pRef)
					{
						pSource = pButton;
						break;
					}
				}
				FOR_EACH_END;
			}
			if ((DynAnimTemplateNode*)pFlowNode->userData == pRef->p)
			{
				pDest = pFlowNode->inputButtons[0];
			}
		}
		FOR_EACH_END;
		assert(pSource && pDest);
		ui_FlowchartLink(pSource, pDest);
	}
}

static void ATEReflowFlowchart(AnimTemplateDoc* pDoc)
{
	void* pConnectingRef = NULL;
	pDoc->bReflowing = true;
	if (pDoc->pFlowchart->connecting)
	{
		pConnectingRef = pDoc->pFlowchart->connecting->userData;
	}
	ui_FlowchartClear(pDoc->pFlowchart);
	FOR_EACH_IN_EARRAY_FORWARDS(pDoc->pObject->eaNodes, DynAnimTemplateNode, pNode)
	{
		ATEFlowchartNodeCreateFromDynAnimTemplateNode(pNode, pDoc, ipNodeIndex);
	}
	FOR_EACH_END;
	FOR_EACH_IN_EARRAY_FORWARDS(pDoc->pObject->eaNodes, DynAnimTemplateNode, pNode)
	{
		ATEReconnectNodes(pDoc, pNode, &pNode->defaultNext);

		FOR_EACH_IN_EARRAY(pNode->eaSwitch, DynAnimTemplateSwitch, pSwitch)
		{
			ATEReconnectNodes(pDoc, pNode, &pSwitch->next);
		}
		FOR_EACH_END;

		FOR_EACH_IN_EARRAY(pNode->eaPath, DynAnimTemplatePath, pPath)
		{
			ATEReconnectNodes(pDoc, pNode, &pPath->next);
		}
		FOR_EACH_END;

		if (ipNodeIndex == pDoc->iFocusIndex)
		{
			FOR_EACH_IN_EARRAY(pDoc->pFlowchart->nodeWindows, UIFlowchartNode, pUINode)
			{
				if (pUINode->userData == pNode)
					ui_WidgetGroupSteal( UI_WIDGET(pUINode)->group, UI_WIDGET(pUINode) );
			}
			FOR_EACH_END;
		}
	}
	FOR_EACH_END;
	if (pConnectingRef)
	{
		pDoc->pFlowchart->connecting = NULL;
		FOR_EACH_IN_EARRAY(pDoc->pFlowchart->nodeWindows, UIFlowchartNode, pNode)
		{
			FOR_EACH_IN_EARRAY(pNode->outputButtons, UIFlowchartButton, pButton)
			{
				if (pButton->userData == pConnectingRef)
					pDoc->pFlowchart->connecting = pButton;
			}
			FOR_EACH_END;
		}
		FOR_EACH_END;
	}
	pDoc->bReflowing = false;
}

//---------------------------------------------------------------------------------------------------
// UI Logic
//---------------------------------------------------------------------------------------------------

static void ATEAddFieldToParent(MEField* pField, UIWidget* pParent, F32 x, F32 y, F32 xPercent, F32 w, UIUnitType wUnit, F32 padRight, AnimTemplateDoc* pDoc)
{
	MEFieldAddToParent(pField, pParent, x, y);
	ui_WidgetSetPositionEx(pField->pUIWidget, x, y, xPercent, 0, UITopLeft);
	ui_WidgetSetWidthEx(pField->pUIWidget, w, wUnit);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, padRight, 0, 0);
	MEFieldSetChangeCallback(pField, ATEFieldChangedCB, pDoc);
	MEFieldSetPreChangeCallback(pField, ATEFieldPreChangeCB, pDoc);
}


static UIExpander* ATECreateExpander(UIExpanderGroup* pExGroup, const char* pcName, int index)
{
	UIExpander* pExpander = ui_ExpanderCreate(pcName, 0);
	ui_WidgetSkin(UI_WIDGET(pExpander), gBoldExpanderSkin);
	ui_ExpanderGroupInsertExpander(pExGroup, pExpander, index);
	ui_ExpanderSetOpened(pExpander, 1);

	return pExpander;
}


// This is called whenever any def data changes to do cleanup
static void ATEAnimTemplateChanged(AnimTemplateDoc* pDoc, bool bUndoable, bool bFlowchart, bool bPropsPanel, bool bNodesPanel, bool bGraphsPanel)
{
	if (!pDoc->bIgnoreFieldChanges)
	{
		ATEUpdateDisplay(pDoc, false, bFlowchart, bPropsPanel, bNodesPanel, bGraphsPanel);

		if (bUndoable)
		{
			ATEUndoData* pData = calloc(1, sizeof(ATEUndoData));
			pData->pPreObject = pDoc->pNextUndoObject;
			pData->pPostObject = StructClone(parse_DynAnimTemplate, pDoc->pObject);
			EditCreateUndoCustom(pDoc->emDoc.edit_undo_stack, ATEAnimTemplateUndoCB, ATEAnimTemplateRedoCB, ATEAnimTemplateUndoFreeCB, pData);
			pDoc->pNextUndoObject = StructClone(parse_DynAnimTemplate, pDoc->pObject);
		}
	}
}


// This is called by MEField prior to allowing an edit
static bool ATEFieldPreChangeCB(MEField* pField, bool bFinished, AnimTemplateDoc* pDoc)
{
	return true;
}


// This is called when an MEField is changed
static void ATEFieldChangedCB(MEField* pField, bool bFinished, AnimTemplateDoc* pDoc)
{
	ATEAnimTemplateChanged(pDoc, bFinished, true, false, false, false);
}

static void ATEInheritFieldChangedCB(MEField *pField, bool bFinished, AnimTemplateDoc *pDoc)
{
	ATEAnimTemplateChanged(pDoc, bFinished, true, true, true, true);
}

static void ATERemoveSwitch(UIButton* pButton, DynAnimTemplateSwitch* pSwitch)
{
	AnimTemplateDoc* pDoc = (AnimTemplateDoc*)emGetActiveEditorDoc();
	if (pDoc && pSwitch) {
		FOR_EACH_IN_EARRAY(pDoc->pObject->eaNodes, DynAnimTemplateNode, pNode)
		{
			int iIndex = eaFind(&pNode->eaSwitch, pSwitch);
			if (iIndex >= 0)
			{
				eaRemove(&pNode->eaSwitch, iIndex);
				StructDestroy(parse_DynAnimTemplateSwitch, pSwitch);
				ATEAnimTemplateChanged(pDoc, true, true, false, true, false);
				return;
			}
		}
		FOR_EACH_END;
	}
}

static void ATEAddSwitch(UIButton* pButton, AnimTemplateDoc* pDoc)
{
	if (pDoc) {
		DynAnimTemplateNode* pEditNode = pDoc->pObject->eaNodes[pDoc->iFocusIndex];
		DynAnimTemplateSwitch* pSwitch = StructCreate(parse_DynAnimTemplateSwitch);
		pSwitch->pcFlag = allocAddString("NewSwitch");
		pSwitch->bInterrupt = true;
		eaPush(&pEditNode->eaSwitch, pSwitch);
		ATEAnimTemplateChanged(pDoc, true, true, false, true, false);
	}
}

static void ATERemoveSuppress(UIButton* pButton, DynAnimGraphSuppress* pSuppress)
{
	AnimTemplateDoc* pDoc = (AnimTemplateDoc*)emGetActiveEditorDoc();
	if (pDoc) {
		bool bFound = false;
		assert(pDoc);

		FOR_EACH_IN_EARRAY(pDoc->pObject->pDefaultsGraph->eaSuppress, DynAnimGraphSuppress, pWalk)
		{
			if (pSuppress == pWalk)
			{
				eaRemove(&pDoc->pObject->pDefaultsGraph->eaSuppress, ipWalkIndex);
				StructDestroy(parse_DynAnimGraphSuppress, pWalk);

				bFound = true;
				break;
			}
		}
		FOR_EACH_END;

		assert(bFound);
		ATEAnimTemplateChanged(pDoc, true, true, true, false, false);
	}
}

static void ATEAddDocSuppress(UIButton* pButton, AnimTemplateDoc *pDoc)
{
	if (pDoc) {
		DynAnimGraphSuppress* pSuppress = StructCreate(parse_DynAnimGraphSuppress);
		eaPush(&pDoc->pObject->pDefaultsGraph->eaSuppress, pSuppress);

		ATEAnimTemplateChanged(pDoc, true, true, true, false, false);
	}
}

static void ATERemoveStance(UIButton* pButton, DynAnimGraphStance* pStance)
{
	AnimTemplateDoc* pDoc = (AnimTemplateDoc*)emGetActiveEditorDoc();
	if (pDoc) {
		bool bFound = false;
		assert(pDoc);

		FOR_EACH_IN_EARRAY(pDoc->pObject->pDefaultsGraph->eaStance, DynAnimGraphStance, pWalk)
		{
			if (pStance == pWalk)
			{
				eaRemove(&pDoc->pObject->pDefaultsGraph->eaStance, ipWalkIndex);
				StructDestroy(parse_DynAnimGraphStance, pWalk);

				bFound = true;
				break;
			}
		}
		FOR_EACH_END;

		assert(bFound);
		ATEAnimTemplateChanged(pDoc, true, true, true, false, false);
	}
}

static void ATEChangeStance(UIComboBox *pComboBox, void* pFakePtr)
{
	AnimTemplateDoc* pDoc = (AnimTemplateDoc*)emGetActiveEditorDoc();
	if (pDoc) {
		bool foundIt = false;
		char *pcSelected = NULL;

		ui_ComboBoxGetSelectedsAsString(pComboBox, &pcSelected);
		if (pcSelected)
		{
			const char *pcSelectedFromStringBase = allocAddString(pcSelected);
			FOR_EACH_IN_EARRAY(pDoc->pObject->pDefaultsGraph->eaStance, DynAnimGraphStance, pStance)
			{
				if (pStance->pcStance == pcSelectedFromStringBase)
				{
					foundIt = true;
					break;
				}
			}
			FOR_EACH_END;
			if (!foundIt)
			{
				pDoc->pObject->pDefaultsGraph->eaStance[PTR_TO_U32(pFakePtr)]->pcStance = pcSelectedFromStringBase;
				ATEAnimTemplateChanged(pDoc, true, false, true, false, false);
			}
			else
			{
				ATEAnimTemplateChanged(pDoc, false, false, true, false, false);
			}
		}
		else
		{
			ATEAnimTemplateChanged(pDoc, false, false, true, false, false);
		}
	}
}

static void ATEAddDocStance(UIButton* pButton, AnimTemplateDoc *pDoc)
{
	if (pDoc) {
		DynAnimGraphStance* pStance = StructCreate(parse_DynAnimGraphStance);
		eaPush(&pDoc->pObject->pDefaultsGraph->eaStance, pStance);

		ATEAnimTemplateChanged(pDoc, true, true, true, false, false);
	}
}

static void ATESetScopeCB(MEField* pField, bool bFinished, AnimTemplateDoc* pDoc)
{
	if (!pDoc->bIgnoreFilenameChanges)
	{
		// Update the filename appropriately
		resFixFilename(hAnimTemplateDict, pDoc->pObject->pcName, pDoc->pObject);
	}

	// Call on to do regular updates
	ATEFieldChangedCB(pField, bFinished, pDoc);
}

static void ATERemovePath(UIButton* pButton, DynAnimGraphPath *pRemovePath)
{
	AnimTemplateDoc* pDoc = (AnimTemplateDoc*)emGetActiveEditorDoc();
	if (pDoc) {
		bool bFound = false;

		FOR_EACH_IN_EARRAY(pDoc->pObject->pDefaultsGraph->eaNodes, DynAnimGraphNode, pNode)
		{
			FOR_EACH_IN_EARRAY(pNode->eaPath, DynAnimGraphPath, pWalk)
			{
				if (pRemovePath == pWalk)
				{
					DynAnimTemplatePath *freeMeT;
					DynAnimGraphPath    *freeMeG;
					freeMeT = eaRemove(&pNode->pTemplateNode->eaPath, ipWalkIndex);
					freeMeG = eaRemove(&pNode->eaPath,                ipWalkIndex);
					StructDestroy(parse_DynAnimTemplatePath, freeMeT);
					StructDestroy(parse_DynAnimGraphPath,    freeMeG);
					dynAnimGraphNodeNormalizeChances(pNode);
					bFound = true;
					break;
				}
			}
			FOR_EACH_END;

			if (bFound)
				break;
		}
		FOR_EACH_END;

		assert(bFound);
		ATEAnimTemplateChanged(pDoc, true, true, false, true, false);
	}
}

static void ATEPathChangeChance(MEField* pField, bool bFinished, AnimTemplateDoc *pDoc)
{
	ATEFieldChangedCB(pField, bFinished, pDoc);
}

static void ATEAddPath(UIButton* pButton, DynAnimGraphNode* pNode)
{
	AnimTemplateDoc* pDoc = (AnimTemplateDoc*)emGetActiveEditorDoc();
	if (pDoc) {
		DynAnimTemplatePath *pTPath = StructCreate(parse_DynAnimTemplatePath);
		DynAnimGraphPath    *pGPath = StructCreate(parse_DynAnimGraphPath);
		F32 fNumPaths;

		eaPush(&pNode->pTemplateNode->eaPath, pTPath);
		eaPush(&pNode->eaPath, pGPath);

		fNumPaths = (F32)eaSize(&pNode->eaPath); // can't be zero, we just pushed onto it
		FOR_EACH_IN_EARRAY_FORWARDS(pNode->eaPath, DynAnimGraphPath, pWalk)
		{
			if (pWalk != pGPath)
			{
				pWalk->fChance *= ((fNumPaths-1) / fNumPaths);
			}
			else
			{
				pWalk->fChance = 1.0f / fNumPaths;
			}
		}
		FOR_EACH_END;

		ATEAnimTemplateChanged(pDoc, false, true, false, true, false);
	}
}

static void ATESetNodeNameCB(MEField* pField, bool bFinished, AnimTemplateDoc* pDoc)
{
	//modify the nodes name in the template
	dynAnimTemplateFixGraphNode(pDoc->pObject, (DynAnimTemplateNode *)pField->pOld, (DynAnimTemplateNode *)pField->pNew);
}

static void ATESetNameCB(MEField* pField, bool bFinished, AnimTemplateDoc* pDoc)
{
	MEFieldFixupNameString(pField, &pDoc->pObject->pcName);

	// When the name changes, change the title of the window
	//ui_WindowSetTitle(pDoc->pMainWindow, pDoc->pObject->pcName);

	// Make sure the browser picks up the new def name if the name changed
	sprintf(pDoc->emDoc.doc_name, "%s", pDoc->pObject->pcName);
	sprintf(pDoc->emDoc.doc_display_name, "%s", pDoc->pObject->pcName);
	pDoc->emDoc.name_changed = 1;

	// Call the scope function to avoid duplicating logic
	ATESetScopeCB(pField, bFinished, pDoc);
}

static void ATERemoveMove(UIButton* pButton, DynAnimGraphMove* pMove)
{
	AnimTemplateDoc* pDoc = (AnimTemplateDoc*)emGetActiveEditorDoc();
	if (pDoc) {
		bool bFound = false;
		assert(pDoc);

		FOR_EACH_IN_EARRAY(pDoc->pObject->pDefaultsGraph->eaNodes, DynAnimGraphNode, pNode)
		{
			FOR_EACH_IN_EARRAY(pNode->eaMove, DynAnimGraphMove, pWalk)
			{
				if (pMove == pWalk)
				{
					eaRemove(&pNode->eaMove, ipWalkIndex);
					StructDestroy(parse_DynAnimGraphMove, pWalk);
					dynAnimGraphNodeNormalizeChances(pNode);

					bFound = true;
					break;
				}
			}
			FOR_EACH_END;

			if (bFound)
				break;
		}
		FOR_EACH_END;

		assert(bFound);
		ATEAnimTemplateChanged(pDoc, true, true, false, true, false);
	}
}

static void ATEOpenMove(UIButton* pButton, DynAnimGraphMove* pMove)
{
	if (pMove && GET_REF(pMove->hMove))
		emOpenFileEx(GET_REF(pMove->hMove)->pcName, DYNMOVE_TYPENAME);
}

static bool ATEChooseMoveCallback(EMPicker *picker, EMPickerSelection **selections, DynAnimGraphMove* pMove)
{
	AnimTemplateDoc* pDoc = (AnimTemplateDoc*)emGetActiveEditorDoc();

	if (!eaSize(&selections))
		return false;

	SET_HANDLE_FROM_STRING(DYNMOVE_DICTNAME, selections[0]->doc_name, pMove->hMove);
	ATEAnimTemplateChanged(pDoc, true, true, false, true, false);
	return true;
}

static void ATEChooseMove(UIWidget* pWidget, SA_PARAM_NN_VALID DynAnimGraphMove* pMove)
{
	AnimTemplateDoc* pDoc = (AnimTemplateDoc*)emGetActiveEditorDoc();
	if (pDoc) {
		EMPicker* pMovePicker = emPickerGetByName( "Move Library" );

		if (pMovePicker)
			emPickerShow(pMovePicker, NULL, false, ATEChooseMoveCallback, pMove);
	}
}

static void ATEMoveChangeChance(MEField* pField, bool bFinished, AnimTemplateDoc *pDoc)
{
	ATEFieldChangedCB(pField, bFinished, pDoc);
}

static void ATEAddMove(UIButton* pButton, DynAnimGraphNode* pNode)
{
	AnimTemplateDoc* pDoc = (AnimTemplateDoc*)emGetActiveEditorDoc();
	if (pDoc) {
		EMPicker* pMovePicker = emPickerGetByName( "Move Library" );
		DynAnimGraphMove* pMove = StructCreate(parse_DynAnimGraphMove);
		F32 fNumMoves;
		eaPush(&pNode->eaMove, pMove);

		fNumMoves = (F32)eaSize(&pNode->eaMove); // can't be zero, we just pushed onto it
		pMove->fChance = 1.0f / fNumMoves;

		FOR_EACH_IN_EARRAY_FORWARDS(pNode->eaMove, DynAnimGraphMove, pWalk)
		{
			if (pWalk != pMove)
			{
				pWalk->fChance *= ((fNumMoves-1) / fNumMoves);
			}
		}
		FOR_EACH_END;

		if (pMovePicker)
			emPickerShow(pMovePicker, NULL, false, ATEChooseMoveCallback, pMove);

		ATEAnimTemplateChanged(pDoc, false, true, false, true, false);
	}
}

static void ATERemoveImpact(UIButton *pButton, DynAnimGraphTriggerImpact *pImpact)
{
	AnimTemplateDoc *pDoc = (AnimTemplateDoc*)emGetActiveEditorDoc();
	if (pDoc) {
		bool bFound = false;
		assert(pDoc);

		FOR_EACH_IN_EARRAY(pDoc->pObject->pDefaultsGraph->eaNodes, DynAnimGraphNode, pNode)
		{
			FOR_EACH_IN_EARRAY(pNode->eaGraphImpact, DynAnimGraphTriggerImpact, pWalk)
			{
				if (pImpact == pWalk)
				{
					eaRemove(&pNode->eaGraphImpact, ipWalkIndex);
					StructDestroy(parse_DynAnimGraphTriggerImpact, pWalk);

					bFound = true;
					break;
				}
			}
			FOR_EACH_END;

			if (bFound)
				break;
		}
		FOR_EACH_END;

		assert(bFound);
		ATEAnimTemplateChanged(pDoc, true, true, false, true, false);
	}
}

static void ATEAddImpact(UIButton *pButton, DynAnimGraphNode *pNode)
{
	AnimTemplateDoc *pDoc = (AnimTemplateDoc*)emGetActiveEditorDoc();
	if (pDoc) {
		DynAnimGraphTriggerImpact *pImpact = StructCreate(parse_DynAnimGraphTriggerImpact);
		pImpact->pcBone = allocAddString("WepR");
		eaPush(&pNode->eaGraphImpact, pImpact);

		ATEAnimTemplateChanged(pDoc, true, true, false, true, false);
	}
}

static void ATERefreshGraphsCallback(UIButton *pButton, AnimTemplateDoc *pDoc)
{
	ATEAnimTemplateChanged(pDoc, false, false, false, false, true);
}

static void ATEOpenGraphCallback(UIButton* pButton, AnimTemplateDoc *pDoc)
{
	const char* buttonText = ui_WidgetGetText(UI_WIDGET(pButton));
	if (buttonText) emOpenFileEx(buttonText, "AnimGraph");
}

static void ATERemoveFxEvent(UIButton* pButton, DynAnimGraphFxEvent* pFxEvent)
{
	AnimTemplateDoc* pDoc = (AnimTemplateDoc*)emGetActiveEditorDoc();
	if (pDoc) {
		bool bFound = false;
		assert(pDoc);

		FOR_EACH_IN_EARRAY(pDoc->pObject->pDefaultsGraph->eaOnEnterFxEvent, DynAnimGraphFxEvent, pWalk)
		{
			if (pFxEvent == pWalk)
			{
				eaRemove(&pDoc->pObject->pDefaultsGraph->eaOnEnterFxEvent, ipWalkIndex);
				StructDestroy(parse_DynAnimGraphFxEvent, pWalk);
				bFound = true;
				break;
			}
		}
		FOR_EACH_END;

		if (!bFound) {
			FOR_EACH_IN_EARRAY(pDoc->pObject->pDefaultsGraph->eaOnExitFxEvent, DynAnimGraphFxEvent, pWalk)
			{
				if (pFxEvent == pWalk)
				{
					eaRemove(&pDoc->pObject->pDefaultsGraph->eaOnExitFxEvent, ipWalkIndex);
					StructDestroy(parse_DynAnimGraphFxEvent, pWalk);
					bFound = true;
					break;
				}
			}
			FOR_EACH_END;
		}

		if (!bFound) {
			FOR_EACH_IN_EARRAY(pDoc->pObject->pDefaultsGraph->eaNodes, DynAnimGraphNode, pNode)
			{
				FOR_EACH_IN_EARRAY(pNode->eaFxEvent, DynAnimGraphFxEvent, pWalk)
				{
					if (pFxEvent == pWalk)
					{
						eaRemove(&pNode->eaFxEvent, ipWalkIndex);
						StructDestroy(parse_DynAnimGraphFxEvent, pWalk);
						bFound = true;
						break;
					}
				}
				FOR_EACH_END;

				if (bFound)
					break;
			}
			FOR_EACH_END;
		}

		assert(bFound);
		ATEAnimTemplateChanged(pDoc, true, true, true, true, false);
	}
}

static void ATEOpenFxEvent(UIButton* pButton, DynAnimGraphFxEvent* pFxEvent)
{
	assert(pFxEvent);
	assert(!pFxEvent->bMessage);
	if (dynFxInfoExists(pFxEvent->pcFx))
	{
		dfxEdit(pFxEvent->pcFx);
	}
}

static bool ATEChooseFxEventCallback(EMPicker *picker, EMPickerSelection **selections, DynAnimGraphFxEvent* pFxEvent)
{
	AnimTemplateDoc* pDoc = (AnimTemplateDoc*)emGetActiveEditorDoc();
	char cFXName[1024];

	if (!eaSize(&selections))
		return false;

	getFileNameNoExt(cFXName, selections[0]->doc_name);
	pFxEvent->pcFx = allocAddString(cFXName);
	ATEAnimTemplateChanged(pDoc, true, true, true, true, false);
	return true;
}

static void ATEChooseFxEvent(UIButton* pButton, DynAnimGraphFxEvent* pFxEvent)
{
	EMPicker* picker = emPickerGetByName( "FX Library" );

	if (picker)
		emPickerShow(picker, NULL, false, ATEChooseFxEventCallback, pFxEvent);
}

static void ATEAddFXMessage(UIButton* pButton, DynAnimGraphNode* pNode)
{
	AnimTemplateDoc* pDoc = (AnimTemplateDoc*)emGetActiveEditorDoc();
	if (pDoc) {
		DynAnimGraphFxEvent* pFxEvent = StructCreate(parse_DynAnimGraphFxEvent);
		pFxEvent->bMessage = true;
		eaPush(&pNode->eaFxEvent, pFxEvent);

		ATEAnimTemplateChanged(pDoc, true, true, false, true, false);
	}
}

static void ATEAddFX(UIButton* pButton, DynAnimGraphNode* pNode)
{
	AnimTemplateDoc* pDoc = (AnimTemplateDoc*)emGetActiveEditorDoc();
	if (pDoc) {
		DynAnimGraphFxEvent* pFxEvent = StructCreate(parse_DynAnimGraphFxEvent);
		eaPush(&pNode->eaFxEvent, pFxEvent);

		ATEAnimTemplateChanged(pDoc, true, true, false, true, false);
	}
}

static void ATEAddDocOnEnterFXMessage(UIButton* pButton, AnimTemplateDoc *pDoc)
{
	if (pDoc) {
		DynAnimGraphFxEvent* pFxEvent = StructCreate(parse_DynAnimGraphFxEvent);
		pFxEvent->bMessage = true;
		eaPush(&pDoc->pObject->pDefaultsGraph->eaOnEnterFxEvent, pFxEvent);

		ATEAnimTemplateChanged(pDoc, true, true, true, false, false);
	}
}

static void ATEAddDocOnEnterFX(UIButton* pButton, AnimTemplateDoc* pDoc)
{
	if (pDoc) {
		DynAnimGraphFxEvent* pFxEvent = StructCreate(parse_DynAnimGraphFxEvent);
		eaPush(&pDoc->pObject->pDefaultsGraph->eaOnEnterFxEvent, pFxEvent);

		ATEAnimTemplateChanged(pDoc, true, true, true, false, false);
	}
}

static void ATEAddDocOnExitFXMessage(UIButton* pButton, AnimTemplateDoc *pDoc)
{
	if (pDoc) {
		DynAnimGraphFxEvent* pFxEvent = StructCreate(parse_DynAnimGraphFxEvent);
		pFxEvent->bMessage = true;
		eaPush(&pDoc->pObject->pDefaultsGraph->eaOnExitFxEvent, pFxEvent);

		ATEAnimTemplateChanged(pDoc, true, true, true, false, false);
	}
}

static void ATEAddDocOnExitFX(UIButton* pButton, AnimTemplateDoc* pDoc)
{
	if (pDoc) {
		DynAnimGraphFxEvent* pFxEvent = StructCreate(parse_DynAnimGraphFxEvent);
		eaPush(&pDoc->pObject->pDefaultsGraph->eaOnExitFxEvent, pFxEvent);

		ATEAnimTemplateChanged(pDoc, true, true, true, false, false);
	}
}

static void ATEChooseNodePostIdle(UIButton *pButton, AnimTemplateDoc *pDoc)
{
	//dummy function, should never get called
	assert(false);
}

static void ATERemoveNodePostIdle(UIButton *pButton, AnimTemplateDoc *pDoc)
{
	//dummy function, should never get called
	assert(false);
}

static void ATEOpenNodePostIdle(UIButton *pButton, AnimTemplateDoc *pDoc)
{
	//dummy function, should never get called
	assert(false);
}

static void ATEUpdateDisplay(AnimTemplateDoc* pDoc, bool bImmediate, bool bFlowchart, bool bPropsPanel, bool bNodesPanel, bool bGraphsPanel)
{
	if (bFlowchart)
		pDoc->bNeedsFlowchartReflow = true;
	if (bPropsPanel)
		pDoc->bNeedsPropPanelReflow = true;
	if (bNodesPanel)
		pDoc->bNeedsNodePanelReflow = true;
	if (bGraphsPanel)
		pDoc->bNeedsGraphPanelReflow = true;
	if (!bImmediate)
	{
		pDoc->bNeedsDisplayUpdate = true;
	}
	else
	{
		bool bSearchNeedsReflow =	pDoc->bNeedsGraphPanelReflow	||
									pDoc->bNeedsNodePanelReflow		||
									pDoc->bNeedsPropPanelReflow;
		int i;

		// Ignore changes while UI refreshes
		pDoc->bIgnoreFieldChanges = true;

		// Refresh doc-level fields
		for(i=eaSize(&pDoc->eaDocFields)-1; i>=0; --i)
		{
			MEFieldSetAndRefreshFromData(pDoc->eaDocFields[i], pDoc->pOrigObject, pDoc->pObject);
		}

		//for(i=eaSize(&pDoc->eaPropFields)-1; i>=0; --i)
		//{
		//	MEFieldSetAndRefreshFromData(pDoc->eaPropFields[i], pDoc->pOrigObject, pDoc->pObject);
		//}


		// Update non-field UI components
		ui_GimmeButtonSetName(pDoc->pFileButton, pDoc->pObject->pcName);
		ui_GimmeButtonSetReferent(pDoc->pFileButton, pDoc->pObject);
		ui_LabelSetText(pDoc->pFilenameLabel, pDoc->pObject->pcFilename);

		// Reflow flowchart
		if (pDoc->bNeedsFlowchartReflow)
			ATEReflowFlowchart(pDoc);
		pDoc->bNeedsFlowchartReflow = false;

		//Reflow properties panel
		if (pDoc->bNeedsPropPanelReflow)
		{
			if (pDoc->pPropsPanel)
			{
				eaDestroyEx(&pDoc->eaPropFields, MEFieldDestroy);
				eaFindAndRemove(&pDoc->emDoc.em_panels, pDoc->pPropsPanel);
				eaClear(&pDoc->eaInactivePropPanelWidgets);
				emPanelFree(pDoc->pPropsPanel);
			}
			pDoc->pPropsPanel = ATEInitPropertiesPanel(pDoc);
			FOR_EACH_IN_EARRAY(pDoc->eaInactivePropPanelWidgets, UIWidget, pUIWidget)
			{
				ui_SetActive(pUIWidget, false);
			}
			FOR_EACH_END;
			eaInsert(&pDoc->emDoc.em_panels, pDoc->pPropsPanel, 0);
		}
		pDoc->bNeedsPropPanelReflow = false;

		// Reflow nodes panel
		if (pDoc->bNeedsNodePanelReflow)
		{
			if (pDoc->pNodesPanel)
			{
				eaDestroyEx(&pDoc->eaNodeFields, MEFieldDestroy);
				eaDestroyEx(&pDoc->eaMoveFields, AnimEditor_MoveMEFieldDestroy);
				eaDestroyEx(&pDoc->eaPathFields, AnimEditor_PathMEFieldDestroy);
				eaFindAndRemove(&pDoc->emDoc.em_panels, pDoc->pNodesPanel);
				eaClear(&pDoc->eaInactiveNodesPanelWidgets);
				emPanelFree(pDoc->pNodesPanel);
			}
			pDoc->pNodesPanel = ATEInitNodesPanel(pDoc);
			FOR_EACH_IN_EARRAY(pDoc->eaInactiveNodesPanelWidgets, UIWidget, pUIWidget)
			{
				ui_SetActive(pUIWidget, false);
			}
			FOR_EACH_END;
			eaPush(&pDoc->emDoc.em_panels, pDoc->pNodesPanel);
		}
		else
		{
			FOR_EACH_IN_EARRAY(pDoc->eaMoveFields, MoveMEField, pMoveField)
			{
				DynAnimGraphMove* pOrigMove =
					(
					pDoc->pOrigObject &&
					pDoc->pOrigObject->pDefaultsGraph &&
					pDoc->pOrigObject->pDefaultsGraph->eaNodes &&
					eaSize(&pDoc->pOrigObject->pDefaultsGraph->eaNodes) > pMoveField->iNodeIndex &&
					eaSize(&pDoc->pOrigObject->pDefaultsGraph->eaNodes[pMoveField->iNodeIndex]->eaMove) > pMoveField->iMoveIndex
					) ?
						pDoc->pOrigObject->pDefaultsGraph->eaNodes[pMoveField->iNodeIndex]->eaMove[pMoveField->iMoveIndex] :
						NULL;
				DynAnimGraphMove* pMove =
					(
					pDoc->pObject &&
					pDoc->pObject->pDefaultsGraph &&
					pDoc->pObject->pDefaultsGraph->eaNodes &&
					eaSize(&pDoc->pObject->pDefaultsGraph->eaNodes) > pMoveField->iNodeIndex &&
					eaSize(&pDoc->pObject->pDefaultsGraph->eaNodes[pMoveField->iNodeIndex]->eaMove) > pMoveField->iMoveIndex
					) ?
						pDoc->pObject->pDefaultsGraph->eaNodes[pMoveField->iNodeIndex]->eaMove[pMoveField->iMoveIndex] :
						NULL;

				assert(pMove);

				MEFieldSetAndRefreshFromData(pMoveField->pField, pOrigMove, pMove);
				if (GET_REF(pMove->hMove) &&
					!dynAnimGraphGroupMoveVerify(pDoc->pObject->pDefaultsGraph, pDoc->pObject->pDefaultsGraph->eaNodes[pMoveField->iNodeIndex], false))
					//!dynAnimGraphIndividualMoveVerify(pDoc->pObject->pDefaultsGraph, pDoc->pObject->pDefaultsGraph->eaNodes[pMoveField->iNodeIndex], pMove, false))
					ui_WidgetSkin(pMoveField->pField->pUIWidget, &ateNodeSkin[eATEUnconnected1]);
				else
					ui_WidgetSkin(pMoveField->pField->pUIWidget, NULL );
			}
			FOR_EACH_END;

			FOR_EACH_IN_EARRAY(pDoc->eaPathFields, PathMEField, pPathField)
			{
				DynAnimGraphPath *pOrigPath =
					(
					pDoc->pOrigObject &&
					pDoc->pOrigObject->pDefaultsGraph &&
					pDoc->pOrigObject->pDefaultsGraph->eaNodes &&
					eaSize(&pDoc->pOrigObject->pDefaultsGraph->eaNodes) > pPathField->iNodeIndex &&
					eaSize(&pDoc->pOrigObject->pDefaultsGraph->eaNodes[pPathField->iNodeIndex]->eaPath) > pPathField->iPathIndex
					) ?
					pDoc->pOrigObject->pDefaultsGraph->eaNodes[pPathField->iNodeIndex]->eaPath[pPathField->iPathIndex] :
				NULL;
				DynAnimGraphPath *pPath =
					(
					pDoc->pObject &&
					pDoc->pObject->pDefaultsGraph &&
					pDoc->pObject->pDefaultsGraph->eaNodes &&
					eaSize(&pDoc->pObject->pDefaultsGraph->eaNodes) > pPathField->iNodeIndex &&
					eaSize(&pDoc->pObject->pDefaultsGraph->eaNodes[pPathField->iNodeIndex]->eaPath) > pPathField->iPathIndex
					) ?
					pDoc->pObject->pDefaultsGraph->eaNodes[pPathField->iNodeIndex]->eaPath[pPathField->iPathIndex] :
				NULL;

				assert(pPath);

				MEFieldSetAndRefreshFromData(pPathField->pField, pOrigPath, pPath);
				if (!dynAnimGraphGroupPathVerify(pDoc->pObject->pDefaultsGraph, pDoc->pObject->pDefaultsGraph->eaNodes[pPathField->iNodeIndex], false))
					//!dynAnimGraphIndividualPathVerify(pDoc->pObject->pDefaultsGraph, pDoc->pObject->pDefaultsGraph->eaNodes[pPathField->iNodeIndex], pPath, false))
					ui_WidgetSkin(pPathField->pField->pUIWidget, &ateNodeSkin[eATEUnconnected1]);
				else
					ui_WidgetSkin(pPathField->pField->pUIWidget, NULL );
			}
			FOR_EACH_END;
		}
		pDoc->bNeedsNodePanelReflow = false;

		//reflow graphs panel
		if (pDoc->bNeedsGraphPanelReflow)
		{
			if (pDoc->pGraphsPanel)
			{
				eaDestroyEx(&pDoc->eaGraphFields, MEFieldDestroy);
				eaFindAndRemove(&pDoc->emDoc.em_panels, pDoc->pGraphsPanel);
				eaClear(&pDoc->eaInactiveGraphsPanelWidgets);
				emPanelFree(pDoc->pGraphsPanel);
			}
			pDoc->pGraphsPanel = ATEInitGraphsPanel(pDoc);
			FOR_EACH_IN_EARRAY(pDoc->eaInactiveGraphsPanelWidgets, UIWidget, pUIWidget)
			{
				ui_SetActive(pUIWidget, false);
			}
			FOR_EACH_END;
			eaPush(&pDoc->emDoc.em_panels, pDoc->pGraphsPanel);
		}
		pDoc->bNeedsGraphPanelReflow = false;

		//reflow search panel
		if (bSearchNeedsReflow)
		{
			if (pDoc->pSearchPanel)
			{
				eaFindAndRemove(&pDoc->emDoc.em_panels, pDoc->pSearchPanel);
				emPanelFree(pDoc->pSearchPanel);
			}
			pDoc->pSearchPanel = ATEInitSearchPanel(pDoc);
			eaPush(&pDoc->emDoc.em_panels, pDoc->pSearchPanel);
		}
		bSearchNeedsReflow = false;

		// Update saved flag
		// Must fixup indices first for structcompare to be accurate
		dynAnimTemplateFixDefaultsGraph(pDoc->pObject);
		dynAnimTemplateFixIndices(pDoc->pObject);
		if (pDoc->pOrigObject)
		{
			dynAnimTemplateFixDefaultsGraph(pDoc->pOrigObject);
			dynAnimTemplateFixIndices(pDoc->pOrigObject);
		}
		pDoc->emDoc.saved = pDoc->pOrigObject && (StructCompare(parse_DynAnimTemplate, pDoc->pOrigObject, pDoc->pObject, 0, 0, 0) == 0);

		emRefreshDocumentUI();

		// Start paying attention to changes again
		pDoc->bIgnoreFieldChanges = false;
		pDoc->iOldFocusIndex = pDoc->iFocusIndex;
	}
}

void ATEOncePerFrame(AnimTemplateDoc* pDoc)
{
	{
		float fR = sin( timeGetTime() / 250.0 ) / 2 + 0.5;
		ui_SkinSetBackground( &ateNodeSkin[ eATEUnconnected ], ColorLerp(ateNodeTypeColor[ eATEUnconnected ], ateNodeTypeColor[ eATEUnconnected1 ], fR));
		ui_SkinSetButton( &ateNodeSkin[ eATEUnconnected1 ], ColorLerp(ateNodeTypeColor[ eATEUnconnected ], ateNodeTypeColor[ eATEUnconnected1 ], fR));
	}

	if (eaSize(&pDoc->eaNodesToFree) > 0)
	{
		FOR_EACH_IN_EARRAY_FORWARDS(pDoc->eaNodesToFree, DynAnimTemplateNode, pNode)
		{
			ATERemoveNode(pNode, pDoc);
		}
		FOR_EACH_END;
		eaClear(&pDoc->eaNodesToFree);
	}
	if (pDoc->bNeedsDisplayUpdate)
	{
		pDoc->bNeedsDisplayUpdate = false;
		ATEUpdateDisplay(pDoc, true, false, false, false, false);
	}

	if (!ui_ExpanderIsOpened(emPanelGetExpander(pDoc->pPropsPanel)))
		emPanelSetOpened(pDoc->pPropsPanel, true);

	if (!ui_ExpanderIsOpened(emPanelGetExpander(pDoc->pNodesPanel)))
		emPanelSetOpened(pDoc->pNodesPanel, true);

	if (!ui_ExpanderIsOpened(emPanelGetExpander(pDoc->pGraphsPanel)))
		emPanelSetOpened(pDoc->pGraphsPanel, true);

	if (!ui_ExpanderIsOpened(emPanelGetExpander(pDoc->pSearchPanel)))
		emPanelSetOpened(pDoc->pSearchPanel, true);
}


//---------------------------------------------------------------------------------------------------
// UI Initialization
//---------------------------------------------------------------------------------------------------

static void ATEInitViewPane(AnimTemplateDoc* pDoc)
{
	F32 canvasX, canvasY, canvasWidth, canvasHeight;
	emGetCanvasSize( &canvasX, &canvasY, &canvasWidth, &canvasHeight );
	pDoc->pViewPane = ui_PaneCreate( 0, 0, 1, (canvasHeight - ATE_TABBAR_HEIGHT) / 2, UIUnitPercentage, UIUnitFixed, 0 );

	pDoc->pViewPane->widget.tickF = ATEFlowchartPaneTick;
	ui_WidgetSkin( UI_WIDGET( pDoc->pViewPane ), &ateViewPaneSkin );
	ui_WidgetSetPositionEx( UI_WIDGET( pDoc->pViewPane ), 0, 0, 0, 0, UIBottomRight );

	pDoc->pFlowchartViewHideButton = ui_ButtonCreateImageOnly( "eui_arrow_large_up", -12, 0, ATEFlowchartViewHideShowClicked, pDoc );
	ui_WidgetSetDimensions( UI_WIDGET( pDoc->pFlowchartViewHideButton ), 32, 12 );
	ui_ButtonSetImageStretch( pDoc->pFlowchartViewHideButton, true );
	pDoc->pFlowchartViewHideButton->widget.xPOffset = 0.5;
	pDoc->pFlowchartViewHideButton->widget.offsetFrom = UITopLeft;
	pDoc->pFlowchartViewHideButton->widget.priority = 100;
	ui_WidgetSetDragCallback( UI_WIDGET( pDoc->pFlowchartViewHideButton ), ATEFlowchartViewExpandDrag, pDoc );
	pDoc->pFlowchartViewHideButton = ui_ButtonCreateImageOnly( "eui_arrow_large_down", -12, 0, ATEFlowchartViewHideShowClicked, pDoc );
	ui_WidgetSetDimensions( UI_WIDGET( pDoc->pFlowchartViewHideButton ), 32, 12 );
	ui_ButtonSetImageStretch( pDoc->pFlowchartViewHideButton, true );
	pDoc->pFlowchartViewHideButton->widget.xPOffset = 0.5;
	pDoc->pFlowchartViewHideButton->widget.offsetFrom = UITopLeft;
	pDoc->pFlowchartViewHideButton->widget.priority = 100;
	ui_WidgetSetDragCallback( UI_WIDGET( pDoc->pFlowchartViewHideButton ), ATEFlowchartViewExpandDrag, pDoc );
	ui_WidgetAddChild( UI_WIDGET( pDoc->pViewPane ), UI_WIDGET( pDoc->pFlowchartViewHideButton ));

	pDoc->pFlowchart = ui_FlowchartCreate( NULL, NULL, NULL, NULL );
	ui_ScrollAreaSetNoCtrlDraggable( UI_SCROLLAREA( pDoc->pFlowchart ), true );
	ui_WidgetSetDimensionsEx( UI_WIDGET( pDoc->pFlowchart ), 1, 1, UIUnitPercentage, UIUnitPercentage );
	ui_WidgetAddChild( UI_WIDGET( pDoc->pViewPane ), UI_WIDGET( pDoc->pFlowchart ));

	ui_FlowchartSetLinkedCallback( pDoc->pFlowchart, ATEFlowchartLinkRequest, pDoc );
	ui_FlowchartSetUnlinkedCallback( pDoc->pFlowchart, ATEFlowchartUnlinkRequest, pDoc );
	ui_FlowchartSetLinkBeginCallback( pDoc->pFlowchart, ATEFlowchartLinkBegin, pDoc );
	ui_FlowchartSetLinkEndCallback( pDoc->pFlowchart, ATEFlowchartLinkEnd, pDoc );
	ui_FlowchartSetNodeRemovedCallback( pDoc->pFlowchart, ATEFlowchartNodeRemoveRequest, pDoc );
	ui_FlowchartSetNodeRemovedLateCallback( pDoc->pFlowchart, ATEFlowchartNodeRemoved, pDoc );
}

static EMPanel* ATEInitPropertiesPanel(AnimTemplateDoc* pDoc)
{
	EMPanel* pPanel;
	UILabel* pLabel;
	MEField* pField;
	UISeparator* pSeparator;
	F32 x = 0;
	F32 y = 0;
	F32 fBottomY = 0;
	F32 fTopY = 0;

	// Create the panel
	pPanel = emPanelCreate("Properties", "Properties", 0);

	//TEMPLATE PARTS

	pSeparator = ui_SeparatorCreate(UIHorizontal);
	ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pSeparator), 1.0, 5, UIUnitPercentage, UIUnitFixed);
	emPanelAddChild(pPanel, pSeparator, false);

	y += SEPARATOR_HEIGHT;

	// Name
	pLabel = ui_LabelCreate("Name", 0, y);
	emPanelAddChild(pPanel, pLabel, false);
	pField = MEFieldCreateSimple(kMEFieldType_TextEntry, pDoc->pOrigObject, pDoc->pObject, parse_DynAnimTemplate, "Name");
	ATEAddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 21, pDoc);
	MEFieldSetChangeCallback(pField, ATESetNameCB, pDoc);
	eaPush(&pDoc->eaPropFields, pField);

	y += STANDARD_ROW_HEIGHT;

	// Scope
	pLabel = ui_LabelCreate("Scope", 0, y);
	emPanelAddChild(pPanel, pLabel, false);
	pField = MEFieldCreateSimpleDataProvided(kMEFieldType_TextEntry, pDoc->pOrigObject, pDoc->pObject, parse_DynAnimTemplate, "Scope", NULL, &geaScopes, NULL);
	ATEAddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 21, pDoc);
	MEFieldSetChangeCallback(pField, ATESetScopeCB, pDoc);
	eaPush(&pDoc->eaPropFields, pField);

	y += STANDARD_ROW_HEIGHT;

	// File Name
	pLabel = ui_LabelCreate("File Name", 0, y);
	emPanelAddChild(pPanel, pLabel, false);
	pDoc->pFileButton = ui_GimmeButtonCreate(X_OFFSET_CONTROL, y, "AnimTemplate", pDoc->pObject->pcName, pDoc->pObject);
	emPanelAddChild(pPanel, pDoc->pFileButton, false);
	pLabel = ui_LabelCreate(pDoc->pObject->pcFilename, X_OFFSET_CONTROL+20, y);
	emPanelAddChild(pPanel, pLabel, false);
	ui_WidgetSetWidthEx(UI_WIDGET(pLabel), 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(UI_WIDGET(pLabel), 0, 21, 0, 0);
	pDoc->pFilenameLabel = pLabel;

	y += STANDARD_ROW_HEIGHT;

	// Comments
	pLabel = ui_LabelCreate("Comments", 0, y);
	emPanelAddChild(pPanel, pLabel, false);
	pField = MEFieldCreateSimple(kMEFieldType_MultiText, pDoc->pOrigObject, pDoc->pObject, parse_DynAnimTemplate, "Comments");
	ATEAddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 21, pDoc);
	eaPush(&pDoc->eaPropFields, pField);

	y += STANDARD_ROW_HEIGHT;

	// Type
	pLabel = ui_LabelCreate("Type", 0, y);
	emPanelAddChild(pPanel, pLabel, false);
	pField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pDoc->pOrigObject, pDoc->pObject, parse_DynAnimTemplate, "Type", eAnimTemplateTypeEnum);
	ATEAddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 21, pDoc);
	eaPush(&pDoc->eaPropFields, pField);

	y += STANDARD_ROW_HEIGHT;

	//graph mode
	if (pDoc->pObject->pDefaultsGraph->bPartialGraph)
		pLabel = ui_LabelCreate("Graph Mode: Partial", 0, y);	
	else
		pLabel = ui_LabelCreate("Graph Mode: Normal", 0, y);
	emPanelAddChild(pPanel, pLabel, false);

	y += STANDARD_ROW_HEIGHT;

	pSeparator = ui_SeparatorCreate(UIHorizontal);
	ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pSeparator), 1.0, 5, UIUnitPercentage, UIUnitFixed);
	emPanelAddChild(pPanel, pSeparator, false);

	y += 30;

	pSeparator = ui_SeparatorCreate(UIHorizontal);
	ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pSeparator), 1.0, 5, UIUnitPercentage, UIUnitFixed);
	emPanelAddChild(pPanel, pSeparator, false);

	y += SEPARATOR_HEIGHT;

	//GRAPH PARTS

	y = AnimEditor_ReflowGraphProperties(
		(pDoc->pOrigObject ? pDoc->pOrigObject->pDefaultsGraph : NULL), pDoc->pObject->pDefaultsGraph,
		pDoc, pPanel,
		&pDoc->eaPropFields, &pDoc->eaInactivePropPanelWidgets,
		ATEFieldPreChangeCB, ATEFieldChangedCB, ATEInheritFieldChangedCB,
		ATEAddDocOnEnterFX, ATEAddDocOnEnterFXMessage,
		ATEAddDocOnExitFX, ATEAddDocOnExitFXMessage,
		ATEChooseFxEvent, ATEOpenFxEvent, ATERemoveFxEvent,
		ATEAddDocSuppress, ATERemoveSuppress,
		ATEAddDocStance, ATEChangeStance, ATERemoveStance,
		&ateNodeSkin[eATEUnconnected1],
		y
		);

	pSeparator = ui_SeparatorCreate(UIHorizontal);
	ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pSeparator), 1.0, 5, UIUnitPercentage, UIUnitFixed);
	emPanelAddChild(pPanel, pSeparator, false);

	y += SEPARATOR_HEIGHT;

	emPanelSetHeight(pPanel, y);

	return pPanel;
}

static F32 ATENodeEditReflow(AnimTemplateDoc* pDoc, EMPanel* pPanel, F32 y)
{
	MEField *pField;
	UILabel *pLabel;
	UIButton* pButton;
	UISeparator* pSeparator;
	DynAnimTemplateNode *pEditTemplateNode, *pOrigTemplateNode;
	DynAnimGraphNode    *pEditGraphNode,    *pOrigGraphNode;
	F32 x;

	pEditTemplateNode = pDoc->pObject->eaNodes[pDoc->iFocusIndex];
	pEditGraphNode    = pDoc->pObject->pDefaultsGraph->eaNodes[pDoc->iFocusIndex];

	pOrigTemplateNode = pDoc->pOrigObject ? (pDoc->iFocusIndex < eaSize(&pDoc->pOrigObject->eaNodes)?pDoc->pOrigObject->eaNodes[pDoc->iFocusIndex]:NULL) : NULL;
	pOrigGraphNode    = pDoc->pOrigObject ? (pDoc->iFocusIndex < eaSize(&pDoc->pOrigObject->pDefaultsGraph->eaNodes)?pDoc->pOrigObject->pDefaultsGraph->eaNodes[pDoc->iFocusIndex]:NULL) : NULL;

	pLabel = ui_LabelCreate("Name", 0, y);
	emPanelAddChild(pPanel, pLabel, false);
	pField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigTemplateNode, pEditTemplateNode, parse_DynAnimTemplateNode, "Name");
	MEFieldSetChangeCallback(pField, ATESetNodeNameCB, pDoc);
	ATEAddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 21, pDoc);
	if (pEditTemplateNode->eType == eAnimTemplateNodeType_Start)
		eaPush(&pDoc->eaInactiveNodesPanelWidgets, pField->pUIWidget);
	eaPush(&pDoc->eaNodeFields, pField);

	y += STANDARD_ROW_HEIGHT;

	switch(pEditTemplateNode->eType)
	{
		xcase eAnimTemplateNodeType_Start      : {pLabel = ui_LabelCreate("Type: Start",      0, y);}
		xcase eAnimTemplateNodeType_End        : {pLabel = ui_LabelCreate("Type: End",        0, y);}
		xcase eAnimTemplateNodeType_Normal     : {pLabel = ui_LabelCreate("Type: Normal",     0, y);}
		xcase eAnimTemplateNodeType_Randomizer : {pLabel = ui_LabelCreate("Type: Randomizer", 0, y);}
		break; default                         : {pLabel = ui_LabelCreate("Type: Unknown",    0, y);}
	}
	emPanelAddChild(pPanel, pLabel, false);

	y += STANDARD_ROW_HEIGHT;

	if (pEditTemplateNode->eType == eAnimTemplateNodeType_Normal)
	{
		pLabel = ui_LabelCreate("Interruptible by movement", 0, y);
		emPanelAddChild(pPanel, pLabel, false);
		pField = MEFieldCreateSimple(kMEFieldType_Check, pOrigTemplateNode, pEditTemplateNode, parse_DynAnimTemplateNode, "Interruptible");
		ATEAddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), 175, y, 0, 50.0, UIUnitFixed, 0, pDoc);
		eaPush(&pDoc->eaNodeFields, pField);

		y += STANDARD_ROW_HEIGHT;
	}

	if (pEditTemplateNode->eType == eAnimTemplateNodeType_Start ||
		pEditTemplateNode->eType == eAnimTemplateNodeType_Normal)
	{
		pSeparator = ui_SeparatorCreate(UIHorizontal);
		ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
		emPanelAddChild(pPanel, pSeparator, false);

		y += SEPARATOR_HEIGHT;

		FOR_EACH_IN_EARRAY_FORWARDS(pEditTemplateNode->eaSwitch, DynAnimTemplateSwitch, pSwitch)
		{
			DynAnimTemplateSwitch* pOrigSwitch = (pOrigTemplateNode && ipSwitchIndex < eaSize(&pOrigTemplateNode->eaSwitch))?pOrigTemplateNode->eaSwitch[ipSwitchIndex]:NULL;

			x = 20;
			pLabel = ui_LabelCreate("Switch", 0, y);
			emPanelAddChild(pPanel, pLabel, false);
			x += ui_WidgetGetWidth( UI_WIDGET(pLabel) );

			pField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigSwitch, pSwitch, parse_DynAnimTemplateSwitch, "Flag");
			ATEAddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 100, UIUnitFixed, 0, pDoc);
			eaPush(&pDoc->eaNodeFields, pField);
			x += ui_WidgetGetWidth(pField->pUIWidget);

			pButton = ui_ButtonCreate("X", x, y, ATERemoveSwitch, pSwitch);
			emPanelAddChild(pPanel, pButton, false);
			x += ui_WidgetGetWidth( UI_WIDGET(pButton) ) + 50;

			if (pEditTemplateNode->eType != eAnimTemplateNodeType_Start)
			{
				pLabel = ui_LabelCreate("Interrupt", x, y);
				emPanelAddChild(pPanel, pLabel, false);
				x += ui_WidgetGetWidth( UI_WIDGET(pLabel) );

				pField = MEFieldCreateSimple(kMEFieldType_Check, pOrigSwitch, pSwitch, parse_DynAnimTemplateSwitch, "Interrupt");
				ATEAddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 50.0, UIUnitFixed, 0, pDoc);
				eaPush(&pDoc->eaNodeFields, pField);
			}

			y += STANDARD_ROW_HEIGHT;
		}
		FOR_EACH_END;

		pButton = ui_ButtonCreate("Add Switch", 0, y, ATEAddSwitch, pDoc);
		emPanelAddChild(pPanel, pButton, false);

		y += STANDARD_ROW_HEIGHT;
	}

	if (pEditTemplateNode->eType == eAnimTemplateNodeType_Normal)
	{
		pSeparator = ui_SeparatorCreate(UIHorizontal);
		ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
		emPanelAddChild(pPanel, pSeparator, false);

		y += SEPARATOR_HEIGHT;

		y = AnimEditor_ReflowNormalNode(
			pDoc->pObject->pDefaultsGraph,
			pDoc, pDoc->iFocusIndex, pPanel,
			&pDoc->eaNodeFields, &pDoc->eaMoveFields, NULL, &pDoc->eaInactiveNodesPanelWidgets,
			pOrigGraphNode, pEditGraphNode,
			ATEFieldPreChangeCB, ATEFieldChangedCB, ATEInheritFieldChangedCB,
			ATEAddFX, ATEAddFXMessage, ATEChooseFxEvent, ATEOpenFxEvent, ATERemoveFxEvent,
			ATEAddImpact, ATERemoveImpact,
			ATEAddMove, ATEChooseMove, ATEMoveChangeChance, ATEOpenMove, ATERemoveMove,
			&ateNodeSkin[eATEUnconnected1],
			false,
			y
			);
	}
	else if (pEditTemplateNode->eType == eAnimTemplateNodeType_Randomizer)
	{
		y = AnimEditor_ReflowRandomizerNode(
			pDoc->pObject->pDefaultsGraph,
			pDoc, pDoc->iFocusIndex, pPanel,
			&pDoc->eaNodeFields, &pDoc->eaPathFields, &pDoc->eaInactiveNodesPanelWidgets,
			pOrigGraphNode, pEditGraphNode,
			ATEFieldPreChangeCB, ATEFieldChangedCB, ATEInheritFieldChangedCB,
			ATEAddPath, ATEPathChangeChance, ATERemovePath,
			&ateNodeSkin[eATEUnconnected1],
			y
			);
	}
	else if (pEditTemplateNode->eType == eAnimTemplateNodeType_End)
	{
		y = AnimEditor_ReflowExitNode(
			pDoc->pObject->pDefaultsGraph,
			pDoc, pDoc->iFocusIndex, pPanel,
			&pDoc->eaNodeFields, &pDoc->eaPathFields, &pDoc->eaInactiveNodesPanelWidgets,
			pOrigGraphNode, pEditGraphNode,
			ATEFieldPreChangeCB, ATEFieldChangedCB, ATEInheritFieldChangedCB,
			ATEChooseNodePostIdle, ATEOpenNodePostIdle, ATERemoveNodePostIdle,
			&ateNodeSkin[eATEUnconnected1],
			y
			);
	}

	return y;
}

static EMPanel* ATEInitNodesPanel(AnimTemplateDoc* pDoc)
{
	EMPanel* pPanel;
	UISeparator* pSeparator;
	F32 y = 0;

	// Create the panel
	pPanel = emPanelCreate("Nodes", "Nodes", 0);

	//NODES PART

	pSeparator = ui_SeparatorCreate(UIHorizontal);
	ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pSeparator), 1.0, 5, UIUnitPercentage, UIUnitFixed);
	emPanelAddChild(pPanel, pSeparator, false);

	y += SEPARATOR_HEIGHT/2;

	{
		UIButton* pButton = ui_ButtonCreate( "Add Node", 0, y, ATEAddNormalNodeCallback, pDoc );
		ui_WidgetSetWidthEx( UI_WIDGET( pButton ), 1, UIUnitPercentage );
		emPanelAddChild(pPanel, pButton, false);
	}
	
	y += STANDARD_ROW_HEIGHT;

	{
		UIButton* pButton = ui_ButtonCreate( "Add Randomizer Node", 0, y, ATEAddRandomizerNodeCallback, pDoc );
		ui_WidgetSetWidthEx( UI_WIDGET( pButton ), 1, UIUnitPercentage );
		emPanelAddChild(pPanel, pButton, false);
	}

	y += STANDARD_ROW_HEIGHT;

	{
		UIButton *pButton = ui_ButtonCreate( "Add Exit Node", 0, y, ATEAddExitNodeCallback, pDoc );
		ui_WidgetSetWidthEx( UI_WIDGET( pButton ), 1, UIUnitPercentage );
		emPanelAddChild(pPanel, pButton, false);
	}

	y += STANDARD_ROW_HEIGHT;

	pSeparator = ui_SeparatorCreate(UIHorizontal);
	ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pSeparator), 1.0, 5, UIUnitPercentage, UIUnitFixed);
	emPanelAddChild(pPanel, pSeparator, false);

	y += 30;

	pSeparator = ui_SeparatorCreate(UIHorizontal);
	ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pSeparator), 1.0, 5, UIUnitPercentage, UIUnitFixed);
	emPanelAddChild(pPanel, pSeparator, false);

	y += SEPARATOR_HEIGHT;

	if (pDoc->iFocusIndex >= 0 && pDoc->iFocusIndex < eaSize(&pDoc->pObject->eaNodes))
	{
		y = ATENodeEditReflow(pDoc, pPanel, y);
	}
	else
	{
		UILabel* pLabel = ui_LabelCreate("No Node Selected.", 0, y);
		emPanelAddChild(pPanel, pLabel, false);

		y += STANDARD_ROW_HEIGHT;
	}

	pSeparator = ui_SeparatorCreate(UIHorizontal);
	ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pSeparator), 1.0, 5, UIUnitPercentage, UIUnitFixed);
	emPanelAddChild(pPanel, pSeparator, false);

	y += SEPARATOR_HEIGHT;

	emPanelSetHeight(pPanel, y);

	return pPanel;
}

static EMPanel* ATEInitGraphsPanel(AnimTemplateDoc* pDoc)
{
	EMPanel* pPanel;
	UISeparator *pSeparator;
	UIButton* pButton;
	UILabel *pLabel;
	F32 y = 0.0;

	// Create the panel
	pPanel = emPanelCreate("Used by", "Used by", 0);

	pSeparator = ui_SeparatorCreate(UIHorizontal);
	ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pSeparator), 1.0, 5, UIUnitPercentage, UIUnitFixed);
	emPanelAddChild(pPanel, pSeparator, false);

	y += SEPARATOR_HEIGHT/2;

	// Refresh button
	pButton = ui_ButtonCreate( "Refresh", 0, y, ATERefreshGraphsCallback, pDoc );
	ui_WidgetSetWidthEx( UI_WIDGET( pButton ), 1, UIUnitPercentage );
	emPanelAddChild(pPanel, pButton, false);

	y += STANDARD_ROW_HEIGHT;

	pSeparator = ui_SeparatorCreate(UIHorizontal);
	ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pSeparator), 1.0, 5, UIUnitPercentage, UIUnitFixed);
	emPanelAddChild(pPanel, pSeparator, false);

	y += 30;

	pSeparator = ui_SeparatorCreate(UIHorizontal);
	ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pSeparator), 1.0, 5, UIUnitPercentage, UIUnitFixed);
	emPanelAddChild(pPanel, pSeparator, false);

	y += SEPARATOR_HEIGHT;

	pLabel = ui_LabelCreate("Dependent Graphs:", 0, y);
	emPanelAddChild(pPanel, pLabel, false);
		
	y += STANDARD_ROW_HEIGHT;

	pSeparator = ui_SeparatorCreate(UIHorizontal);
	ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
	emPanelAddChild(pPanel, pSeparator, false);
		
	y += SEPARATOR_HEIGHT;

	{
		DictionaryEArrayStruct *pAnimGraphArray = resDictGetEArrayStruct(hAnimGraphDict);
		FOR_EACH_IN_EARRAY(pAnimGraphArray->ppReferents, DynAnimGraph, pGraph)
		{
			if (GET_REF(pGraph->hTemplate) && REF_HANDLE_GET_STRING(pGraph->hTemplate) == pDoc->pObject->pcName)
			{
				pButton = ui_ButtonCreate(pGraph->pcName, 0, y, ATEOpenGraphCallback, pDoc);
				ui_WidgetSetWidthEx( UI_WIDGET( pButton ), 1, UIUnitPercentage );
				emPanelAddChild(pPanel, pButton, false);

				y += STANDARD_ROW_HEIGHT;
			}
		}
		FOR_EACH_END;
	}

	pSeparator = ui_SeparatorCreate(UIHorizontal);
	ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pSeparator), 1.0, 5, UIUnitPercentage, UIUnitFixed);
	emPanelAddChild(pPanel, pSeparator, false);

	y += SEPARATOR_HEIGHT;

	emPanelSetHeight(pPanel, y);
	

	return pPanel;
}

static EMPanel* ATEInitSearchPanel(AnimTemplateDoc* pDoc)
{
	EMPanel* pPanel;
	F32 y = 0.0;

	// Create the panel
	pPanel = emPanelCreate("Search", "Search", 0);

	y += AnimEditor_Search(	pDoc,
							pPanel,
							AnimEditor_SearchText,
							ATESearchTextChanged,
							ATERefreshSearchPanel
							);

	emPanelSetHeight(pPanel, y);

	return pPanel;
}

static void ATEInitDisplay(EMEditor* pEditor, AnimTemplateDoc* pDoc)
{
	// Create the panel (ignore field change callbacks during init)
	pDoc->bIgnoreFieldChanges = true;
	pDoc->bIgnoreFilenameChanges = true;
	pDoc->pPropsPanel = ATEInitPropertiesPanel(pDoc);
	pDoc->pNodesPanel = ATEInitNodesPanel(pDoc);
	pDoc->pGraphsPanel = ATEInitGraphsPanel(pDoc);
	pDoc->pSearchPanel = ATEInitSearchPanel(pDoc);
	pDoc->bIgnoreFieldChanges = false;
	pDoc->bIgnoreFilenameChanges = false;

	ATEInitViewPane(pDoc);

	//pDoc->emDoc.primary_ui_window = pDoc->pMainWindow;
	//eaPush(&pDoc->emDoc.ui_windows, pDoc->pMainWindow);
	eaPush(&pDoc->emDoc.em_panels, pDoc->pPropsPanel);
	eaPush(&pDoc->emDoc.em_panels, pDoc->pNodesPanel);
	eaPush(&pDoc->emDoc.em_panels, pDoc->pGraphsPanel);
	eaPush(&pDoc->emDoc.em_panels, pDoc->pSearchPanel);

	// Update the rest of the UI
	ATEUpdateDisplay(pDoc, true, true, true, true, true);
}

#define BUTTON_SPACING 3.0f

#define ADD_BUTTON( text, callback, callbackdata ) \
	pButton = ui_ButtonCreate(text, fX, 0, callback, callbackdata); \
	pButton->widget.widthUnit = UIUnitFitContents; \
	emToolbarAddChild(pToolbar, pButton, false); \
	fX += ui_WidgetGetWidth(UI_WIDGET(pButton)) + BUTTON_SPACING; \

static void ATEInitToolbarsAndMenus(EMEditor* pEditor)
{
	EMToolbar* pToolbar;
	UIButton *pButton;
	F32 fX;

	// Toolbar
	pToolbar = emToolbarCreateFileToolbar(EM_FILE_TOOLBAR_NEW|EM_FILE_TOOLBAR_OPEN|EM_FILE_TOOLBAR_SAVE);
	fX = emToolbarGetPaneWidget(pToolbar)->width;
	ADD_BUTTON("Duplicate", ATEDuplicateDoc, NULL);
	emToolbarSetWidth(pToolbar, fX);
	eaPush(&pEditor->toolbars, pToolbar);
	
	eaPush(&pEditor->toolbars, emToolbarCreateWindowToolbar());

	// File menu
	emMenuItemCreate(pEditor, "ate_reverttemplate", "Revert", NULL, NULL, "ATE_RevertTemplate");
	emMenuRegister(pEditor, emMenuCreate(pEditor, "File", "ate_reverttemplate", NULL));
}


//---------------------------------------------------------------------------------------------------
// Public Interface
//---------------------------------------------------------------------------------------------------

void ATEInitData(EMEditor* pEditor)
{
	if (pEditor && !gInitializedEditor)
	{
		gBoldExpanderSkin = ui_SkinCreate(NULL);
		SET_HANDLE_FROM_STRING(g_ui_FontDict, "Default_Bold", gBoldExpanderSkin->hNormal);

		// skins
		{
			int iColor;
			for( iColor = 0; iColor < eATECount; ++iColor )
			{
				ui_SkinCopy( &ateNodeSkin[ iColor ], NULL );
				ui_SkinSetBackground( &ateNodeSkin[ iColor ], ateNodeTypeColor[ iColor ]);
			}
		}

		ui_SkinCopy( &ateViewPaneSkin, NULL );
		ateViewPaneSkin.background[ 0 ].a /= 2;
		ateViewPaneSkin.background[ 1 ].a /= 2;

		ATEInitToolbarsAndMenus(pEditor);

		// Have Editor Manager handle a lot of change tracking
		emAutoHandleDictionaryStateChange(pEditor, "AnimTemplate", true, NULL, NULL, NULL, NULL, NULL);

		resGetUniqueScopes(hAnimTemplateDict, &geaScopes);

		gInitializedEditor = true;
	}

	if (!gInitializedEditorData)
	{
		// Make sure lists refresh if dictionary changes
		resDictRegisterEventCallback(hAnimTemplateDict, ATEContentDictChanged, NULL);

		gInitializedEditorData = true;
	}
}

static void ATETemplatePostOpenFixup(DynAnimTemplate* pTemplate)
{
}


static void ATETemplatePreSaveFixup(DynAnimTemplate* pTemplate)
{
	FOR_EACH_IN_EARRAY(pTemplate->pDefaultsGraph->eaNodes, DynAnimGraphNode, pGraphNode)
	{
		dynAnimGraphNodeNormalizeChances(pGraphNode);
	}
	FOR_EACH_END;
}


static AnimTemplateDoc* ATEInitDoc(DynAnimTemplate* pTemplate, bool bCreated, bool bEmbedded)
{
	AnimTemplateDoc* pDoc;
	char nameBuf[260];

	// Initialize the structure
	pDoc = (AnimTemplateDoc*)calloc(1,sizeof(AnimTemplateDoc));

	// Fill in the def data
	if (bCreated && pTemplate)
	{
		pDoc->pObject = StructClone(parse_DynAnimTemplate, pTemplate);
		assert(pDoc->pObject);
		sprintf(pDoc->emDoc.doc_name, "%s_Dup%d", pTemplate->pcName, ++ateNewNameCount);
		pDoc->pObject->pcName = StructAllocString(pDoc->emDoc.doc_name);
		sprintf(nameBuf, "dyn/animtemplate/%s/%s.atemp", pDoc->pObject->pcScope, pDoc->pObject->pcName);
		pDoc->pObject->pcFilename = allocAddFilename(nameBuf);
	}
	else if (bCreated)
	{
		pDoc->pObject = StructCreate(parse_DynAnimTemplate);
		assert(pDoc->pObject);
		dynAnimTemplateInit(pDoc->pObject);
		emMakeUniqueDocName(&pDoc->emDoc, DEFAULT_DOC_NAME, "DynAnimTemplate", "DynAnimTemplate");
		pDoc->pObject->pcName = StructAllocString(pDoc->emDoc.doc_name);
		sprintf(nameBuf, "dyn/animtemplate/%s.atemp", pDoc->pObject->pcName);
		pDoc->pObject->pcFilename = allocAddFilename(nameBuf);
		ATETemplatePostOpenFixup(pDoc->pObject);
	}
	else
	{
		pDoc->pObject = StructClone(parse_DynAnimTemplate, pTemplate);
		assert(pDoc->pObject);
		ATETemplatePostOpenFixup(pDoc->pObject);
		pDoc->pOrigObject = StructClone(parse_DynAnimTemplate, pDoc->pObject);
		emDocAssocFile(&pDoc->emDoc, pDoc->pObject->pcFilename);
	}

	pDoc->iFocusIndex = -1;
	pDoc->iOldFocusIndex = -2;

	// Set up the undo stack
	pDoc->emDoc.edit_undo_stack = EditUndoStackCreate();
	EditUndoSetContext(pDoc->emDoc.edit_undo_stack, pDoc);
	pDoc->pNextUndoObject = StructClone(parse_DynAnimTemplate, pDoc->pObject);

	return pDoc;
}


AnimTemplateDoc* ATEOpenAnimTemplate(EMEditor* pEditor, char* pcName, DynAnimTemplate *pTemplateIn)
{
	AnimTemplateDoc* pDoc = NULL;
	DynAnimTemplate* pTemplate = NULL;
	bool bCreated = false;

	if (pTemplateIn)
	{
		pTemplate = pTemplateIn;
		bCreated = true;
	}
	else if (pcName && resIsEditingVersionAvailable(hAnimTemplateDict, pcName))
	{
		// Simply open the object since it is in the dictionary
		pTemplate = RefSystem_ReferentFromString(hAnimTemplateDict, pcName);
	}
	else if (pcName)
	{
		// Wait for object to show up so we can open it
		resSetDictionaryEditMode(hAnimTemplateDict, true);
		emSetResourceState(pEditor, pcName, EMRES_STATE_OPENING);
		resRequestOpenResource(hAnimTemplateDict, pcName);
	}
	else
	{
		// Create a new object since it is not in the dictionary
		bCreated = true;
	}

	if (pTemplate || bCreated)
	{
		pDoc = ATEInitDoc(pTemplate, bCreated, false);
		ATEInitDisplay(pEditor, pDoc);
		resFixFilename(hAnimTemplateDict, pDoc->pObject->pcName, pDoc->pObject);
	}

	return pDoc;
}

static void ATEDeleteOldDirectoryIfEmpty(AnimTemplateDoc *pDoc)
{
	char dir[MAX_PATH], out_dir[MAX_PATH];
	char cmd[MAX_PATH];

	sprintf(dir, "/dyn/Animtemplate/%s", NULL_TO_EMPTY(pDoc->pOrigObject->pcScope));
	fileLocateWrite(dir, out_dir);
	if (dirExists(out_dir))
	{
		backSlashes(out_dir);
		sprintf(cmd, "rd %s", out_dir);
		system(cmd);
	}
}

void ATERevertAnimTemplate(AnimTemplateDoc* pDoc)
{
	DynAnimTemplate* pTemplate;

	if (!pDoc->emDoc.orig_doc_name[0])
	{
		// Cannot revert if no original
		return;
	}

	//if we're reverting due to save, remove the old directory if it's empty post scope change
	if (pDoc->pOrigObject && pDoc->pObject->pcScope != pDoc->pOrigObject->pcScope)
		ATEDeleteOldDirectoryIfEmpty(pDoc);

	pTemplate = RefSystem_ReferentFromString(hAnimTemplateDict, pDoc->emDoc.orig_doc_name);
	if (pTemplate)
	{
		// Revert the def
		StructDestroy(parse_DynAnimTemplate, pDoc->pObject);
		StructDestroy(parse_DynAnimTemplate, pDoc->pOrigObject);
		pDoc->pObject = StructClone(parse_DynAnimTemplate, pTemplate);
		ATETemplatePostOpenFixup(pDoc->pObject);
		pDoc->pOrigObject = StructClone(parse_DynAnimTemplate, pDoc->pObject);

		// Clear the undo stack on revert
		EditUndoStackClear(pDoc->emDoc.edit_undo_stack);
		StructDestroy(parse_DynAnimTemplate, pDoc->pNextUndoObject);
		pDoc->pNextUndoObject = StructClone(parse_DynAnimTemplate, pDoc->pObject);

		// Refresh the UI
		pDoc->bIgnoreFieldChanges = true;
		pDoc->bIgnoreFilenameChanges = true;
		ATEUpdateDisplay(pDoc, false, true, true, true, true);
		pDoc->bIgnoreFieldChanges = false;
		pDoc->bIgnoreFilenameChanges = false;
	}

}


void ATECloseAnimTemplate(AnimTemplateDoc* pDoc)
{
	// Free doc fields
	eaDestroyEx(&pDoc->eaDocFields,  MEFieldDestroy);
	eaDestroyEx(&pDoc->eaPropFields, MEFieldDestroy);
	eaDestroyEx(&pDoc->eaNodeFields, MEFieldDestroy);
	eaDestroyEx(&pDoc->eaGraphFields, MEFieldDestroy);
	eaDestroyEx(&pDoc->eaMoveFields, AnimEditor_MoveMEFieldDestroy);
	eaDestroyEx(&pDoc->eaPathFields, AnimEditor_PathMEFieldDestroy);

	// Free the groups
	//FreeInteractionPropertiesGroup(pDoc->pPropsGroup);

	// Free the objects
	StructDestroy(parse_DynAnimTemplate, pDoc->pObject);
	if (pDoc->pOrigObject)
	{
		StructDestroy(parse_DynAnimTemplate, pDoc->pOrigObject);
	}
	StructDestroy(parse_DynAnimTemplate, pDoc->pNextUndoObject);

	// Close the window
	//ui_WindowHide(pDoc->pMainWindow);
	//ui_WidgetQueueFree(UI_WIDGET(pDoc->pMainWindow));
	emPanelFree(pDoc->pPropsPanel);
	emPanelFree(pDoc->pNodesPanel);
	emPanelFree(pDoc->pGraphsPanel);
	emPanelFree(pDoc->pSearchPanel);
	pDoc->pPropsPanel = NULL;
	pDoc->pNodesPanel = NULL;
	pDoc->pGraphsPanel = NULL;
	pDoc->pSearchPanel = NULL;
}


EMTaskStatus ATESaveAnimTemplate(AnimTemplateDoc* pDoc, bool bSaveAsNew)
{
	EMTaskStatus status;
	const char* pcName;
	DynAnimTemplate* pTemplateCopy;

	// Deal with state changes
	pcName = pDoc->pObject->pcName;
	if (emHandleSaveResourceState(pDoc->emDoc.editor, pcName, &status))
	{
		if (status == EM_TASK_FAILED)
		{
			Errorf("Template save failed on emHandleSaveResourceState");
		}
		return status;
	}

	if (strnicmp(pcName, DEFAULT_DOC_NAME, strlen(DEFAULT_DOC_NAME))==0)
	{
		Errorf("Must choose a name besides %s", DEFAULT_DOC_NAME);
		return EM_TASK_FAILED;
	}


	// Do cleanup before validation
	pTemplateCopy = StructClone(parse_DynAnimTemplate, pDoc->pObject);
	ATETemplatePreSaveFixup(pTemplateCopy);

	// Perform validation
	if (!dynAnimTemplateVerify(pTemplateCopy))
	{
		StructDestroy(parse_DynAnimTemplate, pTemplateCopy);
		Errorf("Template save failed verification");
		return EM_TASK_FAILED;
	}

	// Do the save (which will free the copy)
	status = emSmartSaveDoc(&pDoc->emDoc, pTemplateCopy, pDoc->pOrigObject, bSaveAsNew);
	emDocRemoveAllFiles(&pDoc->emDoc, false);
	emDocAssocFile(&pDoc->emDoc, pDoc->pObject->pcFilename);

	if (status == EM_TASK_FAILED)
	{
		Errorf("Template save failed at end of function");
	}

	return status;
}

void ATEDuplicateDoc(UIButton *button, UserData uData)
{
	AnimTemplateDoc *pDoc = (AnimTemplateDoc*)emGetActiveEditorDoc();
	emNewDoc("AnimTemplate", pDoc->pObject);
}

void ATEGotFocus(AnimTemplateDoc *pDoc)
{
	ATERefreshSearchPanel(NULL, pDoc);
}

#endif
