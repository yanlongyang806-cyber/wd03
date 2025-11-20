#include "error.h"
#include "Color.h"
#include "FolderCache.h"
#include "mathutil.h"
#include "rgb_hsv.h"
#include "Color_h_ast.h"

Color ColorWhite = {0xFF, 0xFF, 0xFF, 0xFF};
Color ColorBlack = {0, 0, 0, 0xFF};
Color ColorRed = {0xFF, 0, 0, 0xFF};
Color ColorGreen = {0, 0xFF, 0, 0xFF};
Color ColorBlue = {0, 0, 0xFF, 0xFF};
Color ColorLightBlue = {0xCF, 0xCF, 0xFF, 0xFF};
Color ColorLightRed = {0xFF, 0x7F, 0x7F, 0xFF};
Color ColorOrange = {0xFF, 0x80, 0x40, 0xFF};
Color ColorCyan = {0x00, 0xFF, 0xFF, 0xFF};
Color ColorYellow = {0xFF, 0xFF, 0x00, 0xFF};
Color ColorMagenta = {0xFF, 0x00, 0xFF, 0xFF};
Color ColorPurple = {0x99, 0x00, 0x99, 0xFF};
Color ColorHalfBlack = {0, 0, 0, 0x77};
Color ColorTransparent = {0, 0, 0, 0x00};
Color ColorGray = {0x80, 0x80, 0x80, 0xFF};

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

Color colorFlip( Color c )
{
	Color col;

	col.r = c.a;
	col.g = c.b;
	col.b = c.g;
	col.a = c.r;

	return col;
}

int colorEqualsIgnoreAlpha( Color c1, Color c2 )
{
	if( c1.r == c2.r &&
		c1.g == c2.g &&
		c1.b == c2.b )
		return 1;

	return 0;
}


U32 darkenColorRGBA(U32 rgba, int amount)
{
	Color c;
	setColorFromRGBA(&c, rgba);
	c.r = MAX(c.r-amount, 0);
	c.g = MAX(c.g-amount, 0);
	c.b = MAX(c.b-amount, 0);
	return RGBAFromColor(c);
}

Color ColorLighten(Color old, int amount)
{
	Color newColor;
	newColor.r = CLAMP(old.r + amount, 0, 255);
	newColor.g = CLAMP(old.g + amount, 0, 255);
	newColor.b = CLAMP(old.b + amount, 0, 255);
	newColor.a = old.a;
	return newColor;
}

Color ColorDarken(Color old, int amount)
{
	Color newColor;
	newColor.r = CLAMP(old.r - amount, 0, 255);
	newColor.g = CLAMP(old.g - amount, 0, 255);
	newColor.b = CLAMP(old.b - amount, 0, 255);
	newColor.a = old.a;
	return newColor;
}

Color ColorDarkenPercent(Color old, F32 percent)
{
	Color newColor;
	newColor.r = CLAMP(old.r * percent, 0, 255);
	newColor.g = CLAMP(old.g * percent, 0, 255);
	newColor.b = CLAMP(old.b * percent, 0, 255);
	newColor.a = old.a;
	return newColor;
}

// Returns a color that is fPercent brighter than the given color.
// If fPercent=1, you'll get white.  If fPercent=0, you'll get the 
// given color.
Color ColorLightenPercent(Color old, F32 fPercent) {
	Color newColor;
	newColor.r = CLAMP(old.r + (255-old.r) * fPercent, 0, 255);
	newColor.g = CLAMP(old.g + (255-old.g) * fPercent, 0, 255);
	newColor.b = CLAMP(old.b + (255-old.b) * fPercent, 0, 255);
	newColor.a = old.a;

	return newColor;
}

// By invert, we mean so that the resulting color will always be visible on top of the original color (e.g. CSS definition of invert).
// We do this by converting from RGB to HSV, inverting the Hue, then converting back.
Color ColorInvert(Color old)
{
	Vec3 rgb = { old.r / 255.0f, old.g / 255.0f, old.b / 255.0f };
	Vec3 hsv;

	if(rgbToHsv(rgb, hsv))
	{
		Color newColor;

		if(hsv[0] >= 180.0f)
			hsv[0] -= 180.0f;
		else
			hsv[0] += 180.0f;

		hsvToRgb(hsv, rgb);

		newColor.r = rgb[0] * 255;
		newColor.g = rgb[1] * 255;
		newColor.b = rgb[2] * 255;
		newColor.a = old.a;

		return newColor;
	}

	if(old.r <= 127)
		return ColorLightenPercent(old, 0.5f);

	return ColorDarkenPercent(old, 0.5f);
}

