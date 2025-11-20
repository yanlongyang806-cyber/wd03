/***************************************************************************



***************************************************************************/

#include "earray.h"

#include "input.h"
#include "inputLib.h"
#include "inputMouse.h"
#include "EditorPrefs.h"

#include "Color.h"
#include "GfxSprite.h"
#include "GfxSpriteText.h"
#include "GfxTexAtlas.h"
#include "GraphicsLib.h"
#include "StringUtil.h"

#include "UIColor.h"
#include "UIColorButton.h"
#include "UISprite.h"
#include "UIInternal.h"
#include "rgb_hsv.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

#define PAST_COLOR_CNT 10

void ui_ColorButtonFreeInternal(UIColorButton *cbutton);
static void ui_ColorButtonSetColorAndCallbackEx(UIColorButton *cbutton, bool finished, const Vec4 color);

//Orphaned windows do not try to change values on select or cancel, as it's likely that their pointers have become invalid
static UIColorWindow **g_OpenWindows = NULL;

void ui_ColorWindowOrphanAll()
{
	int i;
	for (i = 0; i < eaSize(&g_OpenWindows); i++)
	{
		g_OpenWindows[i]->bOrphaned = true;
	}
}

static Color getPixelColor(U8 alpha, F32 x, F32 y)
{
#if !PLATFORM_CONSOLE
	HDC hdc;
	hdc = GetDC(NULL); // Get the DrawContext of the desktop
	if (hdc) {
		COLORREF cr;
		POINT p;
		Color c;
		p.x = x;
		p.y = y;
		ClientToScreen(gInput->hwnd, &p);
		cr = GetPixel(hdc, p.x, p.y);
		ReleaseDC(NULL, hdc);
		setColorFromABGR(&c, cr);
		c.a = alpha;
		return c;
	}
#endif
	return ColorBlack;
}

void ui_ColorPaletteTick(UIColorPalette *palette, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(palette);
	UI_TICK_EARLY(palette, false, true);
	if (mouseDownHit(MS_LEFT, &box) || (inpLevelPeek(INP_SHIFT) && !gfxIsInactiveApp()))
	{
		ui_ColorPaletteSetColorAndCallback(palette, getPixelColor(0xFF, g_ui_State.mouseX, g_ui_State.mouseY));
		inpHandled();
	}
}

void ui_ColorPaletteSetColor(UIColorPalette *palette, Color c)
{
	palette->color = c;
}

void ui_ColorPaletteSetColorAndCallback(UIColorPalette *palette, Color c)
{
	ui_ColorPaletteSetColor(palette, c);
	if (palette->chooseF)
		palette->chooseF(palette, palette->chooseData);
}

void ui_ColorPaletteDraw(UIColorPalette *palette, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(palette);
	display_sprite_box(palette->sprite, &box, z, RGBAFromColor(ColorWhite));
}

UIColorPalette *ui_ColorPaletteCreate(F32 x, F32 y)
{
	UIColorPalette *palette = (UIColorPalette *)calloc(1, sizeof(UIColorPalette));
	ui_ColorPaletteInitialize(palette, x, y);
	return palette;
}

void ui_ColorPaletteInitialize(UIColorPalette *palette, F32 x, F32 y)
{
	AtlasTex *spr = atlasLoadTexture("ColorPicker.tga");
	ui_WidgetInitialize(UI_WIDGET(palette), ui_ColorPaletteTick, ui_ColorPaletteDraw, ui_ColorPaletteFreeInternal, NULL, NULL);
	ui_WidgetSetPosition(UI_WIDGET(palette), x, y);
	palette->sprite = spr;
	palette->widget.height = spr->height;
	palette->widget.width = spr->width;
	palette->color = ColorBlack;
}

void ui_ColorPaletteFreeInternal(UIColorPalette *palette)
{
	ui_WidgetFreeInternal(UI_WIDGET(palette));
}

void ui_ColorPaletteSetChooseCallback(UIColorPalette *palette, UIActivationFunc chooseF, UserData chooseData)
{
	palette->chooseF = chooseF;
	palette->chooseData = chooseData;
}

Color ui_ColorPaletteGetColor(UIColorPalette *palette)
{
	return palette->color;
}

static UpdatePreview(UIColorWindow *window)
{
	Color c;
	Vec3 current;
	UISprite *preview = window->pPreview;
	char value[10];

	if (window->pRedSlider)
		copyVec3(window->pRedSlider->current, current);
	else if (window->pHueSlider)
		hsvToRgb(window->pHueSlider->current, current);
	else
		setVec3same(current, 1);

	vec3ToColor(&c, current);

	if (window->pAlphaSlider)
	{
		c.a = CLAMP(255 * window->pAlphaSlider->current[0], 0, 255);
	}
	else
	{
		c.a = 255;
	}

	preview->tint = c;

	copyVec3(current, window->v4Color);
	if (window->pAlphaSlider)
	{
		window->v4Color[3] = window->pAlphaSlider->current[0];
	}
	else
	{
		window->v4Color[3] = 1;
	}

	// Disable scaling on inappropriate axes.
	if (window->pRedSlider)
	{
		window->pRedSlider->min[1] = window->pRedSlider->max[1] = window->pRedSlider->current[1];
		window->pRedSlider->min[2] = window->pRedSlider->max[2] = window->pRedSlider->current[2];
		window->pGreenSlider->min[0] = window->pGreenSlider->max[0] = window->pGreenSlider->current[0];
		window->pGreenSlider->min[2] = window->pGreenSlider->max[2] = window->pGreenSlider->current[2];
		window->pBlueSlider->min[0] = window->pBlueSlider->max[0] = window->pBlueSlider->current[0];
		window->pBlueSlider->min[1] = window->pBlueSlider->max[1] = window->pBlueSlider->current[1];

		// Reset the text entries.
		if (window->pFloatRGB && ui_CheckButtonGetState(window->pFloatRGB))
		{
			sprintf(value, "%0.2f", window->pRedSlider->current[0]);
			ui_TextEntrySetText(window->pRedEntry, value);
			sprintf(value, "%0.2f", window->pGreenSlider->current[1]);
			ui_TextEntrySetText(window->pGreenEntry, value);
			sprintf(value, "%0.2f", window->pBlueSlider->current[2]);
			ui_TextEntrySetText(window->pBlueEntry, value);
		}
		else
		{
			sprintf(value, "%d", (U32)round(window->pRedSlider->current[0] * 255.0));
			ui_TextEntrySetText(window->pRedEntry, value);
			sprintf(value, "%d", (U32)round(window->pGreenSlider->current[1] * 255.0));
			ui_TextEntrySetText(window->pGreenEntry, value);
			sprintf(value, "%d", (U32)round(window->pBlueSlider->current[2] * 255.0));
			ui_TextEntrySetText(window->pBlueEntry, value);
		}
	}

	if (window->pHueSlider)
	{
		window->pHueSlider->min[1] = window->pHueSlider->max[1] = window->pHueSlider->current[1];
		window->pHueSlider->min[2] = window->pHueSlider->max[2] = window->pHueSlider->current[2];
		window->pSaturationSlider->min[0] = window->pSaturationSlider->max[0] = window->pSaturationSlider->current[0];
		window->pSaturationSlider->min[2] = window->pSaturationSlider->max[2] = window->pSaturationSlider->current[2];
		window->pValueSlider->min[0] = window->pValueSlider->max[0] = window->pValueSlider->current[0];
		window->pValueSlider->min[1] = window->pValueSlider->max[1] = window->pValueSlider->current[1];

		// Reset the text entries.
		sprintf(value, "%0.1f", window->pHueSlider->current[0]);
		ui_TextEntrySetText(window->pHueEntry, value);
		sprintf(value, "%0.2f", window->pSaturationSlider->current[1]);
		ui_TextEntrySetText(window->pSaturationEntry, value);
		sprintf(value, "%0.2f", window->pValueSlider->current[2]);
		ui_TextEntrySetText(window->pValueEntry, value);
	}

	if (window->pAlphaSlider)
	{
		// Reset the text entries.
		if (window->pFloatRGB && ui_CheckButtonGetState(window->pFloatRGB))
			sprintf(value, "%0.2f", window->pAlphaSlider->current[0]);
		else
			sprintf(value, "%d", (U32)round(window->pAlphaSlider->current[0] * 255.0));
		ui_TextEntrySetTextAndCallback(window->pAlphaEntry, value);
	}

	// Reset the text entries.
	sprintf(value, "%0.2f", window->pLuminanceSlider->current[2]);
	ui_TextEntrySetTextAndCallback(window->pLuminanceEntry, value);

	if (current[0] > 1 || current[0] < 0 || current[1] > 1 || current[1] < 0 || current[2] > 1 || current[2] < 0)
		ui_LabelSetText(window->pBroken, "Preview (HDR)");
	else
		ui_LabelSetText(window->pBroken, "Preview");

	if (window->bLiveUpdate && window->pButton)
		ui_ColorButtonSetColorAndCallbackEx(window->pButton, false, window->v4Color);
}

