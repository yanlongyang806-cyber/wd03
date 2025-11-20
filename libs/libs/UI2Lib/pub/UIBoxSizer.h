/***************************************************************************



***************************************************************************/

#ifndef UI_BOX_SIZER_H
#define UI_BOX_SIZER_H
GCC_SYSTEM

#include "UISizer.h"

typedef struct UIBoxSizer
{
	UI_INHERIT_FROM(UI_SIZER_TYPE);

	UIDirection orientation;

	PrivateData privateData;
} UIBoxSizer;

SA_RET_NN_VALID UIBoxSizer *ui_BoxSizerCreate(UIDirection orientation);
void ui_BoxSizerFree(SA_PARAM_NN_VALID UIBoxSizer *pBoxSizer);

void ui_BoxSizerAddWidget(SA_PARAM_NN_VALID UIBoxSizer *pBoxSizer, SA_PARAM_NN_VALID UIWidget *pChildWidget, int proportion, UIDirection direction, int border);
void ui_BoxSizerAddSizer(SA_PARAM_NN_VALID UIBoxSizer *pBoxSizer, SA_PARAM_NN_VALID UISizer *pChildSizer, int proportion, UIDirection direction, int border);
void ui_BoxSizerAddSpacer(SA_PARAM_NN_VALID UIBoxSizer *pBoxSizer, int size);
void ui_BoxSizerAddFiller(SA_PARAM_NN_VALID UIBoxSizer *pBoxSizer, int proportion);

#endif
