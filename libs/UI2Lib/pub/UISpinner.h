#pragma once
GCC_SYSTEM
/***************************************************************************



***************************************************************************/

#ifndef UI_SPINNER_H
#define UI_SPINNER_H

#include "UICore.h"

typedef struct UISprite UISprite;

//////////////////////////////////////////////////////////////////////////
// A float spinner.  Has up and down arrows and can be dragged up and down.
typedef struct UISpinner
{
	UIWidget widget;
	UIActivationFunc changedF;
	UserData changedData;

	UIActivationFunc startSpinF;
	UserData startSpinData;
	UIActivationFunc stopSpinF;
	UserData stopSpinData;

	bool pressed : 1;
	F32 min, max, step;
	F32 currentVal;
} UISpinner;

SA_RET_NN_VALID UISpinner *ui_SpinnerCreate(F32 x, F32 y, F32 minValue, F32 maxValue, F32 step, F32 currentValue, UIActivationFunc changedF, UserData changedData);
void ui_SpinnerInitialize(SA_PRE_NN_FREE SA_POST_NN_VALID UISpinner *spinner, F32 x, F32 y, F32 minValue, F32 maxValue, F32 step, F32 currentValue, UIActivationFunc changedF, UserData changedData);
void ui_SpinnerFreeInternal(SA_PRE_NN_VALID SA_POST_P_FREE UISpinner *spinner);

void ui_SpinnerSetChangedCallback(SA_PARAM_NN_VALID UISpinner *spinner, UIActivationFunc changedF, UserData changedData);
void ui_SpinnerSetStartSpinCallback(SA_PARAM_NN_VALID UISpinner *spinner, UIActivationFunc startSpinF, UserData startSpinData);
void ui_SpinnerSetStopSpinCallback(SA_PARAM_NN_VALID UISpinner *spinner, UIActivationFunc stopSpinF, UserData stopSpinData);

void ui_SpinnerUp(SA_PARAM_NN_VALID UISpinner *spinner);
void ui_SpinnerDown(SA_PARAM_NN_VALID UISpinner *spinner);

F32 ui_SpinnerGetValue(SA_PARAM_NN_VALID UISpinner *spinner);
void ui_SpinnerSetValue(SA_PARAM_NN_VALID UISpinner *spinner, F32 value);
void ui_SpinnerSetValueAndCallback(SA_PARAM_NN_VALID UISpinner *spinner, F32 value);

void ui_SpinnerTick(SA_PARAM_NN_VALID UISpinner *spinner, UI_PARENT_ARGS);
void ui_SpinnerDraw(SA_PARAM_NN_VALID UISpinner *spinner, UI_PARENT_ARGS);

void ui_SpinnerSetBounds(SA_PARAM_NN_VALID UISpinner *spinner, F32 fMin, F32 fMax, F32 fStep);
void ui_SpinnerSetBoundsAndCallback(SA_PARAM_NN_VALID UISpinner *spinner, F32 fMin, F32 fMax, F32 fStep);

#endif