__forceinline U8 F32ToColorComponent(F32 f)
{
	int ret = round(f * 255.f);
	return CLAMP(ret, 0, 255);
}

Color *vec3ToColor(Color *clr, const Vec3 vec)
{
	clr->r = F32ToColorComponent(vec[0]);
	clr->g = F32ToColorComponent(vec[1]);
	clr->b = F32ToColorComponent(vec[2]);
	clr->a = 255;
	return clr;
}

Color *vec4ToColor(Color *clr, const Vec4 vec)
{
	clr->r = F32ToColorComponent(vec[0]);
	clr->g = F32ToColorComponent(vec[1]);
	clr->b = F32ToColorComponent(vec[2]);
	clr->a = F32ToColorComponent(vec[3]);
	return clr;
}

ColorBGRA *vec4ToColorBGRA(ColorBGRA *clr, const Vec4 vec)
{
	clr->r = F32ToColorComponent(vec[0]);
	clr->g = F32ToColorComponent(vec[1]);
	clr->b = F32ToColorComponent(vec[2]);
	clr->a = F32ToColorComponent(vec[3]);
	return clr;
}

static DefineContext *s_pColorContext;
StaticDefineInt ColorEnum[] =
{
	DEFINE_INT
	DEFINE_EMBEDDYNAMIC_INT(s_pColorContext)
	DEFINE_END
};

static DefineContext *s_pColorPackedContext;
StaticDefineInt ColorPackedEnum[] =
{
	DEFINE_INT
	DEFINE_EMBEDDYNAMIC_INT(s_pColorPackedContext)
	DEFINE_END
};

static ColorDefLoading s_Colors = {0};
static ColorDefLoading **s_eaColorDefs;

AUTO_RUN;
void ColorInit(void)
{
	StaticDefineInt_PossiblyAddFastIntLookupCache(ColorEnum);
	StaticDefineInt_PossiblyAddFastIntLookupCache(ColorPackedEnum);
}

static void ColorReload(const char *pchPath, S32 iWhen)
{
	loadstart_printf("Loading colors... ");
	StructReset(parse_ColorDefLoading, &s_Colors);
	ParserLoadFiles("ui", "Colors.def", "Colors.bin", 0, parse_ColorDefLoading, &s_Colors);
	ColorDefLoadingRefresh();
	loadend_printf("done. (%d colors)", eaSize(&s_Colors.eaColors));
}

AUTO_STARTUP(Colors);
void ColorLoad(void)
{
	ColorDefLoadingAdd(&s_Colors);
	ColorReload("", 0);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "ui/Colors.def*", ColorReload);
}

Color ColorFromName(const char *pchName)
{
	return colorFromRGBA(StaticDefineIntGetInt(ColorEnum, pchName));
}

U32 ColorRGBAFromName(const char *pchName)
{
	return StaticDefineIntGetInt(ColorEnum, pchName);
}

void ColorDefLoadingAdd(ColorDefLoading *pColorDefs)
{
	eaPushUnique(&s_eaColorDefs, pColorDefs);
}

void ColorDefLoadingRefresh(void)
{
	S32 i, j;
	if (s_pColorContext)
		DefineDestroyByHandle(&s_pColorContext);
	if (s_pColorPackedContext)
		DefineDestroyByHandle(&s_pColorPackedContext);
	s_pColorContext = DefineCreate();
	s_pColorPackedContext = DefineCreate();
	for (j = 0; j < eaSize(&s_eaColorDefs); j++)
	{
		ColorDefLoading *pLoading = s_eaColorDefs[j];
		for (i = 0; i < eaSize(&pLoading->eaColors); i++)
		{
			if (pLoading->eaColors[i])
			{
				Color packed = colorFromRGBA(pLoading->eaColors[i]->uiColor);
				DefineAddInt_EnumOverlapOK(s_pColorContext, pLoading->eaColors[i]->pchName, pLoading->eaColors[i]->uiColor);
				DefineAddInt_EnumOverlapOK(s_pColorPackedContext, pLoading->eaColors[i]->pchName, packed.integer_for_equality_only);
			}
		}
	}
}

