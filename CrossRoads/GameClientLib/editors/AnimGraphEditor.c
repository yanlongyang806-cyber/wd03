/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "AnimGraphEditor.h"
#include "AnimEditorCommon.h"
#include "dynAnimGraph.h"
#include "dynAnimGraphUpdater.h"
#include "dynAnimTemplate.h"
#include "dynFxDebug.h"


#include "winutil.h"
#include "Color.h"
#include "Error.h"
#include "StringCache.h"
#include "UIGimmeButton.h"
#include "gclCostumeView.h"
#include "EditorPrefs.h"
#include "inputMouse.h"
#include "UISeparator.h"
#include "EditorManagerUIToolbars.h"
#include "GfxTexAtlas.h"
#include "GfxFont.h"
#include "GfxDebug.h"
#include "GfxSpriteText.h"
#include "GameClientLib.h"
#include "dynFxManager.h"
#include "dynFxInfo.h"
#include "file.h"
#include "WorldLib.h"

#include "soundLib.h"
#include "gclUtils.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

#define DEFAULT_DOC_NAME "New_AnimGraph"


//-----------------------------------------------------------------------------------
// Global Data
//-----------------------------------------------------------------------------------

#define DEFAULT_NODE_WIDTH 300
#define DEFAULT_START_NODE_WIDTH 100

static int ageNewNameCount = 0;

static bool ageDrawGrid = true;
static UICheckButton *ageDrawGridCheckButton;

static bool gInitializedEditor = false;
static bool gInitializedEditorData = false;
static bool gIndexChanged = false;

static bool ageWarnedNoSkeleton = false;

static char** geaScopes = NULL;

static UISkin* gBoldExpanderSkin;

static UISprite* gBackgroundScrollSprite;

static UIStyleFont* gActiveMoveFont;
static UIStyleFont* gBoldPanelFont;
static UISkin ageViewPaneSkin;

typedef enum eAGESkinType
{
	eAGENormal,
	eAGERandomizer,
	eAGEStart,
	eAGEEnd,
	eAGECount,
} eAGESkinType;

UISkin ageNodeSkin[ eAGECount ];

// Node skin colors based on info
static const Color ageNodeTypeColor[eAGECount] = {
	{ 192, 192, 192, 255 },		// eAGENormal
	{ 240, 240, 128, 255 },     // eAGERandomizer
	{ 128, 240, 128, 255 },		// eAGEStart
	{ 128, 128, 240, 255 },		// eAGEEnd
};

UISkin ageBadDataBackground;
UISkin ageBadDataButton;

static const Color ageBadDataColor[2] = {
	{ 255, 000, 000, 255 },	
	{ 255, 255, 000, 255 },
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

#define AGE_TABBAR_HEIGHT 32

#define INSERT_SEPARATOR(y) \
{ \
	UISeparator* pSeparator = ui_SeparatorCreate(UIHorizontal); \
	ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y); \
	emPanelAddChild(pPanel, pSeparator, false); \
	y += SEPARATOR_HEIGHT; \
	}


static void AGEFieldChangedCB(MEField* pField, bool bFinished, AnimGraphDoc* pDoc);
static bool AGEFieldPreChangeCB(MEField* pField, bool bFinished, AnimGraphDoc* pDoc);
static void AGEAnimGraphChanged(AnimGraphDoc* pDoc, bool bUndoable, bool bFlowchart, bool bNodesPanel, bool bPropsPanel, bool bChartsPanel);
static void AGEGraphPreSaveFixup(DynAnimGraph* pGraph);
static void AGEUpdateDisplay(SA_PARAM_NN_VALID AnimGraphDoc* pDoc, bool bImmediate, bool bFlowchart, bool bNodesPanel, bool bPropsPanel, bool bChartsPanel);
static EMPanel* AGEInitNodesPanel(AnimGraphDoc* pDoc);
SA_RET_NN_VALID static EMPanel* AGEInitPropertiesPanel(AnimGraphDoc* pDoc);
static EMPanel* AGEInitChartsPanel(AnimGraphDoc* pDoc);
static EMPanel* AGEInitSearchPanel(AnimGraphDoc* pDoc);

static AnimEditor_CostumePickerData* AGEGetCostumePickerData(void);
static void AGEPostCostumeChangeCB( void );
static void AGEGetPaneBox(CBox* pBox);

static void AGECreateTracks(AnimGraphDoc* pDoc);

static void AGEMoveBiasToggled(UICheckButton *pButton, UserData pData);
static void AGEPathBiasToggled(UICheckButton *pButton, UserData pData);

//---------------------------------------------------------------------------------------------------
// Grid
//---------------------------------------------------------------------------------------------------

static void AGEDrawGridCheckboxToggle(UICheckButton *pButton, UserData pData)
{
	AnimGraphDoc *pDoc = (AnimGraphDoc*)emGetActiveEditorDoc();
	ageDrawGrid = ageDrawGridCheckButton->state;
	pDoc->bNeedsDisplayUpdate = true;
}

static void AGEDrawGrid(void)
{
	Vec3 vGridPt1, vGridPt2;
	S32 uiGridLine;
	U32 uiGridColor;

	for (uiGridLine = -30; uiGridLine <= 30; uiGridLine++)
	{
		uiGridColor = uiGridLine % 10 == 0 ?
			uiGridLine == 0 ? 0x88FFFFFF : 0x44FFFFFF :
			0x22FFFFFF;
		vGridPt1[0] = uiGridLine; vGridPt1[1] = 0.f; vGridPt1[2] = -30.f;
		vGridPt2[0] = uiGridLine; vGridPt2[1] = 0.f; vGridPt2[2] =  30.f;

		wlDrawLine3D_2(vGridPt1, uiGridColor, vGridPt2, uiGridColor);
	}

	for (uiGridLine = -30; uiGridLine <= 30; uiGridLine++)
	{
		uiGridColor = uiGridLine % 10 == 0 ?
			uiGridLine == 0 ? 0x88FFFFFF : 0x44FFFFFF :
			0x22FFFFFF;
		vGridPt1[0] = -30.f; vGridPt1[1] = 0.f; vGridPt1[2] = uiGridLine;
		vGridPt2[0] =  30.f; vGridPt2[1] = 0.f; vGridPt2[2] = uiGridLine;

		wlDrawLine3D_2(vGridPt1, uiGridColor, vGridPt2, uiGridColor);
	}

	uiGridColor = 0x44FFFFFF;
	vGridPt1[1] = 0.f;
	vGridPt2[1] = 0.f;

	vGridPt1[0] = -0.05f; vGridPt1[2] = -30.f;
	vGridPt2[0] = -0.05f; vGridPt2[2] =  30.f;
	wlDrawLine3D_2(vGridPt1, uiGridColor, vGridPt2, uiGridColor);

	vGridPt1[0] = 0.05f; vGridPt1[2] = -30.f;
	vGridPt2[0] = 0.05f; vGridPt2[2] =  30.f;
	wlDrawLine3D_2(vGridPt1, uiGridColor, vGridPt2, uiGridColor);

	vGridPt1[0] = -30.f; vGridPt1[2] = -0.05f;
	vGridPt2[0] =  30.f; vGridPt2[2] = -0.05f;
	wlDrawLine3D_2(vGridPt1, uiGridColor, vGridPt2, uiGridColor);

	vGridPt1[0] = -30.f; vGridPt1[2] = 0.05f;
	vGridPt2[0] =  30.f; vGridPt2[2] = 0.05f;
	wlDrawLine3D_2(vGridPt1, uiGridColor, vGridPt2, uiGridColor);
}

//---------------------------------------------------------------------------------------------------
// Searching
//---------------------------------------------------------------------------------------------------

static void AGERefreshSearchPanel(UIButton *pButton, UserData uData)
{
	AnimGraphDoc *pDoc = (AnimGraphDoc*)emGetActiveEditorDoc();

	if (pDoc->pSearchPanel)
	{
		eaFindAndRemove(&pDoc->emDoc.em_panels, pDoc->pSearchPanel);
		emPanelFree(pDoc->pSearchPanel);
	}
	pDoc->pSearchPanel = AGEInitSearchPanel(pDoc);
	eaPush(&pDoc->emDoc.em_panels, pDoc->pSearchPanel);
	emRefreshDocumentUI();
}

static void AGESearchTextChanged(UITextEntry *pEntry, UserData uData)
{
	if (pEntry) {
		AnimGraphDoc *pDoc = (AnimGraphDoc*)emGetActiveEditorDoc();
		AnimEditor_SearchText = allocAddString(ui_TextEntryGetText(pEntry));
		if (!strlen(AnimEditor_SearchText))
			AnimEditor_SearchText = NULL;
		AGERefreshSearchPanel(NULL, uData);
	}
}


//---------------------------------------------------------------------------------------------------
// Data Manipulation
//---------------------------------------------------------------------------------------------------

static void AGEAnimGraphUndoCB(AnimGraphDoc* pDoc, AGEUndoData* pData)
{
	// Put the undo def into the editor
	StructDestroy(parse_DynAnimGraph, pDoc->pObject);
	pDoc->bForceGraphChange = true;
	pDoc->pObject = StructClone(parse_DynAnimGraph, pData->pPreObject);
	if (pDoc->pNextUndoObject)
		StructDestroy(parse_DynAnimGraph, pDoc->pNextUndoObject);
	pDoc->pNextUndoObject = StructClone(parse_DynAnimGraph, pDoc->pObject);

	// Update the UI
	AGEAnimGraphChanged(pDoc, false, true, true, true, true);
}


static void AGEAnimGraphRedoCB(AnimGraphDoc* pDoc, AGEUndoData* pData)
{
	// Put the undo def into the editor
	StructDestroy(parse_DynAnimGraph, pDoc->pObject);
	pDoc->bForceGraphChange = true;
	pDoc->pObject = StructClone(parse_DynAnimGraph, pData->pPostObject);
	if (pDoc->pNextUndoObject)
		StructDestroy(parse_DynAnimGraph, pDoc->pNextUndoObject);
	pDoc->pNextUndoObject= StructClone(parse_DynAnimGraph, pDoc->pObject);

	// Update the UI
	AGEAnimGraphChanged(pDoc, false, true, true, true, true);
}


static void AGEAnimGraphUndoFreeCB(AnimGraphDoc* pDoc, AGEUndoData* pData)
{
	// Free the memory
	StructDestroy(parse_DynAnimGraph, pData->pPreObject);
	StructDestroy(parse_DynAnimGraph, pData->pPostObject);
	free(pData);
}


static void AGEIndexChangedCB(void* unused)
{
	if (gIndexChanged)
	{
		gIndexChanged = false;
		resGetUniqueScopes(hAnimGraphDict, &geaScopes);
	}
}


static void AGEContentDictChanged(enumResourceEventType eType, const char* pDictName, const char* pcName, Referent pReferent, void* pUserData)
{
	if ((eType == RESEVENT_INDEX_MODIFIED) && !gIndexChanged)
	{
		gIndexChanged = true;
		emQueueFunctionCall(AGEIndexChangedCB, NULL);
	}
}


//---------------------------------------------------------------------------------------------------
// FlowChart Editing UI
//---------------------------------------------------------------------------------------------------

// Called by the flowchart widget every time a link is added --
// returns if the link is allowed.
bool AGEFlowchartLinkRequest( UIFlowchart* pFlowchart, UIFlowchartButton* pSource, UIFlowchartButton* pDest, bool bForce, AnimGraphDoc* pDoc )
{
	return pDoc->bReflowing;
}

// Always return TRUE.  Called by the flowchart widget every time a
// link is removed, to update internal state.
bool AGEFlowchartUnlinkRequest( UIFlowchart* pFlowchart, UIFlowchartButton* pSource, UIFlowchartButton* pDest, bool bForce, AnimGraphDoc* pDoc )
{
	return pDoc->bReflowing;
}

// Called by the flowchart widget ONCE every time a link add is
// requested.  This will be the first call.
bool AGEFlowchartLinkBegin( UIFlowchart* pFlowchart, UIFlowchartButton* pSource, UIFlowchartButton* pDest, bool bForce, AnimGraphDoc* pDoc )
{
	return pDoc->bReflowing;
}

// Called by the flowchart widget ONCE every time a link add is
// requested.  This will be the last call.
bool AGEFlowchartLinkEnd( UIFlowchart* pFlowchart, UIFlowchartButton* pSource, UIFlowchartButton* pDest, bool bLinked, AnimGraphDoc* pDoc )
{
	return pDoc->bReflowing;
}

// Called by the flowchart widget every time a node is requested to
// be removed, before its links are broken.
static bool AGEFlowchartNodeRemoveRequest( UIFlowchart* pFlowchart, UIFlowchartNode* pUINode, AnimGraphDoc* pDoc )
{
	return pDoc->bReflowing;
}


// Hide or show the node view window.
static void AGEFlowchartViewHideShowClicked( UIButton* ignored, AnimGraphDoc* pDoc )
{
	F32 canvasX, canvasY, canvasWidth, canvasHeight;
	emGetCanvasSize( &canvasX, &canvasY, &canvasWidth, &canvasHeight );

	ui_WidgetRemoveChild( UI_WIDGET( pDoc->pViewPane ), UI_WIDGET( pDoc->pFlowchartViewHideButton ));
	ui_WidgetRemoveChild( UI_WIDGET( pDoc->pViewPane ), UI_WIDGET( pDoc->pFlowchartViewHideButton ));

	if( pDoc->pViewPane->widget.height < canvasHeight - AGE_TABBAR_HEIGHT )
	{
		ui_WidgetSetHeightEx( UI_WIDGET( pDoc->pViewPane ), canvasHeight - AGE_TABBAR_HEIGHT, UIUnitFixed );
		ui_WidgetAddChild( UI_WIDGET( pDoc->pViewPane ), UI_WIDGET( pDoc->pFlowchartViewHideButton ));
	}
	else
	{
		ui_WidgetSetHeightEx( UI_WIDGET( pDoc->pViewPane ), (canvasHeight - AGE_TABBAR_HEIGHT) / 2, UIUnitFixed );
		ui_WidgetAddChild( UI_WIDGET( pDoc->pViewPane ), UI_WIDGET( pDoc->pFlowchartViewHideButton ));
	}
}

static void AGEFlowchartNodeFocusCB(UIWidget *pUINode, AnimGraphDoc* pDoc)
{
	AGENodeData* pNodeData = ((UIFlowchartNode*)pUINode)->userData;
	DynAnimGraphNode* pNode = pNodeData->pNode;
	pDoc->iOldFocusIndex = pDoc->iFocusIndex;
	pDoc->iFocusIndex = eaFind(&pDoc->pObject->eaNodes, pNode);

	if (!pDoc->bReflowing && pDoc->iFocusIndex != pDoc->iOldFocusIndex)
		AGEUpdateDisplay(pDoc, false, false, true, false, false);
}

static void AGEFlowchartNodeUnfocusCB(UIFlowchartNode* pUINode, AnimGraphDoc* pDoc)
{
}

static void AGEFlowchartNodeMovedCB(UIFlowchartNode* pUINode, AnimGraphDoc* pDoc)
{
	if (pDoc)
	{
		AGENodeData* pNodeData = pUINode->userData;
		DynAnimGraphNode* pNode = pNodeData->pNode;
		if (pNode)
		{
			F32 fX = ui_WidgetGetX( UI_WIDGET(pUINode) );
			F32 fY = ui_WidgetGetY( UI_WIDGET(pUINode) );
			if (fabsf(pNode->fX - fX) > 5.0f || fabsf(pNode->fY - fY) > 5.0f)
			{
				pNode->fX = fX;
				pNode->fY = fY;
				AGEAnimGraphChanged(pDoc, true, true, false, false, false);
			} else {
				ui_WidgetSetPosition(UI_WIDGET(pUINode), pNode->fX, pNode->fY);
			}
		}
	}
	else
	{
		AGEAnimGraphChanged(pDoc, false, true, false, false, false);
	}
}



