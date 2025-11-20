/***************************************************************************



***************************************************************************/


#include "inputMouse.h"

#include "Color.h"
#include "GfxClipper.h"
#include "GfxSprite.h"

#include "UISpinner.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

UISpinner *ui_SpinnerCreate(F32 x, F32 y, F32 minValue, F32 maxValue, F32 step, F32 currentValue, UIActivationFunc changedF, UserData changedData)
{
	UISpinner *spinner = calloc(1, sizeof(UISpinner));
	ui_SpinnerInitialize(spinner, x, y, minValue, maxValue, step, currentValue, changedF, changedData);
	return spinner;
}

void ui_SpinnerInitialize(UISpinner *spinner, F32 x, F32 y, F32 minValue, F32 maxValue, F32 step, F32 currentValue, UIActivationFunc changedF, UserData changedData)
{
	ui_WidgetInitialize(UI_WIDGET(spinner), ui_SpinnerTick, ui_SpinnerDraw, ui_SpinnerFreeInternal, NULL, ui_WidgetDummyFocusFunc);
	ui_WidgetSetPosition(UI_WIDGET(spinner), x, y);
	ui_SpinnerSetChangedCallback(spinner, changedF, changedData);
	ui_SpinnerSetBounds(spinner, minValue, maxValue, step);
	ui_SpinnerSetValue(spinner, currentValue);
}

void ui_SpinnerFreeInternal(UISpinner *spinner)
{
	ui_WidgetFreeInternal(UI_WIDGET(spinner));
}

void ui_SpinnerSetChangedCallback(UISpinner *spinner, UIActivationFunc changedF, UserData changedData)
{
	spinner->changedF = changedF;
	spinner->changedData = changedData;
}

void ui_SpinnerSetStartSpinCallback(UISpinner *spinner, UIActivationFunc startSpinF, UserData startSpinData)
{
	spinner->startSpinF = startSpinF;
	spinner->startSpinData = startSpinData;
}

void ui_SpinnerSetStopSpinCallback(UISpinner *spinner, UIActivationFunc stopSpinF, UserData stopSpinData)
{
	spinner->stopSpinF = stopSpinF;
	spinner->stopSpinData = stopSpinData;
}

void ui_SpinnerUp(UISpinner *spinner)
{
	ui_SpinnerSetValueAndCallback(spinner, spinner->currentVal + spinner->step);
}

void ui_SpinnerDown(UISpinner *spinner)
{
	ui_SpinnerSetValueAndCallback(spinner, spinner->currentVal - spinner->step);
}

void ui_SpinnerSetValue(UISpinner *spinner, F32 value)
{
	spinner->currentVal = CLAMP(value, spinner->min, spinner->max);
}

void ui_SpinnerSetValueAndCallback(UISpinner *spinner, F32 value)
{
	F32 currentVal = spinner->currentVal;
	ui_SpinnerSetValue(spinner, value);
	if (spinner->changedF && currentVal != spinner->currentVal)
		spinner->changedF(spinner, spinner->changedData);
}

F32 ui_SpinnerGetValue(UISpinner *spinner)
{
	return spinner->currentVal;
}

