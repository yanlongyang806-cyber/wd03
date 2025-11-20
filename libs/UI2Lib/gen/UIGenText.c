#include "error.h"
#include "earray.h"
#include "GfxSpriteText.h"
#include "UIGenText.h"
#include "UICore_h_ast.h"
#include "UIGen_h_ast.h"
#include "MemoryPool.h"
#include "UIGenPrivate.h"
#include "GfxSprite.h"
#include "StringUtil.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

MP_DEFINE(UIGenText);
MP_DEFINE(UIGenTextState);

AUTO_RUN;
void UIGenTextInitMemPools(void)
{
	MP_CREATE(UIGenText, 64);
	MP_CREATE(UIGenTextState, 32);
}

void ui_GenUpdateText(UIGen *pGen)
{
	UIGenText *pText = UI_GEN_RESULT(pGen, Text);
	UIGenTextState *pState = UI_GEN_STATE(pGen, Text);
	bool bChanged = false;
	UIStyleFont *pFont = ui_GenBundleTextGetFont(&pText->TextBundle);

	if (ui_GenBundleTextGetText(pGen, &pText->TextBundle, pState->pchStaticString, &pState->pchString))
		bChanged = true;

	if (pState->pTextFont != pFont)
	{
		pState->pTextFont = pFont;
		bChanged = true;
	}

	if (bChanged)
	{
		ui_StyleFontDimensions(pFont, 1.f, NULL_TO_EMPTY(pState->pchString), &pState->v2TextSize[0], &pState->v2TextSize[1], true);
		pState->v2TextSize[0] = roundTiesUp(pState->v2TextSize[0]);
		pState->v2TextSize[1] = roundTiesUp(pState->v2TextSize[1]);

		// Force draw to recalculate cached layout information
		pState->v2DrawAreaSize[0] = pState->v2DrawAreaSize[1] = -1;
		pState->v2DrawFinalSize[0] = pState->v2TextSize[0];
		pState->v2DrawFinalSize[1] = pState->v2TextSize[1];
		estrDestroy(&pState->pchDrawFinalText);
	}
}

void ui_GenDrawEarlyText(UIGen *pGen)
{
	UIGenText *pText = UI_GEN_RESULT(pGen, Text);
	UIGenTextState *pState = UI_GEN_STATE(pGen, Text);
	SpriteProperties *pSpriteProperties = GenSpritePropGetCurrent();
	bool useSpriteProps = pSpriteProperties && pSpriteProperties->is_3D;
	const char *pchText = pState->pchString;
	CBox *pBox = &pGen->ScreenBox;
	F32 fScale = pGen->fScale;
	F32 fX, fY, fWidth, fHeight;
	UIStyleFont *pFont;

	if (!(pchText && *pchText))
		return;

	pFont = ui_GenBundleTextGetFont(&pText->TextBundle);
	fWidth = CBoxWidth(pBox);
	fHeight = CBoxHeight(pBox);

	if (pState->v2DrawAreaSize[0] != fWidth
		|| pState->v2DrawAreaSize[1] != fHeight
		|| pState->fDrawInputScale != fScale
		|| pState->pDrawTextFont != pFont)
	{
		// Only do this on a layout change
		pState->v2DrawAreaSize[0] = fWidth;
		pState->v2DrawAreaSize[1] = fHeight;
		pState->fDrawInputScale = fScale;
		pState->pDrawTextFont = pFont;

		if (pState->v2TextSize[0] * fScale > fWidth)
		{
			if (!ui_GenBundleTruncate(&pState->Truncate, pFont, GET_REF(pText->hTruncate), CBoxWidth(&pGen->ScreenBox), fScale, pState->pchString, &pState->pchDrawFinalText))
				estrDestroy(&pState->pchDrawFinalText);
		}
		else if (pState->pchDrawFinalText)
			estrDestroy(&pState->pchDrawFinalText);

		pchText = FIRST_IF_SET(pState->pchDrawFinalText, pState->pchString);

		if (pText->TextBundle.bScaleToFit || pText->TextBundle.bShrinkToFit)
		{
			F32 fTextWidth = pState->v2TextSize[0];
			F32 fTextHeight = pState->v2TextSize[1];
			F32 fOtherScale;

			// If the string was truncated, use the size of the truncated string.
			if (pchText != pState->pchString)
				ui_StyleFontDimensions(pFont, 1.f, NULL_TO_EMPTY(pchText), &fTextWidth, &fTextHeight, true);

			fScale = CBoxWidth(pBox) / fTextWidth;
			fOtherScale = CBoxHeight(pBox) / fTextHeight;
			fScale = min(fScale, fOtherScale);

			if (pText->TextBundle.bShrinkToFit && fScale > pGen->fScale)
				fScale = pGen->fScale;
		}

		pState->fDrawFinalScale = fScale;

		ui_StyleFontDimensions(pFont, fScale, NULL_TO_EMPTY(pchText), &pState->v2DrawFinalSize[0], &pState->v2DrawFinalSize[1], true);
		pState->v2DrawFinalSize[0] = roundTiesUp(pState->v2DrawFinalSize[0]);
		pState->v2DrawFinalSize[1] = roundTiesUp(pState->v2DrawFinalSize[1]);
	}
	else
	{
		pchText = FIRST_IF_SET(pState->pchDrawFinalText, pState->pchString);
		fScale = pState->fDrawFinalScale;
	}

	if (pText->TextBundle.eAlignment & UILeft)
		fX = pBox->lx;
	else if (pText->TextBundle.eAlignment & UIRight)
		fX = pBox->hx - pState->v2DrawFinalSize[0];
	else
		fX = pBox->lx + (fWidth - pState->v2DrawFinalSize[0]) / 2;

	if (pText->TextBundle.eAlignment & UITop)
		fY = pBox->ly + pState->v2DrawFinalSize[1];
	else if (pText->TextBundle.eAlignment & UIBottom)
		fY = pBox->hy;
	else
		fY = pBox->ly + (fHeight - pState->v2DrawFinalSize[1]) / 2 + pState->v2DrawFinalSize[1];

	ui_StyleFontUse(pFont, false, kWidgetModifier_None);
	gfxfont_MultiplyAlpha(pGen->chAlpha);

	gfxfont_PrintEx(g_font_Active, fX, fY, useSpriteProps ? pSpriteProperties->screen_distance : UI_GET_Z(), fScale, fScale, 0, pchText, UTF8GetLength(pchText), 0, useSpriteProps ? pSpriteProperties : NULL);
}


void ui_GenFitContentsSizeText(UIGen *pGen, UIGenText *pText, CBox *pOut)
{
	UIGenTextState *pState = UI_GEN_STATE(pGen, Text);
	pOut->hx = pState->v2TextSize[0];
	pOut->hy = pState->v2TextSize[1];
}

AUTO_RUN;
void ui_GenRegisterText(void)
{
	ui_GenRegisterType(kUIGenTypeText, 
		UI_GEN_NO_VALIDATE, 
		UI_GEN_NO_POINTERUPDATE,
		ui_GenUpdateText, 
		UI_GEN_NO_LAYOUTEARLY, 
		UI_GEN_NO_LAYOUTLATE, 
		UI_GEN_NO_TICKEARLY, 
		UI_GEN_NO_TICKLATE, 
		ui_GenDrawEarlyText,
		ui_GenFitContentsSizeText, 
		UI_GEN_NO_FITPARENTSIZE, 
		UI_GEN_NO_HIDE, 
		UI_GEN_NO_INPUT, 
		UI_GEN_NO_UPDATECONTEXT, 
		UI_GEN_NO_QUEUERESET);
}

#include "UIGenText_h_ast.c"
