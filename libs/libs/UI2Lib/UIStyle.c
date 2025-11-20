#include "CBox.h"
#include "Color.h"
#include "StringUtil.h"
#include "ResourceManager.h"
#include "file.h" // For opening files in the BarBrowser. I might want to move this to UIGenDebug.c --ama
#include "FolderCache.h"
#include "rgb_hsv.h"

#include "GraphicsLib.h"
#include "GfxSprite.h"
#include "GfxSpriteText.h"
#include "GfxTexAtlas.h"
#include "GfxClipper.h"
#include "UITextureAssembly.h"

#include "UICore.h"
#include "UILabel.h"
#include "UITextEntry.h"
#include "UIStyle.h"
#include "UISpinner.h"
#include "UISlider.h"
#include "UIPane.h"
#include "UIList.h"
#include "UIWindow.h"

#include "UICore_h_ast.h"
#include "UIStyle_h_ast.c"
#include "UITextureAssembly_h_ast.h"
#include "AutoGen/Color_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

DictionaryHandle g_ui_FontDict = NULL;
DictionaryHandle g_ui_BorderDict = NULL;
DictionaryHandle g_ui_BarDict = NULL;

void ui_StyleLoadColorPalettes(void);

UIStyleFont *ui_StyleFontCreate(const char *pchName, GfxFont *pContext, Color color, bool bBold, bool bItalic, S32 iOutlineWidth)
{
	UIStyleFont *pFont = StructCreate(parse_UIStyleFont);
	pFont->pchName = StructAllocString(pchName);
	if (pContext)
		SET_HANDLE_FROM_REFERENT("GfxFont", pContext, pFont->hFace);
	pFont->bBold = bBold;
	pFont->bItalic = bItalic;
	pFont->iOutlineWidth = iOutlineWidth;
	pFont->uiColor = RGBAFromColor(color);
	pFont->uiSelectedColor = RGBAFromColor(color);
	return pFont;
}

void ui_StyleFontRegister(UIStyleFont* pFont)
{
	RefSystem_AddReferent(g_ui_FontDict, pFont->pchName, pFont);
}

UIStyleFont *ui_StyleFontGet(const char *pchName)
{
	return pchName ? RefSystem_ReferentFromString(g_ui_FontDict, pchName) : NULL;
}

GfxFont *ui_StyleFontGetContext(UIStyleFont *pFont)
{
	GfxFont *pContext = GET_REF(pFont->hFace);
	if (pContext)
		return pContext;
	else
		return &g_font_Sans;
}

__forceinline static Color ui_StyleApplyModifiers(Color color, UIWidgetModifier eMods)
{
	if (eMods & kWidgetModifier_Inactive)
	{
		color.a = color.a / 2;
		return color;
	}

	if (eMods & kWidgetModifier_Pressed)
		color = ColorDarken(color, 32);
	else if (eMods & (kWidgetModifier_Hovering | kWidgetModifier_Focused))
		color = ColorLighten(color, 32);

	if (eMods & kWidgetModifier_Changed)
		color.r = min(color.r + 32, 255);
	if (eMods & kWidgetModifier_Inherited)
		color.b = min(color.b + 32, 255);

	return color;
}

void ui_StyleFontUse(UIStyleFont *pFont, bool bSelected, UIWidgetModifier eMods)
{
	U32 uiColor;
	if (pFont == NULL)
		pFont = GET_REF(g_ui_State.font);
	assertmsg(pFont && GET_REF(pFont->hFace), "Font has an invalid face");
	uiColor = bSelected && !pFont->bNoAutomaticColors ? pFont->uiSelectedColor : pFont->uiColor;
	gfxfont_SetFontEx(GET_REF(pFont->hFace), pFont->bItalic, pFont->bBold, pFont->iOutlineWidth, pFont->iShadowOffset, 0, 0);
	//gfxfont_SetFontEx sets g_font_Active
	g_font_Active->snapToPixels = !pFont->bDontSnapToPixels;
	if (uiColor)
	{
		uiColor = ui_StyleColorPaletteIndex(uiColor);
		if( !pFont->bNoAutomaticColors ) {
			uiColor = RGBAFromColor(ui_StyleApplyModifiers(colorFromRGBA(uiColor), eMods & ~(kWidgetModifier_Focused | kWidgetModifier_Hovering)));
		}
		gfxfont_SetColorRGBA(uiColor, uiColor);
	}
	else
	{
		S32 i;
		U32 aModColors[4];
		if ((eMods & kWidgetModifier_Inactive) && pFont->bNoAutomaticColors)
		{
			aModColors[0] = ui_StyleColorPaletteIndex(pFont->uiTopLeftInactiveColor);
			aModColors[1] = ui_StyleColorPaletteIndex(pFont->uiTopRightInactiveColor);
			aModColors[2] = ui_StyleColorPaletteIndex(pFont->uiBottomRightInactiveColor);
			aModColors[3] = ui_StyleColorPaletteIndex(pFont->uiBottomLeftInactiveColor);
		}
		else if (bSelected)
		{
			aModColors[0] = ui_StyleColorPaletteIndex(pFont->uiTopLeftSelectedColor);
			aModColors[1] = ui_StyleColorPaletteIndex(pFont->uiTopRightSelectedColor);
			aModColors[2] = ui_StyleColorPaletteIndex(pFont->uiBottomRightSelectedColor);
			aModColors[3] = ui_StyleColorPaletteIndex(pFont->uiBottomLeftSelectedColor);
		}
		else
		{
			aModColors[0] = ui_StyleColorPaletteIndex(pFont->uiTopLeftColor);
			aModColors[1] = ui_StyleColorPaletteIndex(pFont->uiTopRightColor);
			aModColors[2] = ui_StyleColorPaletteIndex(pFont->uiBottomRightColor);
			aModColors[3] = ui_StyleColorPaletteIndex(pFont->uiBottomLeftColor);
		}

		if (eMods && !pFont->bNoAutomaticColors)
			for (i = 0; i < ARRAY_SIZE(aModColors); i++)
				aModColors[i] = RGBAFromColor(ui_StyleApplyModifiers(colorFromRGBA(aModColors[i]), eMods));
		gfxfont_SetColorRGBA4(aModColors);
	}
	gfxfont_SetOutlineColorRGBA(ui_StyleColorPaletteIndex(pFont->uiOutlineColor));
	gfxfont_SetDropShadowColorRGBA(ui_StyleColorPaletteIndex(pFont->uiDropShadowColor));
}

F32 ui_StyleFontHeightWrapped(UIStyleFont *pFont, F32 fWidth, F32 fScale, const char *pchText)
{
	F32 fFontHeight = ui_StyleFontLineHeight(pFont, fScale);
	return gfxfont_PrintWrapped(0, 0, 0, fWidth, fScale, fScale, 0, false, pchText) * fFontHeight;
}

F32 ui_StyleFontWidth(UIStyleFont *pFont, F32 fScale, const char *pchText)
{
	F32 fWidth = 0.f;
	U32 iColor1, iColor2;
	if (!pchText)
		return 0;
	if (!pFont)
		pFont = GET_REF(g_ui_State.font);
	assertmsg(pFont && GET_REF(pFont->hFace), "Font has an invalid face");
	gfxfont_GetColorRGBA(&iColor1, &iColor2);
	gfxfont_SetFontEx(GET_REF(pFont->hFace), pFont->bItalic, pFont->bBold, pFont->iOutlineWidth, pFont->iShadowOffset, iColor1, iColor2);
	gfxfont_Dimensions(GET_REF(pFont->hFace), fScale, fScale, pchText, UTF8GetLength(pchText), &fWidth, NULL, NULL, true);
	return fWidth;
}

F32 ui_StyleFontWidthNoCache(UIStyleFont *pFont, F32 fScale, const char *pchText)
{
	F32 fWidth = 0.f;
	U32 iColor1, iColor2;
	if (!pFont)
		pFont = GET_REF(g_ui_State.font);
	assertmsg(pFont && GET_REF(pFont->hFace), "Font has an invalid face");
	assert(pchText);
	gfxfont_GetColorRGBA(&iColor1, &iColor2);
	gfxfont_SetFontEx(GET_REF(pFont->hFace), pFont->bItalic, pFont->bBold, pFont->iOutlineWidth, pFont->iShadowOffset, iColor1, iColor2);
	gfxfont_Dimensions(GET_REF(pFont->hFace), fScale, fScale, pchText, UTF8GetLength(pchText), &fWidth, NULL, NULL, false);
	return fWidth;
}

void ui_StyleFontDimensions(UIStyleFont *pFont, F32 fScale, const char *pchText, F32 *pfWidth, F32 *pfHeight, bool cacheable)
{
	F32 fWidth = 0.f;
	U32 iColor1, iColor2;
	if (!pFont)
		pFont = GET_REF(g_ui_State.font);
	assertmsg(pFont && GET_REF(pFont->hFace), "Font has an invalid face");
	assert(pchText);
	gfxfont_GetColorRGBA(&iColor1, &iColor2);
	gfxfont_SetFontEx(GET_REF(pFont->hFace), pFont->bItalic, pFont->bBold, pFont->iOutlineWidth, pFont->iShadowOffset, iColor1, iColor2);
	gfxfont_Dimensions(GET_REF(pFont->hFace), fScale, fScale, pchText, UTF8GetLength(pchText), pfWidth, pfHeight, NULL, cacheable);
}

F32 ui_StyleFontLineHeight(UIStyleFont *pFont, F32 fScale)
{
	GfxFont *pCtx;
	U32 iColor1, iColor2;
	if (!pFont)
		pFont = GET_REF(g_ui_State.font);
	assertmsg(pFont && GET_REF(pFont->hFace), "Font has an invalid face");
	pCtx = GET_REF(pFont->hFace);
	gfxfont_GetColorRGBA(&iColor1, &iColor2);
	gfxfont_SetFontEx(pCtx, pFont->bItalic, pFont->bBold, pFont->iOutlineWidth, pFont->iShadowOffset, iColor1, iColor2);
	return gfxfont_FontHeight(pCtx, fScale);
}

unsigned int ui_StyleFontCountGlyphsInArea(UIStyleFont *pFont, F32 fScale, const char *pchText, Vec2 v2AllowedArea)
{
	F32 fWidth = 0.f;
	U32 iColor1, iColor2;
	if (!pFont)
		pFont = GET_REF(g_ui_State.font);
	assertmsg(pFont && GET_REF(pFont->hFace), "Font has an invalid face");
	assert(pchText);
	gfxfont_GetColorRGBA(&iColor1, &iColor2);
	gfxfont_SetFontEx(GET_REF(pFont->hFace), pFont->bItalic, pFont->bBold, pFont->iOutlineWidth, pFont->iShadowOffset, iColor1, iColor2);
	return gfxFontCountGlyphsInArea(GET_REF(pFont->hFace), pchText, v2AllowedArea);
}

Color ui_StyleFontGetColor(UIStyleFont *pFont)
{
	if (!pFont)
		pFont = GET_REF(g_ui_State.font);
	assertmsg(pFont && GET_REF(pFont->hFace), "Font has an invalid face");
	return colorFromRGBA(ui_StyleColorPaletteIndex(pFont->uiColor ? pFont->uiColor : pFont->uiTopLeftColor));
}

U32 ui_StyleFontGetColorValue(UIStyleFont *pFont)
{
	if (!pFont)
		pFont = GET_REF(g_ui_State.font);
	assertmsg(pFont && GET_REF(pFont->hFace), "Font has an invalid face");
	return ui_StyleColorPaletteIndex(pFont->uiColor ? pFont->uiColor : pFont->uiTopLeftColor);
}

static bool ResourceHasMatchingFilename(const char *pchName, const char *pchFilename, const char *pchExtension)
{
	if (pchFilename && pchName && pchExtension && strEndsWith(pchFilename, pchExtension))
	{
		const char *pchBase = strrchr(pchFilename, '/');
		if (pchBase)
		{
			char achDesired[MAX_PATH];
			sprintf(achDesired, "%s%s", pchName, pchExtension);
			return !(stricmp(achDesired, pchBase + 1));
		}
	}
	return true;
}

static void ui_StyleFontResEvent(enumResourceEventType eType, const char *pDictName, const char *pFontName, UIStyleFont *pFont, void* pUnused)
{
	switch (eType)
	{
	case RESEVENT_RESOURCE_ADDED:
	case RESEVENT_RESOURCE_MODIFIED: 
		{
			UIStyleFont *pBorrow = GET_REF(pFont->hBorrowFrom);
			if (pBorrow)
			{
				UIStyleFont *pNestedBorrow = GET_REF(pBorrow->hBorrowFrom);
				if (pNestedBorrow)
				{
					ErrorFilenamefInternal(__FILE__, __LINE__, pFont->pchFilename, "%s: Nested font BorrowFroms are not allowed.", pFont->pchName);
				}
				if (stricmp(pBorrow->pchFilename, pFont->pchFilename) == 0)
				{
					UIStyleFont *pTemp = StructClone(parse_UIStyleFont, pBorrow);
					
					// uiColor and the directional colors clash. If both exist, remove one manually.
					bool bRemoveColor = pBorrow->uiColor && (pFont->uiTopLeftColor || pFont->uiTopRightColor || pFont->uiBottomLeftColor || pFont->uiBottomRightColor);
					bool bRemoveCornerColors = pFont->uiColor && (pBorrow->uiTopLeftColor || pBorrow->uiTopRightColor || pBorrow->uiBottomLeftColor || pBorrow->uiBottomRightColor);

					StructOverride(parse_UIStyleFont, pTemp, pFont, true, true, true);
					StructCopyAll(parse_UIStyleFont, pTemp, pFont);
					StructDestroy(parse_UIStyleFont, pTemp);

					if (bRemoveColor)
					{
						pFont->uiColor = 0;
					}
					else if (bRemoveCornerColors)
					{
						pFont->uiTopLeftColor = pFont->uiTopRightColor = pFont->uiBottomLeftColor = pFont->uiBottomRightColor = 0;
					}
				}
				else
				{
					ErrorFilenamefInternal(__FILE__, __LINE__, pFont->pchFilename, "%s: Font borrows from a StyleFont that is not in the same file.", pFont->pchName);
				}
			}
		}
	}
}

AUTO_FIXUPFUNC;
TextParserResult ui_StyleFontParserFixup(UIStyleFont *pFont, enumTextParserFixupType eType, void *pExtraData)
{
	if (eType == FIXUPTYPE_POST_BIN_READ || eType == FIXUPTYPE_POST_RELOAD || eType == FIXUPTYPE_POST_TEXT_READ)
	{
		g_ui_State.uiLastFontLoad = g_ui_State.uiFrameCount;
	}
	return PARSERESULT_SUCCESS;
}


UIStyleBorder *ui_StyleBorderGet(const char *pchName)
{
	return RefSystem_ReferentFromString(g_ui_BorderDict, pchName);
}

#define ATLAS_TEX_TO_BASIC_TEX(pAtlasTex) texLoadBasic((pAtlasTex)->name, TEX_LOAD_IN_BACKGROUND, WL_FOR_UI)

