/***************************************************************************
 
 
 
 ***************************************************************************/

#include "estring.h"
#include "Expression.h"

#include "Message.h"
#include "MessageExpressions.h"
#include "inputMouse.h"
#include "TextFilter.h"

#include "GfxTexAtlas.h"
#include "GfxPrimitive.h"
#include "GfxSprite.h"

#include "UIGen.h"
#include "UITextureAssembly.h"
#include "UIGenPrivate.h"
#include "UIInternal.h"
#include "smf/smf_format.h"
#include "smf_render.h"

#include "StringFormat.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

static struct
{
	UIGen *pLastGen;
	UIGen *pLastParent;
	UIGen *pCurrentGen;
	SMFBlock *pBlock;
	SMFBlock **eaSecondaryBlocks;
	unsigned char *pchTooltip;
	unsigned char **eapchSecondaryTooltips;
	UITextureAssembly **eaSecondaryAssemblies;
	F32 fTimer;
	F32 fPrimaryX;
	F32 fPrimaryY;
	F32 fSecondaryX;
	F32 fSecondaryY;
	F32 fScale;
	bool bFlipX;
	bool bFlipY;

	REF_TO(UIStyleFont) hTooltipFont;
	REF_TO(UITextureAssembly) hTooltipAssembly;
} s_GenTooltipState;

static F32 s_fTooltipDelay = 0;
// Sets the additional delay, in seconds, before tooltips appear
AUTO_CMD_FLOAT(s_fTooltipDelay, ui_TooltipDelay) ACMD_ACCESSLEVEL(0) ACMD_CALLBACK(ui_TooltipDelay);

static TextAttribs s_DefaultAttribs;

AUTO_RUN;
void ui_GenTooltipsAutoRun(void)
{
	s_DefaultAttribs = *smf_DefaultTextAttribs();
}

void ui_TooltipDelay(void)
{
	s_fTooltipDelay = max(0, s_fTooltipDelay);
}

AUTO_EXPR_FUNC(UIGen);
void SetTooltipMouseAnchor(bool val)
{
	// TODO(CM): Delete me!
}

AUTO_EXPR_FUNC(UIGen);
F32 GetTooltipDelay(void)
{
	return s_fTooltipDelay;
}

UITextureAssembly *ui_GenTooltipGetAssembly(UIGen *pGen, const char *pchAssembly, bool bSecondary)
{
	static char *s_estrLastError;
	static char *s_estrAssemblyFormatted;
	UITextureAssembly *pAssembly = NULL;

	if (pchAssembly && strchr(pchAssembly, STRFMT_TOKEN_START))
	{
		estrClear(&s_estrAssemblyFormatted);
		exprFormat(&s_estrAssemblyFormatted, pchAssembly, ui_GenGetContext(pGen), pGen->pchFilename);

		if (s_estrAssemblyFormatted && *s_estrAssemblyFormatted)
		{
			pAssembly = RefSystem_ReferentFromString("UITextureAssembly", s_estrAssemblyFormatted);
			if (!pAssembly)
			{
				if (!s_estrLastError || !stricmp(s_estrLastError, s_estrAssemblyFormatted))
				{
					ErrorFilenamef(pGen->pchFilename, "%s: Invalid %s assembly %s", pGen->pchName, bSecondary ? "secondary tooltip" : "tooltip", s_estrAssemblyFormatted);
					estrCopy2(&s_estrLastError, s_estrAssemblyFormatted);
				}
			}
		}

		// Default fall back to Tooltip. If Tooltip assembly doesn't exist,
		// then it'll fallback to drawing white.tga.
		if (!pAssembly)
		{
			if (!IS_HANDLE_ACTIVE(s_GenTooltipState.hTooltipAssembly))
				SET_HANDLE_FROM_STRING("UITextureAssembly", "Tooltip", s_GenTooltipState.hTooltipAssembly);
			pAssembly = GET_REF(s_GenTooltipState.hTooltipAssembly);
		}
	}
	else if (pchAssembly && *pchAssembly)
	{
		pAssembly = RefSystem_ReferentFromString("UITextureAssembly", pchAssembly);

		if (!pAssembly)
		{
			if (!s_estrLastError || !stricmp(s_estrLastError, pchAssembly))
			{
				ErrorFilenamef(pGen->pchFilename, "%s: Invalid %s assembly %s", pGen->pchName, bSecondary ? "secondary tooltip" : "tooltip", pchAssembly);
				estrCopy2(&s_estrLastError, pchAssembly);
			}
		}
	}

	return pAssembly;
}

