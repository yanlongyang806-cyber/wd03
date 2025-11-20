/***************************************************************************
 
 
 
 ***************************************************************************/

#include "earray.h"
#include "sm_parser.h"
#include "textparser.h"
#include "MemoryPool.h"
#include "Color.h"

#include "GfxSprite.h"
#include "GfxSpriteText.h"
#include "GfxClipper.h"
#include "GfxPrimitive.h"
#include "crypt.h"
#include "StringUtil.h"

#include "smf_parse.h"
#include "smf_format.h"
#include "smf_render.h"
#include "inputMouse.h"
#include "GfxTexAtlas.h"
#include "cmdparse.h"

#include "UICore.h"
#include "UIStyle.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

static bool s_bAllowAnchor = true;
static SMFPlaySoundCallbackFunc s_playSoundCallbackFunc = NULL;
static SMFImageSkinOverrideFunc s_imageSkinOverrideFunc = NULL;
static bool s_bDebug = false;

// Toggle smf layout debugging
AUTO_CMD_INT(s_bDebug, smf_debug) ACMD_CATEGORY(Debug);

#define TAG_MATCHES(y) (smf_aTagDefs[pBlock->sType].id==k_##y)

void smf_setPlaySoundCallbackFunc(SMFPlaySoundCallbackFunc func)
{
	s_playSoundCallbackFunc = func;
}

void smf_setImageSkinOverrideCallbackFunc(SMFImageSkinOverrideFunc func)
{
	s_imageSkinOverrideFunc = func;
}

/**********************************************************************func*
 * Render
 *
 */
