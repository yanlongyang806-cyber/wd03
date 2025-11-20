#include "UITextureAssembly.h"
#include "GfxSprite.h"
#include "GfxSpriteText.h"
#include "UIGen_h_ast.h"
#include "UICore_h_ast.h"

#include "UIGenCheckButton.h"
#include "UIGenPrivate.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

bool ui_GenValidateCheckButton(UIGen *pGen, UIGenInternal *pInt, const char *pchDescriptor)
{
	//UIGenCheckButton *pBase = (UIGenCheckButton*)pGen->pBase;
	//UIGenCheckButton *pButton = (UIGenCheckButton*)pInt;
	//UIGenStateDef *pClicked = eaIndexedGetUsingInt(&pGen->eaStates, kUIGenStateMouseClick);

	//UI_GEN_FAIL_IF_RETURN(pGen, pButton->pOnClicked && SAFE_MEMBER(pClicked, pOnEnter), 0, 
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

void ui_GenTickEarlyCheckButton(UIGen *pGen)
{
	UIGenCheckButton *pCheckButton = UI_GEN_RESULT(pGen, CheckButton);
	UIGenCheckButtonState *pState = UI_GEN_STATE(pGen, CheckButton);
	//CBox *pBox = &pGen->ScreenBox;

	if (ui_GenInState(pGen, kUIGenStateLeftMouseClick) || (ui_GenInState(pGen, kUIGenStateLeftMouseUp) && ui_GenInState(pGen, kUIGenStatePressed)))
	{
		ui_GenRunAction(pGen, pCheckButton->pOnClicked);
	}
	//if (ui_GenInState(pGen, kUIGenStateLeftMouseDown))
	//{
	//	ui_GenSetState(pGen, kUIGenStatePressed);
	//}
	//else if (!ui_GenInState(pGen, kUIGenStateLeftMouseDownAnywhere))
	//{
	//	ui_GenUnsetState(pGen, kUIGenStatePressed);
	//}
}

void ui_GenDrawEarlyCheckButton(UIGen *pGen)
{
	static unsigned char *s_estrText;
	UIGenCheckButton *pCheckButton = UI_GEN_RESULT(pGen, CheckButton);
	UIGenCheckButtonState *pState = UI_GEN_STATE(pGen, CheckButton);
	F32 fLeft = pGen->ScreenBox.lx;
	F32 fRight = pGen->ScreenBox.hx;
	F32 fTop = pGen->ScreenBox.ly;
	F32 fBottom = pGen->ScreenBox.hy;
	F32 fCenterY = (fTop + fBottom) / 2;
	F32 fScale = pGen->fScale;
	const char *pchText = pState->pchString;
	UIStyleFont *pFont = pchText && *pchText ? ui_GenBundleTextGetFont(&pCheckButton->TextBundle) : NULL;
	F32 fX, fY;

	F32 fZ1 = UI_GET_Z();
	F32 fZ4 = UI_GET_Z();
	F32 fZ2 = fZ1 + (fZ4 - fZ1) * 0.33333f;
	F32 fZ3 = fZ1 + (fZ4 - fZ1) * 0.66666f;

	// This should actually be done in fixup - ama
	int iButtonWidth = pCheckButton->iButtonHeight == -1 ? ui_TextureAssemblyWidth(GET_REF(pCheckButton->hBorderAssembly)) : pCheckButton->iButtonWidth;
	int iButtonHeight = pCheckButton->iButtonHeight == -1 ?  ui_TextureAssemblyHeight(GET_REF(pCheckButton->hBorderAssembly)) : pCheckButton->iButtonHeight;
	CBox box = 
	{
		pGen->ScreenBox.lx,
		(pGen->ScreenBox.ly + pGen->ScreenBox.hy - iButtonHeight) /2,
		pGen->ScreenBox.lx + iButtonWidth,
		(pGen->ScreenBox.ly + pGen->ScreenBox.hy + iButtonHeight) /2,
	};

	ui_TextureAssemblyDraw(GET_REF(pCheckButton->hBorderAssembly), &box, NULL, pGen->fScale, fZ1, fZ2, pGen->chAlpha, NULL);
	if (pState->bInconsistent)
		ui_TextureAssemblyDraw(GET_REF(pCheckButton->hInconsistentAssembly), &box, NULL, pGen->fScale, fZ2, fZ3, pGen->chAlpha, NULL);
	if (pState->bChecked)
		ui_TextureAssemblyDraw(GET_REF(pCheckButton->hCheckAssembly), &box, NULL, pGen->fScale, fZ3, fZ4, pGen->chAlpha, NULL);

	fY = fCenterY;
	fX = box.hx + pCheckButton->fSpacing * pGen->fScale;

	if (ui_GenBundleTruncate(&pState->Truncate, pFont, GET_REF(pCheckButton->hTruncate), fRight - box.hx, pGen->fScale, pchText, &s_estrText))
	{
		pchText = s_estrText;
	}

	if (pchText)
	{
		ui_StyleFontUse(pFont, false, kWidgetModifier_None);
		gfxfont_MultiplyAlpha(pGen->chAlpha);
		gfxfont_Print(fX, fY, UI_GET_Z(), fScale, fScale, CENTER_Y, pchText);
	}
}

void ui_GenUpdateCheckButton(UIGen *pGen)
{
	UIGenCheckButton *pCheckButton = UI_GEN_RESULT(pGen, CheckButton);
	UIGenCheckButtonState *pState = UI_GEN_STATE(pGen, CheckButton);
	ui_GenBundleTextGetText(pGen, &pCheckButton->TextBundle, NULL, &pState->pchString);
}

void ui_GenFitContentsSizeCheckButton(UIGen *pGen, UIGenCheckButton *pCheckButton, CBox *pOut)
{
	UIGenCheckButtonState *pState = UI_GEN_STATE(pGen, CheckButton);
	F32 fWidth, fHeight;
	F32 fTextWidth, fTextHeight;
	const char *pchText = pState->pchString;

	fWidth = pCheckButton->iButtonHeight == -1 ? ui_TextureAssemblyWidth(GET_REF(pCheckButton->hBorderAssembly)) : pCheckButton->iButtonWidth;
	fHeight = pCheckButton->iButtonHeight == -1 ?  ui_TextureAssemblyHeight(GET_REF(pCheckButton->hBorderAssembly)) : pCheckButton->iButtonHeight;

	if (pchText)
	{
		UIStyleFont *pFont = ui_GenBundleTextGetFont(&pCheckButton->TextBundle);
		fTextWidth = ui_StyleFontWidth(pFont, 1.f, pchText);
		fTextHeight = ui_StyleFontLineHeight(pFont, 1.f);
		fWidth += fTextWidth + pCheckButton->fSpacing;
		fHeight = max(fHeight, fTextHeight);
	}

	BuildCBox(pOut, 0, 0, fWidth, fHeight);
}

AUTO_RUN;
void ui_GenRegisterCheckButton(void)
{
	ui_GenRegisterType(kUIGenTypeCheckButton, 
		UI_GEN_NO_VALIDATE, 
		UI_GEN_NO_POINTERUPDATE,
		ui_GenUpdateCheckButton, 
		UI_GEN_NO_LAYOUTEARLY, 
		UI_GEN_NO_LAYOUTLATE, 
		ui_GenTickEarlyCheckButton, 
		UI_GEN_NO_TICKLATE, 
		ui_GenDrawEarlyCheckButton,
		ui_GenFitContentsSizeCheckButton, 
		UI_GEN_NO_FITPARENTSIZE, 
		UI_GEN_NO_HIDE, 
		UI_GEN_NO_INPUT, 
		UI_GEN_NO_UPDATECONTEXT, 
		UI_GEN_NO_QUEUERESET);
}

#include "UIGenCheckButton_h_ast.c"