void ui_GenHandleRelativeTooltipOffset(UIGen *pGen, CBox *pBox)
{
	F32 xUnflippedOverflow = 0.f; // Used to measure which side fits better
	F32 yUnflippedOverflow = 0.f;
	ui_GenHandleRelativeOffset(pGen, pBox, pGen->pResult->pTooltip->pRelative);
	s_GenTooltipState.bFlipX = pBox->lx < UI_STEP || pBox->hx > g_ui_State.screenWidth - UI_STEP;
	s_GenTooltipState.bFlipY = pBox->ly < UI_STEP || pBox->hy > g_ui_State.screenHeight - UI_STEP;
	if (s_GenTooltipState.bFlipX || s_GenTooltipState.bFlipY)
	{
		UIGenRelative *pRelative = SAFE_MEMBER3(pGen, pResult, pTooltip, pRelative);
		UIGenRelative tempRelative = {0};
		bool bFlipX = false;
		bool bFlipY = false;
		if (pRelative)
			tempRelative = *pRelative;

		if (s_GenTooltipState.bFlipX)
		{
			UIGenRelativeAtom swap = tempRelative.RightFrom;
			xUnflippedOverflow = MAX(UI_STEP - pBox->lx, pBox->hx - (g_ui_State.screenWidth - UI_STEP));
			tempRelative.RightFrom = tempRelative.LeftFrom;
			tempRelative.LeftFrom = swap;
			if (tempRelative.LeftFrom.pchName)
				tempRelative.LeftFrom.eOffset ^= UIHorizontal;
			if (tempRelative.RightFrom.pchName)
				tempRelative.RightFrom.eOffset ^= UIHorizontal;
		}
		if (s_GenTooltipState.bFlipY)
		{
			UIGenRelativeAtom swap = tempRelative.BottomFrom;
			yUnflippedOverflow = MAX(UI_STEP - pBox->ly, pBox->hy - (g_ui_State.screenHeight - UI_STEP));
			tempRelative.BottomFrom = tempRelative.TopFrom;
			tempRelative.TopFrom = swap;
			if (tempRelative.TopFrom.pchName)
				tempRelative.TopFrom.eOffset ^= UIVertical;
			if (tempRelative.BottomFrom.pchName)
				tempRelative.BottomFrom.eOffset ^= UIVertical;
		}
		ui_GenHandleRelativeOffset(pGen, pBox, &tempRelative);

		// Now if you're still off the screen (which you really shouldn't be at 
		// this point), move to the side that the tool tip fits better on
		if (pBox->lx < UI_STEP || pBox->hx - (g_ui_State.screenWidth - UI_STEP))
		{
			F32 xFlippedOverflow = MAX(UI_STEP - pBox->lx, pBox->hx - (g_ui_State.screenWidth - UI_STEP));
			bFlipX = (xFlippedOverflow > xUnflippedOverflow);
		}
		if (pBox->ly < UI_STEP || pBox->hy - (g_ui_State.screenHeight - UI_STEP))
		{
			F32 yFlippedOverflow = MAX(UI_STEP - pBox->ly, pBox->hy - (g_ui_State.screenHeight - UI_STEP));
			bFlipY = (yFlippedOverflow > yUnflippedOverflow);
		}

		// Flip back to the original sides if they fit better there. 
		s_GenTooltipState.bFlipX ^= bFlipX;
		s_GenTooltipState.bFlipY ^= bFlipY;

		if (bFlipX || bFlipY)
		{
			// Undoing what was just done
			if (bFlipX)
			{
				UIGenRelativeAtom swap = tempRelative.RightFrom;
				tempRelative.RightFrom = tempRelative.LeftFrom;
				tempRelative.LeftFrom = swap;
				if (tempRelative.LeftFrom.pchName)
					tempRelative.LeftFrom.eOffset ^= UIHorizontal;
				if (tempRelative.RightFrom.pchName)
					tempRelative.RightFrom.eOffset ^= UIHorizontal;
			}
			if (bFlipY)
			{
				UIGenRelativeAtom swap = tempRelative.BottomFrom;
				tempRelative.BottomFrom = tempRelative.TopFrom;
				tempRelative.TopFrom = swap;
				if (tempRelative.TopFrom.pchName)
					tempRelative.TopFrom.eOffset ^= UIVertical;
				if (tempRelative.BottomFrom.pchName)
					tempRelative.BottomFrom.eOffset ^= UIVertical;
			}
			ui_GenHandleRelativeOffset(pGen, pBox, &tempRelative);
		}
	}
}

void ui_GenTooltipClear(void)
{
	s_GenTooltipState.pCurrentGen = NULL;
}

void ui_GenTooltipClearGen(UIGen *pGen)
{
	if (s_GenTooltipState.pCurrentGen == pGen)
		s_GenTooltipState.pCurrentGen = NULL;
	if (s_GenTooltipState.pLastGen == pGen)
		s_GenTooltipState.pLastGen = NULL;
}

void ui_GenTooltipSet(UIGen *pGen)
{
	if (s_GenTooltipState.pCurrentGen)
		return; // Called already this frame.
	else if (pGen != s_GenTooltipState.pLastGen)
	{
		s_GenTooltipState.fPrimaryX = -1;
		s_GenTooltipState.fPrimaryY = -1;
		s_GenTooltipState.fSecondaryX = -1;
		s_GenTooltipState.fSecondaryY = -1;

		// A gen with the same parent as the last one does not reset the tooltip timer.
		if (pGen && pGen->pResult && pGen->pResult->pTooltip &&
			(s_GenTooltipState.fTimer > 0
			|| (pGen->pParent
				&& pGen->pParent != s_GenTooltipState.pLastParent
				&& !pGen->pResult->pTooltip->bIgnoreParent)))
		{
			s_GenTooltipState.fTimer = pGen->pResult->pTooltip->fDelay;
			if (s_GenTooltipState.fTimer < 0)
				s_GenTooltipState.fTimer = s_fTooltipDelay;
		}
	}
	s_GenTooltipState.pCurrentGen = pGen;
}

static void ui_GenDrawTooltipEx(UIGen *pGen, SMFBlock *pBlock, const char *pchTooltip, CBox *pBox, UITextureAssembly *pAssembly)
{
	static AtlasTex *pWhite = NULL;
	if (!pWhite)
		pWhite = atlasLoadTexture("white");

	if (pAssembly)
	{
		F32 fZ = UI_GET_Z();
		ui_TextureAssemblyDraw(pAssembly, pBox, NULL, s_GenTooltipState.fScale, fZ, fZ + 0.9, pGen->pResult->pTooltip->bInheritAlpha ? pGen->chAlpha : 0xFF, NULL);
	}
	else
	{
		display_sprite(pWhite, pBox->lx, pBox->ly, UI_GET_Z(), CBoxWidth(pBox) / pWhite->width, CBoxHeight(pBox) / pWhite->height, ColorRGBAMultiplyAlpha(0xFFFFFFFF, pGen->pResult->pTooltip->bInheritAlpha ? pGen->chAlpha : 0xFF));
		gfxDrawBox(pBox->lx, pBox->ly, pBox->hx, pBox->hy, UI_GET_Z(), ColorWhite);
	}
	smf_ParseAndDisplay(pBlock, pchTooltip, pBox->lx + ui_TextureAssemblyLeftSize(pAssembly), pBox->ly + ui_TextureAssemblyTopSize(pAssembly), UI_GET_Z(), pGen->pResult->pTooltip->iMaxWidth * s_GenTooltipState.fScale, CBoxHeight(pBox), false, false, false, &s_DefaultAttribs, pGen->pResult->pTooltip->bInheritAlpha ? pGen->chAlpha : 0xFF, NULL, GenSpritePropGetCurrent());
}

