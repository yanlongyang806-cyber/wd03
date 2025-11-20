/***************************************************************************
 
 
 
 ***************************************************************************/

#include <wininclude.h>
#include "earray.h"
#include "sm_parser.h"

#include "GfxSpriteText.h"
#include "smf_parse.h"
#include "smf_format.h"
#include "timing.h"
#include "UICore.h"
#include "UIStyle.h"

#include "StringUtil.h"

static int s_iTagWS = -1;
static int s_iTagText = -1;

extern int g_preventLinks;

typedef struct
{
	char *pcSpecialCharStr;
	char cReplace;
} SMFSpecialCharReplace;

static SMFSpecialCharReplace s_SpecialCharTable[] = {
	{"nbsp;", ' '},
	{"lt;", '<'},
	{"gt;", '>'},
	{"amp;", '&'},
	{"apos;", '\''},
	{"quot;", '"'},
	{"lcb;", '{'},
	{"rcb;", '}'},
};

void smf_EntityReplace(char* str);

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

/**********************************************************************func*
 * TextPush
 *
 */
SMBlock *TextPush(SMBlock *pBlock, const char *pch, int iLen)
{
	SMBlock *pnew = NULL;
	int i = 0;

	if(iLen<1)
	{
		return NULL;
	}

	// Split the text string by whitespace.
	// Any whitespace is considered a word break and nothing more.
	while(i<iLen)
	{
		int iStart = i;

		while((pch[i]==' ' || pch[i]=='\r' || pch[i]=='\n' || pch[i]=='\t')
			&& i<iLen)
		{
			i++;
		}

		// Whitespace isn't perfect. A series of tags with intervening spaces
		// and no text will make width calculations inaccurate. It shouldn't
		// be too bad, though.
		if(i!=iStart)
		{
			// Only insert a whitespace if the previous block is a text
			// block and not in the no-man's-land inside a table but not in
			// a tr or td.
			int iLast = eaSize(&pBlock->ppBlocks)-1;
			if(iLast>=0
				&& pBlock->ppBlocks[iLast]->sType!=s_iTagWS
				&& smf_aTagDefs[pBlock->ppBlocks[iLast]->sType].id!=k_sm_bsp
				&& smf_aTagDefs[pBlock->ppBlocks[iLast]->sType].id!=k_sm_br
				&& smf_aTagDefs[pBlock->ppBlocks[iLast]->sType].id!=k_sm_tr
				&& smf_aTagDefs[pBlock->ppBlocks[iLast]->sType].id!=k_sm_td)
			{
				pnew = sm_AppendNewBlock(pBlock);
				pnew->sType = s_iTagWS;
			}
		}

		iStart=i;

		while(pch[i]!=' ' && pch[i]!='\r' && pch[i]!='\n' && pch[i]!='\t'
			&& i<iLen)
		{
			i++;
		}

		if(i-iStart>0)
		{
			pnew = sm_AppendNewBlockAndData(pBlock, i-iStart+1);
			pnew->sType = s_iTagText;
			strncpy_s(pnew->pv, i-iStart+1, pch+iStart, i-iStart);

			smf_EntityReplace(pnew->pv);
		}
	}

	return pnew;
}

/**********************************************************************func*
 * sm_ws
 *
 */
SMBlock *sm_ws(SMBlock *pBlock, int iTag, TupleSS aParams[SM_MAX_PARAMS])
{
	return pBlock;
}

/**********************************************************************func*
 * sm_bsp
 *
 */
SMBlock *sm_bsp(SMBlock *pBlock, int iTag, TupleSS aParams[SM_MAX_PARAMS])
{
	SMBlock *pnew = sm_AppendNewBlockAndData(pBlock, 2);
	pnew->sType = s_iTagText;
	*((char *)pnew->pv) = ' ';

	return pBlock;
}

/**********************************************************************func*
 * sm_br
 *
 */
SMBlock *sm_br(SMBlock *pBlock, int iTag, TupleSS aParams[SM_MAX_PARAMS])
{
	SMBlock *pnew = sm_AppendNewBlock(pBlock);
	pnew->sType = iTag;

	return pBlock;
}

/**********************************************************************func*
 * sm_toggle_end
 *
 */
SMBlock *sm_toggle_end(SMBlock *pBlock, int iTag, TupleSS aParams[SM_MAX_PARAMS])
{
	SMBlock *pnew = sm_AppendNewBlock(pBlock);
	pnew->sType = iTag;

	return pBlock;
}

/**********************************************************************func*
* sm_toggle_link_end
*
*/
SMBlock *sm_toggle_link_end(SMBlock *pBlock, int iTag, TupleSS aParams[SM_MAX_PARAMS])
{
	if (g_preventLinks)
		return NULL;
	else
		return sm_toggle_end(pBlock, iTag, aParams);
}

/**********************************************************************func*
 * sm_b
 *
 */
SMBlock *sm_b(SMBlock *pBlock, int iTag, TupleSS aParams[SM_MAX_PARAMS])
{
	SMBlock *pnew = sm_AppendNewBlock(pBlock);
	pnew->sType = iTag;
	pnew->pv = (void *)1;

	return pBlock;
}

/**********************************************************************func*
 * sm_i
 *
 */
SMBlock *sm_i(SMBlock *pBlock, int iTag, TupleSS aParams[SM_MAX_PARAMS])
{
	SMBlock *pnew = sm_AppendNewBlock(pBlock);
	pnew->sType = iTag;
	pnew->pv = (void *)1;

	return pBlock;
}

/**********************************************************************func*
 * sm_color
 *
 */
