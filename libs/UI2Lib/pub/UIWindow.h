/***************************************************************************



***************************************************************************/

#ifndef UI_WINDOW_H
#define UI_WINDOW_H
GCC_SYSTEM

#include "UICore.h"

typedef struct UITitleButton
{
	const char *textureName;
	UIActivationFunc callback;
	UserData callbackData;

	// runtime state:
	unsigned bIsHovering : 1;
	unsigned bIsPressed : 1;
} UITitleButton;

#define UI_WINDOW_TYPE UIWindow window;
#define UI_WINDOW(widget) (&widget->window)

//////////////////////////////////////////////////////////////////////////
// Top-level windows, which can be moved around, shown or hidden, and
// contain any kind of widgets (including other windows).
//
// Do not change the window's offsetFrom type; it must be specified from the
// top-left, or resizing/moving won't work. Likewise, with percentage-based
// sizes, resizing will not work, and will be disabled.
typedef struct UIWindow
{
	UIWidget widget;

	// Titlebar stuff.
	bool dragging;
	int grabbedX, grabbedY;
	UITitleButton **buttons;

	const char* astrStyleOverride;

	// Whether the window is resizable. If any of the size units are
	// UIUnitPercentage, the window can't be resized regardless.
	bool resizable : 1;

	// Whether the window can be dragged to move it.
	bool movable : 1;

	// Whether the window can be double-clicked to shade it. 
	bool shadable : 1;

	// If true, the window does not let anything else in the UI receive
	// input until it is gone.
	bool modal : 1;

	// If the window prefs set this window to hide, then setting the active editor should not force it to show.
	// This should only be set when applying or saving a window's preferences
	bool setToHide : 1;

	// If false, the window does not tick or draw. Can't be a bitfield
	// since it's address must used as a state pointer elsewhere.
	bool show;

	// Minimum size for the window, not including decorations
	F32 minW, minH;

	// Modal alpha fade value.  0 = not faded at all, 1 = fully faded.
	// Changed in Tick.
	F32 modalAlphaCur;

	// Modal alpha fully faded value.  If -1, use the default.
	F32 modalAlphaTarget;

	// While > 0, flash the window title bar.
	F32 flashSeconds;

	// Called when the window is closed. A common reaction might be to queue
	// the window for freeing.
	UICloseFunc closeF;
	UserData closeData;

	// Called when the user raises the window.
	UIActivationFunc raisedF;
	UserData raisedData;

	// Called when the user clicks on the window (and not any of its children)
	// and it's already raised.
	UIActivationFunc clickedF;
	UserData clickedData;

	// Called when the user finishes moving the window
	UIActivationFunc movedF;
	UserData movedData;

	// Called when the user finishes resizing the window
	UIActivationFunc resizedF;
	UserData resizedData;

	// What direction the window is currently resizing in, if any.
	UIDirection resizing;

	bool shaded : 1;
	unsigned bDimensionsIncludesNonclient : 1;

	// Called when the window is shaded or unshaded (check the shaded
	// boolean to see which, it's updated before the function is called).
	UIActivationFunc shadedF;
	UserData shadedData;

	int iOverrideModalBackgroundColor;
} UIWindow;

// Create a new window, with the given size. If NULL is given as the title,
// the window will not have a title bar (and cannot be moved).
SA_RET_NN_VALID UIWindow *ui_WindowCreateEx(SA_PARAM_OP_STR const char *title, F32 x, F32 y, F32 w, F32 h MEM_DBG_PARMS);
#define ui_WindowCreate(title,x,y,w,h) ui_WindowCreateEx(title,x,y,w,h MEM_DBG_PARMS_INIT)
void ui_WindowInitializeEx(SA_PARAM_NN_VALID UIWindow *window, SA_PARAM_OP_STR const char *title, F32 x, F32 y, F32 w, F32 h MEM_DBG_PARMS);
void ui_WindowFreeInternal(SA_PARAM_NN_VALID UIWindow *window);

void ui_WindowSetTitle(SA_PARAM_NN_VALID UIWindow *window, SA_PARAM_OP_STR const char *title);

void ui_WindowAddChild(SA_PARAM_NN_VALID UIWindow *window, SA_PRE_NN_BYTES(sizeof(UIWidget)) SA_POST_NN_VALID UIAnyWidget *child);
void ui_WindowRemoveChild(SA_PARAM_NN_VALID UIWindow *window, SA_PRE_NN_BYTES(sizeof(UIWidget)) SA_POST_NN_VALID UIAnyWidget *child);

void ui_WindowRemoveFromGroup(SA_PARAM_NN_VALID UIWindow *window);

// Add/remove the window to/from the current device.
void ui_WindowShow(SA_PARAM_NN_VALID UIWindow *window);
void ui_WindowShowEx(UIWindow *window, bool forceWindowGroup);
void ui_WindowHide(SA_PARAM_NN_VALID UIWindow *window);

// Show the window and have it steal focus.
void ui_WindowPresent(SA_PARAM_NN_VALID UIWindow *window);
void ui_WindowPresentEx(SA_PARAM_NN_VALID UIWindow *window, bool forceWindowGroup);