static void ui_GenDrawSecondaryTooltips(UIGen *pGen, UIGenSecondaryTooltipGroup *pTooltipGroup, CBox *pBox)
{
	int i;
	S32 iOffset = 0;
	for (i = 0; i < eaSize(&pTooltipGroup->eaSecondaryToolTips); i++)
	{
		CBox tempBox = {0, 0, 0, 0};
		UIGenSecondaryTooltip *pSecondaryTooltip = pTooltipGroup->eaSecondaryToolTips[i];
		UITextureAssembly *pSecondaryAssembly = s_GenTooltipState.eaSecondaryAssemblies[i];
		if (s_GenTooltipState.eapchSecondaryTooltips[i] && !*s_GenTooltipState.eapchSecondaryTooltips[i])
			continue;
		CBoxSetWidth(&tempBox, smfblock_GetMinWidth(s_GenTooltipState.eaSecondaryBlocks[i]) + ui_TextureAssemblyWidth(pSecondaryAssembly) * s_GenTooltipState.fScale);
		CBoxSetHeight(&tempBox, smfblock_GetHeight(s_GenTooltipState.eaSecondaryBlocks[i]) + ui_TextureAssemblyHeight(pSecondaryAssembly) * s_GenTooltipState.fScale);
		if (pTooltipGroup->eStackDirection == UIHorizontal)
		{
			CBoxMoveX(&tempBox, iOffset + pBox->lx);
			CBoxMoveY(&tempBox, pBox->ly);
			iOffset += CBoxWidth(&tempBox) + pTooltipGroup->iSecondarySpacing * s_GenTooltipState.fScale;
			
			// Centered aligned
			if (pTooltipGroup->eAlignment == UINoDirection)
			{
				CBoxMoveY(&tempBox, tempBox.ly - CBoxHeight(&tempBox)/2 + CBoxHeight(pBox)/2);
			}
			// Right aligned (left when flipped)
			else if (!(pTooltipGroup->eAlignment & UIBottom) ^ !s_GenTooltipState.bFlipY)
			{
				CBoxMoveY(&tempBox, pBox->hy - CBoxHeight(&tempBox));
			}
		}
		else
		{
			CBoxMoveX(&tempBox, pBox->lx);
			CBoxMoveY(&tempBox, iOffset + pBox->ly);
			iOffset += CBoxHeight(&tempBox) + pTooltipGroup->iSecondarySpacing * s_GenTooltipState.fScale;

			// Centered aligned
			if (pTooltipGroup->eAlignment == UINoDirection)
			{
				CBoxMoveX(&tempBox, tempBox.lx - CBoxWidth(&tempBox)/2 + CBoxWidth(pBox)/2);
			}
			// Right aligned (left when flipped)
			else if (!(pTooltipGroup->eAlignment & UIRight) ^ !s_GenTooltipState.bFlipX)
			{
				CBoxMoveX(&tempBox, pBox->hx - CBoxWidth(&tempBox));
			}
		}
		ui_GenDrawTooltipEx(pGen, s_GenTooltipState.eaSecondaryBlocks[i], s_GenTooltipState.eapchSecondaryTooltips[i], &tempBox, pSecondaryAssembly);
	}
}

static void GenTooltipCreateText(UIGen *pGen, ExprContext *pContext, SMFBlock **ppBlock, unsigned char **ppchResult, Message *pMessage, Expression *pExpr, bool bFilterProfanity)
{
	estrClear(ppchResult);
	if (pExpr)
	{
		MultiVal mv = {0};
		exprEvaluate(pExpr, pContext, &mv);
		MultiValToEString(&mv, ppchResult);
	}
	else if (pMessage)
	{
		const char *pchText = TranslateMessagePtr(pMessage);
		exprFormat(ppchResult, pchText, pContext, pGen->pchFilename);
	}
	if (*ppBlock == NULL)
		*ppBlock = smfblock_Create();
}