SMBlock *sm_color(SMBlock *pBlock, int iTag, TupleSS aParams[SM_MAX_PARAMS])
{
	SMBlock *pnew = sm_AppendNewBlock(pBlock);
	pnew->sType = iTag;
	pnew->pv = U32_TO_PTR((sm_GetColor("color", aParams)));

	return pBlock;
}

/**********************************************************************func*
 * sm_outlinecolor
 *
 */
SMBlock *sm_outlinecolor(SMBlock *pBlock, int iTag, TupleSS aParams[SM_MAX_PARAMS])
{
	SMBlock *pnew = sm_AppendNewBlock(pBlock);
	pnew->sType = iTag;
	pnew->pv = U32_TO_PTR((sm_GetColor("outlinecolor", aParams)));

	return pBlock;
}

/**********************************************************************func*
 * sm_dropshadowcolor
 *
 */
SMBlock *sm_dropshadowcolor(SMBlock *pBlock, int iTag, TupleSS aParams[SM_MAX_PARAMS])
{
	SMBlock *pnew = sm_AppendNewBlock(pBlock);
	pnew->sType = iTag;
	pnew->pv = U32_TO_PTR((sm_GetColor("dropshadowcolor", aParams)));

	return pBlock;
}

/**********************************************************************func*
 * sm_colorboth
 *
 */
SMBlock *sm_colorboth(SMBlock *pBlock, int iTag, TupleSS aParams[SM_MAX_PARAMS])
{
	sm_color(pBlock, kFormatTags_ColorTop, aParams);
	sm_color(pBlock, kFormatTags_ColorBottom, aParams);

	return pBlock;
}

/**********************************************************************func*
 * sm_colorboth_end
 *
 */
SMBlock *sm_colorboth_end(SMBlock *pBlock, int iTag, TupleSS aParams[SM_MAX_PARAMS])
{
	sm_toggle_end(pBlock, kFormatTags_ColorTop+kFormatTags_Count, aParams);
	sm_toggle_end(pBlock, kFormatTags_ColorBottom+kFormatTags_Count, aParams);

	return pBlock;
}

/**********************************************************************func*
 * sm_scale
 *
 */
SMBlock *sm_scale(SMBlock *pBlock, int iTag, TupleSS aParams[SM_MAX_PARAMS])
{
	SMBlock *pnew=sm_AppendNewBlock(pBlock);
	pnew->sType = iTag;

	pnew->pv = U32_TO_PTR(atof(sm_GetVal("scale", aParams))*SMF_FONT_SCALE);

	return pBlock;
}

/**********************************************************************func*
 * sm_outline
 *
 */
SMBlock *sm_outline(SMBlock *pBlock, int iTag, TupleSS aParams[SM_MAX_PARAMS])
{
	SMBlock *pnew=sm_AppendNewBlock(pBlock);
	pnew->sType = iTag;

	pnew->pv = S32_TO_PTR(atoi(sm_GetVal("outline", aParams)));

	return pBlock;
}

/**********************************************************************func*
 * sm_shadow
 *
 */
SMBlock *sm_shadow(SMBlock *pBlock, int iTag, TupleSS aParams[SM_MAX_PARAMS])
{
	SMBlock *pnew=sm_AppendNewBlock(pBlock);
	pnew->sType = iTag;

	pnew->pv = S32_TO_PTR(atoi(sm_GetVal("shadow", aParams)));

	return pBlock;
}

/**********************************************************************func*
* sm_snaptopixels
*
*/
SMBlock *sm_snaptopixels(SMBlock *pBlock, int iTag, TupleSS aParams[SM_MAX_PARAMS])
{
	SMBlock *pnew=sm_AppendNewBlock(pBlock);
	pnew->sType = iTag;

	pnew->pv = S32_TO_PTR(atoi(sm_GetVal("snaptopixels", aParams)));

	return pBlock;
}

/**********************************************************************func*
 * sm_face
 *
 */
SMBlock *sm_face(SMBlock *pBlock, int iTag, TupleSS aParams[SM_MAX_PARAMS])
{
	char *pch;
	SMBlock *pnew=sm_AppendNewBlock(pBlock);
	pnew->sType = iTag;

	pch = sm_GetVal("face", aParams);

	// First check to see if it's in the face dictionary
	if( (pnew->pv = gfxfont_GetFace(pch)) )
	{
		return pBlock;
	}

	// It's not in the face dictionary, go to the standard names
	if(stricmp(pch, "courier")==0 || stricmp(pch, "monospace")==0 || stricmp(pch, "mono")==0)
	{
		pnew->pv = &g_font_Mono;
	}
	else if (stricmp(pch, "computer")==0)
	{
		pnew->pv = &g_font_Mono;
	}
	else
	{
		pnew->pv = &g_font_Sans;
	}

	return pBlock;
}

/**********************************************************************func*
 * sm_font
 *
 */
