/***************************************************************************
 
 
 
 ***************************************************************************/

#include "earray.h"
#include "textparser.h"
#include "sm_parser.h"
#include "mathutil.h"
#include "StringUtil.h"

#include "GfxTexAtlas.h"
#include "GfxFont.h"

#include "smf_parse.h"
#include "smf_format.h"
#include "timing.h"

#include "AppLocale.h"

#if 0
#define DBG_PRINTF(x) printf x
#else
#define DBG_PRINTF(x)
#endif

#define TAG_MATCHES(y) (smf_aTagDefs[pBlock->sType].id==k_##y)

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

/**********************************************************************func*
 * MakeFont
 *
 */
void smf_MakeFont(GfxFont **pFont, GfxFont* pCopyRenderParams, TextAttribs *pattrs)
{
	// Set the font
	S32 iFace = eaSize(&pattrs->ppFace)-1;
	*pFont = (GfxFont *)eaGet(&pattrs->ppFace, iFace);

	// Remember the original renderparams
	StructCopyAll(parse_GfxFont, *pFont, pCopyRenderParams);

	// Now set each renderparam
	(*pFont)->bold         = eaTail(&pattrs->ppBold) ? true : false;
	(*pFont)->italicize    = eaTail(&pattrs->ppItalic) ? true : false;
	(*pFont)->snapToPixels = eaTail(&pattrs->ppSnapToPixels) ? true : false;

	(*pFont)->outlineWidth = PTR_TO_U32(eaTail(&pattrs->ppOutline));
	(*pFont)->outline = (*pFont)->outlineWidth > 0;

	(*pFont)->dropShadowOffset[0] = (*pFont)->dropShadowOffset[1] = PTR_TO_U32(eaTail(&pattrs->ppShadow));
	(*pFont)->dropShadow = (*pFont)->dropShadowOffset[0] ? true : false;

	(*pFont)->color.uiTopLeftColor = (*pFont)->color.uiTopRightColor = PTR_TO_U32(eaTail(&pattrs->ppColorTop));
	(*pFont)->color.uiBottomLeftColor = (*pFont)->color.uiBottomRightColor = PTR_TO_U32(eaTail(&pattrs->ppColorBottom));

	(*pFont)->uiOutlineColor = PTR_TO_U32(eaTail(&pattrs->ppOutlineColor));
	(*pFont)->uiDropShadowColor = PTR_TO_U32(eaTail(&pattrs->ppDropShadowColor));
	(*pFont)->softShadow = 0;
	(*pFont)->softShadowSpread = 2;
}


/**********************************************************************func*
 * smf_PushAttrib
 *
 */
void smf_PushAttrib(SMBlock *pBlock, TextAttribs *pattr)
{
	void ***ppp = (void ***)pattr;

	if(ppp!=NULL
		&& pBlock->sType>=0
		&& pBlock->sType<kFormatTags_Count)
	{
		eaPush(&ppp[pBlock->sType], pBlock->pv);
	}
}

/**********************************************************************func*
 * smf_PopAttrib
 *
 */
void *smf_PopAttrib(SMBlock *pBlock, TextAttribs *pattr)
{
	int idx = pBlock->sType - kFormatTags_Count;

	return smf_PopAttribInt(idx, pattr);
}

/**********************************************************************func*
 * smf_PopAttribInt
 *
 */
void *smf_PopAttribInt(int idx, TextAttribs *pattr)
{
	void ***ppp = (void ***)pattr;

	if(ppp!=NULL
		&& idx>=0
		&& idx<kFormatTags_Count
		&& eaSize(&ppp[idx])>1)
	{
		return eaPop(&ppp[idx]);
	}

	return NULL;
}

/**********************************************************************func*
 * smf_HandleFormatting
 *
 */
bool smf_HandleFormatting(SMBlock *pBlock, TextAttribs *pattrs)
{
	if(pBlock->sType == kFormatTags_Font+kFormatTags_Count)
	{
		SMFont *pFont = smf_PopAttrib(pBlock, pattrs);

		if (pFont)
		{
			if(pFont->bColorTop)
				smf_PopAttribInt(kFormatTags_ColorTop, pattrs);
			if(pFont->bColorBottom)
				smf_PopAttribInt(kFormatTags_ColorBottom, pattrs);
			if(pFont->bFace)
				smf_PopAttribInt(kFormatTags_Face, pattrs);
			if(pFont->bShadow)
				smf_PopAttribInt(kFormatTags_Shadow, pattrs);
			if(pFont->bOutline)
				smf_PopAttribInt(kFormatTags_Outline, pattrs);
			if(pFont->bDropShadowColor)
				smf_PopAttribInt(kFormatTags_DropShadowColor, pattrs);
			if(pFont->bOutlineColor)
				smf_PopAttribInt(kFormatTags_OutlineColor, pattrs);
			if(pFont->bScale)
				smf_PopAttribInt(kFormatTags_Scale, pattrs);
			if(pFont->bItalic)
				smf_PopAttribInt(kFormatTags_Italic, pattrs);
			if(pFont->bBold)
				smf_PopAttribInt(kFormatTags_Bold, pattrs);
			if(pFont->bSnapToPixels)
				smf_PopAttribInt(kFormatTags_SnapToPixels, pattrs);
		}
	}
	else if(pBlock>=0 && pBlock->sType<kFormatTags_Count)
	{
		smf_PushAttrib(pBlock, pattrs);
	}
	else if(pBlock->sType>=kFormatTags_Count && pBlock->sType<kFormatTags_Count*2)
	{
		smf_PopAttrib(pBlock, pattrs);
	}
	else
	{
		return false;
	}

	return true;
}

/**********************************************************************func*
 * FontSize
 *
 */
static void FontSize(GfxFont *pttFont, float fScale, char *pch, S16 *piWidth, S16 *piHeight)
{
	float fXScreenScale = 1.0f;
	float fYScreenScale = 1.0f;
	
	Vec2 fontDims;
	float oldSize = pttFont->renderSize;
	pttFont->renderSize *= fScale;
	gfxFontMeasureString(pttFont, pch, fontDims);
	pttFont->renderSize = oldSize;

	*piHeight = fontDims[1];
	*piWidth = fontDims[0];

	*piHeight = (S16)(*piHeight/fYScreenScale);
	*piWidth = (S16)(*piWidth/fXScreenScale);
	if (*piHeight < 18 && (getCurrentLocale() == 3)) {
		*piHeight = 18;
	}
}

/**********************************************************************func*
 * CalcTextSize
 *
 */
void CalcTextSize(SMBlock *pBlock, TextAttribs *pattrs)
{
	GfxFont *pttFont;
	GfxFont rp;
	float fScale;

	// Set the font up
	smf_MakeFont(&pttFont, &rp, pattrs);

	fScale = ((float)PTR_TO_U32(pattrs->ppScale[eaSize(&pattrs->ppScale)-1])/SMF_FONT_SCALE);

	FontSize(pttFont, fScale,
		(char *)pBlock->pv,
		&pBlock->pos.iMinWidth, &pBlock->pos.iMinHeight);

	// Reset the font we used.
	StructCopyAll(parse_GfxFont,  &rp, pttFont);
}

/**********************************************************************func*
 * CalcSpaceSize
 *
 */
void CalcSpaceSize(SMBlock *pBlock, TextAttribs *pattrs)
{
	GfxFont *pttFont;
	GfxFont rp;
	float fScale;

	// Set the font up
	smf_MakeFont(&pttFont, &rp, pattrs);

	fScale = ((float)PTR_TO_S32(pattrs->ppScale[eaSize(&pattrs->ppScale)-1])/SMF_FONT_SCALE);

	FontSize(pttFont, fScale," ", &pBlock->pos.iMinWidth, &pBlock->pos.iMinHeight);

	// Reset the font we used.
	StructCopyAll(parse_GfxFont,  &rp, pttFont);
}

/**********************************************************************func*
 * GetMinimumSize
 *
 */
