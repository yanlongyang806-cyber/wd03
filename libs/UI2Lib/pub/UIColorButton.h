/***************************************************************************



***************************************************************************/

#ifndef UI_COLORBUTTON_H
#define UI_COLORBUTTON_H
GCC_SYSTEM

#include "UICore.h"
#include "UIButton.h"
#include "UIWindow.h"

typedef struct UIWindow UIWindow;
typedef struct UIColorButton UIColorButton;
typedef struct UIColorSlider UIColorSlider;
typedef struct UILabel UILabel;
typedef struct UICheckButton UICheckButton;
typedef struct UITextEntry UITextEntry;
typedef struct UISprite UISprite;
typedef struct UIColorSet UIColorSet;

// You probably want to skip ahead to UIColorButton now.

//////////////////////////////////////////////////////////////////////////
// A color palette. Clicking on it will result in the color changing and the
// callback being called. This only supports real colors.
typedef struct UIColorPalette
{
	UIWidget widget;
	AtlasTex *sprite;
	Color color;
	UIActivationFunc chooseF;
	UserData chooseData;
} UIColorPalette;

SA_RET_NN_VALID UIColorPalette *ui_ColorPaletteCreate(F32 x, F32 y);
void ui_ColorPaletteInitialize(SA_PRE_NN_FREE SA_POST_NN_VALID UIColorPalette *palette, F32 x, F32 y);
void ui_ColorPaletteFreeInternal(SA_PRE_NN_VALID SA_POST_P_FREE UIColorPalette *palette);

Color ui_ColorPaletteGetColor(SA_PARAM_NN_VALID UIColorPalette *palette);
void ui_ColorPaletteSetColor(SA_PARAM_NN_VALID UIColorPalette *palette, Color color);
void ui_ColorPaletteSetColorAndCallback(SA_PARAM_NN_VALID UIColorPalette *palette, Color color);

void ui_ColorPaletteSetChooseCallback(SA_PARAM_NN_VALID UIColorPalette *palette, UIActivationFunc chooseF, UserData chooseData);

void ui_ColorPaletteTick(SA_PARAM_NN_VALID UIColorPalette *palette, UI_PARENT_ARGS);
void ui_ColorPaletteDraw(SA_PARAM_NN_VALID UIColorPalette *palette, UI_PARENT_ARGS);

//////////////////////////////////////////////////////////////////////////
// A window to choose a color. It contains a color palette, various
// sliders, and a button that is connected to a callback which receives
// the UIWindow as its user data (and a UIButton as its source).

typedef struct UIColorWindow
{
	UI_INHERIT_FROM(UI_WIDGET_TYPE UI_WINDOW_TYPE);

	UIColorSlider *pRedSlider, *pGreenSlider, *pBlueSlider;
	UIColorSlider *pHueSlider, *pSaturationSlider, *pValueSlider;
	UIColorSlider *pAlphaSlider, *pLuminanceSlider;
	UITextEntry *pRedEntry, *pGreenEntry, *pBlueEntry;
	UITextEntry	*pHueEntry, *pSaturationEntry, *pValueEntry;
	UITextEntry *pAlphaEntry, *pLuminanceEntry;
	UILabel *pRedLabel, *pGreenLabel, *pBlueLabel;
	UILabel	*pHueLabel, *pSaturationLabel, *pValueLabel;
	UILabel *pAlphaLabel, *pLuminanceLabel;
	UILabel *pBroken;
	UISprite *pPreview;
	Vec4 v4Color;
	Vec4 v4OrigColor;
	UICheckButton *pFloatRGB;
	UICheckButton *pAllowHDR;
	UICheckButton *pLimitColors;
	UIColorButton *pButton;
	UIColorPalette *pPalette;
	bool bLiveUpdate;
	bool bForceHDR;
	bool bOrphaned;
	F32 min;
	F32 max;
	UIColorSet *pColorSet;
	UIColorButton **eaButtons;
} UIColorWindow;