void smf_Render(SMBlock *pBlock, TextAttribs *pattrs,
	int iXBase, int iYBase, float fZBase, unsigned char chAlpha,
	float fRenderScale,
	int (*callback)(char *pch), const CBox *pClip, SpriteProperties *pSpriteProps)
{
	bool bHover = false;
	SMAnchor *pAnchor = NULL;
	bool bDraw = true;

	if(!pBlock->bAlwaysRender
		&& (pBlock->pos.iWidth==0 || pBlock->pos.iX<0 || pBlock->pos.iY<0)
		&& pBlock->sType>=kFormatTags_Count*2)
	{
		return;
	}

	if (pClip)
	{
		CBox BlockBox;
		BuildCBox(&BlockBox, pBlock->pos.iX*fRenderScale + iXBase, pBlock->pos.iY*fRenderScale + iYBase, pBlock->pos.iWidth*fRenderScale, pBlock->pos.iHeight*fRenderScale);
		// if everything is within the box, we don't need to check intersections for children either.
		if (CBoxContains(pClip, &BlockBox))
			pClip = NULL;
		if (pClip && !CBoxIntersects(&BlockBox, pClip))
			bDraw = false;
	}

	PERFINFO_AUTO_START("top", 1);

	// This draws bounding boxes around every block. Useful for layout
	//   debugging.
	if(bDraw && s_bDebug)
	{
		extern void gfxDrawBox(F32 x1, F32 y1, F32 x2, F32 y2, F32 z, Color color);

		clipperPush(NULL);
		gfxDrawBox(pBlock->pos.iX + iXBase,
			pBlock->pos.iY*fRenderScale + iYBase,
			pBlock->pos.iWidth*fRenderScale + pBlock->pos.iX*fRenderScale + iXBase,
			pBlock->pos.iHeight*fRenderScale + pBlock->pos.iY*fRenderScale + iYBase,
			fZBase,
			colorFromRGBA(0xff00ffff));
		clipperPop();
	}

	// Although tracked, multiply nested anchors are ignored. Always use
	// the first anchor if available.
	if(eaSize(&pattrs->ppAnchor)>1 && s_bAllowAnchor)
	{
		pAnchor = (SMAnchor *)pattrs->ppAnchor[1];
	}

	if(pBlock->sType == -1)
	{
		// Top level block
	}
	else if(smf_HandleFormatting(pBlock, pattrs))
	{
		// Should be before the rest.
		// Nothing more to do
	}
	else if(TAG_MATCHES(sm_nolink))
	{
		s_bAllowAnchor = false;
	}
	else if(TAG_MATCHES(sm_nolink_end))
	{
		s_bAllowAnchor = true;
	}
	else if (TAG_MATCHES(sm_sound))
	{
		SMSound *pSound = (SMSound *)pBlock->pv;
		if(pSound)
		{
			if(pSound->pcSoundEventPath)
			{
				if(s_playSoundCallbackFunc)
				{
					// since this may be called more than once with the same data
					// make sure we don't re-fire the event
					if(!pSound->bPlayed)
					{
						s_playSoundCallbackFunc(pSound->pcSoundEventPath);
						pSound->bPlayed = 1;
					}
				}
			}
		}
	}
	else if (TAG_MATCHES(sm_ws))
	{
		if (pAnchor && pAnchor->bHover)
		{
			GfxFont *pttFont;
			GfxFont rp;
			U32 iColorTop;
			U32 iColorBottom;
			F32 fX;
			F32 fY;
			smf_MakeFont(&pttFont, &rp, pattrs);
			fX = pBlock->pos.iX*fRenderScale + iXBase;
			fY = pBlock->pos.iY*fRenderScale + iYBase + pBlock->pos.iHeight*fRenderScale;
			iColorTop = PTR_TO_U32(eaTail(&pattrs->ppColorTop));
			iColorBottom = PTR_TO_U32(eaTail(&pattrs->ppColorBottom));
			gfxDrawLine(fX, fY, fZBase + 0.001, fX + pBlock->pos.iWidth, fY, colorFromRGBA(lerpRGBAColors(iColorBottom, iColorTop, 0.5)));
			gfxDrawLine(fX, fY + 1, fZBase + 0.001, fX + pBlock->pos.iWidth, fY + 1, ColorBlack);
		}
	}
	else if(TAG_MATCHES(sm_text))
	{
		U32 iColorTop;
		U32 iColorBottom;
		int rgba[4];
		float fScale;
		GfxFont *pttFont;
		GfxFont rp;

		// Set the font up
		smf_MakeFont(&pttFont, &rp, pattrs);

		// Get color
		iColorTop = PTR_TO_U32(eaTail(&pattrs->ppColorTop));
		iColorBottom = PTR_TO_U32(eaTail(&pattrs->ppColorBottom));

		rgba[0] = rgba[1] = ColorRGBAMultiplyAlpha(iColorTop, chAlpha);
		rgba[2] = rgba[3] = ColorRGBAMultiplyAlpha(iColorBottom, chAlpha);

		// Todo (ama): Solve this in a better way later.
		pttFont->uiDropShadowColor = ColorRGBAMultiplyAlpha(pttFont->uiDropShadowColor, chAlpha);
		pttFont->uiOutlineColor = ColorRGBAMultiplyAlpha(pttFont->uiOutlineColor, chAlpha);

		// Scale
		fScale = ((float)PTR_TO_U32(pattrs->ppScale[eaSize(&pattrs->ppScale)-1])/SMF_FONT_SCALE)*fRenderScale;

		if (bDraw)
		{
			F32 fX = pBlock->pos.iX*fRenderScale + iXBase;
			F32 fY = pBlock->pos.iY*fRenderScale + iYBase + pBlock->pos.iHeight*fRenderScale;
			bool useSpriteProps = pSpriteProps && pSpriteProps->is_3D;

			gfxfont_PrintEx(pttFont, fX, fY,
				useSpriteProps ? pSpriteProps->screen_distance : fZBase,
				fScale, fScale, 0,
				pBlock->pv, UTF8GetLength(pBlock->pv),
				rgba, useSpriteProps ? pSpriteProps : NULL);

			if (pAnchor && pAnchor->bHover)
			{
				gfxDrawLine(fX, fY, fZBase + 0.001, fX + pBlock->pos.iWidth*fRenderScale, fY, colorFromRGBA(lerpRGBAColors(iColorBottom, iColorTop, 0.5)));
				gfxDrawLine(fX, fY + 1, fZBase + 0.001, fX + pBlock->pos.iWidth*fRenderScale, fY + 1, ColorBlack);
			}
		}

		// Reset the font we used.
		StructCopyAll(parse_GfxFont, &rp, pttFont);
	}
	else if(TAG_MATCHES(sm_image))
	{
		SMImage *pimg = (SMImage *)pBlock->pv;
		char *pchTexName = pimg->achTex;
		float fXScale;
		float fYScale;
		AtlasTex *ptex = NULL;
		float scale = ((float)PTR_TO_S32(pattrs->ppScale[eaSize(&pattrs->ppScale)-1])/SMF_FONT_SCALE);
		int iWidth = round(pimg->iWidth * scale);
		int iHeight = round(pimg->iHeight * scale);

		if(pAnchor!=NULL)
		{
			if(pAnchor->bHover)
			{
				if(pimg->iHighlight>0 && bDraw)
				{
					AtlasTex *ptexWhite = white_tex_atlas;
					int iColor = PTR_TO_U32(pattrs->ppLinkHover[eaSize(&pattrs->ppLinkHover)-1]);

					fXScale = ((float)(iWidth+pimg->iHighlight*2)/(float)ptexWhite->width);
					fYScale = ((float)(iHeight+pimg->iHighlight*2)/(float)ptexWhite->height);

					display_sprite(ptexWhite,
						pBlock->pos.iX*fRenderScale + pBlock->pos.iBorder*fRenderScale - pimg->iHighlight*fRenderScale + iXBase,
						pBlock->pos.iY*fRenderScale + pBlock->pos.iBorder*fRenderScale - pimg->iHighlight*fRenderScale + iYBase,
						fZBase,
						fXScale*fRenderScale, fYScale*fRenderScale, ColorRGBAMultiplyAlpha(iColor, chAlpha));
				}

				if(pimg->achTexHover[0]!='\0')
				{
					pchTexName = pimg->achTexHover;
				}
			}
		}

		if (bDraw)
		{
			ptex = atlasLoadTexture(pimg->iSkinOverride && s_imageSkinOverrideFunc ? s_imageSkinOverrideFunc(pchTexName) : pchTexName);
			fXScale = ((float)iWidth/(float)ptex->width);
			fYScale = ((float)iHeight/(float)ptex->height);

			display_sprite(ptex,
				pBlock->pos.iX*fRenderScale + pBlock->pos.iBorder*fRenderScale + iXBase,
				pBlock->pos.iY*fRenderScale + pBlock->pos.iBorder*fRenderScale + iYBase,
				fZBase,
				fXScale*fRenderScale, fYScale*fRenderScale, ColorRGBAMultiplyAlpha(pimg->iColor, chAlpha));
		}
	}
	else
	{
		//printf("");
	}


	PERFINFO_AUTO_STOP();

	if(pBlock->bHasBlocks)
	{
		int i;
		int iSize = eaSize(&pBlock->ppBlocks);
		for(i=0; i<iSize; i++)
		{
			PERFINFO_AUTO_START("smf_Render", 1);
				smf_Render(pBlock->ppBlocks[i], pattrs, iXBase, iYBase, fZBase, chAlpha, fRenderScale, callback, pClip, pSpriteProps);
			PERFINFO_AUTO_STOP();
		}
	}
}