// Expand the node view window.
static void AGEFlowchartViewExpandDrag( UIButton* button, AnimGraphDoc* pDoc )
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
static void AGEFlowchartPaneTick( UIPane* pane, UI_PARENT_ARGS )
{
	AnimGraphDoc* pDoc = (AnimGraphDoc*)emGetActiveEditorDoc();
	bool bConnectingShouldFinish = false;
	// deal with resizing
	if( pDoc && pDoc->bFlowchartViewIsResizing )
	{
		if( mouseIsDown( MS_LEFT ))
		{
			int mouseX, mouseY;
			F32 canvasX, canvasY, canvasWidth, canvasHeight;
			int maxHeight = pDoc->iFlowchartViewResizingCanvasHeight - AGE_TABBAR_HEIGHT;
			int newHeight;
			mousePos( &mouseX, &mouseY );
			emGetCanvasSize( &canvasX, &canvasY, &canvasWidth, &canvasHeight );

			newHeight = MIN( MAX( pDoc->iFlowchartViewResizingCanvasHeight - (mouseY - canvasY),
				100 ),
				pDoc->iFlowchartViewResizingCanvasHeight - AGE_TABBAR_HEIGHT );

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

static void AGENodeDataFree(AGENodeData* pNodeData)
{
	if (!pNodeData)
		return;

	eaDestroy(&pNodeData->eaMoveLabels);
	eaDestroy(&pNodeData->eaBiasButtons);
	free(pNodeData);
}

static void AGEFlowchartNodeOnFree(UIFlowchartNode* pUINode)
{
	AGENodeDataFree(pUINode->userData);
}

#define NODE_MARGINS 5.0f

static UIFlowchartNode* AGEFlowchartNodeCreate(DynAnimGraphNode* pNode, AnimGraphDoc* pDoc, int iNodeIndex)
{
	UIFlowchartButton** eaInputButtons = NULL;
	UIFlowchartButton** eaOutputButtons = NULL;
	UIFlowchartNode* pUINode = NULL;

	DynAnimTemplateNode* pTemplateNode = pNode->pTemplateNode;
	AGENodeData* pNodeData = calloc(sizeof(AGENodeData), 1);
	pNodeData->pNode = pNode;

	assert(pTemplateNode);

	if (pTemplateNode->eType != eAnimTemplateNodeType_Start)
	{
		UIFlowchartButton* inputButton = ui_FlowchartButtonCreate( pDoc->pFlowchart, UIFlowchartNormal, ui_LabelCreate( pTemplateNode->eType == eAnimTemplateNodeType_End?"Exit Animation":"In", 0, 0 ), NULL );
		ui_WidgetSetTooltipString( UI_WIDGET( inputButton ), "Input" );

		ui_FlowchartButtonSetSingleConnection( inputButton, false );
		eaPush( &eaInputButtons, inputButton );
	}

	if (pTemplateNode->eType != eAnimTemplateNodeType_End)
	{
		UIFlowchartButton* outputButton;

		if (pTemplateNode->eType == eAnimTemplateNodeType_Start ||
			pTemplateNode->eType == eAnimTemplateNodeType_Normal)
		{
			outputButton = ui_FlowchartButtonCreate( pDoc->pFlowchart, UIFlowchartNormal, ui_LabelCreate( "Default", 0, 0 ), &pTemplateNode->defaultNext );
			ui_FlowchartButtonSetSingleConnection( outputButton, true );
			eaPush( &eaOutputButtons, outputButton );

			FOR_EACH_IN_EARRAY_FORWARDS(pTemplateNode->eaSwitch, DynAnimTemplateSwitch, pSwitch)
			{
				outputButton = ui_FlowchartButtonCreate( pDoc->pFlowchart, UIFlowchartNormal, ui_LabelCreate( pSwitch->pcFlag, 0, 0 ), &pSwitch->next );
				ui_FlowchartButtonSetSingleConnection( outputButton, true );
				eaPush( &eaOutputButtons, outputButton );
			}
			FOR_EACH_END;
		}

		if (pTemplateNode->eType == eAnimTemplateNodeType_Randomizer)
		{
			FOR_EACH_IN_EARRAY_FORWARDS(pTemplateNode->eaPath, DynAnimTemplatePath, pPath)
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
		eAGESkinType eType = eAGENormal;
		switch (pTemplateNode->eType)
		{
			xcase eAnimTemplateNodeType_Start:
			{
				eType = eAGEStart;
			}
			xcase eAnimTemplateNodeType_End:
			{
				eType = eAGEEnd;
			}
			xcase eAnimTemplateNodeType_Normal:
			{
				eType = eAGENormal;
			}
			xcase eAnimTemplateNodeType_Randomizer:
			{
				eType = eAGERandomizer;
			}
		}

		pUINode = ui_FlowchartNodeCreate( pNode->pcName, pNode->fX, pNode->fY, pTemplateNode->eType == eAnimTemplateNodeType_Normal?DEFAULT_NODE_WIDTH:DEFAULT_START_NODE_WIDTH, 100, &eaInputButtons, &eaOutputButtons, pNodeData );
		ui_WidgetSetFreeCallback(UI_WIDGET(pUINode), AGEFlowchartNodeOnFree);
		if (iNodeIndex == 2)
		{
		}

		ui_WidgetSkin( UI_WIDGET( pUINode ), &ageNodeSkin[eType] );
		if (!dynAnimGraphNodeVerify(pDoc->pObject, pNode, false))
			ui_WidgetSkin( UI_WIDGET( pUINode ), &ageBadDataBackground );
	}
	//pUINode->widget.tickF = mated2FlowchartNodeTick;
	//ui_WindowSetClickedCallback( UI_WINDOW( pUINode ), (UIActivationFunc)mated2FlowchartNodeClicked, pDoc );
	//ui_WindowSetShadedCallback( UI_WINDOW( pUINode ), mated2FlowchartNodeShaded, doc );
	//ui_WidgetSetFreeCallback( UI_WIDGET( pUINode ), AGENodeFree );
	ui_WidgetSetFocusCallback( UI_WIDGET( pUINode ), AGEFlowchartNodeFocusCB, pDoc);
	ui_WindowSetRaisedCallback( (UIWindow*)pUINode, AGEFlowchartNodeFocusCB, pDoc);
	ui_WidgetSetUnfocusCallback( UI_WIDGET( pUINode ), AGEFlowchartNodeUnfocusCB, pDoc);
	ui_WindowSetMovedCallback( UI_WINDOW( pUINode ), AGEFlowchartNodeMovedCB, pDoc);
	ui_WindowSetResizable( UI_WINDOW( pUINode ), false );
	ui_WindowSetClosable( UI_WINDOW( pUINode), false );
	ui_FlowchartNodeSetAutoResize( pUINode, true );

	{
		int iY = 0;
		int iX = NODE_MARGINS;
		int iMaxX = 0;
		int numMoves;

		numMoves = eaSize(&pNode->eaMove);
		FOR_EACH_IN_EARRAY_FORWARDS(pNode->eaMove, DynAnimGraphMove, pMove)
		{
			char cMove[1024];
			UILabel* pLabel;
			UICheckButton *pBiasButton = NULL;

			sprintf(cMove, " %s - %.2f%%", REF_STRING_FROM_HANDLE(pMove->hMove), pMove->fChance * 100.0f);
			pLabel = ui_LabelCreate(cMove, (numMoves>1)?iX+45:iX, iY);
			pLabel->widget.widthUnit = UIUnitFitContents;
			//ui_WidgetSetWidthEx( UI_WIDGET( pLabel ), 1, UIUnitPercentage );

			if (numMoves > 1) {
				pBiasButton = ui_CheckButtonCreate(iX, iY, "Bias", pMove->bEditorPlaybackBias);
				ui_CheckButtonSetToggledCallback(pBiasButton, AGEMoveBiasToggled, U32_TO_PTR(iNodeIndex*1000+ipMoveIndex));//pMove);
			}

			ui_FlowchartNodeAddChild(pUINode, UI_WIDGET(pLabel), false );
			if (numMoves > 1) ui_FlowchartNodeAddChild(pUINode, UI_WIDGET(pBiasButton), false);
			eaPush(&pNodeData->eaMoveLabels, pLabel);
			if (numMoves > 1) eaPush(&pNodeData->eaBiasButtons, pBiasButton);

			iY += MAX(	numMoves>1 ?
							ui_WidgetGetHeight(UI_WIDGET(pBiasButton)) :
							0,
						ui_WidgetGetHeight(UI_WIDGET(pLabel)) );
			MAX1(iMaxX,
				(numMoves>1) ?
					ui_WidgetGetWidth(UI_WIDGET(pLabel))+45 :
					ui_WidgetGetWidth(UI_WIDGET(pLabel)));
		}
		FOR_EACH_END;

		if (SAFE_MEMBER(pTemplateNode,bInterruptible))
		{
			UILabel* pLabel;
			iX = NODE_MARGINS;
			pLabel = ui_LabelCreate(" Interruptible by Movement", iX, iY);
			pLabel->widget.widthUnit = UIUnitFitContents;
			ui_FlowchartNodeAddChild(pUINode, UI_WIDGET(pLabel), false );
			pNodeData->pInterpByMovementLabel = pLabel;
			iY += ui_WidgetGetHeight(UI_WIDGET(pLabel));
			MAX1(iMaxX, ui_WidgetGetWidth(UI_WIDGET(pLabel)));
		}

		FOR_EACH_IN_EARRAY_FORWARDS(pNode->eaPath, DynAnimGraphPath, pPath)
		{
			char cPath[1024];
			UICheckButton *pBiasButton;

			sprintf(cPath, "Bias Path %i", ipPathIndex+1);
			pBiasButton = ui_CheckButtonCreate(iX, iY, cPath, pPath->bEditorPlaybackBias);
			ui_CheckButtonSetToggledCallback(pBiasButton, AGEPathBiasToggled, U32_TO_PTR(iNodeIndex*1000+ipPathIndex));//pPath);

			ui_FlowchartNodeAddChild(pUINode, UI_WIDGET(pBiasButton), false);
			eaPush(&pNodeData->eaBiasButtons, pBiasButton);

			iY += ui_WidgetGetHeight(UI_WIDGET(pBiasButton));
			MAX1(iMaxX, ui_WidgetGetWidth(UI_WIDGET(pBiasButton)));
		}
		FOR_EACH_END;

		if (REF_HANDLE_IS_ACTIVE(pNode->hPostIdle))
		{
			char cPostIdle[1024];
			UILabel* pLabel;

			sprintf(cPostIdle, " %s", REF_STRING_FROM_HANDLE(pNode->hPostIdle));
			pLabel = ui_LabelCreate(cPostIdle, iX, iY);
			pLabel->widget.widthUnit = UIUnitFitContents;
			ui_FlowchartNodeAddChild(pUINode, UI_WIDGET(pLabel), false );
			pNodeData->pPostIdleLabel = pLabel;

			iY += ui_WidgetGetHeight(UI_WIDGET(pLabel));
			MAX1(iMaxX, ui_WidgetGetWidth(UI_WIDGET(pLabel)));
		}

		{
			char cMove[1024];
			UILabel* pLabel;
			UIProgressBar* pBar;
			iX = NODE_MARGINS;
			sprintf(cMove, " Blend %.1f%%", 0.7f);
			pNodeData->pBlendLabel = pLabel = ui_LabelCreate(cMove, iX, iY);
			ui_WidgetSetWidthEx(UI_WIDGET(pLabel), 80.0f, UIUnitFixed);
			ui_FlowchartNodeAddChild(pUINode, UI_WIDGET(pLabel), false);
			iX += ui_WidgetGetWidth(UI_WIDGET(pLabel)) + 5.0f;

			pNodeData->pBlendBar = pBar = ui_ProgressBarCreate(iX, iY, iMaxX - iX);
			ui_ProgressBarSet(pBar, 0.5f);
			ui_FlowchartNodeAddChild(pUINode, UI_WIDGET(pBar), false);
			iY += MAX(ui_WidgetGetHeight(UI_WIDGET(pBar)), ui_WidgetGetHeight(UI_WIDGET(pLabel)));
		}

		ui_WidgetSetHeight( UI_WIDGET( pUINode->beforePane ), iY );
		iMaxX += NODE_MARGINS;
		if (pTemplateNode->eType == eAnimTemplateNodeType_End			&& iMaxX > DEFAULT_START_NODE_WIDTH	||
			pTemplateNode->eType == eAnimTemplateNodeType_Normal		&& iMaxX > DEFAULT_NODE_WIDTH		||
			pTemplateNode->eType == eAnimTemplateNodeType_Randomizer	&& iMaxX > DEFAULT_NODE_WIDTH		)
		{
			ui_WidgetSetWidth( UI_WIDGET( pUINode ), iMaxX );
		}
	}

	ui_FlowchartAddNode(pDoc->pFlowchart, pUINode);
	return pUINode;
}

static void AGEReconnectNodes( AnimGraphDoc* pDoc, DynAnimTemplateNode* pNode, DynAnimTemplateNodeRef* pRef ) 
{
	if (pNode && pRef->p)
	{
		// Find source and dest nodes
		UIFlowchartButton* pSource = NULL;
		UIFlowchartButton* pDest = NULL;
		FOR_EACH_IN_EARRAY(pDoc->pFlowchart->nodeWindows, UIFlowchartNode, pFlowNode)
		{
			AGENodeData* pNodeData = pFlowNode->userData;
			DynAnimTemplateNode* pTemplateNode = pNodeData->pNode->pTemplateNode;
			assert(pTemplateNode);
			if (pTemplateNode == pNode)
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
			if (pTemplateNode == pRef->p)
			{
				pDest = pFlowNode->inputButtons[0];
			}
		}
		FOR_EACH_END;
		assert(pSource && pDest);
		ui_FlowchartLink(pSource, pDest);
	}
}

static void AGEReflowFlowchart(AnimGraphDoc* pDoc)
{
	pDoc->bReflowing = true;
	ui_FlowchartClear(pDoc->pFlowchart);

	FOR_EACH_IN_EARRAY_FORWARDS(pDoc->pObject->eaNodes, DynAnimGraphNode, pNode)
	{
		AGEFlowchartNodeCreate(pNode, pDoc, ipNodeIndex);
	}
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY_FORWARDS(pDoc->pObject->eaNodes, DynAnimGraphNode, pNode)
	{
		DynAnimTemplateNode* pTemplateNode = pNode->pTemplateNode;
		assert(pTemplateNode);
		AGEReconnectNodes(pDoc, pTemplateNode, &pTemplateNode->defaultNext);

		FOR_EACH_IN_EARRAY(pTemplateNode->eaSwitch, DynAnimTemplateSwitch, pSwitch)
		{
			AGEReconnectNodes(pDoc, pTemplateNode, &pSwitch->next);
		}
		FOR_EACH_END;

		FOR_EACH_IN_EARRAY(pTemplateNode->eaPath, DynAnimTemplatePath, pPath)
		{
			AGEReconnectNodes(pDoc, pTemplateNode, &pPath->next);
		}
		FOR_EACH_END;

		if (ipNodeIndex == pDoc->iFocusIndex)
		{
			FOR_EACH_IN_EARRAY(pDoc->pFlowchart->nodeWindows, UIFlowchartNode, pUINode)
			{
				AGENodeData* pNodeData = pUINode->userData;
				if (pNodeData && pNodeData->pNode == pNode)
					ui_WidgetGroupSteal( UI_WIDGET(pUINode)->group, UI_WIDGET(pUINode) );
			}
			FOR_EACH_END;
		}
	}
	FOR_EACH_END;
	pDoc->bReflowing = false;
}

//---------------------------------------------------------------------------------------------------
// UI Logic
//---------------------------------------------------------------------------------------------------

static void AGEAddFieldToParent(MEField* pField, UIWidget* pParent, F32 x, F32 y, F32 xPercent, F32 w, UIUnitType wUnit, F32 padRight, AnimGraphDoc* pDoc)
{
	MEFieldAddToParent(pField, pParent, x, y);
	ui_WidgetSetPositionEx(pField->pUIWidget, x, y, xPercent, 0, UITopLeft);
	ui_WidgetSetWidthEx(pField->pUIWidget, w, wUnit);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, padRight, 0, 0);
	MEFieldSetChangeCallback(pField, AGEFieldChangedCB, pDoc);
	MEFieldSetPreChangeCallback(pField, AGEFieldPreChangeCB, pDoc);
}


static UIExpander* AGECreateExpander(UIExpanderGroup* pExGroup, const char* pcName, int index)
{
	UIExpander* pExpander = ui_ExpanderCreate(pcName, 0);
	ui_WidgetSkin(UI_WIDGET(pExpander), gBoldExpanderSkin);
	ui_ExpanderGroupInsertExpander(pExGroup, pExpander, index);
	ui_ExpanderSetOpened(pExpander, 1);

	return pExpander;
}


// This is called whenever any def data changes to do cleanup
static void AGEAnimGraphChanged(AnimGraphDoc* pDoc, bool bUndoable, bool bFlowchart, bool bNodesPanel, bool bPropsPanel, bool bChartsPanel)
{
	if (!pDoc->bIgnoreFieldChanges)
	{
		AGEUpdateDisplay(pDoc, false, bFlowchart, bNodesPanel, bPropsPanel, bChartsPanel);

		if (bFlowchart && pDoc->bCostumeAnimating)
			pDoc->bReflowAnimation = true;
		
		if (bUndoable)
		{
			AGEUndoData* pData = calloc(1, sizeof(AGEUndoData));
			pData->pPreObject = pDoc->pNextUndoObject;
			pData->pPostObject = StructClone(parse_DynAnimGraph, pDoc->pObject);
			EditCreateUndoCustom(pDoc->emDoc.edit_undo_stack, AGEAnimGraphUndoCB, AGEAnimGraphRedoCB, AGEAnimGraphUndoFreeCB, pData);
			pDoc->pNextUndoObject = StructClone(parse_DynAnimGraph, pDoc->pObject);
		}
	}
}


// This is called by MEField prior to allowing an edit
static bool AGEFieldPreChangeCB(MEField* pField, bool bFinished, AnimGraphDoc* pDoc)
{
	return true;
}


// These are called when an MEField is changed
static void AGENodeFieldChangedCB(MEField* pField, bool bFinished, AnimGraphDoc* pDoc)
{
	AGEAnimGraphChanged(pDoc, bFinished, true, false, false, true);
}
static void AGEFieldChangedCB(MEField* pField, bool bFinished, AnimGraphDoc *pDoc)
{
	AGEAnimGraphChanged(pDoc, bFinished, false, false, false, true);
}

static void AGEInheritFieldChangedCB(MEField *pField, bool bFinished, AnimGraphDoc *pDoc)
{
	//dynAnimGraphApplyInheritBits(pDoc->pObject);
	dynAnimGraphDefaultsFixup(pDoc->pObject);
	AGEAnimGraphChanged(pDoc, bFinished, true, true, true, true);
}

static void AGESetScopeCB(MEField* pField, bool bFinished, AnimGraphDoc* pDoc)
{
	if (!pDoc->bIgnoreFilenameChanges)
	{
		// Update the filename appropriately
		resFixFilename(hAnimGraphDict, pDoc->pObject->pcName, pDoc->pObject);
	}

	// Call on to do regular updates
	AGEFieldChangedCB(pField, bFinished, pDoc); }

static void AGESetNameCB(MEField* pField, bool bFinished, AnimGraphDoc* pDoc)
{
	MEFieldFixupNameString(pField, &pDoc->pObject->pcName);

	// When the name changes, change the title of the window
	//ui_WindowSetTitle(pDoc->pMainWindow, pDoc->pObject->pcName);

	// Make sure the browser picks up the new def name if the name changed
	sprintf(pDoc->emDoc.doc_name, "%s", pDoc->pObject->pcName);
	sprintf(pDoc->emDoc.doc_display_name, "%s", pDoc->pObject->pcName);
	pDoc->emDoc.name_changed = 1;

	// Call the scope function to avoid duplicating logic
	AGESetScopeCB(pField, bFinished, pDoc);
}


static void AGEUpdateDisplay(AnimGraphDoc* pDoc, bool bImmediate, bool bFlowchart, bool bNodesPanel, bool bPropsPanel, bool bChartsPanel)
{
	if (bFlowchart)
		pDoc->bNeedsFlowchartReflow = true;
	if (bNodesPanel)
		pDoc->bNeedsNodePanelReflow = true;
	if (bPropsPanel)
		pDoc->bNeedsPropsPanelReflow = true;
	if (bChartsPanel)
		pDoc->bNeedsChartsPanelReflow = true;
	if (!bImmediate)
	{
		pDoc->bNeedsDisplayUpdate = true;
	}
	else
	{
		bool bNeedsSearchReflow =	pDoc->bNeedsChartsPanelReflow	||
									pDoc->bNeedsNodePanelReflow		||
									pDoc->bNeedsPropsPanelReflow;
		int i;

		// Ignore changes while UI refreshes
		pDoc->bIgnoreFieldChanges = true;

		// Refresh doc-level fields
		for(i=eaSize(&pDoc->eaDocFields)-1; i>=0; --i)
		{
			MEFieldSetAndRefreshFromData(pDoc->eaDocFields[i], pDoc->pOrigObject, pDoc->pObject);
		}


		// Update non-field UI components
		ui_GimmeButtonSetName(pDoc->pFileButton, pDoc->pObject->pcName);
		ui_GimmeButtonSetReferent(pDoc->pFileButton, pDoc->pObject);
		ui_LabelSetText(pDoc->pFilenameLabel, pDoc->pObject->pcFilename);

		if (pDoc->bNeedsFlowchartReflow)
		{
			if (!GET_REF(pDoc->pObject->hTemplate))
				ui_WidgetSkin(UI_WIDGET(pDoc->pViewPane), &ageBadDataBackground);
			else
				ui_WidgetSkin(UI_WIDGET(pDoc->pViewPane), &ageViewPaneSkin);

			AGEReflowFlowchart(pDoc);
		}

		pDoc->bNeedsFlowchartReflow = false;

		// Reflow nodes panel
		if (pDoc->bNeedsNodePanelReflow)
		{
			UIExpanderGroup *pOldGroup = NULL;
			if (pDoc->pNodesPanel)
			{
				pOldGroup = emPanelGetExpanderGroup(pDoc->pNodesPanel);
				eaDestroyEx(&pDoc->eaNodeFields, MEFieldDestroy);
				eaDestroyEx(&pDoc->eaMoveFields, AnimEditor_MoveMEFieldDestroy);
				eaDestroyEx(&pDoc->eaMoveFxFields, AnimEditor_MoveFxMEFieldDestroy);
				eaDestroyEx(&pDoc->eaPathFields, AnimEditor_PathMEFieldDestroy);
				eaFindAndRemove(&pDoc->emDoc.em_panels, pDoc->pNodesPanel);
				eaClear(&pDoc->eaInactiveNodesPanelWidgets);
				emPanelFree(pDoc->pNodesPanel);
			}
			pDoc->pNodesPanel = AGEInitNodesPanel(pDoc);
			if (pOldGroup) {
				//the pOldGroup's children will be wrong until the document refreshes,
				//but this is required to preserve the scrollbar's height when recreating the panel
				assert(!emPanelGetExpanderGroup(pDoc->pNodesPanel));
				emPanelSetExpanderGroup(pDoc->pNodesPanel, pOldGroup);
			}
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
					pDoc->pOrigObject->eaNodes &&
					eaSize(&pDoc->pOrigObject->eaNodes) > pMoveField->iNodeIndex &&
					eaSize(&pDoc->pOrigObject->eaNodes[pMoveField->iNodeIndex]->eaMove) > pMoveField->iMoveIndex
					) ?
						pDoc->pOrigObject->eaNodes[pMoveField->iNodeIndex]->eaMove[pMoveField->iMoveIndex] :
						NULL;
				DynAnimGraphMove* pMove =
					(
					pDoc->pObject &&
					pDoc->pObject->eaNodes &&
					eaSize(&pDoc->pObject->eaNodes) > pMoveField->iNodeIndex &&
					eaSize(&pDoc->pObject->eaNodes[pMoveField->iNodeIndex]->eaMove) > pMoveField->iMoveIndex
					) ?
						pDoc->pObject->eaNodes[pMoveField->iNodeIndex]->eaMove[pMoveField->iMoveIndex] :
						NULL;

				assert(pMove);

				MEFieldSetAndRefreshFromData(pMoveField->pField, pOrigMove, pMove);
				if (GET_REF(pMove->hMove) &&
					!dynAnimGraphGroupMoveVerifyChance(pDoc->pObject, pDoc->pObject->eaNodes[pMoveField->iNodeIndex], false))
					ui_WidgetSkin(pMoveField->pField->pUIWidget, &ageBadDataButton);
				else
					ui_WidgetSkin(pMoveField->pField->pUIWidget, NULL );
			}
			FOR_EACH_END;

			FOR_EACH_IN_EARRAY(pDoc->eaMoveFxFields, MoveFxMEField, pMoveFxField)
			{
				DynAnimGraphFxEvent* pOrigFx =
					(
					pDoc->pOrigObject &&
					pDoc->pOrigObject->eaNodes &&
					eaSize(&pDoc->pOrigObject->eaNodes) > pMoveFxField->iNodeIndex &&
					eaSize(&pDoc->pOrigObject->eaNodes[pMoveFxField->iNodeIndex]->eaMove) > pMoveFxField->iMoveIndex &&
					eaSize(&pDoc->pOrigObject->eaNodes[pMoveFxField->iNodeIndex]->eaMove[pMoveFxField->iMoveIndex]->eaFxEvent) > pMoveFxField->iFxIndex
					) ?
						pDoc->pOrigObject->eaNodes[pMoveFxField->iNodeIndex]->eaMove[pMoveFxField->iMoveIndex]->eaFxEvent[pMoveFxField->iFxIndex] :
						NULL;
				DynAnimGraphFxEvent* pFx =
					(
					pDoc->pObject &&
					pDoc->pObject->eaNodes &&
					eaSize(&pDoc->pObject->eaNodes) > pMoveFxField->iNodeIndex &&
					eaSize(&pDoc->pObject->eaNodes[pMoveFxField->iNodeIndex]->eaMove) > pMoveFxField->iMoveIndex &&
					eaSize(&pDoc->pObject->eaNodes[pMoveFxField->iNodeIndex]->eaMove[pMoveFxField->iMoveIndex]->eaFxEvent) > pMoveFxField->iFxIndex
					) ?
						pDoc->pObject->eaNodes[pMoveFxField->iNodeIndex]->eaMove[pMoveFxField->iMoveIndex]->eaFxEvent[pMoveFxField->iFxIndex] :
						NULL;

				assert(pFx);

				MEFieldSetAndRefreshFromData(pMoveFxField->pField, pOrigFx, pFx);
				if (!dynAnimGraphIndividualMoveVerifyFx(pDoc->pObject, pDoc->pObject->eaNodes[pMoveFxField->iNodeIndex], pDoc->pObject->eaNodes[pMoveFxField->iNodeIndex]->eaMove[pMoveFxField->iMoveIndex], pDoc->pObject->eaNodes[pMoveFxField->iNodeIndex]->eaMove[pMoveFxField->iMoveIndex]->eaFxEvent[pMoveFxField->iFxIndex], false)) {
					if (TRUE_THEN_RESET(pMoveFxField->bValid)) {
						AGEUpdateDisplay(pDoc, false, true, false, false, false);
					}
					ui_WidgetSkin(pMoveFxField->pField->pUIWidget, &ageBadDataButton);
					ui_WidgetSkin(pMoveFxField->pOptWidget,        &ageBadDataButton);
				} else {
					if (FALSE_THEN_SET(pMoveFxField->bValid)) {
						AGEUpdateDisplay(pDoc, false, true, false, false, false);
					}
					ui_WidgetSkin(pMoveFxField->pField->pUIWidget, NULL );
					ui_WidgetSkin(pMoveFxField->pOptWidget,        NULL );
				}
			}
			FOR_EACH_END;

			FOR_EACH_IN_EARRAY(pDoc->eaPathFields, PathMEField, pPathField)
			{
				DynAnimGraphPath *pOrigPath =
					(
					pDoc->pOrigObject &&
					pDoc->pOrigObject->eaNodes &&
					eaSize(&pDoc->pOrigObject->eaNodes) > pPathField->iNodeIndex &&
					eaSize(&pDoc->pOrigObject->eaNodes[pPathField->iNodeIndex]->eaPath) > pPathField->iPathIndex
					) ?
						pDoc->pOrigObject->eaNodes[pPathField->iNodeIndex]->eaPath[pPathField->iPathIndex] :
						NULL;
				DynAnimGraphPath *pPath =
					(
					pDoc->pObject &&
					pDoc->pObject->eaNodes &&
					eaSize(&pDoc->pObject->eaNodes) > pPathField->iNodeIndex &&
					eaSize(&pDoc->pObject->eaNodes[pPathField->iNodeIndex]->eaPath) > pPathField->iPathIndex
					) ?
						pDoc->pObject->eaNodes[pPathField->iNodeIndex]->eaPath[pPathField->iPathIndex] :
						NULL;

				assert(pPath);

				MEFieldSetAndRefreshFromData(pPathField->pField, pOrigPath, pPath);
				if (!dynAnimGraphGroupPathVerify(pDoc->pObject, pDoc->pObject->eaNodes[pPathField->iNodeIndex], false))
					//!dynAnimGraphIndividualPathVerify(pDoc->pObject, pDoc->pObject->eaNodes[pPathField->iNodeIndex], pPath, false))
					ui_WidgetSkin(pPathField->pField->pUIWidget, &ageBadDataButton);
				else
					ui_WidgetSkin(pPathField->pField->pUIWidget, NULL );
			}
			FOR_EACH_END;
		}
		pDoc->bNeedsNodePanelReflow = false;

		// Reflow properties panel
		if (pDoc->bNeedsPropsPanelReflow)
		{
			UIExpanderGroup *pOldGroup = NULL;
			if (pDoc->pPropsPanel)
			{
				pOldGroup = emPanelGetExpanderGroup(pDoc->pPropsPanel);
				eaDestroyEx(&pDoc->eaPropFields, MEFieldDestroy);
				eaFindAndRemove(&pDoc->emDoc.em_panels, pDoc->pPropsPanel);
				eaClear(&pDoc->eaInactivePropPanelWidgets);
				emPanelFree(pDoc->pPropsPanel);
			}
			pDoc->pPropsPanel = AGEInitPropertiesPanel(pDoc);
			if (pOldGroup) {
				//the pOldGroup's children will be wrong until the document refreshes,
				//but this is required to preserve the scrollbar's height when recreating the panel
				assert(!emPanelGetExpanderGroup(pDoc->pPropsPanel));
				emPanelSetExpanderGroup(pDoc->pPropsPanel, pOldGroup);
			}
			FOR_EACH_IN_EARRAY(pDoc->eaInactivePropPanelWidgets, UIWidget, pUIWidget)
			{
				ui_SetActive(pUIWidget, false);
			}
			FOR_EACH_END;
			eaInsert(&pDoc->emDoc.em_panels, pDoc->pPropsPanel, 0);
		}
		else
		{
			// Not valid, parent is not necessarily pOrigObject
// 			for(i=eaSize(&pDoc->eaPropFields)-1; i>=0; --i)
// 			{
// 				MEFieldSetAndRefreshFromData(pDoc->eaPropFields[i], pDoc->pOrigObject, pDoc->pObject);
// 			}
		}
		pDoc->bNeedsPropsPanelReflow = false;

		//reflow charts panel
		if (pDoc->bNeedsChartsPanelReflow)
		{
			if (pDoc->pChartsPanel)
			{
				eaDestroyEx(&pDoc->eaChartFields, MEFieldDestroy);
				eaFindAndRemove(&pDoc->emDoc.em_panels, pDoc->pChartsPanel);
				eaClear(&pDoc->eaInactiveChartsPanelWidgets);
				emPanelFree(pDoc->pChartsPanel);
			}
			pDoc->pChartsPanel = AGEInitChartsPanel(pDoc);
			FOR_EACH_IN_EARRAY(pDoc->eaInactiveChartsPanelWidgets, UIWidget, pUIWidget)
			{
				ui_SetActive(pUIWidget, false);
			}
			FOR_EACH_END;
			eaPush(&pDoc->emDoc.em_panels, pDoc->pChartsPanel);
		}
		pDoc->bNeedsChartsPanelReflow = false;

		//reflow search panel
		if (bNeedsSearchReflow)
		{
			if (pDoc->pSearchPanel)
			{
				eaFindAndRemove(&pDoc->emDoc.em_panels, pDoc->pSearchPanel);
				emPanelFree(pDoc->pSearchPanel);
			}
			pDoc->pSearchPanel = AGEInitSearchPanel(pDoc);
			eaPush(&pDoc->emDoc.em_panels, pDoc->pSearchPanel);
		}
		bNeedsSearchReflow = false;

		// Update saved flag
		// Must fixup indices first for structcompare to be accurate
		pDoc->emDoc.saved = pDoc->pOrigObject && (StructCompare(parse_DynAnimGraph, pDoc->pOrigObject, pDoc->pObject, 0, 0, 0) == 0);


		emRefreshDocumentUI();

		// Start paying attention to changes again
		pDoc->bIgnoreFieldChanges = false;
		pDoc->iOldFocusIndex = pDoc->iFocusIndex;
	}
}

static void AGEOpenTemplate(UIButton* pButton, AnimGraphDoc* pDoc)
{
	if (GET_REF(pDoc->pObject->hTemplate))
	{
		emOpenFileEx(GET_REF(pDoc->pObject->hTemplate)->pcName, "AnimTemplate");
	}
}

static bool AGEChooseTemplateCallback(EMPicker *picker, EMPickerSelection **selections, AnimGraphDoc* pDoc)
{
	char cTemplateName[1024];

	if (!eaSize(&selections))
		return false;

	getFileNameNoExt(cTemplateName, selections[0]->doc_name);
	SET_HANDLE_FROM_STRING(hAnimTemplateDict, cTemplateName, pDoc->pObject->hTemplate);
	dynAnimGraphTemplateChanged(pDoc->pObject);
	if (GET_REF(pDoc->costumeData.hCostume)) {
		AnimEditor_InitCostume(&pDoc->costumeData);
	}
	AGECreateTracks(pDoc);
	AGEAnimGraphChanged(pDoc, true, true, true, true, false);

	return true;
}

static void AGEChooseTemplate(UIWidget* pWidget, SA_PARAM_NN_VALID AnimGraphDoc* pDoc)
{
	if (pDoc) {
		EMPicker* texturePicker = emPickerGetByName( "AnimTemplate" );

		if (texturePicker)
			emPickerShow(texturePicker, NULL, false, AGEChooseTemplateCallback, pDoc);
	}
}

static bool AGEChooseMoveCallback(EMPicker *picker, EMPickerSelection **selections, DynAnimGraphMove* pMove)
{
	AnimGraphDoc* pDoc = (AnimGraphDoc*)emGetActiveEditorDoc();

	if (!eaSize(&selections))
		return false;

	SET_HANDLE_FROM_STRING(DYNMOVE_DICTNAME, selections[0]->doc_name, pMove->hMove);
	AGEAnimGraphChanged(pDoc, true, true, true, false, false);
	return true;
}

static void AGEChooseMove(UIWidget* pWidget, SA_PARAM_NN_VALID DynAnimGraphMove* pMove)
{
	AnimGraphDoc* pDoc = (AnimGraphDoc*)emGetActiveEditorDoc();
	if (pDoc) {
		EMPicker* pMovePicker = emPickerGetByName( "Move Library" );

		if (pMovePicker)
			emPickerShow(pMovePicker, NULL, false, AGEChooseMoveCallback, pMove);
	}
}

static void AGEAddMove(UIButton* pButton, DynAnimGraphNode* pNode)
{
	AnimGraphDoc* pDoc = (AnimGraphDoc*)emGetActiveEditorDoc();
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
			emPickerShow(pMovePicker, NULL, false, AGEChooseMoveCallback, pMove);

		AGEAnimGraphChanged(pDoc, false, true, true, false, false);
	}
}

