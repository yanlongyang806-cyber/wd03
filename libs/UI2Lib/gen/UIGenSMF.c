#include "Message.h"
#include "StringFormat.h"
#include "EString.h"
#include "MultiVal.h"
#include "GfxClipper.h"
#include "TextFilter.h"

#include "UISMFView.h"
#include "UICore_h_ast.h"
#include "UIGen.h"
#include "UIGen_h_ast.h"
#include "UIGenSMF.h"
#include "MessageExpressions.h"
#include "smf_render.h"
#include "smf/smf_format.h"
#include "UIGenPrivate.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

MP_DEFINE(UIGenSMF);
MP_DEFINE(UIGenSMFState);

AUTO_FIXUPFUNC;
TextParserResult ui_GenSMFStateParserFixup(UIGenSMFState *pState, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
	case FIXUPTYPE_DESTRUCTOR:
		if (pState->pBlock)
		{
			smfblock_Destroy(pState->pBlock);
			pState->pBlock = NULL;
		}
		SAFE_FREE(pState->pAttribs);
		break;
	}
	return PARSERESULT_SUCCESS;
}

AUTO_FIXUPFUNC;
TextParserResult ui_GenSMFParserFixup(UIGenSMF *pSMF, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
	case FIXUPTYPE_POST_TEXT_READ:
	case FIXUPTYPE_POST_RELOAD:
		if (IS_HANDLE_ACTIVE(pSMF->Defaults.hFont) && !IS_HANDLE_ACTIVE(pSMF->hFont))
		{
			COPY_HANDLE(pSMF->hFont, pSMF->Defaults.hFont);
		}
		break;
	}
	return ui_GenInternalParserFixup(&pSMF->polyp, eType, pExtraData);
}

void ui_GenUpdateSMF(UIGen *pGen)
{
	UIGenSMF *pSMF = UI_GEN_RESULT(pGen, SMF);
	UIGenSMFState *pState = UI_GEN_STATE(pGen, SMF);

	ui_GenGetTextFromExprMessage(pGen, pSMF->pTextExpr, GET_REF(pSMF->hText), pState->pchStaticString, &pState->pchText, pSMF->bFilterProfanity);
}

void ui_GenTickEarlySMF(UIGen *pGen)
{
	UIGenSMF *pSMF = UI_GEN_RESULT(pGen, SMF);
	UIGenSMFState *pState = UI_GEN_STATE(pGen, SMF);

	ui_GenTickScrollbar(pGen, &pSMF->scrollbar, &pState->scrollbar);
	if (pState->pAttribs && pState->pBlock && pSMF->bAllowInteract)
	{
		CBox box = pGen->ScreenBox;
		CBox smfBox;
		BuildCBox(&smfBox, 0, 0, smfblock_GetMinWidth(pState->pBlock), smfblock_GetHeight(pState->pBlock));
		ui_AlignCBox(&box, &smfBox, pSMF->eAlignment);
		//ui_GenClearPointer(pGen);
		smf_Interact(pState->pBlock, pState->pAttribs, smfBox.lx, smfBox.ly - pState->scrollbar.fScrollPosition, g_SMFNavigateCallback, g_SMFHoverCallback, pGen);
	}
}

void ui_GenLayoutEarlySMF(UIGen *pGen)
{
	UIGenSMF *pSMF = UI_GEN_RESULT(pGen, SMF);
	UIGenSMFState *pState = UI_GEN_STATE(pGen, SMF);
	F32 fTotalHeight = 0;

	if (!pState->pBlock)
		pState->pBlock = smfblock_Create();

	pState->pAttribs = smf_TextAttribsFromFont(pState->pAttribs, GET_REF(pSMF->hFont));
	if (pSMF->Defaults.uiColor)
	{
		pState->pAttribs->ppColorBottom = U32_TO_PTR(pSMF->Defaults.uiColor);
	}
	if (pSMF->Defaults.uiShadow)
		pState->pAttribs->ppShadow = U32_TO_PTR(pSMF->Defaults.uiShadow);
	pState->pAttribs->ppScale = U32_TO_PTR((U32)(pGen->fScale * SMF_FONT_SCALE));

	if(pSMF->bScaleToFit || pSMF->bShrinkToFit)
	{
		pState->pBlock->bScaleToFit = true;
		pState->pBlock->fMinRenderScale = 0.0001f;
		if(pSMF->bShrinkToFit)
		{
			pState->pBlock->fMaxRenderScale = 1.0f;
		}
		else
		{
			pState->pBlock->fMaxRenderScale = FLT_MAX;
		}
	}
	pState->pBlock->bNoWrap = pSMF->bNoWrap;

	if (pState->pchText && *pState->pchText)
	{
		// Don't let rounding errors cause an SMF reflow.
		F32 fCurrentWidth = smfblock_GetWidth(pState->pBlock);
		F32 fTextHeight = smfblock_GetHeight(pState->pBlock);
		CBox ScreenBox;
		F32 fTextWidth;

		ui_GenScrollbarBox(&pSMF->scrollbar, &pState->scrollbar, &pGen->ScreenBox, &ScreenBox, pGen->fScale);
		fTextWidth = CBoxWidth(&ScreenBox);
		if (UI_GEN_NEARF(fCurrentWidth, round(fTextWidth)))
			fTextWidth = fCurrentWidth;
		// Passing in a height is pointless. If it's too big we cause a reflow for no reason;
		// if it's too small we cause a reflow but it ends up too tall anyway.
		fTextWidth = max(0, fTextWidth);
		fTextHeight = max(0, fTextHeight);

		// We are reformatting here! This works around a bug where an SMF child embedded in a UIGenListRow template would not format correctly (i.e. scrunched text bug).
		// Performance does not appear to be impacted by this.
		smf_ParseAndFormat(pState->pBlock, pState->pchText, 0, 0, 0, round(fTextWidth), round(max(1, fTextHeight)), /*bReparse=*/false, /*bReformat=*/true, pSMF->bSafeMode, pState->pAttribs);
		fTotalHeight = smfblock_GetHeight(pState->pBlock);
	}

	ui_GenLayoutScrollbar(pGen, &pSMF->scrollbar, &pState->scrollbar, fTotalHeight);
}