__forceinline static void CalcXYScaleForCorner(AtlasTex *pThis, AtlasTex *pVert, AtlasTex *pHoriz, F32 fBorderHeight, F32 fBorderWidth, F32 fScale, F32 *pfXScale, F32 *pfYScale)
{
	F32 fHeight = fScale * ((pThis ? pThis->height : 0) + (pVert ? pVert->height : 0));
	F32 fWidth = fScale * ((pThis ? pThis->width : 0) + (pHoriz ? pHoriz->width : 0));

	if(pfYScale)
	{
		*pfYScale = fScale;
		if(fHeight > fBorderHeight)
			*pfYScale *= fBorderHeight/fHeight;
	}

	if(pfXScale)
	{
		*pfXScale = fScale;
		if(fWidth > fBorderWidth)
			*pfXScale *= fBorderWidth/fWidth;
	}

	// I've decided to always scale X and Y evenly. The smallest wins.
	if(pfYScale && pfXScale)
	{
		if(*pfYScale > *pfXScale)
			*pfYScale=*pfXScale;
		else
			*pfXScale=*pfYScale;
	}
}

static void ui_StyleDisplayPatternLayer( AtlasTex* pLayer2, AtlasTex* mask, float lx, float ly, float hx, float hy, float z, float scale, const CBox* pBox )
{
	display_sprite_effect_ex(
			NULL, ATLAS_TEX_TO_BASIC_TEX(pLayer2), NULL, ATLAS_TEX_TO_BASIC_TEX(mask),
			lx, ly, z,
			(hx - lx) / pLayer2->width, (hy - ly) / pLayer2->height,
			-1, -1, -1, -1,
			(lx - pBox->lx) / pLayer2->width * scale, (ly - pBox->ly) / pLayer2->height * scale,
			(hx - pBox->lx) / pLayer2->width * scale, (hy - pBox->ly) / pLayer2->height * scale,
			0, 0, (hx - lx) / mask->width * scale, (hy - ly) / mask->height * scale,
			0, 0, clipperGetCurrent(), RdrSpriteEffect_TwoTex, 0, SPRITE_2D);
}

U32 interpColorBox(U32 colors[4], const CBox* box, F32 x, F32 y)
{
	if( nearf( box->hx, box->lx ) || nearf( box->hy, box->ly ) ) {
		return colors[0];
	} else { 
		return interpBilinearColor( (x - box->lx) / (box->hx - box->lx), (y - box->ly) / (box->hy - box->ly), colors );
	}
}

void ui_StyleBorderDrawEx(UIStyleBorder *pBorder, const CBox *pBox, U32 aiOuterColors[4], U32 aiInnerColors[4], F32 centerX, F32 centerY, F32 rot, F32 fZ, F32 fScale, U8 chAlpha)
{
	AtlasTex *pTopLeft, *pTop, *pTopRight;
	AtlasTex *pLeft, *pRight;
	AtlasTex *pBottomLeft, *pBottom, *pBottomRight;
	AtlasTex *pBackground;
	AtlasTex *pPattern;
	const F32 fRaise = 0.001f; // Between the background and border
	bool bBorderTiled = pBorder ? (pBorder->eBorderType == UITextureModeTiled) : false;
	S32 i;
	bool bVisible = false;
	CBox floored = *pBox;
	U32* fullInterpColors = NULL;

	F32 fBorderHeight = pBox->hy - pBox->ly;
	F32 fBorderWidth = pBox->hx - pBox->lx;
	F32 fXScale, fYScale;

	if (!pBorder)
		pBorder = GET_REF(g_ui_Tex.hCapsule);
	if (!pBorder)
	{
		Errorf("Default capsule border not found, is your core data missing?");
		return;
	}

	CBoxInt(&floored);
	pBox = &floored;

	// Array arguments are pointers, not arrays, so ARRAY_SIZE doesn't work.  Just hardcoding 4 for now.
	if (aiOuterColors) {
		for (i = 0; i < 4/*ARRAY_SIZE(aiOuterColors)*/; i++)
			aiOuterColors[i] = ColorRGBAMultiplyAlpha(aiOuterColors[i], chAlpha);
	}
	if (aiInnerColors) {
		for (i = 0; i < 4/*ARRAY_SIZE(aiInnerColors)*/; i++)
			aiInnerColors[i] = ColorRGBAMultiplyAlpha(aiInnerColors[i], chAlpha);
	}

	// If no color is visible, don't draw anything.
	for (i = 0; i < 4 && !bVisible; i++)
		bVisible = (aiInnerColors && (aiInnerColors[i] & 0xFF)) || (aiOuterColors && (aiOuterColors[i] & 0xFF));
	if (!bVisible)
		return;

	if (!aiOuterColors) {
		fullInterpColors = aiInnerColors;
	}
	if (!aiInnerColors) {
		fullInterpColors = aiOuterColors;
	}

	pTopLeft = pBorder->pTopLeft;
	pTop = pBorder->pTop;
	pTopRight = pBorder->pTopRight;
	pLeft = pBorder->pLeft;
	pRight = pBorder->pRight;
	pBottomLeft = pBorder->pBottomLeft;
	pBottom = pBorder->pBottom;
	pBottomRight = pBorder->pBottomRight;
	pBackground = pBorder->pBackground;
	pPattern = pBorder->pPattern;

	// Draw things in order of increasing Z

	if (pBackground)
	{
		U32 colors[4];
		CBox box = *pBox;
		
		if (!pBorder->bDrawUnder)
		{
			ui_StyleBorderInnerCBoxEx(pBorder, &box, fScale, false);			
		}

		if( fullInterpColors ) {
			colors[0] = interpColorBox(fullInterpColors, pBox, box.lx, box.ly);
			colors[1] = interpColorBox(fullInterpColors, pBox, box.hx, box.ly);
			colors[2] = interpColorBox(fullInterpColors, pBox, box.hx, box.hy);
			colors[3] = interpColorBox(fullInterpColors, pBox, box.lx, box.hy);
		} else {
			colors[0] = aiInnerColors[0];
			colors[1] = aiInnerColors[1];
			colors[2] = aiInnerColors[2];
			colors[3] = aiInnerColors[3];
		}

		
		switch (pBorder->eBackgroundType)
		{
		case UITextureModeTiled:
			{
				display_sprite_tiled_box_4Color_scaled_rot(ATLAS_TEX_TO_BASIC_TEX(pBackground), &box, fZ,
														   colors[0], colors[1], colors[2], colors[3], fScale, fScale,
														   centerX, centerY, rot);
			}
		xdefault:
			display_sprite_box_4Color_rot(pBackground, &box, fZ,
										  colors[0], colors[1], colors[2], colors[3],
										  centerX, centerY, rot);
		}

		if (pPattern)
		{
			ui_StyleDisplayPatternLayer( pPattern, pBackground, box.lx, box.ly, box.hx, box.hy, fZ, fScale, pBox );
		}
	}

	fZ += fRaise;

	if (pTopLeft)
	{
		CBox box;
		U32 colors[4];
	
		CalcXYScaleForCorner(pTopLeft, pBottomLeft, pTopRight, fBorderHeight, fBorderWidth, fScale, &fXScale, &fYScale);
		BuildCBox( &box, pBox->lx, pBox->ly, pTopLeft->width * fXScale, pTopLeft->height * fYScale );

		if( fullInterpColors ) {
			F32 w = pTopLeft->width * fXScale;
			F32 h = pTopLeft->height * fYScale;
			colors[0] = interpColorBox(fullInterpColors, pBox, pBox->lx, pBox->ly);
			colors[1] = interpColorBox(fullInterpColors, pBox, pBox->lx + w, pBox->ly);
			colors[2] = interpColorBox(fullInterpColors, pBox, pBox->lx + w, pBox->ly + h);
			colors[3] = interpColorBox(fullInterpColors, pBox, pBox->lx, pBox->ly + h);
		} else {
			colors[0] = colors[1] = colors[2] = colors[3] = aiOuterColors[0];
		}

		display_sprite_box_4Color_rot(pTopLeft, &box, fZ,
									  colors[0], colors[1], colors[2], colors[3],
									  centerX, centerY, rot);

		if (pPattern) {
			ui_StyleDisplayPatternLayer( pPattern, pTopLeft, box.lx, box.ly, box.hx, box.hy, fZ, fScale, pBox );
		}
	}
	if (pTopRight)
	{
		CBox box;
		U32 colors[4];

		CalcXYScaleForCorner(pTopRight, pBottomRight, pTopLeft, fBorderHeight, fBorderWidth, fScale, &fXScale, &fYScale);
		BuildCBox( &box, pBox->hx - pTopRight->width * fXScale, pBox->ly,
				   pTopRight->width * fXScale, pTopRight->height * fYScale );

		if( fullInterpColors ) {
			F32 w = pTopRight->width * fXScale;
			F32 h = pTopRight->height * fYScale;
			colors[0] = interpColorBox(fullInterpColors, pBox, pBox->hx - w, pBox->ly);
			colors[1] = interpColorBox(fullInterpColors, pBox, pBox->hx, pBox->ly);
			colors[2] = interpColorBox(fullInterpColors, pBox, pBox->hx, pBox->ly + h);
			colors[3] = interpColorBox(fullInterpColors, pBox, pBox->hx - w, pBox->ly + h);
		} else {
			colors[0] = colors[1] = colors[2] = colors[3] = aiOuterColors[1];
		}
		
		display_sprite_box_4Color_rot(pTopRight, &box, fZ,
									  colors[0], colors[1], colors[2], colors[3],
									  centerX, centerY, rot);

		if (pPattern) {
			ui_StyleDisplayPatternLayer( pPattern, pTopRight, box.lx, box.ly, box.hx, box.hy, fZ, fScale, pBox );
		}
	}

	CalcXYScaleForCorner(pTopLeft, pBottomLeft, pTopRight, fBorderHeight, fBorderWidth, fScale, &fXScale, &fYScale);
	if (pTop)
	{
		CBox box = {pBox->lx + (pTopLeft ? pTopLeft->width * fXScale : 0), pBox->ly,
			pBox->hx - (pTopRight ? pTopRight->width * fXScale : 0), pBox->hy};

		if(box.hx > box.lx)
		{
			U32 colors[4];
			F32 fScale2;
			
			CalcXYScaleForCorner(pTop, pBottom, NULL, fBorderHeight, fBorderWidth, fScale, NULL, &fScale2);
			box.hy = pBox->ly + pTop->height * fScale2;
			if( fullInterpColors ) {
				colors[0] = interpColorBox(fullInterpColors, pBox, box.lx, box.ly);
				colors[1] = interpColorBox(fullInterpColors, pBox, box.hx, box.ly);
				colors[2] = interpColorBox(fullInterpColors, pBox, box.hx, box.hy);
				colors[3] = interpColorBox(fullInterpColors, pBox, box.lx, box.hy);
			} else {
				colors[0] = colors[3] = aiOuterColors[0];
				colors[1] = colors[2] = aiOuterColors[1];
			}

			if(box.hy > box.ly)
			{
				if (bBorderTiled)
					display_sprite_tiled_box_4Color_scaled_rot(ATLAS_TEX_TO_BASIC_TEX(pTop), &box, fZ,
															   colors[0], colors[1], colors[2], colors[3], fScale, fScale,
															   centerX, centerY, rot);
				else
					display_sprite_box_4Color_rot(pTop, &box, fZ,
												  colors[0], colors[1], colors[2], colors[3],
												  centerX, centerY, rot);
			}

			if (pPattern)
			{
				ui_StyleDisplayPatternLayer( pPattern, pTop, box.lx, box.ly, box.hx, box.hy, fZ, fScale, pBox );
			}
		}
	}

	if (pLeft)
	{
		F32 fTopOffset, fBottomOffset;
		CBox box = {pBox->lx, pBox->ly /* filled in below */, pBox->hx, pBox->hy}; 

		CalcXYScaleForCorner(pTopLeft, pBottomLeft, pTopRight, fBorderHeight, fBorderWidth, fScale, &fXScale, &fYScale);
		if (pTopLeft)
			fTopOffset = pTopLeft->height;
		else if (pTop)
			fTopOffset = pTop->height;
		else
			fTopOffset = 0;

		fTopOffset *= fYScale;

		CalcXYScaleForCorner(pBottomLeft, pTopLeft, pBottomRight, fBorderHeight, fBorderWidth, fScale, &fXScale, &fYScale);
		if (pBottomLeft)
			fBottomOffset = pBottomLeft->height;
		else if (pBottom)
			fBottomOffset = pBottom->height;
		else
			fBottomOffset = 0;

		fBottomOffset *= fYScale;

		box.ly += fTopOffset;
		box.hy -= fBottomOffset;
		if(box.hy > box.ly)
		{
			U32 colors[4];
			F32 fScale2;

			CalcXYScaleForCorner(pLeft, NULL, pRight, fBorderHeight, fBorderWidth, fScale, &fScale2, NULL);
			box.hx = pBox->lx + pLeft->width * fScale2;
			if( fullInterpColors ) {
				colors[0] = interpColorBox(fullInterpColors, pBox, box.lx, box.ly);
				colors[1] = interpColorBox(fullInterpColors, pBox, box.hx, box.ly);
				colors[2] = interpColorBox(fullInterpColors, pBox, box.hx, box.hy);
				colors[3] = interpColorBox(fullInterpColors, pBox, box.lx, box.hy);
			} else {
				colors[0] = colors[1] = aiOuterColors[0];
				colors[2] = colors[3] = aiOuterColors[3];
			}

			if(box.hx > box.lx)
			{
				if (bBorderTiled)
					display_sprite_tiled_box_4Color_scaled_rot(ATLAS_TEX_TO_BASIC_TEX(pLeft), &box, fZ+.01,
															   colors[0], colors[1], colors[2], colors[3], fScale, fScale,
															   centerX, centerY, rot);
				else
					display_sprite_box_4Color_rot(pLeft, &box, fZ+.01,
												  colors[0], colors[1], colors[2], colors[3],
												  centerX, centerY, rot);

				if (pPattern)
				{
					ui_StyleDisplayPatternLayer( pPattern, pLeft, box.lx, box.ly, box.hx, box.hy, fZ+.01, fScale, pBox );
				}
			}
		}
	}

	if (pRight)
	{
		F32 fTopOffset, fBottomOffset;
		CBox box = {pBox->lx, pBox->ly, pBox->hx, pBox->hy}; 

		CalcXYScaleForCorner(pTopRight, pBottomRight, pTopLeft, fBorderHeight, fBorderWidth, fScale, &fXScale, &fYScale);
		if (pTopRight)
			fTopOffset = pTopRight->height;
		else if (pTop)
			fTopOffset = pTop->height;
		else
			fTopOffset = 0;

		fTopOffset *= fYScale;

		CalcXYScaleForCorner(pBottomRight, pTopRight, pBottomLeft, fBorderHeight, fBorderWidth, fScale, &fXScale, &fYScale);

		if (pBottomRight)
			fBottomOffset = pBottomRight->height;
		else if (pBottom)
			fBottomOffset = pBottom->height;
		else
			fBottomOffset = 0;

		fBottomOffset *= fYScale;

		box.ly += fTopOffset;
		box.hy -= fBottomOffset;

		if(box.hy> box.ly)
		{
			U32 colors[4];
			F32 fScale2;

			CalcXYScaleForCorner(pRight, NULL, pLeft, fBorderHeight, fBorderWidth, fScale, &fScale2, NULL);
			box.lx = pBox->hx - pRight->width * fScale2;
			if( fullInterpColors ) {
				colors[0] = interpColorBox(fullInterpColors, pBox, box.lx, box.ly);
				colors[1] = interpColorBox(fullInterpColors, pBox, box.hx, box.ly);
				colors[2] = interpColorBox(fullInterpColors, pBox, box.hx, box.hy);
				colors[3] = interpColorBox(fullInterpColors, pBox, box.lx, box.hy);
			} else {
				colors[0] = colors[1] = aiOuterColors[1];
				colors[2] = colors[3] = aiOuterColors[2];
			}

			if(box.hx > box.lx)
			{
				if (bBorderTiled)
					display_sprite_tiled_box_4Color_scaled_rot(ATLAS_TEX_TO_BASIC_TEX(pRight), &box, fZ+.01,
															   colors[0], colors[1], colors[2], colors[3], fScale, fScale,
															   centerX, centerY, rot);
				else
					display_sprite_box_4Color_rot(pRight, &box, fZ+.01,
												  colors[0], colors[1], colors[2], colors[3],
												  centerX, centerY, rot);

				if (pPattern)
				{
					ui_StyleDisplayPatternLayer( pPattern, pRight, box.lx, box.ly, box.hx, box.hy, fZ+.01, fScale, pBox );
				}
			}
			
		}
	}

	CalcXYScaleForCorner(pBottomLeft, pTopLeft, pBottomRight, fBorderHeight, fBorderWidth, fScale, &fXScale, &fYScale);
	if (pBottom)
	{
		CBox box = {pBox->lx + (pBottomLeft ? pBottomLeft->width * fXScale : 0), pBox->ly,
			pBox->hx - (pBottomRight ? pBottomRight->width * fXScale : 0), pBox->hy};

		if(box.hx > box.lx)
		{
			U32 colors[4];
			F32 fScale2;

			CalcXYScaleForCorner(pBottom, pTop, NULL, fBorderHeight, fBorderWidth, fScale, NULL, &fScale2);
			box.ly = pBox->hy - pBottom->height * fScale2;
			if( fullInterpColors ) {
				colors[0] = interpColorBox(fullInterpColors, pBox, box.lx, box.ly);
				colors[1] = interpColorBox(fullInterpColors, pBox, box.hx, box.ly);
				colors[2] = interpColorBox(fullInterpColors, pBox, box.hx, box.hy);
				colors[3] = interpColorBox(fullInterpColors, pBox, box.lx, box.hy);
			} else {
				colors[0] = colors[3] = aiOuterColors[3];
				colors[1] = colors[2] = aiOuterColors[2];
			}
			
			if(box.hy > box.ly)
			{
				if (bBorderTiled)
					display_sprite_tiled_box_4Color_scaled_rot(ATLAS_TEX_TO_BASIC_TEX(pBottom), &box, fZ+.02,
															   colors[0], colors[1], colors[2], colors[3], fScale, fScale,
															   centerX, centerY, rot);
				else
					display_sprite_box_4Color_rot(pBottom, &box, fZ+.02,
												  colors[0], colors[1], colors[2], colors[3],
												  centerX, centerY, rot);

				if (pPattern)
				{
					ui_StyleDisplayPatternLayer( pPattern, pBottom, box.lx, box.ly, box.hx, box.hy, fZ+.02, fScale, pBox );
				}
			}
		}
	}

	if (pBottomLeft)
	{
		CBox box;
		U32 colors[4];

		CalcXYScaleForCorner(pBottomLeft, pTopLeft, pBottomRight, fBorderHeight, fBorderWidth, fScale, &fXScale, &fYScale);
		BuildCBox( &box, pBox->lx, pBox->hy - pBottomLeft->height * fYScale,
				   pBottomLeft->width * fXScale, pBottomLeft->height * fYScale );

		if( fullInterpColors ) {
			F32 w = pBottomLeft->width * fXScale;
			F32 h = pBottomLeft->height * fYScale;
			colors[0] = interpColorBox(fullInterpColors, pBox, pBox->lx, pBox->hy - h);
			colors[1] = interpColorBox(fullInterpColors, pBox, pBox->lx + w, pBox->hy - h);
			colors[2] = interpColorBox(fullInterpColors, pBox, pBox->lx + w, pBox->hy);
			colors[3] = interpColorBox(fullInterpColors, pBox, pBox->lx, pBox->hy);
		} else {
			colors[0] = colors[1] = colors[2] = colors[3] = aiOuterColors[3];
		}

		display_sprite_box_4Color_rot(pBottomLeft, &box, fZ+.02,
									  colors[0], colors[1], colors[2], colors[3],
									  centerX, centerY, rot);

		if (pPattern) {
			ui_StyleDisplayPatternLayer( pPattern, pBottomLeft, box.lx, box.ly, box.hx, box.hy, fZ+.02, fScale, pBox );
		}
	}
	if (pBottomRight)
	{
		CBox box;
		U32 colors[4];

		CalcXYScaleForCorner(pBottomRight, pTopRight, pBottomLeft, fBorderHeight, fBorderWidth, fScale, &fXScale, &fYScale);
		BuildCBox( &box, pBox->hx - pBottomRight->width * fXScale, pBox->hy - pBottomRight->height * fYScale,
				   pBottomRight->width * fXScale, pBottomRight->height * fYScale );

		if( fullInterpColors ) {
			F32 w = pBottomLeft->width * fXScale;
			F32 h = pBottomLeft->height * fYScale;
			colors[0] = interpColorBox(fullInterpColors, pBox, pBox->hx - w, pBox->hy - h);
			colors[1] = interpColorBox(fullInterpColors, pBox, pBox->hx, pBox->hy - h);
			colors[2] = interpColorBox(fullInterpColors, pBox, pBox->hx, pBox->hy);
			colors[3] = interpColorBox(fullInterpColors, pBox, pBox->hx - w, pBox->hy);
		} else {
			colors[0] = colors[1] = colors[2] = colors[3] = aiOuterColors[1];
		}

		display_sprite_box_4Color_rot(pBottomRight, &box, fZ+.02,
									  colors[0], colors[1], colors[2], colors[3],
									  centerX, centerY, rot);

		if (pPattern) {
			ui_StyleDisplayPatternLayer( pPattern, pBottomRight, box.lx, box.ly, box.hx, box.hy, fZ+.02, fScale, pBox );
		}
	}
}