static void AGERemoveMove(UIButton* pButton, DynAnimGraphMove* pMove)
{
	AnimGraphDoc* pDoc = (AnimGraphDoc*)emGetActiveEditorDoc();
	if (pDoc) {
		bool bFound = false;
		assert(pDoc);

		FOR_EACH_IN_EARRAY(pDoc->pObject->eaNodes, DynAnimGraphNode, pNode)
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
		AGEAnimGraphChanged(pDoc, true, true, true, false, false);
	}
}

static void AGEOpenMove(UIButton* pButton, DynAnimGraphMove* pMove)
{
	if (pMove && GET_REF(pMove->hMove))
		emOpenFileEx(GET_REF(pMove->hMove)->pcName, DYNMOVE_TYPENAME);
}

static void AGEMoveChangeChance(MEField* pField, bool bFinished, AnimGraphDoc *pDoc)
{
	AGEFieldChangedCB(pField, bFinished, pDoc);
}

static void AGEMoveBiasToggled(UICheckButton *pButton, UserData pData)
{
	AnimGraphDoc *pDoc = (AnimGraphDoc*)emGetActiveEditorDoc();
	DynAnimGraph *pGraph = pDoc->pObject;
	DynAnimGraphNode *pNode = pGraph ? pGraph->eaNodes[((U32)PTR_TO_U32(pData))/1000] : NULL;
	DynAnimGraphMove *pMove = pNode ? pNode->eaMove[((U32)PTR_TO_U32(pData))%1000] : NULL;

	if (pMove)
	{
		FOR_EACH_IN_EARRAY(pNode->eaMove, DynAnimGraphMove, pResetMove) {
			pResetMove->bEditorPlaybackBias = false;
		} FOR_EACH_END;
		pMove->bEditorPlaybackBias = ui_CheckButtonGetState(pButton);
		AGEAnimGraphChanged(pDoc, true, true, false, false, false);
	}
}