// This isn't 100% reliable. If the window is the child of a window that is
// not currently being shown, then this will give a false positive. It's more
// "is this parented?" than "is this visible?"
bool ui_WindowIsVisible(SA_PARAM_NN_VALID UIWindow *window);

void ui_WindowSetModal(SA_PARAM_NN_VALID UIWindow *window, bool modal);
void ui_WindowSetResizable(SA_PARAM_NN_VALID UIWindow *window, bool resizable);
void ui_WindowSetClosable(SA_PARAM_NN_VALID UIWindow *window, bool closable);
void ui_WindowSetMovable(SA_PARAM_NN_VALID UIWindow *window, bool movable);
void ui_WindowSetShadable(SA_PARAM_NN_VALID UIWindow *window, bool shadable);
void ui_WindowSetDimensions(SA_PARAM_NN_VALID UIWindow *window, F32 w, F32 h, F32 minW, F32 minH);
void ui_WindowAutoSetDimensions(SA_PARAM_NN_VALID UIWindow *window);

void ui_WindowSetCycleBetweenDisplays(SA_PARAM_NN_VALID UIWindow *pWindow, bool bCyclable);

// The same as UIWindowHide, but calls any user close callbacks.
void ui_WindowClose(SA_PARAM_NN_VALID UIWindow *window);

// Free the window, with a signature appropriate for a callback.
bool ui_WindowFreeOnClose(UIWindow *window, UserData dummy);
bool ui_WindowSetShowToFalseOnClose(UIWindow *window, UserData ignored);

void ui_WindowToggleShadedAndCallback(UIWindow *window);

void ui_WindowSetCloseCallback(SA_PARAM_NN_VALID UIWindow *window, UICloseFunc closeF, UserData closeData);
void ui_WindowSetRaisedCallback(SA_PARAM_NN_VALID UIWindow *window, UIActivationFunc raisedF, UserData raisedData);
void ui_WindowSetClickedCallback(SA_PARAM_NN_VALID UIWindow *window, UIActivationFunc clickedF, UserData clickedData);
void ui_WindowSetShadedCallback(SA_PARAM_NN_VALID UIWindow *window, UIActivationFunc shadedF, UserData shadedData);
void ui_WindowSetMovedCallback(SA_PARAM_NN_VALID UIWindow *window, UIActivationFunc shadedF, UserData shadedData);
void ui_WindowSetResizedCallback(SA_PARAM_NN_VALID UIWindow *window, UIActivationFunc shadedF, UserData shadedData);

AtlasTex* ui_WindowGetButtonTex(UIWindow *window, UITitleButton *button);

void ui_TitleBarCheckDragging(SA_PARAM_NN_VALID UIWindow *window, UI_PARENT_ARGS, UI_MY_ARGS);

SA_RET_NN_VALID UITitleButton *ui_WindowTitleButtonCreate(SA_PARAM_NN_STR const char *texture, UIActivationFunc clickedF, UserData clickedData);
void ui_WindowAddTitleButton(SA_PARAM_NN_VALID UIWindow *window, SA_PARAM_NN_VALID UITitleButton *button);
void ui_WindowRemoveTitleButton(SA_PARAM_NN_VALID UIWindow *window, SA_PARAM_NN_VALID UITitleButton *button);

bool ui_WindowInput(SA_PARAM_NN_VALID UIWindow *window, SA_PARAM_NN_VALID KeyInput *key);
void ui_WindowCheckResizing(SA_PARAM_NN_VALID UIWindow *window, UI_PARENT_ARGS, UI_MY_ARGS);
void ui_WindowTitleTick(SA_PARAM_NN_VALID UIWindow *window, UI_PARENT_ARGS, UI_MY_ARGS, F32 fullHeight);
void ui_WindowTick(SA_PARAM_NN_VALID UIWindow *window, UI_PARENT_ARGS);
void ui_WindowDraw(SA_PARAM_NN_VALID UIWindow *window, UI_PARENT_ARGS);
F32 ui_WindowGetModalAlpha(SA_PARAM_NN_VALID UIWindow *window);
void ui_WindowPlaceAtCursorOrWidgetBox( SA_PARAM_NN_VALID UIWindow* window );

/// Close all open windows
void ui_WindowCloseAll( void );

// Sanity check (for percentage based windows, we can't do it in ui_CheckResizing)
#define UI_WINDOW_SANITY_CHECK(window) \
	F32 dummyIf1 = ((w + ((g_ui_Tex.windowTitleLeft)->width + (g_ui_Tex.windowTitleRight)->width) * scale) > pW)?	\
	(w = (pW - ((g_ui_Tex.windowTitleLeft)->width + (g_ui_Tex.windowTitleRight)->width) * scale)):0;			\
	F32 dummyIf2 = ((h + ((g_ui_Tex.windowTitleLeft)->height + (g_ui_Tex.windowTitleRight)->height) * scale) > pH)?	\
	(h = (pH - ((g_ui_Tex.windowTitleLeft)->height + (g_ui_Tex.windowTitleRight)->height) * scale)):0


#endif
