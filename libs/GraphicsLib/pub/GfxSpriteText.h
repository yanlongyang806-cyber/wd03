/***************************************************************************
 
 
 
 ***************************************************************************/

#ifndef GFXSPRITETEXT_H
#define GFXSPRITETEXT_H
GCC_SYSTEM

#include "GfxFont.h"

typedef struct GfxFont GfxFont;

/////////////////////////////////////////////////////////////////////////
// Possible flags to text drawing functions.
#define CENTER_X   1
#define CENTER_Y   2
#define CENTER_XY  3

//////////////////////////////////////////////////////////////////////////
// Control the current default font rendering. All functions except
// PrintEx and PrintWideEx use these for color and font information.
void gfxfont_SetFont(GfxFont *f);
GfxFont* gfxfont_GetFont(void);
extern GfxFont *g_font_Active;

void gfxfont_SetColor(Color clr0, Color clr1);
void gfxfont_SetColor4(Color colors[4]);
void gfxfont_SetColorRGBA4(U32 colors[4]);
void gfxfont_SetColorRGBA(int rgba0, int rgba1);
void gfxfont_GetColorRGBA(int *rgba0, int *rgba1);
void gfxfont_SetOutlineColorRGBA(int rgba);
void gfxfont_SetDropShadowColorRGBA(int rgba);
void gfxfont_SetFontEx(GfxFont *font, int ital, int bold, int outline, int dropShadow, int clr0, int clr1);
void gfxfont_SetAlpha(U8 cAlpha);
void gfxfont_MultiplyAlpha(U8 cAlpha);

//////////////////////////////////////////////////////////////////////////
// Print UTF-8 or UTF-16 strings without further processing.
void gfxfont_PrintEx(GfxFont *font, F32 x, F32 y, F32 z, F32 xScale, F32 yScale, int flags,
					 const char *str, int charsToPrint, int rgba[4], SpriteProperties *pProps);
void gfxfont_Print(F32 x, F32 y, F32 z, F32 xsc, F32 ysc, int flags, const char *str);

// Print the strings, formatted.
void gfxfont_Printf_actual(F32 x, F32 y, F32 z, F32 xsc, F32 ysc, int flags, SpriteProperties *pProps, FORMAT_STR const char *fmt, ...);
#define gfxfont_PrintfEx(x, y, z, xsc, ysc, flags, pProps, fmt, ...) gfxfont_Printf_actual(x, y, z, xsc, ysc, flags, pProps, FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)
#define gfxfont_Printf(x, y, z, xsc, ysc, flags, fmt, ...) gfxfont_Printf_actual(x, y, z, xsc, ysc, flags, NULL, FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)
void gfxfont_vPrintf(F32 x, F32 y, F32 z, F32 xsc, F32 ysc, int flags, FORMAT_STR const char *fmt, va_list args, SpriteProperties *pProps);

// Print this text, word-wrapped. Return the number of lines it took. If bPrint is
// false, just return the number of lines and don't actually print.
S32 gfxfont_PrintWrapped(F32 x, F32 y, F32 z, F32 w, F32 xscale, F32 yscale, S32 iFlags, bool bPrint, const char *pchString);
S32 gfxfont_PrintfWrapped(F32 x, F32 y, F32 z, F32 w, F32 xscale, F32 yscale, S32 iFlags, bool bPrint, FORMAT_STR const char *fmt, ...);
#define gfxfont_PrintfWrapped(x, y, z, w, xscale, yscale, iFlags, bPrint, fmt, ...) \
	gfxfont_PrintfWrapped(x, y, z, w, xscale, yscale, iFlags, bPrint, FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)

// Print this text, but if it gets too long truncate it and put an elipses at then end.
// Returns number of characters in pchString that were (or will be) consumed. If bPrint is
// false, just return the number of characters and don't actually print.
U32 gfxfont_PrintMaxWidthEx(F32 x, F32 y, F32 z, F32 w, F32 xscale, F32 yscale, S32 iFlags, bool bPrint, bool bEllipsis, const char *pchString);
#define gfxfont_PrintMaxWidth(x, y, z, w, xscale, yscale, iFlags, pchString) \
	gfxfont_PrintMaxWidthEx(x, y, z, w, xscale, yscale, iFlags, true, true, pchString)

S32 gfxfont_PrintWrapped2(F32 x, F32 y, F32 z, F32 w, F32 fFirstLineIndex, F32 fFollowingLineIndent, F32 *pfLastLineWidth, F32 *pfLineHeight, F32 xscale, F32 yscale, S32 iFlags, bool bPrint, const char *pchString);
S32 gfxfont_PrintfWrapped2(F32 x, F32 y, F32 z, F32 w, F32 fFirstLineIndex, F32 fFollowingLineIndent, F32 *pfLastLineWidth, F32 *pfLineHeight, F32 xscale, F32 yscale, S32 iFlags, bool bPrint, FORMAT_STR const char *fmt, ...);
#define gfxfont_PrintfWrapped2(x, y, z, w, fFirstLineIndex, fFollowingLineIndent, pfLastLineWidth, pfLineHeight, xscale, yscale, iFlags, bPrint, fmt, ...) \
	gfxfont_PrintfWrapped2(x, y, z, w, fFirstLineIndex, fFollowingLineIndent, pfLastLineWidth, pfLineHeight, xscale, yscale, iFlags, bPrint, FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)

