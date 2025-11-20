/// A variant of UIFlowchart that assumes the data is roughly tree
/// structured.  The assumption of a rough tree structure makes the
/// widget able to be automatically laid out and have a nice DnD
/// interface.
#pragma once

#include"CBox.h"
#include"UICore.h"
#include"UIScrollbar.h"
#include"stdtypes.h"

typedef struct UIButton UIButton;
typedef struct UITreechart UITreechart;
typedef struct UITreechartChildColumn UITreechartChildColumn;
typedef struct UITreechartNode UITreechartNode;

typedef void (*UITreechartDragNodeTrashFunc)( UITreechart* chart, UserData userData, UserData nodeData );
typedef bool (*UITreechartDragNodeNodeFunc)( UITreechart* chart, UserData userData, bool isCommit, UserData srcData, UserData destData );
typedef bool (*UITreechartDragNodeArrowFunc)( UITreechart* chart, UserData userData, bool isCommit, UserData srcData, UserData destBeforeData, UserData destAfterData );
typedef bool (*UITreechartDragArrowNodeFunc)( UITreechart* chart, UserData userData, bool isCommit, UserData srcBeforeData, UserData srcAfterData, UserData destData );
typedef void (*UITreechartNodeAnimateFunc)( UITreechart* chart, UserData userData, UserData nodeData, float x, float y );
typedef void (*UITreechartDrawDragNodeNodeFunc)( UITreechart* chart, UserData userData, CBox *box, float z, float scale, UserData srcData, UserData destData );
typedef bool (*UITreechartClickInsertionPlusFunc)(UITreechart* chart, UserData userData, bool isCommit, UserData prevNode, UserData nextNode);
typedef bool (*UITreechartDragNodeNodeColumnFunc)(UITreechart* chart, UserData userData, bool isCommit, UserData srcData, UserData destData, int column );
// TODO: add DragArrowArrow functions?  How about DrawArrow/NodeInsert functions?

typedef enum UITreechartNodeFlags
{
	TreeNode_Normal				  = 0,
	TreeNode_NoSelect			  = 1 << 0,
	TreeNode_NoDrag				  = 1 << 1,
	TreeNode_ArrowNode			  = 1 << 2,
	TreeNode_DragArrowBefore	  = 1 << 3,
	TreeNode_NoArrowBefore		  = 1 << 4,
	TreeNode_NoArrowAfter		  = 1 << 5,
	TreeNode_AlternateArrowBefore = 1 << 6,
	TreeNode_ContainerUI		  = 1 << 7,
	TreeNode_FullWidthContainerUI = 1 << 8,
	TreeNode_FullWidthDropTarget  = 1 << 9,
	TreeNode_BranchArrowUI		  = 1 << 10,

	// Deprecated flags -- try to avoid using these.
	TreeNode_Stationary	          = 1 << 15,	//< roughly equivalent to: NoSelect | NoDrop
} UITreechartNodeFlags;

/// A single node in the chart.
typedef struct UITreechartNode
{
	UIWidget* widget;
	UserData data;
	UITreechartNodeFlags flags;
	const char* iconName;
	UIWidget* leftWidget;

	UITreechartChildColumn** columns;
	int columnTotalWidth;
	int columnTotalHeight;

	// DnD support
	UILoopFunction widgetBackupDrawF;

	// Animation support
	Vec2 targetPos;
} UITreechartNode;

/// An extra arrow to draw
typedef struct UITreechartQueuedArrow
{
	Vec2 start;
	Vec2 end;
	bool isArrowDraggable;
	float scale;
} UITreechartQueuedArrow;

/// The base widget.  When you pass a widget to the chart, it takes
/// ownership.
typedef struct UITreechart
{
	UI_INHERIT_FROM(UI_WIDGET_TYPE UI_SCROLLAREA_TYPE);

	UITreechartNode** treeNodes;

	// Should only be set between calls to ui_TreechartBeginRefresh
	// and ui_TreechartEndRefresh.  Will have all nodes previously in
	// the treechart.
	UIWidget** eaTreeWidgetsNotRefreshed;

	// Trash button
	UIButton* trashButton;

	// DnD data
	float lastDrawX;
	float lastDrawY;
	float lastDrawZ;
	CBox lastDrawPixelsRect;
	float lastDrawScale;
	CBox lastDrawSelectedPixelsRect;

	UITreechartNode* selectedNode;
	bool selectedNodeMakeVisible;	
	UITreechartNode* draggingNode;
	UITreechartNode externalDragNode;

	UITreechartNode* beforeDraggingNode;
	UITreechartNode* afterDraggingNode;

	UserData cbData;
	UITreechartDragNodeNodeFunc dragNNF;
	UITreechartDragNodeArrowFunc dragNAF;
	UITreechartDragArrowNodeFunc dragANF;
	UITreechartNodeAnimateFunc nodeAnimateF;
	UITreechartDragNodeTrashFunc nodeTrashF;
	UITreechartDrawDragNodeNodeFunc drawDragNNF;
	UITreechartClickInsertionPlusFunc clickInsertF;
	UITreechartDragNodeNodeColumnFunc dragNNColumnF;

	// For queued arrows
	UITreechartQueuedArrow** queuedArrows;

	// The treechart calls ui_SetFocus, which could cause some local
	// pointers to be stale.  This helps catch that logic. 
	bool disallowTreechartModification : 1;

	// If set, this should set up a clip rect in preparation for
	// having full width nodes
	unsigned clipForFullWidth : 1;
} UITreechart;

