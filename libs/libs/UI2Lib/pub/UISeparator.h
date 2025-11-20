/***************************************************************************



***************************************************************************/

#ifndef UI_SEPARATOR_H
#define UI_SEPARATOR_H
GCC_SYSTEM

#include "UICore.h"

typedef struct UISeparator
{
	UIWidget widget;

	UIDirection orientation;
} UISeparator;

// By default separators fill 100% of parent space along their direction and 2px in the opposite direction.
SA_RET_NN_VALID UISeparator *ui_SeparatorCreate(UIDirection orientation);
void ui_SeparatorFreeInternal(SA_PRE_NN_VALID SA_POST_P_FREE UISeparator *sep);

void ui_SeparatorDraw(SA_PARAM_NN_VALID UISeparator *sep, UI_PARENT_ARGS);

void ui_SeparatorResize(SA_PARAM_NN_VALID UISeparator *sep);

#endif
