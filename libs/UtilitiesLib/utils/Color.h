/* File Color.h
 *	Defines a structure for holding RGBA color information that can be accessed
 *	in 3 different formats.
 *
 *
 */

#ifndef COLOR_H
#define COLOR_H
#pragma once
GCC_SYSTEM


// in memory: b g r a
typedef union ColorBGRA
{
	// Allows direct access to individual components.
	struct{
		U8 b;
		U8 g;
		U8 r;
		U8 a;
	};

	// Allows color component indexing like an array.
	U8 bgra[4];

	U32 integer_for_equality_only; // Not endian safe, only use for equality checking (use union assignment for fast assignment)

} ColorBGRA;

typedef union Color565
{
	// Allows the color to be moved around like an integer.
	U16 integer;

	// Allows direct access to individual components.
	struct{
		U16 b:5;
		U16 g:6;
		U16 r:5;
	};
} Color565;

typedef union Color5551
{
	// Allows the color to be moved around like an integer.
	U16 integer;

	// Allows direct access to individual components.
	struct{
		U16 b:5;
		U16 g:5;
		U16 r:5;
		U16 a:1;
	};
} Color5551;

AUTO_STRUCT;
typedef struct ColorDef
{
	const char *pchName; AST(POOL_STRING KEY STRUCTPARAM)
	U32 uiColor; AST(STRUCTPARAM NAME("UIColor") SUBTABLE(ColorEnum))
} ColorDef;

AUTO_STRUCT;
typedef struct ColorDefLoading
{
	ColorDef **eaColors; AST(NAME(Color))
} ColorDefLoading;

AUTO_STRUCT;
typedef struct Color4
{
	U32 uiTopLeftColor; AST(NAME(TopLeftColor, Color, TopColor, LeftColor) SUBTABLE(ColorEnum) STRUCTPARAM)
	U32 uiTopRightColor; AST(NAME(TopRightColor, RightColor) SUBTABLE(ColorEnum))
	U32 uiBottomRightColor; AST(NAME(BottomRightColor) SUBTABLE(ColorEnum))
	U32 uiBottomLeftColor; AST(NAME(BottomLeftColor, BottomColor) SUBTABLE(ColorEnum))
} Color4;

// Maps color names to RGBA values.
extern StaticDefineInt ColorEnum[];

// Maps color names to Color objects (meaning integer value is RGBA on the PC, ABGR on the Xbox 360)
extern StaticDefineInt ColorPackedEnum[];

// Color constructors
__forceinline static Color CreateColor(U8 r, U8 g, U8 b, U8 a)
{
	Color c; 
	c.r = r;
	c.g = g;
	c.b = b;
	c.a = a;
	return c;
}

__forceinline static Color colorFromU8(U8 *rgba)
{
	Color c; 
	c.r = rgba[0];
	c.g = rgba[1];
	c.b = rgba[2];
	c.a = rgba[3];
	return c;
}

__forceinline static void setU8FromRGBA(U8 *dst, int rgba)
{
	dst[0] = (rgba >> 24) & 0xff;
	dst[1] = (rgba >> 16) & 0xff;
	dst[2] = (rgba >> 8) & 0xff;
	dst[3] = rgba & 0xff;
}

__forceinline static void setColorFromARGB(Color *c, int argb)
{
	c->b = argb & 0xff;
	c->g = (argb >> 8) & 0xff;
	c->r = (argb >> 16) & 0xff;
	c->a = (argb >> 24) & 0xff;
}

__forceinline static void setColorFromABGR(Color *c, int abgr)
{
	c->r = abgr & 0xff;
	c->g = (abgr >> 8) & 0xff;
	c->b = (abgr >> 16) & 0xff;
	c->a = (abgr >> 24) & 0xff;
}

__forceinline static void copyColor(const Color* src, Color* dst)
{
	memcpy(dst, src, sizeof(src));
}

__forceinline static U32 ARGBFromColor(Color c)
{
	return (c.a << 24) | (c.r << 16) | (c.g << 8) | (c.b);
}

__forceinline static Color ARGBToColor(int argb)
{
	Color c;
	setColorFromARGB(&c, argb);
	return c;
}

//assumes input in the range [0 .. 1]
__forceinline static U32 Vec3ToRGBA(Vec3 v3)
{
	return ((int)(v3[0]*255) << 24) | ((int)(v3[1]*255) << 16) | ((int)(v3[2]*255) << 8) | 255;
}

__forceinline static U32 RGBAFromColor(Color c)
{
	return (c.r << 24) | (c.g << 16) | (c.b << 8) | (c.a);
}

__forceinline static void setColorFromRGBA(Color *c, int rgba)
{
	c->a = rgba & 0xff;
	c->b = (rgba >> 8) & 0xff;
	c->g = (rgba >> 16) & 0xff;
	c->r = (rgba >> 24) & 0xff;
}

__forceinline static Color colorFromRGBA(int rgba)
{
	Color c;
	setColorFromRGBA(&c, rgba);
	return c;
}

__forceinline static Color CreateColorRGB(U8 r, U8 g, U8 b)
{
	Color c; 
	c.r = r;
	c.g = g;
	c.b = b;
	c.a = 255;
	return c;
}

Color colorFlip( Color c );
int colorEqualsIgnoreAlpha( Color c1, Color c2 );

#define U8TOF32_COLOR (1.f/255.f)

__forceinline static F32 colorComponentToF32(U8 c)
{
	return c * U8TOF32_COLOR;
}