void ui_StyleBorderDraw(UIStyleBorder *pBorder, const CBox *pBox, U32 outerColor, U32 innerColor, F32 fZ, F32 fScale, U8 chAlpha)
{
	U32 aOuters[4];
	U32 aInners[4];
	S32 i;
	for (i = 0; i < ARRAY_SIZE(aOuters); i++)
	{
		aOuters[i] = outerColor;
		aInners[i] = innerColor;
	}
	ui_StyleBorderDrawEx(pBorder, pBox, aOuters, aInners, 0, 0, 0, fZ, fScale, chAlpha);
}

void ui_StyleBorderDrawOutside(UIStyleBorder *pBorder, const CBox *pBox, U32 outerColor, U32 innerColor, F32 fZ, F32 fScale, U8 chAlpha)
{
	CBox box = *pBox;
	ui_StyleBorderOuterCBox(pBorder, &box, fScale);
	ui_StyleBorderDraw(pBorder, &box, outerColor, innerColor, fZ, fScale, chAlpha);
}

void ui_StyleBorderDrawMagic(UIStyleBorder *pBorder, const CBox *pBox, F32 fZ, F32 fScale, U8 chAlpha)
{
	if (!pBorder)
		pBorder = GET_REF(g_ui_Tex.hCapsule);
	if (!pBorder)
	{
		Errorf("Default capsule border not found, is your core data missing?");
		return;
	}
	else
	{
		U32 aiColors[4] = {
			ui_StyleColorPaletteIndex(pBorder->uiTopLeftColor),
			ui_StyleColorPaletteIndex(pBorder->uiTopRightColor),
			ui_StyleColorPaletteIndex(pBorder->uiBottomRightColor),
			ui_StyleColorPaletteIndex(pBorder->uiBottomLeftColor)
		};

		if (!pBorder->uiInnerColor && !pBorder->uiOuterColor) {
			ui_StyleBorderDrawEx(pBorder, pBox, aiColors, NULL, 0, 0, 0, fZ, fScale, chAlpha);
		} else {
			U32 uiInnerColor = ui_StyleColorPaletteIndex(pBorder->uiInnerColor);
			U32 uiOuterColor = ui_StyleColorPaletteIndex(pBorder->uiOuterColor);
			U32 auiInnerColors[4] = {uiInnerColor, uiInnerColor, uiInnerColor, uiInnerColor};
			U32 auiOuterColors[4] = {uiOuterColor, uiOuterColor, uiOuterColor, uiOuterColor};
		
			if (!auiOuterColors[0]) {
				memcpy(auiOuterColors, aiColors, sizeof( auiOuterColors ));
			}
			if (!auiInnerColors[0]) {
				memcpy(auiInnerColors, aiColors, sizeof( auiInnerColors));
			}
			
			ui_StyleBorderDrawEx(pBorder, pBox, auiOuterColors, auiInnerColors, 0, 0, 0, fZ, fScale, chAlpha);
		}
	}
}

void ui_StyleBorderDrawMagicRot(UIStyleBorder *pBorder, const CBox *pBox, F32 x, F32 y, F32 rot, F32 fZ, F32 fScale, U8 chAlpha)
{
	if (!pBorder)
		pBorder = GET_REF(g_ui_Tex.hCapsule);
	if (!pBorder)
	{
		Errorf("Default capsule border not found, is your core data missing?");
		return;
	}
	else
	{
		U32 aiColors[4] = {
			ui_StyleColorPaletteIndex(pBorder->uiTopLeftColor),
			ui_StyleColorPaletteIndex(pBorder->uiTopRightColor),
			ui_StyleColorPaletteIndex(pBorder->uiBottomRightColor),
			ui_StyleColorPaletteIndex(pBorder->uiBottomLeftColor)
		};

		if (!pBorder->uiInnerColor && !pBorder->uiOuterColor) {
			ui_StyleBorderDrawEx(pBorder, pBox, aiColors, NULL, x, y, rot, fZ, fScale, chAlpha);
		} else {
			U32 uiInnerColor = ui_StyleColorPaletteIndex(pBorder->uiInnerColor);
			U32 uiOuterColor = ui_StyleColorPaletteIndex(pBorder->uiOuterColor);
			U32 auiInnerColors[4] = {uiInnerColor, uiInnerColor, uiInnerColor, uiInnerColor};
			U32 auiOuterColors[4] = {uiOuterColor, uiOuterColor, uiOuterColor, uiOuterColor};
		
			if (!auiOuterColors[0]) {
				memcpy(auiOuterColors, aiColors, sizeof( auiOuterColors ));
			}
			if (!auiInnerColors[0]) {
				memcpy(auiInnerColors, aiColors, sizeof( auiInnerColors));
			}
			
			ui_StyleBorderDrawEx(pBorder, pBox, auiOuterColors, auiInnerColors, x, y, rot, fZ, fScale, chAlpha);
		}
	}
}

void ui_StyleBorderDrawMagicOutside(UIStyleBorder *pBorder, const CBox *pBox, F32 fZ, F32 fScale, U8 chAlpha)
{
	CBox box = *pBox;
	ui_StyleBorderOuterCBox(pBorder, &box, 1.f);
	ui_StyleBorderDrawMagic(pBorder, &box, fZ, fScale, chAlpha);
}

S32 ui_StyleBorderLeftSizeEx(UIStyleBorder *pBorder, bool bUsePadding)
{
	F32 fSize;

	if (!pBorder)
		pBorder = GET_REF(g_ui_Tex.hCapsule);

	if (!pBorder)
	{
		Errorf("Default capsule border not found, is your core data missing?");
		return 0;
	}

	fSize = pBorder->fPaddingLeft;
	if(!bUsePadding || fSize == BORDER_PADDING_NOT_SET)
	{
		fSize = UI_TEXTURE_WIDTH(pBorder->pTopLeft);
		MAX1(fSize, UI_TEXTURE_WIDTH(pBorder->pLeft));
		MAX1(fSize, UI_TEXTURE_WIDTH(pBorder->pBottomLeft));
	}
	return fSize;
}

S32 ui_StyleBorderRightSizeEx(UIStyleBorder *pBorder, bool bUsePadding)
{
	F32 fSize;

	if (!pBorder)
		pBorder = GET_REF(g_ui_Tex.hCapsule);

	if (!pBorder)
	{
		Errorf("Default capsule border not found, is your core data missing?");
		return 0;
	}

	fSize = pBorder->fPaddingRight;
	if(!bUsePadding || fSize == BORDER_PADDING_NOT_SET)
	{
		fSize = UI_TEXTURE_WIDTH(pBorder->pTopRight);
		MAX1(fSize, UI_TEXTURE_WIDTH(pBorder->pRight));
		MAX1(fSize, UI_TEXTURE_WIDTH(pBorder->pBottomRight));
	}

	return fSize;
}

S32 ui_StyleBorderTopSizeEx(UIStyleBorder *pBorder, bool bUsePadding)
{
	F32 fSize;

	if (!pBorder)
		pBorder = GET_REF(g_ui_Tex.hCapsule);

	if (!pBorder)
	{
		Errorf("Default capsule border not found, is your core data missing?");
		return 0;
	}

	fSize = pBorder->fPaddingTop;
	if(!bUsePadding || fSize == BORDER_PADDING_NOT_SET)
	{
		fSize = UI_TEXTURE_HEIGHT(pBorder->pTopLeft);
		MAX1(fSize, UI_TEXTURE_HEIGHT(pBorder->pTop));
		MAX1(fSize, UI_TEXTURE_HEIGHT(pBorder->pTopRight));
	}

	return fSize;
}

S32 ui_StyleBorderBottomSizeEx(UIStyleBorder *pBorder, bool bUsePadding)
{
	F32 fSize;

	if (!pBorder)
		pBorder = GET_REF(g_ui_Tex.hCapsule);
	if (!pBorder)
	{
		Errorf("Default capsule border not found, is your core data missing?");
		return 0;
	}

	fSize = pBorder->fPaddingBottom;
	if(!bUsePadding || fSize == BORDER_PADDING_NOT_SET)
	{
		fSize = UI_TEXTURE_HEIGHT(pBorder->pBottomLeft);
		MAX1(fSize, UI_TEXTURE_HEIGHT(pBorder->pBottom));
		MAX1(fSize, UI_TEXTURE_HEIGHT(pBorder->pBottomRight));
	}

	return fSize;
}

void ui_StyleBorderInnerCBoxEx(UIStyleBorder *pBorder, CBox *pBox, F32 fScale, bool bUsePadding)
{
	F32 fLeft = ui_StyleBorderLeftSizeEx(pBorder, bUsePadding);
	F32 fRight = ui_StyleBorderRightSizeEx(pBorder, bUsePadding);
	F32 fTop = ui_StyleBorderTopSizeEx(pBorder, bUsePadding);
	F32 fBottom = ui_StyleBorderBottomSizeEx(pBorder, bUsePadding);

	CBoxNormalize(pBox);

	pBox->lx += fLeft * fScale;
	pBox->hx -= fRight * fScale;
	pBox->ly += fTop * fScale;
	pBox->hy -= fBottom * fScale;

	if (pBox->hx < pBox->lx)
		pBox->hx = pBox->lx = (S32)(pBox->hx + pBox->lx) / 2;
	if (pBox->hy < pBox->ly)
		pBox->hy = pBox->ly = (S32)(pBox->hy + pBox->ly) / 2;
}