bool ui_GenTooltipFormatBlocks(UIGen *pGen, UIGenTooltip *pTooltip, CBox *pPrimaryBox) 
{
	int i;
	ExprContext *pContext = ui_GenGetContext(pGen);
	UIStyleFont *pFont = GET_REF(s_GenTooltipState.hTooltipFont);
	bool bFilterProfanity = g_bUIGenFilterProfanityThisFrame;
	if (!pFont)
	{
		SET_HANDLE_FROM_STRING("UIStyleFont", "Tooltip", s_GenTooltipState.hTooltipFont);
		pFont = GET_REF(s_GenTooltipState.hTooltipFont);
	}

	GenTooltipCreateText(pGen, pContext, &s_GenTooltipState.pBlock, &s_GenTooltipState.pchTooltip, GET_REF(pTooltip->hTooltip), pTooltip->pTooltipExpr, pTooltip->bFilterProfanity && bFilterProfanity);
	eaSetSize(&s_GenTooltipState.eaSecondaryBlocks, eaSize(&pTooltip->secondaryTooltipGroup.eaSecondaryToolTips));
	eaSetSize(&s_GenTooltipState.eapchSecondaryTooltips, eaSize(&pTooltip->secondaryTooltipGroup.eaSecondaryToolTips));
	eaSetSize(&s_GenTooltipState.eaSecondaryAssemblies, eaSize(&pTooltip->secondaryTooltipGroup.eaSecondaryToolTips));
	for (i = 0; i < eaSize(&pTooltip->secondaryTooltipGroup.eaSecondaryToolTips); i++)
	{
		UIGenSecondaryTooltip *pSecondaryTooltip = pTooltip->secondaryTooltipGroup.eaSecondaryToolTips[i];
		s_GenTooltipState.eaSecondaryAssemblies[i] = ui_GenTooltipGetAssembly(pGen, pSecondaryTooltip->pchAssembly, true);
		GenTooltipCreateText(pGen, pContext, &s_GenTooltipState.eaSecondaryBlocks[i], &s_GenTooltipState.eapchSecondaryTooltips[i],  GET_REF(pSecondaryTooltip->hTooltip), pSecondaryTooltip->pTooltipExpr, pTooltip->bFilterProfanity && bFilterProfanity);
	}

	if (!(s_GenTooltipState.pchTooltip && s_GenTooltipState.pchTooltip[0]))
		return false;

	if (pFont)
	{
		s_DefaultAttribs.ppBold = U32_TO_PTR(pFont->bBold);
		s_DefaultAttribs.ppItalic = U32_TO_PTR(pFont->bItalic);
		if (GET_REF(pFont->hFace))
			s_DefaultAttribs.ppFace = (void *)GET_REF(pFont->hFace);
		s_DefaultAttribs.ppShadow = U32_TO_PTR(pFont->iShadowOffset);
		s_DefaultAttribs.ppOutline = U32_TO_PTR(pFont->iOutlineWidth);
		s_DefaultAttribs.ppOutlineColor = U32_TO_PTR(ui_StyleColorPaletteIndex(pFont->uiOutlineColor));
		if (pFont->uiColor)
			s_DefaultAttribs.ppColorTop = s_DefaultAttribs.ppColorBottom = U32_TO_PTR(ui_StyleColorPaletteIndex(pFont->uiColor));
		else
		{
			s_DefaultAttribs.ppColorTop = U32_TO_PTR(ui_StyleColorPaletteIndex(pFont->uiTopLeftColor));
			s_DefaultAttribs.ppColorBottom= U32_TO_PTR(ui_StyleColorPaletteIndex(pFont->uiBottomLeftColor));
		}
		s_DefaultAttribs.ppSnapToPixels = U32_TO_PTR(pFont->bDontSnapToPixels ? 0 : 1);
	}
	s_DefaultAttribs.ppScale = U32_TO_PTR((U32)(s_GenTooltipState.fScale * SMF_FONT_SCALE));

	smf_ParseAndFormat(s_GenTooltipState.pBlock, s_GenTooltipState.pchTooltip, pPrimaryBox->lx, pPrimaryBox->ly, UI_GET_Z(), pTooltip->iMaxWidth * s_GenTooltipState.fScale, 100000.f, false, false, pTooltip->bSafeMode, &s_DefaultAttribs);
	for (i = 0; i < eaSize(&s_GenTooltipState.eaSecondaryBlocks); i++)
	{
		UIGenSecondaryTooltip *pSecondaryTooltip = eaGet(&pTooltip->secondaryTooltipGroup.eaSecondaryToolTips, i);
		bool bSafeMode = pSecondaryTooltip && pSecondaryTooltip->bSafeMode;
		smf_ParseAndFormat(s_GenTooltipState.eaSecondaryBlocks[i], s_GenTooltipState.eapchSecondaryTooltips[i], pPrimaryBox->lx, pPrimaryBox->ly, UI_GET_Z(), pTooltip->iMaxWidth * s_GenTooltipState.fScale, 100000.f, false, false, bSafeMode, &s_DefaultAttribs);
	}
	return true;
}

void ui_GenTooltipPrimaryDimensions(UIGenTooltip *pTooltip, CBox *pPrimaryBox, UITextureAssembly *pAssembly) 
{
	CBoxSetHeight(pPrimaryBox, smfblock_GetHeight(s_GenTooltipState.pBlock) + ui_TextureAssemblyHeight(pAssembly) * s_GenTooltipState.fScale);
	CBoxSetWidth(pPrimaryBox, MIN(CBoxWidth(pPrimaryBox), smfblock_GetMinWidth(s_GenTooltipState.pBlock)));
	CBoxSetWidth(pPrimaryBox, CBoxWidth(pPrimaryBox) + ui_TextureAssemblyWidth(pAssembly) * s_GenTooltipState.fScale);
}

