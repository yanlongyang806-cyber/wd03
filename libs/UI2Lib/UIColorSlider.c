/***************************************************************************



***************************************************************************/



#include "Color.h"

#include "GfxSprite.h"
#include "GfxTexAtlas.h"

#include "inputMouse.h"
#include "inputText.h"

#include "rgb_hsv.h"

#include "UIColorSlider.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

bool ui_ColorSliderInput(UIColorSlider *cslider, KeyInput *key)
{
	if (key->type == KIT_EditKey)
	{
		Vec3 step, diff, newcurrent;
		subVec3(cslider->max, cslider->min, diff);
		scaleVec3(diff, 1.f/20, step);

		switch (key->scancode)
		{
		case INP_LEFT:
			subVec3(cslider->current, step, newcurrent);
			break;
		case INP_RIGHT:
			addVec3(cslider->current, step, newcurrent);
			break;
		default:
			return false;
		}
		ui_ColorSliderSetValueAndCallback(cslider, newcurrent);
		return true;
	}
	else
		return false;
}

UIColorSlider *ui_ColorSliderCreate(F32 x, F32 y, F32 width, Vec3 min, Vec3 max, bool hsv)
{
	UIColorSlider *cslider = (UIColorSlider *)calloc(1, sizeof(UIColorSlider));
	ui_ColorSliderInitialize(cslider, x, y, width, min, max, hsv);
	return cslider;
}

void ui_ColorSliderInitialize(UIColorSlider *cslider, F32 x, F32 y, F32 width, Vec3 min, Vec3 max, bool hsv) 
{
	ui_WidgetInitialize(UI_WIDGET(cslider), ui_ColorSliderTick, ui_ColorSliderDraw, ui_ColorSliderFreeInternal, ui_ColorSliderInput, ui_WidgetDummyFocusFunc);
	ui_WidgetSetPosition(UI_WIDGET(cslider), x, y);
	ui_WidgetSetDimensions(UI_WIDGET(cslider), width, 16);
	ui_ColorSliderSetRange(cslider, min, max);
	ui_ColorSliderSetValueAndCallback(cslider, min);
	cslider->hsv = hsv;
	
	cslider->clamp = true;
}

void ui_ColorSliderFreeInternal(UIColorSlider *cslider)
{
	ui_WidgetFreeInternal(UI_WIDGET(cslider));
}

void ui_ColorSliderSetRange(UIColorSlider *cslider, Vec3 min, Vec3 max)
{
	copyVec3(min, cslider->min);
	copyVec3(max, cslider->max);
}

void ui_ColorSliderSetValue(UIColorSlider *cslider, Vec3 value)
{
	if (cslider->clamp)
	{
		cslider->current[0] = CLAMP(value[0], cslider->min[0], cslider->max[0]);
		cslider->current[1] = CLAMP(value[1], cslider->min[1], cslider->max[1]);
		cslider->current[2] = CLAMP(value[2], cslider->min[2], cslider->max[2]);
	}
	else
	{
		cslider->current[0] = value[0];
		cslider->current[1] = value[1];
		cslider->current[2] = value[2];
	}
}

void ui_ColorSliderSetValueAndCallback(UIColorSlider *cslider, Vec3 value)
{
	ui_ColorSliderSetValue(cslider, value);
	if (cslider->changedF)
		cslider->changedF(cslider, cslider->changedData);
}

void ui_ColorSliderSetChangedCallback(UIColorSlider *cslider, UIActivationFunc changedF, UserData changedData)
{
	cslider->changedF = changedF;
	cslider->changedData = changedData;
}

void ui_ColorSliderGetValue(UIColorSlider *cslider, Vec3 value)
{
	value[0] = cslider->current[0];
	value[1] = cslider->current[1];
	value[2] = cslider->current[2];
}

void ui_ColorSliderTick(UIColorSlider *cslider, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(cslider);
	UI_TICK_EARLY(cslider, false, true);
	box.ly += UI_HSTEP_SC;
	box.hy -= UI_HSTEP_SC;
	if ((mouseDownHit(MS_LEFT, &box) || cslider->dragging) && mouseIsDown(MS_LEFT))
	{
		Vec3 delta, current;
		F32 value = (g_ui_State.mouseX - x) / w;
		ui_SetFocus(cslider);
		subVec3(cslider->max, cslider->min, delta);
		scaleVec3(delta, value, current);
		addVec3(current, cslider->min, current);
		ui_ColorSliderSetValueAndCallback(cslider, current);
		cslider->dragging = true;
		inpHandled();
	}
	else if (!mouseIsDown(MS_LEFT))
	{
		if (cslider->dragging)
			ui_ColorSliderSetValueAndCallback(cslider, cslider->current);
		cslider->dragging = false;
	}
}

#define HDR_STEPS 6

