/***************************************************************************
 
 
 
 ***************************************************************************/

#include "Color.h"
#include "StringUtil.h"
#include "GfxFont.h"
#include "math.h"

#include "GfxTexAtlas.h"
#include "GfxSpriteText.h"
#include "Clipper.h" //include this before GfxSprite.h when inside GraphicsLib to get the fully inlined clipper functions
#include "GfxSprite.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Fonts););

GfxFont *g_font_Active = &g_font_Sans;

typedef enum{
	QC_topLeft, 
	QC_topRight, 
	QC_bottomRight,
	QC_bottomLeft, 
} QuadRGBA;

void gfxfont_SetOutlineColorRGBA(int rgba)
{
	g_font_Active->uiOutlineColor = rgba;
}

void gfxfont_SetDropShadowColorRGBA(int rgba)
{
	g_font_Active->uiDropShadowColor = rgba;
}

void gfxfont_SetColorRGBA(int rgba0, int rgba1)
{
	g_font_Active->color.uiTopLeftColor = g_font_Active->color.uiTopRightColor = rgba0;
	g_font_Active->color.uiBottomLeftColor = g_font_Active->color.uiBottomRightColor = rgba1;
}

void gfxfont_SetColor4(Color colors[4])
{
	g_font_Active->color.uiTopLeftColor = RGBAFromColor(colors[QC_topLeft]);
	g_font_Active->color.uiTopRightColor = RGBAFromColor(colors[QC_topRight]);
	g_font_Active->color.uiBottomLeftColor = RGBAFromColor(colors[QC_bottomLeft]);
	g_font_Active->color.uiBottomRightColor = RGBAFromColor(colors[QC_bottomRight]);
}

void gfxfont_SetColorRGBA4(U32 colors[4])
{
	g_font_Active->color.uiTopLeftColor =  colors[0];
	g_font_Active->color.uiTopRightColor = colors[QC_topRight];
	g_font_Active->color.uiBottomLeftColor = colors[QC_bottomLeft];
	g_font_Active->color.uiBottomRightColor = colors[QC_bottomRight];
}

void gfxfont_GetColorRGBA(int *rgba0, int *rgba1)
{
	if(rgba0) *rgba0 = g_font_Active->color.uiTopLeftColor;
	if(rgba1) *rgba1 = g_font_Active->color.uiBottomLeftColor;
}

void gfxfont_SetColor(Color clr0, Color clr1)
{
	gfxfont_SetColorRGBA(RGBAFromColor(clr0), RGBAFromColor(clr1));
}

#define MULTIPLY_ALPHA_COMPONENT(a1, a2) \
	((U8)(((a1) * (a2))/255.f))

void gfxfont_MultiplyAlpha(U8 cAlpha)
{
	g_font_Active->color.uiTopLeftColor = (g_font_Active->color.uiTopLeftColor & 0xFFFFFF00) | MULTIPLY_ALPHA_COMPONENT((g_font_Active->color.uiTopLeftColor & 0xFF), cAlpha);
	g_font_Active->color.uiTopRightColor = (g_font_Active->color.uiTopRightColor & 0xFFFFFF00) | MULTIPLY_ALPHA_COMPONENT((g_font_Active->color.uiTopRightColor & 0xFF), cAlpha);
	g_font_Active->color.uiBottomLeftColor = (g_font_Active->color.uiBottomLeftColor & 0xFFFFFF00) | MULTIPLY_ALPHA_COMPONENT((g_font_Active->color.uiBottomLeftColor & 0xFF), cAlpha);
	g_font_Active->color.uiBottomRightColor = (g_font_Active->color.uiBottomRightColor & 0xFFFFFF00) | MULTIPLY_ALPHA_COMPONENT((g_font_Active->color.uiBottomRightColor & 0xFF), cAlpha);
	g_font_Active->uiDropShadowColor = (g_font_Active->uiDropShadowColor & 0xFFFFFF00) | MULTIPLY_ALPHA_COMPONENT((g_font_Active->uiDropShadowColor  & 0xFF), cAlpha);
	g_font_Active->uiOutlineColor = (g_font_Active->uiOutlineColor & 0xFFFFFF00) | MULTIPLY_ALPHA_COMPONENT((g_font_Active->uiOutlineColor & 0xFF), cAlpha);
}

