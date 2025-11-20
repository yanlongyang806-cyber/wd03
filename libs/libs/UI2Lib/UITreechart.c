#include"UITreechart.h"

#include"CBox.h"
#include"EArray.h"
#include"GfxClipper.h"
#include"GfxPrimitive.h"
#include"GfxSprite.h"
#include"GfxTexAtlas.h"
#include"UIButton.h"
#include"UIDnD.h"
#include"UIInternal.h"
#include"UITextureAssembly.h"
#include"input.h"
#include"inputMouse.h"
#include"mathutil.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

static void ui_TreechartDrawDiagrams( SA_PARAM_NN_VALID UITreechart* chart, UITreechartNode** nodes, CBox* fullWidthClipBox, CBox* nonFullWidthClipBox );
static void ui_TreechartDrawDiagramsForNodeChildren( UITreechart* chart, UITreechartNode* node, CBox* fullWidthClipBox, CBox* nonFullWidthClipBox );
static void ui_TreechartLayoutMeasure( int* outWidth, int* outHeight, UITreechart* chart, UITreechartNode** nodes );
static void ui_TreechartLayoutPlace( UITreechart* chart, UITreechartNode** nodes, int x, int y, int w, int h );
static void ui_TreechartUpdateDrawState( UITreechart* chart, UITreechartNode** nodes, bool parentIsDragging );
static UITreechartNode* ui_TreechartFindNodeByWidget( UITreechart* chart, UIWidget* widget );
static void ui_TreechartFindNodeEAByWidget( UITreechartNode**** ppEA, int* outIndex, UITreechart* chart, UIWidget* widget );
static void ui_TreechartNodeCalcChildArea( UITreechart* chart, UITreechartNode* node, int* out_x, int* out_y, int* out_w, int* out_h );

/// Widget CBs
static void ui_TreechartWidgetDrawPlaceholder( UIWidget* widget, UI_PARENT_ARGS );
static void ui_TreechartWidgetDrawPlaceholderChild( UIWidget* widget, UI_PARENT_ARGS );
static void ui_TreechartWidgetDummyTick( UIWidget* widget, UI_PARENT_ARGS );
static void ui_TreechartWidgetPreDrag( UIWidget* dragWidget, UIWidget* rawChart );
static void ui_TreechartWidgetDragBegin( UIWidget* dragWidget, UIWidget* rawChart );
static void ui_TreechartWidgetDragEnd( UIWidget* dragWidget, UIWidget* ignoredDest, UIDnDPayload* ignored, UIWidget* rawChart );

/// Arrow CBs
static void ui_TreechartArrowDrag( SA_PARAM_NN_VALID UITreechart* chart, SA_PARAM_OP_VALID UITreechartNode* beforeNode, SA_PARAM_OP_VALID UITreechartNode* afterNode );

/// Positioning logic and drawing
static UITreechartNode* ui_TreechartNextNodeNonArrow( UITreechartNode** nodes, int pos, int dir );
static void ui_TreechartNodeGetBox( CBox* outBox, UITreechart* chart, UITreechartNode* node );
static void ui_TreechartNodeGetTopAnchor( Vec2 outPos, UITreechart* chart, UITreechartNode* node );
static void ui_TreechartNodeGetBottomAnchor( Vec2 outPos, UITreechart* chart, UITreechartNode* node );
static void ui_TreechartNodeDrawHighlight( UITreechart* chart, UITreechartNode* node, F32 z, bool isHighlight, bool isSelection );
static void ui_TreechartNodeDrawColumnHighlights( UITreechart* chart, UITreechartNode* node, F32 z );
static bool ui_TreechartNodeCollides( UITreechart* chart, UITreechartNode* node, float x, float y );
static int ui_TreechartNodeBottomHeight( UITreechart* chart, UITreechartNode* node );

static void ui_TreechartArrowDrawToMouse( UISkin* skin, Vec2 start, float z, bool isArrowDraggable, float scale );
static bool ui_TreechartArrowCollides( Vec2 start, Vec2 end, float scale, float x, float y );
static bool ui_TreeChartInsertionPlusCollides(UISkin* skin, Vec2 start, Vec2 end, float scale, float x, float y);
static void ui_TreechartCheckDragDiagrams( SA_PARAM_NN_VALID UITreechart* chart, UITreechartNode*** peaNodes );
static bool ui_TreeChartCheckInsertionPlusClicks(UITreechart* chart, UITreechartNode*** peaNodes);

/// Style borders
static void ui_TreechartBGStyleFillDrawingDescription( SA_PARAM_NN_VALID UITreechart* chart, UIDrawingDescription* desc );
static void ui_TreechartPlaceholderFillDrawingDescription( SA_PARAM_NN_VALID UITreechart* chart, UIDrawingDescription* desc );
static void ui_TreechartGroupContentBGFillDrawingDescription( SA_PARAM_NN_VALID UITreechart* chart, UIDrawingDescription* desc );
void ui_TreechartGroupBottomFillDrawingDescription( SA_PARAM_NN_VALID UITreechart* chart, UIDrawingDescription* desc );
static void ui_TreechartFullWidthContentBGFillDrawingDescription( SA_PARAM_NN_VALID UITreechart* chart, UIDrawingDescription* desc );
static void ui_TreechartNodeHighlightFillDrawingDescription( SA_PARAM_NN_VALID UITreechart* chart, UIDrawingDescription* desc );
static void ui_TreechartArrowHighlightFillDrawingDescription( SA_PARAM_NN_VALID UITreechart* chart, UIDrawingDescription* desc );
static void ui_TreechartArrowHighlightActiveFillDrawingDescription( SA_PARAM_NN_VALID UITreechart* chart, UIDrawingDescription* desc );
static void ui_TreechartArrowFillDrawingDescription( SA_PARAM_NN_VALID UITreechart* chart, UIDrawingDescription* desc, bool isArrowDraggable, bool isArrowheadVisible, bool isAlternate );
static void ui_TreechartArrowFillDrawingDescriptionForSkin( SA_PARAM_NN_VALID UISkin* skin, UIDrawingDescription* desc, bool isArrowDraggable, bool isArrowheadVisible, bool isAlternate );
static void ui_TreechartBranchArrowFillDrawingDescription( SA_PARAM_NN_VALID UITreechart* chart, UIDrawingDescription* desc );

int ui_TreechartNodeDefaultBottomHeight = 10;
AUTO_CMD_INT( ui_TreechartNodeDefaultBottomHeight, ui_TreechartNodeDefaultBottomHeight );
int ui_TreechartArrowAreaWidth = 140;
AUTO_CMD_INT( ui_TreechartArrowAreaWidth, ui_TreechartArrowAreaWidth );
int ui_TreechartMaxHLDist = 200;
AUTO_CMD_INT( ui_TreechartMaxHLDist, ui_TreechartMaxHLDist );
int ui_TreechartMinHLValue = 20;
AUTO_CMD_INT( ui_TreechartMinHLValue, ui_TreechartMinHLValue );
int ui_TreechartFullWidthContainerWidth = 60;
AUTO_CMD_INT( ui_TreechartFullWidthContainerWidth, ui_TreechartFullWidthContainerWidth );
int ui_TreechartBetweenColumnWidth = 20;
AUTO_CMD_INT( ui_TreechartBetweenColumnWidth, ui_TreechartBetweenColumnWidth );

//insertion plus ui layout settings
int ui_insertion_plus_x = 0;
AUTO_CMD_INT( ui_insertion_plus_x, uiplusx );
int ui_insertion_plus_y = 10;
AUTO_CMD_INT( ui_insertion_plus_y, uiplusy );

typedef struct UIWidgetClass
{
	UIWidget widget;
} UIWidgetClass;

static bool treechartDrawFoundHighlight = false;

UITreechart* ui_TreechartCreate( UserData cbData, UITreechartDragNodeNodeFunc dragNNF,
								 UITreechartDragNodeArrowFunc dragNAF, UITreechartDragArrowNodeFunc dragANF,
								 UITreechartNodeAnimateFunc nodeAnimateF, UITreechartDragNodeTrashFunc nodeTrashF,
								 UITreechartDrawDragNodeNodeFunc drawDragNNF, UITreechartClickInsertionPlusFunc clickInsertF,
								 UITreechartDragNodeNodeColumnFunc dragNNColumnF )
{
	UITreechart* accum = calloc( 1, sizeof( *accum ));

	ui_ScrollAreaInitialize( UI_SCROLLAREA( accum ), 0, 0, 0, 0, 0, 0, 1, 1 );
	UI_SCROLLAREA( accum )->autosize = true;
	UI_SCROLLAREA( accum )->beforeDragF = ui_TreechartCheckDragDiagrams;
	UI_SCROLLAREA( accum )->beforeDragData = &accum->treeNodes;

	UI_WIDGET( accum )->freeF = ui_TreechartFree;
	UI_WIDGET( accum )->tickF = ui_TreechartTick;
	UI_WIDGET( accum )->drawF = ui_TreechartDraw;

	accum->cbData = cbData;
	accum->dragNNF = dragNNF;
	accum->dragNAF = dragNAF;
	accum->dragANF = dragANF;
	accum->nodeAnimateF = nodeAnimateF;
	accum->nodeTrashF = nodeTrashF;
	accum->drawDragNNF = drawDragNNF;
	accum->clickInsertF = clickInsertF;
	accum->dragNNColumnF = dragNNColumnF;

	accum->trashButton = ui_ButtonCreateImageOnly( "white", 10, 14, NULL, NULL );
	ui_WidgetSetDimensions( UI_WIDGET( accum->trashButton ), 80, 80 );

	ui_WidgetSetAcceptCallback( UI_WIDGET( accum ), ui_TreechartWidgetDragEnd, accum );

	return accum;
}

static void ui_TreechartClearNodeWidget( UITreechartNode* node, bool removeFromGroup )
{
	if( node->widget ) {
		UIWidget* widget = node->widget;

		assert( !widget->preDragF || widget->preDragF == ui_TreechartWidgetPreDrag );
		ui_WidgetSetPreDragCallback( widget, NULL, NULL );
		ui_WidgetSetDragCallback( widget, NULL, NULL );
		ui_WidgetSetAcceptCallback( widget, NULL, NULL  );
		if( node->widgetBackupDrawF ) {
			widget->drawF = node->widgetBackupDrawF;
			node->widgetBackupDrawF = NULL;
		}
		if (removeFromGroup) {
			ui_WidgetRemoveFromGroup( node->widget );
		}
		node->widget = NULL;
	}
}

static void ui_TreechartClear1( UITreechartNode*** peaNodes, bool removeFromGroup )
{
	int it;
	int colIt;
	for( it = 0; it != eaSize( peaNodes ); ++it ) {
		UITreechartNode* node = (*peaNodes)[ it ];

		ui_TreechartClearNodeWidget( node, removeFromGroup );
		for( colIt = 0; colIt != eaSize( &(*peaNodes)[ it ]->columns ); ++colIt ) {
			ui_TreechartClear1( &(*peaNodes)[ it ]->columns[ colIt ]->nodes, removeFromGroup );
			free( (*peaNodes)[ it ]->columns[ colIt ]);
		}
	}
	eaDestroyEx( peaNodes, NULL );
}

static void ui_TreechartClearEx( UITreechart* chart, bool removeFromGroup )
{
	assert( !chart->disallowTreechartModification );

	// clear any drag state
	chart->draggingNode = NULL;
	chart->selectedNode = NULL;
	ui_DragCancel();

	ui_TreechartClear1( &chart->treeNodes, removeFromGroup );
}

void ui_TreechartClear( UITreechart* chart )
{
	ui_TreechartClearEx( chart, true );
}

void ui_TreechartFree( UITreechart* chart )
{
	ui_TreechartClear( chart );
	ui_ButtonFreeInternal( chart->trashButton );
	eaDestroyEx( &chart->queuedArrows, NULL );
	eaDestroy( &chart->eaTreeWidgetsNotRefreshed );
	ui_ScrollAreaFreeInternal( UI_SCROLLAREA( chart ));
}

static int widgetPriorityCmd( const UIWidget** lhs, const UIWidget** rhs, const void* ignored )
{
	return (int)(*rhs)->priority - (int)(*lhs)->priority;
}