static void AGEPathBiasToggled(UICheckButton *pButton, UserData pData)
{
	AnimGraphDoc *pDoc = (AnimGraphDoc*)emGetActiveEditorDoc();
	DynAnimGraph *pGraph = pDoc->pObject;
	DynAnimGraphNode *pNode = pGraph ? pGraph->eaNodes[((U32)PTR_TO_U32(pData))/1000] : NULL;
	DynAnimGraphPath *pPath = pNode ? pNode->eaPath[((U32)PTR_TO_U32(pData))%1000] : NULL;

	if (pPath)
	{
		FOR_EACH_IN_EARRAY(pNode->eaPath, DynAnimGraphPath, pResetPath) {
			pResetPath->bEditorPlaybackBias = false;
		} FOR_EACH_END;
		pPath->bEditorPlaybackBias = ui_CheckButtonGetState(pButton);
		AGEAnimGraphChanged(pDoc, true, true, false, false, false);
	}
}

//////////////////////////////////////////////////////////////////////////

static void AGEAddPath(UIButton* pButton, DynAnimGraphNode* pNode)
{
	//dummy function that should never be called
	//changes to the path structure should only be allowed through the template editor
	;
}

static void AGERemovePath(UIButton* pButton, DynAnimGraphPath* pRandPath)
{
	//dummy function that should never be called
	//changes to the path structure should only be allowed through the template editor
	;
}

static void AGEPathChangeChance(MEField* pField, bool bFinished, AnimGraphDoc *pDoc)
{
	AGEFieldChangedCB(pField, bFinished, pDoc);
}

//////////////////////////////////////////////////////////////////////////

static bool AGEChooseNodePostIdleCallback(EMPicker *picker, EMPickerSelection **selections, DynAnimGraphNode* pNode)
{
	AnimGraphDoc *pDoc = (AnimGraphDoc*)emGetActiveEditorDoc();
	char cPostIdleName[1024];

	if (!eaSize(&selections))
		return false;

	getFileNameNoExt(cPostIdleName, selections[0]->doc_name);
	SET_HANDLE_FROM_STRING(hAnimGraphDict, cPostIdleName, pNode->hPostIdle);
	AGEAnimGraphChanged(pDoc, true, true, true, false, false);
	return true;
}

static void AGEChooseNodePostIdle(UIButton *pButton, DynAnimGraphNode *pNode)
{
	AnimGraphDoc *pDoc = (AnimGraphDoc*)emGetActiveEditorDoc();
	if (pDoc) {
		EMPicker* picker = emPickerGetByName( "Animation Graph Library" );
		if (picker)
			emPickerShow(picker, NULL, false, AGEChooseNodePostIdleCallback, pNode);
	}
}

static void AGERemoveNodePostIdle(UIButton *pButton, DynAnimGraphNode *pNode)
{
	AnimGraphDoc *pDoc = (AnimGraphDoc*)emGetActiveEditorDoc();
	if (pDoc) {
		SET_HANDLE_FROM_STRING(hAnimGraphDict, NULL, pNode->hPostIdle);
		AGEAnimGraphChanged(pDoc, true, true, true, false, false);
	}
}

static void AGEOpenNodePostIdle(UIButton *pButton, DynAnimGraphNode *pNode)
{
	if (GET_REF(pNode->hPostIdle))
	{
		emOpenFileEx(GET_REF(pNode->hPostIdle)->pcName, "AnimGraph");
	}
}

//////////////////////////////////////////////////////////////////////////

static void AGEAddFXMessage(UIButton* pButton, DynAnimGraphMove* pMove)
{
	AnimGraphDoc* pDoc = (AnimGraphDoc*)emGetActiveEditorDoc();
	if (pDoc) {
		DynAnimGraphFxEvent* pFxEvent = StructCreate(parse_DynAnimGraphFxEvent);
		pFxEvent->bMessage = true;
		eaPush(&pMove->eaFxEvent, pFxEvent);

		AGEAnimGraphChanged(pDoc, true, false, true, false, false);
	}
}

static void AGEAddFX(UIButton* pButton, DynAnimGraphMove* pMove)
{
	AnimGraphDoc* pDoc = (AnimGraphDoc*)emGetActiveEditorDoc();
	if (pDoc) {
		DynAnimGraphFxEvent* pFxEvent = StructCreate(parse_DynAnimGraphFxEvent);
		eaPush(&pMove->eaFxEvent, pFxEvent);

		AGEAnimGraphChanged(pDoc, true, false, true, false, false);
	}
}

static void AGEAddDocOnEnterFXMessage(UIButton* pButton, AnimGraphDoc *pDoc)
{
	if (pDoc) {
		DynAnimGraphFxEvent* pFxEvent = StructCreate(parse_DynAnimGraphFxEvent);
		pFxEvent->bMessage = true;
		eaPush(&pDoc->pObject->eaOnEnterFxEvent, pFxEvent);

		AGEAnimGraphChanged(pDoc, true, false, false, true, false);
	}
}

static void AGEAddDocOnEnterFX(UIButton* pButton, AnimGraphDoc* pDoc)
{
	if (pDoc) {
		DynAnimGraphFxEvent* pFxEvent = StructCreate(parse_DynAnimGraphFxEvent);
		eaPush(&pDoc->pObject->eaOnEnterFxEvent, pFxEvent);

		AGEAnimGraphChanged(pDoc, true, false, false, true, false);
	}
}

static void AGEAddDocOnExitFXMessage(UIButton* pButton, AnimGraphDoc *pDoc)
{
	if (pDoc) {
		DynAnimGraphFxEvent* pFxEvent = StructCreate(parse_DynAnimGraphFxEvent);
		pFxEvent->bMessage = true;
		eaPush(&pDoc->pObject->eaOnExitFxEvent, pFxEvent);

		AGEAnimGraphChanged(pDoc, true, false, false, true, false);
	}
}

static void AGEAddDocOnExitFX(UIButton* pButton, AnimGraphDoc* pDoc)
{
	if (pDoc) {
		DynAnimGraphFxEvent* pFxEvent = StructCreate(parse_DynAnimGraphFxEvent);
		eaPush(&pDoc->pObject->eaOnExitFxEvent, pFxEvent);

		AGEAnimGraphChanged(pDoc, true, false, false, true, false);
	}
}

static void AGERemoveFxEvent(UIButton* pButton, DynAnimGraphFxEvent* pFxEvent)
{
	AnimGraphDoc* pDoc = (AnimGraphDoc*)emGetActiveEditorDoc();
	if (pDoc) {
		bool bFound = false;
		assert(pDoc);

		FOR_EACH_IN_EARRAY(pDoc->pObject->eaOnEnterFxEvent, DynAnimGraphFxEvent, pWalk)
		{
			if (pFxEvent == pWalk)
			{
				eaRemove(&pDoc->pObject->eaOnEnterFxEvent, ipWalkIndex);
				StructDestroy(parse_DynAnimGraphFxEvent, pWalk);
				bFound = true;
				break;
			}
		}
		FOR_EACH_END;

		if (!bFound) {
			FOR_EACH_IN_EARRAY(pDoc->pObject->eaOnExitFxEvent, DynAnimGraphFxEvent, pWalk)
			{
				if (pFxEvent == pWalk)
				{
					eaRemove(&pDoc->pObject->eaOnExitFxEvent, ipWalkIndex);
					StructDestroy(parse_DynAnimGraphFxEvent, pWalk);

					bFound = true;
					break;
				}
			}
			FOR_EACH_END;
		}
		
		if (!bFound) {
			FOR_EACH_IN_EARRAY(pDoc->pObject->eaNodes, DynAnimGraphNode, pNode)
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

				FOR_EACH_IN_EARRAY(pNode->eaMove, DynAnimGraphMove, pMove)
				{
					FOR_EACH_IN_EARRAY(pMove->eaFxEvent, DynAnimGraphFxEvent, pWalk)
					{
						if (pFxEvent == pWalk)
						{
							eaRemove(&pMove->eaFxEvent, ipWalkIndex);
							StructDestroy(parse_DynAnimGraphFxEvent, pWalk);
							bFound = true;
							break;
						}
					}
					FOR_EACH_END;
				}
				FOR_EACH_END;
				if (bFound)
					break;
			}
			FOR_EACH_END;
		}

		assert(bFound);
		AGEAnimGraphChanged(pDoc, true, false, true, true, false);
	}
}

static void AGEOpenFxEvent(UIButton* pButton, DynAnimGraphFxEvent* pFxEvent)
{
	assert(pFxEvent);
	assert(!pFxEvent->bMessage);
	if (dynFxInfoExists(pFxEvent->pcFx))
	{
		dfxEdit(pFxEvent->pcFx);
	}
}

static bool AGEChooseFxEventCallback(EMPicker *picker, EMPickerSelection **selections, DynAnimGraphFxEvent* pFxEvent)
{
	AnimGraphDoc* pDoc = (AnimGraphDoc*)emGetActiveEditorDoc();
	char cFXName[1024];

	if (!eaSize(&selections))
		return false;

	getFileNameNoExt(cFXName, selections[0]->doc_name);
	pFxEvent->pcFx = allocAddString(cFXName);
	AGEAnimGraphChanged(pDoc, true, false, true, true, false);
	return true;
}

static void AGEChooseFxEvent(UIButton* pButton, DynAnimGraphFxEvent* pFxEvent)
{
	EMPicker* picker = emPickerGetByName( "FX Library" );

	if (picker)
		emPickerShow(picker, NULL, false, AGEChooseFxEventCallback, pFxEvent);
}

//////////////////////////////////////////////////////////////////////////
static void AGEAddImpact(UIButton *pButton, DynAnimGraphNode *pNode)
{
	AnimGraphDoc *pDoc = (AnimGraphDoc*)emGetActiveEditorDoc();
	if (pDoc) {
		DynAnimGraphTriggerImpact *pImpact = StructCreate(parse_DynAnimGraphTriggerImpact);
		pImpact->pcBone = allocAddString("WepR");
		eaPush(&pNode->eaGraphImpact, pImpact);

		AGEAnimGraphChanged(pDoc, true, false, true, false, false);
	}
}

static void AGERemoveImpact(UIButton *pButton, DynAnimGraphTriggerImpact *pImpact)
{
	AnimGraphDoc *pDoc = (AnimGraphDoc*)emGetActiveEditorDoc();
	if (pDoc) {
		bool bFound = false;
		assert(pDoc);

		FOR_EACH_IN_EARRAY(pDoc->pObject->eaNodes, DynAnimGraphNode, pNode)
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
		AGEAnimGraphChanged(pDoc, true, false, true, true, false);
	}
}

//////////////////////////////////////////////////////////////////////////
static void AGEAddDocSuppress(UIButton* pButton, AnimGraphDoc *pDoc)
{
	if (pDoc) {
		DynAnimGraphSuppress* pSuppress = StructCreate(parse_DynAnimGraphSuppress);
		eaPush(&pDoc->pObject->eaSuppress, pSuppress);

		AGEAnimGraphChanged(pDoc, true, false, false, true, false);
	}
}

static void AGERemoveSuppress(UIButton* pButton, DynAnimGraphSuppress* pSuppress)
{
	AnimGraphDoc* pDoc = (AnimGraphDoc*)emGetActiveEditorDoc();
	if (pDoc) {
		bool bFound = false;
		assert(pDoc);

		FOR_EACH_IN_EARRAY(pDoc->pObject->eaSuppress, DynAnimGraphSuppress, pWalk)
		{
			if (pSuppress == pWalk)
			{
				eaRemove(&pDoc->pObject->eaSuppress, ipWalkIndex);
				StructDestroy(parse_DynAnimGraphSuppress, pWalk);

				bFound = true;
				break;
			}
		}
		FOR_EACH_END;

// 		FOR_EACH_IN_EARRAY(pDoc->pObject->eaNodes, DynAnimGraphNode, pNode)
// 		{
// 			FOR_EACH_IN_EARRAY(pNode->eaSuppress, DynAnimGraphSuppress, pWalk)
// 			{
// 				if (pSuppress == pWalk)
// 				{
// 					eaRemove(&pNode->eaSuppress, ipWalkIndex);
// 					StructDestroy(parse_DynAnimGraphSuppress, pWalk);
// 
// 					bFound = true;
// 					break;
// 				}
// 			}
// 			FOR_EACH_END;
// 
// 			if (bFound)
// 				break;
// 		}
// 		FOR_EACH_END;
// 
		assert(bFound);
		AGEAnimGraphChanged(pDoc, true, false, true, true, false);
	}
}


