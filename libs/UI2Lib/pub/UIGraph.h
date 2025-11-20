#ifndef UI_GRAPH_H
#define UI_GRAPH_H
GCC_SYSTEM

#include "UICore.h"
#include "UIPane.h"

typedef struct UITextEntry UITextEntry;
typedef struct UISpinner UISpinner;
typedef struct UILabel UILabel;

//////////////////////////////////////////////////////////////////////////
// A UIGraph is a widget that displays a simple 2D geometric graph. It
// supports displaying linear interpolations between points, various
// visualization options (axis labels, endpoint labeling, scale ticks).
// The points also support a symmetrical "margin", which can be used e.g.
// as an error bar.

typedef struct UIGraphPoint
{
	Vec2 v2Position;
	Vec2 v2DraggingDiff; // for dragging multiple selected points
	F32 fMargin;
	U32 bSelected : 1;
} UIGraphPoint;

SA_RET_NN_VALID UIGraphPoint *ui_GraphPointCreate(Vec2 v2Point, F32 fMargin);
void ui_GraphPointFree(SA_PRE_NN_VALID SA_POST_P_FREE UIGraphPoint *pPoint);

typedef struct UIGraph
{
	UI_INHERIT_FROM(UI_WIDGET_TYPE);

	UIGraphPoint **eaPoints;

	char *pchLabelX;
	char *pchLabelY;

	Vec2 v2LowerBound;
	Vec2 v2UpperBound;

	bool bDrawScale : 1;
	bool bDrawConnection : 1;
	bool bConnectToEnd : 1;
	bool bMargins : 1;

	// If true, the first point in the graph is locked to the initial X.
	bool bLockToX0 : 1;

	// If true, points cannot be dragged past neighboring points.
	bool bLockToIndex : 1;

	// If true, this forces the points to be sorted from lowest to highest X.
	bool bSort : 1;

	// Draw this many ticks along the axis. This does not include the origin,
	// but does include the upper bound.
	U8 chResolutionX;
	U8 chResolutionY;

	// The maximum number of points that can be in this graph. The user
	// cannot create more than this, or it will just move them.
	U8 chMaxPoints;

	// The minimum number of points.
	U8 chMinPoints;

	// Called as the user is moving a point.
	UIActivationFunc cbDragging;
	UserData pDraggingData;

	// Called when a point is added, removed, or done moving.
	UIActivationFunc cbChanged;
	UserData pChangedData;

	// Internal widget state; when dragging a point this is set.
	U8 bDragging : 1;
	U8 bDraggingMargin : 1;
	U8 chDraggingPoint;

} UIGraph;

SA_RET_NN_VALID UIGraph *ui_GraphCreate(SA_PARAM_OP_STR const char *pchLabelX, SA_PARAM_OP_STR const char *pchLabelY, const Vec2 v2Lower, const Vec2 v2Upper, U8 chMaxPoints, bool bMargins);
void ui_GraphFreeInternal(SA_PRE_NN_VALID SA_POST_P_FREE UIGraph *pGraph);
void ui_GraphTick(SA_PARAM_NN_VALID UIGraph *pGraph, UI_PARENT_ARGS);
void ui_GraphDraw(SA_PARAM_NN_VALID UIGraph *pGraph, UI_PARENT_ARGS);

SA_RET_OP_VALID UIGraphPoint *ui_GraphGetPoint(SA_PARAM_NN_VALID UIGraph *pGraph, S32 iPoint);

void ui_GraphAddPoint(SA_PARAM_NN_VALID UIGraph *pGraph, SA_PARAM_NN_VALID UIGraphPoint *pPoint);
void ui_GraphAddPointAndCallback(SA_PARAM_NN_VALID UIGraph *pGraph, SA_PARAM_NN_VALID UIGraphPoint *pPoint);

void ui_GraphRemovePoint(SA_PARAM_NN_VALID UIGraph *pGraph, SA_PARAM_NN_VALID UIGraphPoint *pPoint);
void ui_GraphRemovePointAndCallback(SA_PARAM_NN_VALID UIGraph *pGraph, SA_PARAM_NN_VALID UIGraphPoint *pPoint);