F32 luminanceFromRGB(const Vec3 rgb, UIColorWindow *window)
{
	F32 retVal = (rgb[0] * 0.299) + (rgb[1] * 0.587) + (rgb[2] * 0.114);
	/*
	//I don't really understand why, so I'm leaving this here, but it seems like this conversion is not necessary.
	bool bFloatMode = window->pFloatRGB ? ui_CheckButtonGetState(window->pFloatRGB) : false;
	if ((!bFloatMode) && (window->pRedSlider))
	{
		retVal *= U8TOF32_COLOR;
	}
	*/
	return retVal;
}

static void ColorWindowPastColorButtonCallback(UIColorButton *button, UIColorWindow *window)
{
	Vec4 color;
	ui_ColorButtonGetColor(button, color);
	if (!window->pAllowHDR && !window->bForceHDR)
		CLAMPVEC4(color, 0.0f, 1.0f);
	if (window->pRedSlider)
	{
		copyVec3(color, window->pRedSlider->current);
		copyVec3(color, window->pGreenSlider->current);
		copyVec3(color, window->pBlueSlider->current);
	}
	if (window->pHueSlider)
	{
		Vec3 hsv;
		rgbToHsv(color, hsv);
		copyVec3(hsv, window->pHueSlider->current);
		copyVec3(hsv, window->pSaturationSlider->current);
		copyVec3(hsv, window->pValueSlider->current);
	}
	if (window->pAlphaSlider)
	{
		setVec3same(window->pAlphaSlider->current, color[3]);
	}
	if (window->pLuminanceSlider)
	{
		Vec3 hsv, hsl;
		rgbToHsv(color, hsv);
		hsvToHsl(hsv, hsl);
		hsl[2] = luminanceFromRGB(color, window);
		copyVec3(hsl, window->pLuminanceSlider->current);
	}
	UpdatePreview(window);
}