void ui_TreechartTick( UITreechart* chart, UI_PARENT_ARGS )
{
	UI_GET_COORDINATES( chart );
	UISkin* skin = UI_GET_SKIN( chart );

	if( ui_IsHovering( UI_WIDGET( chart->trashButton ))) {
		ui_ButtonSetImage( chart->trashButton, skin->astrTreechartTrashHighlight );
	} else {
		ui_ButtonSetImage( chart->trashButton, skin->astrTreechartTrash );
	}

	if (chart->clickInsertF ) {
		if (ui_TreeChartCheckInsertionPlusClicks(chart, NULL)){
			inpHandled();
		}
	}

	// Handle external drag
	if( chart->draggingNode == &chart->externalDragNode ) {
		chart->scrollArea.forceAutoEdgePan = true;
		if( mouseUnfilteredUp( MS_LEFT )) {
			ui_TreechartWidgetDragEnd( NULL, NULL, NULL, UI_WIDGET( chart ));
			inpHandled();
		}
	} else {
		chart->scrollArea.forceAutoEdgePan = false;;
	}
	if( ui_TreechartIsDragging( chart ) && chart->draggingNode && chart->nodeTrashF ) {
		float oldZ = g_ui_State.drawZ;
		g_ui_State.drawZ = UI_TOP_Z; //< What, 9000?  There's no way that can be right.
		chart->trashButton->widget.tickF( chart->trashButton, UI_MY_VALUES );
		g_ui_State.drawZ = oldZ;
	}

	// Make the selected node have higher priority
	{
		const int fullWidthPriority = 20;
		const int selectedPriority = 10;
		const int normalPriority = 0;

		int it;
		for( it = 0; it != eaSize( &chart->widget.children ); ++it ) {
			UIWidget* child = chart->widget.children[ it ];

			if( child->bNoScrollX ) {
				child->priority = fullWidthPriority;
			} else if( chart->selectedNode && child == chart->selectedNode->widget ) {
				child->priority = selectedPriority;
			} else {
				child->priority = normalPriority;
			}
		}

		eaStableSort( chart->widget.children, NULL, widgetPriorityCmd );
	}

	{
		bool oldClickThrough = chart->widget.uClickThrough;
		chart->widget.uClickThrough = true;
		ui_ScrollAreaTick( UI_SCROLLAREA( chart ), UI_PARENT_VALUES );
		chart->widget.uClickThrough = oldClickThrough;
	}

	UI_TICK_EARLY( chart, false, false );


	{
		int width = 0;
		int height = 0;
		ui_TreechartLayoutMeasure( &width, &height, chart, chart->treeNodes );
		ui_TreechartLayoutPlace( chart, chart->treeNodes, 0, 0, width, height );
	}


	if( chart->selectedNodeMakeVisible ) {
		chart->selectedNodeMakeVisible = false;

		if( chart->selectedNode ) {
			Vec2 widgetCenter = { chart->selectedNode->targetPos[ 0 ] + chart->selectedNode->widget->width / 2,
								  chart->selectedNode->targetPos[ 1 ] + chart->selectedNode->widget->height / 2 };
			ui_ScrollAreaScrollToPosition( UI_SCROLLAREA( chart ), widgetCenter[ 0 ], widgetCenter[ 1 ]);
			chart->scrollArea.autoScrollCenter = true;
		}
	}
	ui_TreechartUpdateDrawState( chart, chart->treeNodes, false );

	UI_TICK_LATE( chart );
}

void ui_TreechartDraw( UITreechart* chart, UI_PARENT_ARGS )
{
	UI_GET_COORDINATES( chart );
	F32 origX = x;
	F32 origY = y;
	F32 origW = w;
	F32 origH = h;
	F32 origScale = scale;

	// If there's no smooth animation, then we need to make sure
	// everything is in the correct place or there will be flickering.
	if( !chart->nodeAnimateF ) {
		int width = 0;
		int height = 0;
		ui_TreechartLayoutMeasure( &width, &height, chart, chart->treeNodes );
		ui_TreechartLayoutPlace( chart, chart->treeNodes, 0, 0, width, height );

		// MJF TODO: possibly this should just always happen in draw?
		ui_TreechartUpdateDrawState( chart, chart->treeNodes, false );
	}

	ui_ScrollAreaDraw( UI_SCROLLAREA( chart ), UI_PARENT_VALUES );

	// Adjust drawing to clip to scroll bar
	{
		float sbW = 0;
		float sbH = 0;

		if (UI_WIDGET(chart)->sb->scrollX)
			sbH = ui_ScrollbarHeight(UI_WIDGET(chart)->sb) * scale;
		if (UI_WIDGET(chart)->sb->scrollY)
			sbW = ui_ScrollbarWidth(UI_WIDGET(chart)->sb) * scale;

		w -= sbW;
		h -= sbH;
		box.hx -= sbW;
		box.hy -= sbH;
	}

	UI_DRAW_EARLY( chart );

	// simulate the scrolling
	chart->lastDrawPixelsRect = box;
	x -= chart->widget.sb->xpos;
	y -= chart->widget.sb->ypos;

	chart->lastDrawX = x;
	chart->lastDrawY = y;
	chart->lastDrawZ = z;
	chart->lastDrawScale = scale;

	{
		UIDrawingDescription bgDesc = { 0 };
		ui_TreechartBGStyleFillDrawingDescription( chart, &bgDesc );
		ui_DrawingDescriptionDraw( &bgDesc, &box, UI_SCROLLAREA( chart )->childScale, z, 255, ColorBlack, ColorBlack );
	}

	treechartDrawFoundHighlight = false;
	{
		CBox fullWidthCBox = *clipperGetCurrentCBox();
		CBox nonFullWidthCBox = fullWidthCBox;
		if( chart->clipForFullWidth ) {
			nonFullWidthCBox.lx += ui_TreechartFullWidthContainerWidth;
		}
		ui_TreechartDrawDiagrams( chart, chart->treeNodes, &fullWidthCBox, &nonFullWidthCBox );
	}
	UI_DRAW_LATE_IF( chart, false );

	if( ui_TreechartIsDragging( chart ) && chart->draggingNode && chart->nodeTrashF ) {
		chart->trashButton->widget.drawF( chart->trashButton, origX, origY, origW, origH, origScale );
	}

	eaDestroyEx( &chart->queuedArrows, NULL );
}

static void ui_TreechartBeginRefresh1( UITreechart* chart, UITreechartNode*** peaNodes )
{
	int it;
	int colIt;
	for( it = 0; it != eaSize( peaNodes ); ++it ) {
		UITreechartNode* node = (*peaNodes)[ it ];

		eaPush( &chart->eaTreeWidgetsNotRefreshed, node->widget );
		ui_TreechartClearNodeWidget( node, false );
		if( node->leftWidget ) {
			eaPush( &chart->eaTreeWidgetsNotRefreshed, node->leftWidget );
		}

		for( colIt = 0; colIt != eaSize( &node->columns ); ++colIt ) {
			ui_TreechartBeginRefresh1( chart, &node->columns[ colIt ]->nodes );
		}
	}
}

void ui_TreechartBeginRefresh( SA_PARAM_NN_VALID UITreechart* chart )
{
	assert( eaSize( &chart->eaTreeWidgetsNotRefreshed ) == 0 );
	ui_TreechartBeginRefresh1( chart, &chart->treeNodes );
	ui_TreechartClearEx( chart, false );
}

void ui_TreechartEndRefresh( SA_PARAM_NN_VALID UITreechart* chart )
{
	FOR_EACH_IN_EARRAY( chart->eaTreeWidgetsNotRefreshed, UIWidget, widget ) {
		ui_WidgetRemoveFromGroup( widget );
	} FOR_EACH_END;

	eaDestroy( &chart->eaTreeWidgetsNotRefreshed );
}

void ui_TreechartDrawExtraArrowAngled( UITreechart* chart, Vec2 start, Vec2 end, bool isArrowDraggable, float scale )
{
	UITreechartQueuedArrow* accum = calloc( 1, sizeof( *accum ));
	copyVec2( start, accum->start );
	copyVec2( end, accum->end );
	accum->isArrowDraggable = isArrowDraggable;
	accum->scale = scale;

	eaPush( &chart->queuedArrows, accum );
}

/// Draw all the extra treechart things like arrows and containers.
void ui_TreechartDrawDiagrams( UITreechart* chart, UITreechartNode** nodes, CBox* fullWidthClipBox, CBox* nonFullWidthClipBox )
{
	float x = chart->lastDrawX;
	float y = chart->lastDrawY;
	float z = chart->lastDrawZ;
	float xNoScroll = chart->lastDrawPixelsRect.lx;
	float yNoScroll = chart->lastDrawPixelsRect.ly;
	
	float highlightZ = UI_GET_Z();
	float arrowZ = UI_GET_Z();
	float drawFZ = UI_GET_Z();
	float extraArrowZ = UI_GET_Z();
	float scale = chart->lastDrawScale * chart->scrollArea.childScale;
	int mouseX;
	int mouseY;
	int it;

	mousePos( &mouseX, &mouseY );
	for( it = 0; it != eaSize( &nodes ); ++it ) {
		UITreechartNode* node = eaGet( &nodes, it );

		if( node->flags & TreeNode_FullWidthContainerUI ) {
			clipperPush( fullWidthClipBox );
		} else {
			clipperPush( nonFullWidthClipBox );
		}

		// If a node is being dragged, we're not going to draw it
		// during the normal draw loop.  Draw a placeholder in its
		// place.
		if( node == chart->draggingNode ) {
			UIDrawingDescription desc = { 0 };
			CBox box;
			ui_TreechartPlaceholderFillDrawingDescription( chart, &desc );
			ui_TreechartNodeGetBox( &box, chart, node );

			if( node->flags & TreeNode_ContainerUI ) {
				box.hx = box.lx + MAX( node->columnTotalWidth, node->widget->width ) * scale;
				box.hy = box.ly + (node->widget->height + node->columnTotalHeight) * scale;
			}
			if( node->flags & TreeNode_FullWidthContainerUI ) {
				box.lx = chart->lastDrawPixelsRect.lx;
				box.hx = chart->lastDrawPixelsRect.hx;
			}

			ui_DrawingDescriptionDraw( &desc, &box, scale, highlightZ, 255, ColorWhite, ColorBlack );
		}

		ui_TreechartDrawDiagramsForNodeChildren( chart, node, fullWidthClipBox, nonFullWidthClipBox );

		// Draw arrow highlights
		if( !(node->flags & TreeNode_ArrowNode) && !(node->flags & TreeNode_NoArrowAfter) ) {
			UITreechartNode* afterNode = ui_TreechartNextNodeNonArrow( nodes, it, +1 );

			if( !(SAFE_MEMBER( afterNode, flags ) & TreeNode_NoArrowBefore) ) {
				UITreechartNode* immediatelyAfterNode = eaGet( &nodes, it + 1 );
				Vec2 afterAnchors[ 2 ];	//start and end of arrow.
				bool arrowHighlight = false;
				bool arrowIsValid;

				ui_TreechartNodeGetBottomAnchor( afterAnchors[ 0 ], chart, node );
				if( afterNode ) {
					ui_TreechartNodeGetTopAnchor( afterAnchors[ 1 ], chart, afterNode );
				} else {
					copyVec2( afterAnchors [ 0 ], afterAnchors[ 1 ]);
					afterAnchors[ 1 ][ 1 ] += 15;
				}

				if( chart->draggingNode && afterNode && chart->dragNAF ) {
					arrowIsValid = chart->dragNAF( chart, chart->cbData, false, chart->draggingNode->data, node->data, immediatelyAfterNode->data );
				} else {
					arrowIsValid = false;
				}

				if( ui_TreechartIsDragging( chart )) {
					if(   chart->draggingNode && !treechartDrawFoundHighlight && arrowIsValid
						  && ui_TreechartArrowCollides( afterAnchors[ 0 ], afterAnchors[ 1 ], scale, mouseX, mouseY )) {
						arrowHighlight = true;
						treechartDrawFoundHighlight = true;
					}
				}

				if( afterNode ) {
					if( chart->beforeDraggingNode == nodes[ it ] && chart->afterDraggingNode == afterNode ) {
						ui_TreechartArrowDrawToMouse( UI_GET_SKIN( chart ), afterAnchors[ 0 ], arrowZ, true, scale );
					} else {
						ui_TreechartArrowDraw( chart, UI_GET_SKIN( chart ), afterAnchors[ 0 ], afterAnchors[ 1 ], arrowZ,
											   !!(afterNode->flags & TreeNode_DragArrowBefore),
											   !(afterNode->flags & TreeNode_BranchArrowUI),
											   !!(afterNode->flags & TreeNode_AlternateArrowBefore),
											   arrowIsValid, arrowHighlight, scale );
						if (  !chart->draggingNode && chart->clickInsertF
							  && chart->clickInsertF( chart, chart->cbData, false, node->data, afterNode->data )) {
							ui_TreechartInsertionPlusDraw( UI_GET_SKIN( chart ), afterAnchors[ 0 ], afterAnchors[ 1 ], arrowZ, scale );
						}
					}
				}
			}
		}

		if( ui_TreechartIsDragging( chart )) {
			// The next two checks deal with drop targets, so they
			// need to ALSO pay attention to the FullWidthDropTarget
			// flag.
			{
				if( node->flags & (TreeNode_FullWidthContainerUI | TreeNode_FullWidthDropTarget) ) {
					clipperPush( fullWidthClipBox );
				} else {
					clipperPush( nonFullWidthClipBox );
				}
				if(   !treechartDrawFoundHighlight && !(node->flags & TreeNode_Stationary) && node != chart->draggingNode
					  && ui_TreechartNodeCollides( chart, node, mouseX, mouseY )
					  && ((chart->dragNNF && chart->draggingNode)
						  || chart->dragANF && (chart->beforeDraggingNode || chart->afterDraggingNode)) ) {
					// Dragging onto a node -- call the drawDragNNF
					ui_TreechartNodeDrawHighlight( chart, node, highlightZ, true, false );
					
					if( chart->draggingNode && chart->drawDragNNF ) {
						float wX = x + node->widget->x * scale;
						float wY = y + node->widget->y * scale;
						float wW = node->widget->width * scale;
						float wH = node->widget->height * scale;
						CBox box = { wX, wY, wX + wW, wY + wH };
						chart->drawDragNNF( chart, chart->cbData, &box, drawFZ, scale, chart->draggingNode->data, node->data );
					}

					treechartDrawFoundHighlight = true;
				} else if( !(node->flags & TreeNode_Stationary) && chart->draggingNode != node
						   && ((chart->dragNNF && chart->draggingNode)
							   || chart->dragANF && (chart->beforeDraggingNode || chart->afterDraggingNode)) ) {
					// Dragging near a node -- just highlight it
					ui_TreechartNodeDrawHighlight( chart, node, highlightZ, false, false );
				}

				clipperPop();
			}

			if( chart->dragNNColumnF && chart->draggingNode ) {
				ui_TreechartNodeDrawColumnHighlights( chart, node, highlightZ );
			}
		}

		if(   !ui_TreechartIsDragging( chart ) && node == chart->selectedNode
			  && !(node->flags & TreeNode_NoSelect) ) {
			// Selection
			
			ui_TreechartNodeDrawHighlight( chart, node, highlightZ, true, true );
		}

		clipperPop();
	}

	for( it = 0; it != eaSize( &chart->queuedArrows ); ++it ) {
		UITreechartQueuedArrow* arrow = chart->queuedArrows[ it ];
		ui_TreechartArrowDrawAngled( UI_GET_SKIN( chart ), arrow->start, arrow->end, extraArrowZ, arrow->isArrowDraggable, arrow->scale );
	}
}