static void AGEAddDocStance(UIButton* pButton, AnimGraphDoc *pDoc)
{
	if (pDoc) {
		DynAnimGraphStance* pStance = StructCreate(parse_DynAnimGraphStance);
		eaPush(&pDoc->pObject->eaStance, pStance);

		AGEAnimGraphChanged(pDoc, true, false, false, true, false);
	}
}

static void AGEChangeStance(UIComboBox *pComboBox, void* pFakePtr)
{
	AnimGraphDoc* pDoc = (AnimGraphDoc*)emGetActiveEditorDoc();
	if (pDoc) {
		bool foundIt = false;
		char *pcSelected = NULL;

		ui_ComboBoxGetSelectedsAsString(pComboBox, &pcSelected);
		if (pcSelected)
		{
			const char *pcSelectedFromStringBase;
			pcSelectedFromStringBase = allocAddString(pcSelected);
			FOR_EACH_IN_EARRAY(pDoc->pObject->eaStance, DynAnimGraphStance, pStance)
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
				pDoc->pObject->eaStance[PTR_TO_U32(pFakePtr)]->pcStance = pcSelectedFromStringBase;
				AGEAnimGraphChanged(pDoc, true, false, false, true, false);
			}
			else
			{
				AGEAnimGraphChanged(pDoc, false, false, false, true, false);
			}
		}
		else
		{
			AGEAnimGraphChanged(pDoc, false, false, false, true, false);
		}
	}
}

static void AGERemoveStance(UIButton* pButton, DynAnimGraphStance* pStance)
{
	AnimGraphDoc* pDoc = (AnimGraphDoc*)emGetActiveEditorDoc();
	if (pDoc) {
		bool bFound = false;
		assert(pDoc);

		FOR_EACH_IN_EARRAY(pDoc->pObject->eaStance, DynAnimGraphStance, pWalk)
		{
			if (pStance == pWalk)
			{
				eaRemove(&pDoc->pObject->eaStance, ipWalkIndex);
				StructDestroy(parse_DynAnimGraphStance, pWalk);

				bFound = true;
				break;
			}
		}
		FOR_EACH_END;

		assert(bFound);
		AGEAnimGraphChanged(pDoc, true, false, true, true, false);
	}
}

void AGEDrawGhosts(AnimGraphDoc* pDoc)
{
	F32 fAnimTime = pDoc->playbackInfo.bPaused?0.0f:pDoc->playbackInfo.fPlaybackRate * gGCLState.frameElapsedTime;

	AnimEditor_DrawCostumeGhosts(pDoc->costumeData.pGraphics, fAnimTime);

	if (ageDrawGrid)
	{
		AGEDrawGrid();
	}
}

void AGESetNodeBlend(AnimGraphDoc* pDoc, DynAnimGraphNode* pNode, F32 fBlendFactor)
{
	FOR_EACH_IN_EARRAY(pDoc->pFlowchart->nodeWindows, UIFlowchartNode, pUINode)
	{
		AGENodeData* pNodeData = pUINode->userData;
		if (pNodeData && pNodeData->pNode == pNode)
		{
			char cMove[1024];
			sprintf(cMove, " Blend %1.0f%%", fBlendFactor*100);
			ui_LabelSetText(pNodeData->pBlendLabel, cMove);
			ui_ProgressBarSet(pNodeData->pBlendBar, fBlendFactor);
		}
	}
	FOR_EACH_END;
}

void AGESetCurrentNodeAndMove(AnimGraphDoc* pDoc, const DynAnimGraphNode* pNode, const DynAnimGraphMove* pMove, DynAnimGraphUpdater* pUpdater)
{
	FOR_EACH_IN_EARRAY(pDoc->pFlowchart->nodeWindows, UIFlowchartNode, pUINode)
	{
		AGENodeData* pNodeData = pUINode->userData;
		assert(pNodeData);
		if (pNodeData->pNode == pNode)
		{
			int iMoveIndex = eaFind(&pNode->eaMove, pMove);
			F32 fLength = dynAnimGraphUpdaterGetMoveLength(pUpdater);
			F32 fTime = dynAnimGraphUpdaterGetMoveTime(pUpdater);

			pUINode->backgroundScrollSprite = gBackgroundScrollSprite;

			if(fLength > 0) {
				pUINode->backgroundScrollXPercent = fTime/fLength;			
				gfxfont_SetFontEx(&g_font_Mono, false, false, 1, false, 0xFFFFFFFF, 0xFFFFFFFF);
				gfxfont_Printf(10, g_ui_State.viewportMin[1]+25, GRAPHICSLIB_Z+100, 1, 1, 0, "Node \"%s\" Frame: %g / %g", pNode->pcName, fTime, fLength);
				gfxfont_Printf(10, g_ui_State.viewportMin[1]+50, GRAPHICSLIB_Z+100, 1, 1, 0, "Graph Frame: %f", pUpdater->fTimeOnCurrentGraph);
			} else {
				pUINode->backgroundScrollXPercent = 0;
			}

			pUINode->backgroundScrollWidthPercent = 0.05f;
			FOR_EACH_IN_EARRAY(pNodeData->eaMoveLabels, UILabel, pLabel)
			{
				if (ipLabelIndex == iMoveIndex)
					ui_LabelSetFont(pLabel, gActiveMoveFont);
				else
					ui_LabelSetFont(pLabel, NULL);
			}
			FOR_EACH_END;
		}
		else
		{
			pUINode->backgroundScrollSprite = NULL;
			FOR_EACH_IN_EARRAY(pNodeData->eaMoveLabels, UILabel, pLabel)
				ui_LabelSetFont(pLabel, NULL);
			FOR_EACH_END;
		}
	}
	FOR_EACH_END;
}

static void AGEPlayButtonCB(UIButton* pButton, AnimGraphDoc* pDoc)
{
	pDoc->playbackInfo.bPaused = false;
}

static void AGEPauseButtonCB(UIButton* pButton, AnimGraphDoc* pDoc)
{
	pDoc->playbackInfo.bPaused = true;
}

static void AGEStopButtonCB(UIButton* pButton, AnimGraphDoc* pDoc)
{
	pDoc->playbackInfo.bPaused = true;
	pDoc->bForceGraphChange = true;
}

#define AGETimelineTime(x) ((int)(x * 1000.0f))
#define AGERealTimeFromTimeline(x) (((F32)x) * 0.001f)

static DynSkeleton *AGEFindFirstSkeletonForGraph(DynSkeleton *pSkeleton, DynAnimGraph *pAnimGraph)
{
	const WLCostume* pCostume  = pSkeleton ? GET_REF(pSkeleton->hCostume) : NULL;
	const SkelInfo*  pSkelInfo = pCostume  ? GET_REF(pCostume->hSkelInfo) : NULL;

	assert(pSkeleton);

	if (pSkelInfo)
	{
		bool validSequencers = true;

		//check that every possible move is a valid sequencer for the skeleton
		FOR_EACH_IN_EARRAY(pAnimGraph->eaNodes, DynAnimGraphNode, curNode)
		{
			FOR_EACH_IN_EARRAY(curNode->eaMove, DynAnimGraphMove, curMoveNode)
			{
				DynMove *curMove = GET_REF(curMoveNode->hMove);
				U32 uiDynMoveSeqIndex, uiNumDynMoveSeqs;
				U32 uiBestDynMoveSeqIndex, uiBestDynMoveSeqRank;

				uiBestDynMoveSeqRank = 0xFFFFFFFF;
				uiNumDynMoveSeqs = eaSize(&curMove->eaDynMoveSeqs);
				for (uiDynMoveSeqIndex = 0; uiDynMoveSeqIndex < uiNumDynMoveSeqs; ++uiDynMoveSeqIndex)
				{
					U32 uiRank = 0;
					if (wlSkelInfoFindSeqTypeRank(pSkelInfo, curMove->eaDynMoveSeqs[uiDynMoveSeqIndex]->pcDynMoveSeq, &uiRank) && uiRank < uiBestDynMoveSeqRank)
					{
						uiBestDynMoveSeqIndex = uiDynMoveSeqIndex;
						uiBestDynMoveSeqRank = uiRank;
					}
				}

				if (uiBestDynMoveSeqRank == 0xFFFFFFFF)
				{
					validSequencers = false;
					break;
				}
			}
			FOR_EACH_END;
		}
		FOR_EACH_END;

		//given that the animation is for this skeleton, return it
		if (validSequencers)
			return pSkeleton;
	}

	//recurse on children
	FOR_EACH_IN_EARRAY(pSkeleton->eaDependentSkeletons, DynSkeleton, pChildSkeleton)
	{
		return AGEFindFirstSkeletonForGraph(pChildSkeleton, pAnimGraph);
	}
	FOR_EACH_END;

	return NULL;
}

static void AGEDisableAllButGraphSkeletonAnimations(DynSkeleton *pSkeleton, DynSkeleton *pGraphSkeleton)
{
	assert(pSkeleton);

	//turn off animation
	if (pSkeleton != pGraphSkeleton)
		pSkeleton->bAnimDisabled = true;
	else
		pSkeleton->bAnimDisabled = false;

	//recurse on children
	FOR_EACH_IN_EARRAY(pSkeleton->eaDependentSkeletons, DynSkeleton, pChildSkeleton)
	{
		AGEDisableAllButGraphSkeletonAnimations(pChildSkeleton, pGraphSkeleton);
	}
	FOR_EACH_END;
}

static void AGESetForceOverrideGraphs(DynSkeleton *pSkeleton, DynAnimGraphUpdater *pUpdater, DynAnimGraph *pAnimGraph)
{
	assert(pSkeleton);
	dynAnimGraphUpdaterSetForceOverrideGraph(pSkeleton, pUpdater, pAnimGraph);
	//FOR_EACH_IN_EARRAY(pSkeleton->eaDependentSkeletons, DynSkeleton, pChildSkeleton)
	//{
	//	AGESetForceOverrideGraphs(pChildSkeleton, pChildSkeleton->eaAGUpdater[0], pAnimGraph);
	//}
	//FOR_EACH_END;
}

void AGEOncePerFrame(AnimGraphDoc* pDoc)
{
	F32 fAnimTime = pDoc->playbackInfo.bPaused?0.0f:pDoc->playbackInfo.fPlaybackRate * gGCLState.frameElapsedTime;
	{
		float fR = sin( timeGetTime() / 250.0 ) / 2 + 0.5;
		ui_SkinSetBackground( &ageBadDataBackground, ColorLerp(ageBadDataColor[ 0 ], ageBadDataColor[ 1 ], fR));
		ui_SkinSetButton( &ageBadDataButton, ColorLerp(ageBadDataColor[ 1 ], ageBadDataColor[ 0 ], fR));
		ui_SkinSetEntry( &ageBadDataButton, ColorLerp(ageBadDataColor[ 1 ], ageBadDataColor[ 0 ], fR));
	}

	if (!pDoc->bHasPickedCostume && (AnimGraphDoc*)emGetActiveEditorDoc() == pDoc)
	{
		AnimEditor_LastCostume(NULL, AGEGetCostumePickerData);
		pDoc->bHasPickedCostume = true;
	}

	{
		DynAnimGraph *pDictGraph = RefSystem_ReferentFromString(hAnimGraphDict, pDoc->pObject->pcName);
		if (!pDictGraph && pDoc->pOrigObject)
		{
			pDictGraph = RefSystem_ReferentFromString(hAnimGraphDict, pDoc->pOrigObject->pcName);
		}
		if (pDictGraph && pDictGraph->bNeedsReloadRefresh)
		{
			pDictGraph->bNeedsReloadRefresh = false;
			dynAnimGraphFixup(pDoc->pObject);
			dynAnimGraphDefaultsFixup(pDoc->pObject);
			pDoc->pObject->bNeedsReloadRefresh = false;
			AGEAnimGraphChanged(pDoc, false, true, true, true, true);
		}
	}

	if (pDoc->pObject->bNeedsReloadRefresh)
	{
		dynAnimGraphDefaultsFixup(pDoc->pObject);
		pDoc->pObject->bNeedsReloadRefresh = false;
		AGEAnimGraphChanged(pDoc, false, true, true, true, true);
	}

	if (pDoc->bNeedsDisplayUpdate)
	{
		pDoc->bNeedsDisplayUpdate = false;
		AGEUpdateDisplay(pDoc, true, false, false, false, false);
	}

	if (!ui_ExpanderIsOpened(emPanelGetExpander(pDoc->pPropsPanel)))
		emPanelSetOpened(pDoc->pPropsPanel, true);

	if (!ui_ExpanderIsOpened(emPanelGetExpander(pDoc->pNodesPanel)))
		emPanelSetOpened(pDoc->pNodesPanel, true);

	if (!ui_ExpanderIsOpened(emPanelGetExpander(pDoc->pChartsPanel)))
		emPanelSetOpened(pDoc->pChartsPanel, true);

	if (!ui_ExpanderIsOpened(emPanelGetExpander(pDoc->pSearchPanel)))
		emPanelSetOpened(pDoc->pSearchPanel, true);

	if (GET_REF(pDoc->costumeData.hCostume))
	{
		if (pDoc->costumeData.uiFrameCreated < dynDebugState.uiAnimDataResetFrame)
			AnimEditor_InitCostume(&pDoc->costumeData);

		if (!pDoc->costumeData.pGraphics->costume.pSkel)
		{
			if (FALSE_THEN_SET(ageWarnedNoSkeleton)) {
				Errorf("Anim. graph editor won't display graphics, missing costume or skeleton, please fix validation errors!");
			}
		}
		else  if (	pDoc->pObject->bPartialGraph == false	&&
					dynAnimGraphVerify(pDoc->pObject, false))
		{
			DynSkeleton *pSkeleton = pDoc->costumeData.pGraphics->costume.pSkel;
			if (eaSize(&pSkeleton->eaDependentSkeletons) > 0)
			{
				DynSkeleton *pGraphSkeleton = AGEFindFirstSkeletonForGraph(pSkeleton, pDoc->pObject);
				AGEDisableAllButGraphSkeletonAnimations(pSkeleton, pGraphSkeleton);
				pSkeleton = pGraphSkeleton;
			}
			
			if (pSkeleton && pSkeleton->eaAGUpdater)
			{
				DynAnimGraphUpdater* pUpdater = pSkeleton->eaAGUpdater[0];
				DynAnimGraphUpdaterNode* pCurrentNode = &pUpdater->nodes[0];
				F32 fCurrentTime = pUpdater->fTimeOnCurrentGraph / 30.0f;
				DynFxManager *pFxMan;
				bool bSetForceLoopFlag = false;

				if (pDoc->costumeData.pGraphics->costume.pSkel->pFxManager) {
						pUpdater->pEditorFxManager = pDoc->costumeData.pGraphics->costume.pSkel->pFxManager;
				} else if (pDoc->costumeData.pGraphics->costume.pFxManager) {
						pUpdater->pEditorFxManager = pDoc->costumeData.pGraphics->costume.pFxManager;
				} else {
						pUpdater->pEditorFxManager = NULL;
				}
				pFxMan = pUpdater->pEditorFxManager;

				if(	!pDoc->bCostumeAnimating ||
					!pUpdater->bForceLoopCurrentGraph ||
					pDoc->bForceGraphChange ||
					pDoc->bScrubTime ||
					pDoc->bReflowAnimation)
				{
					pUpdater->pEditorFxManager = NULL;
					pDoc->bFxPlaybackDetached = true;
					if (pDoc->bReflowAnimation)
						fAnimTime = fCurrentTime;
					AGESetForceOverrideGraphs(pSkeleton, pUpdater, pDoc->pObject);
					pUpdater->pcForceLoopFlag = NULL;
					FOR_EACH_IN_EARRAY_FORWARDS(pDoc->eaFlagFrames, FlagFrame, pFrame) {
						if (pFrame->fTime == 0) {
							pUpdater->pcForceLoopFlag = pFrame->pcFlag;
							bSetForceLoopFlag = true;
						}
					} FOR_EACH_END;
					fCurrentTime = 0.0f;
					pDoc->bForceGraphChange = false;
					pDoc->bReflowAnimation = false;
				}
				if (pDoc->bScrubTime)
				{
					fAnimTime = pDoc->fNewScrubTime;
					pDoc->bScrubTime = false;
				}
				if (pSkeleton->pRoot->pParent)
				{
					bool bProcessed = false;

					while (fAnimTime > 0.0f || !bProcessed)
					{
						FlagFrame* pSplittingFrame = NULL;
						F32 fUpdateTime;
						FOR_EACH_IN_EARRAY_FORWARDS(pDoc->eaFlagFrames, FlagFrame, pFrame)
						{
							if (pFrame->fTime > fCurrentTime && pFrame->fTime <= (fCurrentTime + fAnimTime))
							{
								// We found a frame we're going to hit, so split the time up and process it after the update
								pSplittingFrame = pFrame;
								break;
							}
						}
						FOR_EACH_END;

						bProcessed = true;
						if (pSplittingFrame)
						{
							fUpdateTime = pSplittingFrame->fTime - fCurrentTime;
							assert(fUpdateTime > 0.0f);
							fAnimTime -= fUpdateTime;
						}
						else
						{
							fUpdateTime = fAnimTime;
							fAnimTime = 0.0f;
						}

						if (TRUE_THEN_RESET(bSetForceLoopFlag)) {
							dynAnimGraphUpdaterSetFlag(pUpdater, pUpdater->pcForceLoopFlag, 0);
						}

						dynSkeletonUpdate(pSkeleton, fUpdateTime, NULL);

						if (GET_REF(pCurrentNode->pMove_debug->hMove))
							AGESetCurrentNodeAndMove(pDoc, pCurrentNode->pGraphNode, pCurrentNode->pMove_debug, pUpdater);

						if (pDoc->bFxPlaybackDetached)
							pUpdater->pEditorFxManager = pFxMan;

						FOR_EACH_IN_EARRAY(pDoc->pObject->eaNodes, DynAnimGraphNode, pNode)
						{
							AGESetNodeBlend(pDoc, pNode, dynAnimGraphUpdaterDebugCalcBlend(pUpdater, pNode));
						}
						FOR_EACH_END;
						fCurrentTime = pUpdater->fTimeOnCurrentGraph / 30.0f;

						if (pSplittingFrame)
						{
							dynAnimGraphUpdaterSetFlag(pUpdater, pSplittingFrame->pcFlag, 0);
						}
					}

					ui_TimelineSetTime(pDoc->pTimeline, MIN(AGETimelineTime(pUpdater->fTimeOnCurrentGraph), AGETimelineTime(1200.0f)));
					pDoc->bCostumeAnimating = true;
				}
			}
		}
		else
		{
			DynSkeleton* pSkeleton = pDoc->costumeData.pGraphics->costume.pSkel;
			if (pSkeleton && pSkeleton->pDrawSkel)
				dynDrawSkeletonBasePose(pSkeleton->pDrawSkel);
			pDoc->bCostumeAnimating = false;
		}
	}
	else
		pDoc->bCostumeAnimating = false;

	if (!pDoc->bCostumeAnimating)
	{
		AGESetCurrentNodeAndMove(pDoc, NULL, NULL, NULL);
		FOR_EACH_IN_EARRAY(pDoc->pObject->eaNodes, DynAnimGraphNode, pNode)
		{
			AGESetNodeBlend(pDoc, pNode, 0.0f);
		}
		FOR_EACH_END;
	}


	AnimEditor_DrawCostume(&pDoc->costumeData, fAnimTime);


}