void ui_StyleBorderOuterCBox(UIStyleBorder *pBorder, CBox *pBox, F32 fScale)
{
	F32 fLeft = ui_StyleBorderLeftSize(pBorder);
	F32 fRight = ui_StyleBorderRightSize(pBorder);
	F32 fTop = ui_StyleBorderTopSize(pBorder);
	F32 fBottom = ui_StyleBorderBottomSize(pBorder);
	CBoxNormalize(pBox);

	pBox->lx -= fLeft * fScale;
	pBox->hx += fRight * fScale;
	pBox->ly -= fTop * fScale;
	pBox->hy += fBottom * fScale;
}

S32 ui_StyleBorderWidth(UIStyleBorder *pBorder)
{
	return ui_StyleBorderLeftSize(pBorder) + ui_StyleBorderRightSize(pBorder);
}

S32 ui_StyleBorderHeight(UIStyleBorder *pBorder)
{
	return ui_StyleBorderTopSize(pBorder) + ui_StyleBorderBottomSize(pBorder);
}

void ui_StyleBorderLoadTextures(UIStyleBorder *pBorder)
{
	UI_LOAD_TEXTURE_FOR(pBorder, Top);
	UI_LOAD_TEXTURE_FOR(pBorder, Bottom);
	UI_LOAD_TEXTURE_FOR(pBorder, Left);
	UI_LOAD_TEXTURE_FOR(pBorder, Right);
	UI_LOAD_TEXTURE_FOR(pBorder, TopLeft);
	UI_LOAD_TEXTURE_FOR(pBorder, TopRight);
	UI_LOAD_TEXTURE_FOR(pBorder, BottomLeft);
	UI_LOAD_TEXTURE_FOR(pBorder, BottomRight);
	UI_LOAD_TEXTURE_FOR(pBorder, Background);
	UI_LOAD_TEXTURE_FOR(pBorder, Pattern);
}

AUTO_FIXUPFUNC;
TextParserResult ui_StyleBorderParserFixup(UIStyleBorder *pBorder, enumTextParserFixupType eType, void *pExtraData)
{
	if (eType == FIXUPTYPE_POST_BIN_READ || eType == FIXUPTYPE_POST_RELOAD || eType == FIXUPTYPE_POST_TEXT_READ)
	{
		ui_StyleBorderLoadTextures(pBorder);
	}
	return PARSERESULT_SUCCESS;
}


UIStyleBar *ui_StyleBarGet(const char *pchName)
{
	return pchName ? RefSystem_ReferentFromString(g_ui_BarDict, pchName) : NULL;
}

void ui_StyleBarDrawTicks(const UIStyleBar *pBar, const CBox *pBox, const CBox *pFullBox, S32 iTickCount, F32 fZ, S32 iAlpha, F32 fBarScale)
{
	const F32 fLeft = pBox->lx;
	const F32 fRight = pBox->hx;
	const F32 fTop = pBox->ly;
	const F32 fBottom = pBox->hy;
	const F32 fHeight = fBottom - fTop;
	U32 uiColor, uiColorFilled;
	AtlasTex *pTick, *pTickFilled;

	uiColor = pBar->uiTickColor;
	uiColorFilled = pBar->uiTickFilledColor;
	pTick = pBar->pTick;
	pTickFilled = pBar->pTickFilled;

	if (!pTickFilled)
		pTickFilled = pTick;
	if (uiColor && !uiColorFilled)
		uiColorFilled = uiColor;

	if (iAlpha & 0xFF)
	{
		uiColor = ColorRGBAMultiplyAlpha(ui_StyleColorPaletteIndex(uiColor), iAlpha);
		uiColorFilled = ColorRGBAMultiplyAlpha(ui_StyleColorPaletteIndex(uiColorFilled), iAlpha);
	}
	else
		uiColor = uiColorFilled = 0;

	// Do not draw if texture is completely alpha'd out
	if (!(uiColor & 0xFF))
		pTick = NULL;
	if (!(uiColorFilled & 0xFF))
		pTickFilled = NULL;

	// Draw ticks
	if (pTick || pTickFilled)
	{
		F32 fOffsetX = 0, fOffsetY = 0;
		F32 fScaleX, fScaleY, fX, fY;
		F32 fFillScaleX, fFillScaleY, fFillX, fFillY;
		F32 fReferenceX, fReferenceY;
		S32 i;
		bool bRoundX = false, bRoundY = false;

		if (pBar->eFillFrom == UINoDirection || pBar->eFillFrom == UIAnyDirection
			|| pBar->eFillFrom == UITopLeft || pBar->eFillFrom == UIBottomLeft
			|| pBar->eFillFrom == UITopRight || pBar->eFillFrom == UIBottomRight)
		{
			// Cases: Corner/Center/CenterOut
			bool bL = (pBar->eFillFrom & UILeft) && !(pBar->eFillFrom & UIRight);
			bool bR = !(pBar->eFillFrom & UILeft) && (pBar->eFillFrom & UIRight);
			bool bT = (pBar->eFillFrom & UITop) && !(pBar->eFillFrom & UIBottom);
			bool bB = !(pBar->eFillFrom & UITop) && (pBar->eFillFrom & UIBottom);

			fOffsetX = (bL ? 1 : bR ? -1 : 0) * CBoxWidth(pBox) / (iTickCount + 1);
			fOffsetY = (bT ? 1 : bB ? -1 : -.5f) * CBoxHeight(pBox) / (iTickCount + 1);

			fReferenceX = bL ? pBox->lx : bR ? pBox->hx : (pBox->lx + pBox->hx) / 2;
			fReferenceY = bT ? pBox->ly : bB ? pBox->hy : (pBox->ly + pBox->hy) / 2;

			fScaleY = pTick && (pBar->bScaleTick || pBar->bStretchTick) ? CBoxHeight(pBox) / pTick->height : 1;
			fScaleX = pBar->bScaleTick ? fScaleY : 1;

			fFillScaleY = pTickFilled && (pBar->bScaleTick || pBar->bStretchTick) ? CBoxHeight(pBox) / pTickFilled->height : 1;
			fFillScaleX = pBar->bScaleTick ? fFillScaleY : 1;

			bRoundX = pBar->bTickSnapToPixel;
			bRoundY = pBar->bTickSnapToPixel;
		}
		else if ((pBar->eFillFrom & UIHorizontal) && ((pBar->eFillFrom & UIVertical) == 0 || (pBar->eFillFrom & UIVertical) == UIVertical))
		{
			fOffsetX = CBoxWidth(pBox) / (iTickCount + 1);

			fReferenceX = pBox->lx;
			fReferenceY = (pBox->ly + pBox->hy) / 2;

			fScaleY = pTick && (pBar->bScaleTick || pBar->bStretchTick) ? CBoxHeight(pBox) / pTick->height : 1;
			fScaleX = pBar->bScaleTick ? fScaleY : 1;

			fFillScaleY = pTickFilled && (pBar->bScaleTick || pBar->bStretchTick) ? CBoxHeight(pBox) / pTickFilled->height : 1;
			fFillScaleX = pBar->bScaleTick ? fFillScaleY : 1;

			bRoundX = pBar->bTickSnapToPixel;
		}
		else
		{
			fOffsetY = CBoxHeight(pBox) / (iTickCount + 1);

			fReferenceX = (pBox->lx + pBox->hx) / 2;
			fReferenceY = pBox->ly;

			fScaleX = pTick && (pBar->bScaleTick || pBar->bStretchTick) ? CBoxWidth(pBox) / pTick->width : 1;
			fScaleY = pBar->bScaleTick ? fScaleX : 1;

			fFillScaleX = pTickFilled && (pBar->bScaleTick || pBar->bStretchTick) ? CBoxWidth(pBox) / pTickFilled->width : 1;
			fFillScaleY = pBar->bScaleTick ? fFillScaleX : 1;

			bRoundY = pBar->bTickSnapToPixel;
		}

		fX = fReferenceX - (SAFE_MEMBER(pTick, width) * fScaleX) / 2;
		fY = fReferenceY - (SAFE_MEMBER(pTick, height) * fScaleY) / 2;

		fFillX = fReferenceX - (SAFE_MEMBER(pTickFilled, width) * fFillScaleX) / 2;
		fFillY = fReferenceY - (SAFE_MEMBER(pTickFilled, height) * fFillScaleY) / 2;

		for (i = 0; i < iTickCount; i++)
		{
			fX += fOffsetX;
			fY += fOffsetY;

			fFillX += fOffsetX;
			fFillY += fOffsetY;

			fReferenceX += fOffsetX;
			fReferenceY += fOffsetY;

			if (pFullBox && pTickFilled && point_cbox_clsn(fReferenceX, fReferenceY, pFullBox))
			{
				display_sprite(pTickFilled,
					bRoundX ? round(fFillX) : fFillX,
					bRoundY ? round(fFillY) : fFillY,
					fZ,
					fFillScaleX,
					fFillScaleY,
					uiColorFilled
				);
			}
			else if (pTick)
			{
				display_sprite(pTick,
					bRoundX ? round(fX) : fX,
					bRoundY ? round(fY) : fY,
					fZ,
					fScaleX,
					fScaleY,
					uiColor
				);
			}
		}
	}
}

// Draw text over bar
void ui_StyleBarDrawText(const UIStyleBar *pBar, const CBox *pBox, F32 fPercentFull, F32 fNotch, const char *pchText, F32 fZ, S32 iAlpha, bool bClipToBox, F32 *pfLeftPadding, F32 fBarScale)
{
	const F32 fHeight = pBox->hy - pBox->ly;

	if (pchText && *pchText)
	{
		UIStyleFont *pFont = GET_REF(pBar->hFont);
		F32 fLineHeight = ui_StyleFontLineHeight(pFont, 1.f);
		F32 fScale = min(fBarScale, fHeight / fLineHeight);
		ui_StyleFontUse(pFont, false, kWidgetModifier_None);
		gfxfont_SetAlpha(iAlpha);
		gfxfont_Printf((*pfLeftPadding), (pBox->ly + pBox->hy) / 2, fZ, fScale, fScale, CENTER_Y, "%s", pchText);
	}
}

void ui_StyleBarLayoutVerticalFilledBox(CBox *pFillBox, const CBox *pPaddedBox, UIDirection eFillFrom, F32 fPercentFull)
{
	if ((eFillFrom & UIVertical) == UIVertical)
	{
		CBoxSetY(pFillBox, (pFillBox->ly + pFillBox->hy - CBoxHeight(pPaddedBox) * fPercentFull)/2, CBoxHeight(pFillBox) * fPercentFull);
	}
	else if (eFillFrom & UITop)
	{			
		CBoxSetHeight(pFillBox, CBoxHeight(pPaddedBox) * fPercentFull);
	}
	else if (eFillFrom & UIBottom)
	{
		pFillBox->ly = pFillBox->hy - (CBoxHeight(pPaddedBox) * fPercentFull);
	}
	else
	{
		CBoxSetHeight(pFillBox, CBoxHeight(pPaddedBox));
	}
}

void ui_StyleBarLayoutHorizontalFilledBox(CBox *pFillBox, const CBox *pPaddedBox, UIDirection eFillFrom, F32 fPercentFull)
{
	if ((eFillFrom & UIHorizontal) == UIHorizontal)
	{
		CBoxSetX(pFillBox, (pFillBox->lx + pFillBox->hx - CBoxWidth(pPaddedBox) * fPercentFull)/2, CBoxWidth(pFillBox) * fPercentFull);
	}
	else if (eFillFrom & UIRight)
	{
		pFillBox->lx = pFillBox->hx - (CBoxWidth(pPaddedBox) * fPercentFull);
	}
	else if (eFillFrom & UILeft)
	{
		CBoxSetWidth(pFillBox, CBoxWidth(pPaddedBox) * fPercentFull);
	}
	else
	{
		CBoxSetWidth(pFillBox, CBoxWidth(pPaddedBox));
	}
}

void ui_StyleBarDraw(const UIStyleBar *pBar, const CBox *pBox, F32 fPercentFull, F32 fNotch, S32 iTickCount, F32 fMovingOverlayAlpha, const char *pchText, F32 fZ, S32 iAlpha, bool bClipToBox, F32 fScale, SpriteProperties *pSpriteProps)
{
	if (pBar)
	{
		CBox PaddedBox = {0};
		CBox FillBox = {0};
		F32 fMaxZ = UI_GET_Z();
		const F32 fLeft = pBox->lx;
		const F32 fRight = pBox->hx;
		const F32 fTop = pBox->ly;
		const F32 fBottom = pBox->hy;
		const F32 fHeight = fBottom - fTop;

		F32 fLeftFill = fLeft;
		F32 fRightFill = fRight;

		F32 fLeftPadding;
		F32 *pfLeftPadding = &fLeftPadding;

		const F32 fEmptyZ = fZ;
		const F32 fFilledZ = fEmptyZ + 0.1;
		const F32 fDynamicOverlayZ = fFilledZ + 0.1;
		const F32 fStaticOverlayZ = fDynamicOverlayZ + 0.1;
		const F32 fTickZ = fStaticOverlayZ + 0.1;
		const F32 fTextZ = fTickZ + 0.1;
		const F32 fNotchZ = fTextZ + 0.1;
		F32 fScreenDist = pSpriteProps ? pSpriteProps->screen_distance : 0.0;
		bool bIs3D = pSpriteProps ? pSpriteProps->is_3D : false;

		if (iTickCount == -1)
			iTickCount = pBar->iTickCount;

		// Make sure we didn't add so many Zs that we passed our allocated range.
		assert(fNotchZ < fZ + 1);

		PaddedBox = *pBox;
		if (GET_REF(pBar->hEmpty))
		{
			ui_TextureAssemblyDrawEx(GET_REF(pBar->hEmpty), pBox, &PaddedBox, fScale, fEmptyZ, fFilledZ, iAlpha, NULL, fScreenDist, bIs3D);
		}
		FillBox = PaddedBox;
		ui_StyleBarLayoutHorizontalFilledBox(&FillBox, &PaddedBox, pBar->eFillFrom, fPercentFull);
		ui_StyleBarLayoutVerticalFilledBox(&FillBox, &PaddedBox, pBar->eFillFrom, fPercentFull);
		if (GET_REF(pBar->hFilled))
		{
			if (pBar->bClipFilledArea)
				clipperPushRestrict(&FillBox);
			ui_TextureAssemblyDrawEx(GET_REF(pBar->hFilled), pBar->bClipFilledArea ? &PaddedBox : &FillBox, NULL, fScale, fFilledZ, fDynamicOverlayZ, iAlpha, NULL, fScreenDist, bIs3D);
			if (pBar->bClipFilledArea)
				clipperPop();
		}
		if (GET_REF(pBar->hDynamicOverlay))
		{
			ui_TextureAssemblyDrawEx(GET_REF(pBar->hDynamicOverlay), &FillBox, NULL, fScale, fDynamicOverlayZ, fStaticOverlayZ, iAlpha * fMovingOverlayAlpha, NULL, fScreenDist, bIs3D);
		}
		if (GET_REF(pBar->hStaticOverlay))
		{
			ui_TextureAssemblyDrawEx(GET_REF(pBar->hStaticOverlay), &PaddedBox, NULL, fScale, fStaticOverlayZ, fTickZ, iAlpha, NULL, fScreenDist, bIs3D);
		}
		ui_StyleBarDrawTicks(pBar, &PaddedBox, &FillBox, iTickCount, fTickZ, iAlpha, fScale);
		ui_StyleBarDrawText(pBar, &PaddedBox, fPercentFull, fNotch, pchText, fTextZ, iAlpha, bClipToBox, &PaddedBox.lx, fScale);

		if (GET_REF(pBar->hNotch) && pBar->eFillFrom)
		{
			UITextureAssembly *pNotch = GET_REF(pBar->hNotch);
			const F32 fNotchWidth = ui_TextureAssemblyWidth(pNotch) * fScale;
			const F32 fNotchHeight = ui_TextureAssemblyHeight(pNotch) * fScale;
			CBox DrawBox = {0};
			if (pBar->bNotchForceInside)
			{
				PaddedBox.lx += fNotchWidth/2;
				PaddedBox.hx -= fNotchWidth/2;
				PaddedBox.ly += fNotchHeight/2;
				PaddedBox.hy -= fNotchHeight/2;
			}

			if ((pBar->eFillFrom & UIHorizontal) == UIHorizontal || (pBar->eFillFrom & UIHorizontal) == UINoDirection)
				CBoxSetX(&DrawBox, PaddedBox.lx + (CBoxWidth(&PaddedBox) * .5) - (fNotchWidth/2), fNotchWidth);
			else if (pBar->eFillFrom & UILeft)
				CBoxSetX(&DrawBox, PaddedBox.lx + (CBoxWidth(&PaddedBox) * fNotch) - (fNotchWidth/2), fNotchWidth);
			else if (pBar->eFillFrom & UIRight)
				CBoxSetX(&DrawBox, PaddedBox.hx - (CBoxWidth(&PaddedBox) * fNotch) - (fNotchWidth/2), fNotchWidth);


			if ((pBar->eFillFrom & UIVertical) == UIVertical || (pBar->eFillFrom & UIVertical) == UINoDirection)
				CBoxSetY(&DrawBox, PaddedBox.ly + (CBoxHeight(&PaddedBox) * .5) - (fNotchHeight/2), fNotchHeight);
			else if (pBar->eFillFrom & UITop)
				CBoxSetY(&DrawBox, PaddedBox.ly + (CBoxHeight(&PaddedBox) * fNotch) - (fNotchHeight/2), fNotchHeight);
			else if (pBar->eFillFrom & UIBottom)
				CBoxSetY(&DrawBox, PaddedBox.hy - (CBoxHeight(&PaddedBox) * fNotch) - (fNotchHeight/2), fNotchHeight);

			CBoxFloor(&DrawBox);

			ui_TextureAssemblyDrawEx(pNotch, &DrawBox, NULL, fScale, fNotchZ, fNotchZ+.1, (char)iAlpha, NULL, fScreenDist, bIs3D);
		}
	}
}