typedef struct UITreechartChildColumn
{
	UITreechartNode** nodes;

	int columnWidth;
	int columnHeight;
} UITreechartChildColumn;

SA_RET_NN_VALID UITreechart* ui_TreechartCreate( UserData cbData, UITreechartDragNodeNodeFunc dragNNF,
												 UITreechartDragNodeArrowFunc dragNAF, UITreechartDragArrowNodeFunc dragANF,
												 UITreechartNodeAnimateFunc nodeAnimateF, UITreechartDragNodeTrashFunc nodeTrashF,
												 UITreechartDrawDragNodeNodeFunc drawDragNNF, UITreechartClickInsertionPlusFunc clickInsertF,
												 UITreechartDragNodeNodeColumnFunc dragNNColumnF );
void ui_TreechartFree( SA_PRE_NN_VALID SA_POST_P_FREE UITreechart* chart );
void ui_TreechartTick( SA_PARAM_NN_VALID UITreechart* chart, UI_PARENT_ARGS );
void ui_TreechartDraw( SA_PARAM_NN_VALID UITreechart* chart, UI_PARENT_ARGS );

// Two ways to refresh:
//
// Old way: Call ui_TreechartClear(), then add all the widgets.
// New way: Call ui_TreechartBeginRefresh(), add all the widgets, then call ui_TreechartEndRefresh().
void ui_TreechartClear( SA_PARAM_NN_VALID UITreechart* chart );
void ui_TreechartBeginRefresh( SA_PARAM_NN_VALID UITreechart* chart );
void ui_TreechartEndRefresh( SA_PARAM_NN_VALID UITreechart* chart );

void ui_TreechartDrawExtraArrowAngled( SA_PARAM_NN_VALID UITreechart* chart, Vec2 start, Vec2 end, bool isArrowDraggable, float scale );

bool ui_TreechartIsDragging( SA_PARAM_NN_VALID UITreechart* chart );
void ui_TreechartSetExternalDrag( SA_PARAM_NN_VALID UITreechart* chart, SA_PARAM_NN_STR const char* iconName, UserData data );
void ui_TreechartSetSelectedChild( SA_PARAM_NN_VALID UITreechart* chart, SA_PARAM_OP_VALID UIWidget* child, bool makeVisible );
UIWidget* ui_TreechartGetSelectedChild( SA_PARAM_NN_VALID UITreechart* chart );
void ui_TreechartSetClipForFullWidth( SA_PARAM_NN_VALID UITreechart* chart, bool bValue );
void ui_TreechartSetLeftWidget( SA_PARAM_NN_VALID UITreechart* chart, SA_PARAM_OP_VALID UIWidget* child, UIWidget* leftWidget );

void ui_TreechartAddWidget( SA_PARAM_NN_VALID UITreechart* chart, SA_PARAM_OP_VALID UIWidget* beforeWidget, SA_PARAM_NN_VALID UIWidget* widget, SA_PARAM_OP_STR const char* iconName, UserData data, UITreechartNodeFlags flags );
void ui_TreechartAddChildWidget( SA_PARAM_NN_VALID UITreechart* chart,SA_PARAM_NN_VALID UIWidget* parentWidget, SA_PARAM_NN_VALID UIWidget* childWidget, SA_PARAM_OP_STR const char* iconName, UserData data, UITreechartNodeFlags flags );

void ui_TreechartArrowDraw( UITreechart* chart, UISkin* skin, Vec2 start, Vec2 end, float z, bool isArrowDraggable, bool isArrowheadVisible, bool isAlternate, bool isDragging, bool isHighlighted, float scale );
void ui_TreechartInsertionPlusDraw( UISkin* skin, Vec2 start, Vec2 end, float z, float scale );
void ui_TreechartArrowDrawAngled( UISkin* skin, Vec2 start, Vec2 end, float z, bool isArrowDraggable, float scale );
void ui_TreechartArrowDrawComplex( Vec2 start, Vec2 end, float in_len, float out_len, float center, float z, Color c, float scale );

// querying
void ui_TreechartGroupBottomFillDrawingDescription( SA_PARAM_NN_VALID UITreechart* chart, UIDrawingDescription* desc );
extern int ui_TreechartNodeDefaultBottomHeight;
extern int ui_TreechartFullWidthContainerWidth;