// Draw the diagrams for all the child columns inside NODE.
void ui_TreechartDrawDiagramsForNodeChildren( UITreechart* chart, UITreechartNode* node, CBox* fullWidthClipBox, CBox* nonFullWidthClipBox )
{
	float x = chart->lastDrawX;
	float y = chart->lastDrawY;
	float z = chart->lastDrawZ;
	float arrowZ = UI_GET_Z();
	float scale = chart->lastDrawScale * chart->scrollArea.childScale;
	int mouseX;
	int mouseY;
	UIDrawingDescription contentBGDesc = { 0 };
	UIDrawingDescription bottomDesc = { 0 };
	UIDrawingDescription fullWidthBGDesc = { 0 };
	int childX, childY, childW, childH;

	if( !eaSize( &node->columns )) {
		return;
	}
	if( chart->draggingNode == node && (node->flags & (TreeNode_ContainerUI | TreeNode_FullWidthContainerUI)) ) {
		return;
	}

	mousePos( &mouseX, &mouseY );
	ui_TreechartGroupContentBGFillDrawingDescription( chart, &contentBGDesc );
	ui_TreechartGroupBottomFillDrawingDescription( chart, &bottomDesc );
	ui_TreechartFullWidthContentBGFillDrawingDescription( chart, &fullWidthBGDesc );

	ui_TreechartNodeCalcChildArea( chart, node, &childX, &childY, &childW, &childH );

	// Draw ContainerUI specific extras:
	//
	// * Background of container
	// * Bottom widget
	if( node->flags & TreeNode_ContainerUI ) {
		CBox cbox;
		CBoxSet( &cbox, childX, childY, childX + childW, childY + childH - ui_TreechartNodeBottomHeight( chart, node ) * scale);
		ui_DrawingDescriptionDraw( &contentBGDesc, &cbox, scale, z + 0.1, 255, ColorWhite, ColorBlack );

		CBoxSet( &cbox, childX, childY + childH - ui_TreechartNodeBottomHeight( chart, node ) * scale, childX + childW, childY + childH );
		ui_DrawingDescriptionDraw( &bottomDesc, &cbox, scale, z + 0.1, 255, ColorWhite, ColorBlack );
	}

	// Draw FullWidth specific extras:
	//
	// * Background of container
	if( node->flags & TreeNode_FullWidthContainerUI ) {
		CBox cbox;
		CBoxSet( &cbox, chart->lastDrawPixelsRect.lx, childY, chart->lastDrawPixelsRect.hx, childY + childH );
		ui_DrawingDescriptionDraw( &fullWidthBGDesc, &cbox, scale, z + 0.1, 255, ColorWhite, ColorBlack );
	}

	{
		int colIt;
		for( colIt = 0; colIt != eaSize( &node->columns ); ++colIt ) {
			UITreechartChildColumn* col = node->columns[ colIt ];
			UITreechartNode* colFront = ui_TreechartNextNodeNonArrow( col->nodes, -1, +1 );
			UITreechartNode* colBack = ui_TreechartNextNodeNonArrow( col->nodes, eaSize( &col->nodes ), -1 );
			bool beforeColHighlight = false;
			bool afterColHighlight = false;
			bool beforeColIsValid = false;
			bool afterColIsValid = false;
			Vec2 beforeColAnchors[ 2 ];
			Vec2 afterColAnchors[ 2 ];

			if( chart->dragNAF && chart->draggingNode ) {
				beforeColIsValid = chart->dragNAF( chart, chart->cbData, false, chart->draggingNode->data, NULL, colFront->data );
				afterColIsValid = chart->dragNAF( chart, chart->cbData, false, chart->draggingNode->data, colBack->data, NULL );
			} else {
				beforeColIsValid = false;
				afterColIsValid = false;
			}

			assert( eaSize( &col->nodes ));
			ui_TreechartNodeGetTopAnchor( beforeColAnchors[ 1 ], chart, colFront );
			setVec2( beforeColAnchors[ 0 ], beforeColAnchors[ 1 ][ 0 ], childY );

			ui_TreechartNodeGetBottomAnchor( afterColAnchors[ 0 ], chart, colBack );
			setVec2( afterColAnchors[ 1 ], afterColAnchors[ 0 ][ 0 ], childY + childH - ui_TreechartNodeBottomHeight( chart, node ));

			ui_TreechartDrawDiagrams( chart, node->columns[ colIt ]->nodes, fullWidthClipBox, nonFullWidthClipBox );

			if(   !treechartDrawFoundHighlight && !(colFront->flags & TreeNode_Stationary) && beforeColIsValid
				  && ui_TreechartArrowCollides( beforeColAnchors[ 0 ], beforeColAnchors[ 1 ], scale, mouseX, mouseY )) {
				beforeColHighlight = true;
				treechartDrawFoundHighlight = true;
			}
			if(   !treechartDrawFoundHighlight && !(colBack->flags & TreeNode_Stationary) && afterColIsValid
				  && ui_TreechartArrowCollides( afterColAnchors[ 0 ], afterColAnchors[ 1 ], scale, mouseX, mouseY )) {
				afterColHighlight = true;
				treechartDrawFoundHighlight = true;
			}

			if( colFront && (colFront->flags & TreeNode_NoArrowBefore) == 0 ) {
				if( chart->beforeDraggingNode == NULL && chart->afterDraggingNode == colFront ) {
					ui_TreechartArrowDrawToMouse( UI_GET_SKIN( chart ), beforeColAnchors[ 0 ], arrowZ, true, scale );
				} else {
					ui_TreechartArrowDraw( chart, UI_GET_SKIN( chart ), beforeColAnchors[ 0 ], beforeColAnchors[ 1 ], arrowZ,
										   !!(colFront->flags & TreeNode_DragArrowBefore),
										   !(colFront->flags & TreeNode_BranchArrowUI),
										   !!(colFront->flags & TreeNode_AlternateArrowBefore),
										   beforeColIsValid, beforeColHighlight, scale );
					if (! chart->draggingNode && chart->clickInsertF) {
						ui_TreechartInsertionPlusDraw( UI_GET_SKIN( chart ), beforeColAnchors[ 0 ], beforeColAnchors[ 1 ], arrowZ, scale );
					}
				}
			}
			if( colBack && (colBack->flags & TreeNode_NoArrowAfter) == 0 ) {
				if( node->flags & (TreeNode_ContainerUI | TreeNode_FullWidthContainerUI | TreeNode_BranchArrowUI) ) {
					if( chart->beforeDraggingNode == colBack && chart->afterDraggingNode == NULL ) {
						ui_TreechartArrowDrawToMouse( UI_GET_SKIN( chart ), afterColAnchors[ 0 ], arrowZ, true, scale );
					} else {
						ui_TreechartArrowDraw( chart, UI_GET_SKIN( chart ), afterColAnchors[ 0 ], afterColAnchors[ 1 ], arrowZ,
											   false,
											   !(node->flags & TreeNode_BranchArrowUI),
											   false,
											   afterColIsValid, afterColHighlight, scale );
						if (! chart->draggingNode && chart->clickInsertF) {
							ui_TreechartInsertionPlusDraw( UI_GET_SKIN( chart ), afterColAnchors[ 0 ], afterColAnchors[ 1 ], arrowZ, scale );
						}
					}
				}
			}
		}
	}

	// Draw BranchArrowUI specific extras:
	//
	// * Top arrow split
	// * Bottom arrow merge
	if( node->flags & TreeNode_BranchArrowUI ) {
		if( eaSize( &node->columns ) > 1 ) {
			UITreechartChildColumn* firstColumn = node->columns[ 0 ];
			UITreechartChildColumn* lastColumn = node->columns[ eaSize( &node->columns ) - 1 ];
			UITreechartNode* firstColumnFront = ui_TreechartNextNodeNonArrow( firstColumn->nodes, -1, +1 );
			UITreechartNode* lastColumnFront = ui_TreechartNextNodeNonArrow( lastColumn->nodes, -1, +1 );
			UITreechartNode* firstColumnBack = ui_TreechartNextNodeNonArrow( firstColumn->nodes, eaSize( &firstColumn->nodes ), -1 );
			UITreechartNode* lastColumnBack = ui_TreechartNextNodeNonArrow( lastColumn->nodes, eaSize( &lastColumn->nodes ), -1 );
			UIDrawingDescription branchDesc = { 0 };
			CBox cbox;
			Vec2 frontAnchors[ 2 ];
			Vec2 backAnchors[ 2 ];

			ui_TreechartBranchArrowFillDrawingDescription( chart, &branchDesc );

			ui_TreechartNodeGetTopAnchor( frontAnchors[ 0 ], chart, firstColumnFront );
			ui_TreechartNodeGetTopAnchor( frontAnchors[ 1 ], chart, lastColumnFront );
			CBoxSet( &cbox, frontAnchors[ 0 ][ 0 ], childY, frontAnchors[ 1 ][ 0 ], childY );
			ui_DrawingDescriptionOuterBox( &branchDesc, &cbox, scale );
			ui_DrawingDescriptionDraw( &branchDesc, &cbox, scale, arrowZ, 255, ColorBlack, ColorBlack );

			ui_TreechartNodeGetBottomAnchor( backAnchors[ 0 ], chart, firstColumnBack );
			ui_TreechartNodeGetBottomAnchor( backAnchors[ 1 ], chart, lastColumnBack );
			CBoxSet( &cbox, backAnchors[ 0 ][ 0 ], childY + childH, backAnchors[ 1 ][ 0 ], childY + childH );
			ui_DrawingDescriptionOuterBox( &branchDesc, &cbox, scale );
			ui_DrawingDescriptionDraw( &branchDesc, &cbox, scale, arrowZ, 255, ColorBlack, ColorBlack );
		}
	}
}

/// Return if the treechart is dragging something.
bool ui_TreechartIsDragging( UITreechart* chart )
{
	return chart->draggingNode != NULL || chart->beforeDraggingNode != NULL || chart->afterDraggingNode != NULL;
}

/// Set an externally managed thing to get drag-n-drop behavior like
/// an internal one.
void ui_TreechartSetExternalDrag( UITreechart* chart, const char* iconName, UserData data )
{
	UISkin* skin = UI_GET_SKIN( chart );
	UIDeviceState *device = ui_StateForDevice(g_ui_State.device);

	memset( &chart->externalDragNode, 0, sizeof( chart->externalDragNode ));
	chart->externalDragNode.iconName = iconName;
	chart->externalDragNode.data = data;
	chart->externalDragNode.flags = TreeNode_Normal;

	// Start up an external drag
	chart->draggingNode = &chart->externalDragNode;

	device->cursor.draggedIcon = atlasLoadTexture( iconName );
	device->cursor.draggedIconCenter = true;
	device->cursor.draggedIconColor = 0xFFFFFF70;
	device->cursor.dragCursorName = skin->astrTreechartDragCursor;
}

/// Set a specific widget as the selected widget.  This widget will
/// render differently.
void ui_TreechartSetSelectedChild( SA_PARAM_NN_VALID UITreechart* chart, SA_PARAM_OP_VALID UIWidget* child, bool makeVisible )
{
	bool changedSelection = false;

	if( child ) {
		UITreechartNode* node = ui_TreechartFindNodeByWidget( chart, child );
		changedSelection = (chart->selectedNode != node);
		chart->selectedNode = node;
	} else {
		changedSelection = (chart->selectedNode != NULL);
		chart->selectedNode = NULL;
	}

	if( chart->selectedNode ) {
		chart->selectedNodeMakeVisible = chart->selectedNodeMakeVisible || makeVisible;
	}
}

UIWidget* ui_TreechartGetSelectedChild( SA_PARAM_NN_VALID UITreechart* chart )
{
	return SAFE_MEMBER( chart->selectedNode, widget );
}

void ui_TreechartSetClipForFullWidth( SA_PARAM_NN_VALID UITreechart* chart, bool bValue )
{
	chart->clipForFullWidth = bValue;
}

void ui_TreechartSetLeftWidget( SA_PARAM_NN_VALID UITreechart* chart, SA_PARAM_OP_VALID UIWidget* child, UIWidget* leftWidget )
{
	UITreechartNode* node = ui_TreechartFindNodeByWidget( chart, child );
	node->leftWidget = leftWidget;
	if( node->leftWidget ) {
		ui_WidgetGroupMove( &UI_WIDGET( chart )->children, node->leftWidget );
	}
	eaFindAndRemove( &chart->eaTreeWidgetsNotRefreshed, leftWidget );
}

