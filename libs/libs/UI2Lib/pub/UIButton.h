/***************************************************************************



***************************************************************************/

#ifndef UI_BUTTON_H
#define UI_BUTTON_H
GCC_SYSTEM

#include "UICore.h"

typedef struct UILabel UILabel;
typedef struct UISprite UISprite;

//////////////////////////////////////////////////////////////////////////
// A simple button. It can trigger a callback when clicked, and have
// arbitrary children inside it.
typedef struct UIButton
{
	UI_INHERIT_FROM(UI_WIDGET_TYPE);

	REF_TO(UIStyleBorder) hBorderOverride;

	bool toggle : 1; // whether or not this button is a toggle button

	bool down : 1; // whether or not this button is in the down state, only ever true if this is a toggle button
	bool pressed : 1; // whether button is current being held down
	bool spriteInheritsColor : 1;
	bool bChildrenOverlapBorder : 1;
	unsigned bNoAutoTruncateText : 1;
	unsigned bFocusOnClick : 1;
	unsigned bImageStretch : 1;

	// If set AND the button has image and text, both of those will get centered.
	unsigned bCenterImageAndText : 1;

	// If set, this button is allowed to draw its border outside any
	// logical rect.  Added for exactly one case -- UGC's tab buttons
	unsigned bDrawBorderOutsideRect : 1;

	// If set, then this overrides the normal behavior for positioning
	// text
	UIDirection textOffsetFrom;

	UIActivationFunc toggledF;
	UIActivationFunc clickedF;
	UIActivationFunc downF;
	UIActivationFunc upF;
	UserData toggledData;
	UserData clickedData;
	UserData downData;
	UserData upData;

	AtlasTex* normalImage;
	AtlasTex* highlightImage;
	AtlasTex* focusedImage;
	AtlasTex* pressedImage;
	AtlasTex* disabledImage;
} UIButton;

#define UI_BUTTON_TYPE UIButton button;
#define UI_BUTTON(widget) (&((widget)->button))

// If text is non-NULL, a UILabel is added as a child widget and the button
// is automatically sized to fit it.
SA_RET_NN_VALID UIButton *ui_ButtonCreateEx(SA_PARAM_OP_STR const char *text, F32 x, F32 y, UIActivationFunc clickedF, UserData clickedData MEM_DBG_PARMS);
#define ui_ButtonCreate(text, x, y, clickedF, clickedData) ui_ButtonCreateEx(text, x, y, clickedF, clickedData MEM_DBG_PARMS_INIT)
SA_RET_NN_VALID UIButton *ui_ButtonCreateWithIconEx(SA_PARAM_NN_STR const char *text, const char *texture, UIActivationFunc clickedF, UserData clickedData MEM_DBG_PARMS);
#define ui_ButtonCreateWithIcon(text, texture, clickedF, clickedData) ui_ButtonCreateWithIconEx(text, texture, clickedF, clickedData MEM_DBG_PARMS_INIT)
SA_RET_NN_VALID UIButton *ui_ButtonCreateWithDownUpEx(SA_PARAM_OP_STR const char *text, F32 x, F32 y, UIActivationFunc downF, UserData downData, UIActivationFunc upF, UserData upData MEM_DBG_PARMS);
#define ui_ButtonCreateWithDownUp(text, x, y, downF, downData, upF, upData) ui_ButtonCreateWithDownUpEx(text, x, y, downF, downData, upF, upData MEM_DBG_PARMS_INIT)
SA_RET_NN_VALID UIButton *ui_ButtonCreateImageOnlyEx(SA_PARAM_NN_STR const char *texture, F32 x, F32 y, UIActivationFunc clickedF, UserData clickedData MEM_DBG_PARMS);
#define ui_ButtonCreateImageOnly(texture, x, y, clickedF, clickedData) ui_ButtonCreateImageOnlyEx(texture, x, y, clickedF, clickedData MEM_DBG_PARMS_INIT)
void ui_ButtonInitialize(SA_PRE_NN_FREE SA_POST_NN_VALID UIButton *button, SA_PARAM_OP_STR const char *text, F32 x, F32 y, UIActivationFunc clickedF, UserData clickedData MEM_DBG_PARMS);
void ui_ButtonFreeInternal(SA_PRE_NN_VALID SA_POST_P_FREE UIButton *button);