void ui_GraphRemovePointIndex(SA_PARAM_NN_VALID UIGraph *pGraph, S32 iPoint);
void ui_GraphRemovePointIndexAndCallback(SA_PARAM_NN_VALID UIGraph *pGraph, S32 iPoint);

void ui_GraphMovePoint(SA_PARAM_NN_VALID UIGraph *pGraph, SA_PARAM_NN_VALID UIGraphPoint *pPoint, const Vec2 v2Position, F32 fMargin);
void ui_GraphMovePointAndCallback(SA_PARAM_NN_VALID UIGraph *pGraph, SA_PARAM_NN_VALID UIGraphPoint *pPoint, const Vec2 v2Position, F32 fMargin);
void ui_GraphMovePointIndex(SA_PARAM_NN_VALID UIGraph *pGraph, S32 iPoint, const Vec2 v2Position, F32 fMargin);
void ui_GraphMovePointIndexAndCallback(SA_PARAM_NN_VALID UIGraph *pGraph, S32 iPoint, const Vec2 v2Position, F32 fMargin);

void ui_GraphSetChangedCallback(SA_PARAM_NN_VALID UIGraph *pGraph, UIActivationFunc cbChanged, UserData pChangedData);
void ui_GraphSetDraggingCallback(SA_PARAM_NN_VALID UIGraph *pGraph, UIActivationFunc cbDragging, UserData pDraggingData);

void ui_GraphSetLockToX0(SA_PARAM_NN_VALID UIGraph *pGraph, bool bLockToX0);
void ui_GraphSetLockToIndex(SA_PARAM_NN_VALID UIGraph *pGraph, bool bLockToIndex);
void ui_GraphSetMinPoints(SA_PARAM_NN_VALID UIGraph *pGraph, U8 chMinPoints);
void ui_GraphSetMaxPoints(SA_PARAM_NN_VALID UIGraph *pGraph, U8 chMaxPoints);
void ui_GraphSetResolution(SA_PARAM_NN_VALID UIGraph *pGraph, U8 chResolutionX, U8 chResolutionY);
void ui_GraphSetBounds(SA_PARAM_NN_VALID UIGraph *pGraph, const Vec2 v2Lower, const Vec2 v2Upper);

void ui_GraphEnforceBounds(SA_PARAM_NN_VALID UIGraph *pGraph);
void ui_GraphEnforceBoundsAndCallback(SA_PARAM_NN_VALID UIGraph *pGraph);

void ui_GraphSetSort(SA_PARAM_NN_VALID UIGraph *pGraph, bool bSort);
void ui_GraphSetDrawConnection(SA_PARAM_NN_VALID UIGraph *pGraph, bool bDrawConnection, bool bConnectToEnd);
void ui_GraphSetMargins(SA_PARAM_NN_VALID UIGraph *pGraph, bool bMargins);
void ui_GraphSetDrawScale(SA_PARAM_NN_VALID UIGraph *pGraph, bool bDrawScale);

void ui_GraphSetLabels(SA_PARAM_NN_VALID UIGraph *pGraph, SA_PARAM_OP_STR const char *pchLabelX, SA_PARAM_OP_STR const char *pchLabelY);

void ui_GraphSort(SA_PARAM_NN_VALID UIGraph *pGraph);
void ui_GraphSortAndCallback(SA_PARAM_NN_VALID UIGraph *pGraph);

S32 ui_GraphGetPointCount(SA_PARAM_NN_VALID UIGraph *pGraph);

U32 ui_GraphGetSelectedPointBitField(SA_PARAM_NN_VALID UIGraph* pGraph);
void ui_GraphSetSelectedPointBitField(SA_PARAM_NN_VALID UIGraph* pGraph, U32 uiSelectedPoints);

//////////////////////////////////////////////////////////////////////////
// A graph pane is a pane that contains a graph, along with text entries
// to edit its values, and spinners to adjust its scale.