//---------------------------------------------------------------------------------------------------
// Timeline Initialization
//---------------------------------------------------------------------------------------------------

#define AGE_GAP 5
#define AGE_PLAY_BUTTON_HEIGHT 20
#define AGE_PLAY_BUTTON_WIDTH 25
#define AGE_PLAY_IMAGE_HEIGHT 15
#define AGE_PLAY_IMAGE_WIDTH 15

static void AGECreateFlagFramesFromTimeline(AnimGraphDoc* pDoc)
{
	eaDestroyEx(&pDoc->eaFlagFrames, NULL);
	FOR_EACH_IN_EARRAY(pDoc->pTimeline->tracks, UITimelineTrack, pTrack)
	{
		FOR_EACH_IN_EARRAY(pTrack->frames, UITimelineKeyFrame, pFrame)
		{
			FlagFrame* pKeyFrame = calloc(sizeof(FlagFrame), 1);
			pKeyFrame->fTime = AGERealTimeFromTimeline(pFrame->time) / 30.0f;
			pKeyFrame->pcFlag = allocAddString(pTrack->name);
			eaPush(&pDoc->eaFlagFrames, pKeyFrame);
		}
		FOR_EACH_END;
	}
	FOR_EACH_END;
	pDoc->bReflowAnimation = true;
}

static void AGETimelineTimeChangedCB(UITimeline *timeline, int time_in, AnimGraphDoc* pDoc)
{
	if (timeline->scrubbing)
	{
		pDoc->bScrubTime = true;
		pDoc->fNewScrubTime = AGERealTimeFromTimeline(time_in) / 30.0f;
	}
}

static void AGETimelineFrameTimeChangedCB(UITimeline *pTimeline, AnimGraphDoc* pDoc)
{
	AGECreateFlagFramesFromTimeline(pDoc);
}

static void AGETimelineSelectionChangedCB(UITimeline *pTimeline, AnimGraphDoc* pDoc)
{
}

static void AGETimelineRightClickCB(void *unused, AnimGraphDoc* pDoc)
{
}

static void AGETimelineFrameRightClickCB(UITimelineKeyFrame* pFrame, AnimGraphDoc* pDoc)
{
	ui_TimelineKeyFrameFree(pFrame);
	AGECreateFlagFramesFromTimeline(pDoc);
}

static void AGETimelineTrackRightClickCB(UITimelineTrack* pTrack, int iTime, AnimGraphDoc* pDoc)
{
	UITimelineKeyFrame* pFrame = ui_TimelineKeyFrameCreate();
	pFrame->time = iTime;
	ui_TimelineTrackAddFrame(pTrack, pFrame);
	ui_TimelineKeyFrameSetRightClickCallback(pFrame, AGETimelineFrameRightClickCB, pDoc);
	AGECreateFlagFramesFromTimeline(pDoc);
}

static void AGEInitTimelinePane(AnimGraphDoc* pDoc)
{
	int y = 4;
	UIPane *pPane;
	UITimeline *pTimeline;
	//UITimelineTrack *pTrack;
	UIButton *pButton;

	pPane = ui_PaneCreate( 0, 35, 1, 150, UIUnitPercentage, UIUnitFixed, UI_PANE_VP_BOTTOM );
	pPane->widget.offsetFrom = UITopLeft;
	pDoc->pTimelinePane = pPane;

	pTimeline = ui_TimelineCreate(0,0,1);
	ui_WidgetSetDimensionsEx( UI_WIDGET( pTimeline ), 1, 1, UIUnitPercentage, UIUnitPercentage );
	ui_TimelineSetTimeChangedCallback(pTimeline, AGETimelineTimeChangedCB, pDoc);
	ui_TimelineSetFrameChangedCallback(pTimeline, AGETimelineFrameTimeChangedCB, pDoc);
	ui_TimelineSetSelectionChangedCallback(pTimeline, AGETimelineSelectionChangedCB, pDoc);
	ui_TimelineSetRightClickCallback(pTimeline, AGETimelineRightClickCB, pDoc);
	pTimeline->title_bar_height = 50.0f;
	pTimeline->continuous = true;
	pTimeline->max_time_mode = true;
	pTimeline->limit_zoom_out = true;
	pTimeline->zoomed_out = true;
	pTimeline->time_ticks_in_units = true;

	pDoc->pTimeline = pTimeline;
	ui_PaneAddChild(pDoc->pTimelinePane, pTimeline);


	AGECreateTracks(pDoc);


	/*
	pTrack = AGECreateTrack("Camera Position", NULL);
	pTrack->draw_lines = true;
	pTrack->prevent_order_changes = true;
	pDoc->pTrackCamPos = pTrack;
	ui_TimelineAddTrack(pTimeline, pTrack);

	pTrack = AGECreateTrack("Look At Position", NULL);
	pTrack->draw_lines = true;
	pTrack->prevent_order_changes = true;
	pDoc->pTrackCamLookAt = pTrack;
	ui_TimelineAddTrack(pTimeline, pTrack);
	*/

	pButton = ui_ButtonCreateImageOnly("play_icon", 0, y, AGEPlayButtonCB, pDoc);
	ui_ButtonSetImageStretch( pButton, true );
	ui_WidgetSetDimensions(UI_WIDGET(pButton), AGE_PLAY_BUTTON_WIDTH, AGE_PLAY_BUTTON_HEIGHT);
	ui_WidgetGroupAdd(&UI_WIDGET(pTimeline)->children, (UIWidget *)pButton);
	pButton = ui_ButtonCreateImageOnly("pause_icon", ui_WidgetGetNextX(UI_WIDGET(pButton)) + AGE_GAP, y, AGEPauseButtonCB, pDoc);
	ui_ButtonSetImageStretch( pButton, true );
	ui_WidgetSetDimensions(UI_WIDGET(pButton), AGE_PLAY_BUTTON_WIDTH, AGE_PLAY_BUTTON_HEIGHT);
	ui_WidgetGroupAdd(&UI_WIDGET(pTimeline)->children, (UIWidget *)pButton);
	pButton = ui_ButtonCreateImageOnly("stop_icon", ui_WidgetGetNextX(UI_WIDGET(pButton)) + AGE_GAP, y, AGEStopButtonCB, pDoc);
	ui_ButtonSetImageStretch( pButton, true );
	ui_WidgetSetDimensions(UI_WIDGET(pButton), AGE_PLAY_BUTTON_WIDTH, AGE_PLAY_BUTTON_HEIGHT);
	ui_WidgetGroupAdd(&UI_WIDGET(pTimeline)->children, (UIWidget *)pButton);
	y += 25;

	pDoc->pPlaybackRateField = MEFieldCreateSimple(kMEFieldType_SliderText, NULL, &pDoc->playbackInfo, parse_AnimEditorPlaybackInfo, "PlaybackRate");
	MEFieldAddToParent(pDoc->pPlaybackRateField, UI_WIDGET(pTimeline), 0, y);
	ui_WidgetSetWidth(pDoc->pPlaybackRateField->pUIWidget, 100.0f);
	ui_SliderTextEntrySetRange(pDoc->pPlaybackRateField->pUISliderText, 0, 2, 1/20.0f);
	MEFieldSetAndRefreshFromData(pDoc->pPlaybackRateField, NULL, &pDoc->playbackInfo);

}

static void AGECreateTracks(AnimGraphDoc* pDoc)
{
	DynAnimTemplate* pTemplate = GET_REF(pDoc->pObject->hTemplate);
	eaDestroyEx(&pDoc->pTimeline->tracks, ui_TimelineTrackFree);

	if (pTemplate)
	{
		FOR_EACH_IN_EARRAY_FORWARDS(pTemplate->eaFlags, const char, pcFlag)
		{
			UITimelineTrack* pTrack = ui_TimelineTrackCreate(pcFlag);
			pTrack->draw_lines = true;
			ui_TimelineAddTrack(pDoc->pTimeline, pTrack);
			ui_TimelineTrackSetRightClickCallback(pTrack, AGETimelineTrackRightClickCB, pDoc);
		}
		FOR_EACH_END;
	}
}


//---------------------------------------------------------------------------------------------------
// UI Initialization
//---------------------------------------------------------------------------------------------------

static void AGEInitViewPane(AnimGraphDoc* pDoc)
{
	F32 canvasX, canvasY, canvasWidth, canvasHeight;
	emGetCanvasSize( &canvasX, &canvasY, &canvasWidth, &canvasHeight );
	pDoc->pViewPane = ui_PaneCreate( 0, 0, 1, (canvasHeight - AGE_TABBAR_HEIGHT) / 2, UIUnitPercentage, UIUnitFixed, 0 );

	pDoc->pViewPane->widget.tickF = AGEFlowchartPaneTick;
	ui_WidgetSkin( UI_WIDGET( pDoc->pViewPane ), &ageViewPaneSkin );
	ui_WidgetSetPositionEx( UI_WIDGET( pDoc->pViewPane ), 0, 0, 0, 0, UIBottomRight );

	pDoc->pFlowchartViewHideButton = ui_ButtonCreateImageOnly( "eui_arrow_large_up", -12, 0, AGEFlowchartViewHideShowClicked, pDoc );
	ui_WidgetSetDimensions( UI_WIDGET( pDoc->pFlowchartViewHideButton ), 32, 12 );
	ui_ButtonSetImageStretch( pDoc->pFlowchartViewHideButton, true );
	pDoc->pFlowchartViewHideButton->widget.xPOffset = 0.5;
	pDoc->pFlowchartViewHideButton->widget.offsetFrom = UITopLeft;
	pDoc->pFlowchartViewHideButton->widget.priority = 100;
	ui_WidgetSetDragCallback( UI_WIDGET( pDoc->pFlowchartViewHideButton ), AGEFlowchartViewExpandDrag, pDoc );
	pDoc->pFlowchartViewHideButton = ui_ButtonCreateImageOnly( "eui_arrow_large_down", -12, 0, AGEFlowchartViewHideShowClicked, pDoc );
	ui_WidgetSetDimensions( UI_WIDGET( pDoc->pFlowchartViewHideButton ), 32, 12 );
	ui_ButtonSetImageStretch( pDoc->pFlowchartViewHideButton, true );
	pDoc->pFlowchartViewHideButton->widget.xPOffset = 0.5;
	pDoc->pFlowchartViewHideButton->widget.offsetFrom = UITopLeft;
	pDoc->pFlowchartViewHideButton->widget.priority = 100;
	ui_WidgetSetDragCallback( UI_WIDGET( pDoc->pFlowchartViewHideButton ), AGEFlowchartViewExpandDrag, pDoc );
	ui_WidgetAddChild( UI_WIDGET( pDoc->pViewPane ), UI_WIDGET( pDoc->pFlowchartViewHideButton ));

	pDoc->pFlowchart = ui_FlowchartCreate( NULL, NULL, NULL, NULL );
	ui_ScrollAreaSetNoCtrlDraggable( UI_SCROLLAREA( pDoc->pFlowchart ), true );
	ui_WidgetSetDimensionsEx( UI_WIDGET( pDoc->pFlowchart ), 1, 1, UIUnitPercentage, UIUnitPercentage );
	ui_WidgetAddChild( UI_WIDGET( pDoc->pViewPane ), UI_WIDGET( pDoc->pFlowchart ));

	ui_FlowchartSetLinkedCallback( pDoc->pFlowchart, AGEFlowchartLinkRequest, pDoc );
	ui_FlowchartSetUnlinkedCallback( pDoc->pFlowchart, AGEFlowchartUnlinkRequest, pDoc );
	ui_FlowchartSetLinkBeginCallback( pDoc->pFlowchart, AGEFlowchartLinkBegin, pDoc );
	ui_FlowchartSetLinkEndCallback( pDoc->pFlowchart, AGEFlowchartLinkEnd, pDoc );
	ui_FlowchartSetNodeRemovedCallback( pDoc->pFlowchart, AGEFlowchartNodeRemoveRequest, pDoc );
	//ui_FlowchartSetNodeRemovedLateCallback( pDoc->pFlowchart, AGEFlowchartNodeRemoved, pDoc );
}