#define ui_ButtonCreateAndAdd(button, window, text, x, y, clickedF, clickedData) ui_WindowAddChild(window, button = ui_ButtonCreate(text, x, y, clickedF, clickedData))
#define ui_ButtonCreateAndAddToExpander(button, expander, text, x, y, clickedF, clickedData) ui_ExpanderAddChild(expander, &((button = ui_ButtonCreate(text, x, y, clickedF, clickedData))->widget))

// Set the button's callback when clicked.
void ui_ButtonSetCallback(SA_PARAM_NN_VALID UIButton *button, UIActivationFunc clickedF, UserData clickedData);
void ui_ButtonSetDownCallback(SA_PARAM_NN_VALID UIButton *button, UIActivationFunc downF, UserData downData);
void ui_ButtonSetUpCallback(SA_PARAM_NN_VALID UIButton *button, UIActivationFunc upF, UserData upData);

// Set the button's callback to execute the given command.
void ui_ButtonSetCommand(SA_PARAM_NN_VALID UIButton *button, SA_PARAM_NN_STR const char *cmd);
void ui_ButtonSetDownCommand(SA_PARAM_NN_VALID UIButton *button, SA_PARAM_NN_STR const char *cmd);
void ui_ButtonSetUpCommand(SA_PARAM_NN_VALID UIButton *button, SA_PARAM_NN_STR const char *cmd);

// Click this button.
void ui_ButtonClick(SA_PARAM_NN_VALID UIButton *button);
void ui_ButtonDown(SA_PARAM_NN_VALID UIButton *button);
void ui_ButtonUp(SA_PARAM_NN_VALID UIButton *button);

void ui_ButtonSetTextAndIconAndResize(SA_PARAM_NN_VALID UIButton *pButton, SA_PARAM_NN_STR const char *pText, SA_PARAM_OP_STR const char *pIcon);

void ui_ButtonSetText(SA_PARAM_NN_VALID UIButton *button, SA_PARAM_OP_STR const char *text);
void ui_ButtonSetTextAndResize(SA_PARAM_NN_VALID UIButton *button, SA_PARAM_OP_STR const char *text);
void ui_ButtonSetMessage(UIButton* button, const char* text);
void ui_ButtonSetMessageAndResize(SA_PARAM_NN_VALID UIButton *button, SA_PARAM_OP_STR const char *text);
void ui_ButtonResize(SA_PARAM_NN_VALID UIButton* button);

const char* ui_ButtonGetText(SA_PARAM_NN_VALID UIButton *button);

void ui_ButtonAddChild(SA_PARAM_NN_VALID UIButton *button, SA_PARAM_OP_VALID UIWidget *child);
void ui_ButtonRemoveChild(SA_PARAM_NN_VALID UIButton *button, SA_PARAM_OP_VALID UIWidget *child);

bool ui_ButtonInput(SA_PARAM_NN_VALID UIButton *button, SA_PARAM_NN_VALID KeyInput *input);
void ui_ButtonTick(SA_PARAM_NN_VALID UIButton *button, UI_PARENT_ARGS);
void ui_ButtonDraw(SA_PARAM_NN_VALID UIButton *button, UI_PARENT_ARGS);

Color ui_WidgetButtonColor(SA_PARAM_NN_VALID UIWidget *widget, bool down, bool pressed); // For anything wanting to be colored like a button

// Sets a tooltip that includes the command for the button, if this is
// a command button.
void ui_ButtonSetTooltip(UIButton *pButton, const char *pchTooltip);

// Create a button for this command, with a controller graphic if a
// controller is being used.
UIButton *ui_ButtonCreateForCommand(const char *pchText, const char *pchCommand);

// Clears the sprite set for the button if any
void ui_ButtonClearImage(UIButton *pButton);

// Sets a new sprite for the button
void ui_ButtonSetImage(UIButton *pButton, const char *pTexture);

// Set a sprite for each of the button's states
void ui_ButtonSetImageEx(UIButton* pButton, const char* normalTex, const char* highlightTex, const char* focusedTex, const char* pressedTex, const char* disabledTex );

// If set to true, the image is stretched to take up all the button's space
void ui_ButtonSetImageStretch(UIButton* pButton, bool value);

#endif