SMBlock *sm_font(SMBlock *pBlock, int iTag, TupleSS aParams[SM_MAX_PARAMS])
{
	SMBlock *pnew = sm_AppendNewBlock(pBlock);
	SMFont *pfont = calloc(1, sizeof(SMFont));
	pnew->sType = iTag;
	pnew->pv = pfont;
	pnew->bFreeOnDestroy = true;

	// FIXME: Man, this is ugly... Since SMF predates StyleFonts, we cheat it
	// by turning a StyleFont into a set of SMF parameters.
	if(*sm_GetVal("style", aParams)!=0)
	{
		const char *pchStyleName = sm_GetVal("style", aParams);
		UIStyleFont *pStyleFont = ui_StyleFontGet(pchStyleName);
		if (pStyleFont)
		{
			SMBlock *pTemp;
			U32 uiColorTop, uiColorBottom;
			pfont->bFace = 1;
			pTemp = sm_AppendNewBlock(pBlock);
			pTemp->sType = kFormatTags_Face;
			pTemp->pv = GET_REF(pStyleFont->hFace);

			if (pStyleFont->iOutlineWidth)
			{
				pTemp = sm_AppendNewBlock(pBlock);
				pTemp->sType = kFormatTags_Outline;
				pTemp->pv = S32_TO_PTR(pStyleFont->iOutlineWidth);
				pfont->bOutline = 1;
			}

			if (pStyleFont->bBold)
			{
				pTemp = sm_AppendNewBlock(pBlock);
				pTemp->sType = kFormatTags_Bold;
				pTemp->pv = S32_TO_PTR(pStyleFont->bBold);
				pfont->bBold = 1;
			}

			if (pStyleFont->bItalic)
			{
				pTemp = sm_AppendNewBlock(pBlock);
				pTemp->sType = kFormatTags_Italic;
				pTemp->pv = S32_TO_PTR(pStyleFont->bItalic);
				pfont->bItalic = 1;
			}

			if (pStyleFont->iShadowOffset)
			{
				pTemp = sm_AppendNewBlock(pBlock);
				pTemp->sType = kFormatTags_Shadow;
				pTemp->pv = S32_TO_PTR(pStyleFont->iShadowOffset);
				pfont->bShadow = 1;
			}

			if (pStyleFont->uiDropShadowColor)
			{
				pTemp = sm_AppendNewBlock(pBlock);
				pTemp->sType = kFormatTags_DropShadowColor;
				pTemp->pv = S32_TO_PTR(ui_StyleColorPaletteIndex(pStyleFont->uiDropShadowColor));
				pfont->bDropShadowColor = 1;
			}

			if (pStyleFont->uiOutlineColor)
			{
				pTemp = sm_AppendNewBlock(pBlock);
				pTemp->sType = kFormatTags_OutlineColor;
				pTemp->pv = S32_TO_PTR(ui_StyleColorPaletteIndex(pStyleFont->uiOutlineColor));
				pfont->bOutlineColor = 1;
			}

			if (pStyleFont->uiTopLeftColor)
			{
				uiColorTop = ui_StyleColorPaletteIndex(pStyleFont->uiTopLeftColor);
				uiColorBottom = ui_StyleColorPaletteIndex(pStyleFont->uiBottomLeftColor);
			}
			else
			{
				uiColorTop = uiColorBottom = ui_StyleColorPaletteIndex(pStyleFont->uiColor);
			}

			pTemp = sm_AppendNewBlock(pBlock);
			pTemp->sType = kFormatTags_ColorTop;
			pTemp->pv = U32_TO_PTR(uiColorTop);
			pTemp = sm_AppendNewBlock(pBlock);
			pTemp->sType = kFormatTags_ColorBottom;
			pTemp->pv = U32_TO_PTR(uiColorBottom);
			pfont->bColorTop = 1;
			pfont->bColorBottom = 1;

			pTemp = sm_AppendNewBlock(pBlock);
			pTemp->sType = kFormatTags_SnapToPixels;
			pTemp->pv = S32_TO_PTR(pStyleFont->bDontSnapToPixels ? 0 : 1);
			pfont->bSnapToPixels = 1;
		}
	}

	if(*sm_GetVal("face", aParams)!=0)
	{
		sm_face(pBlock, kFormatTags_Face, aParams);
		pfont->bFace = 1;
	}

	if(*sm_GetVal("bold", aParams)!=0)
	{
		sm_b(pBlock, kFormatTags_Bold, aParams);
		pfont->bBold = 1;
	}

	if(*sm_GetVal("italic", aParams)!=0)
	{
		sm_i(pBlock, kFormatTags_Italic, aParams);
		pfont->bItalic = 1;
	}

	if(*sm_GetVal("scale", aParams)!=0)
	{
		sm_scale(pBlock, kFormatTags_Scale, aParams);
		pfont->bScale = 1;
	}

	if(*sm_GetVal("color", aParams)!=0)
	{
		sm_color(pBlock, kFormatTags_ColorTop, aParams);
		sm_color(pBlock, kFormatTags_ColorBottom, aParams);
		pfont->bColorTop = 1;
		pfont->bColorBottom = 1;
	}

	if(*sm_GetVal("colortop", aParams)!=0)
	{
		sm_color(pBlock, kFormatTags_ColorTop, aParams);
		pfont->bColorTop = 1;
	}

	if(*sm_GetVal("colorbottom", aParams)!=0)
	{
		sm_color(pBlock, kFormatTags_ColorBottom, aParams);
		pfont->bColorBottom = 1;
	}

	if(*sm_GetVal("outline", aParams)!=0)
	{
		sm_outline(pBlock, kFormatTags_Outline, aParams);
		pfont->bOutline = 1;
	}

	if(*sm_GetVal("shadow", aParams)!=0)
	{
		sm_shadow(pBlock, kFormatTags_Shadow, aParams);
		pfont->bShadow = 1;
	}

	if(*sm_GetVal("outlinecolor", aParams)!=0)
	{
		sm_outlinecolor(pBlock, kFormatTags_OutlineColor, aParams);
		pfont->bOutlineColor = 1;
	}

	if(*sm_GetVal("dropshadowcolor", aParams)!=0)
	{
		sm_dropshadowcolor(pBlock, kFormatTags_DropShadowColor, aParams);
		pfont->bDropShadowColor = 1;
	}

	if(*sm_GetVal("snaptopixels", aParams)!=0)
	{
		sm_snaptopixels(pBlock, kFormatTags_SnapToPixels, aParams);
		pfont->bSnapToPixels = 1;
	}
	return pBlock;
}

