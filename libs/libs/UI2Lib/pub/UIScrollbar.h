/***************************************************************************



***************************************************************************/

#ifndef UI_SCROLLBAR_H
#define UI_SCROLLBAR_H
GCC_SYSTEM

#include "UICore.h"
#include "inputLibEnums.h"

typedef struct UISlider UISlider;
typedef struct UISprite UISprite;

//////////////////////////////////////////////////////////////////////////
// Horizontal and vertical scrollbars. These are not really widgets; other
// widgets embed them and control them when necessary.

// What kind of dragging, if any, is going on.
typedef enum UIScrollDrag
{
	UIScrollNone,
	UIScrollHorizontal,
	UIScrollVertical,
} UIScrollDrag;

// There are a few options for clamping the scroll position (i.e. determining min and max allowed scroll). Each leads to slightly different behavior.
typedef enum UIScrollBounds
{
	// Originally implemented and default bounds constraint. If content size is smaller than view size, clamps at 0 (i.e. never allows scroll position to go negative).
	// However, scrolling out, then in, can loose center of focus in contents.
	//
	// This is the best one for scroll areas consisting of other UI widgets in its content area (e.g. lists).
	UIScrollBounds_KeepContentsInView,

	// This bounds constraint allows the contents to go fully outside the view area, but no further. This makes sure that scrolling out, then in, maintains the center of focus
	// in the contents.
	//
	// It is possible to end up not drawing any child contents at the scroll extents.
	UIScrollBounds_AllowContentsOutOfView,

	// This clamp always maintains that the view center intersects the contents. Like the one above, this makes sure that scrolling out, then in, maintains the
	// center of focus in virtual contents. However, it makes it impossible to loose the contents out of view. This is possibly the best implementation for
	// editing maps and tree charts, where zooming in and out a lot is necessary.
	UIScrollBounds_KeepContentsAtViewCenter,
} UIScrollBounds;

typedef struct UIScrollbar
{
	// Whether to show the scrollbars for those axes.
	bool scrollX : 1;
	bool scrollY : 1;
	bool alwaysScrollX : 1;
	bool alwaysScrollY : 1;
	bool disableScrollWheel : 1;
	unsigned needCtrlToScroll : 1;

	UIScrollBounds scrollBoundsX;
	UIScrollBounds scrollBoundsY;

	F32 xpos;
	F32 ypos; // the offset relative to the last xSize/ySize passed in
	F32 dragOffset; // where the bar was grabbed.

	// Button pressed state
	unsigned pressedHorizontalLeftButton : 1;
	unsigned pressedHorizontalHandle : 1;
	unsigned pressedHorizontalRightButton : 1;
	unsigned pressedVerticalUpButton : 1;
	unsigned pressedVerticalHandle : 1;
	unsigned pressedVerticalDownButton : 1;
	unsigned hoverHorizontalLeftButton : 1;
	unsigned hoverHorizontalHandle : 1;
	unsigned hoverHorizontalRightButton : 1;
	unsigned hoverVerticalUpButton : 1;
	unsigned hoverVerticalHandle : 1;
	unsigned hoverVerticalDownButton : 1;
	unsigned disabledHorizontal : 1;
	unsigned disabledVertical : 1;

	REF_TO(UISkin) hOverrideSkin;
	UISkin* pOverrideSkin;
	Color color[3]; // 0 = buttons, 1 = trough, 2 = handle
} UIScrollbar;

// These are called by the widget attaching the scrollbar, in its tick/draw loop. 
void ui_ScrollbarTick(SA_PARAM_NN_VALID UIScrollbar *sb, F32 x, F32 y, F32 w, F32 h, F32 z, F32 scale, F32 xSize, F32 ySize);
void ui_ScrollbarDraw(SA_PARAM_NN_VALID UIScrollbar *sb, F32 x, F32 y, F32 w, F32 h, F32 z, F32 scale, F32 xSize, F32 ySize);

// Called when the mouse wheel is scrolled, even if the scrollbar is not updated
// (if it's already at the bottom, or a modifier key is held down, for example).

// The attached widget is basically responsible for the scrollbar's allocation and freeing.
SA_RET_NN_VALID UIScrollbar *ui_ScrollbarCreate(bool scrollX, bool scrollY);
void ui_ScrollbarFree(SA_PRE_NN_VALID SA_POST_P_FREE UIScrollbar *sb);

// The width/height of the scrollbar.
F32 ui_ScrollbarWidth(UIScrollbar *sb);
F32 ui_ScrollbarHeight(UIScrollbar *sb);

// Functions used inside ticks to push/pop the scrollbar state, for focus change
void ui_ScrollbarPushState(UIScrollbar *pBar, F32 x, F32 y, F32 w, F32 h, F32 scale, F32 xSize, F32 ySize);
void ui_ScrollbarPopState(void);

// Using the scrollbar state, sets the exact scroll position
void ui_ScrollbarParentSetScrollPos(F32 xPos, F32 yPos);

// Using the scrollbar state, attempt to change it so the specified point is visible
void ui_ScrollbarParentScrollTo(F32 x, F32 y);

//////////////////////////////////////////////////////////////////////////
// A specifically-sized area with scrollbars. Use this if you need to make
// a widget that doesn't normally have a scrollbar, have a scrollbar.

