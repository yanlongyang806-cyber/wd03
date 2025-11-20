#pragma once
GCC_SYSTEM

#include "referencesystem.h"
#include "Color.h"

typedef struct BasicTexture BasicTexture;
typedef struct SpriteProperties SpriteProperties;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("EndGlyph");
typedef struct GfxFontGlyphData
{
	U16 codePoint; AST( NAME(CodePoint) )
	U16	texPos[2];	   AST( NAME(TexPos) )
	U16	size[2];	   AST( NAME(Size) )
	U16	xAdvance;	   AST( NAME(XAdvance) )
} GfxFontGlyphData;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("EndGlyph");
typedef struct GfxFontLanguageSubGlyphData
{
	GfxFontGlyphData glyph; AST( NAME(Glyph) EMBEDDED_FLAT)
	const char* pcLangCode; AST( NAME(LangCode) )
} GfxFontLanguageSubGlyphData;

typedef struct GfxFontData GfxFontData;
typedef struct GfxFont GfxFont;


AUTO_STRUCT AST_STARTTOK("Font") AST_ENDTOK("EndFont");
typedef struct GfxFontData
{
	const char* filename; AST(CURRENTFILE KEY)
	const char* commandLine; AST( ESTRING NAME("CommandLine") NO_WRITE) // The command line used to generate this font
	const char* fontTextureFile; AST( POOL_STRING NAME("Texture") )
	const char* substitutionBoldFile; AST( POOL_STRING NAME("SubstitutionBold") )
	const char* substitutionItalicFile; AST( POOL_STRING NAME("SubstitutionItalic") )
	const char* substitutionBoldItalicFile; AST( POOL_STRING NAME("SubstitutionBoldItalic") )
	const char* substitutionMissingGlyphsFile; AST( POOL_STRING NAME("SubstitutionMissingGlyphs") )
	U16		fontSize; AST( NAME(FontSize) )
	U16		spread;	  AST( NAME(Spread) )
	U16		padding[2];	  AST( NAME(Padding) )
	U16		texSize[2];  AST( NAME(TexSize) )
	U16		maxAscent; AST( NAME(MaxAscent) )
	U16		maxDescent; AST( NAME(MaxDescent) )
	S16		verticalShift; AST( NAME(VerticalShift) DEFAULT(SHRT_MIN))
	U8		ignoreBold : 1; AST( NAME(IgnoreBoldStyle) DEFAULT(0))
	U8		ignoreItalic : 1; AST( NAME(IgnoreItalicStyle) DEFAULT(0))
	float	densityOffset; AST( NAME(DensityOffset) DEFAULT(0))
	float	outlineDensityOffset; AST( NAME(OutlineDensityOffset) DEFAULT(-100000))
	float	smoothingAmt; AST( NAME(Smoothing) DEFAULT(1))
	float	outlineSmoothingAmt; AST( NAME(OutlineSmoothing) DEFAULT(-1)) //negative one = 1/2 of smoothingAmt (which was the old behavior)
	float	spacingAdjustment; AST( NAME(SpacingAdjustment) DEFAULT(1))
	U16		extraWidthPixels; AST( NAME(ExtraWidthPixels) DEFAULT(2))
	U16		verticalGradShift; AST( NAME(VerticalGradientShift) DEFAULT(0))
	float	verticalGradientIntensity; AST( NAME(VerticalGradientIntensity) DEFAULT(1))
	GfxFontGlyphData**	eaGlyphData; AST( NAME(Glyph) )
	GfxFontLanguageSubGlyphData**	eaLanguageSubstitutionGlyphData; AST( NAME(GlyphLangSub) )
	BasicTexture* pFontTexture; NO_AST
	StashTable	stFontGlyphs; NO_AST
	GfxFontData* substitutionBold; NO_AST
	GfxFontData* substitutionItalic; NO_AST
	GfxFontData* substitutionBoldItalic; NO_AST
	GfxFontData* substitutionMissingGlyphs; NO_AST
} GfxFontData;

AUTO_STRUCT;
typedef struct GfxFontDataList
{
	GfxFontData** eaFontData; AST( NAME("Font") )
} GfxFontDataList;

extern ParseTable parse_GfxFontGlyphData[];
#define TYPE_parse_GfxFontGlyphData GfxFontGlyphData
extern ParseTable parse_GfxFontData[];
#define TYPE_parse_GfxFontData GfxFontData
extern ParseTable parse_GfxFontDataList[];
#define TYPE_parse_GfxFontDataList GfxFontDataList

//These have WAY too many arguments but I'm trying to keep these the same as the other sprite drawing stuff
typedef void (*gfxFont_display_sprite_distance_field_one_layer_func)(float advWidth, float advHeight, void* userData, BasicTexture *btex, float xp, float yp, float zp, float xscale, float yscale, int rgba, int rgba2, int rgba3, int rgba4, float u1, float v1, float u2, float v2, float angle, float skew, int additive, float uOffsetL1,float vOffsetL1, float minDenL1, float outlineDenL1, float maxDenL1, float tightnessL1, int rgbaMainL1, int rgbaOutlineL1, float verticalGradStart, float verticalGradEnd, SpriteProperties *pProps);
typedef void (*gfxFont_display_sprite_distance_field_two_layers_func)(float advWidth, float advHeight, void* userData, BasicTexture *btex, float xp, float yp, float zp, float xscale, float yscale, int rgba, int rgba2, int rgba3, int rgba4, float u1, float v1, float u2, float v2, float angle, float skew, int additive, float uOffsetL1,float vOffsetL1, float minDenL1, float outlineDenL1, float maxDenL1, float tightnessL1, int rgbaMainL1, int rgbaOutlineL1, float uOffsetL2,float vOffsetL2, float minDenL2, float outlineDenL2, float maxDenL2, float tightnessL2, int rgbaMainL2, int rgbaOutlineL2, float verticalGradStart, float verticalGradEnd, SpriteProperties *pProps);

void gfxFontPrintExWithFuncs(GfxFont* font, F32 x, F32 y, F32 z, const char* pchString, const char* pchStopPtr,
											 gfxFont_display_sprite_distance_field_one_layer_func callback1Layer,
											 gfxFont_display_sprite_distance_field_two_layers_func callback2Layers,
											 void* userData);