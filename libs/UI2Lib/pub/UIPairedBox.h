/***************************************************************************



***************************************************************************/

#ifndef UI_PAIREDBOX_H
#define UI_PAIREDBOX_H
GCC_SYSTEM

#include "UICore.h"

//////////////////////////////////////////////////////////////////////////
// Paired boxes are just two widgets with a spline drawn between them.

typedef struct UIPairedBox UIPairedBox;

typedef struct UIPairedBoxLine
{
	UIWidget widget;

	UIPairedBox *dest;
	UIPairedBox *source;
	F32 lineScale;
} UIPairedBoxLine;

typedef struct UIPairedBox
{
	UIWidget widget;

	Color color;

	UIPairedBox *otherBox;
	UIPairedBoxLine *line;

	F32 lastX;
	F32 lastY;

	bool bVertical : 1;
} UIPairedBox;

// These are set up a bit different from other widgets:
// UIPairedBox *source, *dest;
// ui_PairedBoxCreate(&source, &dest, ColorGreen);
// ui_WindowAddChild(window1, source);
// ui_WindowAddChild(window1, dest);
void ui_PairedBoxCreatePair(SA_PRE_NN_FREE SA_POST_NN_VALID UIPairedBox **source, SA_PRE_NN_FREE SA_POST_NN_VALID UIPairedBox **dest, Color color, SA_PARAM_NN_VALID UIAnyWidget *lineParent);

// Make a single box pointing to another.
SA_RET_NN_VALID UIPairedBox *ui_PairedBoxCreate(Color color);

// Call this on the dest box if you need to hide it, to stop the line from
// being drawn.
void ui_PairedBoxDisable(SA_PARAM_NN_VALID UIPairedBox *dest);

void ui_PairedBoxConnect(SA_PARAM_NN_VALID UIPairedBox *source, SA_PARAM_NN_VALID UIPairedBox *dest, SA_PARAM_NN_VALID UIAnyWidget *lineParent);
void ui_PairedBoxDisconnect(SA_PARAM_NN_VALID UIPairedBox *source, SA_PARAM_NN_VALID UIPairedBox *dest);

void ui_PairedBoxFreeInternal(SA_PRE_NN_VALID SA_POST_P_FREE UIPairedBox *box);

void ui_PairedBoxDraw(SA_PARAM_NN_VALID UIPairedBox *paired, UI_PARENT_ARGS);
void ui_PairedBoxTick(UIPairedBox *paired, UI_PARENT_ARGS);

#endif