/**********************************************************************func*
 * smf_Navigate
 *
 */
int smf_Navigate(const char *pch)
{
	if (strnicmp(pch, "cmd:", 4) != 0)
	{
		return false;
	}

	pch += 4;
	globCmdParse(pch);

	return true;
}

int smf_Hover(const char *pch, UIGen* pGen)
{
	return true;
}

static void smf_InteractInternal(SMBlock *pBlock, TextAttribs *pattrs, int iXBase, int iYBase, float fRenderScale, int (*callback)(char *pch), int (*hoverCallback)(char *pch, UIGen* pGen), UIGen* pGen)
{
	if((pBlock->pos.iWidth==0 || pBlock->pos.iX<0 || pBlock->pos.iY<0)
			&& pBlock->sType>=kFormatTags_Count*2)
	{
		return;
	}

	if(pBlock->sType == kFormatTags_Anchor)
	{
		SMAnchor *pAnchor = (SMAnchor *)pBlock->pv;
		pAnchor->bHover = false;
		pAnchor->bSelected = false;
	}

	if(pBlock->sType == -1)
	{
		// Top level block
	}
	else if(smf_HandleFormatting(pBlock, pattrs))
	{
		// Should be before the rest.
		// Nothing more to do for these
	}
	else if(TAG_MATCHES(sm_nolink))
	{
		s_bAllowAnchor = false;
	}
	else if(TAG_MATCHES(sm_nolink_end))
	{
		s_bAllowAnchor = true;
	}
	else
	{
		// Although tracked, multiply nested anchors are ignored. Always use
		// the first anchor if available.
		if(eaSize(&pattrs->ppAnchor)>1 && s_bAllowAnchor)
		{
			CBox box;

			BuildCBox(&box,
				pBlock->pos.iX*fRenderScale + iXBase, pBlock->pos.iY*fRenderScale + iYBase,
				pBlock->pos.iWidth*fRenderScale, pBlock->pos.iHeight*fRenderScale);

			if (mouseCollision(&box))
			{
				SMAnchor *pAnchor = (SMAnchor *)pattrs->ppAnchor[1];
				if (mouseUpHit(MS_LEFT, &box) && !pAnchor->bSelected)
				{
					callback(pAnchor->ach);
					pAnchor->bSelected = true;
				}

				if(pAnchor->type == SMAnchorItem && !pAnchor->bHover)
				{
					hoverCallback(pAnchor->ach, pGen);
					pAnchor->bHover = true;
				}
			}
		}
	}

	if(pBlock->bHasBlocks)
	{
		int i;
		int iSize = eaSize(&pBlock->ppBlocks);
		for(i=0; i<iSize; i++)
		{
			smf_InteractInternal(pBlock->ppBlocks[i], pattrs, iXBase, iYBase, fRenderScale, callback, hoverCallback, pGen);
		}
	}
}