void ui_GenTooltipSecondaryDimensions(UIGenTooltip *pTooltip, CBox *pSecondaryBox) 
{
	int i;
	S32 iNonemptySecondaryTooltips = 0;
	for (i = 0; i < eaSize(&s_GenTooltipState.eaSecondaryBlocks); i++)
	{
		UITextureAssembly *pSecondaryAssembly = s_GenTooltipState.eaSecondaryAssemblies[i];
		if (s_GenTooltipState.eapchSecondaryTooltips[i] && !*s_GenTooltipState.eapchSecondaryTooltips[i])
			continue;
		iNonemptySecondaryTooltips++;
		if (pTooltip->secondaryTooltipGroup.eStackDirection == UIHorizontal)
		{
			CBoxSetWidth(pSecondaryBox, CBoxWidth(pSecondaryBox) + smfblock_GetMinWidth(s_GenTooltipState.eaSecondaryBlocks[i]) + ui_TextureAssemblyWidth(pSecondaryAssembly) * s_GenTooltipState.fScale);
			CBoxSetHeight(pSecondaryBox, MAX(CBoxHeight(pSecondaryBox), smfblock_GetHeight(s_GenTooltipState.eaSecondaryBlocks[i]) + ui_TextureAssemblyHeight(pSecondaryAssembly) * s_GenTooltipState.fScale));
		}
		else
		{
			CBoxSetWidth(pSecondaryBox, MIN(MAX(CBoxWidth(pSecondaryBox), smfblock_GetMinWidth(s_GenTooltipState.eaSecondaryBlocks[i]) + ui_TextureAssemblyWidth(pSecondaryAssembly) * s_GenTooltipState.fScale), pTooltip->iMaxWidth * s_GenTooltipState.fScale + ui_TextureAssemblyWidth(pSecondaryAssembly) * s_GenTooltipState.fScale));
			CBoxSetHeight(pSecondaryBox, CBoxHeight(pSecondaryBox) + smfblock_GetHeight(s_GenTooltipState.eaSecondaryBlocks[i]) + ui_TextureAssemblyHeight(pSecondaryAssembly) * s_GenTooltipState.fScale);
		}
	}
	// Apply primary and secondary spacing
	if (eaSize(&s_GenTooltipState.eaSecondaryBlocks) && iNonemptySecondaryTooltips > 0)
	{
		F32 *pfSecondary;
		if (pTooltip->secondaryTooltipGroup.eStackDirection == UIHorizontal)
		{
			pfSecondary = &pSecondaryBox->hx;
		}
		else 
		{
			pfSecondary = &pSecondaryBox->hy;
		}
		*pfSecondary += pTooltip->secondaryTooltipGroup.iSecondarySpacing * (iNonemptySecondaryTooltips - 1) * s_GenTooltipState.fScale;
	}
}

void ui_GenTooltipPrimaryRelative(UIGen * pGen, UIGenTooltip *pTooltip, CBox *pPrimaryBox, CBox *pSecondaryBox) 
{
	CBox container = {0};
	F32 dx, dy;
	if (pTooltip->secondaryTooltipGroup.eOrientation == UIHorizontal)
	{
		if (pTooltip->secondaryTooltipGroup.eAlignment == UITop)
		{
			CBoxSetX(&container, pPrimaryBox->lx, CBoxWidth(pPrimaryBox) + CBoxWidth(pSecondaryBox) + pTooltip->secondaryTooltipGroup.iPrimarySpacing);
			CBoxSetY(&container, pPrimaryBox->ly, MAX(CBoxHeight(pPrimaryBox), CBoxHeight(pSecondaryBox)));
		}
		else if (pTooltip->secondaryTooltipGroup.eAlignment == UIBottom)
		{
			F32 h1 = CBoxHeight(pPrimaryBox);
			F32 h2 = CBoxHeight(pSecondaryBox);
			CBoxSetX(&container, pPrimaryBox->lx, CBoxWidth(pPrimaryBox) + CBoxWidth(pSecondaryBox) + pTooltip->secondaryTooltipGroup.iPrimarySpacing);
			if (h1 >= h2)
				CBoxSetY(&container, pPrimaryBox->ly, h1);
			else
				CBoxSetY(&container, pPrimaryBox->hy - h2, h2);
		}
		else
		{
			F32 h1 = CBoxHeight(pPrimaryBox);
			F32 h2 = CBoxHeight(pSecondaryBox);
			CBoxSetX(&container, pPrimaryBox->lx, CBoxWidth(pPrimaryBox) + CBoxWidth(pSecondaryBox) + pTooltip->secondaryTooltipGroup.iPrimarySpacing);
			if (h1 >= h2)
				CBoxSetY(&container, pPrimaryBox->ly, h1);
			else
				CBoxSetY(&container, pPrimaryBox->ly - (h2-h1)/2, h2);
		}
	}
	else
	{
		if (pTooltip->secondaryTooltipGroup.eAlignment == UILeft)
		{
			CBoxSetX(&container, pPrimaryBox->lx, MAX(CBoxWidth(pPrimaryBox), CBoxWidth(pSecondaryBox)));
			CBoxSetY(&container, pPrimaryBox->ly, CBoxHeight(pPrimaryBox) + CBoxHeight(pSecondaryBox) + pTooltip->secondaryTooltipGroup.iPrimarySpacing);
		}
		else if (pTooltip->secondaryTooltipGroup.eAlignment == UIRight)
		{
			F32 w1 = CBoxWidth(pPrimaryBox);
			F32 w2 = CBoxWidth(pSecondaryBox);
			if (w1 >= w2)
				CBoxSetX(&container, pPrimaryBox->lx, w1);
			else
				CBoxSetX(&container, pPrimaryBox->hx - w2, w2);
			CBoxSetY(&container, pPrimaryBox->ly, CBoxHeight(pPrimaryBox) + CBoxHeight(pSecondaryBox) + pTooltip->secondaryTooltipGroup.iPrimarySpacing);
		}
		else
		{
			F32 w1 = CBoxWidth(pPrimaryBox);
			F32 w2 = CBoxWidth(pSecondaryBox);
			if (w1 >= w2)
				CBoxSetX(&container, pPrimaryBox->lx, w1);
			else
				CBoxSetX(&container, pPrimaryBox->lx - (w2-w1)/2, w2);
			CBoxSetY(&container, pPrimaryBox->ly, CBoxHeight(pPrimaryBox) + CBoxHeight(pSecondaryBox) + pTooltip->secondaryTooltipGroup.iPrimarySpacing);
		}
	}
	ui_GenHandleRelativeTooltipOffset(pGen, &container);
	dx = s_GenTooltipState.bFlipX ? container.hx - CBoxWidth(pPrimaryBox) : (container.lx - pPrimaryBox->lx);
	dy = s_GenTooltipState.bFlipY ? container.hy - CBoxHeight(pPrimaryBox) : (container.ly - pPrimaryBox->ly);
	pPrimaryBox->lx += dx;
	pPrimaryBox->ly += dy;
	pPrimaryBox->hx += dx;
	pPrimaryBox->hy += dy;
}

