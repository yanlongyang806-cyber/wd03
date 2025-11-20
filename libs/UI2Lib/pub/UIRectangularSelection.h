#pragma once
GCC_SYSTEM
/***************************************************************************



***************************************************************************/


#include "UICore.h"
#include "UIWindow.h"

// A resizeable widget for making a rectangular selection

typedef enum UIResizingFlag {
	UIRF_Left = 1 << 0,
	UIRF_Right = 1 << 1,
	UIRF_Top = 1 << 2,
	UIRF_Bottom = 1 << 3,
} UIResizingFlag;

typedef struct UIRectangularSelection
{
	UIWidget widget;
	UIWidget orig_widget; // For saving old position and size, do not free, it's struct copied!
	Color color;
	Color rolloverColor;
	UIResizingFlag resizing; // Mask for which edges are being resized/moved
	UIResizingFlag would_be_resizing; // Mask for which edges would be resized based on current mouse position
	UIDirection cursor_direction; // What "direction" of a cursor do we need
	F32 minWidth, maxWidth;
	F32 minHeight, maxHeight;

	S32 grabbedX, grabbedY; // Where it was grabbed

	UIActivationFunc resizeFinishedF;
	UserData resizeFinishedData;
} UIRectangularSelection;

SA_RET_NN_VALID UIRectangularSelection *ui_RectangularSelectionCreate(F32 x, F32 y, F32 w, F32 h, Color color, Color rolloverColor);
void ui_RectangularSelectionInitialize(SA_PRE_NN_FREE SA_POST_NN_VALID UIRectangularSelection *rect, F32 x, F32 y, F32 w, F32 h, Color color, Color rolloverColor);
void ui_RectangularSelectionFreeInternal(SA_PRE_NN_VALID SA_POST_P_FREE UIRectangularSelection *rect);
void ui_RectangularSelectionSetResizeFinishedCallback(SA_PRE_NN_VALID SA_POST_P_FREE UIRectangularSelection *rect, UIActivationFunc resizeFinishedF, UserData resizeFinishedData);