void ui_SpinnerTick(UISpinner *spinner, UI_PARENT_ARGS)
{
	CBox halfBox;

	UI_GET_COORDINATES(spinner);

	UI_TICK_EARLY(spinner, false, true);

	halfBox = box;
	CBoxSetHeight(&halfBox, 0.5f * CBoxHeight(&box));

	if (mouseClickHit(MS_LEFT, &halfBox))
	{
		if (spinner->pressed && spinner->stopSpinF)
			spinner->stopSpinF(spinner, spinner->stopSpinData);
		spinner->pressed = false;
		ui_SetFocus(spinner);
		inpHandled();
		ui_SpinnerUp(spinner);
		return;
	}
	
	CBoxMoveY(&halfBox, halfBox.ly + CBoxHeight(&halfBox));
	if (mouseClickHit(MS_LEFT, &halfBox))
	{
		if (spinner->pressed && spinner->stopSpinF)
			spinner->stopSpinF(spinner, spinner->stopSpinData);
		spinner->pressed = false;
		ui_SetFocus(spinner);
		inpHandled();
		ui_SpinnerDown(spinner);
		return;
	}

	if (mouseDownHit(MS_LEFT, &box))
	{
		spinner->pressed = true;
		if (spinner->startSpinF)
			spinner->startSpinF(spinner, spinner->startSpinData);
		ui_SetFocus(spinner);
		inpHandled();
	}
	else if (!mouseIsDown(MS_LEFT) && spinner->pressed)
	{
		spinner->pressed = false;
		// Make callback, in case someone is waiting for the spinner to be released
		if (spinner->stopSpinF)
			spinner->stopSpinF(spinner, spinner->stopSpinData);
		ui_SetFocus(spinner);
		inpHandled();
	}

	if (spinner->pressed)
	{
		int dx, dy;
		mouseDiffLegacy(&dx, &dy);
		if (dy)
			ui_SpinnerSetValueAndCallback(spinner, spinner->currentVal + -dy * spinner->step);
		ui_SetCursorForDirection(UIBottom);
		ui_CursorLock();
	}

	UI_TICK_LATE(spinner);
}

void ui_SpinnerDraw(UISpinner *spinner, UI_PARENT_ARGS)
{
	CBox halfBox;
	Color c;
	AtlasTex *tex_up = NULL, *tex_dn = NULL;

	UI_GET_COORDINATES(spinner);

	UI_DRAW_EARLY(spinner);

	if (UI_GET_SKIN(spinner))
	{
		int state = 0;
		if (!ui_IsActive(UI_WIDGET(spinner)))
			state = 3;
		else if (ui_IsHovering(UI_WIDGET(spinner)) || ui_IsFocused(spinner))
			state = 1;
		c = UI_GET_SKIN(spinner)->button[state];
		tex_up = UI_TEXTURE(UI_GET_SKIN(spinner)->pchSpinnerArrowUp);
		tex_dn = UI_TEXTURE(UI_GET_SKIN(spinner)->pchSpinnerArrowDown);
	}
	else
	{
		if (ui_IsFocused(spinner))
			c = ColorLighten(spinner->widget.color[0], 50);
		else if (ui_IsHovering(UI_WIDGET(spinner)))
			c = ColorLighten(spinner->widget.color[0], 50);
		else
			c = spinner->widget.color[0];
	}

	if (!tex_up)
		tex_up = g_ui_Tex.arrowSmallUp;
	halfBox = box;
	CBoxSetHeight(&halfBox, 0.5f * CBoxHeight(&box));
	display_sprite_box(tex_up, &halfBox, z + 0.02, RGBAFromColor(c));

	if (!tex_dn)
		tex_dn = g_ui_Tex.arrowSmallDown;
	CBoxMoveY(&halfBox, halfBox.ly + CBoxHeight(&halfBox));
	display_sprite_box(tex_dn, &halfBox, z + 0.02, RGBAFromColor(c));

	UI_DRAW_LATE(spinner);
}

void ui_SpinnerSetBounds(UISpinner *spinner, F32 fMin, F32 fMax, F32 fStep)
{
	if (fStep == 0)
		fStep = (fMax - fMin) / 50.f;
	spinner->min = fMin;
	spinner->max = fMax;
	spinner->step = fStep;
	spinner->currentVal = CLAMP(spinner->currentVal, fMin, fMax);
}

void ui_SpinnerSetBoundsAndCallback(UISpinner *spinner, F32 fMin, F32 fMax, F32 fStep)
{
	ui_SpinnerSetBounds(spinner, fMin, fMax, fStep);
	if (spinner->changedF)
		spinner->changedF(spinner, spinner->changedData);
}