/**********************************************************************func*
 * sm_anchor
 *
 */
SMBlock *sm_anchor(SMBlock *pBlock, int iTag, TupleSS aParams[SM_MAX_PARAMS])
{
	if (g_preventLinks && strlen(sm_GetVal("item", aParams)) == 0)
		return NULL;
	else
	{
		char* str;
		SMAnchorType type;
		size_t szString;
		SMAnchor *p;
		SMBlock *pnew;

		//Our link can hold either an item name or a url
		//If both are defined, use the item
		if(strlen(sm_GetVal("item", aParams)) > 0)
		{
			str = sm_GetVal("item", aParams);
			type = SMAnchorItem;
		}
		else
		{
			str = sm_GetVal("href", aParams);
			type = SMAnchorLink;
		}

		szString = strlen(str) + 1;
		pnew = sm_AppendNewBlock(pBlock);
		pnew->sType = iTag;

		p = calloc(1, sizeof(SMAnchor) + szString);
		strcpy_s(p->ach, szString, str);
		p->type = type;

		pnew->pv = p;
		pnew->bFreeOnDestroy = true;

		return pBlock;
	}
}

/**********************************************************************func*
 * sm_align
 *
 */
SMBlock *sm_align(SMBlock *pBlock, int iTag, TupleSS aParams[SM_MAX_PARAMS])
{
	SMBlock *pnew=sm_AppendNewBlock(pBlock);
	pnew->sType = iTag;

	pnew->pv = S32_TO_PTR(sm_GetAlignment("align", aParams));

	return pBlock;
}


/**********************************************************************func*
 * sm_valign
 *
 */
SMBlock *sm_valign(SMBlock *pBlock, int iTag, TupleSS aParams[SM_MAX_PARAMS])
{
	SMBlock *pnew=sm_AppendNewBlock(pBlock);
	pnew->sType = iTag;

	pnew->pv = S32_TO_PTR(sm_GetAlignment("valign", aParams));

	return pBlock;
}

/**********************************************************************func*
 * sm_image
 *
 */
SMBlock *sm_image(SMBlock *pBlock, int iTag, TupleSS aParams[SM_MAX_PARAMS])
{
	SMBlock *pnew = sm_AppendNewBlockAndData(pBlock, sizeof(SMImage));
	SMImage *pImg = (SMImage *)pnew->pv;
	pnew->sType = iTag;

	pImg->iWidth = atoi(sm_GetVal("width", aParams));
	pImg->iHeight = atoi(sm_GetVal("height", aParams));
	pImg->iHighlight = atoi(sm_GetVal("highlight", aParams));
	pImg->iColor = sm_GetColor("color", aParams);
	pImg->iSkinOverride = atoi(sm_GetVal("uiskin", aParams));
	strcpy(pImg->achTex, sm_GetVal("src", aParams));
	strcpy(pImg->achTexHover, sm_GetVal("srchover", aParams));

	pnew->pos.iBorder = atoi(sm_GetVal("border", aParams));
	pnew->pos.alignHoriz = sm_GetAlignment("align", aParams);

	return pBlock;
}

/**********************************************************************func*
 * sm_table
 *
 */
SMBlock *sm_table(SMBlock *pBlock, int iTag, TupleSS aParams[SM_MAX_PARAMS])
{
	SMBlock *pnew = sm_AppendNewContainerAndData(pBlock, sizeof(SMTable));
	SMTable *pTable = (SMTable *)pnew->pv;

	pnew->sType = iTag;

	pTable->eCollapse = sm_GetAlignment("collapse", aParams);
	pnew->pos.iBorder = atoi(sm_GetVal("border", aParams));

	return pnew;
}

/**********************************************************************func*
 * sm_table_end
 *
 */
SMBlock *sm_table_end(SMBlock *pBlock, int iTag, TupleSS aParams[SM_MAX_PARAMS])
{
	return pBlock->pParent;
}

/**********************************************************************func*
 * sm_tr
 *
 */
SMBlock *sm_tr(SMBlock *pBlock, int iTag, TupleSS aParams[SM_MAX_PARAMS])
{
	SMBlock *pnew = sm_AppendNewContainerAndData(pBlock, sizeof(SMRow));
	SMRow *pRow = (SMRow *)pnew->pv;

	pnew->sType = iTag;

	pRow->iHighlight = atoi(sm_GetVal("highlight", aParams));
	pRow->iSelected = atoi(sm_GetVal("selected", aParams));
	pnew->pos.iBorder = atoi(sm_GetVal("border", aParams));

	return pnew;
}

/**********************************************************************func*
 * sm_tr_end
 *
 */
SMBlock *sm_tr_end(SMBlock *pBlock, int iTag, TupleSS aParams[SM_MAX_PARAMS])
{
	return pBlock->pParent;
}

/**********************************************************************func*
 * sm_td
 *
 */
SMBlock *sm_td(SMBlock *pBlock, int iTag, TupleSS aParams[SM_MAX_PARAMS])
{
	SMBlock *pnew = sm_AppendNewContainerAndData(pBlock, sizeof(SMColumn));
	SMColumn *pCol = (SMColumn *)pnew->pv;
	char *pch;

	pnew->sType = iTag;

	pch = sm_GetVal("width", aParams);
	pCol->iWidthRequested = atoi(pch);
	pCol->bFixedWidth = (pCol->iWidthRequested>0 && strchr(pch, '%')==NULL);

	pCol->iHighlight = atoi(sm_GetVal("highlight", aParams));
	pCol->iSelected = atoi(sm_GetVal("selected", aParams));
	pnew->pos.iBorder = atoi(sm_GetVal("border", aParams));
	pnew->pos.alignHoriz = sm_GetAlignment("align", aParams);
	pnew->pos.alignVert = sm_GetAlignment("valign", aParams);

	return pnew;
}