S32 ui_StyleBarGetHeight(UIStyleBar *pBar)
{
	if (pBar)
		return ui_TextureAssemblyHeight(GET_REF(pBar->hEmpty)) + ui_TextureAssemblyHeight(GET_REF(pBar->hFilled));
	else
		return 0;
}

S32 ui_StyleBarGetLeftPad(UIStyleBar *pBar)
{
	if (pBar)
		return ui_TextureAssemblyLeftSize(GET_REF(pBar->hEmpty));
	else
		return 0;
}

S32 ui_StyleBarGetRightPad(UIStyleBar *pBar)
{
	if (pBar)
		return ui_TextureAssemblyRightSize(GET_REF(pBar->hEmpty));
	else
		return 0;
}

UIStyleFont *ui_StyleBarGetFont(UIStyleBar *pBar)
{
	return pBar ? GET_REF(pBar->hFont) : NULL;
}

#define STYLEBAR_WARN_IF(cond, field) if (cond) ErrorFilenamef(pBar->pchFilename, "%s: Field \"%s\" is deprecated. Please update bar definition to use Texture Assemblies.", pBar->pchName, field)

AUTO_FIXUPFUNC;
TextParserResult ui_StyleBarParserFixup(UIStyleBar *pBar, enumTextParserFixupType eType, void *pExtraData)
{
	if (eType == FIXUPTYPE_POST_BIN_READ || eType == FIXUPTYPE_POST_RELOAD || eType == FIXUPTYPE_POST_TEXT_READ)
	{
		UI_LOAD_TEXTURE_FOR(pBar, Tick);
		UI_LOAD_TEXTURE_FOR(pBar, TickFilled);
		if (!ResourceHasMatchingFilename(pBar->pchName, pBar->pchFilename, ".bar"))
		{
			ErrorFilenamef(pBar->pchFilename, "%s: Bars must appear in a filename matching their logical name.", pBar->pchName);
		}
	}
	return PARSERESULT_SUCCESS;
}
#undef STYLEBAR_WARN_IF

void ui_LoadStyles(void)
{
	ui_StyleLoadColorPalettes();

	g_ui_FontDict = RefSystem_RegisterSelfDefiningDictionary("UIStyleFont", false, parse_UIStyleFont, true, true, NULL);
	resDictRegisterEventCallback(g_ui_FontDict, ui_StyleFontResEvent, NULL);
	resLoadResourcesFromDisk(g_ui_FontDict, "ui/fonts", ".font", "UIFonts.bin", PARSER_CLIENTSIDE | RESOURCELOAD_USEOVERLAYS);

	g_ui_BorderDict = RefSystem_RegisterSelfDefiningDictionary("UIStyleBorder", false, parse_UIStyleBorder, true, true, NULL);
	resLoadResourcesFromDisk(g_ui_BorderDict, "ui/borders", ".border", "UIBorders.bin", PARSER_CLIENTSIDE | RESOURCELOAD_USEOVERLAYS);

	g_ui_BarDict = RefSystem_RegisterSelfDefiningDictionary("UIStyleBar", false, parse_UIStyleBar, true, true, NULL);
	resLoadResourcesFromDisk(g_ui_BarDict, "ui/bars", ".bar", "UIBars.bin", PARSER_CLIENTSIDE | RESOURCELOAD_USEOVERLAYS);
}

//////////////////////////////////////////////////////////////////////////
// Bar Debugging tool / Browser

typedef struct StyleBarPane
{
	UIWidget widget;
	REF_TO(UIStyleBar) hDef;
	UILabel *pScaleLabel;
	UILabel *pAlphaLabel;
	UILabel *pValueLabel;
	UILabel *pNotchLabel;
	UISlider *pScale;
	UISlider *pAlpha;
	UISlider *pValue;
	UISlider *pNotch;
	UISlider *pHeight;
	UISlider *pWidth;
	UITextEntry *pScaleTextEntry;
	UITextEntry *pAlphaTextEntry;
	UITextEntry *pValueTextEntry;
	UITextEntry *pNotchTextEntry;
	UITextEntry *pHeightTextEntry;
	UITextEntry *pWidthTextEntry;
} StyleBarPane;

static void StyleBarPaneFree(StyleBarPane *pPane)
{
	REMOVE_HANDLE(pPane->hDef);
	ui_WidgetFreeInternal(UI_WIDGET(pPane));
}

static void StyleBarPaneDraw(StyleBarPane *pPane, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(pPane);
	UIStyleBar *pBar = GET_REF(pPane->hDef);
	UI_DRAW_EARLY(pPane);
	if (pBar)
	{
		F32 fMinZ = UI_GET_Z();
		int iAlpha = (int)(0xff*pPane->pAlpha->currentVals[0]);
		float fValue = pPane->pValue->currentVals[0];
		float fNotch = pPane->pNotch->currentVals[0];
		float fScale = pPane->pScale->currentVals[0];
		CBox BarBox = 
		{
			x + (w - pPane->pWidth->currentVals[0]) / 2, 
			y + (h - pPane->pHeight->currentVals[0]) / 2, 
			x + (w + pPane->pWidth->currentVals[0]) / 2, 
			y + (h + pPane->pHeight->currentVals[0]) / 2
		};
		ui_StyleBarDraw(pBar, &BarBox, fValue, fNotch, -1, 0, "", fMinZ, iAlpha, false, fScale, NULL);
	}
	UI_DRAW_LATE(pPane);
}

static void StyleBarPaneTick(StyleBarPane *pPane, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(pPane);
	ui_SliderTick(pPane->pScale, UI_MY_VALUES);
	ui_SliderTick(pPane->pAlpha, UI_MY_VALUES);
	ui_SliderTick(pPane->pValue, UI_MY_VALUES);
	ui_SliderTick(pPane->pNotch, UI_MY_VALUES);
	ui_SliderTick(pPane->pHeight, UI_MY_VALUES);
	ui_SliderTick(pPane->pWidth, UI_MY_VALUES);
	ui_TextEntryTick(pPane->pScaleTextEntry, UI_MY_VALUES);
	ui_TextEntryTick(pPane->pAlphaTextEntry, UI_MY_VALUES);
	ui_TextEntryTick(pPane->pValueTextEntry, UI_MY_VALUES);
	ui_TextEntryTick(pPane->pNotchTextEntry, UI_MY_VALUES);
	ui_TextEntryTick(pPane->pHeightTextEntry, UI_MY_VALUES);
	ui_TextEntryTick(pPane->pWidthTextEntry, UI_MY_VALUES);
}

void StyleBarPaneSetSliderFloat(UITextEntry *pTextEntry, UserData userdata)
{
	UISlider *pSlider = (UISlider*)userdata;
	if (pSlider)
	{
		float f = atof(ui_TextEntryGetText(pTextEntry));
		ui_SliderSetValue(pSlider, f);
	}
}

void StyleBarPaneSetTextEntryFloat(UISlider *pSlider, bool bFinished, UserData userdata)
{
	UITextEntry *pTextEntry = (UITextEntry*)userdata;
	if (pTextEntry)
	{
		char* text = NULL;
		estrPrintf(&text, "%.2f", pSlider->currentVals[0]);
		ui_TextEntrySetText(pTextEntry, text);
	}
}

void StyleBarPaneSetSliderInt(UITextEntry *pTextEntry, UserData userdata)
{
	UISlider *pSlider = (UISlider*)userdata;
	if (pSlider)
	{
		int i = atoi(ui_TextEntryGetText(pTextEntry));
		ui_SliderSetValue(pSlider, i);
	}
}

void StyleBarPaneSetTextEntryInt(UISlider *pSlider, bool bFinished, UserData userdata)
{
	UITextEntry *pTextEntry = (UITextEntry*)userdata;
	if (pTextEntry)
	{
		char* text = NULL;
		estrPrintf(&text, "%i", (int)pSlider->currentVals[0]);
		ui_TextEntrySetText(pTextEntry, text);
	}
}

static StyleBarPane *StyleBarPaneCreate(void)
{
	StyleBarPane *pPane = calloc(1, sizeof(StyleBarPane));
	Vec4 v4White = {1, 1, 1, 1};
	ui_WidgetInitialize(UI_WIDGET(pPane), StyleBarPaneTick, StyleBarPaneDraw, StyleBarPaneFree, NULL, NULL);
	pPane->pScaleLabel = ui_LabelCreate("Scale", 0, 0);
	pPane->pAlphaLabel = ui_LabelCreate("Alpha", 0, 20);
	pPane->pValueLabel = ui_LabelCreate("Value", 0, 40);
	pPane->pNotchLabel = ui_LabelCreate("Notch", 0, 60);
	pPane->pScale = ui_SliderCreate(35, 0, .85, 0, 4, 1);
	pPane->pAlpha = ui_SliderCreate(35, 20, .85, 0, 1, 1);
	pPane->pValue = ui_SliderCreate(35, 40, .85, 0, 1, .5);
	pPane->pNotch = ui_SliderCreate(35, 60, .85, 0, 1, .5);
	pPane->pWidth = ui_SliderCreate(0, 0, .35, 1, 200, 50);
	pPane->pHeight = ui_SliderCreate(0, 0, .35, 1, 200, 20);
	pPane->pScaleTextEntry = ui_TextEntryCreate("1", 0, 0);
	pPane->pAlphaTextEntry = ui_TextEntryCreate("1", 0, 20);
	pPane->pValueTextEntry = ui_TextEntryCreate(".5", 0, 40);
	pPane->pNotchTextEntry = ui_TextEntryCreate(".5", 0, 60);
	pPane->pWidthTextEntry = ui_TextEntryCreate("50", 0, 0);
	pPane->pHeightTextEntry = ui_TextEntryCreate("20", 0, 0);
	ui_TextEntrySetFloatOnly(pPane->pScaleTextEntry);
	ui_TextEntrySetFloatOnly(pPane->pAlphaTextEntry);
	ui_TextEntrySetFloatOnly(pPane->pValueTextEntry);
	ui_TextEntrySetFloatOnly(pPane->pNotchTextEntry);
	ui_TextEntrySetIntegerOnly(pPane->pWidthTextEntry);
	ui_TextEntrySetIntegerOnly(pPane->pHeightTextEntry);
	ui_TextEntrySetEnterCallback(pPane->pScaleTextEntry, StyleBarPaneSetSliderFloat, pPane->pScale);
	ui_TextEntrySetEnterCallback(pPane->pAlphaTextEntry, StyleBarPaneSetSliderFloat, pPane->pAlpha);
	ui_TextEntrySetEnterCallback(pPane->pValueTextEntry, StyleBarPaneSetSliderFloat, pPane->pValue);
	ui_TextEntrySetEnterCallback(pPane->pNotchTextEntry, StyleBarPaneSetSliderFloat, pPane->pNotch);
	ui_TextEntrySetEnterCallback(pPane->pWidthTextEntry, StyleBarPaneSetSliderInt, pPane->pWidth);
	ui_TextEntrySetEnterCallback(pPane->pHeightTextEntry, StyleBarPaneSetSliderInt, pPane->pHeight);
	ui_SliderSetChangedCallback(pPane->pScale, StyleBarPaneSetTextEntryFloat, pPane->pScaleTextEntry);
	ui_SliderSetChangedCallback(pPane->pAlpha, StyleBarPaneSetTextEntryFloat, pPane->pAlphaTextEntry);
	ui_SliderSetChangedCallback(pPane->pValue, StyleBarPaneSetTextEntryFloat, pPane->pValueTextEntry);
	ui_SliderSetChangedCallback(pPane->pNotch, StyleBarPaneSetTextEntryFloat, pPane->pNotchTextEntry);
	ui_SliderSetChangedCallback(pPane->pWidth, StyleBarPaneSetTextEntryInt, pPane->pWidthTextEntry);
	ui_SliderSetChangedCallback(pPane->pHeight, StyleBarPaneSetTextEntryInt, pPane->pHeightTextEntry);
	UI_WIDGET(pPane->pScale)->widthUnit = UIUnitPercentage;
	UI_WIDGET(pPane->pAlpha)->widthUnit = UIUnitPercentage;
	UI_WIDGET(pPane->pValue)->widthUnit = UIUnitPercentage;
	UI_WIDGET(pPane->pNotch)->widthUnit = UIUnitPercentage;
	UI_WIDGET(pPane->pWidth)->widthUnit = UIUnitPercentage;
	UI_WIDGET(pPane->pHeight)->widthUnit = UIUnitPercentage;
	UI_WIDGET(pPane->pHeight)->widthUnit = UIUnitPercentage;
	UI_WIDGET(pPane->pScaleTextEntry)->widthUnit = UIUnitPercentage;
	UI_WIDGET(pPane->pAlphaTextEntry)->widthUnit = UIUnitPercentage;
	UI_WIDGET(pPane->pValueTextEntry)->widthUnit = UIUnitPercentage;
	UI_WIDGET(pPane->pNotchTextEntry)->widthUnit = UIUnitPercentage;
	UI_WIDGET(pPane->pWidthTextEntry)->widthUnit = UIUnitPercentage;
	UI_WIDGET(pPane->pHeightTextEntry)->widthUnit = UIUnitPercentage;

	UI_WIDGET(pPane->pScaleTextEntry)->offsetFrom = UITopRight;
	UI_WIDGET(pPane->pAlphaTextEntry)->offsetFrom = UITopRight;
	UI_WIDGET(pPane->pNotchTextEntry)->offsetFrom = UITopRight;
	UI_WIDGET(pPane->pValueTextEntry)->offsetFrom = UITopRight;
	UI_WIDGET(pPane->pWidth)->offsetFrom = UIBottomLeft;
	UI_WIDGET(pPane->pHeight)->offsetFrom = UIBottomLeft;
	UI_WIDGET(pPane->pWidthTextEntry)->offsetFrom = UIBottomLeft;
	UI_WIDGET(pPane->pHeightTextEntry)->offsetFrom = UIBottomLeft;

	UI_WIDGET(pPane->pWidth)->xPOffset= 0.0;
	UI_WIDGET(pPane->pHeight)->xPOffset= 0.5;
	UI_WIDGET(pPane->pWidthTextEntry)->xPOffset= 0.35;
	UI_WIDGET(pPane->pHeightTextEntry)->xPOffset= 0.85;

	UI_WIDGET(pPane->pWidth)->width = 0.35;
	UI_WIDGET(pPane->pHeight)->width = 0.35;
	UI_WIDGET(pPane->pScaleTextEntry)->width = 0.15;
	UI_WIDGET(pPane->pAlphaTextEntry)->width = 0.15;
	UI_WIDGET(pPane->pNotchTextEntry)->width = 0.15;
	UI_WIDGET(pPane->pValueTextEntry)->width = 0.15;
	UI_WIDGET(pPane->pWidthTextEntry)->width = 0.15;
	UI_WIDGET(pPane->pHeightTextEntry)->width = 0.15;
	UI_WIDGET(pPane->pScaleTextEntry)->height = 20;
	UI_WIDGET(pPane->pAlphaTextEntry)->height = 20;
	UI_WIDGET(pPane->pNotchTextEntry)->height = 20;
	UI_WIDGET(pPane->pValueTextEntry)->height = 20;
	ui_WidgetAddChild(UI_WIDGET(pPane), UI_WIDGET(pPane->pScaleLabel));
	ui_WidgetAddChild(UI_WIDGET(pPane), UI_WIDGET(pPane->pAlphaLabel));
	ui_WidgetAddChild(UI_WIDGET(pPane), UI_WIDGET(pPane->pValueLabel));
	ui_WidgetAddChild(UI_WIDGET(pPane), UI_WIDGET(pPane->pNotchLabel));
	ui_WidgetAddChild(UI_WIDGET(pPane), UI_WIDGET(pPane->pScale));
	ui_WidgetAddChild(UI_WIDGET(pPane), UI_WIDGET(pPane->pAlpha));
	ui_WidgetAddChild(UI_WIDGET(pPane), UI_WIDGET(pPane->pValue));
	ui_WidgetAddChild(UI_WIDGET(pPane), UI_WIDGET(pPane->pNotch));
	ui_WidgetAddChild(UI_WIDGET(pPane), UI_WIDGET(pPane->pWidth));
	ui_WidgetAddChild(UI_WIDGET(pPane), UI_WIDGET(pPane->pHeight));
	ui_WidgetAddChild(UI_WIDGET(pPane), UI_WIDGET(pPane->pScaleTextEntry));
	ui_WidgetAddChild(UI_WIDGET(pPane), UI_WIDGET(pPane->pAlphaTextEntry));
	ui_WidgetAddChild(UI_WIDGET(pPane), UI_WIDGET(pPane->pValueTextEntry));
	ui_WidgetAddChild(UI_WIDGET(pPane), UI_WIDGET(pPane->pNotchTextEntry));
	ui_WidgetAddChild(UI_WIDGET(pPane), UI_WIDGET(pPane->pWidthTextEntry));
	ui_WidgetAddChild(UI_WIDGET(pPane), UI_WIDGET(pPane->pHeightTextEntry));

	return pPane;
}