static EMPanel* AGEInitPropertiesPanel(AnimGraphDoc* pDoc)
{
	EMPanel* pPanel;
	UILabel* pLabel;
	UIButton* pButton;
	UISeparator* pSeparator;
	MEField* pField;
	F32 x = X_OFFSET_CONTROL;
	F32 y = 0;
	F32 fBottomY = 0;
	F32 fTopY = 0;
	char buf[1024];

	// Create the panel
	pPanel = emPanelCreate("Graph", "Properties", 0);

	pSeparator = ui_SeparatorCreate(UIHorizontal);
	ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pSeparator), 1.0, 5, UIUnitPercentage, UIUnitFixed);
	emPanelAddChild(pPanel, pSeparator, false);

	y += SEPARATOR_HEIGHT;

	// Name
	pLabel = ui_LabelCreate("Name", 0, y);
	emPanelAddChild(pPanel, pLabel, false);
	pField = MEFieldCreateSimple(kMEFieldType_TextEntry, pDoc->pOrigObject, pDoc->pObject, parse_DynAnimGraph, "Name");
	AGEAddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 1.0, UIUnitPercentage, 21, pDoc);
	MEFieldSetChangeCallback(pField, AGESetNameCB, pDoc);
	eaPush(&pDoc->eaPropFields, pField);

	y += STANDARD_ROW_HEIGHT;

	// Scope
	pLabel = ui_LabelCreate("Scope", 0, y);
	emPanelAddChild(pPanel, pLabel, false);
	pField = MEFieldCreateSimpleDataProvided(kMEFieldType_TextEntry, pDoc->pOrigObject, pDoc->pObject, parse_DynAnimGraph, "Scope", NULL, &geaScopes, NULL);
	AGEAddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 1.0, UIUnitPercentage, 21, pDoc);
	MEFieldSetChangeCallback(pField, AGESetScopeCB, pDoc);
	eaPush(&pDoc->eaPropFields, pField);

	y += STANDARD_ROW_HEIGHT;

	// File Name
	pLabel = ui_LabelCreate("File Name", 0, y);
	emPanelAddChild(pPanel, pLabel, false);
	pDoc->pFileButton = ui_GimmeButtonCreate(x, y, "AnimGraph", pDoc->pObject->pcName, pDoc->pObject);
	x += 20.0f;
	emPanelAddChild(pPanel, pDoc->pFileButton, false);
	pLabel = ui_LabelCreate(pDoc->pObject->pcFilename, x, y);
	emPanelAddChild(pPanel, pLabel, false);
	ui_WidgetSetWidthEx(UI_WIDGET(pLabel), 1.0, UIUnitPercentage);
	ui_WidgetSetPaddingEx(UI_WIDGET(pLabel), 0, 21, 0, 0);
	pDoc->pFilenameLabel = pLabel;

	x = X_OFFSET_CONTROL;
	y += STANDARD_ROW_HEIGHT;

	// Comments
	pLabel = ui_LabelCreate("Comments", 0, y);
	emPanelAddChild(pPanel, pLabel, false);
	pField = MEFieldCreateSimple(kMEFieldType_MultiText, pDoc->pOrigObject, pDoc->pObject, parse_DynAnimGraph, "Comments");
	AGEAddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 1.0, UIUnitPercentage, 21, pDoc);
	eaPush(&pDoc->eaPropFields, pField);

	y += STANDARD_ROW_HEIGHT;

	pSeparator = ui_SeparatorCreate(UIHorizontal);
	ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
	emPanelAddChild(pPanel, pSeparator, false);

	y += SEPARATOR_HEIGHT;

	{
		F32 x1, x2;

		pLabel = ui_LabelCreate("Template", 0, y);
		emPanelAddChild(pPanel, pLabel, false);
		x1 = ui_WidgetGetWidth(UI_WIDGET(pLabel)) + 5;

		pButton = ui_ButtonCreate("Open", 0, y, AGEOpenTemplate, pDoc);
		ui_WidgetSetPositionEx(UI_WIDGET(pButton), 0, y, 0, 0, UITopRight);
		emPanelAddChild(pPanel, pButton, false);
		x2 = ui_WidgetGetWidth(UI_WIDGET(pButton)) + 5;

		pButton = ui_ButtonCreate(GET_REF(pDoc->pObject->hTemplate)?GET_REF(pDoc->pObject->hTemplate)->pcName:"NONE SELECTED", 0, y, AGEChooseTemplate, pDoc);
		ui_WidgetSetPaddingEx(UI_WIDGET(pButton), x1, x2, 0, 0);
		ui_WidgetSetDimensionsEx(UI_WIDGET(pButton), 1.0, UI_WIDGET(pButton)->height, UIUnitPercentage, UI_WIDGET(pButton)->heightUnit);
		ui_WidgetSetTooltipString(UI_WIDGET(pButton), GET_REF(pDoc->pObject->hTemplate)?GET_REF(pDoc->pObject->hTemplate)->pcName:"NONE SELECTED");
		emPanelAddChild(pPanel, pButton, false);
	}
	
	y += STANDARD_ROW_HEIGHT;

	if (GET_REF(pDoc->pObject->hTemplate))
	{
		sprintf(buf, "Template Type: %s", StaticDefineIntRevLookup(eAnimTemplateTypeEnum, GET_REF(pDoc->pObject->hTemplate)->eType));
	} else {
		sprintf(buf, "");
	}
	pLabel = ui_LabelCreate(buf, 0, y);
	emPanelAddChild(pPanel, pLabel, false);

	y += STANDARD_ROW_HEIGHT;

	if (pDoc->pObject->bPartialGraph)
		pLabel = ui_LabelCreate("Graph Mode: Partial", 0, y);	
	else
		pLabel = ui_LabelCreate("Graph Mode: Normal", 0, y);
	emPanelAddChild(pPanel, pLabel, false);

	y += STANDARD_ROW_HEIGHT;

	pSeparator = ui_SeparatorCreate(UIHorizontal);
	ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pSeparator), 1.0, 5, UIUnitPercentage, UIUnitFixed);
	emPanelAddChild(pPanel, pSeparator, false);

	y += SEPARATOR_HEIGHT;

	y += 30;

	pSeparator = ui_SeparatorCreate(UIHorizontal);
	ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pSeparator), 1.0, 5, UIUnitPercentage, UIUnitFixed);
	emPanelAddChild(pPanel, pSeparator, false);

	y += SEPARATOR_HEIGHT;

	y = AnimEditor_ReflowGraphProperties(
		(pDoc->pOrigObject ? pDoc->pOrigObject : NULL), pDoc->pObject,
		pDoc, pPanel,
		&pDoc->eaPropFields, &pDoc->eaInactivePropPanelWidgets,
		AGEFieldPreChangeCB, AGEFieldChangedCB, AGEInheritFieldChangedCB,
		AGEAddDocOnEnterFX, AGEAddDocOnEnterFXMessage,
		AGEAddDocOnExitFX, AGEAddDocOnExitFXMessage,
		AGEChooseFxEvent, AGEOpenFxEvent, AGERemoveFxEvent,
		AGEAddDocSuppress, AGERemoveSuppress,
		AGEAddDocStance, AGEChangeStance, AGERemoveStance,
		&ageBadDataButton,
		y
		);

	pSeparator = ui_SeparatorCreate(UIHorizontal);
	ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pSeparator), 1.0, 5, UIUnitPercentage, UIUnitFixed);
	emPanelAddChild(pPanel, pSeparator, false);

	y += SEPARATOR_HEIGHT + 30;

	emPanelSetHeight(pPanel, y);

	return pPanel;
}

#define AddMultiEditField(label, metype, ptientry) x = 20.0f; \
		pLabel = ui_LabelCreate(label, x, y); \
		emPanelAddChild(pPanel, pLabel, false); \
		x += ui_WidgetGetWidth(UI_WIDGET(pLabel)) + 10.0f; \
		pField = MEFieldCreateSimple(metype, pOrigNode, pEditNode, parse_DynAnimGraphNode, ptientry); \
		AGEAddFieldToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y, 0, 1.0, UIUnitPercentage, 21, pDoc); \
		eaPush(&pDoc->eaNodeFields, pField); \
		y += STANDARD_ROW_HEIGHT;

static F32 AGENodeEditReflow(AnimGraphDoc* pDoc, EMPanel* pPanel, F32 y)
{
	DynAnimGraphNode* pEditNode = pDoc->pObject->eaNodes[pDoc->iFocusIndex];
	DynAnimGraphNode* pOrigNode = pDoc->pOrigObject?
		(pDoc->iFocusIndex < eaSize(&pDoc->pOrigObject->eaNodes)?pDoc->pOrigObject->eaNodes[pDoc->iFocusIndex]:NULL)
		:NULL;
	eAnimTemplateNodeType editNodeType = pEditNode->pTemplateNode->eType;

	UILabel* pLabel;
	F32 x = 0;
	
	{
		char cTemp[256];
		sprintf(cTemp, "Node: %s", pEditNode->pcName);
		pLabel= ui_LabelCreate(cTemp, x, y);
		ui_LabelSetFont(pLabel, gBoldPanelFont);
		emPanelAddChild(pPanel, pLabel, false);

		y += STANDARD_ROW_HEIGHT;

		switch(editNodeType)
		{
			xcase eAnimTemplateNodeType_Start      : {pLabel = ui_LabelCreate("Type: Start",      0, y);}
			xcase eAnimTemplateNodeType_End        : {pLabel = ui_LabelCreate("Type: End",        0, y);}
			xcase eAnimTemplateNodeType_Normal     : {pLabel = ui_LabelCreate("Type: Normal",     0, y);}
			xcase eAnimTemplateNodeType_Randomizer : {pLabel = ui_LabelCreate("Type: Randomizer", 0, y);}
			break; default                         : {pLabel = ui_LabelCreate("Type: Unknown",    0, y);}
		}
		emPanelAddChild(pPanel, pLabel, false);

		y += STANDARD_ROW_HEIGHT;
	}

	if (editNodeType == eAnimTemplateNodeType_Normal)
	{
		INSERT_SEPARATOR(y);

		y = AnimEditor_ReflowNormalNode(
			pDoc->pObject,
			pDoc, pDoc->iFocusIndex, pPanel,
			&pDoc->eaNodeFields, &pDoc->eaMoveFields, &pDoc->eaMoveFxFields, &pDoc->eaInactiveNodesPanelWidgets,
			pOrigNode, pEditNode,
			AGEFieldPreChangeCB, AGEFieldChangedCB, AGEInheritFieldChangedCB,
			AGEAddFX, AGEAddFXMessage, AGEChooseFxEvent, AGEOpenFxEvent, AGERemoveFxEvent,
			AGEAddImpact, AGERemoveImpact,
			AGEAddMove, AGEChooseMove, AGEMoveChangeChance, AGEOpenMove, AGERemoveMove,
			&ageBadDataButton,
			true,
			y
			);
	}
	else if (editNodeType == eAnimTemplateNodeType_Randomizer)
	{
		INSERT_SEPARATOR(y);

		y = AnimEditor_ReflowRandomizerNode(
			pDoc->pObject,
			pDoc, pDoc->iFocusIndex, pPanel,
			&pDoc->eaNodeFields, &pDoc->eaPathFields, &pDoc->eaInactiveNodesPanelWidgets,
			pOrigNode, pEditNode,
			AGEFieldPreChangeCB, AGEFieldChangedCB, AGEInheritFieldChangedCB,
			AGEAddPath, AGEPathChangeChance, AGERemovePath,
			&ageBadDataButton,
			y
			);
	}
	else if (editNodeType == eAnimTemplateNodeType_End)
	{
		INSERT_SEPARATOR(y);

		y = AnimEditor_ReflowExitNode(
			pDoc->pObject,
			pDoc, pDoc->iFocusIndex, pPanel,
			&pDoc->eaNodeFields, &pDoc->eaPathFields, &pDoc->eaInactiveNodesPanelWidgets,
			pOrigNode, pEditNode,
			AGEFieldPreChangeCB, AGEFieldChangedCB, AGEInheritFieldChangedCB,
			AGEChooseNodePostIdle, AGEOpenNodePostIdle, AGERemoveNodePostIdle,
			&ageBadDataButton,
			y
			);
	}

	return y;
}