//helper for ui_TreeChartAddWidget
static void ui_TreechartAddWidget1( UITreechart* chart, UITreechartNode*** peaNodes, int index, UIWidget* widget, const char* iconName, UserData data, UITreechartNodeFlags flags )
{
	UITreechartNode* widgetNode = calloc( 1, sizeof( *widgetNode ));

	// ArrowLshd and DragArrowBefore are mutually exclusive
	assert( (flags & (TreeNode_ArrowNode | TreeNode_DragArrowBefore))
			!= (TreeNode_ArrowNode | TreeNode_DragArrowBefore) );

	widgetNode->widget = widget;
	widgetNode->data = data;
	widgetNode->flags = flags;
	widgetNode->iconName = iconName;

	// sanity checks:
	assert( widget->widthUnit != UIUnitPercentage );
	assert( widget->heightUnit != UIUnitPercentage );
	assert( !widget->preDragF && !widget->dragF && !widget->dropF );

	if( flags & TreeNode_Stationary ) {
		ui_WidgetSetPreDragCallback( widget, NULL, NULL );
		ui_WidgetSetDragCallback( widget, NULL, NULL );
		ui_WidgetSetAcceptCallback( widget, NULL, NULL );
	} else {
		ui_WidgetSetPreDragCallback( widget, ui_TreechartWidgetPreDrag, chart );
		ui_WidgetSetDragCallback( widget, ui_TreechartWidgetDragBegin, chart );
		ui_WidgetSetAcceptCallback( widget, ui_TreechartWidgetDragEnd, chart );
	}

	// DnD doesn't work if you don't have a tick function.
	if( !widget->tickF ) {
		widget->tickF = ui_TreechartWidgetDummyTick;
	}

	// FullWidth widgets shouldn't scroll and have to appear on top of
	// everything else.
	if( flags & TreeNode_FullWidthContainerUI ) {
		widget->bNoScrollX = true;
	} else {
		widget->bNoScrollX = false;
	}

	ui_WidgetGroupMove( &UI_WIDGET( chart )->children, widget );
	eaInsert( peaNodes, widgetNode, index );
	eaFindAndRemove( &chart->eaTreeWidgetsNotRefreshed, widget );
}

///adds a widget to the uiTreeChart.
void ui_TreechartAddWidget( UITreechart* chart, UIWidget* beforeWidget, UIWidget* widget, const char* iconName, UserData data, UITreechartNodeFlags flags )
{
	assert( !chart->disallowTreechartModification );

	if( !beforeWidget ) {
		ui_TreechartAddWidget1( chart, &chart->treeNodes, eaSize( &chart->treeNodes ), widget, iconName, data, flags );
	} else {
		UITreechartNode*** peaNodes = NULL;
		int index = 0;
		ui_TreechartFindNodeEAByWidget( &peaNodes, &index, chart, beforeWidget );
		assert( peaNodes );
		ui_TreechartAddWidget1( chart, peaNodes, index + 1, widget, iconName, data, flags );
	}
}

/// Add a widget as a child of another widget
void ui_TreechartAddChildWidget( UITreechart* chart, UIWidget* parentWidget, UIWidget* childWidget, const char* iconName, UserData data, UITreechartNodeFlags flags )
{
	UITreechartNode* parentNode = ui_TreechartFindNodeByWidget( chart, parentWidget );
	UITreechartChildColumn* col = calloc( 1, sizeof( *col ));
	assert( parentNode );

	eaPush( &parentNode->columns, col );
	ui_TreechartAddWidget1( chart, &col->nodes, eaSize( &col->nodes ), childWidget, iconName, data, flags );
}

/// Measure the size needed for NODES, store in WIDTH, HEIGHT.
///
/// Fully traverses the tree, also stores in COLUMN-WIDTH,
/// COLUMN-HEIGHT fields of each UITreechartNode.
void ui_TreechartLayoutMeasure( int* outWidth, int* outHeight, UITreechart* chart, UITreechartNode** nodes )
{
	UISkin* skin = UI_GET_SKIN( chart );
	int it;
	int yIt = 0;

	*outWidth = 0;
	*outHeight = 0;

	for( it = 0; it != eaSize( &nodes ); ++it ) {
		UITreechartNode* node = nodes[ it ];
		UITreechartNode* nextNode = eaGet( &nodes, it + 1 );

		if( (node->flags & (TreeNode_FullWidthContainerUI | TreeNode_BranchArrowUI)) == 0 ) {
			yIt += node->widget->height;
		}

		if( eaSize( &node->columns )) {
			int maxHeight = 0;
			int xIt = 0;
			int colIt;

			for( colIt = 0; colIt != eaSize( &node->columns ); ++colIt ) {
				UITreechartChildColumn* column = node->columns[ colIt ];
				UITreechartNode* columnFront = eaGet( &column->nodes, 0 );
				UITreechartNode* columnBack = eaGet( &column->nodes, eaSize( &column->nodes ) - 1 );
				float columnHeightIncludingArrows;
				ui_TreechartLayoutMeasure( &column->columnWidth, &column->columnHeight, chart, column->nodes );

				columnHeightIncludingArrows = column->columnHeight;
				if( columnFront && (columnFront->flags & TreeNode_NoArrowBefore) == 0 ) {
					columnHeightIncludingArrows += skin->iTreechartArrowHeight;
				}
				if( columnBack && (columnBack->flags & TreeNode_NoArrowAfter) == 0 ) {
					columnHeightIncludingArrows += skin->iTreechartArrowHeight;
				}
				maxHeight = MAX( maxHeight, columnHeightIncludingArrows );
				xIt += column->columnWidth + ui_TreechartBetweenColumnWidth;
			}
			xIt -= ui_TreechartBetweenColumnWidth;

			node->columnTotalWidth = xIt;
			node->columnTotalHeight = maxHeight + ui_TreechartNodeBottomHeight( chart, node );

			*outWidth = MAX( *outWidth, node->columnTotalWidth );
			yIt += node->columnTotalHeight;
		} else {
			if( node->flags & TreeNode_ArrowNode ) {
				*outWidth = MAX( *outWidth, node->widget->width * 2 );
			} else {
				*outWidth = MAX( *outWidth, node->widget->width );
			}

			node->columnTotalWidth = 0;
			node->columnTotalHeight = 0;
		}

		if(   nextNode && !(nextNode->flags & TreeNode_NoArrowBefore)
			  && !(node->flags & TreeNode_NoArrowAfter) ) {
			yIt += skin->iTreechartArrowHeight;
		}
	}

	*outHeight = 4 + yIt + 4;
}

/// Places each widget at its final position, assuming it should be
/// centered in the rectangle specified.
void ui_TreechartLayoutPlace( UITreechart* chart, UITreechartNode** nodes, int x, int y, int w, int h )
{
	UISkin* skin = UI_GET_SKIN( chart );
	int it;
	int yIt;

	yIt = y + 4;
	for( it = 0; it != eaSize( &nodes ); ++it ) {
		UITreechartNode* node = nodes[ it ];
		UITreechartNode* nextNode = eaGet( &nodes, it + 1 );

		if( node->flags & TreeNode_FullWidthContainerUI ) {
			// parent has full HEIGHT of children, appears off to the right
			node->widget->height = node->columnTotalHeight;
			node->widget->width = ui_TreechartFullWidthContainerWidth;

			setVec2( node->targetPos, 0, yIt );
		} else if( node->flags & TreeNode_BranchArrowUI ) {
			// The widget itself should be invisible... it isn't
			// actually there, just the arrow branches are
			node->widget->width = 0;
			node->widget->height = 0;

			setVec2( node->targetPos, floorf( x + w / 2 ), yIt );
		} else {
			// parents have the full width of their children, and are stretched out
			if( eaSize( &node->columns )) {
				node->widget->width = node->columnTotalWidth;
			}
			if( node->flags & TreeNode_ArrowNode ) {
				setVec2( node->targetPos, floorf( x + w / 2 ), yIt );
			} else {
				setVec2( node->targetPos, floorf( x + (w - node->widget->width) / 2 ), yIt );
			}
			yIt += node->widget->height;
		}

		if( eaSize( &node->columns )) {
			int xIt = x + (w - node->columnTotalWidth) / 2;
			int colIt;

			for( colIt = 0; colIt != eaSize( &node->columns ); ++colIt ) {
				UITreechartChildColumn* column = node->columns[ colIt ];
				UITreechartNode* columnFront = eaGet( &column->nodes, 0 );
				int columnY = yIt;

				if( columnFront && (columnFront->flags & TreeNode_NoArrowBefore) == 0 ) {
					columnY += skin->iTreechartArrowHeight;
				}

				ui_TreechartLayoutPlace( chart, column->nodes, xIt, columnY, column->columnWidth, column->columnHeight );
				xIt += column->columnWidth + ui_TreechartBetweenColumnWidth;
			}
			xIt -= ui_TreechartBetweenColumnWidth;

			yIt += node->columnTotalHeight;
		}

		if(   nextNode && !(nextNode->flags & TreeNode_NoArrowBefore)
			  && !(node->flags & TreeNode_NoArrowAfter) ) {
			yIt += skin->iTreechartArrowHeight;
		}
	}
}

/// Update widget internals (mainly the draw function) based on the
/// drag state.
void ui_TreechartUpdateDrawState( UITreechart* chart, UITreechartNode** nodes, bool parentIsDragging )
{
	int it;
	for( it = 0; it != eaSize( &nodes ); ++it ) {
		UITreechartNode* node = nodes[ it ];
		bool nodeIsDragging = parentIsDragging || node == chart->draggingNode;

		if( node == chart->draggingNode ) {
			if( node->widget->drawF != ui_TreechartWidgetDrawPlaceholder ) {
				node->widgetBackupDrawF = node->widget->drawF;
				node->widget->drawF = ui_TreechartWidgetDrawPlaceholder;
			}
		} else if( parentIsDragging ) {
			if( node->widget->drawF != ui_TreechartWidgetDrawPlaceholderChild ) {
				node->widgetBackupDrawF = node->widget->drawF;
				node->widget->drawF = ui_TreechartWidgetDrawPlaceholderChild;
			}
		} else if( (node->flags & TreeNode_DragArrowBefore) && node == chart->afterDraggingNode ) {
			if( node->widget->drawF != ui_TreechartWidgetDrawPlaceholder ) {
				node->widgetBackupDrawF = node->widget->drawF;
				node->widget->drawF = ui_TreechartWidgetDrawPlaceholder;
			}
		} else {
			if( node->widgetBackupDrawF ) {
				node->widget->drawF = node->widgetBackupDrawF;
				node->widgetBackupDrawF = NULL;
			}
		}

		if( chart->nodeAnimateF ) {
			float newX = node->widget->x + 0.25 * (node->targetPos[ 0 ] - node->widget->x);
			float newY = node->widget->y + 0.25 * (node->targetPos[ 1 ] - node->widget->y);

			if( fabs( newX - node->targetPos[ 0 ]) < 3 ) {
				newX = node->targetPos[ 0 ];
			}
			if( fabs( newY - node->targetPos[ 1 ]) < 3 ) {
				newY = node->targetPos[ 1 ];
			}

			ui_WidgetSetPosition( node->widget, newX, newY );
			if( node->leftWidget ) {
				ui_WidgetSetPosition( node->leftWidget, 0, newY + (node->widget->height - node->leftWidget->height) / 2 );
				node->leftWidget->bNoScrollX = true;
			}
			chart->nodeAnimateF( chart, chart->cbData, node->data, newX, newY );
		} else {
			ui_WidgetSetPosition( node->widget, node->targetPos[ 0 ], node->targetPos[ 1 ]);
			if( node->leftWidget ) {
				ui_WidgetSetPosition( node->leftWidget, 0, node->targetPos[ 1 ]);
				node->leftWidget->bNoScrollX = true;
			}
		}

		if( eaSize( &node->columns )) {
			int colIt;
			for( colIt = 0; colIt != eaSize( &node->columns ); ++colIt ) {
				ui_TreechartUpdateDrawState( chart, node->columns[ colIt ]->nodes,
											 nodeIsDragging && (node->flags & (TreeNode_ContainerUI | TreeNode_FullWidthContainerUI
																			   |TreeNode_BranchArrowUI)) );
			}
		}
	}
}

static void ui_TreechartFindNodeByWidget1( UITreechartNode**** out_ppeaNodes, int* out_index,  UITreechartNode*** peaNodes, UIWidget* widget )
{
	int it;
	int colIt;

	for( it = 0; it != eaSize( peaNodes ); ++it ) {
		UITreechartNode* node = (*peaNodes)[ it ];

		if( node->widget == widget ) {
			*out_ppeaNodes = peaNodes;
			*out_index = it;
			return;
		}
		for( colIt = 0; colIt != eaSize( &node->columns ); ++colIt ) {
			ui_TreechartFindNodeByWidget1( out_ppeaNodes, out_index, &node->columns[ colIt ]->nodes, widget );
			if( *out_ppeaNodes ) {
				return;
			}
		}
	}

	*out_ppeaNodes = NULL;
	*out_index = 0;
	return;
}

UITreechartNode* ui_TreechartFindNodeByWidget( UITreechart* chart, UIWidget* widget )
{
	UITreechartNode*** pEA = NULL;
	int index = 0;

	ui_TreechartFindNodeByWidget1( &pEA, &index, &chart->treeNodes, widget );
	if( pEA ) {
		return (*pEA)[ index ];
	} else {
		return NULL;
	}
}