SA_RET_NN_VALID UIColorWindow *ui_ColorWindowCreate(SA_PARAM_OP_VALID UIColorButton *cbutton, F32 x, F32 y, F32 min, F32 max, Vec4 orig_color, bool no_alpha, bool no_rgb, bool no_hsv, bool live_update, bool force_hdr);
void ui_ColorWindowFreeInternal(SA_PRE_NN_VALID SA_POST_P_FREE UIColorWindow *window);

void ui_ColorWindowSetColorAndCallback(SA_PARAM_NN_VALID UIColorWindow *window, SA_PRE_NN_RELEMS(4) const Vec4 color);
void ui_ColorWindowGetColor(SA_PARAM_NN_VALID UIColorWindow *window, SA_PRE_NN_ELEMS(4) Vec4 color);

void ui_ColorWindowSetColorSet(SA_PARAM_NN_VALID UIColorWindow *window, UIColorSet *pColorSet);
void ui_ColorWindowSetLimitColors(SA_PARAM_NN_VALID UIColorWindow *window, bool bLimit);

void ui_ColorWindowOrphanAll();

//////////////////////////////////////////////////////////////////////////
// A button that brings up a ColorWindow when clicked.

typedef void (*UIColorChangeFunc)(UIAnyWidget *, bool finished, UserData);

typedef struct UIColorButton
{
	UI_INHERIT_FROM(UI_WIDGET_TYPE UI_BUTTON_TYPE);
	UIColorWindow *activeWindow;
	UIWidgetGroup *parentGroup; // Parent group to put the pop-up window in
	Vec4 color;
	UIColorChangeFunc changedF;
	UserData changedData;
	F32 min;
	F32 max;
	bool noAlpha;
	bool noRGB;
	bool noHSV;
	bool liveUpdate;
	bool bIsModal;
	bool bForceHDR;
	char title[255];
} UIColorButton;

// Unless you need HDR support, 'max' should be 1.0 (e.g. RGB values of 0 to 255, or 0.0 to 1.0).
#define ui_ColorButtonCreate(x,y,initial) ui_ColorButtonCreateEx(x,y,0,1,initial)
SA_RET_NN_VALID UIColorButton *ui_ColorButtonCreateEx(F32 x, F32 y, F32 min, F32 max, SA_PRE_NN_RELEMS(4) const Vec4 initial);
void ui_ColorButtonInitialize(SA_PRE_NN_FREE SA_POST_NN_VALID UIColorButton *cbutton, F32 x, F32 y, F32 min, F32 max, const Vec4 initial);

void ui_ColorButtonSetChangedCallback(SA_PARAM_NN_VALID UIColorButton *cbutton, UIColorChangeFunc changedF, UserData changedData);
void ui_ColorButtonClick(SA_PARAM_NN_VALID UIColorButton *cbutton, UserData dummy);

// Cancels the window (if any is open)
void ui_ColorButtonCancelWindow(SA_PARAM_NN_VALID UIColorButton *cbutton);

void ui_ColorButtonGetColor(SA_PARAM_NN_VALID UIColorButton *cbutton, SA_PRE_NN_ELEMS(4) SA_POST_OP_VALID Vec4 color);
U32 ui_ColorButtonGetColorInt(SA_PARAM_NN_VALID UIColorButton *cbutton);
void ui_ColorButtonSetColor(SA_PARAM_NN_VALID UIColorButton *cbutton, SA_PRE_NN_RELEMS(4) const Vec4 color);
void ui_ColorButtonSetColorAndCallback(SA_PARAM_NN_VALID UIColorButton *cbutton, SA_PRE_NN_RELEMS(4) const Vec4 color);

void ui_ColorButtonSetTitle(SA_PARAM_NN_VALID UIColorButton *cbutton, const char* title);

void ui_ColorButtonDraw(SA_PARAM_NN_VALID UIColorButton *cbutton, UI_PARENT_ARGS);

#endif