static void StyleBarPaneSetRef(UIList *pList, StyleBarPane *pPane)
{
	UIStyleBar *pDef = ui_ListGetSelectedObject(pList);
	if (pDef)
		SET_HANDLE_FROM_STRING(g_ui_BarDict, pDef->pchName, pPane->hDef);
}

static void StyleBarOpenBar(UIList *pList, StyleBarPane *pPane)
{
	UIStyleBar *pDef = ui_ListGetSelectedObject(pList);
	if (pDef)
	{
		char achResolved[CRYPTIC_MAX_PATH];
		fileLocateWrite(pDef->pchFilename, achResolved);
		fileOpenWithEditor(achResolved);
	}
}

// View all currently loaded texture assembly definitions.
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Debug) ACMD_NAME(BarBrowser, UIStyleBarBrowser);
void UIStyleBarBrowser(void)
{
	UIWindow *pWindow = ui_WindowCreate("Bar Definitions", 0, 0, 500, 400);
	DictionaryEArrayStruct *pUIStyleBarArray = resDictGetEArrayStruct(g_ui_BarDict);
	UIList *pList = ui_ListCreate(parse_UIStyleBar, &pUIStyleBarArray->ppReferents, 20);
	StyleBarPane *pPane = StyleBarPaneCreate();
	ui_ListAppendColumn(pList, ui_ListColumnCreate(UIListPTName, "Name", (intptr_t)"Name", NULL));
	ui_WidgetSetDimensionsEx(UI_WIDGET(pList), 0.33f, 1.f, UIUnitPercentage, UIUnitPercentage);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pPane), 0.67f, 1.f, UIUnitPercentage, UIUnitPercentage);
	UI_WIDGET(pPane)->xPOffset = 0.33f;

	ui_WindowAddChild(pWindow, pList);
	ui_WindowAddChild(pWindow, pPane);
	ui_ListSetSelectedCallback(pList, StyleBarPaneSetRef, pPane);
	ui_ListSetActivatedCallback(pList, StyleBarOpenBar, pPane);
	ui_WindowSetCloseCallback(pWindow, ui_WindowFreeOnClose, NULL);
	ui_WindowShow(pWindow);
}

#undef ParserLoadFilesAndSetCallback

typedef struct PrioritizedPaletteName
{
	S32 iPriority;
	const char *pchName;
} PrioritizedPaletteName;

static struct {
	// The size of the color palette
	U32 uiPaletteSize;

	// The state of the palette indices
	ColorDefLoading ColorDefs;

	// The current data version
	U32 uiDataVersion;

	// The global state
	UIStyleColorPaletteState Global;

	// Prioritized palette names
	PrioritizedPaletteName **eaPaletteChoices;

	// The disable flags handler
	ui_StyleColorPaletteGetDisableFlagsCB cbDisableFlags;
} s_ColorPaletteState;

UIStyleColorPaletteLoading g_ColorPalettes;
DefineContext *g_ui_pColorPaletteExtraStates;

#define RGBA_TO_VEC3(v3, rgba) ( \
					(v3)[0] = (((rgba) >> 24) & 0xff) * (1.0f / 255.0f), \
					(v3)[1] = (((rgba) >> 16) & 0xff) * (1.0f / 255.0f), \
					(v3)[2] = (((rgba) >> 8) & 0xff) * (1.0f / 255.0f) \
				)
#define VEC3_TO_RGBA(v3) ( \
					((U32)((v3)[0] * 255.0f) << 24) | \
					((U32)((v3)[1] * 255.0f) << 16) | \
					((U32)((v3)[2] * 255.0f) << 8) \
				)

__forceinline static U32 ui_StyleColorPaletteLerpRGBA(U32 a, U32 b, F32 weight)
{
	U32 out = 0;
	U8* pout = (U8 *)(&out);
	U8* pa = (U8 *)(&a);
	U8* pb = (U8 *)(&b);
	F32 weightinv = 1.f - weight;
	pout[0] = (pa[0] * weightinv + pb[0] * weight + 0.5f);
	pout[1] = (pa[1] * weightinv + pb[1] * weight + 0.5f);
	pout[2] = (pa[2] * weightinv + pb[2] * weight + 0.5f);
	pout[3] = (pa[3] * weightinv + pb[3] * weight + 0.5f);
	return out;
}

__forceinline static U32 ui_StyleColorPaletteLerpHSVA(U32 a, U32 b, F32 weight)
{
	Vec3 rgb_a, rgb_b, rgb_out;
	Vec3 hsv_a, hsv_b, hsv_out;
	U8 A_a = a & 0xff, A_b = a & 0xff, A_out;
	F32 weightinv = 1.f - weight;
	RGBA_TO_VEC3(rgb_a, a);
	RGBA_TO_VEC3(rgb_b, b);
	rgbToHsv(rgb_a, hsv_a);
	rgbToHsv(rgb_b, hsv_b);
	if (ABS(hsv_a[0] - hsv_b[0]) <= 180)
		hsv_out[0] = (hsv_a[0] * weightinv + hsv_b[0] * weight);
	else if (hsv_a[0] < hsv_b[0])
		hsv_out[0] = fmod(((hsv_a[0] + 360) * weightinv + hsv_b[0] * weight), 360);
	else
		hsv_out[0] = fmod((hsv_a[0] * weightinv + (hsv_b[0] + 360) * weight), 360);
	hsv_out[1] = (hsv_a[1] * weightinv + hsv_b[1] * weight);
	hsv_out[2] = (hsv_a[2] * weightinv + hsv_b[2] * weight);
	A_out = (A_a * weightinv + A_b * weight + 0.5f);
	hsvToRgb(hsv_out, rgb_out);
	return VEC3_TO_RGBA(rgb_out) | A_out;
}

__forceinline static U32 ui_StyleColorPaletteLerpDesaturateVA(U32 a, U32 b, F32 weight)
{
	Vec3 rgb_a, rgb_b, rgb_out;
	Vec3 hsv_a, hsv_b, hsv_out;
	U8 A_a = a & 0xff, A_b = a & 0xff, A_out;
	F32 weightinv = 1.f - weight;
	RGBA_TO_VEC3(rgb_a, a);
	RGBA_TO_VEC3(rgb_b, b);
	rgbToHsv(rgb_a, hsv_a);
	rgbToHsv(rgb_b, hsv_b);
	if (weight <= weightinv)
	{
		hsv_out[0] = hsv_a[0];
		hsv_out[1] = hsv_a[1] * (0.5f - weight) * 2.0f;
	}
	else
	{
		hsv_out[0] = hsv_b[0];
		hsv_out[1] = hsv_b[1] * (weight - 0.5f) * 2.0f;
	}
	hsv_out[2] = (hsv_a[2] * weightinv + hsv_b[2] * weight);
	A_out = (A_a * weightinv + A_b * weight + 0.5f);
	hsvToRgb(hsv_out, rgb_out);
	return VEC3_TO_RGBA(rgb_out) | A_out;
}

bool ui_StyleColorPaletteEvaluateTween(UIStyleColorPaletteTweenState *pTweenState, UIStyleColorPalette *pBase, F32 fTweenParam)
{
	UIStyleColorPaletteTween *pTween = pTweenState->pTween;
	U32 i;

	if (fTweenParam > 1.00001)
	{
		// Set output to final
		for (i = 0; i < s_ColorPaletteState.uiPaletteSize; i++)
		{
			UIStyleColorPaletteEntry *pFinal = pTweenState->pFinal ? pTweenState->pFinal->eaPalette[i] : NULL;
			UIStyleColorPaletteEntry *pOutput = pTweenState->pOutput->eaPalette[i];

			if (pFinal && pOutput)
			{
				*pOutput = *pFinal;
			}
			else if (pOutput)
			{
				StructDestroy(parse_UIStyleColorPaletteEntry, pOutput);
				pTweenState->pOutput->eaPalette[i] = NULL;
			}
		}

		return false;
	}

	for (i = 0; i < s_ColorPaletteState.uiPaletteSize; i++)
	{
		UIStyleColorPaletteEntry *pInitial = pTweenState->pInitial ? pTweenState->pInitial->eaPalette[i] : NULL;
		UIStyleColorPaletteEntry *pFinal = pTweenState->pFinal ? pTweenState->pFinal->eaPalette[i] : NULL;
		UIStyleColorPaletteEntry *pOutput = pTweenState->pOutput->eaPalette[i];
		U32 uInitialColor = 0, uFinalColor = 0;

		if (!pOutput)
			continue;

		if (!pInitial)
			pInitial = pBase->eaPalette[i];
		if (!pFinal)
			pFinal = pBase->eaPalette[i];

		uInitialColor = FIRST_IF_SET(pInitial->iCustomColor, pInitial->iColor);
		uFinalColor = FIRST_IF_SET(pFinal->iCustomColor, pFinal->iColor);

		switch (pTween->eMode)
		{
		xcase kUIStyleColorPaletteTweenModeRGBA:
			pOutput->iColor = ui_StyleColorPaletteLerpRGBA(uInitialColor, uFinalColor, fTweenParam);
		xcase kUIStyleColorPaletteTweenModeHSVA:
			pOutput->iColor = ui_StyleColorPaletteLerpHSVA(uInitialColor, uFinalColor, fTweenParam);
		xcase kUIStyleColorPaletteTweenModeDesaturateVA:
			pOutput->iColor = ui_StyleColorPaletteLerpDesaturateVA(uInitialColor, uFinalColor, fTweenParam);
		}
	}

	return true;
}

bool ui_StyleColorPaletteTickTween(UIStyleColorPaletteTweenState *pTweenState, UIStyleColorPalette *pBase)
{
	UIStyleColorPaletteTween *pTween = pTweenState->pTween;
	F32 fTweenParam = ui_TweenGetParam(pTween->eType, pTween->fTotalTime, pTween->fTimeBetweenCycles, pTweenState->fTweenTime, &pTweenState->fTweenTime);
	pTweenState->fTweenTime += g_ui_State.timestep;
	return ui_StyleColorPaletteEvaluateTween(pTweenState, pBase, fTweenParam);
}

UIStyleColorPaletteTweenState *ui_StyleColorPaletteStartTween(UIStyleColorPaletteTween *pTween, UIStyleColorPalette *pInitial, bool bCopyInitial, UIStyleColorPalette *pFinal, bool bCopyFinal, UIStyleColorPalette *pBase)
{
	UIStyleColorPaletteTweenState *pState;
	U32 i;

	if (!pTween || (!pInitial && !pFinal))
		return NULL;

	pState = StructCreate(parse_UIStyleColorPaletteTweenState);
	pState->pTween = pTween;

	if (bCopyInitial)
	{
		pState->pInitial = &pState->InitialPalette;
		eaSetSize(&pState->InitialPalette.eaPalette, s_ColorPaletteState.uiPaletteSize);

		for (i = 0; i < s_ColorPaletteState.uiPaletteSize; i++)
		{
			if (pInitial && pInitial->eaPalette[i])
			{
				pState->InitialPalette.eaPalette[i] = StructCreate(parse_UIStyleColorPaletteEntry);
				pState->InitialPalette.eaPalette[i]->iColor = FIRST_IF_SET(pInitial->eaPalette[i]->iCustomColor, pInitial->eaPalette[i]->iColor);
			}
		}
	}
	else
		pState->pInitial = pInitial;

	if (bCopyFinal)
	{
		pState->pFinal = &pState->FinalPalette;
		eaSetSize(&pState->FinalPalette.eaPalette, s_ColorPaletteState.uiPaletteSize);

		for (i = 0; i < s_ColorPaletteState.uiPaletteSize; i++)
		{
			if (pFinal && pFinal->eaPalette[i])
			{
				pState->FinalPalette.eaPalette[i] = StructCreate(parse_UIStyleColorPaletteEntry);
				pState->FinalPalette.eaPalette[i]->iColor = FIRST_IF_SET(pFinal->eaPalette[i]->iCustomColor, pFinal->eaPalette[i]->iColor);
			}
		}
	}
	else
		pState->pFinal = pFinal;

	pState->pOutput = StructCreate(parse_UIStyleColorPalette);
	eaSetSize(&pState->pOutput->eaPalette, s_ColorPaletteState.uiPaletteSize);

	for (i = 0; i < s_ColorPaletteState.uiPaletteSize; i++)
	{
		if (pState->pInitial && pState->pInitial->eaPalette[i] || pState->pFinal && pState->pFinal->eaPalette[i])
			pState->pOutput->eaPalette[i] = StructCreate(parse_UIStyleColorPaletteEntry);
	}

	return pState;
}