void ui_GenDrawEarlySMF(UIGen *pGen)
{
	UIGenSMF *pSMF = UI_GEN_RESULT(pGen, SMF);
	UIGenSMFState *pState = UI_GEN_STATE(pGen, SMF);
	if (pState->pBlock && pState->pchText && *pState->pchText)
	{
		UIDirection eAlignment = pSMF->eAlignment;
		CBox smfBox;
		BuildCBox(&smfBox, 0, 0, smfblock_GetMinWidth(pState->pBlock) + ui_GenScrollbarWidth(&pSMF->scrollbar, &pState->scrollbar), smfblock_GetHeight(pState->pBlock));
		// Cant use alignments other than top when using scrollbar
		if (!pState->scrollbar.bUnneeded)
		{
			eAlignment &= ~UIVertical;
			eAlignment |= UITop;
		}
		ui_AlignCBox(&pGen->ScreenBox, &smfBox, eAlignment);
		ui_GenScrollbarBox(&pSMF->scrollbar, &pState->scrollbar, &smfBox, &smfBox, pGen->fScale);
		smf_ParseAndDisplay(pState->pBlock, NULL, smfBox.lx, smfBox.ly - pState->scrollbar.fScrollPosition, UI_GET_Z(), smfblock_GetWidth(pState->pBlock), smfblock_GetHeight(pState->pBlock), /*bReparse=*/false, /*bReformat=*/false, pSMF->bSafeMode, pState->pAttribs, pGen->chAlpha, NULL, GenSpritePropGetCurrent());
	}
	ui_GenDrawScrollbar(pGen, &pSMF->scrollbar, &pState->scrollbar);
}

void ui_GenFitContentsSizeSMF(UIGen *pGen, UIGenSMF *pSMF, CBox *pOut)
{
	UIGenSMFState *pState = UI_GEN_STATE(pGen, SMF);
	if (pState->pBlock)
	{
 		F32 fScaledWidth;
		F32 fScaledHeight;

		fScaledWidth = (F32)smfblock_GetWidth(pState->pBlock);
		if (pGen->pResult && pGen->pResult->pos.Width.eUnit == UIUnitFitContents && pGen->fScale > 0) 
			fScaledWidth /= pGen->fScale;
		fScaledHeight = (F32)smfblock_GetHeight(pState->pBlock);
		if (pGen->pResult && pGen->pResult->pos.Height.eUnit == UIUnitFitContents && pGen->fScale > 0)
			fScaledHeight /= pGen->fScale;

		BuildCBox(pOut, 0, 0, fScaledWidth, fScaledHeight);
	}
}

void ui_GenHideSMF(UIGen *pGen)
{
	UIGenSMFState *pState = UI_GEN_STATE(pGen, SMF);
	if (pState)
	{
		UIGenSMF *pSMF = UI_GEN_RESULT(pGen, SMF);
		if (pState->pBlock)
		{
			smfblock_Destroy(pState->pBlock);
			pState->pBlock = NULL;
		}
		ui_GenHideScrollbar(pGen, pSMF ? &pSMF->scrollbar : NULL, &pState->scrollbar);
		SAFE_FREE(pState->pAttribs);
	}
}

AUTO_RUN;
void ui_GenRegisterSMF(void)
{
	MP_CREATE(UIGenSMF, 64);
	MP_CREATE(UIGenSMFState, 64);
	ui_GenRegisterType(kUIGenTypeSMF, 
		UI_GEN_NO_VALIDATE, 
		UI_GEN_NO_POINTERUPDATE,
		ui_GenUpdateSMF, 
		ui_GenLayoutEarlySMF, 
		UI_GEN_NO_LAYOUTLATE, 
		ui_GenTickEarlySMF, 
		UI_GEN_NO_TICKLATE, 
		ui_GenDrawEarlySMF,
		ui_GenFitContentsSizeSMF, 
		UI_GEN_NO_FITPARENTSIZE, 
		ui_GenHideSMF, 
		UI_GEN_NO_INPUT, 
		UI_GEN_NO_UPDATECONTEXT, 
		UI_GEN_NO_QUEUERESET);
}

#include "UIGenSMF_h_ast.c"
