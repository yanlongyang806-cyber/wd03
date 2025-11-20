#include "estring.h"
#include "MessageExpressions.h"
#include "StringFormat.h"
#include "GfxTexAtlas.h"
#include "GfxSpriteText.h"
#include "GfxSprite.h"
#include "inputKeyBind.h"
#include "UICore_h_ast.h"
#include "UIGen_h_ast.h"
#include "UIGenButton.h"
#include "MemoryPool.h"
#include "inputMouse.h"
#include "UIGenPrivate.h"
#include "Message.h"
#include "AppLocale.h"
#include "UIGenTutorial.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

MP_DEFINE(UIGenButton);
MP_DEFINE(UIGenButtonState);

bool ui_GenValidateButton(UIGen *pGen, UIGenInternal *pInt, const char *pchDescriptor)
{
	//UIGenButton *pBase = (UIGenButton*)pGen->pBase;
	//UIGenButton *pButton = (UIGenButton*)pInt;
	//UIGenStateDef *pClicked = eaIndexedGetUsingInt(&pGen->eaStates, kUIGenStateMouseClick);

	//UI_GEN_WARN_IF(pGen, pButton->pOnClicked && SAFE_MEMBER(pClicked, pOnEnter),
	//	"Gen has both OnClicked action and StateDef MouseClick OnEnter action. This is no longer valid.");

	//if (pButton->pOnClicked)
	//{
	//	UI_GEN_FAIL_IF_RETURN(pGen, stricmp(pchDescriptor, "base"), 0, 
	//		"Gen has OnClicked outside of the Base. This is no longer valid.");
	//	if (!pClicked)
	//	{
	//		pClicked = StructCreate(parse_UIGenStateDef);
	//		pClicked->eState = kUIGenStateMouseClick;
	//		eaPush(&pGen->eaStates, pClicked);
	//	}
	//	pClicked->pOnEnter = pButton->pOnClicked;
	//	pButton->pOnClicked = NULL;
	//}

	return true;
}

void ui_GenButtonClick(UIGen *pGen)
{
	UIGenButton *pButton = UI_GEN_RESULT(pGen, Button);
	UIGenButtonState *pState = UI_GEN_STATE(pGen, Button);
	if (pButton)
	{
		if (pButton->pOnClicked)
		{
			ui_GenRunAction(pGen, pButton->pOnClicked);
		}
		else
		{
			ui_GenSetState(pGen, kUIGenStateMouseClick);
			ui_GenSetState(pGen, kUIGenStateLeftMouseClick);
		}
	}

	if (pGen->pTutorialInfo)
		ui_GenTutorialEvent(NULL, pGen, kUIGenTutorialEventTypeClick);
}

void ui_GenTickEarlyButton(UIGen *pGen)
{
	UIGenButton *pButton = UI_GEN_RESULT(pGen, Button);
	UIGenButtonState *pState = UI_GEN_STATE(pGen, Button);

	if (mouseDownHit(MS_LEFT, &pGen->UnpaddedScreenBox))
		pState->bPressed = true;
	if (pState->bPressed && mouseUpHit(MS_LEFT, &pGen->UnpaddedScreenBox))
		ui_GenButtonClick(pGen);

	if (!mouseIsDown(MS_LEFT))
		pState->bPressed = false;

	ui_GenState(pGen, kUIGenStatePressed, pState->bPressed && ui_GenInState(pGen, kUIGenStateMouseOver));
}

static const char *ui_ButtonGetTexture(UIGen *pGen, UIGenButton *pButton, UIGenButtonState *pState)
{
	if (pButton->pchTexture && strchr(pButton->pchTexture, STRFMT_TOKEN_START))
	{
		static char *s_pchTexture = NULL;
		estrClear(&s_pchTexture);
		exprFormat(&s_pchTexture, pButton->pchTexture, ui_GenGetContext(pGen), pGen->pchFilename);
		return s_pchTexture;
	}
	return pButton->pchTexture;
}

void ui_GenUpdateButton(UIGen *pGen)
{
	UIGenButton *pButton = UI_GEN_RESULT(pGen, Button);
	UIGenButtonState *pState = UI_GEN_STATE(pGen, Button);
	const char *pchTexture;
	if (ui_GenBundleTextGetText(pGen, &pButton->TextBundle, NULL, &pState->pchString))
	{
		pState->v2TextSize[0] = pState->v2TextSize[1] = -1;
	}
	pchTexture = ui_ButtonGetTexture(pGen, pButton, pState);
	if (pButton->bSkinningOverride)
		pchTexture = ui_GenGetTextureSkinOverride(pchTexture);
	UI_GEN_LOAD_TEXTURE(pchTexture, pState->pTex);
}

