/***************************************************************************



***************************************************************************/

#ifndef UI_LABEL_H
#define UI_LABEL_H
GCC_SYSTEM

#include "UICore.h"

//////////////////////////////////////////////////////////////////////////
// A simple static text label. Width and height are decoded by the text.
// Unlike every other widgets, the font member of UILabel trumps the
// skin settings. (Though it is initially set from the skin.)

typedef struct UILabel
{
	UIWidget widget;
	UIStyleFont *pLastFont;
	F32 fLastWidth;

	UIDirection textFrom;
	unsigned bWrap : 1;
	unsigned bOpaque : 1;
	unsigned bNoAutosizeWidth : 1;
	unsigned bUseWidgetColor : 1;
	unsigned bRotateCCW : 1;
} UILabel;

SA_RET_NN_VALID UILabel *ui_LabelCreate(const char *text, F32 x, F32 y);
SA_RET_NN_VALID UILabel *ui_LabelCreateWithMessage(const char *message_key, F32 x, F32 y);
void ui_LabelInitialize(SA_PRE_NN_FREE SA_POST_NN_VALID UILabel *label, const char *text, F32 x, F32 y);
void ui_LabelInitializeWithMessage(SA_PRE_NN_FREE SA_POST_NN_VALID UILabel *label, const char *message_key, F32 x, F32 y);
void ui_LabelFreeInternal(SA_PRE_NN_VALID SA_POST_P_FREE UILabel *label);

#define ui_LabelCreateAndAdd(label, window, text, x, y) ui_WindowAddChild(window, label = ui_LabelCreate(text, x, y))
#define ui_LabelCreateAndAddToExpander(label, expander, text, x, y) ui_ExpanderAddChild(expander, &((label = ui_LabelCreate(text, x, y))->widget))

void ui_LabelUpdateDimensionsForWidth(SA_PARAM_NN_VALID UILabel *pLabel, F32 fWidth);

void ui_LabelSetText(SA_PARAM_NN_VALID UILabel *label, const char *text);
void ui_LabelSetMessage(SA_PARAM_NN_VALID UILabel *label, const char *message_key);
void ui_LabelSetFont(SA_PARAM_NN_VALID UILabel *label, SA_PARAM_OP_VALID UIStyleFont *font);

void ui_LabelDraw(SA_PARAM_NN_VALID UILabel *label, UI_PARENT_ARGS);

void ui_LabelSetWordWrap(SA_PARAM_NN_VALID UILabel *pLabel, bool bWrap);
void ui_LabelSetWidthNoAutosize(SA_PARAM_NN_VALID UILabel *pLabel, F32 w, UIUnitType wUnit);
void ui_LabelResize(SA_PARAM_NN_VALID UILabel* pLabel);
void ui_LabelForceAutosize(SA_PARAM_NN_VALID UILabel* pLabel);

void ui_LabelEnableTooltips(UILabel *pLabel);


#endif