__forceinline static void colorToVec4(Vec4 vec, const Color clr)
{
	vec[0] = colorComponentToF32(clr.r);
	vec[1] = colorComponentToF32(clr.g);
	vec[2] = colorComponentToF32(clr.b);
	vec[3] = colorComponentToF32(clr.a);
}

__forceinline static void rgbaToVec4(Vec4 vec, int rgba)
{
	vec[0] = colorComponentToF32((rgba >> 24) & 0xff);
	vec[1] = colorComponentToF32((rgba >> 16) & 0xff);
	vec[2] = colorComponentToF32((rgba >> 8) & 0xff);
	vec[3] = colorComponentToF32(rgba & 0xff);
}


__forceinline static void u8ColorToVec4(Vec4 vec, const U8 *clr)
{
	vec[0] = colorComponentToF32(clr[0]);
	vec[1] = colorComponentToF32(clr[1]);
	vec[2] = colorComponentToF32(clr[2]);
	vec[3] = colorComponentToF32(clr[3]);
}

__forceinline static void colorToVec3(Vec3 vec, const Color clr)
{
	vec[0] = colorComponentToF32(clr.r);
	vec[1] = colorComponentToF32(clr.g);
	vec[2] = colorComponentToF32(clr.b);
}

__forceinline static void u8ColorToVec3(Vec3 vec, const U8 *clr)
{
	vec[0] = colorComponentToF32(clr[0]);
	vec[1] = colorComponentToF32(clr[1]);
	vec[2] = colorComponentToF32(clr[2]);
}

__forceinline static int u8ColorToRGBA(const U8* clr)
{
	return (clr[0] << 24) | (clr[1] << 16) | (clr[2] << 8) | (clr[3] << 0);
}

__forceinline static void RGBAToU8Color(U8* clr, const int rgba)
{
	clr[0] = (rgba >> 24) & 0xFF;
	clr[1] = (rgba >> 16) & 0xFF;
	clr[2] = (rgba >>  8) & 0xFF;
	clr[3] = (rgba >>  0) & 0xFF;
}

Color *vec4ToColor(Color *clr, const Vec4 vec);
Color *vec3ToColor(Color *clr, const Vec3 vec);

ColorBGRA *vec4ToColorBGRA(ColorBGRA *clr, const Vec4 vec);

U32 darkenColorRGBA(U32 rgba, int amount);
Color ColorLighten(Color old, int amount);
Color ColorDarken(Color old, int amount);

// Returns a color that is fPercent as bright as the given color.
// i.e. if fPercent=1, then the given color is returned, if fPercent=0
// you get black.
Color ColorDarkenPercent(Color old, F32 percent);

// Returns a color that is fPercent brighter than the given color.
// If fPercent=1, you'll get white.  If fPercent=0, you'll get the 
// given color.
Color ColorLightenPercent(Color old, F32 fPercent);

// By invert, we mean so that the resulting color will always be visible on top of the original color (e.g. CSS definition of invert).
// We do this by converting from RGB to HSV, inverting the Hue, then converting back.
Color ColorInvert(Color old);

#define GET_COLOR_ALPHA(color) ((color) & 0xFF)
#define COLOR_ALPHA(color, alpha) (((color) & 0xFFFFFF00) | ((alpha) & 0xFF))

__forceinline static int lerpRGBAColors(int a, int b, F32 weight)
{
	int out;
	U8* pout = (U8 *)(&out);
	U8* pa = (U8 *)(&a);
	U8* pb = (U8 *)(&b);
	int i;
	F32 weightinv = 1.f - weight;
	for (i = 0; i < 4; i++)
		pout[i] = pa[i] * weightinv + pb[i] * weight;
	return out;
}

__forceinline static Color ColorLerp(Color a, Color b, F32 p)
{
	F32 q = 1.f - p;
    Color c = {{a.r * p + b.r * q, a.g * p + b.g * q, a.b * p + b.b * q, a.a * p + b.a * q}};
	return c;
}

__forceinline static U32 ColorRGBAMultiplyAlpha(U32 iColor, unsigned char chAlpha)
{
	F32 fAlpha = chAlpha * U8TOF32_COLOR;
	S32 iAlpha = (iColor & 0xFF) * fAlpha;
	chAlpha = (iAlpha < 0) ? 0 : (iAlpha > 255 ? 255 : iAlpha);
	return COLOR_ALPHA(iColor, chAlpha);
}


void ColorLoad(void);
Color ColorFromName(const char *pchName);
U32 ColorRGBAFromName(const char *pchName);
void ColorDefLoadingAdd(ColorDefLoading *pColorDefs);
void ColorDefLoadingRefresh(void);

extern Color ColorRed;
extern Color ColorBlue;
extern Color ColorLightBlue;
extern Color ColorGreen;
extern Color ColorWhite;
extern Color ColorBlack;
extern Color ColorLightRed;
extern Color ColorOrange;
extern Color ColorCyan;
extern Color ColorYellow;
extern Color ColorMagenta;
extern Color ColorPurple;
extern Color ColorHalfBlack;
extern Color ColorTransparent;
extern Color ColorGray;


//////////////////////////////////////////////////////////////////////////
// variable size colors

bool VarColorIsZero(U8 *c, int size);
void VarColorNormalize(U8 *c, int size);
void VarColorAverage(const U8 *c1, const U8 *c2, U8 *cOut, int size);
void VarColorLerp(const U8 *c1, F32 t, const U8 *c0, U8 *cOut, int size);
void VarColorScaleAdd(const U8 *c1, F32 scale, const U8 *c2, U8 *cOut, int size);



__forceinline static void ColorNormalize(Color *c)
{
	VarColorNormalize(c->rgba, 4);
}

#endif