void ui_GenDrawEarlyButton(UIGen *pGen)
{
	static unsigned char *s_estrText;
	UIGenButton *pButton = UI_GEN_RESULT(pGen, Button);
	UIGenButtonState *pState = UI_GEN_STATE(pGen, Button);
	AtlasTex *pTex = pState->pTex;
	F32 fLeft = pGen->ScreenBox.lx;
	F32 fRight = pGen->ScreenBox.hx;
	F32 fTop = pGen->ScreenBox.ly;
	F32 fBottom = pGen->ScreenBox.hy;
	F32 fCenterX;
	F32 fCenterY;
	F32 fTextWidth = 0;
	F32 fTextHeight = 0;
	F32 fScale = pGen->fScale;
	F32 fTextScale = fScale;
	F32 fTextureZ = UI_GET_Z();
	F32 fTextZ = UI_GET_Z();
	U32 iColor = ColorRGBAMultiplyAlpha(ui_StyleColorPaletteIndex(pButton->uiColor), pGen->chAlpha);
	F32 fSpacing = pButton->chSpacing * fScale;
	const char *pchText = pState->pchString;
	UIStyleFont *pFont = pchText && *pchText ? ui_GenBundleTextGetFont(&pButton->TextBundle) : NULL;

	fCenterY = (fTop + fBottom) / 2;
	fCenterX = (fLeft + fRight) / 2;

	if (pTex)
	{
		CBox box;
		F32 fTextureWidth = (pButton->iTextureWidth ? pButton->iTextureWidth : pTex->width) * fScale;
		F32 fTextureHeight = (pButton->iTextureHeight ? pButton->iTextureHeight : pTex->height) * fScale;
		switch (pButton->eTextureAlignment)
		{
		case UITop:
			BuildCBox(&box, fCenterX - fTextureWidth / 2, fTop, fTextureWidth, fTextureHeight);
			fTop += fTextureHeight + fSpacing;
			break;
		case UIBottom:
			BuildCBox(&box, fCenterX - fTextureWidth / 2, fBottom - fTextureHeight, fTextureWidth, fTextureHeight);
			fBottom -= fTextureHeight + fSpacing;
			break;
		case UILeft:
			BuildCBox(&box, fLeft, fCenterY - fTextureHeight / 2, fTextureWidth, fTextureHeight);
			fLeft += fTextureWidth + fSpacing;
			break;
		case UIRight:
			BuildCBox(&box, fRight - fTextureWidth, fCenterY - fTextureHeight / 2, fTextureWidth, fTextureHeight);
			fRight -= fTextureWidth + fSpacing;
			break;
		default:
			BuildCBox(&box, fCenterX - fTextureWidth / 2, fCenterY - fTextureHeight / 2, fTextureWidth, fTextureHeight);
			break;
		}
		display_sprite_box(pTex, &box, fTextureZ, iColor);
	}

	if (ui_GenBundleTruncate(&pState->Truncate, pFont, GET_REF(pButton->hTruncate), fRight - fLeft, fScale, pchText, &s_estrText))
	{
		pchText = s_estrText;
	}

	if (pchText && *pchText)
	{
		ui_StyleFontUse(pFont, false, kWidgetModifier_None);
		ui_StyleFontDimensions(pFont, fScale, pchText, &fTextWidth, &fTextHeight, false);
		if (fTextWidth && fTextHeight)
		{
			if (pButton->TextBundle.bShrinkToFit )
			{
				F32 fXScale = fScale * (fRight - fLeft) / fTextWidth;
				F32 fYScale = fScale * (fBottom - fTop) / fTextHeight;

				// Do our best in crazy conditions.
				if (fXScale * fTextWidth < 1)
					fXScale = fScale;
				if (fYScale * fTextHeight < 1)
					fYScale = fScale;

				fXScale = fYScale = min(fXScale, fYScale);
				if (fXScale < fScale)
				{
					fTextScale = fXScale;
					ui_StyleFontDimensions(pFont, fTextScale, pchText, &fTextWidth, &fTextHeight, false);
				}
			}

			fCenterX = (fLeft + fRight) / 2;
			fCenterY = (fTop + fBottom) / 2;

			gfxfont_MultiplyAlpha(pGen->chAlpha);

			switch (pButton->TextBundle.eAlignment)
			{
			case UITop:
				gfxfont_Print(fCenterX - fTextWidth / 2, fTop + fTextHeight, fTextZ, fTextScale, fTextScale, 0, pchText);
				break;
			case UIBottom:
				gfxfont_Print(fCenterX - fTextWidth / 2, fBottom, fTextZ, fTextScale, fTextScale, 0, pchText);
				break;
			case UILeft:
				gfxfont_Print(fLeft, fCenterY + fTextHeight / 2, fTextZ, fTextScale, fTextScale, 0, pchText);
				break;
			case UIRight:
				gfxfont_Print(fRight - fTextWidth, fCenterY + fTextHeight / 2, fTextZ, fTextScale, fTextScale, 0, pchText);
				break;
			default:
				gfxfont_Print(fCenterX - fTextWidth / 2, fCenterY + fTextHeight / 2, fTextZ, fTextScale, fTextScale, 0, pchText);
				break;
			}
		}
	}
}