static EMPanel* AGEInitNodesPanel(AnimGraphDoc* pDoc)
{
	EMPanel* pPanel;
	UISeparator *pSeparator;
	F32 y = 0;

	// Create the panel
	pPanel = emPanelCreate("Graph", "Nodes", 0);

	// Add nodes button

	pSeparator = ui_SeparatorCreate(UIHorizontal);
	ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pSeparator), 1.0, 5, UIUnitPercentage, UIUnitFixed);
	emPanelAddChild(pPanel, pSeparator, false);

	y += SEPARATOR_HEIGHT;

	if (pDoc->iFocusIndex >= 0 && pDoc->iFocusIndex < eaSize(&pDoc->pObject->eaNodes))
	{
		y = AGENodeEditReflow(pDoc, pPanel, y);
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

static void AGERefreshChartsCallback(UIButton *pButton, AnimGraphDoc *pDoc)
{
	AGEAnimGraphChanged(pDoc, false, false, false, false, true);
}

static void AGEOpenChartCallback(UIButton* pButton, AnimGraphDoc *pDoc)
{
	const char* buttonText = ui_WidgetGetText( UI_WIDGET( pButton ));
	if (buttonText) emOpenFileEx(buttonText, "AnimChart");
}

static EMPanel* AGEInitChartsPanel(AnimGraphDoc* pDoc)
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
	pButton = ui_ButtonCreate( "Refresh", 0, y, AGERefreshChartsCallback, pDoc );
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

	pLabel = ui_LabelCreate("Charts that Reference this Graph:", 0, y);
	emPanelAddChild(pPanel, pLabel, false);

	y += STANDARD_ROW_HEIGHT;

	pSeparator = ui_SeparatorCreate(UIHorizontal);
	ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);
	emPanelAddChild(pPanel, pSeparator, false);

	y += SEPARATOR_HEIGHT;

	{
		DictionaryEArrayStruct *pAnimChartArray = resDictGetEArrayStruct(hAnimChartDictLoadTime);
		FOR_EACH_IN_EARRAY(pAnimChartArray->ppReferents, DynAnimChartLoadTime, pChart)
		{
			int count = 0;

			FOR_EACH_IN_EARRAY(pChart->eaGraphRefs, DynAnimChartGraphRefLoadTime, pGraphRef)
			{
				if (GET_REF(pGraphRef->hGraph) && REF_HANDLE_GET_STRING(pGraphRef->hGraph) == pDoc->pObject->pcName)
					count++;

				FOR_EACH_IN_EARRAY(pGraphRef->eaGraphChances, DynAnimGraphChanceRef, pGraphChanceRef)
					if (GET_REF(pGraphChanceRef->hGraph) && REF_HANDLE_GET_STRING(pGraphChanceRef->hGraph) == pDoc->pObject->pcName)
						count++;
				FOR_EACH_END;
			}
			FOR_EACH_END;
			
			if (count > 0) {
				char countBuff[MAX_PATH];
				itoa(count, countBuff, 10);
				strcat(countBuff, " x ");

				pLabel = ui_LabelCreate(countBuff, 0, y);
				emPanelAddChild(pPanel, pLabel, false);

				pButton = ui_ButtonCreate(pChart->pcName,30, y, AGEOpenChartCallback, pDoc);
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

static EMPanel* AGEInitSearchPanel(AnimGraphDoc* pDoc)
{
	EMPanel* pPanel;
	F32 y = 0.0;

	// Create the panel
	pPanel = emPanelCreate("Search", "Search", 0);

	y += AnimEditor_Search(	pDoc,
							pPanel,
							AnimEditor_SearchText,
							AGESearchTextChanged,
							AGERefreshSearchPanel
							);

	emPanelSetHeight(pPanel, y);

	return pPanel;
}

static void AGEInitDisplay(EMEditor* pEditor, AnimGraphDoc* pDoc)
{
	// Create the panel (ignore field change callbacks during init)
	// JE: note: this gets run again immediately in AGEUpdateDisplay, but without bIgnoreFilenameChanges, not sure what's up with that
	pDoc->bIgnoreFieldChanges = true;
	pDoc->bIgnoreFilenameChanges = true;
	pDoc->pPropsPanel = AGEInitPropertiesPanel(pDoc);
	pDoc->pNodesPanel = AGEInitNodesPanel(pDoc);
	pDoc->pChartsPanel = AGEInitChartsPanel(pDoc);
	pDoc->pSearchPanel = AGEInitSearchPanel(pDoc);
	pDoc->bIgnoreFieldChanges = false;
	pDoc->bIgnoreFilenameChanges = false;

	AGEInitViewPane(pDoc);
	AGEInitTimelinePane(pDoc);

	//pDoc->emDoc.primary_ui_window = pDoc->pMainWindow;
	//eaPush(&pDoc->emDoc.ui_windows, pDoc->pMainWindow);
	eaPush(&pDoc->emDoc.em_panels, pDoc->pPropsPanel);
	eaPush(&pDoc->emDoc.em_panels, pDoc->pNodesPanel);
	eaPush(&pDoc->emDoc.em_panels, pDoc->pChartsPanel);
	eaPush(&pDoc->emDoc.em_panels, pDoc->pSearchPanel);

	// Update the rest of the UI
	AGEUpdateDisplay(pDoc, true, true, true, true, true);
}


static AnimEditor_CostumePickerData* AGEGetCostumePickerData(void)
{
	AnimGraphDoc* pDoc = (AnimGraphDoc*)emGetActiveEditorDoc();
	assert(pDoc);
	pDoc->costumeData.getCostumePickerData = AGEGetCostumePickerData;
	pDoc->costumeData.postCostumeChange = AGEPostCostumeChangeCB;
	pDoc->costumeData.getPaneBox = AGEGetPaneBox;
	return &pDoc->costumeData;
}

static void AGEPostCostumeChangeCB( void ) 
{
}

static void AGEGetPaneBox(CBox* pBox)
{
	AnimGraphDoc* pDoc = (AnimGraphDoc*)emGetActiveEditorDoc();
	assert(pDoc);
	ui_WidgetGetCBox(UI_WIDGET(pDoc->pViewPane), pBox);
}

#define BUTTON_SPACING 3.0f

#define ADD_BUTTON( text, callback, callbackdata ) \
		pButton = ui_ButtonCreate(text, fX, 0, callback, callbackdata); \
		pButton->widget.widthUnit = UIUnitFitContents; \
		emToolbarAddChild(pToolbar, pButton, false); \
		fX += ui_WidgetGetWidth(UI_WIDGET(pButton)) + BUTTON_SPACING; \

static void AGEInitToolbarsAndMenus(EMEditor* pEditor)
{
	EMToolbar* pToolbar;
	UIButton* pButton;
	F32 fX;

	// Menu Bar
	pToolbar = emToolbarCreateFileToolbar(EM_FILE_TOOLBAR_NEW|EM_FILE_TOOLBAR_OPEN|EM_FILE_TOOLBAR_SAVE);
	fX = emToolbarGetPaneWidget(pToolbar)->width;
	ADD_BUTTON("Duplicate", AGEDuplicateDoc, NULL);
	emToolbarSetWidth(pToolbar, fX);
	eaPush(&pEditor->toolbars, pToolbar);
	
	eaPush(&pEditor->toolbars, emToolbarCreateWindowToolbar());

	// Custom Buttons Bar
	{
		fX = 0.0f;
		pToolbar = emToolbarCreate(50);
		ADD_BUTTON("Pick Costume", AnimEditor_CostumePicker, AGEGetCostumePickerData);
		//ADD_BUTTON("Last Costume", AnimEditor_LastCostume, AGEGetCostumePickerData);
		ADD_BUTTON("Fit to Pane", AnimEditor_UIFitCameraToPane, AGEGetCostumePickerData);
		ADD_BUTTON("Fit to Screen", AnimEditor_UICenterCamera, AGEGetCostumePickerData);

		ageDrawGridCheckButton = ui_CheckButtonCreate(fX, 0, "Draw Grid", true);
		ui_CheckButtonSetToggledCallback(ageDrawGridCheckButton, AGEDrawGridCheckboxToggle, NULL);
		emToolbarAddChild(pToolbar, ageDrawGridCheckButton, true);
		fX += ui_WidgetGetWidth(UI_WIDGET(ageDrawGridCheckButton)) + 5.0;

		emToolbarSetWidth(pToolbar, fX);
		eaPush(&pEditor->toolbars, pToolbar);
	}
	{
		fX = 0.0f;
		pToolbar = emToolbarCreate(50);
		ADD_BUTTON("Add Test FX", AnimEditor_UIAddTestFX, AGEGetCostumePickerData);
		ADD_BUTTON("Clear Test FX", AnimEditor_UIClearTestFX, AGEGetCostumePickerData);
		emToolbarSetWidth(pToolbar, fX);
		eaPush(&pEditor->toolbars, pToolbar);	
	}

	// File menu
	emMenuItemCreate(pEditor, "ate_reverttemplate", "Revert", NULL, NULL, "AGE_RevertGraph");
	emMenuRegister(pEditor, emMenuCreate(pEditor, "File", "ate_reverttemplate", NULL));
}


//---------------------------------------------------------------------------------------------------
// Public Interface
//---------------------------------------------------------------------------------------------------


void AGEInitData(EMEditor* pEditor)
{
	if (pEditor && !gInitializedEditor)
	{
		gBoldExpanderSkin = ui_SkinCreate(NULL);
		SET_HANDLE_FROM_STRING(g_ui_FontDict, "Default_Bold", gBoldExpanderSkin->hNormal);

		// skins
		{
			int iColor;
			for( iColor = 0; iColor < eAGECount; ++iColor )
			{
				ui_SkinCopy( &ageNodeSkin[ iColor ], NULL );
				ui_SkinSetBackground( &ageNodeSkin[ iColor ], ageNodeTypeColor[ iColor ]);
				ui_SkinSetBorderEx( &ageNodeSkin[ iColor ], ARGBToColor(0xFFFFFF00), ARGBToColor(0xFFAAAAAA));
			}
		}

		gActiveMoveFont = ui_StyleFontCreate("AGEActiveMoveFont", &g_font_Sans, ColorRed, false, false, 1);
		ui_StyleFontRegister(gActiveMoveFont);

		gBoldPanelFont = ui_StyleFontCreate("AGEBoldPanelFont", &g_font_Sans, ColorWhite, true, false, 1);
		ui_StyleFontRegister(gBoldPanelFont);


		ui_SkinCopy( &ageViewPaneSkin, NULL );
		ageViewPaneSkin.background[ 0 ].a /= 2;
		ageViewPaneSkin.background[ 1 ].a /= 2;

		ui_SkinCopy( &ageBadDataBackground, &ageViewPaneSkin);
		ui_SkinCopy( &ageBadDataButton, NULL );

		gBackgroundScrollSprite = ui_SpriteCreate(0.0f, 0.0f, 100.0f, 100.0f, "smooth_gradient");
		setColorFromRGBA(&gBackgroundScrollSprite->tint, 0x000000FF);

		AGEInitToolbarsAndMenus(pEditor);

		// Have Editor Manager handle a lot of change tracking
		emAutoHandleDictionaryStateChange(pEditor, "AnimGraph", true, NULL, NULL, NULL, NULL, NULL);

		resGetUniqueScopes(hAnimGraphDict, &geaScopes);



		gInitializedEditor = true;
	}

	if (!gInitializedEditorData)
	{
		// Make sure lists refresh if dictionary changes
		resDictRegisterEventCallback(hAnimGraphDict, AGEContentDictChanged, NULL);

		gInitializedEditorData = true;
	}
}

static void AGEGraphPostOpenFixup(DynAnimGraph* pGraph)
{
}


static void AGEGraphPreSaveFixup(DynAnimGraph* pGraph)
{
	AnimGraphDoc* pDoc = (AnimGraphDoc*)emGetActiveEditorDoc();
	if (pDoc)
		pDoc->bForceGraphChange = true;

	FOR_EACH_IN_EARRAY(pGraph->eaNodes, DynAnimGraphNode, pGraphNode)
	{
		dynAnimGraphNodeNormalizeChances(pGraphNode);
	}
	FOR_EACH_END;
}

static AnimGraphDoc* AGEInitDoc(DynAnimGraph* pGraph, bool bCreated, bool bEmbedded)
{
	AnimGraphDoc* pDoc;
	char nameBuf[260];

	// Initialize the structure
	pDoc = (AnimGraphDoc*)calloc(1,sizeof(AnimGraphDoc));
	pDoc->costumeData.eaAddedFx = NULL;
	pDoc->costumeData.bMoveMultiEditor = 0;

	// Fill in the def data
	if (bCreated && pGraph)
	{
		pDoc->pObject = StructClone(parse_DynAnimGraph, pGraph);
		assert(pDoc->pObject);
		sprintf(pDoc->emDoc.doc_name,"%s_Dup%d",pGraph->pcName,++ageNewNameCount);
		pDoc->pObject->pcName = StructAllocString(pDoc->emDoc.doc_name);
		sprintf(nameBuf, "dyn/animgraph/%s/%s.agraph", pDoc->pObject->pcScope, pDoc->pObject->pcName);
		pDoc->pObject->pcFilename = allocAddFilename(nameBuf);
	}
	else if (bCreated)
	{
		pDoc->pObject = StructCreate(parse_DynAnimGraph);
		assert(pDoc->pObject);
		dynAnimGraphInit(pDoc->pObject);
		emMakeUniqueDocName(&pDoc->emDoc, DEFAULT_DOC_NAME, "DynAnimGraph", "DynAnimGraph");
		pDoc->pObject->pcName = StructAllocString(pDoc->emDoc.doc_name);
		sprintf(nameBuf, "dyn/animgraph/%s.agraph", pDoc->pObject->pcName);
		pDoc->pObject->pcFilename = allocAddFilename(nameBuf);
		AGEGraphPostOpenFixup(pDoc->pObject);
	}
	else
	{
		pDoc->pObject = StructClone(parse_DynAnimGraph, pGraph);
		assert(pDoc->pObject);
		AGEGraphPostOpenFixup(pDoc->pObject);
		pDoc->pOrigObject = StructClone(parse_DynAnimGraph, pDoc->pObject);
		emDocAssocFile(&pDoc->emDoc, pDoc->pObject->pcFilename);
	}

	pDoc->iFocusIndex = -1;
	pDoc->iOldFocusIndex = -2;
	pDoc->playbackInfo.bPaused = false;
	pDoc->playbackInfo.fPlaybackRate = 1.0f;


	// Set up the undo stack
	pDoc->emDoc.edit_undo_stack = EditUndoStackCreate();
	EditUndoSetContext(pDoc->emDoc.edit_undo_stack, pDoc);
	pDoc->pNextUndoObject = StructClone(parse_DynAnimGraph, pDoc->pObject);

	if (bCreated && !pGraph)
		AGEChooseTemplate(NULL, pDoc);

	pDoc->costumeData.pGraphics = costumeView_CreateGraphics();

	return pDoc;
}


AnimGraphDoc* AGEOpenAnimGraph(EMEditor* pEditor, char* pcName, DynAnimGraph *pGraphIn)
{
	AnimGraphDoc* pDoc = NULL;
	DynAnimGraph* pGraph = NULL;
	bool bCreated = false;

	if (pGraphIn)
	{
		pGraph = pGraphIn;
		bCreated = true;
	}
	else if (pcName && resIsEditingVersionAvailable(hAnimGraphDict, pcName))
	{
		// Simply open the object since it is in the dictionary
		pGraph = RefSystem_ReferentFromString(hAnimGraphDict, pcName);
	}
	else if (pcName)
	{
		// Wait for object to show up so we can open it
		resSetDictionaryEditMode(hAnimGraphDict, true);
		emSetResourceState(pEditor, pcName, EMRES_STATE_OPENING);
		resRequestOpenResource(hAnimGraphDict, pcName);
	}
	else
	{
		// Create a new object since it is not in the dictionary
		bCreated = true;
	}

	if (pGraph || bCreated)
	{
		pDoc = AGEInitDoc(pGraph, bCreated, false);
		AGEInitDisplay(pEditor, pDoc);
		resFixFilename(hAnimGraphDict, pDoc->pObject->pcName, pDoc->pObject);
	}

	return pDoc;
}

static void AGEDeleteOldDirectoryIfEmpty(AnimGraphDoc *pDoc)
{
	char dir[MAX_PATH], out_dir[MAX_PATH];
	char cmd[MAX_PATH];

	sprintf(dir, "/dyn/Animgraph/%s", NULL_TO_EMPTY(pDoc->pOrigObject->pcScope));
	fileLocateWrite(dir, out_dir);
	if (dirExists(out_dir))
	{
		backSlashes(out_dir);
		sprintf(cmd, "rd %s", out_dir);
		system(cmd);
	}
}

void AGERevertAnimGraph(AnimGraphDoc* pDoc)
{
	DynAnimGraph* pGraph;

	if (!pDoc->emDoc.orig_doc_name[0])
	{
		// Cannot revert if no original
		return;
	}

	//if we're reverting due to save, remove the old directory if it's empty post scope change
	if (pDoc->pOrigObject && pDoc->pObject->pcScope != pDoc->pOrigObject->pcScope)
		AGEDeleteOldDirectoryIfEmpty(pDoc);

	pGraph = RefSystem_ReferentFromString(hAnimGraphDict, pDoc->emDoc.orig_doc_name);
	if (pGraph)
	{
		// Revert the def
		StructDestroy(parse_DynAnimGraph, pDoc->pObject);
		StructDestroy(parse_DynAnimGraph, pDoc->pOrigObject);
		pDoc->pObject = StructClone(parse_DynAnimGraph, pGraph);
		AGEGraphPostOpenFixup(pDoc->pObject);
		pDoc->pOrigObject = StructClone(parse_DynAnimGraph, pDoc->pObject);

		// Clear the undo stack on revert
		EditUndoStackClear(pDoc->emDoc.edit_undo_stack);
		StructDestroy(parse_DynAnimGraph, pDoc->pNextUndoObject);
		pDoc->pNextUndoObject = StructClone(parse_DynAnimGraph, pDoc->pObject);

		// Refresh the UI
		pDoc->bIgnoreFieldChanges = true;
		pDoc->bIgnoreFilenameChanges = true;
		AGEUpdateDisplay(pDoc, false, true, true, true, true);
		pDoc->bIgnoreFieldChanges = false;
		pDoc->bIgnoreFilenameChanges = false;
		pDoc->bForceGraphChange = true;
	}

}


void AGECloseAnimGraph(AnimGraphDoc* pDoc)
{
	// Free doc fields
	eaDestroyEx(&pDoc->eaDocFields,  MEFieldDestroy);
	eaDestroyEx(&pDoc->eaPropFields, MEFieldDestroy);
	eaDestroyEx(&pDoc->eaNodeFields, MEFieldDestroy);
	eaDestroyEx(&pDoc->eaChartFields, MEFieldDestroy);
	eaDestroyEx(&pDoc->eaMoveFields, AnimEditor_MoveMEFieldDestroy);
	eaDestroyEx(&pDoc->eaMoveFxFields, AnimEditor_MoveFxMEFieldDestroy);
	eaDestroyEx(&pDoc->eaPathFields, AnimEditor_PathMEFieldDestroy);

	costumeView_FreeGraphics(pDoc->costumeData.pGraphics);
	REF_HANDLE_REMOVE(pDoc->costumeData.hCostume);

	// Free the groups
	//FreeInteractionPropertiesGroup(pDoc->pPropsGroup);

	// Free the objects
	StructDestroy(parse_DynAnimGraph, pDoc->pObject);
	if (pDoc->pOrigObject)
	{
		StructDestroy(parse_DynAnimGraph, pDoc->pOrigObject);
	}
	StructDestroy(parse_DynAnimGraph, pDoc->pNextUndoObject);

	// Close the window
	//ui_WindowHide(pDoc->pMainWindow);
	//ui_WidgetQueueFree(UI_WIDGET(pDoc->pMainWindow));
	emPanelFree(pDoc->pPropsPanel);
	emPanelFree(pDoc->pNodesPanel);
	emPanelFree(pDoc->pChartsPanel);
	emPanelFree(pDoc->pSearchPanel);
	pDoc->pPropsPanel  = NULL;
	pDoc->pNodesPanel  = NULL;
	pDoc->pChartsPanel = NULL;
	pDoc->pSearchPanel = NULL;
}

EMTaskStatus AGESaveAnimGraph(AnimGraphDoc* pDoc, bool bSaveAsNew)
{
	EMTaskStatus status;
	const char* pcName;
	DynAnimGraph* pGraphCopy;

	// Deal with state changes
	pcName = pDoc->pObject->pcName;
	if (emHandleSaveResourceState(pDoc->emDoc.editor, pcName, &status))
	{
		return status;
	}

	if (strnicmp(pcName, DEFAULT_DOC_NAME, strlen(DEFAULT_DOC_NAME))==0)
	{
		Errorf("Must choose a name besides %s", DEFAULT_DOC_NAME);
		return EM_TASK_FAILED;
	}


	// Do cleanup before validation
	pGraphCopy = StructClone(parse_DynAnimGraph, pDoc->pObject);
	AGEGraphPreSaveFixup(pGraphCopy);

	// Perform validation
	if (!dynAnimGraphVerify(pGraphCopy, true))
	{
		StructDestroy(parse_DynAnimGraph, pGraphCopy);
		return EM_TASK_FAILED;
	}

	// Do the save (which will free the copy)
	status = emSmartSaveDoc(&pDoc->emDoc, pGraphCopy, pDoc->pOrigObject, bSaveAsNew);
	emDocRemoveAllFiles(&pDoc->emDoc, false);
	emDocAssocFile(&pDoc->emDoc, pDoc->pObject->pcFilename);
	
	return status;
}

void AGEDuplicateDoc(UIButton* button, UserData uData)
{
	AnimGraphDoc *pDoc = (AnimGraphDoc*)emGetActiveEditorDoc();
	emNewDoc("AnimGraph", pDoc->pObject);
}

// +------------------+
// |Sound system calls|
// +------------------+

void AGESndPlayerMatCB(Mat4 mat)
{
	identityMat4(mat);
}

void AGESndPlayerVelCB(Vec3 vel)
{
	zeroVec3(vel);
}

void AGEGotFocus(AnimGraphDoc *pDoc)
{
	sndSetPlayerMatCallback(AGESndPlayerMatCB);
	sndSetVelCallback(AGESndPlayerVelCB);
	dynAnimGraphSetEditorMode(true);
	AGERefreshSearchPanel(NULL, pDoc);
}

void AGELostFocus(void)
{
	sndSetPlayerMatCallback(gclSndGetPlayerMatCB);
	sndSetVelCallback(gclSndVelCB);
	dynAnimGraphSetEditorMode(false);
}