/**********************************************************************func*
 * sm_td_end
 *
 */
SMBlock *sm_td_end(SMBlock *pBlock, int iTag, TupleSS aParams[SM_MAX_PARAMS])
{
	return pBlock->pParent;
}

/**********************************************************************func*
 * InitTags
 *
 */
static int s_idxTr = -1;
static int s_idxTd = -1;
static int s_idxTrEnd = -1;
static int s_idxTdEnd = -1;
static int s_idxImg = -1;

static InitTags(void)
{
	if(s_idxTr<0)
	{
		int i;
		for(i = 0; smf_aTagDefs[i].pchName!=NULL; i++)
		{
			if(smf_aTagDefs[i].id == k_sm_tr)
				s_idxTr = i;
			if(smf_aTagDefs[i].id == k_sm_tr_end)
				s_idxTrEnd = i;
			else if(smf_aTagDefs[i].id == k_sm_td)
				s_idxTd = i;
			else if(smf_aTagDefs[i].id == k_sm_td_end)
				s_idxTdEnd = i;
			else if(smf_aTagDefs[i].id == k_sm_image)
				s_idxImg = i;
		}
	}
}

/**********************************************************************func*
 * sm_ul
 *
 * Unordered lists are actually tables: They contain <tr> and <td> elements
 * which are auto-generated via <li> and </li>. Now that I've written it
 * like this and it works, I feel a bit dirty mainly because I needed to
 * find and cache the iTag value (which is the same as the index into the
 * parse table).
 *
 * So, apologies to myself when I come back two years from now and curse
 * this silliness.
 *
 */
SMBlock *sm_ul(SMBlock *pBlock, int iTag, TupleSS aParams[SM_MAX_PARAMS])
{
	SMBlock *pnew = sm_AppendNewContainerAndData(pBlock, sizeof(SMTable));
	SMTable *pTable = (SMTable *)pnew->pv;

	pnew->sType = iTag;

	pnew->pos.iBorder = atoi(sm_GetVal("border", aParams));
	pTable->iIndent = atoi(sm_GetVal("indent", aParams));
	strcpy(pTable->achSymbol, sm_GetVal("symbol", aParams));

	InitTags();

	// Start the sizing row
	{
		pnew = sm_AppendNewContainerAndData(pnew, sizeof(SMRow));
		pnew->sType = s_idxTr;
	}

	// Add the indent column
	{
		SMColumn *pCol;
		pnew = sm_AppendNewContainerAndData(pnew, sizeof(SMColumn));
		pnew->sType = s_idxTd;

		pCol = (SMColumn *)pnew->pv;
		pCol->iWidthRequested = pTable->iIndent;
		pCol->bFixedWidth = true;

		// End the indent
		pnew = pnew->pParent;
	}

	// Add the content column
	{
		SMColumn *pCol;
		pnew = sm_AppendNewContainerAndData(pnew, sizeof(SMColumn));
		pnew->sType = s_idxTd;

		pCol = (SMColumn *)pnew->pv;
		pCol->iWidthRequested = 100;
		pCol->bFixedWidth = false;

		// End the content
		pnew = pnew->pParent;
	}

	// End the sizing row
	pnew = pnew->pParent;

	return pnew;
}

/**********************************************************************func*
 * sm_ul_end
 *
 */
SMBlock *sm_ul_end(SMBlock *pBlock, int iTag, TupleSS aParams[SM_MAX_PARAMS])
{
	return pBlock->pParent;
}

/**********************************************************************func*
 * sm_li
 *
 */
SMBlock *sm_li(SMBlock *pBlock, int iTag, TupleSS aParams[SM_MAX_PARAMS])
{
	SMTable table = { kAlignment_None, 15, "*" };
	SMBlock *pnew;

	InitTags();

	if(smf_aTagDefs[pBlock->sType].id==k_sm_ul)
	{
		table = *(SMTable *)pBlock->pv;
	}

	// Start the row
	{
		pnew = sm_AppendNewContainerAndData(pBlock, sizeof(SMRow));
		pnew->sType = s_idxTr;
	}

	// Add the indent column
	{
		const char *pchSymbol;

		pnew = sm_AppendNewContainerAndData(pnew, sizeof(SMColumn));
		pnew->sType = s_idxTd;

		pnew->pos.alignHoriz = kAlignment_Right;
		pnew->pos.alignVert = kAlignment_Top;

		// Add the symbol
		pchSymbol = sm_GetVal("symbol", aParams);
		if(!pchSymbol || !*pchSymbol)
		{
			pchSymbol = table.achSymbol;
		}
		if(strlen(pchSymbol)>4)
		{
			// Add a texture
			SMBlock *p = sm_AppendNewBlockAndData(pnew, sizeof(SMImage));
			SMImage *pImg = (SMImage *)p->pv;
			p->sType = s_idxImg;

			strcpy(pImg->achTex, pchSymbol);
			pImg->iColor = 0xffffffff;
		}
		else
		{
			TextPush(pnew, pchSymbol, (int)strlen(pchSymbol));
			TextPush(pnew, "&nbsp;", 6);
		}

		// End the indent
		pnew = pnew->pParent;
	}

	// Start the content column
	return sm_td(pnew, s_idxTd, aParams);
}