static void ui_TreechartFindNodeEAByWidget1( UITreechartNode**** ppEA, int* outIndex, UITreechart* chart, UITreechartNode*** pEA, UIWidget* widget )
{
	int it;

	for( it = 0; it != eaSize( pEA ); ++it ) {
		UITreechartNode* node = (*pEA)[ it ];
		if( node->widget == widget ) {

			*ppEA = pEA;
			*outIndex = it;
			return;
		}

		{
			int colIt;
			for( colIt = 0; colIt != eaSize( &node->columns ); ++colIt ) {
				ui_TreechartFindNodeEAByWidget1( ppEA, outIndex, chart, &node->columns[ colIt ]->nodes, widget );
				if( *ppEA ) {
					return;
				}
			}
		}
	}
}

void ui_TreechartFindNodeEAByWidget( UITreechartNode**** ppEA, int* outIndex, UITreechart* chart, UIWidget* widget )
{
	*ppEA = NULL;
	*outIndex = 0;

	ui_TreechartFindNodeEAByWidget1( ppEA, outIndex, chart, &chart->treeNodes, widget );
}

void ui_TreechartNodeCalcChildArea( UITreechart* chart, UITreechartNode* node, int* out_x, int* out_y, int* out_w, int* out_h )
{
	float x = chart->lastDrawX;
	float y = chart->lastDrawY;
	float scale = chart->lastDrawScale * chart->scrollArea.childScale;

	if( node->flags & TreeNode_FullWidthContainerUI ) {
		*out_x = x;
		*out_y = y + node->widget->y * scale;
		*out_w = node->columnTotalWidth * scale;
		*out_h = node->columnTotalHeight * scale;
	} else {
		*out_x = x + (node->widget->x + (node->widget->width - node->columnTotalWidth) / 2) * scale;
		*out_y = y + (node->widget->y + node->widget->height) * scale;
		*out_w = MAX( node->columnTotalWidth, node->widget->width ) * scale;
		*out_h = node->columnTotalHeight * scale;
	}
}

/// Draw the placeholder for a widget being dragged
void ui_TreechartWidgetDrawPlaceholder( UIWidget* widget, UI_PARENT_ARGS )
{
	// do nothing -- I should draw this as part of the treechart diagram rendering.
}

/// Draw the placeholder for a widget that is a child of one being
/// dragged.
void ui_TreechartWidgetDrawPlaceholderChild( UIWidget* widget, UI_PARENT_ARGS )
{
	// do nothing -- I don't want such a widget to draw
}

/// Null tick function for DnD support.
void ui_TreechartWidgetDummyTick( UIWidget* widget, UI_PARENT_ARGS )
{
	UIWidgetClass* widgetHack = (UIWidgetClass*)widget;

	UI_GET_COORDINATES( widgetHack );
	UI_TICK_EARLY( widgetHack, false, false );
	UI_TICK_LATE( widgetHack );
}

/// Drag function for a widget.
void ui_TreechartWidgetPreDrag( UIWidget* dragWidget, UIWidget* rawChart )
{
	UITreechart* chart = (UITreechart*)rawChart;
	UITreechartNode* dragNode = ui_TreechartFindNodeByWidget( chart, dragWidget );
	UISkin* skin = UI_GET_SKIN( chart );

	if( dragNode->flags & (TreeNode_Stationary | TreeNode_NoDrag) ) {
		return;
	}

	ui_SetCursorByName( skin->astrTreechartPredragCursor );
	ui_CursorLock();
}

/// Drag function for a widget.
void ui_TreechartWidgetDragBegin( UIWidget* dragWidget, UIWidget* rawChart )
{
	UITreechart* chart = (UITreechart*)rawChart;
	UITreechartNode* dragNode = ui_TreechartFindNodeByWidget( chart, dragWidget );
	UISkin* skin = UI_GET_SKIN( chart );

	if( dragNode->flags & (TreeNode_Stationary | TreeNode_NoDrag) ) {
		return;
	}

	chart->disallowTreechartModification = true;
	ui_SetFocus( NULL );
	chart->disallowTreechartModification = false;
	ui_DragStartEx( dragWidget, "treechart_drag", NULL, atlasLoadTexture(dragNode->iconName), 0xFFFFFFFF, true, skin->astrTreechartDragCursor );
	chart->draggingNode = dragNode;
}

/// Main workhorse for ui_TreechartWidgetDragEnd().
static bool ui_TreechartWidgetDragEnd1( UITreechart* chart, UITreechartNode** nodes )
{
	int it;
	int mouseX;
	int mouseY;
	float scale = chart->lastDrawScale * chart->scrollArea.childScale;
	mousePos( &mouseX, &mouseY );

	if( !point_cbox_clsn( mouseX, mouseY, &chart->lastDrawPixelsRect )) {
		return false;
	}

	for( it = 0; it != eaSize( &nodes ); ++it ) {
		UITreechartNode* node = eaGet( &nodes, it );

		if( node == chart->draggingNode && (node->flags & TreeNode_ContainerUI) ) {
			continue;
		}

		
		// Column start / end arrows
		{
			int childX, childY, childW, childH;
			int colIt;

			ui_TreechartNodeCalcChildArea( chart, node, &childX, &childY, &childW, &childH );

			for( colIt = 0; colIt != eaSize( &node->columns ); ++colIt ) {
				UITreechartChildColumn* col = node->columns[ colIt ];
				UITreechartNode* colFront = ui_TreechartNextNodeNonArrow( col->nodes, -1, +1 );
				UITreechartNode* colBack = ui_TreechartNextNodeNonArrow( col->nodes, eaSize( &col->nodes ), -1 );
				Vec2 beforeColAnchors[ 2 ];
				Vec2 afterColAnchors[ 2 ];

				assert( eaSize( &col->nodes ));
				ui_TreechartNodeGetTopAnchor( beforeColAnchors[ 1 ], chart, colFront );
				setVec2( beforeColAnchors[ 0 ], beforeColAnchors[ 1 ][ 0 ], childY );

				ui_TreechartNodeGetBottomAnchor( afterColAnchors[ 0 ], chart, colBack );
				setVec2( afterColAnchors[ 1 ], afterColAnchors[ 0 ][ 0 ], childY + childH );

				if( ui_TreechartWidgetDragEnd1( chart, col->nodes )) {
					return true;
				}

				if(   chart->draggingNode && chart->dragNAF && !(colFront->flags & TreeNode_Stationary)
					  && ui_TreechartArrowCollides( beforeColAnchors[ 0 ], beforeColAnchors[ 1 ], chart->lastDrawScale, mouseX, mouseY )) {
					chart->dragNAF( chart, chart->cbData, true, chart->draggingNode->data, NULL, colFront->data );
					return true;
				}
				if(   chart->draggingNode && chart->dragNAF && !(colBack->flags & TreeNode_Stationary)
					  && (node->flags & (TreeNode_ContainerUI | TreeNode_FullWidthContainerUI | TreeNode_BranchArrowUI))
					  && ui_TreechartArrowCollides( afterColAnchors[ 0 ], afterColAnchors[ 1 ], chart->lastDrawScale, mouseX, mouseY )) {
					chart->dragNAF( chart, chart->cbData, true, chart->draggingNode->data, colBack->data, NULL );
					return true;
				}
			}
		}

		// Onto node, onto arrow after  
		{
			UITreechartNode* afterNode = ui_TreechartNextNodeNonArrow( nodes, it, +1 );
			UITreechartNode* immediatelyAfterNode = eaGet( &nodes, it + 1 );
			Vec2 afterAnchors[ 2 ];

			ui_TreechartNodeGetBottomAnchor( afterAnchors[ 0 ], chart, node );
			if( afterNode ) {
				ui_TreechartNodeGetTopAnchor( afterAnchors[ 1 ], chart, afterNode );
			} else {
				copyVec2( afterAnchors[ 0 ], afterAnchors[ 1 ]);
				afterAnchors[ 1 ][ 1 ] += 15;
			}

			if(   chart->draggingNode && chart->dragNAF && afterNode
				  && !(node->flags & TreeNode_NoArrowAfter)
				  && !(afterNode->flags & TreeNode_NoArrowBefore)
				  && ui_TreechartArrowCollides( afterAnchors[ 0 ], afterAnchors[ 1 ], chart->lastDrawScale, mouseX, mouseY )) {
				chart->dragNAF( chart, chart->cbData, true, chart->draggingNode->data, node->data, immediatelyAfterNode->data );
				return true;
			} else if( !(node->flags & (TreeNode_Stationary | TreeNode_ArrowNode)) && ui_TreechartNodeCollides( chart, node, mouseX, mouseY )) {
				if( chart->dragNNF && chart->draggingNode ) {
					chart->dragNNF( chart, chart->cbData, true, chart->draggingNode->data, node->data );
					return true;
				}
				if( chart->dragANF && (chart->beforeDraggingNode || chart->afterDraggingNode) ) {
					chart->dragANF( chart, chart->cbData, true, SAFE_MEMBER( chart->beforeDraggingNode, data ), SAFE_MEMBER( chart->afterDraggingNode, data ), node->data );
					return true;
				}
			}
		}

		// Onto columns
		if( chart->draggingNode && chart->dragNNColumnF ) {
			if( eaSize( &node->columns )) {
				int colIt;
				int childX;
				int childY;
				int childW;
				int childH;
				int xIt;
				ui_TreechartNodeCalcChildArea( chart, node, &childX, &childY, &childW, &childH );
				xIt = childX;
				for( colIt = 0; colIt <= eaSize( &node->columns ); ++colIt ) {
					UITreechartChildColumn* column = eaGet( &node->columns, colIt );
					CBox columnBox;
					columnBox.lx = xIt - ui_TreechartBetweenColumnWidth * scale;
					columnBox.hx = xIt;
					columnBox.ly = childY;
					columnBox.hy = childY + childH;
					if( CBoxIntersectsPoint( &columnBox, mouseX, mouseY )) {
						if( chart->dragNNColumnF( chart, chart->cbData, true, chart->draggingNode->data, node->data, colIt )) {
							return true;
						}
					}

					if( column ) {
						xIt += (column->columnWidth + ui_TreechartBetweenColumnWidth) * scale;
					}
				}
			} else {
				CBox nodeBox;
				CBox columnBox;
				ui_TreechartNodeGetBox( &nodeBox, chart, node );
				columnBox.lx = nodeBox.lx - ui_TreechartBetweenColumnWidth;
				columnBox.hx = nodeBox.lx;
				columnBox.ly = nodeBox.ly;
				columnBox.hy = nodeBox.hy;
				if( CBoxIntersectsPoint( &columnBox, mouseX, mouseY )) {
					if( chart->dragNNColumnF( chart, chart->cbData, true, chart->draggingNode->data, node->data, 0 )) {
						return true;
					}
				}
				columnBox.lx = nodeBox.hx;
				columnBox.hx = nodeBox.hx + ui_TreechartBetweenColumnWidth;
				columnBox.ly = nodeBox.ly;
				columnBox.hy = nodeBox.hy;
				if( CBoxIntersectsPoint( &columnBox, mouseX, mouseY )) {
					if( chart->dragNNColumnF( chart, chart->cbData, true, chart->draggingNode->data, node->data, 1 )) {
						return true;
					}
				}
			}
		}
	}

	return false;
}

/// Drop function for a widget.
void ui_TreechartWidgetDragEnd( UIWidget* dragWidget, UIWidget* ignoredDest, UIDnDPayload* ignored, UIWidget* rawChart )
{
	UIDeviceState *device = ui_StateForDevice(g_ui_State.device);
	UITreechart* chart = (UITreechart*)rawChart;

	if( chart->draggingNode ) {
		// It shouldn't be possible to start dragging such a node, but
		// just in case prevent calling any callbacks.
		if( chart->draggingNode->flags & (TreeNode_Stationary | TreeNode_NoDrag) ) {
			goto cleanup;
		}

		// detect trash
		if( chart->draggingNode && chart->nodeTrashF ) {
			if( ui_IsHovering( UI_WIDGET( chart->trashButton ))) {
				chart->nodeTrashF( chart, chart->cbData, chart->draggingNode->data );
				goto cleanup;
			}
		}

		// detect if a motion would be done
		if( chart->dragNNF || chart->dragNAF || chart->dragNNColumnF ) {
			ui_TreechartWidgetDragEnd1( chart, chart->treeNodes );
			goto cleanup;
		}
	}
	if( chart->beforeDraggingNode || chart->afterDraggingNode ) {
		// detect if a motion would be done
		if( chart->dragANF ) {
			ui_TreechartWidgetDragEnd1( chart, chart->treeNodes );
			goto cleanup;
		}
	}

cleanup:
	chart->draggingNode = NULL;
	chart->beforeDraggingNode = NULL;
	chart->afterDraggingNode = NULL;
	device->cursor.draggedIcon = NULL;
	device->cursor.dragCursorName = NULL;
}