static void ToggleLimitMode(UICheckButton *pCheck, UIColorWindow *pWindow)
{
	int i, x = 0, y = 0;
	UIColorButton *color_button;

	for (i = eaSize(&pWindow->eaButtons)-1; i >= 0; --i)
	{
		ui_WindowRemoveChild(UI_WINDOW(pWindow),UI_WIDGET(pWindow->eaButtons[i]));
		ui_WidgetQueueFree(UI_WIDGET(pWindow->eaButtons[i]));
	}
	eaClear(&pWindow->eaButtons);

	if (ui_CheckButtonGetState(pWindow->pLimitColors) && pWindow->pColorSet)
	{
		if (pWindow->pPalette) ui_WidgetRemoveChild(UI_WIDGET(pWindow), UI_WIDGET(pWindow->pPalette));
		if (pWindow->pFloatRGB) ui_WidgetRemoveChild(UI_WIDGET(pWindow), UI_WIDGET(pWindow->pFloatRGB));
		if (pWindow->pAllowHDR) ui_WidgetRemoveChild(UI_WIDGET(pWindow), UI_WIDGET(pWindow->pAllowHDR));
		if (pWindow->pRedLabel) ui_WidgetRemoveChild(UI_WIDGET(pWindow), UI_WIDGET(pWindow->pRedLabel));
		if (pWindow->pRedEntry) ui_WidgetRemoveChild(UI_WIDGET(pWindow), UI_WIDGET(pWindow->pRedEntry));
		if (pWindow->pRedSlider) ui_WidgetRemoveChild(UI_WIDGET(pWindow), UI_WIDGET(pWindow->pRedSlider));
		if (pWindow->pGreenLabel) ui_WidgetRemoveChild(UI_WIDGET(pWindow), UI_WIDGET(pWindow->pGreenLabel));
		if (pWindow->pGreenEntry) ui_WidgetRemoveChild(UI_WIDGET(pWindow), UI_WIDGET(pWindow->pGreenEntry));
		if (pWindow->pGreenSlider) ui_WidgetRemoveChild(UI_WIDGET(pWindow), UI_WIDGET(pWindow->pGreenSlider));
		if (pWindow->pBlueLabel) ui_WidgetRemoveChild(UI_WIDGET(pWindow), UI_WIDGET(pWindow->pBlueLabel));
		if (pWindow->pBlueEntry) ui_WidgetRemoveChild(UI_WIDGET(pWindow), UI_WIDGET(pWindow->pBlueEntry));
		if (pWindow->pBlueSlider) ui_WidgetRemoveChild(UI_WIDGET(pWindow), UI_WIDGET(pWindow->pBlueSlider));
		if (pWindow->pHueLabel) ui_WidgetRemoveChild(UI_WIDGET(pWindow), UI_WIDGET(pWindow->pHueLabel));
		if (pWindow->pHueEntry) ui_WidgetRemoveChild(UI_WIDGET(pWindow), UI_WIDGET(pWindow->pHueEntry));
		if (pWindow->pHueSlider) ui_WidgetRemoveChild(UI_WIDGET(pWindow), UI_WIDGET(pWindow->pHueSlider));
		if (pWindow->pSaturationLabel) ui_WidgetRemoveChild(UI_WIDGET(pWindow), UI_WIDGET(pWindow->pSaturationLabel));
		if (pWindow->pSaturationEntry) ui_WidgetRemoveChild(UI_WIDGET(pWindow), UI_WIDGET(pWindow->pSaturationEntry));
		if (pWindow->pSaturationSlider) ui_WidgetRemoveChild(UI_WIDGET(pWindow), UI_WIDGET(pWindow->pSaturationSlider));
		if (pWindow->pValueLabel) ui_WidgetRemoveChild(UI_WIDGET(pWindow), UI_WIDGET(pWindow->pValueLabel));
		if (pWindow->pValueEntry) ui_WidgetRemoveChild(UI_WIDGET(pWindow), UI_WIDGET(pWindow->pValueEntry));
		if (pWindow->pValueSlider) ui_WidgetRemoveChild(UI_WIDGET(pWindow), UI_WIDGET(pWindow->pValueSlider));
		if (pWindow->pAlphaLabel) ui_WidgetRemoveChild(UI_WIDGET(pWindow), UI_WIDGET(pWindow->pAlphaLabel));
		if (pWindow->pAlphaEntry) ui_WidgetRemoveChild(UI_WIDGET(pWindow), UI_WIDGET(pWindow->pAlphaEntry));
		if (pWindow->pAlphaSlider) ui_WidgetRemoveChild(UI_WIDGET(pWindow), UI_WIDGET(pWindow->pAlphaSlider));
		if (pWindow->pLuminanceLabel) ui_WidgetRemoveChild(UI_WIDGET(pWindow), UI_WIDGET(pWindow->pLuminanceLabel));
		if (pWindow->pLuminanceEntry) ui_WidgetRemoveChild(UI_WIDGET(pWindow), UI_WIDGET(pWindow->pLuminanceEntry));
		if (pWindow->pLuminanceSlider) ui_WidgetRemoveChild(UI_WIDGET(pWindow), UI_WIDGET(pWindow->pLuminanceSlider));

		x = 30;
		for (i = 0; i < eaSize(&pWindow->pColorSet->eaColors); ++i)
		{
			Vec4 color_buf = {pWindow->pColorSet->eaColors[i]->color[0]/255.0f, pWindow->pColorSet->eaColors[i]->color[1]/255.0f, pWindow->pColorSet->eaColors[i]->color[2]/255.0f, pWindow->pColorSet->eaColors[i]->color[3]/255.0f};
			color_button = ui_ColorButtonCreate(x, y, color_buf);
			ui_WidgetSetWidth(UI_WIDGET(color_button), 12.0);
			ui_WidgetSetHeight(UI_WIDGET(color_button), 12.0);
			UI_WIDGET(color_button)->offsetFrom = UITopLeft;
			ui_ButtonSetCallback(&color_button->button, ColorWindowPastColorButtonCallback, pWindow);
			ui_WindowAddChild(UI_WINDOW(pWindow), color_button);
			eaPush(&pWindow->eaButtons, color_button);
			x += 15;
			if (x > 15*25)
			{
				x = 30;
				y += 15;
			}
		}
	}
	else
	{
		if (pWindow->pPalette) ui_WidgetAddChild(UI_WIDGET(pWindow), UI_WIDGET(pWindow->pPalette));
		if (pWindow->pFloatRGB) ui_WidgetAddChild(UI_WIDGET(pWindow), UI_WIDGET(pWindow->pFloatRGB));
		if (pWindow->pAllowHDR) ui_WidgetAddChild(UI_WIDGET(pWindow), UI_WIDGET(pWindow->pAllowHDR));
		if (pWindow->pRedLabel) ui_WidgetAddChild(UI_WIDGET(pWindow), UI_WIDGET(pWindow->pRedLabel));
		if (pWindow->pRedEntry) ui_WidgetAddChild(UI_WIDGET(pWindow), UI_WIDGET(pWindow->pRedEntry));
		if (pWindow->pRedSlider) ui_WidgetAddChild(UI_WIDGET(pWindow), UI_WIDGET(pWindow->pRedSlider));
		if (pWindow->pGreenLabel) ui_WidgetAddChild(UI_WIDGET(pWindow), UI_WIDGET(pWindow->pGreenLabel));
		if (pWindow->pGreenEntry) ui_WidgetAddChild(UI_WIDGET(pWindow), UI_WIDGET(pWindow->pGreenEntry));
		if (pWindow->pGreenSlider) ui_WidgetAddChild(UI_WIDGET(pWindow), UI_WIDGET(pWindow->pGreenSlider));
		if (pWindow->pBlueLabel) ui_WidgetAddChild(UI_WIDGET(pWindow), UI_WIDGET(pWindow->pBlueLabel));
		if (pWindow->pBlueEntry) ui_WidgetAddChild(UI_WIDGET(pWindow), UI_WIDGET(pWindow->pBlueEntry));
		if (pWindow->pBlueSlider) ui_WidgetAddChild(UI_WIDGET(pWindow), UI_WIDGET(pWindow->pBlueSlider));
		if (pWindow->pHueLabel) ui_WidgetAddChild(UI_WIDGET(pWindow), UI_WIDGET(pWindow->pHueLabel));
		if (pWindow->pHueEntry) ui_WidgetAddChild(UI_WIDGET(pWindow), UI_WIDGET(pWindow->pHueEntry));
		if (pWindow->pHueSlider) ui_WidgetAddChild(UI_WIDGET(pWindow), UI_WIDGET(pWindow->pHueSlider));
		if (pWindow->pSaturationLabel) ui_WidgetAddChild(UI_WIDGET(pWindow), UI_WIDGET(pWindow->pSaturationLabel));
		if (pWindow->pSaturationEntry) ui_WidgetAddChild(UI_WIDGET(pWindow), UI_WIDGET(pWindow->pSaturationEntry));
		if (pWindow->pSaturationSlider)ui_WidgetAddChild(UI_WIDGET(pWindow), UI_WIDGET(pWindow->pSaturationSlider));
		if (pWindow->pValueLabel) ui_WidgetAddChild(UI_WIDGET(pWindow), UI_WIDGET(pWindow->pValueLabel));
		if (pWindow->pValueEntry) ui_WidgetAddChild(UI_WIDGET(pWindow), UI_WIDGET(pWindow->pValueEntry));
		if (pWindow->pValueSlider) ui_WidgetAddChild(UI_WIDGET(pWindow), UI_WIDGET(pWindow->pValueSlider));
		if (pWindow->pAlphaLabel) ui_WidgetAddChild(UI_WIDGET(pWindow), UI_WIDGET(pWindow->pAlphaLabel));
		if (pWindow->pAlphaEntry) ui_WidgetAddChild(UI_WIDGET(pWindow), UI_WIDGET(pWindow->pAlphaEntry));
		if (pWindow->pAlphaSlider) ui_WidgetAddChild(UI_WIDGET(pWindow), UI_WIDGET(pWindow->pAlphaSlider));
		if (pWindow->pLuminanceLabel) ui_WidgetAddChild(UI_WIDGET(pWindow), UI_WIDGET(pWindow->pLuminanceLabel));
		if (pWindow->pLuminanceEntry) ui_WidgetAddChild(UI_WIDGET(pWindow), UI_WIDGET(pWindow->pLuminanceEntry));
		if (pWindow->pLuminanceSlider) ui_WidgetAddChild(UI_WIDGET(pWindow), UI_WIDGET(pWindow->pLuminanceSlider));
	}
}

static void ToggleRGBMode(UICheckButton *pCheck, UIColorWindow *pWindow)
{
	UpdatePreview(pWindow);
}

