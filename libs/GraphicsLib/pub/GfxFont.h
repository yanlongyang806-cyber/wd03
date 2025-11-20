#pragma once
GCC_SYSTEM

#include "referencesystem.h"
#include "Color.h"
#include "RdrEnums.h"

typedef struct BasicTexture BasicTexture;
typedef struct GfxFontData GfxFontData;
typedef struct SpriteProperties SpriteProperties;

AUTO_STRUCT;
typedef struct GfxFont
{
	U8 sizeIsPixels	: 1; AST( NAME(SizeIsPixels) DEFAULT(0) )
	U8 italicize	: 1; AST( NAME(Italicize) DEFAULT(0) )
	U8 bold		: 1; AST( NAME(Bold) DEFAULT(0) )
	U8 dropShadow	: 1; AST( NAME(DropShadow) DEFAULT(0) )
	U8 softShadow  : 1; AST( NAME(SoftShadow) DEFAULT(0) )
	U8 outline  : 1; AST( NAME(Outline) DEFAULT(0) )
	U8 snapToPixels  : 1; AST( NAME(SnapToPixels) DEFAULT(1) )
	U8 outlineWidth; AST( NAME(OutlineWidth) DEFAULT(1) )
	U8 softShadowSpread; AST( NAME(SoftShadowSpread) DEFAULT(2) )
	F32 renderSize; AST( NAME(RenderSize) DEFAULT(16) )
	IVec2 dropShadowOffset; AST( NAME(DropShadowOffset) )
	Color4	color;	  AST( NAME(Color) STRUCT(parse_Color4) )
	U32 uiOutlineColor; AST(NAME(OutlineColor) SUBTABLE(ColorEnum) STRUCTPARAM)
	U32 uiDropShadowColor; AST(NAME(DropShadowColor) SUBTABLE(ColorEnum) STRUCTPARAM)
	F32 aspectRatio; AST(NAME(AspectRatio) DEFAULT(1))
	char* fontDataName; AST( POOL_STRING NAME(Font) )
	GfxFontData* fontData; NO_AST
} GfxFont;

void gfxFontLoadFonts();
void gfxFontUnloadFonts();

char*** gfxFontGetFontNames();

GfxFontData* gfxFontGetFontData(const char* fontName);
GfxFontData* gfxFontGetFallbackFontData();
void gfxFontSetFallbackFontData(GfxFontData* fontData);

GfxFont* gfxFontCreateFromData(GfxFontData* fontData);
GfxFont* gfxFontCreateFromName(const char* fontName);
bool gfxFontInitalizeFromData(GfxFont* font, GfxFontData* fontData);
bool gfxFontInitalizeFromName(GfxFont* font, const char* fontName);

GfxFont* gfxFontCopy(GfxFont* font);
void gfxFontFree(GfxFont* font);

void gfxFontSetFontData(GfxFont* font, GfxFontData* fontData);

void gfxFontPrintEx(GfxFont* font, F32 x, F32 y, F32 z, const char* pchString, const char* pchStopPtr, SpriteProperties *pProps);
static __forceinline void gfxFontPrint(GfxFont* font, F32 x, F32 y, F32 z, const char* pchString, SpriteProperties *pProps)
{
	gfxFontPrintEx(font, x, y, z, pchString, NULL, pProps);
}

void gfxFontSetReportMissingGlyphErrors(bool bReport);

void gfxFontPrintMultiline(GfxFont* font, F32 x, F32 y, F32 z, const char* pchString);

void gfxFontMeasureStringEx(GfxFont* font, const char* pchString, const char* pchStopPtr, Vec2 outSize);
static __forceinline void gfxFontMeasureString(GfxFont* font, const char* pchString, Vec2 outSize)
{
	gfxFontMeasureStringEx(font, pchString, NULL, outSize);
}

static __forceinline F32 gfxFontGetPixelsFromPts(F32 ptSz)
{
	//assume 96-dpi since we can't call GetDeviceCaps here
	return ptSz * 96.0/72.0;
}
static __forceinline F32 gfxFontGetPtsFromPixels(F32 pixelSz)
{
	//assume 96-dpi since we can't call GetDeviceCaps here
	return pixelSz * 72.0/96.0;
}

unsigned int gfxFontCountGlyphsInArea( GfxFont* font, const char* pchString, Vec2 v2AllowedSize);

F32 gfxFontGetGlyphWidth(GfxFont* font, U16 codePt);
F32 gfxFontGetPixelHeight(GfxFont* font);

extern GfxFont g_font_Sans;
extern GfxFont g_font_SansLarge;
extern GfxFont g_font_SansSmall;
extern GfxFont g_font_Mono;
extern GfxFont g_font_Game;
extern GfxFont g_font_GameLarge;
extern GfxFont g_font_GameSmall;

#define FONT_SMALL_RENDER_SIZE 10
#define FONT_NORMAL_RENDER_SIZE 12
#define FONT_LARGE_RENDER_SIZE 16

extern ParseTable parse_GfxFont[];
#define TYPE_parse_GfxFont GfxFont