static void GetMinimumSize(SMBlock *pBlock, TextAttribs *pattrs)
{
	int iChildMaxWidth = 0;
	int iChildMaxHeight = 0;
	int iChildSumWidth = 0;
	int iChildSumHeight = 0;

	if(pBlock->bHasBlocks)
	{
		int i;
		int iSize = eaSize(&pBlock->ppBlocks);
		for(i=0; i<iSize; i++)
		{
			GetMinimumSize(pBlock->ppBlocks[i], pattrs);

			if(iChildMaxWidth < pBlock->ppBlocks[i]->pos.iMinWidth)
			{
				iChildMaxWidth = pBlock->ppBlocks[i]->pos.iMinWidth;
			}
			if(iChildMaxHeight < pBlock->ppBlocks[i]->pos.iMinHeight)
			{
				iChildMaxHeight = pBlock->ppBlocks[i]->pos.iMinHeight;
			}

			iChildSumWidth += pBlock->ppBlocks[i]->pos.iMinWidth;
			iChildSumHeight += pBlock->ppBlocks[i]->pos.iMinHeight;
		}
	}

	if(smf_HandleFormatting(pBlock, pattrs))
	{
		// Must be first
		// Nothing more to do
	}
	else if(TAG_MATCHES(sm_text))
	{
		CalcTextSize(pBlock, pattrs);
	}
	else if(TAG_MATCHES(sm_ws))
	{
		CalcSpaceSize(pBlock, pattrs);
	}
	else if(TAG_MATCHES(sm_br))
	{
		CalcSpaceSize(pBlock, pattrs);
		pBlock->pos.iMinWidth = 0;
	}
	else if(TAG_MATCHES(sm_table) || TAG_MATCHES(sm_ul))
	{
		pBlock->pos.iMinWidth = iChildMaxWidth + pBlock->pos.iBorder*2;
		pBlock->pos.iMinHeight = iChildSumHeight + pBlock->pos.iBorder*2;
	}
	else if(TAG_MATCHES(sm_tr))
	{
		pBlock->pos.iMinWidth = iChildSumWidth + pBlock->pos.iBorder*2;
		pBlock->pos.iMinHeight = iChildMaxHeight + pBlock->pos.iBorder*2;
	}
	else if(TAG_MATCHES(sm_image))
	{
		SMImage *pimg = (SMImage *)pBlock->pv;
		int iWidth = 0;
		int iHeight = 0;
		AtlasTex *ptex;
		float scale = ((float)PTR_TO_S32(pattrs->ppScale[eaSize(&pattrs->ppScale)-1])/SMF_FONT_SCALE);

		if((ptex = atlasLoadTexture(pimg->achTex)) != NULL)
		{
			iWidth = ptex->width;
			iHeight = ptex->height;
		}

		if(pimg->iWidth <= 0)
		{
			pimg->iWidth = iWidth;
		}
		if(pimg->iHeight <= 0)
		{
			pimg->iHeight = iHeight;
		}

		pBlock->pos.iMinWidth = round(pimg->iWidth * scale) + pBlock->pos.iBorder*2;
		pBlock->pos.iMinHeight = round(pimg->iHeight * scale) + pBlock->pos.iBorder*2;
	}
	else if(TAG_MATCHES(sm_td))
	{
		SMColumn *pcol = (SMColumn *)pBlock->pv;
		if(pcol->bFixedWidth)
		{
			pBlock->pos.iMinWidth = max(pcol->iWidthRequested, iChildSumWidth) + pBlock->pos.iBorder*2;
		}
		else
		{
			pBlock->pos.iMinWidth = iChildSumWidth + pBlock->pos.iBorder*2;
		}

		pBlock->pos.iMinHeight = iChildMaxHeight + pBlock->pos.iBorder*2;
	}
	else if(TAG_MATCHES(sm_span) || TAG_MATCHES(sm_time))
	{
		pBlock->pos.iMinWidth = iChildMaxWidth + pBlock->pos.iBorder*2;
		pBlock->pos.iMinHeight = iChildMaxHeight + pBlock->pos.iBorder*2;
	}
	else if(pBlock->sType == -1) // Top level block
	{
		pBlock->pos.iMinWidth = iChildMaxWidth;
		pBlock->pos.iMinHeight = iChildMaxHeight;
	}
	else
	{
		pBlock->pos.iMinWidth = 0;
		pBlock->pos.iMinHeight = 0;
	}
}

/**********************************************************************func*
 * GetRequestedWidth
 *
 */
static void GetRequestedWidth(SMBlock *pBlock, int iWidth)
{
	int iChildMaxWidth = 0;
	int iChildSumWidth = 0;
	int i;

	// Most blocks can't request a width, they demand a certain minimum
	// width instead. This function deals with mainly with tables and their
	// size requests.

	// When this function is done, pos.iWidth will be set to the width
	// request as a percentage of the whole width the element is allowed
	// to take. (Non-table elements will be 0, since they have no request.)

	if(pBlock->bHasBlocks)
	{
		for(i=eaSize(&pBlock->ppBlocks)-1; i>=0; i--)
		{
			GetRequestedWidth(pBlock->ppBlocks[i], iWidth);

			if(iChildMaxWidth < pBlock->ppBlocks[i]->pos.iWidth)
			{
				iChildMaxWidth = pBlock->ppBlocks[i]->pos.iWidth;
			}

			iChildSumWidth += pBlock->ppBlocks[i]->pos.iWidth;
		}
	}

	if(TAG_MATCHES(sm_table) || TAG_MATCHES(sm_ul))
	{
		int aiWidths[SMF_MAX_COLS];
		int iSize;

		// OK, by the time we got here, we've dealt with all of our children.

		memset(aiWidths, -1, sizeof(aiWidths));

		// Find the requested width for each column in the table.
		iSize = eaSize(&pBlock->ppBlocks);
		for(i=0; i<iSize; i++)
		{
			if(smf_aTagDefs[pBlock->ppBlocks[i]->sType].id == k_sm_tr)
			{
				int j;
				int iCnt=0;
				int iSize2 = eaSize(&pBlock->ppBlocks[i]->ppBlocks);

				for(j=0; j<iSize2; j++)
				{
					SMBlock *pTd = pBlock->ppBlocks[i]->ppBlocks[j];

					if(smf_aTagDefs[pTd->sType].id == k_sm_td && iCnt<SMF_MAX_COLS)
					{
						if(aiWidths[iCnt]<pTd->pos.iWidth)
						{
							aiWidths[iCnt] = pTd->pos.iWidth;
						}
						iCnt++;
					}
				}
			}
		}

		iChildSumWidth = 0;
		for(i=0; aiWidths[i]>=0 && i<SMF_MAX_COLS; i++)
		{
			iChildSumWidth += aiWidths[i];
		}

		// OK, now propagate the widths back down the tree
		for(i=0; i<iSize; i++)
		{
			if(smf_aTagDefs[pBlock->ppBlocks[i]->sType].id == k_sm_tr)
			{
				int j;
				int iCnt=0;
				int iSize2 = eaSize(&pBlock->ppBlocks[i]->ppBlocks);

				pBlock->ppBlocks[i]->pos.iWidth = iChildSumWidth;

				for(j=0; j<iSize2 && iCnt<SMF_MAX_COLS; j++)
				{
					if(smf_aTagDefs[pBlock->ppBlocks[i]->sType].id == k_sm_td)
					{
						pBlock->ppBlocks[i]->ppBlocks[j]->pos.iWidth = aiWidths[iCnt];
					}
					iCnt++;
				}
			}
		}

		pBlock->pos.iWidth = iChildSumWidth;
	}
	else if(TAG_MATCHES(sm_tr))
	{
		pBlock->pos.iWidth = iChildSumWidth;
	}
	else if(TAG_MATCHES(sm_td))
	{
		if(((SMColumn *)pBlock->pv)->bFixedWidth)
		{
			pBlock->pos.iWidth  = 0;
		}
		else
		{
			pBlock->pos.iWidth = (((SMColumn *)pBlock->pv)->iWidthRequested);
		}
	}
	else if(pBlock->sType == -1) // top level block
	{
		pBlock->pos.iWidth = iChildSumWidth;
	}
	else
	{
		// Non-table blocks have no width at this point.
		pBlock->pos.iWidth  = 0;
		pBlock->pos.iHeight = 0;
	}
}

/**********************************************************************func*
 * CalcSplitText
 *
 */