void ui_GenFitContentsSizeButton(UIGen *pGen, UIGenButton *pButton, CBox *pOut)
{
	// The native size of a button is the size of its texture,
	// or the size of its text, or the size of both plus some padding,
	// plus the size of the border.
	F32 fTextWidth = 0.f;
	F32 fTextHeight = 0.f;
	F32 fTextureWidth = 0.f;
	F32 fTextureHeight = 0.f;
	F32 fSpacing = UI_STEP;
	F32 fWidth = 0.f;
	F32 fHeight = 0.f;
	UIGenButtonState *pState = UI_GEN_STATE(pGen, Button);
	const char *pchTexture;
	const char *pchText = pState->pchString;

	pchTexture = ui_ButtonGetTexture(pGen, pButton, pState);
	UI_GEN_LOAD_TEXTURE(pchTexture, pState->pTex);

	if (pState->pTex)
	{
		fTextureWidth = pButton->iTextureWidth ? pButton->iTextureWidth : pState->pTex->width;
		fTextureHeight = pButton->iTextureHeight ? pButton->iTextureHeight : pState->pTex->height;
	}

	if (pchText)
	{
		UIStyleFont *pFont = ui_GenBundleTextGetFont(&pButton->TextBundle);
		if (pState->v2TextSize[0] < 0 || pState->pTextFont != pFont)
		{
			fTextWidth = ui_StyleFontWidthNoCache(pFont, 1.f, pchText);
			fTextHeight = ui_StyleFontLineHeight(pFont, 1.f);
			pState->v2TextSize[0] = fTextWidth;
			pState->v2TextSize[1] = fTextHeight;
			pState->pTextFont = pFont;
		}
		else
		{
			fTextWidth = pState->v2TextSize[0];
			fTextHeight = pState->v2TextSize[1];
		}
	}

	if (pButton->eTextureAlignment & (UILeft | UIRight))
	{
		fWidth = fTextWidth + fTextureWidth;
		fHeight = max(fTextHeight, fTextureHeight);
		if (fTextWidth && fTextureWidth)
			fWidth += fSpacing;
	}
	else if (pButton->eTextureAlignment & (UIBottom | UITop))
	{
		fWidth = max(fTextWidth, fTextureWidth);
		fHeight = fTextHeight + fTextureHeight;
		if (fTextHeight && fTextureHeight)
			fHeight += fSpacing;
	}
	else
	{
		fHeight = max(fTextHeight, fTextureHeight);
		fWidth = max(fTextWidth, fTextureWidth);
	}

	if (!pState->pTex && (!pchText || !*pchText))
	{
		Vec2 v2Size = {0, 0};
		ui_GenInternalForEachChild(&pButton->polyp, ui_GenGetBounds, v2Size, UI_GEN_LAYOUT_ORDER);
		fWidth = v2Size[0];
		fHeight = v2Size[1];
	}

	BuildCBox(pOut, 0, 0, fWidth, fHeight);
}

AUTO_RUN;
void ui_GenRegisterButton(void)
{
	MP_CREATE(UIGenButton, 64);
	MP_CREATE(UIGenButtonState, 64);

	ui_GenRegisterType(kUIGenTypeButton, 
		ui_GenValidateButton, 
		UI_GEN_NO_POINTERUPDATE,
		ui_GenUpdateButton, 
		UI_GEN_NO_LAYOUTEARLY, 
		UI_GEN_NO_LAYOUTLATE, 
		ui_GenTickEarlyButton, 
		UI_GEN_NO_TICKLATE, 
		ui_GenDrawEarlyButton,
		ui_GenFitContentsSizeButton, 
		UI_GEN_NO_FITPARENTSIZE, 
		UI_GEN_NO_HIDE, 
		UI_GEN_NO_INPUT, 
		UI_GEN_NO_UPDATECONTEXT, 
		UI_GEN_NO_QUEUERESET);
}

#include "UIGenButton_h_ast.c"