TextAttribs *smf_DefaultTextAttribs(void)
{
	static TextAttribs s_taDefaults =
	{
		/* piBold            */  (void *)0,
		/* piItalic          */  (void *)0,
		/* piColorTop        */  (void *)U32_TO_PTR(0xFF),
		/* piColorBottom     */  (void *)U32_TO_PTR(0xFF),
		/* piOutlineColor    */  (void *)U32_TO_PTR(0xFF),
		/* piDropShadowColor */  (void *)U32_TO_PTR(0xFF),
		/* piScale           */  (void *)(int)(1.0f*SMF_FONT_SCALE),
		/* piFace            */  (void *)&g_font_Sans,
		/* piFont            */  (void *)0,
		/* piAnchor          */  (void *)0,
		/* piLink            */  (void *)U32_TO_PTR(0x0000ffff),
		/* piLinkBG          */  (void *)0,
		/* piLinkHover       */  (void *)U32_TO_PTR(0x4444ffff),
		/* piLinkHoverBG     */  (void *)0,
		/* piOutline         */  (void *)0,
		/* piShadow          */  (void *)0,
		/* piSnapToPixels    */  (void *)1,
	};
	return &s_taDefaults;
}

TextAttribs *smf_CloneDefaultTextAttribs(void)
{
	TextAttribs *pAttribs = calloc(1, sizeof(*pAttribs));
	memcpy(pAttribs, smf_DefaultTextAttribs(), sizeof(*pAttribs));
	return pAttribs;
}