void gfxfont_SetAlpha(U8 cAlpha)
{
	g_font_Active->color.uiTopLeftColor = (g_font_Active->color.uiTopLeftColor & 0xFFFFFF00) | cAlpha;
	g_font_Active->color.uiTopRightColor = (g_font_Active->color.uiTopRightColor & 0xFFFFFF00) | cAlpha;
	g_font_Active->color.uiBottomLeftColor = (g_font_Active->color.uiBottomLeftColor & 0xFFFFFF00) | cAlpha;
	g_font_Active->color.uiBottomRightColor = (g_font_Active->color.uiBottomRightColor & 0xFFFFFF00) | cAlpha;
	g_font_Active->uiDropShadowColor = (g_font_Active->uiDropShadowColor & 0xFFFFFF00) | cAlpha;
	g_font_Active->uiOutlineColor = (g_font_Active->uiOutlineColor & 0xFFFFFF00) | cAlpha;
}

void gfxfont_SetFont(GfxFont * f)
{
	g_font_Active = f;
}

GfxFont* gfxfont_GetFont(void)
{
	return g_font_Active;
}

void gfxfont_SetFontEx(GfxFont * font, int ital, int bold, int outline, int dropShadow, int clr0, int clr1 )
{
	g_font_Active = font;
	g_font_Active->italicize = ital;
	g_font_Active->bold = bold;
	g_font_Active->outline = outline > 0;
	g_font_Active->outlineWidth = outline;
	g_font_Active->dropShadow	= dropShadow ? true : false;
	g_font_Active->dropShadowOffset[0] = dropShadow;
	g_font_Active->dropShadowOffset[1] = dropShadow > 0 ? dropShadow : -dropShadow;
	gfxfont_SetColorRGBA(clr0, clr1);
}

void gfxfont_vPrintf(F32 x, F32 y, F32 z, F32 xsc, F32 ysc, int flags, const char *fmt, va_list args, SpriteProperties *pProps)
{
	static char *s_pchBuffer = NULL;
	estrClear(&s_pchBuffer);
	estrConcatfv(&s_pchBuffer, fmt, args);
	gfxfont_PrintEx(g_font_Active, x, y, z, xsc, ysc, flags, s_pchBuffer, UTF8GetLength(s_pchBuffer), 0, pProps);
}

void gfxfont_PrintEx(GfxFont*	font, float x, float y, float z, float xScale, float yScale, int flags, const char *str, int charsToPrint, int rgba[4], SpriteProperties *pProps)
{
	Vec2 stringSize;
	//the new system doesnt really use scales since they dont really make sense with the new way of representing fonts
	//since you can set the pt size arbitrarily so we can do the scaling here
	F32 oldSize = font->renderSize;
	float oldAspect = font->aspectRatio;
	const char* endStr =  UTF8GetCodepoint(str, charsToPrint);
	Color4 oldColor = font->color;
	
	if(	fabs(xScale) < FLT_EPSILON ||
		fabs(yScale) < FLT_EPSILON)
	{
		return;
	}
	
	font->renderSize *= yScale;
	font->aspectRatio *=  xScale/yScale;
	
	if (rgba)
	{
		font->color.uiTopLeftColor = rgba[QC_topLeft];
		font->color.uiTopRightColor = rgba[QC_topRight];
		font->color.uiBottomLeftColor = rgba[QC_bottomLeft];
		font->color.uiBottomRightColor = rgba[QC_bottomRight];
	}
	

	if (flags & CENTER_X || flags & CENTER_Y)
	{
		gfxFontMeasureStringEx(font, str, endStr, stringSize);
	}
	if (flags & CENTER_X)
	{
		x -= stringSize[0]/2;
	}
	if (flags & CENTER_Y)
	{
		F32 fHeight = stringSize[1];
		y += fHeight / 2.f;
	}
	gfxFontPrintEx(font, x, y, z, str, endStr, pProps);

	font->renderSize = oldSize;
	font->aspectRatio =  oldAspect;
	if (rgba)
		font->color = oldColor;
}

void gfxfont_Dimensions(GfxFont* font, float xScale, float yScale, const char *str, int charsToPrint, float* stringWidth, float* stringHeight, float* nextGlyphLeft, bool cacheable)
{
	Vec2 size;
	F32 oldSize = font->renderSize;
	float oldAspect = font->aspectRatio;

	PERFINFO_AUTO_START(__FUNCTION__, 1);

	font->renderSize *= yScale;
	font->aspectRatio *=  yScale ? xScale/yScale : 0;

	gfxFontMeasureStringEx(font, str, UTF8GetCodepoint(str, charsToPrint), size);
	if (stringWidth) *stringWidth = size[0];
	if (stringHeight) *stringHeight = size[1];
	if (nextGlyphLeft) *nextGlyphLeft += size[0];

	font->renderSize = oldSize;
	font->aspectRatio =  oldAspect;

	PERFINFO_AUTO_STOP();
}