static void ToggleHDRMode(UICheckButton *pCheck, UIColorWindow *pWindow)
{
	Vec3 mMin, hMin, mMax, hMax, aMin = {0.f, 0.f, 0.f}, aMax = {1.f, 1.f, 1.f}, lMin = {0.f, 0.f, 0.f}, lMax = {1.f, 1.f, 1.f};
	bool newAllowHDR;
	
	if (ui_CheckButtonGetState(pWindow->pAllowHDR))
	{
		setVec3(mMin, pWindow->min, pWindow->min, pWindow->min);
		setVec3(mMax, pWindow->max, pWindow->max, pWindow->max);
		setVec3(hMin, 0, 0, pWindow->min);
		setVec3(hMax, 360, 1, pWindow->max);
		newAllowHDR = true;
	}
	else
	{
		setVec3(mMin, 0, 0, 0);
		setVec3(mMax, 1, 1, 1);
		setVec3(hMin, 0, 0, 0);
		setVec3(hMax, 360, 1, 1);
		newAllowHDR = false;
	}

	if (pWindow->pRedSlider)
	{
		ui_ColorSliderSetRange(pWindow->pRedSlider, mMin, mMax);
		pWindow->pRedSlider->clamp = !newAllowHDR;
	}
	if (pWindow->pGreenSlider)
	{
		ui_ColorSliderSetRange(pWindow->pGreenSlider, mMin, mMax);
		pWindow->pGreenSlider->clamp = !newAllowHDR;
	}
	if (pWindow->pBlueSlider)
	{
		ui_ColorSliderSetRange(pWindow->pBlueSlider, mMin, mMax);
		pWindow->pBlueSlider->clamp = !newAllowHDR;
	}
	if (pWindow->pHueSlider)
		ui_ColorSliderSetRange(pWindow->pHueSlider, hMin, hMax);
	if (pWindow->pSaturationSlider)
		ui_ColorSliderSetRange(pWindow->pSaturationSlider, hMin, hMax);
	if (pWindow->pValueSlider)
	{
		ui_ColorSliderSetRange(pWindow->pValueSlider, hMin, hMax);
		pWindow->pValueSlider->clamp = !newAllowHDR;
	}
	if (pWindow->pAlphaSlider)
	{
		ui_ColorSliderSetRange(pWindow->pAlphaSlider, aMin, aMax);
	}
	if (pWindow->pLuminanceSlider)
	{
		if (newAllowHDR)
		{
			ui_ColorSliderSetRange(pWindow->pLuminanceSlider, hMin, hMax);
		}
		else
		{
			ui_ColorSliderSetRange(pWindow->pLuminanceSlider, lMin, lMax);
		}
		pWindow->pValueSlider->clamp = !newAllowHDR;
	}

	UpdatePreview(pWindow);
}

#define SetColorRGB(window, rgb, set_sliders) SetColorRGBEx(window, rgb, set_sliders, true)
static void SetColorRGBEx(UIColorWindow *window, Vec3 rgb, int set_sliders, bool setLuminance)
{
	if (window->pRedSlider)
	{
		if (set_sliders)
		{
			ui_ColorSliderSetValue(window->pRedSlider, rgb);
			ui_ColorSliderSetValue(window->pBlueSlider, rgb);
			ui_ColorSliderSetValue(window->pGreenSlider, rgb);
		}
		else
		{
			copyVec3(rgb, window->pRedSlider->current);
			copyVec3(rgb, window->pGreenSlider->current);
			copyVec3(rgb, window->pBlueSlider->current);
		}
	}

	if (window->pHueSlider)
	{
		Vec3 hsv;
		rgbToHsv(rgb, hsv);
		if (set_sliders)
		{
			ui_ColorSliderSetValue(window->pHueSlider, hsv);
			ui_ColorSliderSetValue(window->pSaturationSlider, hsv);
			ui_ColorSliderSetValue(window->pValueSlider, hsv);
			if ((window->pLuminanceSlider) && (setLuminance) && (!window->pRedSlider))
			{
				Vec3 hsl;
				hsvToHsl(hsv, hsl);
				hsl[2] = luminanceFromRGB(rgb, window);
				ui_ColorSliderSetValue(window->pLuminanceSlider, hsl);
			}
		}
		else
		{
			copyVec3(hsv, window->pHueSlider->current);
			copyVec3(hsv, window->pSaturationSlider->current);
			copyVec3(hsv, window->pValueSlider->current);
			if ((window->pLuminanceSlider) && (setLuminance) && (!window->pRedSlider))
			{
				Vec3 hsl;
				hsvToHsl(hsv, hsl);
				hsl[2] = luminanceFromRGB(rgb, window);
				copyVec3(hsl, window->pLuminanceSlider->current);
			}
		}
	}
	if ((window->pLuminanceSlider) && (setLuminance) && (window->pRedSlider))
	{
		Vec3 hsl, hsv;
		rgbToHsv(rgb, hsv);
		hsvToHsl(hsv, hsl);
		hsl[2] = luminanceFromRGB(rgb, window);
		if (set_sliders)
		{
			ui_ColorSliderSetValue(window->pLuminanceSlider, hsl);
		}
		else
		{
			copyVec3(hsl, window->pLuminanceSlider->current);
		}
	}

	UpdatePreview(window);
}

#define SetColorHSV(window, hsv, set_sliders) SetColorHSVEx(window, hsv, set_sliders, true)
static void SetColorHSVEx(UIColorWindow *window, Vec3 hsv, int set_sliders, bool setLuminance)
{
	if (window->pHueSlider)
	{
		if (set_sliders)
		{
			ui_ColorSliderSetValue(window->pHueSlider, hsv);
			ui_ColorSliderSetValue(window->pSaturationSlider, hsv);
			ui_ColorSliderSetValue(window->pValueSlider, hsv);
			if ((window->pLuminanceSlider) && (setLuminance) && (!window->pRedSlider))
			{
				Vec3 hsl;
				hsvToHsl(hsv, hsl);
				ui_ColorSliderSetValue(window->pLuminanceSlider, hsl);
			}
		}
		else
		{
			copyVec3(hsv, window->pHueSlider->current);
			copyVec3(hsv, window->pSaturationSlider->current);
			copyVec3(hsv, window->pValueSlider->current);
			if ((window->pLuminanceSlider) && (setLuminance) && (!window->pRedSlider))
			{
				Vec3 hsl;
				hsvToHsl(hsv, hsl);
				copyVec3(hsl, window->pLuminanceSlider->current);
			}
		}
	}

	if (window->pRedSlider)
	{
		Vec3 rgb;
		hsvToRgb(hsv, rgb);
		if (set_sliders)
		{
			ui_ColorSliderSetValue(window->pRedSlider, rgb);
			ui_ColorSliderSetValue(window->pBlueSlider, rgb);
			ui_ColorSliderSetValue(window->pGreenSlider, rgb);
		}
		else
		{
			copyVec3(rgb, window->pRedSlider->current);
			copyVec3(rgb, window->pGreenSlider->current);
			copyVec3(rgb, window->pBlueSlider->current);
		}
		if ((window->pLuminanceSlider) && (setLuminance))
		{
			Vec3 hsl;
			hsvToHsl(hsv, hsl);
			hsl[2] = luminanceFromRGB(rgb, window);
			if (set_sliders)
			{
				ui_ColorSliderSetValue(window->pLuminanceSlider, hsl);
			}
			else
			{
				copyVec3(hsl, window->pLuminanceSlider->current);
			}
		}
	}
	UpdatePreview(window);
}

static void ColorPaletteSetAllSliders(UIColorPalette *palette, UIColorWindow *window)
{
	Color c = ui_ColorPaletteGetColor(palette);
	Vec3 color = {c.r * U8TOF32_COLOR, c.g * U8TOF32_COLOR, c.b * U8TOF32_COLOR};
	SetColorRGB(window, color, 0);
}

static UpdateSlidersRGB(UIColorSlider *slider, UIColorWindow *window)
{
	SetColorRGB(window, slider->current, 0);
}

static UpdateSlidersHSV(UIColorSlider *slider, UIColorWindow *window)
{
	SetColorHSV(window, slider->current, 0);
}

static UpdateSlidersAlpha(UIColorSlider *slider, UIColorWindow *window)
{
	UpdatePreview(window);
}