/// Drag function for an arrow
void ui_TreechartArrowDrag( UITreechart* chart, UITreechartNode* beforeNode, UITreechartNode* afterNode )
{
	UISkin* skin = UI_GET_SKIN( chart );
	if( !beforeNode && !afterNode ) {
		return;
	}
	if( !MouseInputIsAllowed() ) {
		return;
	}

	ui_SetCursorByName( skin->astrTreechartPredragCursor );
	ui_CursorLock();

	if( mouseDrag( MS_LEFT )) {
		ui_DragStartEx( UI_WIDGET( chart ), "treechart_drag", NULL, atlasLoadTexture("white"), 0xFFFFFFFF, true, skin->astrTreechartDragCursor );
		chart->beforeDraggingNode = beforeNode;
		chart->afterDraggingNode = afterNode;
	}

	inpHandled();
}

/// Get a node that is not of type ArrowNode.
///
/// Advancing in direction DIR from POS, this looks at every node in
/// NODES to find a non-arrow node.  This is useful to find the
/// termination points of an arrow being drawn by the treechart.
UITreechartNode* ui_TreechartNextNodeNonArrow( UITreechartNode** nodes, int pos, int dir )
{
	while( true ) {
		pos += dir;
		if( pos < 0 || pos >= eaSize( &nodes )) {
			break;
		}
		if( !(nodes[ pos ]->flags & TreeNode_ArrowNode) ) {
			break;
		}
	}
	return eaGet( &nodes, pos );
}

/// Get the node's box.
///
/// This is used to figure out where to draw highlights and such.
void ui_TreechartNodeGetBox( CBox* outBox, UITreechart* chart, UITreechartNode* node )
{
	float scale = chart->lastDrawScale * chart->scrollArea.childScale;

	if( node->widget->bNoScrollX ) {
		outBox->lx = chart->lastDrawPixelsRect.lx + node->widget->x * scale;
	} else {
		outBox->lx = chart->lastDrawX + node->widget->x * scale;
	}
	if( node->widget->bNoScrollY ) {
		outBox->ly = chart->lastDrawPixelsRect.ly + node->widget->y * scale;
	} else {
		outBox->ly = chart->lastDrawY + node->widget->y * scale;
	}
	outBox->hx = outBox->lx + node->widget->width * scale;
	outBox->hy = outBox->ly + node->widget->height * scale;

	outBox->lx += node->widget->leftPad;
	outBox->ly += node->widget->topPad;
	outBox->hx -= node->widget->rightPad;
	outBox->hy -= node->widget->bottomPad;
}

/// Get the top anchor point for NODE and store it in OUT-POS.
///
/// X and Y are used as offsets, they should come from the standard UI
/// traversal.
void ui_TreechartNodeGetTopAnchor( Vec2 outPos, UITreechart* chart, UITreechartNode* node )
{
	float scale = chart->lastDrawScale * chart->scrollArea.childScale;

	if( node->flags & TreeNode_FullWidthContainerUI ) {
		outPos[ 0 ] = chart->lastDrawX + (node->widget->x + node->columnTotalWidth / 2) * scale;
		outPos[ 1 ] = chart->lastDrawY + node->widget->y * scale;
	} else {
		outPos[ 0 ] = chart->lastDrawX + (node->widget->x + node->widget->width / 2) * scale;
		outPos[ 1 ] = chart->lastDrawY + node->widget->y * scale;
	}
}

/// Get the bottom anchor point for NODE and store it in OUT-POS.
///
/// X and Y are used as offsets, they should come from the standard UI
/// traversal.
void ui_TreechartNodeGetBottomAnchor( Vec2 outPos, UITreechart* chart, UITreechartNode* node )
{
	float scale = chart->lastDrawScale * chart->scrollArea.childScale;

	if( node->flags & TreeNode_FullWidthContainerUI ) {
		outPos[ 0 ] = chart->lastDrawX + (node->widget->x + node->columnTotalWidth / 2) * scale;
		outPos[ 1 ] = chart->lastDrawY + (node->widget->y + node->widget->height) * scale;
	} else {
		outPos[ 0 ] = chart->lastDrawX + (node->widget->x + node->widget->width / 2) * scale;
		outPos[ 1 ] = chart->lastDrawY + (node->widget->y + node->widget->height + node->columnTotalHeight) * scale;
	}
}

/// Draw a highlight rect around everything in NODE.
void ui_TreechartNodeDrawHighlight( UITreechart* chart, UITreechartNode* node, F32 z, bool isHighlighted, bool isSelection )
{
	float scale = chart->lastDrawScale * chart->scrollArea.childScale;
	CBox nodeBox;
	U8 alpha;
	UIDrawingDescription desc = { 0 };
	ui_TreechartNodeHighlightFillDrawingDescription( chart, &desc );

	ui_TreechartNodeGetBox( &nodeBox, chart, node );

	if( isSelection ) {
		ui_DrawingDescriptionOuterBox( &desc, &nodeBox, scale );
	} else if( node->flags & TreeNode_FullWidthDropTarget ) {
		// Used to draw drop targets
		nodeBox.lx = chart->lastDrawPixelsRect.lx;
		nodeBox.hx = chart->lastDrawPixelsRect.hx;
	}

	if( chart->draggingNode && chart->dragNNF && !chart->dragNNF( chart, chart->cbData, false, chart->draggingNode->data, node->data )) {
		alpha = 0;
	} else if( (chart->beforeDraggingNode || chart->afterDraggingNode) && chart->dragANF && !chart->dragANF( chart, chart->cbData, false, SAFE_MEMBER( chart->beforeDraggingNode, data ), SAFE_MEMBER( chart->afterDraggingNode, data ), node->data )) {
		alpha = 0;
	} else if( isHighlighted ) {
		alpha = 255;
	} else if( ui_TreechartIsDragging( chart )) {
		int mouse[2];
		float dist;

		mousePos( &mouse[0], &mouse[1] );
		dist = fabs( mouse[ 1 ] - (nodeBox.ly + nodeBox.hy) / 2 );

		alpha = lerp( 128, ui_TreechartMinHLValue, CLAMP( dist / ui_TreechartMaxHLDist, 0, 1 ));
	} else {
		alpha = 0;
	}


	ui_DrawingDescriptionDraw( &desc, &nodeBox, scale, z, alpha, ColorRed, ColorBlack );
}

static U8 ui_TreechartColumnBoxHighlightAlpha( const CBox* columnBox, IVec2 mouse )
{
	if( CBoxIntersectsPoint( columnBox, mouse[0], mouse[1] )) {
		return 255;
	} else {
		float dist;
		dist = fabs(mouse[1] - (columnBox->ly + columnBox->hy) / 2);
		return lerp( 128, ui_TreechartMinHLValue, CLAMP( dist / ui_TreechartMaxHLDist, 0, 1 ));
	}
}

/// Draw a highlight on all the drag target columns of node
void ui_TreechartNodeDrawColumnHighlights( UITreechart* chart, UITreechartNode* node, F32 z )
{
	UISkin* skin = UI_GET_SKIN( chart );
	float scale = chart->lastDrawScale * chart->scrollArea.childScale;
	IVec2 mouse;
	UIDrawingDescription desc = { 0 };
	UIDrawingDescription highlightActiveDesc = { 0 };

	if( node->flags & (TreeNode_FullWidthContainerUI | TreeNode_ContainerUI) ) {
		return;
	}

	mousePos( &mouse[0], &mouse[1] );
	ui_TreechartArrowHighlightFillDrawingDescription( chart, &desc );
	ui_TreechartArrowHighlightActiveFillDrawingDescription( chart, &highlightActiveDesc );

	if( eaSize( &node->columns )) {
		int it;
		int childX;
		int childY;
		int childW;
		int childH;
		int xIt;
		ui_TreechartNodeCalcChildArea( chart, node, &childX, &childY, &childW, &childH );
		xIt = childX;
		for( it = 0; it <= eaSize( &node->columns ); ++it ) {
			UITreechartChildColumn* column = eaGet( &node->columns, it );
			if( chart->dragNNColumnF( chart, chart->cbData, false, chart->draggingNode->data, node->data, it )) {
				CBox columnBox;
				U8 alpha;
				columnBox.lx = xIt - ui_TreechartBetweenColumnWidth * scale;
				columnBox.hx = xIt;
				columnBox.ly = childY;
				columnBox.hy = childY + childH;
				alpha = ui_TreechartColumnBoxHighlightAlpha( &columnBox, mouse );
				ui_DrawingDescriptionDraw( &desc, &columnBox, scale, z, alpha, ColorRed, ColorBlack );
				if( alpha == 255 ) {
					ui_DrawingDescriptionDraw( &highlightActiveDesc, &columnBox, scale, z, 255, ColorWhite, ColorWhite );
				}
			}

			if( column ) {
				xIt += (column->columnWidth + ui_TreechartBetweenColumnWidth) * scale;
			}
		}
	} else {
		CBox nodeBox;
		ui_TreechartNodeGetBox( &nodeBox, chart, node );

		if( chart->dragNNColumnF( chart, chart->cbData, false, chart->draggingNode->data, node->data, 0 )) {
			CBox columnBox;
			U8 alpha;
			columnBox.lx = nodeBox.lx - ui_TreechartBetweenColumnWidth;
			columnBox.hx = nodeBox.lx;
			columnBox.ly = nodeBox.ly;
			columnBox.hy = nodeBox.hy;
			alpha = ui_TreechartColumnBoxHighlightAlpha( &columnBox, mouse );
			ui_DrawingDescriptionDraw( &desc, &columnBox, scale, z, alpha, ColorRed, ColorBlack );
			if( alpha == 255 ) {
				ui_DrawingDescriptionDraw( &highlightActiveDesc, &columnBox, scale, z, 255, ColorWhite, ColorWhite );
			}
		}
		if( chart->dragNNColumnF( chart, chart->cbData, false, chart->draggingNode->data, node->data, 1 )) {
			CBox columnBox;
			U8 alpha;
			columnBox.lx = nodeBox.hx;
			columnBox.hx = nodeBox.hx + ui_TreechartBetweenColumnWidth;
			columnBox.ly = nodeBox.ly;
			columnBox.hy = nodeBox.hy;
			alpha = ui_TreechartColumnBoxHighlightAlpha( &columnBox, mouse );
			ui_DrawingDescriptionDraw( &desc, &columnBox, scale, z, ui_TreechartColumnBoxHighlightAlpha( &columnBox, mouse ), ColorRed, ColorBlack );
			if( alpha == 255 ) {
				ui_DrawingDescriptionDraw( &highlightActiveDesc, &columnBox, scale, z, 255, ColorWhite, ColorWhite );
			}
		}
	}
}

/// Check if position X, Y collides with NODE (or its child area)
bool ui_TreechartNodeCollides( UITreechart* chart, UITreechartNode* node, float x, float y )
{
	float scale = chart->lastDrawScale * chart->scrollArea.childScale;
	CBox nodeBox;

	ui_TreechartNodeGetBox( &nodeBox, chart, node );
	if( node->flags & TreeNode_FullWidthDropTarget ) {
		nodeBox.lx = chart->lastDrawPixelsRect.lx;
		nodeBox.hx = chart->lastDrawPixelsRect.hx;
	}

	if( nodeBox.lx <= x && x <= nodeBox.hx && nodeBox.ly <= y && y <= nodeBox.hy ) {
		return true;
	}

	/* Disabled collision against the child contents since it's not being drawn during the highlight
	if(   wX + (wW - node->columnTotalWidth * scale) / 2 <= x && x <= wX + (wW + node->columnTotalWidth * scale) / 2
		  && wY + wH <= y && y <= wY + wH + node->columnTotalHeight * scale ) {
		return true;
	}
	*/

	return false;
}

/// Return the height in pixels of the bottom for NODE.
int ui_TreechartNodeBottomHeight( UITreechart* chart, UITreechartNode* node )
{
	if( node->flags & TreeNode_ContainerUI ) {
		return ui_TreechartNodeDefaultBottomHeight;
	} else {
		return 0;
	}
}

/// Gets the drawing description for the background of the whole chart
void ui_TreechartBGStyleFillDrawingDescription( SA_PARAM_NN_VALID UITreechart* chart, UIDrawingDescription* desc )
{
	UISkin* skin = UI_GET_SKIN( chart );

	if( skin->bUseStyleBorders || skin->bUseTextureAssemblies ) {
		const char* descName = skin->astrTreechartBGStyle;
		if( skin->bUseTextureAssemblies && RefSystem_ReferentFromString( "UITextureAssembly", descName )) {
			desc->textureAssemblyName = descName;
		} else {
			desc->styleBorderName = descName;
		}
	} else {
		desc->styleBorderNameUsingLegacyColor = "Default_Capsule_Filled";
	}
}

void ui_TreechartPlaceholderFillDrawingDescription( SA_PARAM_NN_VALID UITreechart* chart, UIDrawingDescription* desc )
{
	UISkin* skin = UI_GET_SKIN( chart );

	if( skin->bUseStyleBorders || skin->bUseTextureAssemblies ) {
		const char* descName = skin->astrTreechartPlaceholderStyle;
		if( skin->bUseTextureAssemblies && RefSystem_ReferentFromString( "UITextureAssembly", descName )) {
			desc->textureAssemblyName = descName;
		} else {
			desc->styleBorderName = descName;
		}
	} else {
		desc->styleBorderNameUsingLegacyColor = "Default_Capsule_Filled";
	}
}