TextAttribs *smf_TextAttribsFromFont(TextAttribs *pAttribs, UIStyleFont *pFont)
{
	if (!pAttribs)
		pAttribs = smf_CloneDefaultTextAttribs();
	if (pFont)
	{
		pAttribs->ppBold = U32_TO_PTR(pFont->bBold);
		pAttribs->ppItalic = U32_TO_PTR(pFont->bItalic);
		pAttribs->ppFace = (void *)GET_REF(pFont->hFace);
		pAttribs->ppShadow = U32_TO_PTR(pFont->iShadowOffset);
		pAttribs->ppOutline = U32_TO_PTR(pFont->iOutlineWidth);
		pAttribs->ppOutlineColor = U32_TO_PTR(ui_StyleColorPaletteIndex(pFont->uiOutlineColor));
		if (pFont->uiColor)
			pAttribs->ppColorTop = pAttribs->ppColorBottom = U32_TO_PTR(ui_StyleColorPaletteIndex(pFont->uiColor));
		else
		{
			pAttribs->ppColorTop = U32_TO_PTR(ui_StyleColorPaletteIndex(pFont->uiTopLeftColor));
			pAttribs->ppColorBottom = U32_TO_PTR(ui_StyleColorPaletteIndex(pFont->uiBottomLeftColor));
		}
		pAttribs->ppSnapToPixels = U32_TO_PTR(pFont->bDontSnapToPixels ? 0 : 1);
	}
	else
	{
		memcpy(pAttribs, smf_DefaultTextAttribs(), sizeof(*pAttribs));
	}
	return pAttribs;
}

/**********************************************************************func*
 * InitTextAttribs
 *
 */
TextAttribs *InitTextAttribs(TextAttribs *pattrs, TextAttribs *pdefaults)
{
	if(pdefaults == NULL)
	{
		pdefaults = smf_DefaultTextAttribs();
	}

	if (!pattrs)
		pattrs = calloc(1, sizeof(TextAttribs));
	else
	{
		eaClearFast(&pattrs->ppBold);
		eaClearFast(&pattrs->ppItalic);
		eaClearFast(&pattrs->ppColorTop);
		eaClearFast(&pattrs->ppColorBottom);
		eaClearFast(&pattrs->ppScale);
		eaClearFast(&pattrs->ppFace);
		eaClearFast(&pattrs->ppFont);
		eaClearFast(&pattrs->ppAnchor);
		eaClearFast(&pattrs->ppLink);
		eaClearFast(&pattrs->ppLinkBG);
		eaClearFast(&pattrs->ppLinkHover);
		eaClearFast(&pattrs->ppLinkHoverBG);
		eaClearFast(&pattrs->ppOutline);
		eaClearFast(&pattrs->ppShadow);
		eaClearFast(&pattrs->ppOutlineColor);
		eaClearFast(&pattrs->ppDropShadowColor);
		eaClearFast(&pattrs->ppSnapToPixels);
	}

	// Cram the initial values in. Yes, this is evil.
	eaPush(&pattrs->ppBold,           pdefaults->ppBold);
	eaPush(&pattrs->ppItalic,         pdefaults->ppItalic);
	eaPush(&pattrs->ppColorTop,       pdefaults->ppColorTop);
	eaPush(&pattrs->ppColorBottom,    pdefaults->ppColorBottom);
	eaPush(&pattrs->ppScale,          pdefaults->ppScale);
	eaPush(&pattrs->ppFace,           pdefaults->ppFace);
	eaPush(&pattrs->ppFont,           pdefaults->ppFont);
	eaPush(&pattrs->ppAnchor,         pdefaults->ppAnchor);
	eaPush(&pattrs->ppLink,           pdefaults->ppLink);
	eaPush(&pattrs->ppLinkBG,         pdefaults->ppLinkBG);
	eaPush(&pattrs->ppLinkHover,      pdefaults->ppLinkHover);
	eaPush(&pattrs->ppLinkHoverBG,    pdefaults->ppLinkHoverBG);
	eaPush(&pattrs->ppOutline,        pdefaults->ppOutline);
	eaPush(&pattrs->ppShadow,         pdefaults->ppShadow);
	eaPush(&pattrs->ppOutlineColor,   pdefaults->ppOutlineColor);
	eaPush(&pattrs->ppDropShadowColor,pdefaults->ppDropShadowColor);
	eaPush(&pattrs->ppSnapToPixels,   pdefaults->ppSnapToPixels);

	s_bAllowAnchor = true;
	return pattrs;
}