typedef struct UIGraphPane
{
	UI_INHERIT_FROM(UI_PANE_TYPE UI_WIDGET_TYPE);

	UIGraph *pGraph;

	bool bXEntries;
	bool bYEntries;

	UILabel		*pXLabel;
	UILabel		*pYLabel;
	UILabel		*pMarginLabel;

	UITextEntry **eaXEntries;
	UITextEntry **eaYEntries;
	UITextEntry **eaMarginEntries;

	UITextEntry *pXLowerBoundEntry;
	UISpinner *pXLowerBound;
	UITextEntry *pYLowerBoundEntry;
	UISpinner *pYLowerBound;
	UITextEntry *pXUpperBoundEntry;
	UISpinner *pXUpperBound;
	UITextEntry *pYUpperBoundEntry;
	UISpinner *pYUpperBound;

	UIActivationFunc cbChanged;
	UserData pChangedData;

	// Called when the graph is being dragged, or when the user is still
	// actively typing in a text entry.
	UIActivationFunc cbDragging;
	UserData pDraggingData;

} UIGraphPane;

UIGraphPane *ui_GraphPaneCreate(SA_PARAM_OP_STR const char *pchLabelX, SA_PARAM_OP_STR const char *pchLabelY, const Vec2 v2Lower, const Vec2 v2Upper, U8 chMaxPoints, bool bMargins, bool bXEntries, bool bYEntries);
void ui_GraphPaneInitialize(SA_PRE_NN_FREE SA_POST_NN_VALID UIGraphPane *pGraphPane, const char *pchLabelX, const char *pchLabelY, const Vec2 v2Lower, const Vec2 v2Upper, U8 chMaxPoints, bool bMargins, bool bXEntries, bool bYEntries);
void ui_GraphPaneFreeInternal(SA_PRE_NN_VALID SA_POST_P_FREE UIGraphPane *pGraphPane);
void ui_GraphPaneTick(SA_PARAM_NN_VALID UIGraphPane *pGraphPane, UI_PARENT_ARGS);

// Set the bounds for the graph. If v2MinMin is not equal to v2MaxMin, then
// spinners will be displayed to adjust the scale on the allowed axes.
// Similarly for v2MinMax and v2MaxMax.
void ui_GraphPaneSetBounds(SA_PARAM_NN_VALID UIGraphPane *pGraphPane, const Vec2 v2MinMin, const Vec2 v2MaxMin, const Vec2 v2MinMax, const Vec2 v2MaxMax);

UIGraph *ui_GraphPaneGetGraph(SA_PARAM_NN_VALID UIGraphPane *pGraphPane);

void ui_GraphPaneSetChangedCallback(SA_PARAM_NN_VALID UIGraphPane *pGraphPane, UIActivationFunc cbChanged, UserData pChangedData);
void ui_GraphPaneSetDraggingCallback(SA_PARAM_NN_VALID UIGraphPane *pGraphPane, UIActivationFunc cbDragging, UserData pDraggingData);

// Set whether or not to display entries. Margin entries are displayed if
// Y entries are displayed and margins are enabled on the graph.
void ui_GraphPaneSetEntries(SA_PARAM_NN_VALID UIGraphPane *pGraphPane, bool bXEntries, bool bYEntries);

// Callbacks on the widgets in the pane.
void ui_GraphPaneGraphResetTextEntries(UIGraph *pGraph, UIGraphPane *pGraphPane);
void ui_GraphPaneGraphDraggingCallback(SA_PARAM_NN_VALID UIGraph *pGraph, SA_PARAM_NN_VALID UIGraphPane *pGraphPane);
void ui_GraphPaneGraphChangedCallback(SA_PARAM_NN_VALID UIGraph *pGraph, SA_PARAM_NN_VALID UIGraphPane *pGraphPane);
void ui_GraphPaneEntryChangedCallback(SA_PARAM_NN_VALID UITextEntry *pEntry, SA_PARAM_NN_VALID UIGraphPane *pGraphPane);
void ui_GraphPaneEntryFinishedCallback(SA_PARAM_NN_VALID UITextEntry *pEntry, SA_PARAM_NN_VALID UIGraphPane *pGraphPane);
void ui_GraphPaneSpinnerCallback(SA_PARAM_NN_VALID UISpinner *pSpinner, SA_PARAM_NN_VALID UIGraphPane *pGraphPane);

#endif