static void SplitText(SMBlock ***peaBlocks, int iIndex, int *piBlockCount, int iToWidth, TextAttribs *pattrs)
{
	// Minimum text length to generate, to prevent absurdly small areas from destroying performance.
	const unsigned int MIN_TEXT_LENGTH = 4;

	SMBlock *pBlock = eaGet(peaBlocks, iIndex);
	SMBlock *pSplit;
	GfxFont *pttFont;
	GfxFont rp;
	Vec2 size;

	if (!pBlock || smf_aTagDefs[pBlock->sType].id != k_sm_text || iToWidth <= 0)
		return;

	// Set the font up
	smf_MakeFont(&pttFont, &rp, pattrs);
	pttFont->renderSize *= ((float)PTR_TO_S32(pattrs->ppScale[eaSize(&pattrs->ppScale)-1])/SMF_FONT_SCALE);

	do
	{
		char *pv = (char *)pBlock->pv;
		unsigned int len = strlen(pv);
		unsigned int j, iLen, iGlyphs;
		unsigned int iBestLen = -1;

		if (len <= MIN_TEXT_LENGTH)
			break;

		size[0] = iToWidth;
		size[1] = pBlock->pos.iMinHeight;
		iGlyphs = gfxFontCountGlyphsInArea(pttFont, pv, size);
		iGlyphs = MAX(iGlyphs, MIN_TEXT_LENGTH);
		iLen = 0;
		for (j = 0; j < iGlyphs && pv[iLen]; j++)
		{
			bool splitHere = iLen > 0 && ispunct((unsigned char)pv[iLen]);
			iLen += UTF8GetCodepointLength((unsigned char *)pv + iLen);
			if (splitHere)
				iBestLen = iLen;
		}

		if (iLen >= len)
			break;

		// Very dumb word wrapping, split after punctuation characters (since code points > 126 are probably going to be letters with text decorators).
		if (iBestLen < iLen)
			iLen = iBestLen;

		pSplit = sm_CreateBlock();
		pSplit->bFreeOnDestroy = true;
		pSplit->pos = pBlock->pos;
		pSplit->pParent = pBlock->pParent;
		pSplit->sType = pBlock->sType;
		pSplit->pv = strdup((char *)pBlock->pv + iLen);
		pv[iLen] = '\0';

		gfxFontMeasureString(pttFont, (char *)pBlock->pv, size);
		pSplit->pos.iWidth = pSplit->pos.iMinWidth = pBlock->pos.iWidth - (S16)size[0];
		pSplit->pos.iMinHeight = (S16)size[1];
		pBlock->pos.iWidth = (S16)size[0];

		eaInsert(peaBlocks, pSplit, ++iIndex);
		(*piBlockCount)++;
		pBlock = pSplit;
	}
	while (pBlock->pos.iWidth > iToWidth);

	// Reset the font we used.
	StructCopyAll(parse_GfxFont, &rp, pttFont);
}

/**********************************************************************func*
 * CalcWidths
 *
 */
static bool CalcWidths(SMBlock *pBlock, int iWidth, TextAttribs *pattrs)
{
	bool bRet = true;
	int iChildMaxWidth = 0;
	int iChildSumWidth = 0;
	int k;

	// Previous to this function, you should call GetRequestedWidth.

	// This function reconciles width requests and minimum required size
	// for all the elements. Therefore, for most elements, the width
	// calculated is the same as the minimum width. Tables, though, have
	// to go through a bunch of rigmarole.

	// The width requests from tables don't guarantee that the enclosed
	// element will fit. If the request is too small, it is made large
	// enough to fit. Also, respecting a width request for one column might
	// make it impossible to fit another column. This (and other cases)
	// are handled as best as possible.

	// It is possible for CalcWidths to be unable to reconcile the constraints
	// given. If this occurs, it returns false.

	iWidth -= pBlock->pos.iBorder*2;

	if(pBlock->bHasBlocks)
	{
		int iNumBlocks = eaSize(&pBlock->ppBlocks);
		for(k=0; k<iNumBlocks; k++)
		{
			int i;

			// Previously we've went bottom up to find the minimum and
			// requested sizes. Now we'll go top-down and finalize them.

			if(smf_aTagDefs[pBlock->ppBlocks[k]->sType].id == k_sm_table
				|| smf_aTagDefs[pBlock->ppBlocks[k]->sType].id == k_sm_ul)
			{
				SMBlock *pTable = pBlock->ppBlocks[k];
				float afPortions[SMF_MAX_COLS];
				int iCnt;
				int iCntMin;
				int aiMinWidths[SMF_MAX_COLS];
				int aiWidths[SMF_MAX_COLS];
				int iSum;
				int iDisperse;
				int iNeeded;
				int iRowBorder = 0;
				int iSize;
				int tempWidth = iWidth;

				DBG_PRINTF(("Fit the table in %d\n", iWidth));

				memset(aiMinWidths, -1, sizeof(aiMinWidths));
				memset(aiWidths, -1, sizeof(aiWidths));

				// TODO: If it seems like we just did all of this, we did. The info
				// just wasn't stored anywhere.
				iSize = eaSize(&pTable->ppBlocks);
				for(i=0; i<iSize; i++)
				{
					if(smf_aTagDefs[pTable->ppBlocks[i]->sType].id == k_sm_tr)
					{
						int j;
						int iSize2 = eaSize(&pTable->ppBlocks[i]->ppBlocks);
						if(pTable->ppBlocks[i]->pos.iBorder>iRowBorder)
						{
							iRowBorder = pTable->ppBlocks[i]->pos.iBorder;
						}

						for(iCnt=0, j=0; j<iSize2; j++)
						{
							SMBlock *pTd = pTable->ppBlocks[i]->ppBlocks[j];

							if(smf_aTagDefs[pTd->sType].id == k_sm_td && iCnt<SMF_MAX_COLS)
							{
								if(aiWidths[iCnt]<pTd->pos.iWidth)
								{
									aiWidths[iCnt] = pTd->pos.iWidth;
								}

								if(aiMinWidths[iCnt]<pTd->pos.iMinWidth)
								{
									aiMinWidths[iCnt] = pTd->pos.iMinWidth+1;
								}

								iCnt++;
							}
						}
					}
				}
				// TODO: End of repetition

				// Shrink the available width by the biggest row border;
				tempWidth -= iRowBorder*2;

				// Get the requested percentages.
				for(iCnt=0, i=0; aiWidths[i]>=0 && i<SMF_MAX_COLS; i++)
				{
					afPortions[i] = (float)aiWidths[i]/100.0f;
					iCnt++;
					DBG_PRINTF(("portion[%d] = %f\n", i, afPortions[i]));
				}

				// If a column didn't request a size, then it will have a
				// portion == zero at this point.

				DBG_PRINTF(("%d columns\n", iCnt));

				DBG_PRINTF(("first pass\n"));

				// Do a first pass at sizes.
				// This will give sizes to columns which have a requested
				// width ("stretchy columns").  Ones without a request
				// ("fixed columns") will be fixed up later.
				for(iSum=0, i=0; i<iCnt; i++)
				{
					if(afPortions[i]>0)
					{
						aiWidths[i] = ceilf(afPortions[i]*tempWidth);
						DBG_PRINTF(("size[%d] = %d\n", i, aiWidths[i]));

						// If the requested size is too small, calc an alternative
						// size equal to the min. If this one fails further down,
						// it's OK.
						if(!tempWidth)
						{
							afPortions[i] = 0;
							aiWidths[i] = 0;
						}
						else if(aiWidths[i] < aiMinWidths[i])
						{
							afPortions[i] = ((float)aiMinWidths[i]+0.5)/(float)tempWidth;
							aiWidths[i] = ceilf(afPortions[i]*tempWidth);
							DBG_PRINTF(("was too small updated size[%d] = %d\n", i, aiWidths[i]));
						}

						iSum += aiWidths[i];
					}
				}

				// Now check for minimums.
				// If a column had no requested size or if the requested size
				// was too small, we handle it here by allocating the
				// minimum width needed.

				// The total amount needed for all the fixed columns
				iNeeded = 0;

				// Count of fixed columns.
				iCntMin = 0;

				for(i=0; i<iCnt; i++)
				{
					if(aiWidths[i]<aiMinWidths[i])
					{
						iNeeded += aiMinWidths[i];
						aiWidths[i] = aiMinWidths[i];
						DBG_PRINTF(("col[%d] needs %d.\n", i, aiWidths[i]));

						// If the request was too small to support, ignore
						// the request. Make the stretchy column into a
						// fixed column.
						afPortions[i] = 0;
						iCntMin++;
					}
				}
				DBG_PRINTF(("Need %d (there is %d left)\n", iNeeded, tempWidth-iSum));

				DBG_PRINTF(("second pass\n"));

				// Do a second pass at sizes.
				// This time, the columns with a requested size are only
				// allocated from what's left over from the minimum needs
				// of the fixed columns.

				// This is the amount each stretchy column needs to give back
				// to the fixed ones so they can fit.
				DBG_PRINTF(("%d - %d - %d = %d/%d to give back\n", tempWidth, iSum, iNeeded, tempWidth-iSum-iNeeded, iCnt-iCntMin));
				if(iCnt!=iCntMin)
				{
					// The magic -1 on the next line is used to make sure we
					// actually get enough-- fractional sizes aren't allowed.
					iDisperse = (tempWidth-iSum-iNeeded)/(iCnt-iCntMin)-1;
					if(iDisperse>0)
					{
						// If there is extra space, then we don't need to give
						// any up.
						iDisperse=0;
					}
				}
				else
				{
					iDisperse=0;
				}

				// We need to do this even if we aren't taking stuff back
				// since some of the previous stretchy columns might now
				// be fixed columns. We need to update iSum.
				for(iSum=0, i=0; i<iCnt; i++)
				{
					if(afPortions[i]!=0)
					{
						aiWidths[i] += iDisperse;
						iSum += aiWidths[i];
						DBG_PRINTF(("size[%d] = %d\n", i, aiWidths[i]));
					}
				}

				DBG_PRINTF(("Totalling %d\n", iSum));

				// The fixed columns get whatever is left over split
				// between them equally.
				DBG_PRINTF(("%d - %d - %d = %d/%d to give out\n", tempWidth, iSum, iNeeded, tempWidth-iSum-iNeeded, iCntMin));
				if(iCntMin>0)
				{
					iDisperse = (tempWidth-iSum-iNeeded)/iCntMin;
				}
				else
				{
					iDisperse = 0;
				}

				// Again, always do this to update iSum.
				for(iSum=0, i=0; i<iCnt; i++)
				{
					if(afPortions[i]==0)
					{
						aiWidths[i] += iDisperse;
						DBG_PRINTF(("size[%d] = %d\n", i, aiWidths[i]));
					}

					iSum += aiWidths[i];
				}

				// Spread any excess (due to fractions) over the columns
				for(i=0; i<iCnt && iSum<tempWidth; i++)
				{
					aiWidths[i]++;
					iSum++;
					DBG_PRINTF(("fixup size[%d] = %d\n", i, aiWidths[i]));
				}

				pTable->pos.iWidth = iSum+iRowBorder*2;

				// Ta daaa! All the columns now have a reasonable width.

				// Of course, if the incoming data was bad (like they asked
				// for > 100% of the space to be split up, or there wasn't
				// enough space to fit all the columns) then the overall
				// width may be larger then the original.
				//
				// Because we control the input (and this isn't for an
				// error-tolerant web-browser), this code doesn't try to
				// handle this gracefully. It does flag the problem, though.

				if(iSum>tempWidth)
				{
					DBG_PRINTF(("**** Data doesn't fit into given width!\n"));
					bRet = false;
				}
				for(i=0; i<iCnt; i++)
				{
					if(aiWidths[i]<aiMinWidths[i])
					{
						DBG_PRINTF(("**** Column %d doesn't have enough space!\n", i));
						bRet = false;
					}
				}

				// OK, now propagate the widths back down the tree.
				// Have them determine their widths as well (if they need to).
				for(i=0; i<iSize; i++)
				{
					if(smf_aTagDefs[pTable->ppBlocks[i]->sType].id == k_sm_tr)
					{
						int j;
						int iCnt2=0;
						int iSize2 = eaSize(&pTable->ppBlocks[i]->ppBlocks);

						pTable->ppBlocks[i]->pos.iWidth = tempWidth+iRowBorder*2;

						for(j=0; j<iSize2; j++)
						{
							if(smf_aTagDefs[pTable->ppBlocks[i]->ppBlocks[j]->sType].id == k_sm_td)
							{
								pTable->ppBlocks[i]->ppBlocks[j]->pos.iWidth = (int)aiWidths[iCnt2];
								bRet = CalcWidths(pTable->ppBlocks[i]->ppBlocks[j], (int)aiWidths[iCnt2], pattrs) && bRet;
								if (smf_aTagDefs[pTable->ppBlocks[i]->ppBlocks[j]->sType].id == k_sm_text
									&& pTable->ppBlocks[i]->ppBlocks[j]->pos.iWidth > (int)aiWidths[iCnt2])
								{
									SplitText(&pTable->ppBlocks[i]->ppBlocks, j, &iSize2, (int)aiWidths[iCnt2], pattrs);
								}
								iCnt2++;
							}
							else
							{
								// Well-formed smf shouldn't get here.
								smf_HandleFormatting(pTable->ppBlocks[i]->ppBlocks[j], pattrs);
								pTable->ppBlocks[i]->ppBlocks[j]->pos.iWidth = 0;
								pTable->ppBlocks[i]->ppBlocks[j]->pos.iMinWidth = 0;
								pTable->ppBlocks[i]->ppBlocks[j]->pos.iHeight = 0;
								pTable->ppBlocks[i]->ppBlocks[j]->pos.iMinHeight = 0;
								DBG_PRINTF(("** Extra out-of-band stuff in <tr> definition.\n"));
							}
						}
					}
					else
					{
						// Well-formed smf shouldn't get here.
						smf_HandleFormatting(pTable->ppBlocks[i], pattrs);
						pTable->ppBlocks[i]->pos.iWidth = 0;
						pTable->ppBlocks[i]->pos.iMinWidth = 0;
						pTable->ppBlocks[i]->pos.iHeight = 0;
						pTable->ppBlocks[i]->pos.iMinHeight = 0;
						DBG_PRINTF(("** Extra out-of-band stuff in <table> definition.\n"));
					}
				}
			}
			else
			{
				if(pBlock->ppBlocks[k]->bHasBlocks)
				{
					pBlock->ppBlocks[k]->pos.iWidth = iWidth;
					bRet = CalcWidths(pBlock->ppBlocks[k], iWidth, pattrs) && bRet;
				}
				else
				{
					pBlock->ppBlocks[k]->pos.iWidth = pBlock->ppBlocks[k]->pos.iMinWidth;
					smf_HandleFormatting(pBlock->ppBlocks[k], pattrs);
				}
				if (smf_aTagDefs[pBlock->ppBlocks[k]->sType].id == k_sm_text
					&& pBlock->ppBlocks[k]->pos.iWidth > iWidth)
				{
					SplitText(&pBlock->ppBlocks, k, &iNumBlocks, iWidth, pattrs);
				}
			}
		}
	}
	else
	{
		smf_HandleFormatting(pBlock, pattrs);
	}

	return bRet;
}

