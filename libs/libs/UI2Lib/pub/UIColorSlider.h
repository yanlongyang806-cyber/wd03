#pragma once
GCC_SYSTEM
/***************************************************************************



***************************************************************************/

#ifndef UI_COLOR_SLIDER_H
#define UI_COLOR_SLIDER_H

#include "UICore.h"

//////////////////////////////////////////////////////////////////////////
// A draggable slider that interpolates between colors.

typedef struct UIColorSlider
{
	UIWidget widget;
	UIActivationFunc changedF;
	UserData changedData;
	Vec3 min, max, current;
	bool hsv : 1;
	bool dragging : 1;
	bool clamp : 1;
	bool lum : 1;
} UIColorSlider;

SA_RET_NN_VALID UIColorSlider *ui_ColorSliderCreate(F32 x, F32 y, F32 width, Vec3 min, Vec3 max, bool hsv);
void ui_ColorSliderInitialize(SA_PRE_NN_FREE SA_POST_NN_VALID UIColorSlider *cslider, F32 x, F32 y, F32 width, Vec3 min, Vec3 max, bool hsv);
void ui_ColorSliderFreeInternal(SA_PRE_NN_VALID SA_POST_P_FREE UIColorSlider *cslider);

void ui_ColorSliderSetRange(SA_PARAM_NN_VALID UIColorSlider *cslider, SA_PRE_NN_ELEMS(3) SA_POST_OP_VALID Vec3 min, SA_PRE_NN_ELEMS(3) SA_POST_OP_VALID Vec3 max);
void ui_ColorSliderSetValue(SA_PARAM_NN_VALID UIColorSlider *cslider, SA_PRE_NN_ELEMS(3) SA_POST_OP_VALID Vec3 value);
void ui_ColorSliderSetValueAndCallback(SA_PARAM_NN_VALID UIColorSlider *cslider, SA_PRE_NN_ELEMS(3) SA_POST_OP_VALID Vec3 value);

void ui_ColorSliderSetChangedCallback(SA_PARAM_NN_VALID UIColorSlider *cslider, UIActivationFunc changedF, UserData changedData);

void ui_ColorSliderGetValue(UIColorSlider *cslider, Vec3 value);

bool ui_ColorSliderInput(SA_PARAM_NN_VALID UIColorSlider *cslider, SA_PARAM_NN_VALID KeyInput *key);
void ui_ColorSliderDraw(SA_PARAM_NN_VALID UIColorSlider *cslider, UI_PARENT_ARGS);
void ui_ColorSliderTick(SA_PARAM_NN_VALID UIColorSlider *cslider, UI_PARENT_ARGS);

#endif