/// Get the drawing description for the background of a group.
void ui_TreechartGroupContentBGFillDrawingDescription( SA_PARAM_NN_VALID UITreechart* chart, UIDrawingDescription* desc )
{
	UISkin* skin = UI_GET_SKIN( chart );

	if( skin->bUseStyleBorders || skin->bUseTextureAssemblies ) {
		const char* descName = skin->astrTreechartGroupContentBGStyle;
		if( skin->bUseTextureAssemblies && RefSystem_ReferentFromString( "UITextureAssembly", descName )) {
			desc->textureAssemblyName = descName;
		} else {
			desc->styleBorderName = descName;
		}
	} else {
		desc->styleBorderNameUsingLegacyColor = "Default_Capsule_Filled";
	}
}

/// Get the drawing description for the bottom part of a group.
void ui_TreechartGroupBottomFillDrawingDescription( SA_PARAM_NN_VALID UITreechart* chart, UIDrawingDescription* desc )
{
	UISkin* skin = UI_GET_SKIN( chart );

	if( skin->bUseStyleBorders || skin->bUseTextureAssemblies ) {
		const char* descName = skin->astrTreechartGroupBottomStyle;
		if( skin->bUseTextureAssemblies && RefSystem_ReferentFromString( "UITextureAssembly", descName )) {
			desc->textureAssemblyName = descName;
		} else {
			desc->styleBorderName = descName;
		}
	} else {
		desc->styleBorderNameUsingLegacyColor = "Default_Capsule_Filled";
	}
}

/// Get the drawing description for the background of a FullWidth node.
static void ui_TreechartFullWidthContentBGFillDrawingDescription( SA_PARAM_NN_VALID UITreechart* chart, UIDrawingDescription* desc )
{
	UISkin* skin = UI_GET_SKIN( chart );

	if( skin->bUseStyleBorders || skin->bUseTextureAssemblies ) {
		const char* descName = skin->astrTreechartFullWidthContentBGStyle;
		if( skin->bUseTextureAssemblies && RefSystem_ReferentFromString( "UITextureAssembly", descName )) {
			desc->textureAssemblyName = descName;
		} else {
			desc->styleBorderName = descName;
		}
	} else {
		desc->styleBorderNameUsingLegacyColor = "Default_Capsule_Filled";
	}
}

/// Get the style border for highlighting a node.
///
/// This style border should be able to have its alpha tweaked.
void ui_TreechartNodeHighlightFillDrawingDescription( SA_PARAM_NN_VALID UITreechart* chart, UIDrawingDescription* desc )
{
	UISkin* skin = UI_GET_SKIN( chart );

	if( skin->bUseStyleBorders || skin->bUseTextureAssemblies ) {
		const char* descName;
		if( ui_TreechartIsDragging( chart )) {
			descName = skin->astrTreechartNodeHighlightStyle;
		} else {
			descName = skin->astrTreechartNodeSelectedStyle;
		}

		if( skin->bUseTextureAssemblies && RefSystem_ReferentFromString( "UITextureAssembly", descName )) {
			desc->textureAssemblyName = descName;
		} else {
			desc->styleBorderName = descName;
		}
	} else {
		desc->styleBorderNameUsingLegacyColor = "Default_Capsule_Filled";
	}
}

/// Get the style border for highlighting a node.
///
/// This style border should be able to have its alpha tweaked.
void ui_TreechartArrowHighlightFillDrawingDescription( SA_PARAM_NN_VALID UITreechart* chart, UIDrawingDescription* desc )
{
	UISkin* skin = UI_GET_SKIN( chart );

	if( skin->bUseStyleBorders || skin->bUseTextureAssemblies ) {
		const char* descName = skin->astrTreechartArrowHighlightStyle;

		if( skin->bUseTextureAssemblies && RefSystem_ReferentFromString( "UITextureAssembly", descName )) {
			desc->textureAssemblyName = descName;
		} else {
			desc->styleBorderName = descName;
		}
	} else {
		desc->styleBorderNameUsingLegacyColor = "Default_Capsule_Filled";
	}
}

/// Get the style border for highlighting a node as an active drop.
///
/// This style border should be able to have its alpha tweaked.
void ui_TreechartArrowHighlightActiveFillDrawingDescription( SA_PARAM_NN_VALID UITreechart* chart, UIDrawingDescription* desc )
{
	UISkin* skin = UI_GET_SKIN( chart );

	if( skin->bUseStyleBorders || skin->bUseTextureAssemblies ) {
		const char* descName = skin->astrTreechartArrowHighlightStyleActive;

		if( skin->bUseTextureAssemblies && RefSystem_ReferentFromString( "UITextureAssembly", descName )) {
			desc->textureAssemblyName = descName;
		} else {
			desc->styleBorderName = descName;
		}
	} else {
		desc->styleBorderNameUsingLegacyColor = "Default_Capsule_Filled";
	}
}

/// Get the style border for drawing an arrow.
void ui_TreechartArrowFillDrawingDescription( SA_PARAM_NN_VALID UITreechart* chart, UIDrawingDescription* desc, bool isArrowDraggable, bool isArrowheadVisible, bool isAlternate )
{
	ui_TreechartArrowFillDrawingDescriptionForSkin( UI_GET_SKIN( chart ), desc, isArrowDraggable, isArrowheadVisible, isAlternate );
}

void ui_TreechartArrowFillDrawingDescriptionForSkin( SA_PARAM_NN_VALID UISkin* skin, UIDrawingDescription* desc, bool isArrowDraggable, bool isArrowheadVisible, bool isAlternate )
{
	if( skin->bUseStyleBorders || skin->bUseTextureAssemblies ) {
		const char* descName;
		if( isArrowDraggable ) {
			descName = skin->astrTreechartArrowStyleGrabbable;
		} else if( isArrowheadVisible ) {
			if( isAlternate ) {
				descName = skin->astrTreechartArrowStyleAlternate;
			} else {
				descName = skin->astrTreechartArrowStyle;
			}
		} else {
			descName = skin->astrTreechartArrowStyleNoHead;
		}

		if( skin->bUseTextureAssemblies && RefSystem_ReferentFromString( "UITextureAssembly", descName )) {
			desc->textureAssemblyName = descName;
		} else {
			desc->styleBorderName = descName;
		}
	} else {
		desc->styleBorderNameUsingLegacyColor = "Default_Capsule_Filled";
	}
}

/// Get the style border for drawing the branching part of an arrow.
void ui_TreechartBranchArrowFillDrawingDescription( SA_PARAM_NN_VALID UITreechart* chart, UIDrawingDescription* desc )
{
	UISkin* skin = UI_GET_SKIN( chart );

	if( skin->bUseStyleBorders || skin->bUseTextureAssemblies ) {
		const char* descName = skin->astrTreechartBranchArrowStyle;

		if( skin->bUseTextureAssemblies && RefSystem_ReferentFromString( "UITextureAssembly", descName )) {
			desc->textureAssemblyName = descName;
		} else {
			desc->styleBorderName = descName;
		}
	} else {
		desc->styleBorderNameUsingLegacyColor = "Default_Capsule_Filled";
	}
}

/// Draw an arrow from START to END with the specified parameters.
void ui_TreechartArrowDraw( UITreechart* chart, UISkin* skin, Vec2 start, Vec2 end, float z, bool isArrowDraggable, bool isArrowheadVisible, bool isAlternate, bool isDragging, bool isHighlighted, float scale )
{
	UIDrawingDescription arrowDesc = { 0 };
	CBox arrowBox;
	int mouse[2];
	float arrowZ;
	float highlightZ;
	float highlightActiveZ;

	if( !skin->bTreechartArrowHighlightOnTop ) {
		highlightZ = z + 0.1;
		arrowZ = z + 0.2;
		highlightActiveZ = z + 0.3;
	} else {
		arrowZ = z + 0.1;
		highlightZ = z + 0.2;
		highlightActiveZ = z + 0.2;
	}

	ui_TreechartArrowFillDrawingDescription( chart, &arrowDesc, isArrowDraggable, isArrowheadVisible, isAlternate );

	CBoxSet( &arrowBox, end[0], start[1], end[0], end[1] );
	ui_DrawingDescriptionOuterBox( &arrowDesc, &arrowBox, scale );
	mousePos( &mouse[0], &mouse[1] );

	if( start[ 1 ] >= end[ 1 ]) {
		return;
	}

	{
		U8 alpha;
		if( isHighlighted ) {
			alpha = 255;
		} else if( isDragging ) {
			float dist;

			dist = fabs(mouse[1] - (start[ 1 ] + end[ 1 ]) / 2);

			alpha = lerp( 128, ui_TreechartMinHLValue, CLAMP( dist / ui_TreechartMaxHLDist, 0, 1 ));
		} else {
			alpha = 0;
		}

		if( alpha ) {
			UIDrawingDescription desc = { 0 };
			CBox box;
			ui_TreechartArrowHighlightFillDrawingDescription( chart, &desc );
			CBoxSet( &box, start[ 0 ] - ui_TreechartArrowAreaWidth * scale, start[ 1 ],
					 start[ 0 ] + ui_TreechartArrowAreaWidth * scale, end[ 1 ]);

			{
				Color c = ColorRed;
				c.a = alpha;
				ui_DrawingDescriptionDraw( &desc, &box, scale, highlightZ, alpha, c, ColorRed );
			}
		}
	}

	ui_DrawingDescriptionDraw( &arrowDesc, &arrowBox, scale, arrowZ, 255, ColorBlack, ColorBlack );

	if( isHighlighted ) {
		UIDrawingDescription desc = { 0 };
		CBox box;
		ui_TreechartArrowHighlightActiveFillDrawingDescription( chart, &desc );
		CBoxSet( &box, start[ 0 ] - ui_TreechartArrowAreaWidth * scale, start[ 1 ],
				 start[ 0 ] + ui_TreechartArrowAreaWidth * scale, end[ 1 ]);
		ui_DrawingDescriptionDraw( &desc, &box, scale, highlightActiveZ, 255, ColorWhite, ColorWhite );
	}
}

/// Draw an insertion plus along the arrow from START to END with the specified parameters.
void ui_TreechartInsertionPlusDraw( UISkin* skin, Vec2 start, Vec2 end, float z, float scale )
{
	AtlasTex* insertionPlus = atlasFindTexture( skin->pchTreechartInsertionPlus ? skin->pchTreechartInsertionPlus : "UGC_Icon_Plus_01" );
	float tailTop = start[ 1 ];
	float headBottom = end[ 1 ];

	int mouse[2];

	bool hover = false;
	CBox box;

	if( start[ 1 ] >= end[ 1 ]) {
		return;
	}

	if( skin->bTreechartInsertionPlusIsCentered ) {
		BuildCBox(&box,
				  end[ 0 ] - insertionPlus->width * scale / 2,
				  (start[ 1 ] + end[ 1 ] - insertionPlus->height * scale) / 2,
				  insertionPlus->width * scale,
				  insertionPlus->height * scale);
	} else {
		BuildCBox(&box,
				  end[ 0 ],
				  (start[ 1 ] + end[ 1 ] - insertionPlus->height * scale) / 2,
				  insertionPlus->width * scale,
				  insertionPlus->height * scale);
	}
	mousePos( &mouse[0], &mouse[1] );

	if (ui_TreeChartInsertionPlusCollides( skin, start, end, scale, mouse[0], mouse[1] ) ){
		hover = true;
	}

	if(hover){
		if(mouseIsDown( MS_LEFT )){
			insertionPlus = atlasFindTexture( skin->pchTreechartInsertionPlusPressed ? skin->pchTreechartInsertionPlusPressed : "UGC_Icon_Plus_01_Pressed" );
		}else{
			insertionPlus = atlasFindTexture( skin->pchTreechartInsertionPlusHover ? skin->pchTreechartInsertionPlusHover : "UGC_Icon_Plus_01_Mouseover" );
		}
	}

	//shadow
	display_sprite( insertionPlus, box.lx + 2, box.ly + 2, z + 0.1, scale, scale, 0x00000080 );
	//icon
	display_sprite( insertionPlus, box.lx, box.ly, z + 0.1,
					scale, scale, 0xFFFFFFFF );
}

void ui_TreechartArrowDrawAngled( UISkin* skin, Vec2 start, Vec2 end, float z, bool isArrowDraggable, float scale )
{
	float angle = atan2( end[ 1 ] - start[ 1 ], end[ 0 ] - start[ 0 ]);
	float len = sqrt( SQR( end[ 0 ] - start[ 0 ]) + SQR( end[ 1 ] - start[ 1 ]));
	CBox cbox;
	UIDrawingDescription arrowDesc = { 0 };

	ui_TreechartArrowFillDrawingDescriptionForSkin( skin, &arrowDesc, isArrowDraggable, true, false );

	BuildCBoxFromCenter( &cbox, (start[0] + end[0]) / 2, (start[1] + end[1]) / 2, 0, len );
	ui_DrawingDescriptionOuterBox( &arrowDesc, &cbox, scale );
	ui_DrawingDescriptionDrawRotated( &arrowDesc, &cbox, angle - RAD(90), scale, z, 255, ColorBlack, ColorBlack );
}

/// Draw an arrow from START to the mouse pos.  Used when dragging.
void ui_TreechartArrowDrawToMouse( UISkin* skin, Vec2 start, float z, bool isArrowDraggable, float scale )
{
	int mouseX;
	int mouseY;
	float angle;
	float len;

	mousePos( &mouseX, &mouseY );
	angle = atan2( mouseY - start[ 1 ], mouseX - start[ 0 ]);
	len = sqrt( SQR(mouseX - start[0]) + SQR(mouseY - start[1]) );

	// move the mouse pos a little closer to the source, so you can see the arrow head
	len = MAX( len - 4, 0 );
	mouseX = start[ 0 ] + cos( angle ) * len;
	mouseY = start[ 1 ] + sin( angle ) * len;

	{
		Vec2 end = { mouseX, mouseY };
		ui_TreechartArrowDrawAngled( skin, start, end, z, isArrowDraggable, scale );
	}
}