static int * gpiLeftBuffer=NULL;
static int * gpiRightBuffer=NULL;
// not the total buffer used - this is for recursion.  The actual earray will have more elements than this, but this
// many will belong to the last caller
static int giBufferWaterMark=-1;

static int increaseBorderBuffer(int **ppiLeft, int **ppiRight, int leftv, int rightv,int iNewMinSize)
{
	int oldsize = eaiSize(&gpiLeftBuffer);
	int iNewSize = giBufferWaterMark+iNewMinSize;
	int i;
	int * pLocal;
	const int iNewElems = iNewSize-oldsize;
	eaiSetSizeFast(&gpiLeftBuffer,iNewSize);
	eaiSetSizeFast(&gpiRightBuffer,iNewSize);
	*ppiLeft = gpiLeftBuffer+giBufferWaterMark;
	*ppiRight = gpiRightBuffer+giBufferWaterMark;

	if (iNewElems > 0)
	{
		// doing the loops like this makes the compiler smart enough to do rep stores.  128-bit moves could be considered.
		pLocal = &gpiLeftBuffer[oldsize];
		i = iNewElems;
		while (i)
		{
			*pLocal = leftv;
			pLocal++;
			i--;
		}

		pLocal = &gpiRightBuffer[oldsize];
		i = iNewElems;
		while (i)
		{
			*pLocal = rightv;
			pLocal++;
			i--;
		}
	}

	return iNewSize-giBufferWaterMark;
}

static int getBorderBuffers(int **ppiLeft, int **ppiRight, int leftv, int rightv,int iSize)
{
	int iResult;
	int iOldWaterMark = giBufferWaterMark;

	if (gpiLeftBuffer == NULL)
	{
		eaiSetCapacity(&gpiLeftBuffer,1000);
		eaiSetCapacity(&gpiRightBuffer,1000);
	}

	giBufferWaterMark = eaiSize(&gpiLeftBuffer);

	iResult = increaseBorderBuffer(ppiLeft,ppiRight,leftv,rightv,iSize);
	devassert(iResult == iSize);

	return iOldWaterMark;
}