static int ui_StyleColorPaletteOverrideSort(UIStyleColorPaletteDef *pContext, const UIStyleColorPaletteOverrideState **ppLeft, const UIStyleColorPaletteOverrideState **ppRight)
{
	S32 iPosLeft = eaFind(&pContext->eaStateDef, (*ppLeft)->pStateDef);
	S32 iPosRight = eaFind(&pContext->eaStateDef, (*ppRight)->pStateDef);
	return iPosLeft - iPosRight;
}

void ui_StyleColorPaletteTick(UIStyleColorPaletteState *pPalette)
{
	UIStyleColorPaletteDef *pPaletteDef;
	UIStyleColorPaletteOverrideSequence *pSequence, *pNextSequence;
	const char *pchPaletteName = pPalette ? FIRST_IF_SET(pPalette->pchCurrentPalette, s_ColorPaletteState.Global.pchCurrentPalette) : NULL;
	S32 i, j;
	bool bStateChange = false;
	bool bOverrideChange = false;
	U32 uDisableFlags = s_ColorPaletteState.cbDisableFlags ? s_ColorPaletteState.cbDisableFlags() : 0;
	U32 uDisabledStates[COLOR_PALETTE_MAX_STATE_BUFFER] = {0};

	// Only update once per frame
	if (!pPalette || pPalette->uiLastFrame == g_ui_State.uiFrameCount)
		return;
	pPalette->uiLastFrame = g_ui_State.uiFrameCount;

	PERFINFO_AUTO_START_FUNC();

	// Check to see if the palette needs to be reset
	if (pPalette->uiDataVersion != s_ColorPaletteState.uiDataVersion)
	{
		eaDestroyStruct(&pPalette->eaActiveOverrides, parse_UIStyleColorPaletteOverrideState);
		StructDestroySafe(parse_UIStyleColorPaletteTweenState, &pPalette->pPaletteChange);
		StructDestroySafe(parse_UIStyleColorPalette, &pPalette->pFinalPalette);
		memset(pPalette->uiStates, 0, sizeof(pPalette->uiStates));
		memset(pPalette->uiLastStates, 0, sizeof(pPalette->uiStates));
		pPalette->uiDataVersion = s_ColorPaletteState.uiDataVersion;
	}

	// Initialize the palette
	if (!pPalette->pFinalPalette)
	{
		pPalette->pFinalPalette = StructCreate(parse_UIStyleColorPalette);
		eaSetSize(&pPalette->pFinalPalette->eaPalette, s_ColorPaletteState.uiPaletteSize);
	}

	// Select the palette
	pPaletteDef = eaIndexedGetUsingString(&g_ColorPalettes.eaPalettes, pPalette->pchNextPalette);
	if (!pPaletteDef)
		pPaletteDef = eaIndexedGetUsingString(&g_ColorPalettes.eaPalettes, pchPaletteName);
	if (!pPaletteDef && eaSize(&g_ColorPalettes.eaPalettes))
		pPaletteDef = g_ColorPalettes.eaPalettes[0];
	if (!pPaletteDef)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// Palette change?
	if (pchPaletteName != pPaletteDef->pchName)
	{
		if (eaIndexedGetUsingString(&g_ColorPalettes.eaPalettes, pchPaletteName))
		{
			StructDestroySafe(parse_UIStyleColorPaletteTweenState, &pPalette->pPaletteChange);

			// Tween out of current colors
			if (pPaletteDef->pTween)
				pPalette->pPaletteChange = ui_StyleColorPaletteStartTween(pPaletteDef->pTween, pPalette->pFinalPalette, true, &pPaletteDef->BasePalette, false, NULL);
		}

		if (pchPaletteName == pPalette->pchCurrentPalette)
			pPalette->pchCurrentPalette = pPaletteDef->pchName;
		pPalette->pchNextPalette = NULL;
		memset(pPalette->uiLastStates, 0, sizeof(pPalette->uiStates));
		bStateChange = true;
		eaDestroyStruct(&pPalette->eaActiveOverrides, parse_UIStyleColorPaletteOverrideState);
		SETB(pPalette->uiStates, kUIStyleColorPaletteStateActivate);
	}

	// Tick the palette swap tween
	if (pPalette->pPaletteChange && !ui_StyleColorPaletteTickTween(pPalette->pPaletteChange, &pPaletteDef->BasePalette))
		StructDestroySafe(parse_UIStyleColorPaletteTweenState, &pPalette->pPaletteChange);

	// Handle the disable flags
	for (i = eaSize(&g_ColorPalettes.eaStateNames) - 1; i >= 0; i--)
	{
		if (g_ColorPalettes.eaStateNames[i]->iDisableFlag)
		{
			if ((uDisableFlags & (1 << (g_ColorPalettes.eaStateNames[i]->iDisableFlag - 1))) != 0)
				SETB(uDisabledStates, g_ColorPalettes.eaStateNames[i]->iState);
		}
	}

	// Update the states
	for (i = 0; i < COLOR_PALETTE_MAX_STATE_BUFFER; i++)
	{
		U32 uiStates = (pPalette->uiStates[i] | s_ColorPaletteState.Global.uiStates[i]) & (~uDisabledStates[i]);
		U32 uiDiff = uiStates ^ pPalette->uiLastStates[i];
		if (uiDiff != 0)
		{
			pPalette->uiLastStates[i] = uiStates;
			bStateChange = true;
		}
	}

	// Reevaluate the overrides on a state change
	if (bStateChange)
	{
		for (i = eaSize(&pPaletteDef->eaStateDef) - 1; i >= 0; i--)
		{
			UIStyleColorPaletteStateDef *pStateDef = pPaletteDef->eaStateDef[i];
			for (j = eaiSize(&pStateDef->eaiStates) - 1; j >= 0; j--)
			{
				if (!TSTB(pPalette->uiLastStates, pStateDef->eaiStates[j]))
					break;
			}
			if (j < 0)
			{
				for (j = eaSize(&pPalette->eaActiveOverrides) - 1; j >= 0; j--)
				{
					if (pPalette->eaActiveOverrides[j]->pStateDef == pStateDef)
						break;
				}
				if (j < 0)
				{
					// Add new override
					UIStyleColorPaletteOverrideState *pOverride = StructCreate(parse_UIStyleColorPaletteOverrideState);
					pOverride->pStateDef = pStateDef;
					eaPush(&pPalette->eaActiveOverrides, pOverride);
					bOverrideChange = true;
				}
			}
			else
			{
				for (j = eaSize(&pPalette->eaActiveOverrides) - 1; j >= 0; j--)
				{
					if (pPalette->eaActiveOverrides[j]->pStateDef == pStateDef)
						break;
				}
				if (j >= 0)
				{
					UIStyleColorPaletteOverrideState *pOverride = pPalette->eaActiveOverrides[j];
					// Remove existing override
					if (pOverride->pTweenState)
					{
						// Exit from current colors in the tween
						UIStyleColorPaletteTweenState *pOldTween = pOverride->pTweenState;
						pOverride->pTweenState = ui_StyleColorPaletteStartTween(pOverride->pStateDef->pExitTween, pOldTween->pOutput, true, NULL, false, &pPaletteDef->BasePalette);
						StructDestroy(parse_UIStyleColorPaletteTweenState, pOldTween);
					}
					else
					{
						// Exit from current sequence
						if (pOverride->pStateDef->pExitTween)
						{
							pSequence = eaGet(&pOverride->pStateDef->eaSequence, pOverride->iCurrentSequence);
							pOverride->pTweenState = ui_StyleColorPaletteStartTween(pOverride->pStateDef->pExitTween, pSequence ? &pSequence->Palette : NULL, false, NULL, false, &pPaletteDef->BasePalette);
						}
					}
					pOverride->iNextSequence = eaSize(&pOverride->pStateDef->eaSequence);
					bOverrideChange = true;
				}
			}
		}

		if (bOverrideChange)
		{
			// Sort overrides by StateDef index
			eaQSort_s(pPalette->eaActiveOverrides, ui_StyleColorPaletteOverrideSort, pPaletteDef);
		}
	}

	// Tick the overrides
	for (i = eaSize(&pPalette->eaActiveOverrides) - 1; i >= 0; i--)
	{
		UIStyleColorPaletteOverrideState *pOverride = pPalette->eaActiveOverrides[i];

		if (!pOverride->pTweenState || !ui_StyleColorPaletteTickTween(pOverride->pTweenState, &pPaletteDef->BasePalette))
		{
			if (pOverride->pTweenState)
				StructDestroySafe(parse_UIStyleColorPaletteTweenState, &pOverride->pTweenState);

			if (pOverride->iNextSequence >= eaSize(&pOverride->pStateDef->eaSequence))
			{
				// Exiting override
				eaRemove(&pPalette->eaActiveOverrides, i);
				StructDestroy(parse_UIStyleColorPaletteOverrideState, pOverride);
				continue;
			}
			else if (pOverride->iNextSequence != -1)
			{
				// Switch to next sequence
				pSequence = eaGet(&pOverride->pStateDef->eaSequence, pOverride->iCurrentSequence);
				pNextSequence = eaGet(&pOverride->pStateDef->eaSequence, pOverride->iNextSequence);

				if (pNextSequence && pNextSequence->pTween)
				{
					pOverride->pTweenState = ui_StyleColorPaletteStartTween(pNextSequence->pTween, pSequence ? &pSequence->Palette : NULL, false, &pNextSequence->Palette, false, &pPaletteDef->BasePalette);
					pOverride->iCurrentSequence = pOverride->iNextSequence;
					pOverride->iNextSequence = pOverride->iCurrentSequence + 1;
					if (pNextSequence->pchNext)
					{
						// Find next in sequence
						for (j = eaSize(&pOverride->pStateDef->eaSequence) - 1; j >= 0; j--)
						{
							if (pOverride->pStateDef->eaSequence[j]->pchName == pNextSequence->pchNext)
								break;
						}
						if (j >= 0)
							pOverride->iNextSequence = j;
					}
					if (pOverride->iNextSequence >= eaSize(&pOverride->pStateDef->eaSequence))
						pOverride->iNextSequence = -1;

					// Initialize the tween to the start values
					ui_StyleColorPaletteEvaluateTween(pOverride->pTweenState, &pPaletteDef->BasePalette, 0);
				}
				else if (pNextSequence)
				{
					// Stop on next sequence
					pOverride->iCurrentSequence = pOverride->iNextSequence;
					pOverride->iNextSequence = -1;
				}
				else
				{
					// Stop sequencing on current
					pOverride->iNextSequence = -1;
				}
			}
		}
	}

	// Bake the final color palette
	for (i = 0; i < (S32)s_ColorPaletteState.uiPaletteSize; i++)
	{
		UIStyleColorPaletteEntry *pEntry = NULL;

		for (j = eaSize(&pPalette->eaActiveOverrides) - 1; j >= 0; j--)
		{
			UIStyleColorPaletteOverrideState *pOverride = pPalette->eaActiveOverrides[j];
			pSequence = eaGet(&pOverride->pStateDef->eaSequence, pOverride->iCurrentSequence);
			pEntry = pOverride->pTweenState ? pOverride->pTweenState->pOutput->eaPalette[i] : pSequence ? pSequence->Palette.eaPalette[i] : NULL;
			if (pEntry)
				break;
		}

		if (!pEntry && pPalette->pPaletteChange)
			pEntry = pPalette->pPaletteChange->pOutput->eaPalette[i];

		if (!pEntry)
			pEntry = pPaletteDef->BasePalette.eaPalette[i];

		if (!pEntry)
		{
			if (pPalette->pFinalPalette->eaPalette[i])
			{
				StructDestroy(parse_UIStyleColorPaletteEntry, pPalette->pFinalPalette->eaPalette[i]);
				pPalette->pFinalPalette->eaPalette[i] = NULL;
			}
			continue;
		}

		if (!pPalette->pFinalPalette->eaPalette[i])
			pPalette->pFinalPalette->eaPalette[i] = StructCreate(parse_UIStyleColorPaletteEntry);
		*pPalette->pFinalPalette->eaPalette[i] = *pEntry;
	}

	PERFINFO_AUTO_STOP_FUNC();
}

static void ColorPaletteFixup(UIStyleColorPalette *pPalette, bool bDefaults, UIStyleColorPalette *pDefaultPalette)
{
	S32 j;

	eaSetSize(&pPalette->eaPalette, s_ColorPaletteState.uiPaletteSize)
	for (j = 0; j < eaSize(&s_ColorPaletteState.ColorDefs.eaColors); j++)
	{
		if (s_ColorPaletteState.ColorDefs.eaColors[j])
		{
			UIStyleColorPaletteEntry *pEntry = eaIndexedGetUsingString(&pPalette->eaColorDef, s_ColorPaletteState.ColorDefs.eaColors[j]->pchName);

			if (!pEntry && bDefaults)
			{
				// Create a default palette entry
				if (pDefaultPalette)
				{
					pEntry = eaIndexedGetUsingString(&pDefaultPalette->eaColorDef, s_ColorPaletteState.ColorDefs.eaColors[j]->pchName);
					if (pEntry)
					{
						pEntry = StructClone(parse_UIStyleColorPaletteEntry, pEntry);
						pEntry->iPaletteIndex = j;
					}
				}

				if (!pEntry)
				{
					pEntry = StructCreate(parse_UIStyleColorPaletteEntry);
					pEntry->iPaletteIndex = j;
				}
			}
			else if (pEntry)
			{
				pEntry->iPaletteIndex = j;
				pEntry = StructClone(parse_UIStyleColorPaletteEntry, pEntry);
			}

			pPalette->eaPalette[j] = pEntry;
		}
	}
}

