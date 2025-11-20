#include "earray.h"
#include "UIGenSprite.h"
#include "UICore_h_ast.h"
#include "UIGen_h_ast.h"
#include "GfxTextures.h"
#include "MemoryPool.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

MP_DEFINE(UIGenSprite);
MP_DEFINE(UIGenSpriteState);

AUTO_RUN;
void UIGenSpriteInitMemPools(void)
{
	MP_CREATE(UIGenSprite, 32);
	MP_CREATE(UIGenSpriteState, 32);
}

void ui_GenUpdateSprite(UIGen *pGen)
{
	UIGenSprite *pSprite = UI_GEN_RESULT(pGen, Sprite);
	UIGenSpriteState *pState = UI_GEN_STATE(pGen, Sprite);
	S32 nLowerLayer = eaSize(&pSprite->eaLowerLayer);
	S32 nLayer = eaSize(&pSprite->eaLayer);
	S32 iBaseLowerLayer = -nLowerLayer - 1;
	S32 iBackgroundLayer = pSprite->TextureBundle.bBackground ? 0 : iBaseLowerLayer;
	S32 i;

	eaSetSizeStruct(&pState->eaLowerLayer, parse_UIGenBundleTextureState, nLowerLayer);
	eaSetSizeStruct(&pState->eaLayer, parse_UIGenBundleTextureState, nLayer);

	ui_GenBundleTextureUpdate(pGen, &pSprite->TextureBundle, &pState->TextureState);

	for (i = 0; i < nLowerLayer; i++)
	{
		ui_GenBundleTextureUpdate(pGen, pSprite->eaLowerLayer[i], pState->eaLowerLayer[i]);
		if (pSprite->eaLowerLayer[i]->bBackground)
		{
			MIN1(iBackgroundLayer, iBaseLowerLayer + i);
		}
	}

	for (i = 0; i < nLayer; i++)
	{
		ui_GenBundleTextureUpdate(pGen, pSprite->eaLayer[i], pState->eaLayer[i]);
		if (pSprite->eaLayer[i]->bBackground)
		{
			MIN1(iBackgroundLayer, 1 + i);
		}
	}

	pState->iBackgroundLayer = iBackgroundLayer;
}

void ui_GenDrawEarlySprite(UIGen *pGen)
{
	UIGenSprite *pSprite = UI_GEN_RESULT(pGen, Sprite);
	UIGenSpriteState *pState = UI_GEN_STATE(pGen, Sprite);
	CBox *pBox = &pGen->ScreenBox;
	CBox OutBox;
	S32 nLowerLayer = eaSize(&pSprite->eaLowerLayer);
	S32 nLayer = eaSize(&pSprite->eaLayer);
	S32 iBaseLowerLayer = -nLowerLayer - 1;
	S32 iBackgroundLayer = pState->iBackgroundLayer;
	S32 i;

	if (iBackgroundLayer < 0)
	{
		for (i = 0; i < nLowerLayer; i++)
		{
			if (iBackgroundLayer <= iBaseLowerLayer + i)
			{
				ui_GenBundleTextureDraw(pGen, pGen->pResult, pSprite->eaLowerLayer[i], pBox, 0, 0, false, false, pState->eaLowerLayer[i], &OutBox);
				pBox = &OutBox;
			}
		}
	}

	if (iBackgroundLayer <= 0)
	{
		ui_GenBundleTextureDraw(pGen, pGen->pResult, &pSprite->TextureBundle, pBox, 0, 0, false, false, &pState->TextureState, &OutBox);
		pBox = &OutBox;
	}

	for (i = 0; i < nLayer; i++)
	{
		if (iBackgroundLayer <= 1 + i)
		{
			ui_GenBundleTextureDraw(pGen, pGen->pResult, pSprite->eaLayer[i], pBox, 0, 0, false, false, pState->eaLayer[i], &OutBox);
			pBox = &OutBox;
		}
	}
}

void ui_GenFitContentsSizeSprite(UIGen *pGen, UIGenSprite *pSprite, CBox *pOut)
{
	UIGenSpriteState *pState = UI_GEN_STATE(pGen, Sprite);

	if (!ui_GenBundleTextureFitContentsSize(pGen, &pSprite->TextureBundle, pOut, &pState->TextureState))
	{
		S32 nLowerLayer = eaSize(&pState->eaLowerLayer);
		S32 nLayer = eaSize(&pState->eaLayer);
		S32 i;

		for (i = nLayer - 1; i >= 0; i--)
		{
			if (ui_GenBundleTextureFitContentsSize(pGen, pSprite->eaLayer[i], pOut, pState->eaLowerLayer[i]))
				return;
		}

		for (i = nLowerLayer - 1; i >= 0; i--)
		{
			if (ui_GenBundleTextureFitContentsSize(pGen, pSprite->eaLowerLayer[i], pOut, pState->eaLowerLayer[i]))
				return;
		}
	}
}

AUTO_RUN;
void ui_GenRegisterSprite(void)
{
	ui_GenRegisterType(kUIGenTypeSprite, 
		UI_GEN_NO_VALIDATE, 
		UI_GEN_NO_POINTERUPDATE,
		ui_GenUpdateSprite, 
		UI_GEN_NO_LAYOUTEARLY, 
		UI_GEN_NO_LAYOUTLATE, 
		UI_GEN_NO_TICKEARLY, 
		UI_GEN_NO_TICKLATE, 
		ui_GenDrawEarlySprite,
		ui_GenFitContentsSizeSprite,
		UI_GEN_NO_FITPARENTSIZE,
		UI_GEN_NO_HIDE,
		UI_GEN_NO_INPUT,
		UI_GEN_NO_UPDATECONTEXT,
		UI_GEN_NO_QUEUERESET);
}

#include "UIGenSprite_h_ast.c"