void smf_Interact(SMFBlock *pBlock, TextAttribs *pattrs, int iXBase, int iYBase, int (*callback)(const char *pch), int (*hoverCallback)(const char *pch, UIGen* pGen), UIGen* pGen)
{
	if (pBlock->pBlock)
	{
		static TextAttribs attrs;
		PERFINFO_AUTO_START("InitTextAttribs", 1);
		InitTextAttribs(&attrs, pattrs);
		PERFINFO_AUTO_STOP_START("smf_Interact", 1);
		if (!callback)
			callback = smf_Navigate;
		//If we don't have our hover callback, give it a dummy
		if(!hoverCallback)
			hoverCallback = smf_Hover;
		smf_InteractInternal(pBlock->pBlock, &attrs, iXBase, iYBase, pBlock->fRenderScale, callback, hoverCallback, pGen);
		PERFINFO_AUTO_STOP();
	}
}

/**********************************************************************func*
 * smf_ParseAndFormat
 *
 */
int smf_ParseAndFormat(SMFBlock *pSMFBlock, const char *pch,
	int x, int y, float z, int w, int h,
	bool bReparse, bool bReformat, bool bSafeOnly, TextAttribs *ptaDefaults)
{
	static TextAttribs attrs;
	bool bNew=false;
	U32 crc = 0;

	if(pSMFBlock==NULL)
	{
		return -1;
	}
	
	PERFINFO_AUTO_START("smf_ParseAndFormat", 1);

		if(w != pSMFBlock->iLastWidth)
		{
			bReformat = true;
		}

		if (ptaDefaults)
		{
			if (PTR_TO_S32(ptaDefaults->ppScale) != pSMFBlock->lastFontScale)
			{
				bReformat = true;
			}
			pSMFBlock->lastFontScale = PTR_TO_S32(ptaDefaults->ppScale);
		}

		if(pSMFBlock->pBlock==NULL)
		{
			bReparse=true;
		}

		PERFINFO_AUTO_START("checkCRC", 1);
		if(pch) // if NULL don't bother checking
		{
			cryptAdler32Init();
			crc = cryptAdler32(pch, strlen(pch));
			crc ^= g_ui_State.uiLastFontLoad;

			if (crc != pSMFBlock->ulCrc)
			{
				bReparse = true;
			}
		}
		PERFINFO_AUTO_STOP();

		if(bReparse && !pSMFBlock->dont_reparse_ever_again)
		{
			pSMFBlock->ulCrc = crc;

			if(pSMFBlock->pBlock!=NULL)
			{
				smf_Destroy(pSMFBlock->pBlock);
			}

			pSMFBlock->pBlock = smf_CreateAndParse(pch, bSafeOnly);
			bReformat=true;
		}

		if(bReformat)
		{
			InitTextAttribs(&attrs, ptaDefaults);

			if(!smf_Format(pSMFBlock->pBlock, &attrs, w, h, pSMFBlock->bScaleToFit || pSMFBlock->bNoWrap))
			{
				//printf("*** Unable to fit!\n");
			}

			if(pSMFBlock->bScaleToFit && pSMFBlock->pBlock)
			{
				pSMFBlock->fRenderScale = (float)w/(float)pSMFBlock->pBlock->pos.iMinWidth;

				if(pSMFBlock->fMaxRenderScale > 0.0f
					&& pSMFBlock->fRenderScale > pSMFBlock->fMaxRenderScale)
				{
					pSMFBlock->fRenderScale = pSMFBlock->fMaxRenderScale;

					// OK, this isn't right but it will work for now.
					// Reset the width to be the whole requested width, because that's
					// what callers expect. Otherwise it'll reflow constantly.
					pSMFBlock->pBlock->pos.iMinWidth = pSMFBlock->pBlock->pos.iWidth = w/pSMFBlock->fRenderScale;
				}
				if(pSMFBlock->fMinRenderScale > 0.0f
					&& pSMFBlock->fRenderScale < pSMFBlock->fMinRenderScale)
				{
					pSMFBlock->fRenderScale = pSMFBlock->fMinRenderScale;

					// OK, this isn't right but it will work for now.
					// Reset the width to be the whole requested width, because that's
					// what callers expect. Otherwise it'll reflow constantly.
					pSMFBlock->pBlock->pos.iMinWidth = pSMFBlock->pBlock->pos.iWidth = w/pSMFBlock->fRenderScale;
				}
			}
			else
			{
				pSMFBlock->fRenderScale = 1.0f;
			}

			pSMFBlock->iLastWidth = w;
		}

	PERFINFO_AUTO_STOP();
	
	return pSMFBlock->pBlock ? (pSMFBlock->pBlock->pos.iHeight) : 0;
}