void gfxfont_Print(F32 x, F32 y, F32 z, F32 xsc, F32 ysc, int flags, const char *str)
{
	gfxfont_PrintEx(g_font_Active, x, y, z, xsc, ysc, flags, str, UTF8GetLength(str), 0, NULL);
}

U32 gfxfont_PrintMaxWidthEx(F32 x, F32 y, F32 z, F32 w, F32 xscale, F32 yscale, S32 iFlags, bool bPrint, bool bEllipsis, const char *pchString)
{
	F32 textWidth = gfxfont_StringWidth(g_font_Active, xscale, yscale, pchString);
	char* text;
	U32 iNumCharacters = 0;

	if( textWidth > w && w > 0 ) {
		F32 elipsesWidth = bEllipsis ? gfxfont_StringWidth(g_font_Active, xscale, yscale, "…" ) : 0;

		if( w - elipsesWidth <= 0 ) {
			text = bEllipsis ? "..." : "";
		} else {
			// This could be written much better if there was
			// just a function maxNumCharsGivenWidth(str)
			char* estr = estrCreateFromStr( pchString );
			iNumCharacters = UTF8GetLength( estr );
			while( estrLength(&estr) > 0 && gfxfont_StringWidth(g_font_Active, xscale, yscale, estr) > w - elipsesWidth ) {
				iNumCharacters--;
				estrSetSize(&estr, UTF8GetCodepointOffset(estr, iNumCharacters));
			}

			estrConcatf( &estr, "..." );
			strdup_alloca( text, estr );
			estrDestroy( &estr );
		}
	} else {
		text = (char*)pchString;
		iNumCharacters = strlen(pchString);
	}

	if(bPrint)
		gfxfont_Print(x, y, z, xscale, yscale, iFlags, text);

	return iNumCharacters;
}

S32 gfxfont_PrintWrapped(F32 x, F32 y, F32 z, F32 w, F32 xscale, F32 yscale, S32 iFlags, bool bPrint, const char *pchString) 
{
	F32 fLastLineWidth;
	F32 fLineHeight;
	return gfxfont_PrintWrapped2(x, y, z, w, 0, 0, &fLastLineWidth, &fLineHeight, xscale, yscale, iFlags, bPrint, pchString);
}

S32 gfxfont_PrintWrapped2(F32 x, F32 y, F32 z, F32 w, F32 fFirstLineIndent, F32 fFollowingLineIndent, F32 *pfLastLineWidth, F32 *pfLineHeight, F32 xscale, F32 yscale, S32 iFlags, bool bPrint, const char *pchString)
{
	return gfxfont_PrintWrappedEx(x, y, z, w, fFirstLineIndent, fFollowingLineIndent, -1, pfLastLineWidth, pfLineHeight, xscale, yscale, iFlags, bPrint, pchString);
}