static void freeBorderBuffers(int iWaterMark,int **ppiNewLeft,int **ppiNewRight)
{
	eaiSetSize(&gpiLeftBuffer,giBufferWaterMark);
	eaiSetSize(&gpiRightBuffer,giBufferWaterMark);
	giBufferWaterMark = iWaterMark;

	if (ppiNewLeft)
	{
		*ppiNewLeft = gpiLeftBuffer+giBufferWaterMark;
		*ppiNewRight = gpiRightBuffer+giBufferWaterMark;
	}
}

/**********************************************************************func*
 * Float
 *
 */
static int Float(SMBlock *pBlock, SMAlignment eAlign, int iX, int iY, int **ppiLeft, int **ppiRight)
{
	int k;
	int iMax;
	int iMin;
	int iBorderBufferSize = eaiSizeSlow(ppiLeft);

	// Find the max left value over the vertical span. This is
	// where the element will begin.
	//
	// (Now that I think about it, there isn't any way that any line after
	// this one can be any thinner since we do layout top to bottom and we
	// don't support anything which could do something out of order. I guess
	// this doesn't cause any harm, though.)
	iMax=0;
	iMin=(*ppiRight)[iY];

	if (pBlock->pos.iHeight+iY >= iBorderBufferSize)
	{
		// initialize the buffer to the width of the block we are trying to place this block in.
		iBorderBufferSize = increaseBorderBuffer(ppiLeft, ppiRight, pBlock->pParent->pos.iBorder, pBlock->pParent->pos.iWidth-pBlock->pParent->pos.iBorder,pBlock->pos.iHeight+iY+1);
	}

	for(k=0; k<pBlock->pos.iHeight; k++)
	{
		if(iMax<(*ppiLeft)[iY+k])
		{
			iMax = (*ppiLeft)[iY+k];
		}
		if((*ppiRight)[iY+k]<iMin)
		{
			iMin = (*ppiRight)[iY+k];
		}
	}

	DBG_PRINTF(("  max margin from %d to %d = %d\n", iY, iY+k-1, iMax));
	DBG_PRINTF(("  min margin from %d to %d = %d\n", iY, iY+k-1, iMin));

	pBlock->pos.iX = iMax;
	pBlock->pos.iY = iY;

	if(iMin<iMax+pBlock->pos.iWidth)
	{
		// Uck. It doesn't fit here.
		// A real formatter would deal with this in a graceful fashion.
		// This code isn't going to.
		//
		// Instead, it's not going to update the left margin at all.
		DBG_PRINTF(("  Doesn't fit!!\n"));
	}
	else
	{
		int *piSide;
		int iMargin;

		// OK, push the margin out from the max we found by the
		// width of the element.
		if(eAlign==kAlignment_Right)
		{
			piSide = *ppiRight;
			iMargin = iMin-pBlock->pos.iWidth;
			pBlock->pos.iX = iMargin;
			DBG_PRINTF(("  right align\n"));
		}
		else
		{
			piSide = *ppiLeft;
			iMargin = iMax+pBlock->pos.iWidth;
		}
		DBG_PRINTF(("  %d %d\n", eAlign, kAlignment_Right));

		for(k=0; k<pBlock->pos.iHeight; k++)
		{
			// this should be impossible, because we checked for this above, and then called this [RMARR - 3/19/12]
			devassert(k+iY < iBorderBufferSize);
			/*if (k+iY >= iBorderBufferSize)
			{
				iBorderBufferSize = increaseBorderBuffer(ppiLeft, ppiRight, pBlock->pos.iBorder, pBlock->pos.iWidth-pBlock->pos.iBorder,k+iY+1);
			}*/
			piSide[iY+k] = iMargin;
		}
		DBG_PRINTF(("  set new margin to %d\n", iMargin));
	}

	return iBorderBufferSize;
}

static bool isWrappableType(SMFTags eType)
{
	switch (eType)
	{
	case k_sm_ws:
	case k_sm_br:
	case k_sm_bsp:
	case k_sm_table:
	case k_sm_ul:
	case k_sm_ul_end:
	case k_sm_tr:
	case k_sm_p:
	case k_sm_p_end:
	case k_sm_span:
	case k_sm_span_end:
	case k_sm_time:
	case k_sm_time_end:
	case k_sm_image:
	case k_sm_li:
	case k_sm_li_end:
		return true;
	}

	return false;
}

/**********************************************************************func*
 * CalcHeights
 *
 */