static UpdateSlidersLuminance(UIColorSlider *slider, UIColorWindow *window)
{
	Vec3 current, hsl;

	if (window->pHueSlider)
	{
		copyVec3(window->pHueSlider->current, current);
	}
	else if (window->pRedSlider)
	{
		rgbToHsv(window->pRedSlider->current, current);
	}
	else
	{
		setVec3same(current, 1);
	}

	hsvToHsl(current, hsl);
	hsl[1] = current[1];
	hsl[2] = window->pLuminanceSlider->current[2];
	hslToHsvSmartS(hsl, current, window->pLuminanceSlider->max[2]);

	SetColorHSVEx(window, current, 0, true);
}

static void SetSliderFromEntry(UITextEntry *entry, UIColorSlider *slider)
{
	F32 value;
	UIColorWindow *window = slider->changedData;
	Vec3 newColor;
	copyVec3(slider->current, newColor);
	if (sscanf(ui_TextEntryGetText(entry), "%f", &value) == 1)
	{
		bool bFloatMode = window->pFloatRGB ? ui_CheckButtonGetState(window->pFloatRGB) : false;
		if (slider == window->pRedSlider)
			newColor[0] = bFloatMode ? value : value * U8TOF32_COLOR;
		else if (slider == window->pGreenSlider)
			newColor[1] = bFloatMode ? value : value * U8TOF32_COLOR;
		else if (slider == window->pBlueSlider)
			newColor[2] = bFloatMode ? value : value * U8TOF32_COLOR;
		else if (slider == window->pHueSlider)
			newColor[0] = value;
		else if (slider == window->pSaturationSlider)
			newColor[1] = value;
		else if (slider == window->pValueSlider)
			newColor[2] = value;
		else if (slider == window->pAlphaSlider)
		{
			if (!bFloatMode)
			{
				value *= U8TOF32_COLOR;
			}
			newColor[0] = value;
			newColor[1] = value;
			newColor[2] = value;
		}
		else if (slider == window->pLuminanceSlider)
		{
			newColor[2] = value;
		}
	}
	ui_TextEntrySetCursorPosition(entry, 0);
	ui_ColorSliderSetValueAndCallback(slider, newColor);
}

static void ColorWindowSelectButtonCallback(UIButton *button, UIColorWindow *window)
{
	int i;
	UIColorButton *cbutton = window->pButton;
	Vec4 past_colors[PAST_COLOR_CNT];
	Vec4 color;
	char buf[64];

	ui_ColorWindowGetColor(window, color);
	//Search to see if we already have this color in our history
	//Fill in past_colors as we go, so we don't have to look them up later
	for( i=0; i < PAST_COLOR_CNT-1 ; i++ )
	{
		Vec4 color_buf = {0.5, 0.5, 0.5, 1.0};
		sprintf(buf, "Idx_%02d", i);
		EditorPrefGetPosition("UIColorButton", "PastColors", buf, color_buf, color_buf+1, color_buf+2, color_buf+3);
		copyVec4(color_buf, past_colors[i]);
		if(sameVec4(past_colors[i], color))
			break;
	}
	//At this point, past_colors should be filled up i (exclusive)
	//i should be the index of the matching color, or PAST_COLOR_CNT-1 if no matching color exist
	//Remove i from the history and re-position the times
	for(  ; i > 0 ; i-- )
	{
		sprintf(buf, "Idx_%02d", i);
		EditorPrefStorePosition("UIColorButton", "PastColors", buf, past_colors[i-1][0], past_colors[i-1][1], past_colors[i-1][2], past_colors[i-1][3]);
	}
	sprintf(buf, "Idx_%02d", 0);
	EditorPrefStorePosition("UIColorButton", "PastColors", buf, color[0], color[1], color[2], color[3]);

	if ((cbutton) && (!window->bOrphaned))
	{
		ui_ColorButtonSetColorAndCallback(cbutton, color);
	}
	ui_WindowClose(UI_WINDOW(window));
}

static void ColorWindowCancelButtonCallback(UIButton *button, UIColorWindow *window)
{
	if ((window->pButton) && (!window->bOrphaned))
	{
		ui_ColorButtonSetColorAndCallback(window->pButton, window->v4OrigColor);
	}
	ui_WindowClose(UI_WINDOW(window));
}