static void ColorPalettesReload(const char *pchPath, S32 iWhen)
{
	S32 i, j, k, l;
	const char **eaPaletteNames = NULL;

	eaIndexedDisable(&s_ColorPaletteState.ColorDefs.eaColors);

	s_ColorPaletteState.uiDataVersion++;

	loadstart_printf("Loading colors palettes... ");
	StructReset(parse_UIStyleColorPaletteLoading, &g_ColorPalettes);
	ParserLoadFiles(NULL, UI_STYLE_PALETTE_FILE, "ColorPalettes.bin", PARSER_OPTIONALFLAG, parse_UIStyleColorPaletteLoading, &g_ColorPalettes);

	for (i = 0; i < eaSize(&g_ColorPalettes.eaColors); i++)
		eaPush(&eaPaletteNames, g_ColorPalettes.eaColors[i]->pchName);

	// Remove old color indexes
	for (i = eaSize(&s_ColorPaletteState.ColorDefs.eaColors) - 1; i >= 0; i--)
	{
		if (s_ColorPaletteState.ColorDefs.eaColors[i] && eaFind(&eaPaletteNames, s_ColorPaletteState.ColorDefs.eaColors[i]->pchName) < 0)
		{
			StructDestroy(parse_ColorDef, s_ColorPaletteState.ColorDefs.eaColors[i]);
			s_ColorPaletteState.ColorDefs.eaColors[i] = NULL;
		}

		if (!s_ColorPaletteState.ColorDefs.eaColors[i] && i == eaSize(&s_ColorPaletteState.ColorDefs.eaColors) - 1)
			eaPop(&s_ColorPaletteState.ColorDefs.eaColors);
	}

	// Generate new color indexes
	for (i = 0; i < eaSize(&eaPaletteNames); i++)
	{
		S32 iFree = eaSize(&s_ColorPaletteState.ColorDefs.eaColors);

		// Find existing index or first unused index
		for (j = eaSize(&s_ColorPaletteState.ColorDefs.eaColors) - 1; j >= 0; j--)
		{
			if (s_ColorPaletteState.ColorDefs.eaColors[j] && s_ColorPaletteState.ColorDefs.eaColors[j]->pchName == eaPaletteNames[i])
				break;
			else if (!s_ColorPaletteState.ColorDefs.eaColors[j])
				iFree = j;
		}

		if (j < 0)
		{
			ColorDef *pDef = eaGetStruct(&s_ColorPaletteState.ColorDefs.eaColors, parse_ColorDef, iFree);
			// Set color entry
			pDef->pchName = eaPaletteNames[i];
			pDef->uiColor = UI_STYLE_PALETTE_KEY | (iFree * (UI_STYLE_PALETTE_MASK + 1));
		}
	}

	// Generate the state enum
	DefineClearAllListsAndReset(&g_ui_pColorPaletteExtraStates);
	for (i = 0; i < eaSize(&g_ColorPalettes.eaStateNames); i++)
	{
		g_ColorPalettes.eaStateNames[i]->iState = kUIStyleColorPaletteState_MAX + i;
		DefineAddInt(g_ui_pColorPaletteExtraStates, g_ColorPalettes.eaStateNames[i]->pchName, kUIStyleColorPaletteState_MAX + i);

		if (g_ColorPalettes.eaStateNames[i]->iDisableFlag < 0 || g_ColorPalettes.eaStateNames[i]->iDisableFlag > 32)
			ErrorFilenamef(UI_STYLE_PALETTE_FILE, "Palette override state '%s' is attempting to use DisableFlag %d. Valid DisableFlags are from 0 (no disable flag) to 32.", g_ColorPalettes.eaStateNames[i]->pchName, g_ColorPalettes.eaStateNames[i]->iDisableFlag);
	}

	s_ColorPaletteState.uiPaletteSize = eaSize(&s_ColorPaletteState.ColorDefs.eaColors);

	// Validate color palettes
	for (i = 0; i < eaSize(&g_ColorPalettes.eaPalettes); i++)
	{
		UIStyleColorPaletteDef *pPalette = g_ColorPalettes.eaPalettes[i];
		UIStyleColorPaletteDef *pBorrow = pPalette->pchBorrowFrom ? eaIndexedGetUsingString(&g_ColorPalettes.eaPalettes, pPalette->pchBorrowFrom) : NULL;

		if (pPalette->pchBorrowFrom && !pBorrow)
			ErrorFilenamef(UI_STYLE_PALETTE_FILE, "Palette '%s' borrows from palette '%s' which doesn't exist", pPalette->pchName, pPalette->pchBorrowFrom);

		// Recursive borrow from are not supported
		if (pBorrow && pBorrow->pchBorrowFrom)
			ErrorFilenamef(UI_STYLE_PALETTE_FILE, "Palette '%s' borrows from palette '%s' which borrows from '%s'", pPalette->pchName, pBorrow->pchName, pBorrow->pchBorrowFrom);

		// Ensure all entries are specified
		for (j = 0; j < eaSize(&eaPaletteNames) && !pBorrow; j++)
		{
			if (eaIndexedFindUsingString(&pPalette->BasePalette.eaColorDef, eaPaletteNames[j]) < 0)
				ErrorFilenamef(UI_STYLE_PALETTE_FILE, "Palette '%s' doesn't specify color '%s' in palette", pPalette->pchName, eaPaletteNames[j]);
		}

		// Ensure only valid entries are specified
		for (j = 0; j < eaSize(&pPalette->BasePalette.eaColorDef); j++)
		{
			pPalette->BasePalette.eaColorDef[j]->iPaletteIndex = -1;
			if (eaFind(&eaPaletteNames, pPalette->BasePalette.eaColorDef[j]->pchName) < 0)
				ErrorFilenamef(UI_STYLE_PALETTE_FILE, "Palette '%s' specifies color '%s' which isn't a palette color", pPalette->pchName, pPalette->BasePalette.eaColorDef[j]->pchName);
		}

		// Generate palette array
		ColorPaletteFixup(&pPalette->BasePalette, true, pBorrow ? &pBorrow->BasePalette : NULL);
	}

	// Validate override palettes
	for (i = 0; i < eaSize(&g_ColorPalettes.eaPalettes); i++)
	{
		UIStyleColorPaletteDef *pPalette = g_ColorPalettes.eaPalettes[i];

		// Process the state defs
		for (j = 0; j < eaSize(&pPalette->eaStateDef); j++)
		{
			// Convert the state names to enum values
			for (k = 0; k < eaSize(&pPalette->eaStateDef[j]->eapchInState); k++)
			{
				S32 iState = StaticDefineIntGetInt(UIStyleColorPaletteStatesEnum, pPalette->eaStateDef[j]->eapchInState[k]);
				if (iState == -1)
					ErrorFilenamef(UI_STYLE_PALETTE_FILE, "Palette '%s' specifies state '%s' which isn't a valid palette state", pPalette->pchName, pPalette->eaStateDef[j]->eapchInState[k]);
				else
					eaiPush(&pPalette->eaStateDef[j]->eaiStates, iState);
			}

			// Generate the sequence palettes
			for (k = 0; k < eaSize(&pPalette->eaStateDef[j]->eaSequence); k++)
			{
				UIStyleColorPaletteDef *pDefaultPalette = NULL;

				if (pPalette->eaStateDef[j]->eaSequence[k]->pchNext)
				{
					for (l = eaSize(&pPalette->eaStateDef[j]->eaSequence) - 1; l >= 0; l--)
						if (pPalette->eaStateDef[j]->eaSequence[l]->pchName == pPalette->eaStateDef[j]->eaSequence[k]->pchNext)
							break;
					if (l < 0)
						ErrorFilenamef(UI_STYLE_PALETTE_FILE, "Palette '%s' StateDef sequence refers to sequence '%s' which isn't in the StateDef", pPalette->pchName, pPalette->eaStateDef[j]->eaSequence[k]->pchNext);
				}

				if (pPalette->eaStateDef[j]->eaSequence[k]->pchBorrowFrom)
				{
					pDefaultPalette = eaIndexedGetUsingString(&g_ColorPalettes.eaPalettes, pPalette->eaStateDef[j]->eaSequence[k]->pchBorrowFrom);
					if (!pDefaultPalette)
						ErrorFilenamef(UI_STYLE_PALETTE_FILE, "Palette '%s' StateDef sequence borrows from palette '%s' which doesn't exist", pPalette->pchName, pPalette->eaStateDef[j]->eaSequence[k]->pchNext);
				}

				ColorPaletteFixup(&pPalette->eaStateDef[j]->eaSequence[k]->Palette, pDefaultPalette != NULL, pDefaultPalette ? &pDefaultPalette->BasePalette : NULL);
			}
		}
	}

	ColorDefLoadingRefresh();
	loadend_printf("done. (%d palettes, %d entries)", eaSize(&g_ColorPalettes.eaPalettes), eaSize(&g_ColorPalettes.eaColors));

	eaDestroy(&eaPaletteNames);
}

void ui_StyleLoadColorPalettes(void)
{
	static char once;
	if (!once)
	{
		once = 1;
		ColorDefLoadingAdd(&s_ColorPaletteState.ColorDefs);
		ColorPalettesReload(NULL, 0);
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, UI_STYLE_PALETTE_FILE "*", ColorPalettesReload);
	}
}

void ui_StyleColorPaletteSetState(UIStyleColorPaletteState *pPalette, UIStyleColorPaletteStates eState, bool bToggle)
{
	if (!pPalette)
		pPalette = &s_ColorPaletteState.Global;

	if (bToggle)
		SETB(pPalette->uiStates, eState);
	else
		CLRB(pPalette->uiStates, eState);
}

void ui_StyleColorPaletteSetStateName(UIStyleColorPaletteState *pPalette, const char *pchState, bool bToggle)
{
	S32 iState = StaticDefineIntGetInt(UIStyleColorPaletteStatesEnum, pchState);
	if (iState != -1)
		ui_StyleColorPaletteSetState(pPalette, iState, bToggle);
}

U32 ui_StyleColorPaletteIndexEx(U32 uColor, UIStyleColorPaletteState *pPalette)
{
	if (!pPalette)
		pPalette = &s_ColorPaletteState.Global;

	if ((uColor & UI_STYLE_PALETTE_MASK) == UI_STYLE_PALETTE_KEY)
	{
		if (pPalette->uiLastFrame != g_ui_State.uiFrameCount)
			ui_StyleColorPaletteTick(pPalette);

		if (pPalette->pFinalPalette)
		{
			UIStyleColorPaletteEntry *pEntry = eaGet(&pPalette->pFinalPalette->eaPalette, uColor / (UI_STYLE_PALETTE_MASK + 1));
			if (pEntry)
				return FIRST_IF_SET(pEntry->iCustomColor, pEntry->iColor);
		}
	}

	return uColor;
}

void ui_StyleColorPaletteSetDisableFlags(ui_StyleColorPaletteGetDisableFlagsCB cbDisableFlags)
{
	s_ColorPaletteState.cbDisableFlags = cbDisableFlags;
}

U32 ui_StyleColorPaletteIndex(U32 uColor)
{
	if ((uColor & UI_STYLE_PALETTE_MASK) == UI_STYLE_PALETTE_KEY)
		return ui_StyleColorPaletteIndexEx(uColor, &s_ColorPaletteState.Global);

	return uColor;
}

AUTO_RUN;
void ui_StyleColorPalette_sm_GetColor(void)
{
	extern U32 (*g_sm_GetColorMapping)(U32);
	g_sm_GetColorMapping = ui_StyleColorPaletteIndex;
	g_ui_pColorPaletteExtraStates = DefineCreate();
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ColorPaletteExists);
bool ui_StyleColorPaletteExists(const char *palette)
{
	return eaIndexedGetUsingString(&g_ColorPalettes.eaPalettes, palette) != NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ColorPaletteCurrent);
const char *ui_StyleColorPaletteCurrent(void)
{
	PrioritizedPaletteName *pPrioritizedPalette = NULL;
	if (eaSize(&s_ColorPaletteState.eaPaletteChoices) > 0)
	{
		pPrioritizedPalette = eaTail(&s_ColorPaletteState.eaPaletteChoices);
		return pPrioritizedPalette->pchName;
	}
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ColorPaletteCurrentPriority);
const char *ui_StyleColorPalettePriorityCurrent(S32 iPriority)
{
	S32 i;
	for (i = 0; i < eaSize(&s_ColorPaletteState.eaPaletteChoices); i++)
	{
		if (s_ColorPaletteState.eaPaletteChoices[i]->iPriority == iPriority)
			return s_ColorPaletteState.eaPaletteChoices[i]->pchName;
	}
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(SwitchUIColorPalette);
void ui_StyleColorPaletteSwitch(const char *palette);

AUTO_COMMAND ACMD_NAME("SwitchUIColorPalette") ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void ui_StyleColorPaletteSwitch(const char *palette)
{
	ui_StyleColorPaletteSwitchPriority(palette, kUIStyleColorPalettePriority_Default);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("SwitchUIColorPalettePriority");
void ui_StyleColorPaletteSwitchPriority(const char *palette, S32 iPriority)
{
	PrioritizedPaletteName *pPrioritizedPalette = NULL;
	S32 i;

	for (i = 0; i < eaSize(&s_ColorPaletteState.eaPaletteChoices); i++)
	{
		if (s_ColorPaletteState.eaPaletteChoices[i]->iPriority == iPriority)
		{
			pPrioritizedPalette = s_ColorPaletteState.eaPaletteChoices[i];
			break;
		}
	}

	if (!pPrioritizedPalette)
	{
		if (!palette || !*palette)
			return;

		pPrioritizedPalette = calloc(1, sizeof(PrioritizedPaletteName));
		if (!pPrioritizedPalette)
			return;

		pPrioritizedPalette->iPriority = iPriority;
		if (!eaSize(&s_ColorPaletteState.eaPaletteChoices))
		{
			eaPush(&s_ColorPaletteState.eaPaletteChoices, pPrioritizedPalette);
		}
		else
		{
			for (i = eaSize(&s_ColorPaletteState.eaPaletteChoices) - 1; i > 0; i--)
			{
				if (iPriority >= s_ColorPaletteState.eaPaletteChoices[i]->iPriority)
					break;
			}

			if (iPriority < s_ColorPaletteState.eaPaletteChoices[i]->iPriority)
				eaInsert(&s_ColorPaletteState.eaPaletteChoices, pPrioritizedPalette, i);
			else
				eaInsert(&s_ColorPaletteState.eaPaletteChoices, pPrioritizedPalette, i + 1);
		}
	}

	if (!palette || !*palette)
	{
		eaFindAndRemove(&s_ColorPaletteState.eaPaletteChoices, pPrioritizedPalette);
		free(pPrioritizedPalette);
	}
	else
	{
		pPrioritizedPalette->pchName = allocAddString(palette);
	}

	if (eaSize(&s_ColorPaletteState.eaPaletteChoices) > 0)
	{
		pPrioritizedPalette = eaTail(&s_ColorPaletteState.eaPaletteChoices);
		if (s_ColorPaletteState.Global.pchNextPalette
			? pPrioritizedPalette->pchName != s_ColorPaletteState.Global.pchNextPalette
			: pPrioritizedPalette->pchName != s_ColorPaletteState.Global.pchCurrentPalette)
		{
			if (pPrioritizedPalette->pchName == s_ColorPaletteState.Global.pchCurrentPalette)
				s_ColorPaletteState.Global.pchNextPalette = NULL;
			else
				s_ColorPaletteState.Global.pchNextPalette = pPrioritizedPalette->pchName;
		}
	}
}

AUTO_COMMAND ACMD_NAME(ColorPaletteSetGlobalStateName) ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void ui_StyleColorExprPaletteSetGlobalStateName(const char *pchState, bool bToggle);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ColorPaletteSetGlobalStateName);
void ui_StyleColorExprPaletteSetGlobalStateName(const char *pchState, bool bToggle)
{
	ui_StyleColorPaletteSetStateName(NULL, pchState, bToggle);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GolorPaletteResetGlobalStates);
void ui_StyleColorExprPalettResetGlobalStates(void)
{
	memset(s_ColorPaletteState.Global.uiStates, 0, sizeof(s_ColorPaletteState.Global.uiStates));
}