static void CalcHeights(SMBlock *pBlock,int **ppiParentLeft,int **ppiParentRight)
{
	int iChildSumWidth = 0;
	int iChildMaxHeight = 0;
	int iX;
	int iY;
	int iLastLeft;
	int iLastRight;
	int i;
	SMBlock **ppFloatsLeft = NULL;
	SMBlock **ppFloatsRight = NULL;
	int iBorderBufferSize = -1;

	// These are NOT earrays.  They are part of a different earray
	int *piLeft = 0;
	int *piRight = 0;

	int iWaterMark;

	// Previous to this function, you should call GetRequestedWidth and
	// CalcWidths.

	// This function takes the widths calculated and does layout (word
	// wrap, mainly) of all the blocks. When this function is complete,
	// each block will have it's final width and height as well as an
	// X and Y location with respect to its enclosing block's origin.
	// You can call CalcLocations after this function to make the X and Y
	// values absolute instead.

	// Handing this function a block which failed CalcWidths will give
	// undefined results.

	iX = iLastLeft = iLastRight = pBlock->pos.iBorder;
	iY = pBlock->pos.iBorder;

	iBorderBufferSize = max(50,iY);

	iWaterMark = getBorderBuffers(&piLeft,&piRight,pBlock->pos.iBorder,pBlock->pos.iWidth-pBlock->pos.iBorder,iBorderBufferSize);

	if(pBlock->bHasBlocks)
	{
		int j;
		int iFirstBlockOnLine = 0;
		int iNumBlocks = eaSize(&pBlock->ppBlocks);

		DBG_PRINTF(("Container: %s (%s): loc(%d, %d) size(%dx%d) border(%d)\n",
			pBlock->sType>=0 ? smf_aTagDefs[pBlock->sType].pchName : "root",
			pBlock->bFreeOnDestroy&&pBlock->pv ? pBlock->pv : "",
			pBlock->pos.iX,
			pBlock->pos.iY,
			pBlock->pos.iWidth,
			pBlock->pos.iHeight,
			pBlock->pos.iBorder));


		for(i=0; i<iNumBlocks; i++)
		{
			bool bFloating = false;
			int k;
			int iCombinedWidth;

			CalcHeights(pBlock->ppBlocks[i],&piLeft,&piRight);

			// handle floating images
			if(smf_aTagDefs[pBlock->ppBlocks[i]->sType].id == k_sm_image)
			{
				if(pBlock->ppBlocks[i]->pos.alignHoriz==kAlignment_Left
					|| pBlock->ppBlocks[i]->pos.alignHoriz==kAlignment_Right)
				{
					bFloating = true;
				}
			}

			// Find the width of the next piece of text
			iCombinedWidth = 0;
			for(k=i; k<iNumBlocks; k++)
			{
				if (k > i && isWrappableType(smf_aTagDefs[pBlock->ppBlocks[k]->sType].id))
					break;
				iCombinedWidth += pBlock->ppBlocks[k]->pos.iWidth;
			}

			if(iChildSumWidth+iCombinedWidth > piRight[iY]-piLeft[iY]
				|| bFloating
				|| smf_aTagDefs[pBlock->ppBlocks[i]->sType].id == k_sm_table
				|| smf_aTagDefs[pBlock->ppBlocks[i]->sType].id == k_sm_ul
				|| smf_aTagDefs[pBlock->ppBlocks[i]->sType].id == k_sm_tr
				|| smf_aTagDefs[pBlock->ppBlocks[i]->sType].id == k_sm_p
				|| smf_aTagDefs[pBlock->ppBlocks[i]->sType].id == k_sm_p_end
				|| smf_aTagDefs[pBlock->ppBlocks[i]->sType].id == k_sm_span
				|| smf_aTagDefs[pBlock->ppBlocks[i]->sType].id == k_sm_span_end
				|| smf_aTagDefs[pBlock->ppBlocks[i]->sType].id == k_sm_time
				|| smf_aTagDefs[pBlock->ppBlocks[i]->sType].id == k_sm_time_end
				|| smf_aTagDefs[pBlock->ppBlocks[i]->sType].id == k_sm_br
				)
			{
				int iCntVisibleBlocks = 0;
				int iCurCnt = 0;

				DBG_PRINTF(("  Wrapping: %d > %d-%d\n", iChildSumWidth+pBlock->ppBlocks[i]->pos.iWidth, piRight[iY], piLeft[iY]));

				// Make the size of a space at the end of the line zero
				if(iFirstBlockOnLine<=(i-1)
					&& smf_aTagDefs[pBlock->ppBlocks[i-1]->sType].id == k_sm_ws)
				{
					iX -= pBlock->ppBlocks[i-1]->pos.iWidth;
					pBlock->ppBlocks[i-1]->pos.iWidth = 0;
					pBlock->ppBlocks[i-1]->pos.iHeight = 0;
				}

				// Figure out how many items are on the line so we can justify
				// the text properly.
				if(pBlock->pos.alignHoriz == kAlignment_Both)
				{
					if(smf_aTagDefs[pBlock->ppBlocks[i]->sType].id == k_sm_br)
					{
						piRight[iY] = iX;
					}
					else
					{
						for(j=iFirstBlockOnLine; j<i; j++)
						{
							if(pBlock->ppBlocks[j]->pos.iWidth>0)
							{
								iCntVisibleBlocks++;
							}
						}
					}
				}

				// Promote everything on this line so that they all have the
				// same baseline.
				for(j=iFirstBlockOnLine; j<i; j++)
				{
					DBG_PRINTF(("    Height fixup: %d (%s = %s)\n", iChildMaxHeight, smf_aTagDefs[pBlock->ppBlocks[j]->sType].pchName, pBlock->ppBlocks[j]->bFreeOnDestroy&&pBlock->ppBlocks[j]->pv ? pBlock->ppBlocks[j]->pv : ""));
					pBlock->ppBlocks[j]->pos.iHeight = iChildMaxHeight;

					if(pBlock->pos.alignHoriz == kAlignment_Right)
					{
						pBlock->ppBlocks[j]->pos.iX += piRight[iY]-iX;
					}
					else if(pBlock->pos.alignHoriz == kAlignment_Center)
					{
						pBlock->ppBlocks[j]->pos.iX += (piRight[iY]-iX)/2;
					}
					else if(pBlock->pos.alignHoriz == kAlignment_Both && piRight[iY] != iX)
					{
						if(pBlock->ppBlocks[j]->pos.iWidth>0 && iCntVisibleBlocks > 1)
						{
							pBlock->ppBlocks[j]->pos.iX += ceilf(iCurCnt*(piRight[iY]-iX)/(float)(iCntVisibleBlocks-1));
							iCurCnt++;
						}
					}
				}

				// Move the cursor down.
				iY += iChildMaxHeight;

				if (iY >= iBorderBufferSize)
				{
					iBorderBufferSize = increaseBorderBuffer(&piLeft, &piRight, pBlock->pos.iBorder, pBlock->pos.iWidth-pBlock->pos.iBorder,iY+1);
				}

				///////
				//
				// START of special cases for particular tags.
				//
				// These really should be done more parametrically, but I don't
				// think I'm at the limit for that yet.
				//

				if(smf_aTagDefs[pBlock->ppBlocks[i]->sType].id == k_sm_table
					|| smf_aTagDefs[pBlock->ppBlocks[i]->sType].id == k_sm_ul)
				{
					// tables are always the full width, so they need to "clear left"
					while(piLeft[iY]!=pBlock->pos.iBorder)
					{						
						iY++;
						if (iY >= iBorderBufferSize)
						{
							iBorderBufferSize = increaseBorderBuffer(&piLeft, &piRight, pBlock->pos.iBorder, pBlock->pos.iWidth-pBlock->pos.iBorder,iY+1);
						}
					}
				}
				else if(smf_aTagDefs[pBlock->ppBlocks[i]->sType].id == k_sm_p_end)
				{
					// Paragraphs have a bit of space after them
					iY += 10;
				}

				//
				// END of special cases for tags
				//
				///////

				if(bFloating)
				{
					iBorderBufferSize = Float(pBlock->ppBlocks[i], pBlock->ppBlocks[i]->pos.alignHoriz, iX, iY, &piLeft, &piRight);
					if(pBlock->ppBlocks[i]->pos.alignHoriz==kAlignment_Right)
					{
						eaPush(&ppFloatsRight, pBlock->ppBlocks[i]);
					}
					else
					{
						eaPush(&ppFloatsLeft, pBlock->ppBlocks[i]);
					}

					// Reset word wrap for the new line
					iFirstBlockOnLine = i+1;
				}
				else
				{
					// Reset word wrap for the new line
					iFirstBlockOnLine = i;
				}
				iChildSumWidth = 0;
				iChildMaxHeight = 0;

				if (iY >= iBorderBufferSize)
				{
					iBorderBufferSize = increaseBorderBuffer(&piLeft, &piRight, pBlock->pos.iBorder, pBlock->pos.iWidth-pBlock->pos.iBorder,iY+1);
				}

				// Move the cursor to the left-most point available on this
				// line.			
				{
					int j2;
					int iYShift = iY;

					iX = piLeft[iY];

					// OK, now we need to do some floating fixup

					if(piRight[iY]>iLastRight)
					{
						while(iYShift>=0 && piRight[iYShift]>iLastRight)
						{
							iYShift--;
						}
						// We need to move the floater on this line down to
						// fill the space.
						j2=eaSize(&ppFloatsRight);
						while(j2>0)
						{
							j2--;
							if( 6*ppFloatsRight[j2]->pos.iHeight < (iY-iYShift+1) && ppFloatsRight[j2]->pos.iHeight+ppFloatsRight[j2]->pos.iY-1 == iYShift)
							{
								ppFloatsRight[j2]->pos.iY += (iY-iYShift+1)/2;
							}
						}
					}

					if(piLeft[iY]<iLastLeft)
					{
						while(iYShift>=0 && piLeft[iYShift]<iLastLeft)
						{
							iYShift--;
						}
						// We need to move the floater on this line down to
						// fill the space.
						j2=eaSize(&ppFloatsLeft);
						while(j2>0)
						{
							j2--;
							if( 6*ppFloatsLeft[j2]->pos.iHeight < (iY-iYShift+1) && ppFloatsLeft[j2]->pos.iHeight+ppFloatsLeft[j2]->pos.iY-1 == iYShift)
							{
								ppFloatsLeft[j2]->pos.iY += (iY-iYShift+1)/2;
							}
						}
					}

					iLastRight = piRight[iY];
					iLastLeft = piLeft[iY];
				}
				DBG_PRINTF(("  start line at %d\n", iX));
			}

			if(!bFloating)
			{
				// This keeps whitespace from indenting at the beginning
				// of the line.
				if(smf_aTagDefs[pBlock->ppBlocks[i]->sType].id == k_sm_ws
					&& iX==piLeft[iY])
				{
					pBlock->ppBlocks[i]->pos.iWidth = 0;
					pBlock->ppBlocks[i]->pos.iHeight = 0;
				}

				pBlock->ppBlocks[i]->pos.iX = iX;
				pBlock->ppBlocks[i]->pos.iY = iY;

				iX += pBlock->ppBlocks[i]->pos.iWidth;
				iChildSumWidth += pBlock->ppBlocks[i]->pos.iWidth;

				if(iChildMaxHeight < pBlock->ppBlocks[i]->pos.iHeight)
				{
					iChildMaxHeight = pBlock->ppBlocks[i]->pos.iHeight;
				}
			}

			if(smf_aTagDefs[pBlock->ppBlocks[i]->sType].id != k_sm_ws)
			{
				DBG_PRINTF(("Final: %s (%s): loc(%d, %d) size(%dx%d)\n",
					pBlock->ppBlocks[i]->sType<0 ? "root" : smf_aTagDefs[pBlock->ppBlocks[i]->sType].pchName,
					pBlock->ppBlocks[i]->bFreeOnDestroy&&pBlock->ppBlocks[i]->pv ? pBlock->ppBlocks[i]->pv : "",
					pBlock->ppBlocks[i]->pos.iX,
					pBlock->ppBlocks[i]->pos.iY,
					pBlock->ppBlocks[i]->pos.iWidth,
					pBlock->ppBlocks[i]->pos.iHeight));
			}
		}

		// Clean up block on last line
		for(j=iFirstBlockOnLine; j<i; j++)
		{
			DBG_PRINTF(("    Height fixup: %d (%s = %s)\n",
				iChildMaxHeight,
				pBlock->ppBlocks[j]->sType<0 ? "root" : smf_aTagDefs[pBlock->ppBlocks[j]->sType].pchName,
				pBlock->ppBlocks[j]->bFreeOnDestroy&&pBlock->ppBlocks[j]->pv ? pBlock->ppBlocks[j]->pv : ""));

			pBlock->ppBlocks[j]->pos.iHeight = iChildMaxHeight;

			if(pBlock->pos.alignHoriz == kAlignment_Right)
			{
				pBlock->ppBlocks[j]->pos.iX += piRight[iY]-iX;
			}
			else if(pBlock->pos.alignHoriz == kAlignment_Center)
			{
				pBlock->ppBlocks[j]->pos.iX += (piRight[iY]-iX)/2;
			}
		}
		// And move the height to include the last line.
		iY += iChildMaxHeight;

		if (iY >= iBorderBufferSize)
		{
			iBorderBufferSize = increaseBorderBuffer(&piLeft, &piRight, pBlock->pos.iBorder, pBlock->pos.iWidth-pBlock->pos.iBorder,iY+1);
		}

		// Make sure the height can fit anything floating as well.
		for(i=iBorderBufferSize-1; i>=0; i--)
		{
			if(piLeft[i] != pBlock->pos.iBorder || piRight[i] != pBlock->pos.iWidth-pBlock->pos.iBorder)
			{
				break;
			}
		}
		if(i>iY)
		{
			iY = i + 1;
		}
	}

	iY += pBlock->pos.iBorder;

	if (iY >= iBorderBufferSize)
	{
		iBorderBufferSize = increaseBorderBuffer(&piLeft, &piRight, pBlock->pos.iBorder, pBlock->pos.iWidth-pBlock->pos.iBorder,iY+1);
	}

	if(pBlock->sType == -1)
	{
		if(piLeft[i] != pBlock->pos.iBorder || piRight[i] != pBlock->pos.iWidth-pBlock->pos.iBorder)
		{
			iY++;
			if (iY >= iBorderBufferSize)
			{
				iBorderBufferSize = increaseBorderBuffer(&piLeft, &piRight, pBlock->pos.iBorder, pBlock->pos.iWidth-pBlock->pos.iBorder,iY+1);
			}
		}
		pBlock->pos.iHeight = iY;
	}
	else if(TAG_MATCHES(sm_table) || TAG_MATCHES(sm_ul))
	{
		pBlock->pos.iHeight = iY;
	}
	else if(TAG_MATCHES(sm_tr))
	{
		pBlock->pos.iHeight = iY;
	}
	else if(TAG_MATCHES(sm_td))
	{
		pBlock->pos.iHeight = iY;
	}
	else if(TAG_MATCHES(sm_span))
	{
		pBlock->pos.iHeight = iY;
	}
	else if(TAG_MATCHES(sm_text))
	{
		pBlock->pos.iHeight = pBlock->pos.iMinHeight;
	}
	else if(TAG_MATCHES(sm_br))
	{
		pBlock->pos.iHeight = pBlock->pos.iMinHeight;
	}
	else if(TAG_MATCHES(sm_ws))
	{
		pBlock->pos.iHeight = pBlock->pos.iMinHeight;
	}
	else if(TAG_MATCHES(sm_image))
	{
		pBlock->pos.iHeight = pBlock->pos.iMinHeight;
	}
	else if(TAG_MATCHES(sm_time))
	{
		pBlock->pos.iHeight = iY;
	}
	else
	{
		pBlock->pos.iHeight = 0;
	}

	eaDestroy(&ppFloatsLeft);
	eaDestroy(&ppFloatsRight);

	freeBorderBuffers(iWaterMark,ppiParentLeft,ppiParentRight);
}

