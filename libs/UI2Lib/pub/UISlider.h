#pragma once
GCC_SYSTEM
/***************************************************************************



***************************************************************************/

#ifndef UI_SLIDER_H
#define UI_SLIDER_H

#include "UICore.h"

//////////////////////////////////////////////////////////////////////////
// A draggable slider. It can snap to integer or float values, and calls
// a callback when the value changes.

typedef void (*UISliderChangeFunc)(UIAnyWidget *, bool finished, UserData);

// There are two different policies for when to call the changedF callback.
// Discrete calls it when the user releases the slider in a new position.
// Continuous calls the callback every frame the user moves the slider.
typedef enum UISliderPolicy
{
	UISliderDiscrete = 0,
	UISliderContinuous,
} UISliderPolicy;

typedef struct UISliderSpecialValue {
	F64 value;
	Color c;
} UISliderSpecialValue;

typedef struct UISlider
{
	UIWidget widget;
	UISliderChangeFunc changedF;
	UserData changedData;

	// The slider's value is specified in terms of whatever units you like,
	// and can be clamped to multiples of a step value. If the slider is
	// tracking an integer value, a step of 0 means a step of 1; if floats,
	// a step of 0 means no clamping.
	F64 min, max, step, minSeparation;
	F64 *currentVals;
	S32 count;
	UISliderPolicy policy;
	S32 draggingIdx;
	S32 hoverIdx;
	U32 grabbedX;
	F32 bias;//Power of the function used in the bias. Should be >= 2
	F32 bias_offset;//Value between 0 and 1 that is the mid point on the slider

	// Values that mean something
	UISliderSpecialValue **eaSpecials;
} UISlider;

SA_RET_NN_VALID UISlider *ui_SliderCreate(F32 x, F32 y, F32 width, F64 min, F64 max, F64 current);
void ui_SliderFreeInternal(SA_PRE_NN_VALID SA_POST_P_FREE UISlider *slider);

// multislider functions
void ui_SliderSetCount(SA_PARAM_NN_VALID UISlider *slider, int count, F64 separation);
F64 ui_SliderGetValueEx(SA_PARAM_NN_VALID UISlider *slider, int idx);
void ui_SliderSetValueAndCallbackEx(SA_PARAM_NN_VALID UISlider *slider, int idx, F64 val, int callChangedCallback, bool allowOutOfRange);

__forceinline static F64 ui_SliderGetValue(SA_PARAM_NN_VALID UISlider *slider) { return ui_SliderGetValueEx(slider, 0); }
__forceinline static void ui_SliderSetValue(SA_PARAM_NN_VALID UISlider *slider, F64 current) { ui_SliderSetValueAndCallbackEx(slider, 0, current, 0, false); }
__forceinline static void ui_SliderSetValueAndCallback(SA_PARAM_NN_VALID UISlider *slider, F64 current) { ui_SliderSetValueAndCallbackEx(slider, 0, current, 1, false); }

void ui_SliderSetRange(SA_PARAM_NN_VALID UISlider *slider, F64 min, F64 max, F64 step);
void ui_SliderSetBias(SA_PARAM_NN_VALID UISlider *slider, F32 bias, F32 bias_offset);
void ui_SliderSetChangedCallback(SA_PARAM_NN_VALID UISlider *slider, UISliderChangeFunc changedF, UserData changedData);
void ui_SliderSetPolicy(SA_PARAM_NN_VALID UISlider *slider, UISliderPolicy policy);
void ui_SliderAddSpecialValue(SA_PARAM_NN_VALID UISlider *slider, F64 value, Color c);

bool ui_SliderInput(SA_PARAM_NN_VALID UISlider *slider, SA_PARAM_NN_VALID KeyInput *key);
void ui_SliderDraw(SA_PARAM_NN_VALID UISlider *slider, UI_PARENT_ARGS);
void ui_SliderTick(SA_PARAM_NN_VALID UISlider *slider, UI_PARENT_ARGS);