/// Draw an arrow from START to END with the specified parameters.
void ui_TreechartArrowDrawComplex( Vec2 start, Vec2 end, float in_len, float out_len, float center, float z, Color c, float scale )
{
	Vec2 controlPoints[ 4 ];
	F32 ease_len = 10;

	// Draw in a few parts:
	//
	// 	*START
	//	1
	//	1
	//	1
	//	1
	//	2
	//	2
	//	 2
	//	  222223333333 (y = START + in_len)
	//                3
	//                 3
	//                 3
	//                 4 (x = center)
	//                 4
	//                 4
	//                 4
	//	               5
	//	               5
	//	                5
	//	                 55556666666    (y = END - out_len)
	// 	                            6
	// 	                             6
	//                               6
	//                               7
	//                               7
	// 	   					       *END
	//
	// NOTE: If END is above START, 3, 4, and 5 go up instead of down

	if (end[ 1 ] < start[ 1 ])
		ease_len = -10;

	// Part 1:
	gfxDrawLineEx( start[ 0 ], start[ 1 ], z, start[ 0 ], start[ 1 ] + in_len-10, c, c, scale, true );

	// Part 2:
	setVec2( controlPoints[ 0 ], start[ 0 ], start[ 1 ] + in_len-10 );
	setVec2( controlPoints[ 1 ], start[ 0 ], start[ 1 ] + in_len );
	setVec2( controlPoints[ 2 ], start[ 0 ], start[ 1 ] + in_len );
	setVec2( controlPoints[ 3 ], (start[ 0 ] + center) / 2, start[ 1 ] + in_len );
	gfxDrawBezier( controlPoints, z, c, c, scale );

	// Part 3:
	setVec2( controlPoints[ 0 ], (start[ 0 ] + center) / 2, start[ 1 ] + in_len );
	setVec2( controlPoints[ 1 ], center, start[ 1 ] + in_len );
	setVec2( controlPoints[ 2 ], center, start[ 1 ] + in_len );
	setVec2( controlPoints[ 3 ], center, start[ 1 ] + in_len + ease_len );
	gfxDrawBezier( controlPoints, z, c, c, scale );

	// Part 4:
	gfxDrawLineEx( center, start[ 1 ] + in_len + ease_len, z, center, end[ 1 ] - out_len - ease_len, c, c, scale, true );

	// Part 5:
	setVec2( controlPoints[ 0 ], center, end[ 1 ] - out_len - ease_len );
	setVec2( controlPoints[ 1 ], center, end[ 1 ] - out_len );
	setVec2( controlPoints[ 2 ], center, end[ 1 ] - out_len );
	setVec2( controlPoints[ 3 ], (center + end[ 0 ]) / 2, end[ 1 ] - out_len );
	gfxDrawBezier( controlPoints, z, c, c, scale );

	// Part 6:
	setVec2( controlPoints[ 0 ], (center + end[ 0 ]) / 2, end[ 1 ] - out_len );
	setVec2( controlPoints[ 1 ], end[ 0 ], end[ 1 ] - out_len );
	setVec2( controlPoints[ 2 ], end[ 0 ], end[ 1 ] - out_len );
	setVec2( controlPoints[ 3 ], end[ 0 ], end[ 1 ] - out_len + 10 );
	gfxDrawBezier( controlPoints, z, c, c, scale );

	// Part 7:
	gfxDrawLineEx( end[ 0 ], end[ 1 ], z, end[ 0 ], end[ 1 ] - out_len + 10, c, c, scale, true );

	// And the arrowhead
	gfxDrawLineEx( end[ 0 ], end[ 1 ], z, end[ 0 ] - 5, end[ 1 ] - 5, c, c, scale, true );
	gfxDrawLineEx( end[ 0 ], end[ 1 ], z, end[ 0 ] + 5, end[ 1 ] - 5, c, c, scale, true );
}

/// Check if the position X, Y collides with the arrow from START to
/// END.
bool ui_TreechartArrowCollides( Vec2 start, Vec2 end, float scale, float x, float y )
{
	CBox box;
	CBoxSet( &box, start[ 0 ] - ui_TreechartArrowAreaWidth * scale, start[ 1 ],
			 start[ 0 ] + ui_TreechartArrowAreaWidth * scale, end[ 1 ]);

	return (box.lx <= x && x <= box.hx && box.ly <= y && y <= box.hy);
}

///Check if the position X,Y collides with the insertion plus along the arrow from START to END.
bool ui_TreeChartInsertionPlusCollides(UISkin* skin, Vec2 start, Vec2 end, float scale, float x, float y){
	AtlasTex* insertionPlus = atlasFindTexture( (skin && skin->pchTreechartInsertionPlus) ? skin->pchTreechartInsertionPlus : "UGC_Icon_Plus_01" );
	F32 left, right, bottom, top;

	if( skin->bTreechartInsertionPlusIsCentered ) {
		left = end[ 0 ] - insertionPlus->width * scale / 2;
	} else {
		left = end[ 0 ];
	}
	right = left + insertionPlus->width * scale;
	top = (start[ 1 ] + end[ 1 ] - insertionPlus->height * scale) / 2;
	bottom = top + insertionPlus->height * scale;
	
	return left < x && x < right && top < y && y < bottom;
}

///checks a tree chart for arrow drags and does them.  peaNodes should be the top node, and is
///used for recursive calls.
void ui_TreechartCheckDragDiagrams( SA_PARAM_NN_VALID UITreechart* chart, UITreechartNode*** peaNodes )
{
	float x = chart->lastDrawX;
	float y = chart->lastDrawY;
	float z = chart->lastDrawZ;
	float scale = chart->lastDrawScale * chart->scrollArea.childScale;
	int mouseX;
	int mouseY;
	int it;

	if( !chart->dragANF || ui_TreechartIsDragging( chart )) {
		return;
	}

	mousePos( &mouseX, &mouseY );

	for( it = 0; it != eaSize( peaNodes ); ++it ) {
		UITreechartNode* node = eaGet( peaNodes, it );

		if( eaSize( &node->columns ) && (chart->draggingNode != node || !(node->flags & TreeNode_ContainerUI)) ) {
			int childX, childY, childW, childH;
			ui_TreechartNodeCalcChildArea( chart, node, &childX, &childY, &childW, &childH );

			{
				int colIt;
				for( colIt = 0; colIt != eaSize( &node->columns ); ++colIt ) {
					UITreechartChildColumn* col = node->columns[ colIt ];
					UITreechartNode* colFront = ui_TreechartNextNodeNonArrow( col->nodes, -1, +1 );
					UITreechartNode* colBack = ui_TreechartNextNodeNonArrow( col->nodes, eaSize( &col->nodes ), -1 );
					Vec2 beforeColAnchors[ 2 ];
					//Vec2 afterColAnchors[ 2 ];	//this is never read?

					assert( eaSize( &col->nodes ));
					ui_TreechartNodeGetTopAnchor( beforeColAnchors[ 1 ], chart, colFront );
					setVec2( beforeColAnchors[ 0 ], beforeColAnchors[ 1 ][ 0 ], childY );

					//ui_TreechartNodeGetBottomAnchor( afterColAnchors[ 0 ], chart, colBack );
					//setVec2( afterColAnchors[ 1 ], afterColAnchors[ 0 ][ 0 ], childY + childH - ui_TreechartNodeBottomHeight( chart, node ));

					//recurse on the nodes in this column:
					ui_TreechartCheckDragDiagrams( chart, &node->columns[ colIt ]->nodes );


					if(   (colFront->flags & TreeNode_DragArrowBefore)
						&& ui_TreechartArrowCollides( beforeColAnchors[ 0 ], beforeColAnchors[ 1 ], scale, mouseX, mouseY )) {
							ui_TreechartArrowDrag( chart, NULL, colFront );
					}
				}
			}
		}
		//check the nodes:
		if( !(node->flags & TreeNode_ArrowNode) ) {
			UITreechartNode* afterNode = ui_TreechartNextNodeNonArrow( *peaNodes, it, +1 );
			UITreechartNode* immediatelyAfterNode = eaGet( peaNodes, it + 1 );
			Vec2 afterAnchors[ 2 ];

			ui_TreechartNodeGetBottomAnchor( afterAnchors[ 0 ], chart, node );
			if( afterNode ) {
				ui_TreechartNodeGetTopAnchor( afterAnchors[ 1 ], chart, afterNode );
			} else {
				copyVec2( afterAnchors [ 0 ], afterAnchors[ 1 ]);
				afterAnchors[ 1 ][ 1 ] += 15;
			}

			if(   afterNode && (afterNode->flags & TreeNode_DragArrowBefore)
				&& ui_TreechartArrowCollides( afterAnchors[ 0 ], afterAnchors[ 1 ], scale, mouseX, mouseY )) {
					// Can't use afterNode, because that node may not be
					// the one immediately after
					ui_TreechartArrowDrag( chart, node, immediatelyAfterNode );
			}
		}
	}
}

///checks a tree chart for insertion plus clicks and does them.
/// peaNodes should be the top node, and is used for recursive calls.
bool ui_TreeChartCheckInsertionPlusClicks(UITreechart* chart, UITreechartNode*** peaNodes){
	UISkin* skin = UI_GET_SKIN( chart );
	float x = chart->lastDrawX;
	float y = chart->lastDrawY;
	float z = chart->lastDrawZ;
	float scale = chart->lastDrawScale * chart->scrollArea.childScale;
	int mouseX, mouseY;
	int it, colIt;
	Vec2 arrow[ 2 ];

	mousePos( &mouseX, &mouseY );

	if (peaNodes == NULL){
		peaNodes = &(chart->treeNodes);
	}

	//iterate through all nodes (if any)
	for( it = 0; it != eaSize( peaNodes ); ++it){
		UITreechartNode* node = eaGet( peaNodes, it);
		int childY = y + (node->widget->y + node->widget->height) * scale;
		int childH = node->columnTotalHeight * scale;

		//iterate through columns for each node (if any)
		for( colIt = 0; colIt != eaSize( &node->columns ); ++colIt ) {
			UITreechartChildColumn* col = node->columns[ colIt ];
			UITreechartNode* firstInCol = ui_TreechartNextNodeNonArrow( col->nodes, -1, +1 );
			UITreechartNode* lastInCol = ui_TreechartNextNodeNonArrow( col->nodes, eaSize(&col->nodes), -1 );

			//recurse on each colu`n:
			if (ui_TreeChartCheckInsertionPlusClicks( chart, &node->columns[ colIt ]->nodes )){
				return true;	//it should be impossible to click on more than one at once.
			}


			//check plus at top of column:

			assert( eaSize( &col->nodes ));
			ui_TreechartNodeGetTopAnchor( arrow[1], chart, firstInCol );
			copyVec2( arrow[1], arrow[0] );
			arrow[0][1] -= skin->iTreechartArrowHeight;
			if( ui_TreeChartInsertionPlusCollides( UI_GET_SKIN(chart), arrow[ 0 ], arrow[ 1 ], scale, mouseX, mouseY )) {
				if( mouseClick( MS_LEFT )) {
					chart->clickInsertF( chart, chart->cbData, true, NULL, SAFE_MEMBER(firstInCol, data));
				}
				return true;
			}

			if( lastInCol->flags & TreeNode_ContainerUI ) {
				//check plus at bottom of column:
				ui_TreechartNodeGetBottomAnchor( arrow[0], chart, lastInCol );
				copyVec2( arrow[0], arrow[1] );
				arrow[1][1] += skin->iTreechartArrowHeight;
				if( ui_TreeChartInsertionPlusCollides( UI_GET_SKIN(chart), arrow[ 0 ], arrow[ 1 ], scale,mouseX, mouseY )) {
					if( mouseClick( MS_LEFT )) {
						chart->clickInsertF( chart, chart->cbData, true, SAFE_MEMBER(lastInCol, data), NULL );
					}
					return true;
				}
			}

		}
		//check the nodes:
		if( !(node->flags & TreeNode_ArrowNode) ) {
			UITreechartNode* afterNode = ui_TreechartNextNodeNonArrow( *peaNodes, it, +1 );
			//UITreechartNode* immediatelyAfterNode = eaGet( peaNodes, it + 1 );
			Vec2 afterAnchors[ 2 ];

			ui_TreechartNodeGetBottomAnchor( afterAnchors[ 0 ], chart, node );
			if( afterNode ) {
				ui_TreechartNodeGetTopAnchor( afterAnchors[ 1 ], chart, afterNode );
			} else {
				copyVec2( afterAnchors [ 0 ], afterAnchors[ 1 ]);
				afterAnchors[ 1 ][ 1 ] += 15;
			}

			if( afterNode && ui_TreeChartInsertionPlusCollides( UI_GET_SKIN(chart), afterAnchors[ 0 ],
																afterAnchors[ 1 ], scale, mouseX, mouseY )) {
				// Can't use afterNode, because that node may not be
				// the one immediately after
				if( mouseClick( MS_LEFT )) {
					chart->clickInsertF( chart, chart->cbData, true, SAFE_MEMBER(node, data), SAFE_MEMBER(afterNode, data) );
				}
				return true;
			}
		}
	}
	return false;
}