static void CalcHeightsNoWrap(SMBlock *pBlock)
{
	int iChildMaxHeight = 0;
	int iX;
	int iY;
	int i;
	int iLastLeft;
	int iLastRight;

	// Previous to this function, you should call GetRequestedWidth and
	// CalcWidths.

	// This function takes the widths calculated and does a horizontal
	// layout of all the blocks. When this function is complete,
	// each block will have it's final width and height as well as an
	// X and Y location with respect to its enclosing block's origin.
	// You can call CalcLocations after this function to make the X and Y
	// values absolute instead.

	// Handing this function a block which failed CalcWidths will give
	// undefined results.

	iX = iLastLeft = pBlock->pos.iBorder;
	iY = iLastRight = pBlock->pos.iBorder;

	if(pBlock->bHasBlocks)
	{
		int iFirstBlockOnLine = 0;
		int iNumBlocks = eaSize(&pBlock->ppBlocks);

		DBG_PRINTF(("Container: %s (%s): loc(%d, %d) size(%dx%d) border(%d)\n",
			pBlock->sType>=0 ? smf_aTagDefs[pBlock->sType].pchName : "root",
			pBlock->bFreeOnDestroy&&pBlock->pv ? pBlock->pv : "",
			pBlock->pos.iX,
			pBlock->pos.iY,
			pBlock->pos.iWidth,
			pBlock->pos.iHeight,
			pBlock->pos.iBorder));

		for(i=0; i<iNumBlocks; i++)
		{
			CalcHeightsNoWrap(pBlock->ppBlocks[i]);

			pBlock->ppBlocks[i]->pos.iX = iX;
			pBlock->ppBlocks[i]->pos.iY = iY;

			iX += pBlock->ppBlocks[i]->pos.iWidth;

			if(iChildMaxHeight < pBlock->ppBlocks[i]->pos.iHeight)
			{
				iChildMaxHeight = pBlock->ppBlocks[i]->pos.iHeight;
			}

			if(smf_aTagDefs[pBlock->ppBlocks[i]->sType].id != k_sm_ws)
			{
				DBG_PRINTF(("Final: %s (%s): loc(%d, %d) size(%dx%d)\n",
					pBlock->ppBlocks[i]->sType<0 ? "root" : smf_aTagDefs[pBlock->ppBlocks[i]->sType].pchName,
					pBlock->ppBlocks[i]->bFreeOnDestroy&&pBlock->ppBlocks[i]->pv ? pBlock->ppBlocks[i]->pv : "",
					pBlock->ppBlocks[i]->pos.iX,
					pBlock->ppBlocks[i]->pos.iY,
					pBlock->ppBlocks[i]->pos.iWidth,
					pBlock->ppBlocks[i]->pos.iHeight));
			}
			DBG_PRINTF(("iX = %d\n", iX));
		}

		// And move the height to include the last line.
		iY += iChildMaxHeight;
	}

	iX += pBlock->pos.iBorder;
	iY += pBlock->pos.iBorder;

	if(pBlock->sType == -1)
	{
		pBlock->pos.iHeight = iY;
		pBlock->pos.iWidth = iX;
	}
	else if(TAG_MATCHES(sm_table) || TAG_MATCHES(sm_ul))
	{
		pBlock->pos.iHeight = iY;
		pBlock->pos.iWidth = iX;
	}
	else if(TAG_MATCHES(sm_tr))
	{
		pBlock->pos.iHeight = iY;
		pBlock->pos.iWidth = iX;
	}
	else if(TAG_MATCHES(sm_td))
	{
		pBlock->pos.iHeight = iY;
		pBlock->pos.iWidth = iX;
	}
	else if(TAG_MATCHES(sm_span))
	{
		pBlock->pos.iHeight = iY;
		pBlock->pos.iWidth = iX;
	}
	else if(TAG_MATCHES(sm_text))
	{
		pBlock->pos.iHeight = pBlock->pos.iMinHeight;
	}
	else if(TAG_MATCHES(sm_br))
	{
		pBlock->pos.iHeight = pBlock->pos.iMinHeight;
	}
	else if(TAG_MATCHES(sm_ws))
	{
		pBlock->pos.iHeight = pBlock->pos.iMinHeight;
	}
	else if(TAG_MATCHES(sm_image))
	{
		pBlock->pos.iHeight = pBlock->pos.iMinHeight;
	}
	else if(TAG_MATCHES(sm_time))
	{
		pBlock->pos.iHeight = iY;
	}
	else
	{
		pBlock->pos.iHeight = 0;
	}
}


/**********************************************************************func*
 * CalcAlignment
 *
 */