/**********************************************************************func*
* sm_li_end
*
*/
SMBlock *sm_li_end(SMBlock *pBlock, int iTag, TupleSS aParams[SM_MAX_PARAMS])
{
	bool bCleanup = false;
	SMBlock *pBlockOrig = pBlock;

	InitTags();

	// pBlock should be a <td>
	if(pBlock->bHasBlocks
		&& eaSize(&pBlock->ppBlocks)>0
		&& pBlock->ppBlocks[0]
		&& smf_aTagDefs[pBlock->ppBlocks[0]->sType].id==k_sm_ul)
	{
		// This is a nested <ul>. Try to clean up the extra symbol.
		bCleanup = true;
	}

	pBlock = sm_td_end(pBlock, s_idxTdEnd, aParams);

	// pBlock should be a <tr>
	if(bCleanup)
	{
		if(pBlock->bHasBlocks
			&& eaSize(&pBlock->ppBlocks)>0
			&& pBlock->ppBlocks[0]
			&& smf_aTagDefs[pBlock->ppBlocks[0]->sType].id==k_sm_td)
		{
			SMBlock *pindent = pBlock->ppBlocks[0];

			// We want to remove the contents of the indent block,
			//   but not the block itself.
			int i;
			for(i=eaSize(&pindent->ppBlocks)-1; i>=0; i--)
			{
				sm_DestroyBlock(pindent->ppBlocks[i]);
			}
			eaClear(&pindent->ppBlocks);
		}
	}

	if(pBlock)
		return sm_tr_end(pBlock, s_idxTrEnd, aParams);
	else
		return pBlockOrig->pParent; // This only happens in badly formed SMF and is a fairly safe return.
}

/**********************************************************************func*
 * sm_span
 *
 */
SMBlock *sm_span(SMBlock *pBlock, int iTag, TupleSS aParams[SM_MAX_PARAMS])
{
	SMBlock *pnew = sm_AppendNewContainerAndData(pBlock, sizeof(SMColumn));
	SMColumn *pCol = (SMColumn *)pnew->pv;
	char *pch;

	pnew->sType = iTag;

	pch = sm_GetVal("width", aParams);
	pCol->iWidthRequested = atoi(pch);
	pCol->bFixedWidth = (pCol->iWidthRequested>0 && strchr(pch, '%')==NULL);

	pCol->iHighlight = atoi(sm_GetVal("highlight", aParams));
	pnew->pos.alignHoriz = sm_GetAlignment("align", aParams);
	pnew->pos.alignVert = sm_GetAlignment("valign", aParams);

	return pnew;
}

/**********************************************************************func*
 * sm_span_end
 *
 */
SMBlock *sm_span_end(SMBlock *pBlock, int iTag, TupleSS aParams[SM_MAX_PARAMS])
{
	return pBlock->pParent;
}

/**********************************************************************func*
 * sm_p
 *
 */
SMBlock *sm_p(SMBlock *pBlock, int iTag, TupleSS aParams[SM_MAX_PARAMS])
{
	SMBlock *pnew = sm_AppendNewBlock(pBlock);
	pnew->sType = iTag;

	return pBlock;
}

/**********************************************************************func*
 * sm_p_end
 *
 */
SMBlock *sm_p_end(SMBlock *pBlock, int iTag, TupleSS aParams[SM_MAX_PARAMS])
{
	SMBlock *pnew = sm_AppendNewBlock(pBlock);
	pnew->sType = iTag;

	return pBlock;
}

/**********************************************************************func*
 * sm_nolink
 *
 */
SMBlock *sm_nolink(SMBlock *pBlock, int iTag, TupleSS aParams[SM_MAX_PARAMS])
{
	SMBlock *pnew = sm_AppendNewBlock(pBlock);
	pnew->sType = iTag;
	g_preventLinks++;

	return pBlock;
}

/**********************************************************************func*
 * sm_nolink_end
 *
 */
SMBlock *sm_nolink_end(SMBlock *pBlock, int iTag, TupleSS aParams[SM_MAX_PARAMS])
{
	SMBlock *pnew = sm_AppendNewBlock(pBlock);
	pnew->sType = iTag;
	if (g_preventLinks)
		g_preventLinks--;

	return pBlock;
}


/**********************************************************************func*
 * sm_text
 *
 */
SMBlock *sm_text(SMBlock *pBlock, int iTag, TupleSS aParams[SM_MAX_PARAMS])
{
	return pBlock;
}

/**********************************************************************func*
 * sm_time
 *
 */
SMBlock *sm_time(SMBlock *pBlock, int iTag, TupleSS aParams[SM_MAX_PARAMS])
{
	SMBlock *pnew = sm_AppendNewContainerAndData(pBlock, sizeof(SMTime));
	SMTime *pTime = (SMTime *)pnew->pv;
	F32 fDuration = atof(sm_GetVal("duration", aParams));
	F32 fStart = atof(sm_GetVal("start", aParams));
	F32 fEnd = atof(sm_GetVal("end", aParams));
	if (fStart && fDuration)
	{
		pTime->fStart = fStart;
		pTime->fEnd = fStart + fDuration;
	}
	else if (fDuration && fEnd)
	{
		pTime->fStart = max(0, fEnd - fDuration);
		pTime->fEnd = fEnd;
	}
	else
	{
		pTime->fStart = fStart;
		pTime->fEnd = fEnd;
	}
	pnew->sType = iTag;
	return pnew;
}

/**********************************************************************func*
 * sm_time_end
 *
 */
SMBlock *sm_time_end(SMBlock *pBlock, int iTag, TupleSS aParams[SM_MAX_PARAMS])
{
	SMBlock *pnew = sm_AppendNewBlock(pBlock);
	S32 i;
	for (i = 0; smf_aTagDefs[i].pchName && !pnew->sType; i++)
	{
		if (smf_aTagDefs[i].id == k_sm_br)
		{
			pnew->sType = i;
		}
	}

	return pBlock->pParent;
}