void ui_ColorSliderDraw(UIColorSlider *cslider, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(cslider);
	AtlasTex *bg = atlasLoadTexture("white");
	AtlasTex *selector = atlasLoadTexture("ColorSlider_selector");
	Vec3 delta;
	F32 value = 0.f, xscale, yscale;
	U32 i, rgbaMin, rgbaMax;
	CBoxClipTo(&pBox, &box);

	subVec3(cslider->max, cslider->min, delta);
	for (i = 0; i < 3; i++)
		if (delta[i] != 0)
			value = (cslider->current[i] - cslider->min[i]) / delta[i];
	value = CLAMP(value, 0.0, 1.0);

	xscale = w / bg->width;
	yscale = (h - UI_STEP_SC) / bg->height;
	y += UI_HSTEP_SC;
	if (cslider->hsv)
	{
		xscale *= 1.f / HDR_STEPS;
		for (i = 0; i < HDR_STEPS; i++)
		{
			Vec3 hsv;
			Vec3 rgb;
			Color c;
			
			scaleVec3(delta, i/(F32)HDR_STEPS, hsv);
			addVec3(hsv, cslider->min, hsv);
			MIN1(hsv[2], 1);
			hsvToRgb(hsv, rgb);
			vec3ToColor(&c, rgb);
			rgbaMin = RGBAFromColor(c);
			scaleVec3(delta, (i + 1)/(F32)HDR_STEPS, hsv);
			addVec3(hsv, cslider->min, hsv);
			MIN1(hsv[2], 1);
			hsvToRgb(hsv, rgb);
			vec3ToColor(&c, rgb);
			rgbaMax = RGBAFromColor(c);
			display_sprite_4Color(bg, x + i * w / HDR_STEPS, y, z, xscale, yscale, rgbaMin, rgbaMax, rgbaMax, rgbaMin, false);
		}
	}
	else if (cslider->lum)
	{
		Vec3 current, min, mid, max, rgb;
		F32 totalArea, bgAreaSoFar, bgAreaMultiplier, areaSoFar, bgAreaSoFarMultiplier;
		Color c;
		copyVec3(cslider->min, min);
		copyVec3(cslider->current, current);
		copyVec3(cslider->max, max);
		min[0] = max[0] = current[0];
		totalArea = max[2]-min[2];
		current[2] = 0.5;
		hslToHsvSmartS(current, mid, max[2]);
		copyVec3(min, current);
		hslToHsvSmartS(current, min, max[2]);
		copyVec3(max, current);
		hslToHsvSmartS(current, max, max[2]);
		bgAreaMultiplier = xscale / totalArea;
		bgAreaSoFarMultiplier = w / totalArea;
		bgAreaSoFar = 0;
		areaSoFar = 0;

		copyVec3(min, current);
		MIN1(current[2], 1);
		hsvToRgb(current, rgb);
		vec3ToColor(&c, rgb);
		rgbaMin = rgbaMax = RGBAFromColor(c);

		//Negative inf to 0
		if (min[2] < 0)
		{
			areaSoFar = -min[2];
			bgAreaSoFar = areaSoFar * bgAreaSoFarMultiplier;
			display_sprite_4Color(bg, x, y, z, areaSoFar * bgAreaMultiplier, yscale, rgbaMin, rgbaMax, rgbaMax, rgbaMin, false);
		}

		//0 to 0.5
		MIN1(mid[2], 1);
		hsvToRgb(mid, rgb);
		vec3ToColor(&c, rgb);
		rgbaMax = RGBAFromColor(c);
		display_sprite_4Color(bg, x + bgAreaSoFar, y, z, 0.5*bgAreaMultiplier, yscale, rgbaMin, rgbaMax, rgbaMax, rgbaMin, false);
		bgAreaSoFar += 0.5*bgAreaSoFarMultiplier;
		areaSoFar += 0.5;

		//0.5 to 1
		rgbaMin = rgbaMax;
		copyVec3(max, current);
		MIN1(current[2], 1);
		hsvToRgb(current, rgb);
		vec3ToColor(&c, rgb);
		rgbaMax = RGBAFromColor(c);
		display_sprite_4Color(bg, x + bgAreaSoFar, y, z, 0.5*bgAreaMultiplier, yscale, rgbaMin, rgbaMax, rgbaMax, rgbaMin, false);
		bgAreaSoFar += 0.5*bgAreaSoFarMultiplier;
		areaSoFar += 0.5;

		//1 to inf
		if (max[2] > 1)
		{
			rgbaMin = rgbaMax;
			display_sprite_4Color(bg, x + bgAreaSoFar, y, z, xscale - (areaSoFar*bgAreaMultiplier), yscale, rgbaMin, rgbaMax, rgbaMax, rgbaMin, false);
		}
	}
	else
	{
		xscale *= 1.f / HDR_STEPS;
		for (i = 0; i < HDR_STEPS; i++)
		{
			Vec3 rgb;
			Color c;

			scaleVec3(delta, i/(F32)HDR_STEPS, rgb);
			addVec3(rgb, cslider->min, rgb);
			vec3ToColor(&c, rgb);
			rgbaMin = RGBAFromColor(c);
			scaleVec3(delta, (i + 1)/(F32)HDR_STEPS, rgb);
			addVec3(rgb, cslider->min, rgb);
			vec3ToColor(&c, rgb);
			rgbaMax = RGBAFromColor(c);
			display_sprite_4Color(bg, x + i * w / HDR_STEPS, y, z, xscale, yscale, rgbaMin, rgbaMax, rgbaMax, rgbaMin, false);
		}
	}

	yscale = (h - UI_STEP_SC) / selector->height;
	display_sprite(selector, x + value * w - selector->width/2, y, z + 0.01, 1.0f, yscale, 0x000000ff);
}

#undef HDR_STEPS
