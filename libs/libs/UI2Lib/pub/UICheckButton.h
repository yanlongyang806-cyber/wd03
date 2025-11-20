/***************************************************************************



***************************************************************************/

#ifndef UI_CHECKBUTTON_H
#define UI_CHECKBUTTON_H
GCC_SYSTEM

#include "UICore.h"

//////////////////////////////////////////////////////////////////////////
// A small check box with a label next to it.

#define UI_CHECK_BUTTON_TYPE UICheckButton check;
#define UI_CHECK_BUTTON(widget) (&(widget)->check)

typedef struct UICheckButton
{
	struct UIWidget widget;
	bool state;
	bool *statePtr;
	bool pressed;
	UIActivationFunc toggledF;
	UserData toggledData;
} UICheckButton;

SA_RET_NN_VALID UICheckButton *ui_CheckButtonCreate(F32 x, F32 y, SA_PARAM_NN_STR const char *text, bool state);
void ui_CheckButtonInitialize(SA_PRE_NN_FREE SA_POST_NN_VALID UICheckButton *check, F32 x, F32 y, SA_PARAM_NN_STR const char *text, bool state);
void ui_CheckButtonFreeInternal(SA_PRE_NN_VALID SA_POST_P_FREE UICheckButton *check);

// Calling this resets the width/height based on the text.
void ui_CheckButtonSetText(SA_PARAM_NN_VALID UICheckButton *check, SA_PARAM_NN_STR const char *text);

void ui_CheckButtonSetToggledCallback(SA_PARAM_NN_VALID UICheckButton *check, UIActivationFunc toggledF, UserData toggledData);

bool ui_CheckButtonGetState(SA_PARAM_NN_VALID UICheckButton *check);

void ui_CheckButtonSetStateAndCallback(SA_PARAM_NN_VALID UICheckButton *check, bool state);
void ui_CheckButtonSetState(SA_PARAM_NN_VALID UICheckButton *check, bool state);

int ui_CheckButtonWidthNoText(void);
int ui_CheckButtonHeightNoText(void);

// This calls the callback.
void ui_CheckButtonToggle(SA_PARAM_NN_VALID UICheckButton *check);

bool ui_CheckButtonInput(SA_PARAM_NN_VALID UICheckButton *check, SA_PARAM_NN_VALID KeyInput *input);
void ui_CheckButtonTick(SA_PARAM_NN_VALID UICheckButton *check, UI_PARENT_ARGS);
void ui_CheckButtonDraw(SA_PARAM_NN_VALID UICheckButton *check, UI_PARENT_ARGS);

void ui_CheckButtonResize( SA_PARAM_NN_VALID UICheckButton* check);

#endif