/**********************************************************************func*
* sm_sound
*
*/
SMBlock *sm_sound(SMBlock *pBlock, int iTag, TupleSS aParams[SM_MAX_PARAMS])
{
	SMBlock *pNewBlock = sm_AppendNewBlock(pBlock);
	if(pNewBlock)
	{
		const char *pcEventPath;
		size_t szString; 
		SMSound *pSound;

		pcEventPath = sm_GetVal("event", aParams);
		szString = strlen(pcEventPath) + 1;

		pSound = calloc(1, sizeof(SMSound) + szString);
		if(pSound)
		{
			strcpy_s(pSound->pcSoundEventPath, szString, pcEventPath);

			pNewBlock->sType = iTag;
			pNewBlock->pv = pSound;
			pNewBlock->bFreeOnDestroy = true;
			pNewBlock->bAlwaysRender = true;
		}
	}

	return pBlock;
}



SMTagDef smf_aTagDefs[] =
{
//	{ "tag",     DoTag,     true,	{{ "param1", "default" }, { "param2", "moo" }, { "param3", "foo" }, { "param4", "bar" }} },

	// These need to match the order of the FormatTag enum.
	// TextAttribs, FormatTags, and the SMFTag first N entries must all match.
	// If you add one here, add one there. And vice versa.

	{ "b",            SMF_TAG(sm_b),			true,	},
	{ "i",            SMF_TAG(sm_i),			true,	},
	{ "colortop",     SMF_TAG(sm_color),		true,	{{ "color", "black" }} },
	{ "colorbottom",  SMF_TAG(sm_color),		true,	{{ "color", "black" }} },
	{ "outlinecolor", SMF_TAG(sm_outlinecolor),	true,	{{ "outlinecolor", "black" }} },
	{ "dropshadowcolor",SMF_TAG(sm_dropshadowcolor),	true,	{{ "dropshadowcolor", "black" }} },
	{ "scale",        SMF_TAG(sm_scale),		true,	{{ "scale", "1.0" }}   },
	{ "face",         SMF_TAG(sm_face),			true,	{{ "face", "arial" }}  },
	{ "font",         SMF_TAG(sm_font),			true,	{{ "face", "" }, { "scale", "" }, { "color", "" }, { "colortop", "" }, { "colorbottom", "" }, { "outline", "" }, { "shadow", "" }, { "outlinecolor", "" }, { "dropshadowcolor", "" }, {"style", ""}, {"bold", ""}, {"italic", ""}, {"snaptopixels", ""}} },
	{ "a",            SMF_TAG(sm_anchor),		false,	{{ "href", "" }, {"item", ""}} },
	{ "link",         SMF_TAG(sm_color),		true,	{{ "color", "green"     }} },
	{ "linkbg",       SMF_TAG(sm_color),		true,	{{ "color", "0"         }} },
	{ "linkhover",    SMF_TAG(sm_color),		true,	{{ "color", "lawngreen" }} },
	{ "linkhoverbg",  SMF_TAG(sm_color),		true,	{{ "color", "0"         }} },
	{ "outline",      SMF_TAG(sm_outline),		true,	{{ "outline", "0" }} },
	{ "shadow",       SMF_TAG(sm_shadow),		true,	{{ "shadow", "0" }} },
	{ "snaptopixels", SMF_TAG(sm_snaptopixels),	true,	{{ "snaptopixels", "0" }} },

	// These close tags must match the order of the entries above.
	// TextAttribs, FormatTags, and the SMFTag first N entries must all match.
	// If you add one here, add one there. And vice versa.

	{ "/b",              SMF_TAG(sm_toggle_end),	true,	},
	{ "/i",              SMF_TAG(sm_toggle_end), 	true,	},
	{ "/colortop",       SMF_TAG(sm_toggle_end), 	true,	},
	{ "/colorbottom",    SMF_TAG(sm_toggle_end), 	true,	},
	{ "/outlinecolor",   SMF_TAG(sm_toggle_end), 	true,	},
	{ "/dropshadowcolor",SMF_TAG(sm_toggle_end),	true,	},
	{ "/scale",          SMF_TAG(sm_toggle_end), 	true,	},
	{ "/face",           SMF_TAG(sm_toggle_end), 	true,	},
	{ "/font",           SMF_TAG(sm_toggle_end), 	true,	},
	{ "/a",              SMF_TAG(sm_toggle_link_end), 	false,	},
	{ "/link",           SMF_TAG(sm_toggle_end), 	true,	},
	{ "/linkbg",         SMF_TAG(sm_toggle_end), 	true,	},
	{ "/linkhover",      SMF_TAG(sm_toggle_end), 	true,	},
	{ "/linkhoverbg",    SMF_TAG(sm_toggle_end), 	true,	},
	{ "/outline",        SMF_TAG(sm_toggle_end), 	true,	},
	{ "/shadow",         SMF_TAG(sm_toggle_end), 	true,	},
	{ "/snaptopixels",   SMF_TAG(sm_toggle_end),	true,	},

	{ "text",         SMF_TAG(sm_text),      	true,	}, // must be last
	// don't reorder the items above here.
	{ "ws",           SMF_TAG(sm_ws),         	true,	},
	{ "bsp",          SMF_TAG(sm_bsp),        	true,	},
	{ "br",           SMF_TAG(sm_br),         	true,	},
	{ "table",        SMF_TAG(sm_table),     	false,	{{ "border", "0" }, { "collapse", "none" }} },
	{ "/table",       SMF_TAG(sm_table_end),  	false,	},
	{ "tr",           SMF_TAG(sm_tr),        	false,	{{ "highlight", "0" }, { "border", "0" }, { "selected", "0" }}  },
	{ "/tr",          SMF_TAG(sm_tr_end),     	false,	},
	{ "td",           SMF_TAG(sm_td),        	false,	{{ "width", "0" }, { "align", "none" }, { "valign", "none" }, { "highlight", "0" }, { "border", "0" }, { "selected", "0" }} },
	{ "/td",          SMF_TAG(sm_td_end),     	false,	},
	{ "span",         SMF_TAG(sm_span),      	true,	{{ "width", "0" }, { "align", "none" }, { "valign", "none" }, { "highlight", "0" }} },
	{ "/span",        SMF_TAG(sm_span_end),   	true,	},
	{ "p",            SMF_TAG(sm_p),          	true,	},
	{ "/p",           SMF_TAG(sm_p_end),      	true,	},
	{ "img",          SMF_TAG(sm_image),     	false,	{{ "src", "white.tga" }, { "srchover", "" }, { "align", "none" }, { "width", "-1" }, { "height", "-1" }, { "border", "0" }, { "highlight", "0" }, { "color", "white" }, { "uiskin", "0" }} },
	{ "nolink",       SMF_TAG(sm_nolink),     	false,	},
	{ "/nolink",      SMF_TAG(sm_nolink_end), 	false,	},
	{ "color",        SMF_TAG(sm_colorboth),  	true,	{{ "color", "black" }} },
	{ "/color",       SMF_TAG(sm_colorboth_end), 	true,	},
	{ "time",         SMF_TAG(sm_time),      	false,	{{ "start", "0"}, {"duration", "0"}, {"end", "0"}} },
	{ "/time",        SMF_TAG(sm_time_end),   	false,	},
	{ "ul",           SMF_TAG(sm_ul),        	false,	{{ "border", "0" }, { "indent", "15" }, { "symbol", "*" }} },
	{ "/ul",          SMF_TAG(sm_ul_end),     	false,	},
	{ "li",           SMF_TAG(sm_li),        	false,	{{ "width", "0" }, { "align", "none" }, { "valign", "none" }, { "highlight", "0" }, { "border", "0" }, { "selected", "0" }, { "symbol", "" }} },
	{ "/li",          SMF_TAG(sm_li_end),     	false,	},

	{ "sound",        SMF_TAG(sm_sound),     	false,	{{ "event", "" }} }, // will also require a registered callback see smf_setPlaySoundCallbackFunc
	{ 0 },
};