void ui_GenTooltipAnchorToMouse(CBox *pPrimaryBox, S32 iMouseX, S32 iMouseY)
{
	UIDeviceState *pDevice = ui_StateForDevice(g_ui_State.device);

	S32 iCursorWidth = pDevice && pDevice->cursor.base ? pDevice->cursor.base->width : 0;
	S32 iHotSpotX = pDevice ? pDevice->cursor.hotX : 0;
	S32 iHotSpotY = pDevice ? pDevice->cursor.hotY : 0;

	CBoxMoveX(pPrimaryBox, iMouseX + iCursorWidth - iHotSpotX);
	CBoxMoveY(pPrimaryBox, iMouseY + iHotSpotY);
	if (pPrimaryBox->right > g_ui_State.screenWidth)
	{
		CBoxMoveX(pPrimaryBox, iMouseX - CBoxWidth(pPrimaryBox));
	}
	if (pPrimaryBox->bottom > g_ui_State.screenHeight)
	{
		CBoxMoveY(pPrimaryBox, iMouseY - CBoxHeight(pPrimaryBox));
	}
}

void ui_GenTooltipInitialPositioning(UIGen *pGen, UIGenTooltip *pTooltip, CBox *pPrimaryBox, CBox *pSecondaryBox, S32 iMouseX, S32 iMouseY)
{
	F32 fHorizontalSpacing = pTooltip->secondaryTooltipGroup.eOrientation == UIHorizontal ? pTooltip->secondaryTooltipGroup.iPrimarySpacing + CBoxWidth(pSecondaryBox) : 0;
	F32 fVerticalSpacing = pTooltip->secondaryTooltipGroup.eOrientation == UIVertical ? pTooltip->secondaryTooltipGroup.iPrimarySpacing + CBoxHeight(pSecondaryBox) : 0;

	// X
	if (pTooltip->pRelative ? s_GenTooltipState.bFlipX : pPrimaryBox->lx < 0)
	{
		s_GenTooltipState.bFlipX = 
			(iMouseX + CBoxWidth(pPrimaryBox) + fHorizontalSpacing > g_ui_State.screenWidth - UI_STEP)
			&& (iMouseX > g_ui_State.screenWidth / 2);
		if (s_GenTooltipState.bFlipX)
			CBoxMoveX(pPrimaryBox, iMouseX - CBoxWidth(pPrimaryBox));
		else
			CBoxMoveX(pPrimaryBox, iMouseX);
	}

	// Y
	if (pTooltip->pRelative ? s_GenTooltipState.bFlipY : pPrimaryBox->ly < 0)
	{
		s_GenTooltipState.bFlipY = 
			(pGen->ScreenBox.hy + CBoxHeight(pPrimaryBox) + fVerticalSpacing + UI_DSTEP > g_ui_State.screenHeight - UI_STEP)
			&& (iMouseY > g_ui_State.screenHeight / 2);
		if (s_GenTooltipState.bFlipY)
			CBoxMoveY(pPrimaryBox, pGen->ScreenBox.ly - UI_DSTEP - CBoxHeight(pPrimaryBox));
		else
			CBoxMoveY(pPrimaryBox, pGen->ScreenBox.hy + UI_DSTEP);
	}
}

void ui_GenTooltipPrimaryLocation(UIGen *pGen, UIGenTooltip *pTooltip, CBox *pPrimaryBox, CBox *pSecondaryBox) 
{
	S32 iMouseX = g_ui_State.mouseX;
	S32 iMouseY = g_ui_State.mouseY;

	if (!point_cbox_clsn(iMouseX, iMouseY, &pGen->UnpaddedScreenBox))
	{
		// If the mouse isn't in the box, try to pick a good fake position for it.
		if (pGen->UnpaddedScreenBox.lx < g_ui_State.screenWidth / 2)
			iMouseX = pGen->UnpaddedScreenBox.lx + CBoxWidth(&pGen->UnpaddedScreenBox) * 0.05;
		else
			iMouseX = pGen->UnpaddedScreenBox.hx - CBoxWidth(&pGen->UnpaddedScreenBox) * 0.05;
		iMouseY = pGen->UnpaddedScreenBox.hy + CBoxHeight(&pGen->UnpaddedScreenBox) * 0.05;
	}

	if (pTooltip->pRelative && pPrimaryBox->lx < 0)
		ui_GenTooltipPrimaryRelative(pGen, pTooltip, pPrimaryBox, pSecondaryBox);
	if (pTooltip->bMouseAnchor)
		ui_GenTooltipAnchorToMouse(pPrimaryBox, iMouseX, iMouseY);
	if (pPrimaryBox->lx < 0 || pPrimaryBox->ly < 0)
		ui_GenTooltipInitialPositioning(pGen, pTooltip, pPrimaryBox, pSecondaryBox, iMouseX, iMouseY);
}

