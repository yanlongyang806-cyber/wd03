/***************************************************************************



***************************************************************************/

#ifndef UI_GRAPHNODE_H
#define UI_GRAPHNODE_H
GCC_SYSTEM

#include "UIWindow.h"


typedef void (*UIGraphNodeOnMouseDownFunc)(UIAnyWidget *pWidget, Vec2 clickPoint, void *userData);
typedef void (*UIGraphNodeOnMouseDragFunc)(UIAnyWidget *pWidget, Vec2 clickPoint, void *userData);
typedef void (*UIGraphNodeOnMouseUpFunc)(UIAnyWidget *pWidget, Vec2 clickPoint, void *userData);

//////////////////////////////////////////////////////////////////////////
//
// Specialized window object for representing nodes in a graph
//
//////////////////////////////////////////////////////////////////////////
typedef struct UIGraphNode
{
	UI_INHERIT_FROM( UI_WIDGET_TYPE UI_WINDOW_TYPE );

	UIInputFunction fnOnInput; // input callback
	void *onInputUserData; // input user data

	UIGraphNodeOnMouseDownFunc fnOnMouseDown; // mouse down callback (only called once per click)
	void *onMouseDownUserData; // mouse down user data

	UIGraphNodeOnMouseDragFunc fnOnMouseDrag; // mouse dragging title bar callback
	void *onMouseDragUserData; // mouse drag user data

	UIGraphNodeOnMouseUpFunc fnOnMouseUp; // mouse up callback (only called once)
	void *onMouseUpUserData; // mouse up user data

	void *userData; // userdata for the node

	F32 topPad; // top-padding
	F32 pScale; //  UI scale factor

	UISkin *pSelSkin; // selected skin (only contains a reference)
	UISkin *pNoSelSkin; // not selected skin (only contains a reference)

	U8 bIsSelected : 1; // for drawing state
	U8 bLeftMouseDown : 1; // left mouse is down (on this widget's title bar)
	U8 bLeftMouseDoubleClicked : 1; // left mouse is double clicked (on this widget's title bar)
	U8 bIsMiddleMouseDown : 1; // is middle mouse down (on widget's title bar)
} UIGraphNode;


// Create a new graph node window, with the given size. If NULL is given as the title,
// the window will not have a title bar (and cannot be moved).
UIGraphNode *ui_GraphNodeCreate(const char *title, F32 x, F32 y, F32 w, F32 h, UISkin *pSelSkin, UISkin *pNoSelSkin);
void ui_GraphNodeInitialize(UIGraphNode *pGraphNode, const char *title, F32 x, F32 y, F32 w, F32 h);
void ui_GraphNodeFreeInternal(UIGraphNode *pUIGraphNode);

void ui_GraphNodeSetSelected(UIGraphNode *pUIGraphNode, bool bIsSelected);

void ui_GraphNodeSetOnInputCallback(UIGraphNode *pGraphNode, UIInputFunction fnInput, void *userData);
void ui_GraphNodeSetOnMouseDownCallback(UIGraphNode *pGraphNode, UIGraphNodeOnMouseDownFunc fnInput, void *userData);
void ui_GraphNodeSetOnMouseDragCallback(UIGraphNode *pGraphNode, UIGraphNodeOnMouseDragFunc fnInput, void *userData);
void ui_GraphNodeSetOnMouseUpCallback(UIGraphNode *pGraphNode, UIGraphNodeOnMouseUpFunc fnInput, void *userData);

void ui_GraphNodeClose(UIGraphNode *pGraphNode);

#endif