bool VarColorIsZero(U8 *c, int size)
{
	int i;
	for (i = 0; i < size; ++i)
	{
		if (c[i])
			return false;
	}
	return true;
}

void VarColorNormalize(U8 *c, int size)
{
	int i;
	U16 total = 0;

	if (!size)
		return;

	for (i = 0; i < size; ++i)
		total += c[i];

	if (total > 255)
	{
		F32 scale = 255.f / total;

		total = 0;
		for (i = 0; i < size; ++i)
		{
			int val = round(c[i] * scale);
			c[i] = CLAMP(val, 0, 255);
			total += c[i];
		}
	}

	if (total < 255)
	{
		int max_idx, max_val;
		max_val = c[0];
		max_idx = 0;

		for (i = 1; i < size; ++i)
		{
			if (c[i] > max_val)
			{
				max_val = c[i];
				max_idx = i;
			}
		}

		c[max_idx] += 255 - total;
		total = 255;
	}

	while (total > 255)
	{
		int min_idx, min_val;
		min_idx = -1;
		min_val = 255;
		for (i = 0; i < size; ++i)
		{
			if (c[i] < min_val && c[i] > 0)
			{
				min_val = c[i];
				min_idx = i;
			}
		}

		assert(min_idx >= 0);

		if (c[min_idx] <= total - 255)
		{
			total -= c[min_idx];
			c[min_idx] = 0;
		}
		else
		{
			c[min_idx] -= total - 255;
			total = 255;
		}
	}
}

void VarColorAverage(const U8 *c1, const U8 *c2, U8 *cOut, int size)
{
	int i;
	for (i = 0; i < size; ++i)
		cOut[i] = (c1[i] + c2[i]) / 2;
}

void VarColorScaleAdd(const U8 *c1, F32 scale, const U8 *c2, U8 *cOut, int size)
{
	int i;
	for (i = 0; i < size; ++i)
	{
		int val = round(c1[i] * scale + c2[i]);
		cOut[i] = CLAMP(val, 0, 255);
	}
}

void VarColorLerp(const U8 *c1, F32 t, const U8 *c0, U8 *cOut, int size)
{
	int i;
	for (i = 0; i < size; ++i)
	{
		int val = round(c1[i]*t + c0[i]*(1-t));
		cOut[i] = CLAMP(val, 0, 255);
	}
}

AUTO_FIXUPFUNC;
TextParserResult ui_Color4Fixup(Color4 *pColor, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
	case FIXUPTYPE_POST_TEXT_READ:
		// One color specified, use it.
		if (!pColor->uiTopRightColor
			&& !pColor->uiBottomLeftColor
			&& !pColor->uiBottomRightColor)
		{
			pColor->uiTopRightColor = pColor->uiTopLeftColor;
			pColor->uiBottomLeftColor = pColor->uiTopLeftColor;
			pColor->uiBottomRightColor = pColor->uiTopLeftColor;
		}

		// Left/Right specified.
		if (pColor->uiTopLeftColor && pColor->uiTopRightColor &&
			!pColor->uiBottomLeftColor && !pColor->uiBottomRightColor)
		{
			pColor->uiBottomLeftColor = pColor->uiTopLeftColor;
			pColor->uiBottomRightColor = pColor->uiTopRightColor;
		}

		// Top/Bottom specified.
		if (pColor->uiTopLeftColor && pColor->uiBottomLeftColor &&
			!pColor->uiTopRightColor && !pColor->uiBottomRightColor)
		{
			pColor->uiTopRightColor = pColor->uiTopLeftColor;
			pColor->uiBottomRightColor = pColor->uiBottomLeftColor;
		}
	}
	return PARSERESULT_SUCCESS;
}


#include "Color_h_ast.c"