void ui_GenTooltipSecondaryLocation(UIGenTooltip *pTooltip, CBox *pPrimaryBox, CBox *pSecondaryBox) 
{
	// X
	if (pTooltip->secondaryTooltipGroup.eOrientation == UIHorizontal)
	{
		if (s_GenTooltipState.bFlipX)
			CBoxMoveX(pSecondaryBox, pPrimaryBox->lx - CBoxWidth(pSecondaryBox) - pTooltip->secondaryTooltipGroup.iPrimarySpacing);
		else
			CBoxMoveX(pSecondaryBox, pPrimaryBox->hx + pTooltip->secondaryTooltipGroup.iPrimarySpacing);
	}
	else
	{
		S32 iX;
		if (pTooltip->secondaryTooltipGroup.eStackAlignment == UILeft)
			iX = pPrimaryBox->lx;
		else if (pTooltip->secondaryTooltipGroup.eStackAlignment == UIRight)
			iX = pPrimaryBox->hx - CBoxWidth(pSecondaryBox);
		else 
			iX = pPrimaryBox->lx + CBoxWidth(pPrimaryBox)/2 - CBoxWidth(pSecondaryBox)/2;

		CBoxMoveX(pSecondaryBox, iX);
	}

	// Y
	if (pTooltip->secondaryTooltipGroup.eOrientation == UIVertical)
	{
		if (s_GenTooltipState.bFlipY)
			CBoxMoveY(pSecondaryBox, pPrimaryBox->ly - CBoxHeight(pSecondaryBox) - pTooltip->secondaryTooltipGroup.iPrimarySpacing);
		else
			CBoxMoveY(pSecondaryBox, pPrimaryBox->hy + pTooltip->secondaryTooltipGroup.iPrimarySpacing);
	}
	else
	{
		S32 iY;
		if (pTooltip->secondaryTooltipGroup.eStackAlignment == UITop)
			iY = pPrimaryBox->ly;
		else if (pTooltip->secondaryTooltipGroup.eStackAlignment == UIBottom)
			iY = pPrimaryBox->hy - CBoxHeight(pSecondaryBox);
		else 
			iY = pPrimaryBox->ly + CBoxHeight(pPrimaryBox)/2 - CBoxHeight(pSecondaryBox)/2;

		CBoxMoveY(pSecondaryBox, iY);
	}
}

// Returns true if it needed to be pushed back on screen
// false otherise
bool ui_GenTooltipForceOnscreen(UIGenTooltip *pTooltip, CBox *pPrimaryBox, CBox *pSecondaryBox) 
{
	bool retVal = false;
	if (pTooltip->secondaryTooltipGroup.eOrientation == UIHorizontal)
	{
		if (pPrimaryBox->hx > g_ui_State.screenWidth || pSecondaryBox->hx > g_ui_State.screenWidth)
		{
			F32 iPushback = (pPrimaryBox->hx > pSecondaryBox->hx) ? pPrimaryBox->hx - g_ui_State.screenWidth : pSecondaryBox->hx - g_ui_State.screenWidth;
			CBoxMoveX(pPrimaryBox, pPrimaryBox->lx - iPushback);
			CBoxMoveX(pSecondaryBox, pSecondaryBox->lx - iPushback);
			retVal = true;
		}
		if (pPrimaryBox->lx < 0 || pSecondaryBox->lx < 0)
		{
			F32 iPushback = MIN(pPrimaryBox->lx, pSecondaryBox->lx);
			CBoxMoveX(pPrimaryBox, pPrimaryBox->lx - iPushback);
			CBoxMoveX(pSecondaryBox, pSecondaryBox->lx - iPushback);
			retVal = true;
		}

		if (pPrimaryBox->hy > g_ui_State.screenHeight)
			CBoxMoveY(pPrimaryBox, g_ui_State.screenHeight - CBoxHeight(pPrimaryBox));
		if (pSecondaryBox->hy > g_ui_State.screenHeight)
			CBoxMoveY(pSecondaryBox, g_ui_State.screenHeight - CBoxHeight(pSecondaryBox));
		if (pPrimaryBox->ly < 0)
			CBoxMoveY(pPrimaryBox, 0);
		if (pSecondaryBox->ly < 0)
			CBoxMoveY(pSecondaryBox, 0);
		retVal |= (pPrimaryBox->hy > g_ui_State.screenHeight) || (pSecondaryBox->hy > g_ui_State.screenHeight) || (pPrimaryBox->ly < 0) || (pSecondaryBox->ly < 0);
	}
	else
	{
		if (pPrimaryBox->hx > g_ui_State.screenWidth)
			CBoxMoveX(pPrimaryBox, g_ui_State.screenWidth - CBoxWidth(pPrimaryBox));
		if (pSecondaryBox->hx > g_ui_State.screenWidth)
			CBoxMoveX(pSecondaryBox, g_ui_State.screenWidth - CBoxWidth(pSecondaryBox));
		if (pPrimaryBox->lx < 0)
			CBoxMoveX(pPrimaryBox, 0);
		if (pSecondaryBox->lx < 0)
			CBoxMoveX(pSecondaryBox, 0);
		retVal |= (pPrimaryBox->hx > g_ui_State.screenWidth) || (pSecondaryBox->hx > g_ui_State.screenWidth) || (pPrimaryBox->lx < 0) || (pSecondaryBox->lx < 0);

		if (pPrimaryBox->hy > g_ui_State.screenHeight || pSecondaryBox->hx > g_ui_State.screenHeight)
		{
			F32 iPushback = (pPrimaryBox->hy > pSecondaryBox->hy) ? pPrimaryBox->hy - g_ui_State.screenHeight: pSecondaryBox->hy - g_ui_State.screenHeight;
			CBoxMoveY(pPrimaryBox, pPrimaryBox->ly - iPushback);
			CBoxMoveY(pSecondaryBox, pSecondaryBox->ly - iPushback);
			retVal = true;
		}
		if (pPrimaryBox->ly < 0 || pSecondaryBox->ly < 0)
		{
			F32 iPushback = MIN(pPrimaryBox->ly, pSecondaryBox->ly);
			CBoxMoveY(pPrimaryBox, pPrimaryBox->ly - iPushback);
			CBoxMoveY(pSecondaryBox, pSecondaryBox->ly - iPushback);
			retVal = true;
		}
	}
	return retVal;
}