#define UI_SCROLLAREA_TYPE UIScrollArea scrollArea;
#define UI_SCROLLAREA(widget) (&widget->scrollArea)

typedef struct UIScrollArea
{
	UIWidget widget;

	// Settings
	int scrollPadding;						// Padding to add to the total size of the content
	unsigned autosize : 1;					// Automatically resize content size to fit child widgets
	unsigned draggable : 1;					// Ctrl + Left-click or Mid-click only to pan
	unsigned enableDragOnLeftClick : 1;		// Left-click only to pan
	unsigned enableAutoEdgePan : 1;			// Automatically pan the view when dragging near the edges
	unsigned forceAutoEdgePan : 1;			// If enableAutoEdgePan is set, assume we are dragging now
	unsigned autoScrollCenter : 1;			// When scrolling to target, scroll until target is in the middle of the visible area
	F32 defaultZoomRate;					// Ratio to zoom every frame. Default = 1.15
	F32 maxZoomScale;						// If set, clamp to this maximum zoom. Defaults to x4
	F32 minZoomScale;						// If set, clamp to this minimum zoom and disregard dynamic zoom range

	// State
	F32 xSize, ySize;						// Dimensions of the inner scroll area
	F32 childScale;							// Current scale (zoom level) applied to child widgets
	bool dragging;							// Set if we're currently panning the area
	MouseButton dragbutton;					// Set while dragging, indicating which mouse button initiated the drag
	S32 scrollToTargetWait;					// The scroll-to-target will happen when this is zero. Decremented once per frame.
	F32 scrollToTargetRemaining;			// Time remaining for a scroll-to-target to complete
	Vec2 scrollToTargetPos;					// The current target position
	F32 scrollToTargetZoom;					// The current target zoom level
	S32 scrollToTargetZoomDir;				// -1 zoom out, 1 zoom in, 0 zoom any.
	F32 scrollToTargetZoomRate;				// The current zoom rate

	// Zoom slider
	F32 zoom_slider_queued_child_scale;
	UISlider* zoom_slider;

	// Callback function called before the left mouse button is used
	// to drag (if draggable).  This is needed in case a derived
	// widget wants to do some special case drag handling with the
	// left mouse button in certain areas.  Currently used by the
	// Treechart.
	UIActivationFunc beforeDragF;
	UserData beforeDragData;

	// Callback function called whenever child scale changes in order
	// to compute new scroll area size. Only called if autosize is false.
	UISizeFunc sizeF;
	UserData sizeData;
} UIScrollArea;

SA_RET_NN_VALID UIScrollArea *ui_ScrollAreaCreate(F32 x, F32 y, F32 w, F32 h, F32 xSize, F32 ySize, bool xScroll, bool yScroll);
void ui_ScrollAreaInitialize(SA_PARAM_NN_VALID UIScrollArea *scrollarea, F32 x, F32 y, F32 w, F32 h, F32 xSize, F32 ySize, bool xScroll, bool yScroll);
void ui_ScrollAreaTick(SA_PARAM_NN_VALID UIScrollArea *scrollarea, UI_PARENT_ARGS);
void ui_ScrollAreaDraw(SA_PARAM_NN_VALID UIScrollArea *scrollarea, UI_PARENT_ARGS);
void ui_ScrollAreaFreeInternal(SA_PRE_NN_VALID SA_POST_P_FREE UIScrollArea *scrollarea);

void ui_ScrollAreaSetDraggable(SA_PARAM_NN_VALID UIScrollArea *scrollarea, bool draggable);
void ui_ScrollAreaSetNoCtrlDraggable(SA_PARAM_NN_VALID UIScrollArea *scrollarea, bool draggable);
void ui_ScrollAreaSetSize(SA_PARAM_NN_VALID UIScrollArea *scrollarea, F32 xSize, F32 ySize);
void ui_ScrollAreaSetZoomSlider(SA_PARAM_NN_VALID UIScrollArea *scrollArea, bool enable);
void ui_ScrollAreaSetChildScale(SA_PARAM_NN_VALID UIScrollArea *scrollarea, F32 scale);
void ui_ScrollAreaAddChild(SA_PARAM_NN_VALID UIScrollArea *scrollarea, SA_PRE_NN_BYTES(sizeof(UIWidget)) SA_POST_NN_VALID UIAnyWidget *child);
void ui_ScrollAreaRemoveChild(SA_PARAM_NN_VALID UIScrollArea *scrollarea, SA_PRE_NN_BYTES(sizeof(UIWidget)) SA_POST_NN_VALID UIAnyWidget *child);

void ui_ScrollAreaScrollToPosition(SA_PARAM_NN_VALID UIScrollArea *scrollarea, F32 x, F32 y);
void ui_ScrollAreaZoomToScale(SA_PARAM_NN_VALID UIScrollArea *scrollarea, F32 scale, int dir); // dir = -1 zoom out, 1 zoom in, 0 zoom any.
void ui_ScrollAreaSetAutoZoomRate(SA_PARAM_NN_VALID UIScrollArea *scrollarea, F32 rate);

void ui_ScrollAreaCheckScroll(UIScrollArea *scrollarea, F32 x, F32 y, F32 w, F32 h, CBox *box);


#endif