/**********************************************************************func*
 * smf_ParseAndDisplay
 *
 */
int smf_ParseAndDisplay(SMFBlock *pSMFBlock, const char *pch,
	int x, int y, float z, int w, int h,
	bool bReparse, bool bReformat, bool bSafeOnly, TextAttribs *ptaDefaults, 
	unsigned char chAlpha,
	int (*callback)(char *pch), SpriteProperties *pSpriteProps)
{
	static SMFBlock s_Block = { 0 };
	static TextAttribs attrs;
	float fRenderScale = 1.0f;
	bool bNew=false;

	if(w<0)
		return 0;

	if(pSMFBlock==NULL)
	{
		pSMFBlock = &s_Block;
	}

	smf_ParseAndFormat(pSMFBlock, pch, x, y, z, w, h, bReparse, bReformat, bSafeOnly, ptaDefaults);

	if(pSMFBlock->pBlock)
	{
		PERFINFO_AUTO_START("InitTextAttribs1", 1);
			InitTextAttribs(&attrs, ptaDefaults);
		PERFINFO_AUTO_STOP_START("smf_Render", 1);
			smf_Render(pSMFBlock->pBlock, &attrs, x, y, z, chAlpha, pSMFBlock->fRenderScale, callback, clipperGetCurrentCBox(), pSpriteProps);
		PERFINFO_AUTO_STOP();
		return pSMFBlock->pBlock->pos.iHeight;
	}

	return 0;
}

// smf_Test is replaced by the SMF tab in UITests.c.

void smf_Clear(SMFBlock *pSMFBlock) 
{
	if (pSMFBlock && pSMFBlock->pBlock)
	{
		smf_Destroy(pSMFBlock->pBlock);
	}
	memset(pSMFBlock,0,sizeof(SMFBlock));
}

MP_DEFINE(SMFBlock);
SMFBlock* smfblock_Create( void )
{
	SMFBlock *res = NULL;

	// create the pool, arbitrary number
	MP_CREATE(SMFBlock, 64);
	res = MP_ALLOC( SMFBlock );
	if( verify( res ))
	{
		res->pBlock = sm_CreateBlock();
	}
	return res;
}

void smfblock_Destroy( SMFBlock *hItem )
{
	if(hItem)
	{
		smf_Clear(hItem);
		MP_FREE(SMFBlock, hItem);
	}
}

S32 smfblock_GetWidth(SMFBlock *pBlock)
{
	return (S32)round((pBlock->pBlock ? max(pBlock->pBlock->pos.iWidth, pBlock->pBlock->pos.iMinWidth) : 0)*pBlock->fRenderScale);
	
}

S32 smfblock_GetMinWidth(SMFBlock *pBlock)
{
	return (S32)round((pBlock->pBlock ? min(pBlock->pBlock->pos.iWidth, pBlock->pBlock->pos.iMinWidth) : 0)*pBlock->fRenderScale);
}


S32 smfblock_GetHeight(SMFBlock *pBlock)
{
	return (S32)round((pBlock->pBlock ? pBlock->pBlock->pos.iHeight : 0)*pBlock->fRenderScale);
}

bool smf_GetTime(SMBlock *pBlock, F32 *pfStart, F32 *pfEnd)
{
	if (TAG_MATCHES(sm_time))
	{
		SMTime *pTime = (SMTime *)pBlock->pv;
		*pfStart = pTime->fStart;
		*pfEnd = pTime->fEnd;
		return true;
	}
	else
	{
		*pfStart = 0;
		*pfEnd = 0;
		return false;
	}
}

void smf_TextAttribsSetScale(TextAttribs *pAttribs, F32 fScale)
{
	pAttribs->ppScale = U32_TO_PTR((U32)(fScale * SMF_FONT_SCALE));
}

/* End of File */