// Returns true if the tooltips were repositioned as a result of obscuring thier source gen
// false otherwise. 
bool ui_GenTooltipUnobscureSource(UIGen *pGen, UIGenTooltip *pTooltip, CBox *pPrimaryBox, CBox *pSecondaryBox) 
{
	// Tooltips will obscure their source by design if they're being anchored to it
	if (!pTooltip->bMouseAnchor)
	{
		CBox *pIntersectingBox = NULL;
		if (CBoxIntersects(&pGen->ScreenBox, pPrimaryBox))
			pIntersectingBox = pPrimaryBox;
		if (CBoxIntersects(&pGen->ScreenBox, pSecondaryBox))
			pIntersectingBox = pSecondaryBox;
		if (pIntersectingBox)
		{
			CBox *pTallerBox;
			F32 fOffset;
			if (CBoxHeight(pPrimaryBox) > CBoxHeight(pSecondaryBox))
				pTallerBox = pPrimaryBox;
			else 
				pTallerBox = pSecondaryBox;

			fOffset = pGen->ScreenBox.hy - pIntersectingBox->ly;
			if (pTallerBox->hy + fOffset + UI_DSTEP < g_ui_State.screenHeight)
			{
				CBoxMoveY(pPrimaryBox, pPrimaryBox->ly + fOffset + UI_DSTEP);
				CBoxMoveY(pSecondaryBox, pSecondaryBox->ly + fOffset + UI_DSTEP);
			}
			else
			{
				fOffset = pIntersectingBox->hy - pGen->ScreenBox.ly;
				if (pTallerBox->hy - fOffset - UI_DSTEP > 0)
				{
					CBoxMoveY(pPrimaryBox, pPrimaryBox->ly - fOffset - UI_DSTEP);
					CBoxMoveY(pSecondaryBox, pSecondaryBox->ly - fOffset - UI_DSTEP);
				}
			}
			return true;
		}
	}
	return false;
}

void ui_GenTooltipDraw(void)
{
	UIGen *pGen = s_GenTooltipState.pCurrentGen;

	if (UI_GEN_READY(pGen))
		s_GenTooltipState.fTimer -= g_ui_State.timestep;

	if (UI_GEN_READY(pGen) && pGen->uiFrameLastUpdate != g_ui_State.uiFrameCount)
	{
		ErrorFilenamef(pGen->pchFilename,
			"%s: Displaying a tooltip without updating this frame.",
			pGen->pchName);
		return;
	}

	if (UI_GEN_READY(pGen) && s_GenTooltipState.fTimer <= 0)
	{
		UIGenTooltip *pTooltip = SAFE_MEMBER2(pGen, pResult, pTooltip);
		UITextureAssembly *pAssembly = pTooltip ? ui_GenTooltipGetAssembly(pGen, pTooltip->pchAssembly, false) : NULL;
		CBox primaryBox; // The box containing the primary tooltip
		CBox secondaryBox; // The box containing the secondary tooltips

		if (!pTooltip)
			return;

		s_GenTooltipState.fScale = pTooltip->bInheritScale ? pGen->fScale : g_GenState.fScale;

		BuildCBox(&primaryBox, s_GenTooltipState.fPrimaryX, s_GenTooltipState.fPrimaryY, pTooltip->iMaxWidth * s_GenTooltipState.fScale, 0);
		BuildCBox(&secondaryBox, s_GenTooltipState.fSecondaryX, s_GenTooltipState.fSecondaryY * s_GenTooltipState.fScale, 0, 0);
		s_GenTooltipState.pLastParent = pGen->pParent;

		if (!ui_GenTooltipFormatBlocks(pGen, pTooltip, &primaryBox))
		{
			// if formatting failed, return.
			return;
		}
		ui_GenTooltipPrimaryDimensions(pTooltip, &primaryBox, pAssembly);
		ui_GenTooltipSecondaryDimensions(pTooltip, &secondaryBox);
		ui_GenTooltipPrimaryLocation(pGen, pTooltip, &primaryBox, &secondaryBox);
		ui_GenTooltipSecondaryLocation(pTooltip, &primaryBox, &secondaryBox);
		ui_GenTooltipForceOnscreen(pTooltip, &primaryBox, &secondaryBox);
		if (ui_GenTooltipUnobscureSource(pGen, pTooltip, &primaryBox, &secondaryBox))
		{
			// If it was obscuring something, double check that it's not offscreen
			ui_GenTooltipForceOnscreen(pTooltip, &primaryBox, &secondaryBox); 
		}
		ui_GenDrawTooltipEx(pGen, s_GenTooltipState.pBlock, s_GenTooltipState.pchTooltip, &primaryBox, pAssembly);
		ui_GenDrawSecondaryTooltips(pGen, &pTooltip->secondaryTooltipGroup, &secondaryBox);
		
		s_GenTooltipState.fPrimaryX = primaryBox.lx;
		s_GenTooltipState.fPrimaryY = primaryBox.ly;
		s_GenTooltipState.fSecondaryX = secondaryBox.lx;
		s_GenTooltipState.fSecondaryY = secondaryBox.ly;
	}
	else
	{
		int i;
		s_GenTooltipState.fPrimaryX = -1;
		s_GenTooltipState.fPrimaryY = -1;
		s_GenTooltipState.fSecondaryX = -1;
		s_GenTooltipState.fSecondaryY = -1;
		s_GenTooltipState.pLastParent = NULL;
		smfblock_Destroy(s_GenTooltipState.pBlock);
		for (i = 0; i < eaSize(&s_GenTooltipState.eaSecondaryBlocks); i++)
		{
			smfblock_Destroy(s_GenTooltipState.eaSecondaryBlocks[i]);
			estrClear(&s_GenTooltipState.eapchSecondaryTooltips[i]);
		}
		eaClear(&s_GenTooltipState.eaSecondaryBlocks);
		eaClear(&s_GenTooltipState.eapchSecondaryTooltips);
		s_GenTooltipState.pBlock = NULL;
	}
	s_GenTooltipState.pLastGen = pGen;
}