SA_RET_NN_VALID __forceinline static UISlider *ui_IntSliderCreate(F32 x, F32 y, F32 width, int iMin, int iMax, int iCurrent)
{
	F64 min, max, current;
	UISlider *ret = NULL;
	min = (F64)iMin;
	max = (F64)iMax;
	current = (F64)iCurrent;
	ret = ui_SliderCreate(x, y, width, min, max, current);
	ret->step = 1;
	return ret;
}

SA_RET_NN_VALID __forceinline static UISlider *ui_FloatSliderCreate(F32 x, F32 y, F32 width, F32 fMin, F32 fMax, F32 fCurrent)
{
	F64 min, max, current;
	UISlider *ret = NULL;
	min = (F64)fMin;
	max = (F64)fMax;
	current = (F64)fCurrent;
	ret = ui_SliderCreate(x, y, width, min, max, current);
	return ret;
}

__forceinline static int ui_IntSliderGetValueEx(SA_PARAM_NN_VALID UISlider *slider, int idx)
{
	F64 val = ui_SliderGetValueEx(slider, idx);
	return (int)val;
}

__forceinline static F32 ui_FloatSliderGetValueEx(SA_PARAM_NN_VALID UISlider *slider, int idx)
{
	F64 val = ui_SliderGetValueEx(slider, idx);
	return (F32)val;
}

__forceinline static int ui_IntSliderGetValue(SA_PARAM_NN_VALID UISlider *slider)
{
	return ui_IntSliderGetValueEx(slider, 0);
}

__forceinline static F32 ui_FloatSliderGetValue(SA_PARAM_NN_VALID UISlider *slider)
{
	return ui_FloatSliderGetValueEx(slider, 0);
}

__forceinline static void ui_IntSliderSetValueAndCallbackEx(SA_PARAM_NN_VALID UISlider *slider, int idx, int iVal, int callChangedCallback)
{
	F64 val;
	val = (F64)iVal;
	ui_SliderSetValueAndCallbackEx(slider, idx, val, callChangedCallback, false);
}

__forceinline static void ui_FloatSliderSetValueAndCallbackEx(SA_PARAM_NN_VALID UISlider *slider, int idx, F32 fVal, int callChangedCallback)
{
	F64 val;
	val = (F64)fVal;
	ui_SliderSetValueAndCallbackEx(slider, idx, val, callChangedCallback, false);
}

__forceinline static void ui_IntSliderSetValueAndCallback(SA_PARAM_NN_VALID UISlider *slider, int iVal)
{
	ui_IntSliderSetValueAndCallbackEx(slider, 0, iVal, 1);
}

__forceinline static void ui_FloatSliderSetValueAndCallback(SA_PARAM_NN_VALID UISlider *slider, F32 fVal)
{
	ui_FloatSliderSetValueAndCallbackEx(slider, 0, fVal, 1);
}

__forceinline static void ui_IntSliderSetValue(SA_PARAM_NN_VALID UISlider *slider, int iVal)
{
	ui_IntSliderSetValueAndCallbackEx(slider, 0, iVal, 0);
}

__forceinline static void ui_FloatSliderSetValue(SA_PARAM_NN_VALID UISlider *slider, F32 fVal)
{
	ui_FloatSliderSetValueAndCallbackEx(slider, 0, fVal, 0);
}

__forceinline static void ui_IntSliderSetRange(SA_PARAM_NN_VALID UISlider *slider, int iMin, int iMax)
{
	F64 min, max;
	min = (F64)iMin;
	max = (F64)iMax;
	ui_SliderSetRange(slider, min, max, 1);
}

__forceinline static void ui_FloatSliderSetRange(SA_PARAM_NN_VALID UISlider *slider, F32 fMin, F32 fMax)
{
	F64 min, max;
	min = (F64)fMin;
	max = (F64)fMax;
	ui_SliderSetRange(slider, min, max, 0);
}

#endif