/**********************************************************************func*
 * InitBase
 *
 */
static SMBlock *InitBase(SMBlock *pBlock)
{
	return pBlock;
}

/**********************************************************************func*
 * smf_CreateAndParse
 *
 */
SMBlock *smf_CreateAndParse(const char *pch, bool bSafeOnly)
{
	SMBlock *retBlock;
	
	PERFINFO_AUTO_START("smf_CreateAndParse", 1);
		// Find the whitespace and text tags
		if(s_iTagText==-1)
		{
			int i = 0;
			SMTagDef *ptag=smf_aTagDefs;

			while(ptag[i].pchName != NULL)
			{
				if(ptag[i].id == k_sm_ws)
				{
					s_iTagWS = i;
				}
				else if(ptag[i].id == k_sm_text)
				{
					s_iTagText = i;
				}

				i++;
			}
		}

		PERFINFO_AUTO_START("sm_Parse", 1);
			retBlock = sm_Parse(pch, smf_aTagDefs, bSafeOnly, InitBase, TextPush);
		PERFINFO_AUTO_STOP();
	PERFINFO_AUTO_STOP();
	
	return retBlock;
}

/**********************************************************************func*
 * smf_EntityReplace
 *
 */
void smf_EntityReplace(char* str)
{
	// This covers the ampersand-semicolon special character syntax, using the static replacement
	// table above. The table is currently relatively sparse, but matches the special characters
	// recognized by estrCopyWithHTMLEscaping()
	while(*str!='\0')
	{
		if(*str=='&')
		{
			if (*(str+1) == '#') {
				char *pchEnd = strchr(str+2, ';');
						
				if (pchEnd) {
					wchar_t character[2];
					char converted[16];
					char *pchDigit;
					int val = 0;
					int bytes;
							
					for (pchDigit = str + 2; *pchDigit && isdigit(*pchDigit); pchDigit++) {
						val *= 10;
						val += *pchDigit - '0';
					}
							
					character[0] = val;
					//character[0] = 0x9580; // some Asian character that's useful for testing
					character[1] = '\0';

					bytes = WideToUTF8StrConvert(character, converted, sizeof(converted));

					memcpy_s(str, strlen(str) - 1, converted, bytes);
					if (str+bytes != pchEnd+1) {
						strcpy_s(str+bytes, strlen(str+bytes)-1, pchEnd+1);
					}
				}
			} else {
				int j;
				for (j = (sizeof(s_SpecialCharTable) / sizeof(SMFSpecialCharReplace))-1; j >= 0; j--) {
					if (strnicmp(str+1, s_SpecialCharTable[j].pcSpecialCharStr, strlen(s_SpecialCharTable[j].pcSpecialCharStr)) == 0) {
						*str = s_SpecialCharTable[j].cReplace;
						strcpy_s(str + 1, strlen(str) - 1, str + strlen(s_SpecialCharTable[j].pcSpecialCharStr) + 1);
					}
				}
			}
		}
		else if(*str=='%')
		{
			if(*(str+1)=='%')
			{
				strcpy_s(str + 1, strlen(str) - 1, str + 2);
			}
		}

		str++;
	}
}

/**********************************************************************func*
 * smf_Destroy
 *
 */
void smf_Destroy(SMBlock *pBlock)
{
	sm_DestroyBlock(pBlock);
}




/* End of File */
