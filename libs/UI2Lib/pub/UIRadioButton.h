/***************************************************************************



***************************************************************************/

#ifndef UI_RADIOBUTTON_H
#define UI_RADIOBUTTON_H
GCC_SYSTEM

#include "UICore.h"

typedef struct UIRadioButton UIRadioButton;

//////////////////////////////////////////////////////////////////////////
// A list of radio buttons; only one button in the group may be active at
// a time.
typedef struct UIRadioButtonGroup
{
	UIRadioButton **buttons;
	UIActivationFunc toggledF;
	UserData toggledData;
} UIRadioButtonGroup;

SA_RET_NN_VALID UIRadioButtonGroup *ui_RadioButtonGroupCreate(void);

// Frees the group, but not the buttons in it. Freeing the last button in
// a group will free the group as well.
void ui_RadioButtonGroupFreeInternal(SA_PRE_NN_VALID SA_POST_P_FREE UIRadioButtonGroup *group);

void ui_RadioButtonGroupAdd(SA_PARAM_NN_VALID UIRadioButtonGroup *group, SA_PARAM_NN_VALID UIRadioButton *radio);
void ui_RadioButtonGroupRemove(SA_PARAM_NN_VALID UIRadioButtonGroup *group, SA_PARAM_NN_VALID UIRadioButton *radio);
SA_RET_OP_VALID UIRadioButton *ui_RadioButtonGroupGetActive(SA_PARAM_NN_VALID UIRadioButtonGroup *group);
void ui_RadioButtonGroupSetToggledCallback(SA_PARAM_NN_VALID UIRadioButtonGroup *group, UIActivationFunc toggledF, UserData toggledData);
void ui_RadioButtonGroupSetActive(SA_PARAM_NN_VALID UIRadioButtonGroup *group, SA_PARAM_NN_VALID UIRadioButton *radio);
void ui_RadioButtonGroupSetActiveAndCallback(SA_PARAM_NN_VALID UIRadioButtonGroup *group, SA_PARAM_NN_VALID UIRadioButton *radio);

// If the radio buttons are using fixed heights, this will reposition all
// of them to be stacked vertically starting at the given y.
F32 ui_RadioButtonGroupSetPosition(SA_PARAM_NN_VALID UIRadioButtonGroup *group, F32 x, F32 y);

typedef struct UIRadioButton
{
	UIWidget widget;
	bool state;
	UIRadioButtonGroup *group;
	UIActivationFunc toggledF;
	UserData toggledData;
} UIRadioButton;

SA_RET_NN_VALID UIRadioButton *ui_RadioButtonCreate(F32 x, F32 y, SA_PARAM_NN_STR const char *text, SA_PARAM_OP_VALID UIRadioButtonGroup *group);
void ui_RadioButtonInitialize(SA_PRE_NN_FREE SA_POST_NN_VALID UIRadioButton *radio, F32 x, F32 y, SA_PARAM_NN_STR const char *text, SA_PARAM_OP_VALID UIRadioButtonGroup *group);
void ui_RadioButtonFree(SA_PRE_NN_VALID SA_POST_P_FREE UIRadioButton *radio);

void ui_RadioButtonActivate(SA_PARAM_NN_VALID UIRadioButton *radio);
void ui_RadioButtonSetText(SA_PARAM_NN_VALID UIRadioButton *radio, SA_PARAM_NN_STR const char *text);
void ui_RadioButtonSetMessage(SA_PARAM_NN_VALID UIRadioButton *radio, SA_PARAM_NN_STR const char *message);
void ui_RadioButtonSetToggledCallback(SA_PARAM_NN_VALID UIRadioButton *radio, UIActivationFunc toggledF, UserData toggledData);

bool ui_RadioButtonInput(SA_PARAM_NN_VALID UIRadioButton *radio, SA_PARAM_NN_VALID KeyInput *input);
void ui_RadioButtonDraw(SA_PARAM_NN_VALID UIRadioButton *radio, UI_PARENT_ARGS);
void ui_RadioButtonTick(SA_PARAM_NN_VALID UIRadioButton *radio, UI_PARENT_ARGS);

#endif