S32 gfxfont_PrintWrappedEx(F32 x, F32 y, F32 z, F32 w, F32 fFirstLineIndent, F32 fFollowingLineIndent, S32 iMaxLineCount, F32 *pfLastLineWidth, F32 *pfLineHeight, F32 xscale, F32 yscale, S32 iFlags, bool bPrint, const char *pchString)
{
	F32 fStartX = x + fFirstLineIndent;
	F32 fWidth = 0.f;
	F32 fIndent = fFirstLineIndent;
	S32 iLineNum = 1;
	S32 iLength = 0;
	F32 fBestLineEndWidth = 0.f;
	F32 fSecondBestLineEndWidth = 0.f;
	F32 fLineHeight = gfxfont_FontHeight(g_font_Active, yscale);
	F32 fLastLineWidth = 0.f;
	const char *pchBestEnd = NULL;
	const char *pchSecondBestEnd = NULL;
	const char *pchCheckEnd = NULL;

	for (pchCheckEnd = pchString; *pchCheckEnd; pchCheckEnd = UTF8GetNextCodepoint(pchCheckEnd))
	{
		switch (*pchCheckEnd)
		{
		//handle any combination of /r, /n, /n/r, /r/n
		case '\r':
		case '\n':
			{
				char* charReturnPtr = UTF8GetNextCodepoint(pchCheckEnd);
				if ((*charReturnPtr == '\r' || *charReturnPtr == '\n') && *charReturnPtr != *pchCheckEnd)
				{
					pchCheckEnd = charReturnPtr;
				}

				if (bPrint)
				{
					gfxfont_PrintEx(g_font_Active, fStartX, y, z, xscale, yscale, iFlags, pchString, iLength, 0, NULL);
				}

				iLineNum++;

				y += fLineHeight;
				pchString = UTF8GetNextCodepoint(pchCheckEnd);
				pchBestEnd = NULL;
				fBestLineEndWidth = 0.f;
				fWidth = 0.f;
				fLastLineWidth = 0; // If the string ends in a newline, then the last line has a width of 0.
				iLength = 0;
				fIndent = 0.f;
				fStartX = x + fFollowingLineIndent;
			}
			break;
		case ' ':
			pchBestEnd = pchCheckEnd;
			fBestLineEndWidth = fWidth;
			// NO BREAK HERE!!! - We want to fall through to the default behavior below
		default:
			// Optimization to avoid UTF8GetLength calls.
			fWidth += gfxfont_GetGlyphWidth(g_font_Active, UTF8ToWideCharConvert(pchCheckEnd), xscale, yscale);

			if (fIndent + fWidth >= w)
			{
				bool bUsedSecondBest = false;

				if (!pchBestEnd)
				{
					pchBestEnd = pchSecondBestEnd;
					fBestLineEndWidth = fSecondBestLineEndWidth;
					bUsedSecondBest = true;
				}
				if (!pchBestEnd)
				{
					break;
				}
				if (bPrint)
				{
					gfxfont_PrintEx(g_font_Active, fStartX, y, z, xscale, yscale, iFlags, pchString, UTF8PointerToCodepointIndex(pchString, pchBestEnd), 0, NULL);
				}

				iLineNum++;

				fLastLineWidth = fBestLineEndWidth;

				y += fLineHeight;
				pchString = bUsedSecondBest ? pchBestEnd : UTF8GetNextCodepoint(pchBestEnd);
				pchCheckEnd = pchString;
				pchSecondBestEnd = UTF8GetNextCodepoint(pchCheckEnd); // so that it will always be at least one character long

				pchBestEnd = NULL;
				fBestLineEndWidth = 0.f;
				fStartX = x + fFollowingLineIndent;
				fWidth = 0;
				fIndent = 0.f;
				iLength = 0;

				// Initialize width of next line to be width of first character because we're going to skip it when we loop
				fWidth = gfxfont_GetGlyphWidth(g_font_Active, UTF8ToWideCharConvert(pchCheckEnd), xscale, yscale);
			}
			else
			{
				iLength++;
				pchSecondBestEnd = pchCheckEnd;
			}
			fSecondBestLineEndWidth = fWidth;
			break;
		}

		if(iMaxLineCount > 0 && iLineNum > iMaxLineCount)
			break;
	}

	// Print the remainder of the string
	if(iMaxLineCount <= 0 || iLineNum <= iMaxLineCount)
	{
		if (pchString && *pchString) 
		{
			fLastLineWidth = fWidth;
			if (bPrint) 
			{
				gfxfont_PrintEx(g_font_Active, fStartX, y, z, xscale, yscale, iFlags, pchString, UTF8GetLength(pchString), 0, NULL);
			}
		}
	}

	// Note: We use fLineHeight because we don't know if pfLineHeight
	//       is sharing data with another variable (which can occur if
	//       the caller doesn't care about the results).
	//       Same goes for pfLastLineWidth.
	*pfLineHeight = fLineHeight;
	*pfLastLineWidth = fLastLineWidth;

	return iLineNum;
}

#undef gfxfont_PrintfWrapped
S32 gfxfont_PrintfWrapped(F32 x, F32 y, F32 z, F32 w, F32 xscale, F32 yscale, S32 iFlags, bool bPrint, const char *fmt, ...)
{
	va_list va;
	static char *s_pchBuffer = NULL;
	estrClear(&s_pchBuffer);
	va_start(va, fmt);
	estrConcatfv(&s_pchBuffer, fmt, va);
	va_end(va);
	return gfxfont_PrintWrapped(x, y, z, w, xscale, yscale, iFlags, bPrint, s_pchBuffer);
}

#undef gfxfont_PrintfWrapped2
S32 gfxfont_PrintfWrapped2(F32 x, F32 y, F32 z, F32 w, F32 fFirstLineIndent, F32 fFollowingLineIndent, F32 *pfLastLineWidth, F32 *pfLineHeight, F32 xscale, F32 yscale, S32 iFlags, bool bPrint, const char *fmt, ...)
{
	va_list va;
	static char *s_pchBuffer = NULL;
	estrClear(&s_pchBuffer);
	va_start(va, fmt);
	estrConcatfv(&s_pchBuffer, fmt, va);
	va_end(va);
	return gfxfont_PrintWrapped2(x, y, z, w, fFirstLineIndent, fFollowingLineIndent, pfLastLineWidth, pfLineHeight, xscale, yscale, iFlags, bPrint, s_pchBuffer);
}