static void CalcAlignment(SMBlock *pBlock)
{
	int i;

	if(pBlock->bHasBlocks)
	{
		int iMaxHeight = 0;

		if(pBlock->pos.alignVert!=kAlignment_None)
		{
			for(i=eaSize(&pBlock->ppBlocks)-1; i>=0; i--)
			{
				if(pBlock->ppBlocks[i]->pos.iHeight+pBlock->ppBlocks[i]->pos.iY > iMaxHeight)
				{
					iMaxHeight = pBlock->ppBlocks[i]->pos.iHeight+pBlock->ppBlocks[i]->pos.iY;
				}
			}

			// Do vertical alignment.
			// It's already top aligned by default.
			if(iMaxHeight < pBlock->pos.iHeight)
			{
				int iShift = 0;
				if(pBlock->pos.alignVert == kAlignment_Center)
				{
					iShift = (pBlock->pos.iHeight-iMaxHeight)/2;
				}
				else if(pBlock->pos.alignVert == kAlignment_Bottom)
				{
					iShift = pBlock->pos.iHeight-iMaxHeight;
				}

				for(i=eaSize(&pBlock->ppBlocks)-1; i>=0; i--)
				{
					pBlock->ppBlocks[i]->pos.iY += iShift;
				}
			}

		}

		for(i=eaSize(&pBlock->ppBlocks)-1; i>=0; i--)
		{
			CalcAlignment(pBlock->ppBlocks[i]);
		}
	}
}

/**********************************************************************func*
 * CollapseColumns
 *
 */
static void CollapseColumns(SMBlock *pBlock)
{
	// This function looks for tables which have extra space in their columns
	// and collapses that space away. It changes the widths of the rows
	// and columns of the table, as well as the table width itself.

	// It does not update the width of blocks which contain the table.

	if(pBlock->bHasBlocks)
	{
		int i, j, k;

		for(i=eaSize(&pBlock->ppBlocks)-1; i>=0; i--)
		{
			CollapseColumns(pBlock->ppBlocks[i]);
		}

		if((TAG_MATCHES(sm_table)||TAG_MATCHES(sm_ul)) && ((SMTable *)pBlock->pv)->eCollapse != kAlignment_None)
		{
			int iRowWidth;
			SMAlignment eCollapse = ((SMTable *)pBlock->pv)->eCollapse;
			int aiLeftmost[SMF_MAX_COLS];
			int aiRightmost[SMF_MAX_COLS];

			for(i=0;i<SMF_MAX_COLS;i++) { aiLeftmost[i] = INT_MAX; aiRightmost[i] = INT_MIN; }

			for(i=eaSize(&pBlock->ppBlocks)-1; i>=0; i--)
			{
				SMBlock *pTr = pBlock->ppBlocks[i];
				if(smf_aTagDefs[pTr->sType].id == k_sm_tr)
				{
					int iSize = eaSize(&pTr->ppBlocks);
					int iCol = 0;
					for(j=0; j<iSize && iCol<SMF_MAX_COLS; j++)
					{
						SMBlock *pTd = pTr->ppBlocks[j];
						if(smf_aTagDefs[pTd->sType].id == k_sm_td)
						{
							for(k=eaSize(&pTd->ppBlocks)-1; k>=0; k--)
							{
								MAX1(aiRightmost[iCol], pTd->ppBlocks[k]->pos.iX + pTd->ppBlocks[k]->pos.iWidth + pTd->pos.iBorder);
								MIN1(aiLeftmost[iCol], pTd->ppBlocks[k]->pos.iX - pTd->pos.iBorder);
							}

							if(aiRightmost[iCol] > pTd->pos.iWidth || aiLeftmost[iCol] < 0)
							{
								// This column didn't fit, just give up.
								return;
							}

							iCol++;
						}
					}
				}
			}

			// OK, we now have the leftmost and rightmost block locations
			// for each column. Fix them up.
			for(i=eaSize(&pBlock->ppBlocks)-1; i>=0; i--)
			{
				SMBlock *pTr = pBlock->ppBlocks[i];
				if(smf_aTagDefs[pTr->sType].id == k_sm_tr)
				{
					int iSize = eaSize(&pTr->ppBlocks);
					int iCol = 0;
					iRowWidth = pTr->pos.iBorder;

					for(j=0; j<iSize && iCol<SMF_MAX_COLS; j++)
					{
						SMBlock *pTd = pTr->ppBlocks[j];
						if(smf_aTagDefs[pTd->sType].id == k_sm_td)
						{
							if(aiLeftmost[iCol]==INT_MAX) aiLeftmost[iCol] = 0;
							if(aiRightmost[iCol]==INT_MIN) aiRightmost[iCol] = 2*pTd->pos.iBorder;

							if(eCollapse == kAlignment_Left || eCollapse == kAlignment_Both)
							{
								// Move all the contained blocks to the left
								for(k=eaSize(&pTd->ppBlocks)-1; k>=0; k--)
								{
									pTd->ppBlocks[k]->pos.iX -= aiLeftmost[iCol];
								}
							}

							// Shift the TD block left.
							pTd->pos.iX = iRowWidth;

							// Fix up the width of the TD block
							if(eCollapse == kAlignment_Left)
							{
								pTd->pos.iWidth -= aiLeftmost[iCol];
							}
							else if(eCollapse == kAlignment_Right)
							{
								pTd->pos.iWidth = aiRightmost[iCol];
							}
							else if(eCollapse == kAlignment_Both)
							{ 
								pTd->pos.iWidth = aiRightmost[iCol] - aiLeftmost[iCol];
							}

							iCol++;
						}

						iRowWidth += pTd->pos.iWidth;
					}

					pTr->pos.iWidth = iRowWidth + pTr->pos.iBorder;
				}
			}
			// Since all rows should be the same, we should be able to safely
			// use the last iRowWidth
			pBlock->pos.iWidth = iRowWidth + 2*pBlock->pos.iBorder;
		}
		else if(pBlock->sType == -1)
		{
			int iMaxWidth = 0;

			// OK, this is the root block, update its width to be the max of
			// all its children.
			for(i=eaSize(&pBlock->ppBlocks)-1; i>=0; i--)
			{
				int w = pBlock->ppBlocks[i]->pos.iX + pBlock->ppBlocks[i]->pos.iWidth;
				MAX1(iMaxWidth, w);
			}

			pBlock->pos.iMinWidth = iMaxWidth;
		}
	}
}


/**********************************************************************func*
 * CalcLocations
 *
 */
static void CalcLocations(SMBlock *pBlock)
{
	int i;

	// This function makes all of the X/Y locations absolute. (Or at least
	// absolute with respect to the X/Y value specified in the top level
	// block.

	if(pBlock->bHasBlocks)
	{
		for(i=eaSize(&pBlock->ppBlocks)-1; i>=0; i--)
		{
			pBlock->ppBlocks[i]->pos.iX += pBlock->pos.iX;
			pBlock->ppBlocks[i]->pos.iY += pBlock->pos.iY;

			CalcLocations(pBlock->ppBlocks[i]);
		}
	}
}

/**********************************************************************func*
 * Format
 *
 */
bool smf_Format(SMBlock *pBlock, TextAttribs *pattrs, int iWidth, int iHeight, bool bNoWrap)
{
	bool bRet;

	// This formatter is constricted mainly by width. Width is considered
	// the limiter since there is no horizontal scrolling. Height can be
	// varied, and (frankly) isn't even paid attention to.

	// The order of the functions (and assignment) below are important.
	// Don't re-order them. In fact, you should probably never call these
	// function directly. The Format() function should be called instead.
	// Heck, I'm even going to make them static so you can't call them.

	// If there were problems getting the block to fit in the given
	// region, then false is returned.

	PERFINFO_AUTO_START("smf_Format", 1);
		PERFINFO_AUTO_START("GetMinimumSize", 1);

			GetMinimumSize(pBlock, pattrs);

		PERFINFO_AUTO_STOP_START("GetRequestedWidth", 1);

			GetRequestedWidth(pBlock, iWidth);

		PERFINFO_AUTO_STOP_START("CalcWidths", 1);

			pBlock->pos.iWidth = iWidth;
			bRet = CalcWidths(pBlock, iWidth, pattrs);

		PERFINFO_AUTO_STOP_START("CalcHeights", 1);
	
			if(bNoWrap)
			{
				CalcHeightsNoWrap(pBlock);
			}
			else
			{
				CalcHeights(pBlock,NULL,NULL);
			}
			//sm_BlockDump(pBlock, 0, smf_aTagDefs);

		PERFINFO_AUTO_STOP_START("CollapseColumns", 1);

			CollapseColumns(pBlock);

		PERFINFO_AUTO_STOP_START("CalcAlignment", 1);

			CalcAlignment(pBlock);

		PERFINFO_AUTO_STOP_START("CalcLocations", 1);

			CalcLocations(pBlock);

		PERFINFO_AUTO_STOP();

		bRet = bRet && (pBlock->pos.iHeight<=iHeight);
	PERFINFO_AUTO_STOP();
	
	return bRet;
}

/* End of File */