// Print this text, word-wrapped. Return the number of lines it took. If bPrint is
// false, just return the number of lines and don't actually print.  The first line
// is indented starting at x+fFirstLineIndent, following lines will continue on at 
// x+fFollowingLineIndent. This also returns the width of the last line in pfLastLineWidth, 
// and the height of each line in pfLineHeight.
//
// Here's an example of what could get printed if the text wrapped over multiple lines:
//
//       Axxxxxxxxxxx
// xxxxxxxxxxxxxxxxxx
// BBBB
// 
// Where:
//    A = first character to get printed, which is indented.
//    BBBB = last line to get printed. The width of this line will be written in pfLastLineWidth.
// This is useful for stringing together heterogenously drawn strings of text; e.g. using
// different colors per call to gfxfont_PrintWrappedEx().
// iMaxLineCount controls how many lines are allowed to print. 0 or negative causing all of them to print.
S32 gfxfont_PrintWrappedEx(F32 x, F32 y, F32 z, F32 w, F32 fFirstLineIndex, F32 fFollowingLineIndent, S32 iMaxLineCount, F32 *pfLastLineWidth, F32 *pfLineHeight, F32 xscale, F32 yscale, S32 iFlags, bool bPrint, const char *pchString);
S32 gfxfont_PrintfWrappedEx(F32 x, F32 y, F32 z, F32 w, F32 fFirstLineIndex, F32 fFollowingLineIndent, S32 iMaxLineCount, F32 *pfLastLineWidth, F32 *pfLineHeight, F32 xscale, F32 yscale, S32 iFlags, bool bPrint, FORMAT_STR const char *fmt, ...);
#define gfxfont_PrintfWrappedEx(x, y, z, w, fFirstLineIndex, fFollowingLineIndent, iMaxLineCount, pfLastLineWidth, pfLineHeight, xscale, yscale, iFlags, bPrint, fmt, ...) \
	gfxfont_PrintfWrappedEx(x, y, z, w, fFirstLineIndex, fFollowingLineIndent, iMaxLineCount, pfLastLineWidth, pfLineHeight, xscale, yscale, iFlags, bPrint, FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)

// This function sort of understands newlines and tabs. However, if you want
// real formatting, you should use SMF instead.
void gfxfont_PrintMultiline(F32 x, F32 y, F32 z, F32 xsc, F32 ysc, const char *str);

// Name textures inline with {{{texturename}}}. Should never be used on user input.
void gfxfont_PrintTexturesInline(F32 x, F32 y, F32 z, F32 xscale, F32 yscale, const char *pchOrigStr);


//////////////////////////////////////////////////////////////////////////
// String and font dimension calculation.

void gfxfont_Dimensions(GfxFont *font, F32 xScale, F32 yScale, const char *str, int charsToPrint, F32 *stringWidth, F32 *stringHeight, F32 *nextGlyphLeft, bool cacheable);
void gfxfont_DimensionsWide(GfxFont *font, F32 xScale, F32 yScale, const unsigned short *str, int charsToPrint, F32 *stringWidth, F32 *stringHeight, F32 *nextGlyphLeft, bool cacheable);

F32 gfxfont_StringWidthf(GfxFont * font, F32 xscale, F32 yscale, FORMAT_STR const char *fmt, ...);
#define gfxfont_StringWidthf(font, xscale, yscale, fmt, ...) gfxfont_StringWidthf(font, xscale, yscale, FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)
F32 gfxfont_StringWidth(GfxFont * font, F32 xscale, F32 yscale, const char *str);
F32 gfxfont_StringHeightf(GfxFont * font, F32 xscale, F32 yscale, FORMAT_STR const char *fmt, ...);
#define gfxfont_StringHeightf(font, xscale, yscale, fmt, ...) gfxfont_StringHeightf(font, xscale, yscale, fmt, __VA_ARGS__)

// Return the height of this font as dictated by the font metrics, not the
// rendered height of some particular string.
F32 gfxfont_FontHeight(GfxFont *font, F32 scale);


F32 gfxfont_GetGlyphWidth(SA_PARAM_NN_VALID GfxFont *pContext, U16 usGlyph, F32 fScaleX, F32 fScaleY);
//hack
#define ttGetGlyphWidth gfxfont_GetGlyphWidth

void gfxfont_AddFace(const char *pchName, GfxFont *pFont);
GfxFont *gfxfont_GetFace(const char *pString);

#endif