UIColorWindow *ui_ColorWindowCreate(UIColorButton *cbutton, F32 x, F32 y, F32 min, F32 max, Vec4 orig_color, bool no_alpha, bool no_rgb, bool no_hsv, bool live_update, bool force_hdr)
{
	UIColorWindow *window = calloc(1, sizeof(UIColorWindow));
	UIColorPalette *palette = ui_ColorPaletteCreate(UI_HSTEP, UI_HSTEP);
	UIColorButton *past_color_button;
	UIButton *button;
	UIButton *button2;
	Vec3 mMin = {min, min, min}, hMin = {0.f, 0.f, min}, mMax = {max, max, max}, hMax = {360.f, 1.f, max},
		aMin = {0.f, 0.f, 0.f}, aMax = {1.f, 1.f, 1.f}, lMin = {0.f, 0.f, 0.f}, lMax = {1.f, 1.f, 1.f};
	Vec3 rgb = {0, 0, 0}, alpha = {1.f, 1.f, 1.f}, luminance = {1.f, 1.f, 1.f};
	int i = 0;
	char buf[64];
	F32 fHeight = 24.f;
	bool anyColorHDR = orig_color && (orig_color[ 0 ] > 1 || orig_color[ 0 ] < 0
									  || orig_color[ 1 ] > 1 || orig_color[ 1 ] < 0
									  || orig_color[ 2 ] > 1 || orig_color[ 2 ] < 0
									  || orig_color[ 3 ] > 1 || orig_color[ 3 ] < 0);



	if (orig_color)
	{
		copyVec3(orig_color, rgb);
		setVec3same(alpha, orig_color[3]);
	}

	if (cbutton && !nullStr(cbutton->title)) {
		ui_WindowInitializeEx(UI_WINDOW(window), cbutton->title, x, y, 400, 200 MEM_DBG_PARMS_INIT);
	} else {
		ui_WindowInitializeEx(UI_WINDOW(window), "Choose a Color", x, y, 400, 200 MEM_DBG_PARMS_INIT);
	}
	window->min = min;
	window->max = max;

	window->bLiveUpdate = live_update;
	window->bForceHDR = force_hdr;
	window->pButton = cbutton;
	if (orig_color) 
	{
		button = ui_ButtonCreate("Cancel", UI_HSTEP, UI_HSTEP, NULL, NULL);
		button2 = ui_ButtonCreate("Select", UI_HSTEP + UI_HSTEP + UI_WIDGET(button)->width, UI_HSTEP, NULL, NULL);
		copyVec4(orig_color,window->v4OrigColor);
	} 
	else 
	{
		button = NULL;
		button2 = ui_ButtonCreate("Select", UI_HSTEP, UI_HSTEP, NULL, NULL);
	}

	for( i=0; i < PAST_COLOR_CNT; i++ )
	{
		Vec4 color_buf = {0.5, 0.5, 0.5, 1.0};
		int past_color_pos;
		if(button)
			past_color_pos = (PAST_COLOR_CNT-1-i)*4*UI_HSTEP + 3*UI_HSTEP + UI_WIDGET(button)->width + UI_WIDGET(button2)->width;
		else
			past_color_pos = (PAST_COLOR_CNT-1-i)*4*UI_HSTEP + 2*UI_HSTEP + UI_WIDGET(button2)->width;
		sprintf(buf, "Idx_%02d", i);
		EditorPrefGetPosition("UIColorButton", "PastColors", buf, color_buf, color_buf+1, color_buf+2, color_buf+3);
		past_color_button = ui_ColorButtonCreate(past_color_pos, UI_HSTEP, color_buf);
		ui_WidgetSetWidth(UI_WIDGET(past_color_button), 3*UI_HSTEP);
		UI_WIDGET(past_color_button)->offsetFrom = UIBottomRight;
		ui_ButtonSetCallback(&past_color_button->button, ColorWindowPastColorButtonCallback, window);
		ui_WindowAddChild(UI_WINDOW(window), past_color_button);
	}
	i=0;

	//ui_WindowSetResizable(UI_WINDOW(window), false); 

	window->pPalette = palette;
	ui_ColorPaletteSetChooseCallback(palette, ColorPaletteSetAllSliders, window);
	ui_WindowAddChild(UI_WINDOW(window), palette);

	zeroVec4(window->v4Color);
	window->pPreview = ui_SpriteCreate(UI_HSTEP, 140, 128, 40, "white.tga");
	ui_WindowAddChild(UI_WINDOW(window), window->pPreview);

	window->pBroken = ui_LabelCreate("", UI_HSTEP, UI_WIDGET(window->pPreview)->y + UI_WIDGET(window->pPreview)->height + UI_HSTEP);
	ui_WindowAddChild(UI_WINDOW(window), window->pBroken);

	//fHeight = UI_WIDGET(window->pBroken)->height;

	if (no_hsv && no_rgb && no_alpha)
		no_rgb = 0;

	if (1)
	{
		window->pLimitColors = ui_CheckButtonCreate(0, 210, "Valid Only", false);
		ui_CheckButtonSetToggledCallback(window->pLimitColors, ToggleLimitMode, window);
		ui_WindowAddChild(UI_WINDOW(window), window->pLimitColors);
		//fHeight = UI_WIDGET(window->pLimitColors)->height;
	}
	if (!no_rgb || !no_alpha)
	{
		window->pFloatRGB = ui_CheckButtonCreate(140, UI_HSTEP + fHeight * i++, "Use 0..1 range for RGBA values", false);
		ui_CheckButtonSetToggledCallback(window->pFloatRGB, ToggleRGBMode, window);
		ui_WindowAddChild(UI_WINDOW(window), window->pFloatRGB);
		//fHeight = UI_WIDGET(window->pFloatRGB)->height;
	}
	if (!force_hdr && (min < 0 || max > 1))
	{
		window->pAllowHDR = ui_CheckButtonCreate(140, UI_HSTEP + fHeight * i++, "Allow HDR values", anyColorHDR);
		ui_CheckButtonSetToggledCallback(window->pAllowHDR, ToggleHDRMode, window);
		ui_CheckButtonSetState(window->pAllowHDR, anyColorHDR);
		ui_WindowAddChild(UI_WINDOW(window), window->pAllowHDR);
	}

	if (!no_rgb)
	{
		window->pRedLabel = ui_LabelCreate("R:", 324, UI_HSTEP + fHeight * i);
		ui_WindowAddChild(UI_WINDOW(window), window->pRedLabel);
		window->pRedEntry = ui_TextEntryCreate("", 340, UI_HSTEP + fHeight * i);
		ui_WindowAddChild(UI_WINDOW(window), window->pRedEntry);
		window->pRedSlider = ui_ColorSliderCreate(140, UI_HSTEP + fHeight * i++, 180, mMin, mMax, false);
		ui_WidgetSetHeight(UI_WIDGET(window->pRedSlider), fHeight);
		ui_WindowAddChild(UI_WINDOW(window), window->pRedSlider);
		ui_WidgetSetWidth(UI_WIDGET(window->pRedEntry), 50);
		ui_TextEntrySetFinishedCallback(window->pRedEntry, SetSliderFromEntry, window->pRedSlider);
		ui_ColorSliderSetChangedCallback(window->pRedSlider, UpdateSlidersRGB, window);

		window->pGreenLabel = ui_LabelCreate("G:", 324, UI_HSTEP + fHeight * i);
		ui_WindowAddChild(UI_WINDOW(window), window->pGreenLabel);
		window->pGreenEntry = ui_TextEntryCreate("", 340, UI_HSTEP + fHeight * i);
		ui_WindowAddChild(UI_WINDOW(window), window->pGreenEntry);
		window->pGreenSlider = ui_ColorSliderCreate(140, UI_HSTEP + fHeight * i++, 180, mMin, mMax, false);
		ui_WidgetSetHeight(UI_WIDGET(window->pGreenSlider), fHeight);
		ui_WindowAddChild(UI_WINDOW(window), window->pGreenSlider);
		ui_WidgetSetWidth(UI_WIDGET(window->pGreenEntry), 50);
		ui_TextEntrySetFinishedCallback(window->pGreenEntry, SetSliderFromEntry, window->pGreenSlider);
		ui_ColorSliderSetChangedCallback(window->pGreenSlider, UpdateSlidersRGB, window);

		window->pBlueLabel = ui_LabelCreate("B:", 324, UI_HSTEP + fHeight * i);
		ui_WindowAddChild(UI_WINDOW(window), window->pBlueLabel);
		window->pBlueEntry = ui_TextEntryCreate("", 340, UI_HSTEP + fHeight * i);
		ui_WindowAddChild(UI_WINDOW(window), window->pBlueEntry);
		window->pBlueSlider = ui_ColorSliderCreate(140, UI_HSTEP + fHeight * i++, 180, mMin, mMax, false);
		ui_WidgetSetHeight(UI_WIDGET(window->pBlueSlider), fHeight);
		ui_WindowAddChild(UI_WINDOW(window), window->pBlueSlider);
		ui_WidgetSetWidth(UI_WIDGET(window->pBlueEntry), 50);
		ui_TextEntrySetFinishedCallback(window->pBlueEntry, SetSliderFromEntry, window->pBlueSlider);
		ui_ColorSliderSetChangedCallback(window->pBlueSlider, UpdateSlidersRGB, window);
	}

	if (!no_hsv)
	{
		window->pHueLabel = ui_LabelCreate("H:", 324, UI_HSTEP + fHeight * i);
		ui_WindowAddChild(UI_WINDOW(window), window->pHueLabel);
		window->pHueEntry = ui_TextEntryCreate("", 340, UI_HSTEP + fHeight * i);
		ui_WindowAddChild(UI_WINDOW(window), window->pHueEntry);
		window->pHueSlider = ui_ColorSliderCreate(140, UI_HSTEP + fHeight * i++, 180, hMin, hMax, true);
		ui_WidgetSetHeight(UI_WIDGET(window->pHueSlider), fHeight);
		ui_WindowAddChild(UI_WINDOW(window), window->pHueSlider);
		ui_WidgetSetWidth(UI_WIDGET(window->pHueEntry), 50);
		ui_TextEntrySetFinishedCallback(window->pHueEntry, SetSliderFromEntry, window->pHueSlider);
		ui_ColorSliderSetChangedCallback(window->pHueSlider, UpdateSlidersHSV, window);

		window->pSaturationLabel = ui_LabelCreate("S:", 324, UI_HSTEP + fHeight * i);
		ui_WindowAddChild(UI_WINDOW(window), window->pSaturationLabel);
		window->pSaturationEntry = ui_TextEntryCreate("", 340, UI_HSTEP + fHeight * i);
		ui_WindowAddChild(UI_WINDOW(window), window->pSaturationEntry);
		window->pSaturationSlider = ui_ColorSliderCreate(140, UI_HSTEP + fHeight * i++, 180, hMin, hMax, true);
		ui_WidgetSetHeight(UI_WIDGET(window->pSaturationSlider), fHeight);
		ui_WindowAddChild(UI_WINDOW(window), window->pSaturationSlider);
		ui_WidgetSetWidth(UI_WIDGET(window->pSaturationEntry), 50);
		ui_TextEntrySetFinishedCallback(window->pSaturationEntry, SetSliderFromEntry, window->pSaturationSlider);
		ui_ColorSliderSetChangedCallback(window->pSaturationSlider, UpdateSlidersHSV, window);

		window->pValueLabel = ui_LabelCreate("V:", 324, UI_HSTEP + fHeight * i);
		ui_WindowAddChild(UI_WINDOW(window), window->pValueLabel);
		window->pValueEntry = ui_TextEntryCreate("", 340, UI_HSTEP + fHeight * i);
		ui_WindowAddChild(UI_WINDOW(window), window->pValueEntry);
		window->pValueSlider = ui_ColorSliderCreate(140, UI_HSTEP + fHeight * i++, 180, hMin, hMax, true);
		ui_WidgetSetHeight(UI_WIDGET(window->pValueSlider), fHeight);
		ui_WindowAddChild(UI_WINDOW(window), window->pValueSlider);
		ui_WidgetSetWidth(UI_WIDGET(window->pValueEntry), 50);
		ui_TextEntrySetFinishedCallback(window->pValueEntry, SetSliderFromEntry, window->pValueSlider);
		ui_ColorSliderSetChangedCallback(window->pValueSlider, UpdateSlidersHSV, window);
	}

	if (!no_alpha)
	{
		window->pAlphaLabel = ui_LabelCreate("A:", 324, UI_HSTEP + fHeight * i);
		ui_WindowAddChild(UI_WINDOW(window), window->pAlphaLabel);
		window->pAlphaEntry = ui_TextEntryCreate("", 340, UI_HSTEP + fHeight * i);
		ui_WindowAddChild(UI_WINDOW(window), window->pAlphaEntry);
		window->pAlphaSlider = ui_ColorSliderCreate(140, UI_HSTEP + fHeight * i++, 180, aMin, aMax, false);
		ui_WidgetSetHeight(UI_WIDGET(window->pAlphaSlider), fHeight);
		ui_WindowAddChild(UI_WINDOW(window), window->pAlphaSlider);
		ui_WidgetSetWidth(UI_WIDGET(window->pAlphaEntry), 50);
		ui_TextEntrySetFinishedCallback(window->pAlphaEntry, SetSliderFromEntry, window->pAlphaSlider);
		ui_ColorSliderSetChangedCallback(window->pAlphaSlider, UpdateSlidersAlpha, window);
	}

	window->pLuminanceLabel = ui_LabelCreate("L:", 324, UI_HSTEP + fHeight * i);
	ui_WindowAddChild(UI_WINDOW(window), window->pLuminanceLabel);
	window->pLuminanceEntry = ui_TextEntryCreate("", 340, UI_HSTEP + fHeight * i);
	ui_WindowAddChild(UI_WINDOW(window), window->pLuminanceEntry);
	window->pLuminanceSlider = ui_ColorSliderCreate(140, UI_HSTEP + fHeight * i++, 180, hMin, hMax, false);
	window->pLuminanceSlider->lum = true;
	ui_WidgetSetHeight(UI_WIDGET(window->pLuminanceSlider), fHeight);
	ui_WindowAddChild(UI_WINDOW(window), window->pLuminanceSlider);
	ui_WidgetSetWidth(UI_WIDGET(window->pLuminanceEntry), 50);
	ui_TextEntrySetFinishedCallback(window->pLuminanceEntry, SetSliderFromEntry, window->pLuminanceSlider);
	ui_ColorSliderSetChangedCallback(window->pLuminanceSlider, UpdateSlidersLuminance, window);

	if (button) 
	{
		UI_WIDGET(button)->offsetFrom = UIBottomRight;
		ui_ButtonSetCallback(button, ColorWindowCancelButtonCallback, window);
		ui_WindowAddChild(UI_WINDOW(window), button);
	}
	UI_WIDGET(button2)->offsetFrom = UIBottomRight;
	ui_ButtonSetCallback(button2, ColorWindowSelectButtonCallback, window);
	ui_WindowAddChild(UI_WINDOW(window), button2);

	ui_WidgetSetHeight(UI_WIDGET(window), UI_STEP + fHeight * i + UI_WIDGET(button2)->height + UI_STEP);

	if (cbutton)
		cbutton->activeWindow = window;

	// Update the current HDR state
	if (window->pAlphaSlider)
	{
		ui_ColorSliderSetValue(window->pAlphaSlider, alpha);
	}
	SetColorRGB(window, rgb, 1);
	if (window->pAllowHDR)
	{
		ToggleHDRMode(window->pAllowHDR, window);
	}

	window->widget.freeF = ui_ColorWindowFreeInternal;
	window->bOrphaned = false;
	eaPush(&g_OpenWindows, window);
	return window;
}