void gfxfont_PrintTexturesInline(F32 x, F32 y, F32 z, F32 xscale, F32 yscale, const char *pchOrigStr)
{
	char *pchString = NULL;
	estrStackCreate(&pchString);

	while (pchOrigStr && *pchOrigStr)
	{
		const char *pchNextImage = strstr(pchOrigStr, "{{{");
		estrClear(&pchString);
		if (pchNextImage)
			estrConcat(&pchString, pchOrigStr, pchNextImage - pchOrigStr);
		else
			estrAppend2(&pchString, pchOrigStr);

		gfxfont_Printf(x, y, z, xscale, yscale, CENTER_Y, "%s", pchString);
		x += gfxfont_StringWidth(g_font_Active, xscale, yscale, pchString);

		if (pchNextImage)
		{
			pchOrigStr = strstr(pchNextImage, "}}}");
			pchNextImage += 3;
			if (devassertmsg(pchOrigStr, "Invalid inline texture string passed to function"))
			{
				AtlasTex *pTex;
				estrClear(&pchString);
				estrConcat(&pchString, pchNextImage, pchOrigStr - pchNextImage);
				pTex = atlasLoadTexture(pchString);
				display_sprite(pTex, x, y - pTex->height * yscale / 2, z, xscale, yscale, 0xFFFFFFFF);
				x += pTex->width * xscale;
				pchOrigStr += 3;
			}
		}
		else
		{
			pchOrigStr = NULL;
		}
	}

	estrDestroy(&pchString);
}

void gfxfont_PrintMultiline(F32 x, F32 y, F32 z, F32 xsc, F32 ysc, const char *str)
{
	F32 oldSize = g_font_Active->renderSize;
	float oldAspect = g_font_Active->aspectRatio;

	g_font_Active->renderSize *= ysc;
	g_font_Active->aspectRatio *=  xsc/ysc;

	gfxFontPrintMultiline(g_font_Active, x, y, z, str);

	g_font_Active->renderSize = oldSize;
	g_font_Active->aspectRatio =  oldAspect;
}

void gfxfont_Printf_actual(F32 x, F32 y, F32 z, F32 xsc, F32 ysc, int flags, SpriteProperties *pProps, const char *fmt, ...)
{
	va_list va;
	va_start(va, fmt);
	gfxfont_vPrintf(x, y, z, xsc, ysc, flags, fmt, va, pProps);
	va_end(va);
}

#undef gfxfont_StringWidthf
F32 gfxfont_StringWidthf(GfxFont * font, F32 xscale, F32 yscale, const char *fmt, ...)
{
	va_list va;
	static char *s_pchBuffer = NULL;
	estrClear(&s_pchBuffer);
	va_start(va, fmt);
	estrConcatfv(&s_pchBuffer, fmt, va);
	va_end(va);
 	return gfxfont_StringWidth(font, xscale, yscale, s_pchBuffer);
}

F32 gfxfont_StringWidth(GfxFont * font, F32 xscale, F32 yscale, const char *str)
{
	F32 wd = 0;
	gfxfont_Dimensions(font, xscale, yscale, str, (int)UTF8GetLength(str), &wd, NULL, NULL, true);
	return wd;
}

#undef gfxfont_StringHeightf
F32 gfxfont_StringHeightf(GfxFont * font, F32 xscale, F32 yscale, const char *fmt, ...)
{
	static char *s_pchBuffer = NULL;
	va_list va;
	F32 fHeight = 0;
	estrClear(&s_pchBuffer);
	va_start(va, fmt);
	estrConcatfv(&s_pchBuffer, fmt, va);
	va_end(va);
	gfxfont_Dimensions(font, xscale, yscale, s_pchBuffer, UTF8GetLength(s_pchBuffer), NULL, &fHeight, NULL, true);
	return fHeight;
}

F32 gfxfont_FontHeight(GfxFont *font, F32 scale)
{
	return ceilf(gfxFontGetPixelHeight(font) * scale);
}

F32 gfxfont_GetGlyphWidth( GfxFont *font, U16 codePt, F32 fScaleX, F32 fScaleY )
{
	F32 oldSize = font->renderSize;
	float oldAspect = font->aspectRatio;
	F32 retVal = 1;

	// if fScaleY is 0, the divide by 0 is an exception. But returning 1 for the glyph width at least won't crash.
	if( !nearz(fScaleY) )
	{
		font->renderSize *= fScaleY;
		font->aspectRatio *=  fScaleX/fScaleY;

		retVal = gfxFontGetGlyphWidth(font, codePt);

		font->renderSize = oldSize;
		font->aspectRatio =  oldAspect;
	}

	return retVal;
}