void ui_ColorWindowFreeInternal(UIColorWindow *window)
{
	if (window->pButton)
		window->pButton->activeWindow = NULL;
	eaFindAndRemove(&g_OpenWindows, window);
	ui_WindowFreeInternal(UI_WINDOW(window));
}

void ui_ColorWindowGetColor(UIColorWindow *window, Vec4 color)
{
	copyVec4(window->v4Color, color);
}

void ui_ColorWindowSetColorAndCallback(UIColorWindow *window, const Vec4 color)
{
	Vec3 rgb = {color[0], color[1], color[2]}, alpha = {color[3], color[3], color[3]};
	if (window->pAlphaSlider)
	{
		ui_ColorSliderSetValue(window->pAlphaSlider, alpha);
	}
	SetColorRGB(window, rgb, 1);
}

void ui_ColorButtonDraw(UIColorButton *cbutton, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(cbutton);
	Color color;
	Vec3 hsv, rgb;
	F32 value;

	rgbToHsv(cbutton->color, hsv);
	value = hsv[2];
	if (hsv[2] > 1 || hsv[2] < 0)
	{
		hsv[2] = 1;
	}
	hsvToRgb(hsv, rgb);


	color.r = CLAMP((rgb[0] * 255.0f + 0.5f), 0, 255);
	color.g = CLAMP((rgb[1] * 255.0f + 0.5f), 0, 255);
	color.b = CLAMP((rgb[2] * 255.0f + 0.5f), 0, 255);
	color.a = ui_IsActive(UI_WIDGET(cbutton)) ? 255 : 96;

	CBoxClipTo(&pBox, &box);
	ui_DrawCapsule(&box, z, ((cbutton->button.pressed && ui_IsHovering(UI_WIDGET(cbutton)) || ui_IsFocused(cbutton)) ? ColorDarken(color, 64) : color), scale);
	ui_WidgetGroupDraw(&cbutton->widget.children, UI_MY_VALUES);
	if (value > 1 || value < 0)
	{
		UIStyleFont *font = GET_REF(UI_GET_SKIN(cbutton)->hNormal);
		ui_StyleFontUse(font, false, UI_WIDGET(cbutton)->state);
		gfxfont_Printf(x + w/2, y + h/2, z + 0.01, scale, scale, CENTER_XY, "%.1f", value);
	}
}

static bool ColorWindowCloseCallback(UIColorWindow *window, UIColorButton *cbutton)
{
	if (cbutton && cbutton->activeWindow && (cbutton->activeWindow == window)) {
		ui_WidgetQueueFree(UI_WIDGET(window));
	}
	return true;
}

static bool findWidgetPos(UIWidget *groupWidget, UIWidgetGroup *group, UIWidget *search, F32 *retx, F32 *rety, F32 *retw, F32 *reth, UI_PARENT_ARGS)
{
	int i;
	if (groupWidget) {
		UI_GET_COORDINATES((UIWidgetWidget*)groupWidget);
		if (groupWidget == search) {
			*retx = x;
			*rety = y;
			if (retw)
				*retw = w;
			if (reth)
				*reth = h;
			return true;
		}
		for (i=eaSize(&groupWidget->children)-1; i>=0; i--)
			if (groupWidget->children[i] && findWidgetPos(groupWidget->children[i], NULL, search, retx, rety, retw, reth, UI_MY_VALUES))
				return true;
	} else {
		for (i=eaSize(group)-1; i>=0; i--)
			if ((*group)[i] && findWidgetPos((*group)[i], NULL, search, retx, rety, retw, reth, pX, pY, pW, pH, pScale))
				return true;
	}
	return false;
}

bool ui_WidgetPositionRelativeTo(UIWidget *widget, UIWidgetGroup *group, F32 *x, F32 *y)
{
	return findWidgetPos(NULL, group, widget, x, y, NULL, NULL, 0, 0, 10000, 10000, 1.f);
}

void ui_ColorButtonClick(UIColorButton *cbutton, UserData dummy)
{
	if (!cbutton->activeWindow)
	{
		UIWidgetGroup *group = 	cbutton->parentGroup ? cbutton->parentGroup : ui_WidgetGroupForDevice(NULL);
		F32 x=50, y=50;
		// This function is generating X/Y values in the 10000s which crashes the sprite scissor code
		// and is also obviously wrong.
		//bool bFoundParent = ui_WidgetPositionRelativeTo(UI_WIDGET(cbutton), group, &x, &y);
		UIColorWindow *window = ui_ColorWindowCreate(cbutton, x, y, cbutton->min, cbutton->max, cbutton->color, cbutton->noAlpha, cbutton->noRGB, cbutton->noHSV, cbutton->liveUpdate, cbutton->bForceHDR);
		cbutton->activeWindow = window;
		ui_WindowSetCloseCallback(UI_WINDOW(window), ColorWindowCloseCallback, cbutton);
		ui_ColorWindowSetColorAndCallback(window, cbutton->color);
		if (cbutton->parentGroup)
			ui_WidgetGroupAdd(cbutton->parentGroup, UI_WIDGET(window));
		else
		{
			if (cbutton->bIsModal)
			{
				ui_WindowSetModal(UI_WINDOW(window), true);
			}
			ui_WindowShow(UI_WINDOW(window));
		}
	}
	else
	{
		ui_WindowShow(UI_WINDOW(cbutton->activeWindow));
		ui_WidgetGroupSteal(cbutton->activeWindow->widget.group, UI_WIDGET(cbutton->activeWindow));
	}
}

// Cancels the window (if any is open)
void ui_ColorButtonCancelWindow(UIColorButton *cbutton)
{
	if (cbutton->activeWindow)
	{
		UIColorWindow *pWindow = cbutton->activeWindow;
		cbutton->activeWindow = NULL;
		ColorWindowCancelButtonCallback(NULL, pWindow);
	}
}

UIColorButton *ui_ColorButtonCreateEx(F32 x, F32 y, F32 min, F32 max, const Vec4 initial)
{
	UIColorButton *cbutton = (UIColorButton *)calloc(1, sizeof(UIColorButton));
	ui_ColorButtonInitialize(cbutton, x, y, min, max, initial);
	return cbutton;
}

void ui_ColorButtonInitialize(UIColorButton *cbutton, F32 x, F32 y, F32 min, F32 max, const Vec4 initial)
{
	F32 fontHeight;
	ui_ButtonInitialize(&cbutton->button, NULL, x, y, ui_ColorButtonClick, NULL MEM_DBG_PARMS_INIT);
	fontHeight = ui_StyleFontLineHeight(GET_REF(UI_GET_SKIN(cbutton)->hNormal), 1.f);
	ui_WidgetSetDimensions(UI_WIDGET(cbutton), 64, fontHeight + UI_STEP);
	ui_WidgetSetPosition(UI_WIDGET(cbutton), x, y);
	cbutton->min = min;
	cbutton->max = max;
	cbutton->widget.drawF = ui_ColorButtonDraw;
	cbutton->widget.freeF = ui_ColorButtonFreeInternal;
	cbutton->title[0] = '\0';
	copyVec4(initial, cbutton->color);
}

void ui_ColorButtonSetUserData(SA_PARAM_NN_VALID UIColorButton *cbutton, UserData dummy)
{
	cbutton->changedData = dummy;
}

void ui_ColorButtonSetTitle(UIColorButton *cbutton, const char* title) {
	strcpy(cbutton->title, title);
}

void ui_ColorButtonGetColor(UIColorButton *cbutton, Vec4 color)
{
	copyVec4(cbutton->color, color);
}

U32 ui_ColorButtonGetColorInt(UIColorButton *cbutton)
{
	Color c;
	return RGBAFromColor(*vec4ToColor(&c, cbutton->color));

}

void ui_ColorButtonSetColor(UIColorButton *cbutton, const Vec4 color) 
{
	copyVec4(color, cbutton->color);
}

static void ui_ColorButtonSetColorAndCallbackEx(UIColorButton *cbutton, bool finished, const Vec4 color)
{
	ui_ColorButtonSetColor(cbutton, color);
	if ((cbutton->changedF) && ((!cbutton->activeWindow) || (!cbutton->activeWindow->bOrphaned)))
	{
		cbutton->changedF(cbutton, finished, cbutton->changedData);
	}
}

void ui_ColorButtonSetColorAndCallback(UIColorButton *cbutton, const Vec4 color)
{
	ui_ColorButtonSetColorAndCallbackEx(cbutton, true, color);
}

void ui_ColorButtonSetChangedCallback(UIColorButton *cbutton, UIColorChangeFunc changedF, UserData changedData)
{
	cbutton->changedF = changedF;
	cbutton->changedData = changedData;
	if (cbutton->activeWindow)
	{
		cbutton->activeWindow->bOrphaned = false;
	}
}

void ui_ColorButtonFreeInternal(UIColorButton *cbutton)
{
	// There are issues with this being called when there is an active associated window.
	//  There may be an installed callback that may be referencing now-deleted data.
	//  Regardless, we certainly need to dismiss the active window. Rather than
	//  canceling and invoking the callback, we will just close the window directly instead.
	//  This has the unfortunate effect of not reverting to an original color if it has been
	//  changed in a liveUpdate situation. But this mirrors what happens if the close box
	//  is chosen after a color has been changed. WOLF[7Dec11]

	if (cbutton->activeWindow)
	{
		UIColorWindow *pWindow = cbutton->activeWindow;
		cbutton->activeWindow = NULL;
		ui_WindowClose(UI_WINDOW(pWindow));
	}
	ui_ButtonFreeInternal(UI_BUTTON(cbutton));
}

void ui_ColorWindowSetColorSet(SA_PARAM_NN_VALID UIColorWindow *window, UIColorSet *pColorSet)
{
	window->pColorSet = pColorSet;
	ToggleLimitMode(window->pLimitColors, window);
}

void ui_ColorWindowSetLimitColors(SA_PARAM_NN_VALID UIColorWindow *window, bool bLimit)
{
	ui_CheckButtonSetState(window->pLimitColors, bLimit);
}
