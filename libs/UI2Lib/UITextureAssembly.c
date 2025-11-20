#include "error.h"
#include "earray.h"
#include "CBox.h"
#include "Color.h"
#include "StringCache.h"
#include "MemoryPool.h"
#include "file.h"

#include "ResourceManager.h"

#include "GraphicsLib.h"
#include "GfxClipper.h"
#include "GfxSprite.h"
#include "GfxTexOpts.h"
#include "GfxTextures.h"
#include "GfxTexAtlas.h"
#include "GfxPrimitive.h"

#include "UICore.h"
#include "UIStyle.h"
#include "UISlider.h"
#include "UIList.h"
#include "UIWindow.h"
#include "UIColorButton.h"

#include "UITextureAssembly.h"

GCC_SYSTEM

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

// TODO:
// - More error checking
// - Memory, CPU profiling
// - Pre-multiply texture instance colors if possible
// - Texture scaled mode (rather than stretched)
// - Store an index to dependencies rather than a pointer

// Maybe TODO:
// - Runtime drawing changes - text replacements, texture replacements
// - "Solid" textures that reduce an OutBox?
// - Text in assemblies

extern ParseTable parse_UIStyleBorder[];
#define TYPE_parse_UIStyleBorder UIStyleBorder
extern const char *g_ui_pchParent;
static const char *s_pchContent;

MP_DEFINE(UITextureAssembly);
MP_DEFINE(UITextureInstance);
MP_DEFINE(UITextureInstanceFallback);

AUTO_RUN;
void TextureAssemblyInitMemPools(void)
{
	MP_CREATE(UITextureAssembly, 16);
	MP_CREATE(UITextureInstance, 64);
	MP_CREATE(UITextureInstanceFallback, 16);
}

static void TextureAssemblyConvertBorder(UITextureAssembly *pTexAs, UIStyleBorder *pBorder)
{
	UITextureInstance *pTopLeft = StructCreate(parse_UITextureInstance);
	UITextureInstance *pTopRight = StructCreate(parse_UITextureInstance);
	UITextureInstance *pBottomLeft = StructCreate(parse_UITextureInstance);
	UITextureInstance *pBottomRight = StructCreate(parse_UITextureInstance);
	UITextureInstance *pTop = StructCreate(parse_UITextureInstance);
	UITextureInstance *pBottom = StructCreate(parse_UITextureInstance);
	UITextureInstance *pLeft = StructCreate(parse_UITextureInstance);
	UITextureInstance *pRight = StructCreate(parse_UITextureInstance);
	UITextureInstance *pBackground = StructCreate(parse_UITextureInstance);

	pTopLeft->pchTexture = pBorder->pchTopLeft;
	pTopLeft->pchKey = allocAddString("TopLeft");
	pTopLeft->TopFrom.eDirection = UITop;
	pTopLeft->TopFrom.pchRelative = s_pchContent;
	pTopLeft->LeftFrom.eDirection = UILeft;
	pTopLeft->LeftFrom.pchRelative = s_pchContent;
	if (pBorder->uiOuterColor)
		pTopLeft->uiTopLeftColor = pBorder->uiOuterColor;

	pTopRight->pchTexture = pBorder->pchTopRight;
	pTopRight->pchKey = allocAddString("TopRight");
	pTopRight->TopFrom.eDirection = UITop;
	pTopRight->TopFrom.pchRelative = s_pchContent;
	pTopRight->RightFrom.eDirection = UIRight;
	pTopRight->RightFrom.pchRelative = s_pchContent;
	if (pBorder->uiOuterColor)
		pTopRight->uiTopLeftColor = pBorder->uiOuterColor;

	pBottomLeft->pchTexture = pBorder->pchBottomLeft;
	pBottomLeft->pchKey = allocAddString("BottomLeft");
	pBottomLeft->BottomFrom.eDirection = UIBottom;
	pBottomLeft->BottomFrom.pchRelative = s_pchContent;
	pBottomLeft->LeftFrom.eDirection = UILeft;
	pBottomLeft->LeftFrom.pchRelative = s_pchContent;
		pBottomLeft->uiTopLeftColor = pBorder->uiOuterColor;

	pBottomRight->pchTexture = pBorder->pchBottomRight;
	pBottomRight->pchKey = allocAddString("BottomRight");
	pBottomRight->BottomFrom.eDirection = UIBottom;
	pBottomRight->BottomFrom.pchRelative = s_pchContent;
	pBottomRight->RightFrom.eDirection = UIRight;
	pBottomRight->RightFrom.pchRelative = s_pchContent;
	if (pBorder->uiOuterColor)
		pBottomRight->uiTopLeftColor = pBorder->uiOuterColor;

	pTop->pchTexture = pBorder->pchTop;
	pTop->pchKey = allocAddString("Top");
	pTop->TopFrom.eDirection = UITop;
	pTop->TopFrom.pchRelative = s_pchContent;
	pTop->LeftFrom.eDirection = UIRight;
	pTop->LeftFrom.pchRelative = pTopLeft->pchKey;
	pTop->RightFrom.eDirection = UILeft;
	pTop->RightFrom.pchRelative = pTopRight->pchKey;

	pBottom->pchTexture = pBorder->pchBottom;
	pBottom->pchKey = allocAddString("Bottom");
	pBottom->BottomFrom.eDirection = UIBottom;
	pBottom->BottomFrom.pchRelative = s_pchContent;
	pBottom->LeftFrom.eDirection = UIRight;
	pBottom->LeftFrom.pchRelative = pBottomLeft->pchKey;
	pBottom->RightFrom.eDirection = UILeft;
	pBottom->RightFrom.pchRelative = pBottomRight->pchKey;

	pLeft->pchTexture = pBorder->pchLeft;
	pLeft->pchKey = allocAddString("Left");
	pLeft->LeftFrom.eDirection = UILeft;
	pLeft->LeftFrom.pchRelative = s_pchContent;
	pLeft->TopFrom.eDirection = UIBottom;
	pLeft->TopFrom.pchRelative = pTopLeft->pchKey;
	pLeft->BottomFrom.eDirection = UITop;
	pLeft->BottomFrom.pchRelative = pBottomLeft->pchKey;

	pRight->pchTexture = pBorder->pchRight;
	pRight->pchKey = allocAddString("Right");
	pRight->RightFrom.eDirection = UIRight;
	pRight->RightFrom.pchRelative = s_pchContent;
	pRight->TopFrom.eDirection = UIBottom;
	pRight->TopFrom.pchRelative = pTopRight->pchKey;
	pRight->BottomFrom.eDirection = UITop;
	pRight->BottomFrom.pchRelative = pBottomRight->pchKey;

	pBackground->pchTexture = pBorder->pchBackground;
	pBackground->pchKey = allocAddString("Background");
	pBackground->LeftFrom.eDirection = pBorder->bDrawUnder ? UILeft : UIRight;
	pBackground->LeftFrom.pchRelative = pLeft->pchKey;
	pBackground->RightFrom.eDirection = pBorder->bDrawUnder ? UIRight : UILeft;
	pBackground->RightFrom.pchRelative = pRight->pchKey;
	pBackground->TopFrom.eDirection = pBorder->bDrawUnder ? UITop : UIBottom;
	pBackground->TopFrom.pchRelative = pTop->pchKey;
	pBackground->BottomFrom.eDirection = pBorder->bDrawUnder ? UIBottom : UITop;
	pBackground->BottomFrom.pchRelative = pBottom->pchKey;

	if (pBorder->eBorderType == UITextureModeTiled)
	{
		pTop->eModeHorizontal = UITextureModeTiled;
		pBottom->eModeHorizontal = UITextureModeTiled;
		pLeft->eModeVertical = UITextureModeTiled;
		pRight->eModeVertical = UITextureModeTiled;
	}

	if (pBorder->eBackgroundType == UITextureModeTiled)
	{
		pBackground->eModeVertical = UITextureModeTiled;
		pBackground->eModeHorizontal = UITextureModeTiled;
	}

	if (pBorder->uiOuterColor)
	{
		pTopLeft->uiTopLeftColor = pBorder->uiOuterColor;
		pTopRight->uiTopLeftColor = pBorder->uiOuterColor;
		pBottomLeft->uiTopLeftColor = pBorder->uiOuterColor;
		pBottomRight->uiTopLeftColor = pBorder->uiOuterColor;
		pLeft->uiTopLeftColor = pBorder->uiOuterColor;
		pRight->uiTopLeftColor = pBorder->uiOuterColor;
		pTop->uiTopLeftColor = pBorder->uiOuterColor;
		pBottom->uiTopLeftColor = pBorder->uiOuterColor;
	}

	if (pBorder->uiInnerColor)
	{
		pBackground->uiTopLeftColor = pBorder->uiInnerColor;
	}

	if (pBorder->uiTopLeftColor)
	{
		pTexAs->Colors.uiTopLeftColor = pBorder->uiTopLeftColor;
		pTexAs->Colors.uiTopRightColor = pBorder->uiTopRightColor;
		pTexAs->Colors.uiBottomLeftColor = pBorder->uiBottomLeftColor;
		pTexAs->Colors.uiBottomRightColor = pBorder->uiBottomRightColor;
	}

	eaInsert(&pTexAs->eaTextures, pTopLeft, 0);
	eaInsert(&pTexAs->eaTextures, pTopRight, 0);
	eaInsert(&pTexAs->eaTextures, pBottomLeft, 0);
	eaInsert(&pTexAs->eaTextures, pBottomRight, 0);
	eaInsert(&pTexAs->eaTextures, pTop, 0);
	eaInsert(&pTexAs->eaTextures, pBottom, 0);
	eaInsert(&pTexAs->eaTextures, pLeft, 0);
	eaInsert(&pTexAs->eaTextures, pRight, 0);
	eaInsert(&pTexAs->eaTextures, pBackground, 0);
}

static void TextureAssemblyPostTextRead(UITextureAssembly *pTexAs)
{
	S32 iMaxZ = -1;
	S32 i;

	if (pTexAs->pBorder)
	{
		TextureAssemblyConvertBorder(pTexAs, pTexAs->pBorder);
		StructDestroySafe(parse_UIStyleBorder, &pTexAs->pBorder);
	}

	for (i = 0; i < eaSize(&pTexAs->eaTextures); i++)
	{
		UITextureInstance *pTexInt = pTexAs->eaTextures[i];

		// If no manual Z is specified, number them bottom-up.
		if (!pTexInt->iZ)
			pTexInt->iZ = i;
		MAX1(pTexAs->iMaxZ, pTexInt->iZ + 1);

		if (IS_HANDLE_ACTIVE(pTexInt->hSubAssembly))
			pTexInt->bIsSubAssembly = true;

		if (pTexInt->bIsSubAssembly)
		{
			const char *pchKey = pTexInt->pchTexture ? pTexInt->pchTexture : pTexInt->pchKey;
			if (!GET_REF(pTexInt->hSubAssembly))
				SET_HANDLE_FROM_STRING("UITextureAssembly", pchKey, pTexInt->hSubAssembly);
			if (pTexInt->pFallback)
				InvalidDataErrorf("%s: %s: Subassemblies cannot contain fallbacks.", pTexAs->pchName, pTexInt->pchKey);
		}
		else
		{
			// If no texture is specified, use the key.
			if (!pTexInt->pchTexture)
				pTexInt->pchTexture = pTexInt->pchKey;
		}

		// If no colors are given, use white.
		if (!pTexInt->uiTopLeftColor
			&& !pTexInt->uiTopRightColor
			&& !pTexInt->uiBottomLeftColor
			&& !pTexInt->uiBottomRightColor)
			pTexInt->uiTopLeftColor = 0xFFFFFFFF;

		// One color specified, use it.
		if (!pTexInt->uiTopRightColor
			&& !pTexInt->uiBottomLeftColor
			&& !pTexInt->uiBottomRightColor)
		{
			pTexInt->uiTopRightColor = pTexInt->uiTopLeftColor;
			pTexInt->uiBottomLeftColor = pTexInt->uiTopLeftColor;
			pTexInt->uiBottomRightColor = pTexInt->uiTopLeftColor;
		}

		// Left/Right specified.
		if (pTexInt->uiTopLeftColor && pTexInt->uiTopRightColor &&
			!pTexInt->uiBottomLeftColor && !pTexInt->uiBottomRightColor)
		{
			pTexInt->uiBottomLeftColor = pTexInt->uiTopLeftColor;
			pTexInt->uiTopRightColor = pTexInt->uiBottomRightColor;
		}

		// Top/Bottom specified.
		if (pTexInt->uiTopLeftColor && pTexInt->uiBottomLeftColor &&
			!pTexInt->uiTopRightColor && !pTexInt->uiBottomRightColor)
		{
			pTexInt->uiTopRightColor = pTexInt->uiTopLeftColor;
			pTexInt->uiBottomRightColor = pTexInt->uiBottomLeftColor;
		}

		if (pTexInt->pchTexture)
		{
			if (pTexInt->LeftFrom.pchRelative && !(pTexInt->LeftFrom.eDirection & (UILeft | UIRight)))
				InvalidDataErrorf("%s: %s: LeftFrom must be relative to Left/Right.", pTexAs->pchName, pTexInt->pchKey);
			if (pTexInt->RightFrom.pchRelative && !(pTexInt->RightFrom.eDirection & (UILeft | UIRight)))
				InvalidDataErrorf("%s: %s: RightFrom must be relative to Left/Right.", pTexAs->pchName, pTexInt->pchKey);
			if (pTexInt->TopFrom.pchRelative && !(pTexInt->TopFrom.eDirection & (UITop | UIBottom)))
				InvalidDataErrorf("%s: %s: TopFrom must be relative to Top/Bottom.", pTexAs->pchName, pTexInt->pchKey);
			if (pTexInt->BottomFrom.pchRelative && !(pTexInt->BottomFrom.eDirection & (UITop | UIBottom)))
				InvalidDataErrorf("%s: %s: BottomFrom must be relative to Top/Bottom.", pTexAs->pchName, pTexInt->pchKey);

			if (pTexInt->HorizontalCenterFrom.pchRelative && !(pTexInt->HorizontalCenterFrom.eDirection & (UILeft | UIRight)))
				InvalidDataErrorf("%s: %s: HorizontalCenterFrom must be relative to Left/Right.", pTexAs->pchName, pTexInt->pchKey);
			if (pTexInt->VerticalCenterFrom.pchRelative && !(pTexInt->VerticalCenterFrom.eDirection & (UITop | UIBottom)))
				InvalidDataErrorf("%s: %s: VerticalCenterFrom must be relative to Top/Bottom.", pTexAs->pchName, pTexInt->pchKey);

			if (pTexInt->HorizontalCenterFrom.pchRelative && (pTexInt->LeftFrom.pchRelative || pTexInt->RightFrom.pchRelative))
				InvalidDataErrorf("%s: %s: HorizontalCenterFrom and LeftFrom/RightFrom are not compatible.", pTexAs->pchName, pTexInt->pchKey);
			if (pTexInt->VerticalCenterFrom.pchRelative && (pTexInt->TopFrom.pchRelative || pTexInt->BottomFrom.pchRelative))
				InvalidDataErrorf("%s: %s: VerticalCenterFrom and TopFrom/BottomFrom are not compatible.", pTexAs->pchName, pTexInt->pchKey);

			if (!(pTexInt->HorizontalCenterFrom.pchRelative || pTexInt->LeftFrom.pchRelative || pTexInt->RightFrom.pchRelative))
				InvalidDataErrorf("%s: %s: One of LeftFrom, RightFrom, or HorizontalCenterFrom must be specified.", pTexAs->pchName, pTexInt->pchKey);
			if (!(pTexInt->VerticalCenterFrom.pchRelative || pTexInt->TopFrom.pchRelative || pTexInt->BottomFrom.pchRelative))
				InvalidDataErrorf("%s: %s: One of TopFrom, BottomFrom, or VerticalCenterFrom must be specified.", pTexAs->pchName, pTexInt->pchKey);
		}
	}

	if (eaSize(&pTexAs->eaTextures) > 100)
		InvalidDataErrorf("%s: Too many textures in this assembly.", pTexAs->pchName);

	if (eaSize(&pTexAs->eaTextures) < 1)
		InvalidDataErrorf("%s: Not enough textures in this assembly.", pTexAs->pchName);
}

int TextureInstanceSortByZ(const UITextureInstance **ppA, const UITextureInstance **ppB)
{
	const UITextureInstance *pA = *ppA;
	const UITextureInstance *pB = *ppB;
	return (int)pA->iZ - (int)pB->iZ;
}

// FIXME: Super-lame topological sort because I don't want to build a real graph.
static bool TextureAssemblySort(UITextureAssembly *pTexAs)
{
	static UITextureInstance **s_eaStart = NULL;
	static UITextureInstance **s_eaAll = NULL;
	UITextureInstance *pTexInt;
	S32 i;

	eaClear(&s_eaStart);
	eaClear(&s_eaAll);

	for (i = 0; i < eaSize(&pTexAs->eaTextures); i++)
	{
		pTexInt = pTexAs->eaTextures[i];
		if (!(pTexInt->LeftFrom.pRelative
			|| pTexInt->RightFrom.pRelative
			|| pTexInt->TopFrom.pRelative
			|| pTexInt->BottomFrom.pRelative
			|| pTexInt->HorizontalCenterFrom.pRelative
			|| pTexInt->VerticalCenterFrom.pRelative
			))
			eaPush(&s_eaStart, pTexInt);
		else
			eaPush(&s_eaAll, pTexInt);
	}

	if (eaSize(&s_eaStart) == 0)
	{
		InvalidDataErrorf("%s: No textures can be placed without dependencies.", pTexAs->pchName);
		return false;
	}

	while (pTexInt = eaPop(&s_eaStart))
	{
		eaPush(&pTexAs->eaSortedByDependency, pTexInt);
		for (i = eaSize(&s_eaAll) - 1; i >= 0; i--)
		{
			UITextureInstance *pPossible = s_eaAll[i];
			if ((!pPossible->LeftFrom.pRelative || eaFind(&pTexAs->eaSortedByDependency, pPossible->LeftFrom.pRelative) >= 0)
				&& (!pPossible->RightFrom.pRelative || eaFind(&pTexAs->eaSortedByDependency, pPossible->RightFrom.pRelative) >= 0)
				&& (!pPossible->TopFrom.pRelative || eaFind(&pTexAs->eaSortedByDependency, pPossible->TopFrom.pRelative) >= 0)
				&& (!pPossible->BottomFrom.pRelative || eaFind(&pTexAs->eaSortedByDependency, pPossible->BottomFrom.pRelative) >= 0)
				&& (!pPossible->HorizontalCenterFrom.pRelative || eaFind(&pTexAs->eaSortedByDependency, pPossible->HorizontalCenterFrom.pRelative) >= 0)
				&& (!pPossible->VerticalCenterFrom.pRelative || eaFind(&pTexAs->eaSortedByDependency, pPossible->VerticalCenterFrom.pRelative) >= 0)
				)
			{
				eaPush(&s_eaStart, pPossible);
				eaRemove(&s_eaAll, i);
			}
		}
	}

	if (eaSize(&s_eaAll))
	{
		InvalidDataErrorf("%s: %s: Cycle detected in texture dependencies.", pTexAs->pchName, s_eaAll[0]->pchKey);
		return false;
	}

	eaCopy(&pTexAs->eaSortedByDrawZ, &pTexAs->eaTextures);
	eaQSort(pTexAs->eaSortedByDrawZ, TextureInstanceSortByZ);

	return true;
}

#define ALIGNED_TO_CONTENT_EDGE(pTexInt, Direction) \
	((pTexInt)->Direction##From.pchRelative == s_pchContent \
	 && (pTexInt)->Direction##From.eDirection == UI##Direction \
	 && nearf((pTexInt)->Direction##From.fOffset, round((pTexInt)->Direction##From.fOffset)))

static void TextureAssemblyPostBinning(UITextureAssembly *pTexAs)
{
	S32 i;
	for (i = 0; i < eaSize(&pTexAs->eaTextures); i++)
	{
		UITextureInstance *pTexInt = pTexAs->eaTextures[i];
		UITextureInstancePosition *apPos[] = {&pTexInt->LeftFrom, &pTexInt->TopFrom, &pTexInt->BottomFrom, &pTexInt->RightFrom, &pTexInt->HorizontalCenterFrom, &pTexInt->VerticalCenterFrom};
		S32 j;

		if (!pTexInt->bIsSubAssembly)
		{
			// Load textures.
			pTexInt->bIsAtlas = !(pTexInt->eModeHorizontal == UITextureModeTiled || pTexInt->eModeVertical == UITextureModeTiled);

			if (!pTexInt->pchTexture)
			{
				InvalidDataErrorf("%s: %s: No texture found.", pTexAs->pchName, pTexInt->pchKey);
				continue;
			}

			if (pTexInt->bIsAtlas)
			{
				pTexInt->pAtlasTexture = atlasFindTexture(pTexInt->pchTexture);
				if (!gbNoGraphics && (!pTexInt->pAtlasTexture || (!stricmp(pTexInt->pAtlasTexture->name, "white") && stricmp(pTexInt->pchTexture, "white"))))
					InvalidDataErrorf("%s: Unable to load texture %s.", pTexAs->pchName, pTexInt->pchTexture);
			}
			else if (!gbNoGraphics)
			{
				pTexInt->pTexture = texFindAndFlag(pTexInt->pchTexture, 0, WL_FOR_UI);
				if (!pTexInt->pTexture)
				{
					InvalidDataErrorf("%s: Unable to load texture %s.", pTexAs->pchName, pTexInt->pchTexture);
					pTexInt->pTexture = texFind("white", 0);
				}
				else if (pTexInt->eModeHorizontal == UITextureModeTiled && (pTexInt->pTexture->bt_texopt_flags & TEXOPT_CLAMPS))
					InvalidDataErrorf("%s: Texture %s is horizontally tiled but marked as ClampS. This flag needs to be disabled in the TexOptEditor, or horizontal tiling should be disabled in this assembly.", pTexAs->pchName, pTexInt->pchTexture);
				else if (pTexInt->eModeVertical == UITextureModeTiled && (pTexInt->pTexture->bt_texopt_flags & TEXOPT_CLAMPT))
					InvalidDataErrorf("%s: Texture %s is vertically tiled but marked as ClampT. This flag needs to be disabled in the TexOptEditor, or vertical tiling should be disabled in this assembly.", pTexAs->pchName, pTexInt->pchTexture);
			}

			if (!pTexInt->bIsSubAssembly)
			{
				pTexInt->pNinePatch = texGetNinePatch(pTexInt->pchTexture);
				if (pTexInt->pNinePatch)
				{
					if (pTexInt->pFallback)
						InvalidDataErrorf("%s: NinePatch textures cannot have fallbacks.", pTexInt->pchKey);

					if (pTexInt->eModeHorizontal != UITextureModeStretched
						|| pTexInt->eModeVertical != UITextureModeStretched)
						InvalidDataErrorf("%s: NinePatch textures be stretched.", pTexInt->pchKey);
				}
			}

			if (pTexInt->pFallback && !gbNoGraphics)
			{
				eaClear(&pTexInt->pFallback->eaTextures);
				for (j = 0; j < eaSize(&pTexInt->pFallback->eachFallback); j++)
				{
					const char *pchName = pTexInt->pFallback->eachFallback[j];
					if (!pchName || !*pchName)
					{
						eaPush(&pTexInt->pFallback->eaTextures, NULL);
					}
					else if (pTexInt->bIsAtlas)
					{
						AtlasTex *pTexture = atlasLoadTexture(pchName);
						if (!gbNoGraphics && (!pTexture || (!stricmp(pTexture->name, "white") && stricmp(pchName, "white"))))
						{
							InvalidDataErrorf("%s: Unable to load texture %s.", pTexAs->pchName, pchName);
							pTexture = atlasLoadTexture("white");
						}
						eaPush(&pTexInt->pFallback->eaTextures, pTexture);
					}
					else
					{
						BasicTexture *pTexture = texFindAndFlag(pchName, 0, WL_FOR_UI);
						if (!gbNoGraphics && !pTexture)
						{
							InvalidDataErrorf("%s: Unable to load texture %s.", pTexAs->pchName, pchName);
							pTexture = texFind("white", 0);
						}
						else if (pTexInt->eModeHorizontal == UITextureModeTiled && (pTexture->bt_texopt_flags & TEXOPT_CLAMPS))
							InvalidDataErrorf("%s: Texture %s is horizontally tiled but marked as ClampS. This flag needs to be disabled in the TexOptEditor, or horizontal tiling should be disabled in this assembly.", pTexAs->pchName, pchName);
						else if (pTexInt->eModeVertical == UITextureModeTiled && (pTexture->bt_texopt_flags & TEXOPT_CLAMPT))
							InvalidDataErrorf("%s: Texture %s is vertically tiled but marked as ClampT. This flag needs to be disabled in the TexOptEditor, or vertical tiling should be disabled in this assembly.", pTexAs->pchName, pchName);
						eaPush(&pTexInt->pFallback->eaTextures, pTexture);
					}
				}
			}
		}

		// Set up backpointers for fast traversal.
		for (j = 0; j < ARRAY_SIZE(apPos); j++)
		{
			UITextureInstancePosition *pPos = apPos[j];
			if (pPos->pchRelative && pPos->pchRelative != s_pchContent && pPos->pchRelative != g_ui_pchParent)
			{
				S32 k;

				for (k = 0; k < eaSize(&pTexAs->eaTextures) && !pPos->pRelative; k++)
				{
					if (pTexAs->eaTextures[k]->pchKey == pPos->pchRelative)
						pPos->pRelative = pTexAs->eaTextures[k];
				}
				if (!pPos->pRelative)
					InvalidDataErrorf("%s: Relative to %s but that instance was not found in this assembly.", pTexInt->pchKey, pPos->pchRelative);
			}
		}
	}

	TextureAssemblySort(pTexAs);
}

static void TextureAssemblyCheckReferences(UITextureAssembly *pTexAs)
{
	S32 i;

	for (i = 0; i < eaSize(&pTexAs->eaTextures); i++)
	{
		UITextureInstance *pTexInt = pTexAs->eaTextures[i];

		if (pTexInt->bIsSubAssembly)
		{
			if (!GET_REF(pTexInt->hSubAssembly))
				InvalidDataErrorf("%s: Unknown subassembly %s.", pTexAs->pchName, REF_STRING_FROM_HANDLE(pTexInt->hSubAssembly));
		}
	}
}

#define OFFSET_TO_POS(fOffset, fParent, fScale) ((fOffset) * (((fOffset) < 1 && (fOffset) > -1) ? (fParent) : (fScale)))

__forceinline static int lerpRGBAColorsRound(int a, int b, F32 weight)
{
	int out;
	U8* pout = (U8 *)(&out);
	U8* pa = (U8 *)(&a);
	U8* pb = (U8 *)(&b);
	int i;
	F32 weightinv = 1.f - weight;
	for (i = 0; i < 4; i++)
		pout[i] = (pa[i] * weightinv + pb[i] * weight + 0.5f);
	return out;
}

__forceinline static U32 ColorLerp2D(U32 uiTopLeft, U32 uiTopRight, U32 uiBottomLeft, U32 uiBottomRight, F32 fX, F32 fY)
{
	U32 uiLeft = lerpRGBAColorsRound(uiTopLeft, uiBottomLeft, fY);
	U32 uiRight = lerpRGBAColorsRound(uiTopRight, uiBottomRight, fY);
	return lerpRGBAColorsRound(uiLeft, uiRight, fX);
}

__forceinline static U32 ColorMultiply(U32 uiColor, U32 uiMult)
{
	U32 uiOut;
	U8 *pOut = (U8 *)(&uiOut);
	U8 *pColor = (U8 *)(&uiColor);
	U8 *pMult = (U8 *)(&uiMult);
	S32 i;
	for (i = 0; i < 4; i++)
		pOut[i] = (pColor[i] * pMult[i]) / 255.0;
	return uiOut;
}

__forceinline static void Color4Multiply(Color4 *pA, Color4 *pB, Color4 *pDest)
{
	if (!pA)
		*pDest = *pB;
	else if (!pB)
		*pDest = *pA;
	else
	{
		if (pA->uiTopLeftColor && pB->uiTopLeftColor)
			pDest->uiTopLeftColor = ColorMultiply(pA->uiTopLeftColor, pB->uiTopLeftColor);
		else if (pA->uiTopLeftColor)
			pDest->uiTopLeftColor = pA->uiTopLeftColor;
		else
			pDest->uiTopLeftColor = pB->uiTopLeftColor;

		if (pA->uiTopRightColor && pB->uiTopRightColor)
			pDest->uiTopRightColor = ColorMultiply(pA->uiTopRightColor, pB->uiTopRightColor);
		else if (pA->uiTopRightColor)
			pDest->uiTopRightColor = pA->uiTopRightColor;
		else
			pDest->uiTopRightColor = pB->uiTopRightColor;

		if (pA->uiBottomLeftColor && pB->uiBottomLeftColor)
			pDest->uiBottomLeftColor = ColorMultiply(pA->uiBottomLeftColor, pB->uiBottomLeftColor);
		else if (pA->uiBottomLeftColor)
			pDest->uiBottomLeftColor = pA->uiBottomLeftColor;
		else
			pDest->uiBottomLeftColor = pB->uiBottomLeftColor;

		if (pA->uiBottomRightColor && pB->uiBottomRightColor)
			pDest->uiBottomRightColor = ColorMultiply(pA->uiBottomRightColor, pB->uiBottomRightColor);
		else if (pA->uiBottomRightColor)
			pDest->uiBottomRightColor = pA->uiBottomRightColor;
		else
			pDest->uiBottomRightColor = pB->uiBottomRightColor;
	}
}

#define Color4Lerp2D(pColors, fX, fY) \
		ColorLerp2D( \
				  (pColors)->uiTopLeftColor, (pColors)->uiTopRightColor, \
				  (pColors)->uiBottomLeftColor, (pColors)->uiBottomRightColor, \
				  fX, fY)

__forceinline static void FindFallback(SA_PARAM_NN_VALID const UITextureInstanceFallback *pFallback,
									   const F32 fBoxWidth,
									   const F32 fBoxHeight,
									   const F32 fScale,
									   F32 fTexWidth,
									   F32 fTexHeight,
									   const bool bIsAtlas,
									   SA_PRE_NN_NN_VALID AtlasTex **ppAtlasTex,
									   SA_PRE_NN_NN_VALID BasicTexture **ppBasicTex
									   )
{
	const F32 fMaxWidth = fBoxWidth * pFallback->fOffset;
	const F32 fMaxHeight = fBoxHeight * pFallback->fOffset;
	S32 j;
	for (
		j = 0;
		j < eaSize(&pFallback->eaTextures)
			&& ((pFallback->eDirection == UIWidth
					&& fTexWidth * fScale > fMaxWidth)
				|| (pFallback->eDirection == UIHeight
					&& fTexHeight * fScale > fMaxHeight));
		j++)
	{
		if (!pFallback->eaTextures[j])
		{
			*ppAtlasTex = NULL;
			*ppBasicTex = NULL;
			return;
		}
		else if (bIsAtlas)
		{
			*ppAtlasTex = pFallback->eaTextures[j];
			fTexWidth = (*ppAtlasTex)->width;
			fTexHeight = (*ppAtlasTex)->height;
		}
		else
		{
			*ppBasicTex = pFallback->eaTextures[j];
			fTexWidth = (*ppBasicTex)->width;
			fTexHeight = (*ppBasicTex)->height;
		}
	}
}

__inline
void display_ninepatch_rot_sprite(AtlasTex *pAtlasTex, U32 x, U32 y, U32 auiColors[][4], F32 *afSizeX, F32 *afSizeY,
	F32 *afScaleX, F32 *afScaleY, F32 *afFinalX, F32 *afFinalY, F32 *afFinalU, F32 *afFinalV, F32 *afInsetU, F32 *afInsetV,
	F32 fCenterX, F32 fCenterY, F32 cosRot, F32 sinRot, F32 fRot, bool bAdditive, F32 fZ, F32 screenDist, bool is_3D)
{
	F32 halfWidth = afSizeX[x] / 2;
	F32 halfHeight = afSizeY[y] / 2;
	F32 relativeCenterX = afFinalX[x] + halfWidth - fCenterX;
	F32 relativeCenterY = afFinalY[y] + halfHeight - fCenterY;
	F32 rotatedCenterX = cosRot * relativeCenterX - sinRot * relativeCenterY;
	F32 rotatedCenterY = sinRot * relativeCenterX + cosRot * relativeCenterY;
	
	if (is_3D)
	{
		display_sprite_3d_ex(pAtlasTex, NULL, NULL, NULL,
			rotatedCenterX + fCenterX - halfWidth,
			rotatedCenterY + fCenterY - halfHeight,
			screenDist,
			afScaleX[x], afScaleY[y],
			auiColors[y][x], auiColors[y][x+1], auiColors[y+1][x+1], auiColors[y+1][x],
			afFinalU[x]+afInsetU[x], afFinalV[y]+afInsetV[y], afFinalU[x+1]-afInsetU[x], afFinalV[y+1]-afInsetV[y],
			0, 0, 1, 1,
			fRot, bAdditive, clipperGetCurrent());
	}
	else
	{
		display_sprite_ex(pAtlasTex, NULL, NULL, NULL,
			rotatedCenterX + fCenterX - halfWidth,
			rotatedCenterY + fCenterY - halfHeight,
			fZ,
			afScaleX[x], afScaleY[y],
			auiColors[y][x], auiColors[y][x+1], auiColors[y+1][x+1], auiColors[y+1][x],
			afFinalU[x]+afInsetU[x], afFinalV[y]+afInsetV[y], afFinalU[x+1]-afInsetU[x], afFinalV[y+1]-afInsetV[y],
			0, 0, 1, 1,
			fRot, bAdditive, clipperGetCurrent());
	}
}

__inline
void display_ninepatch_sprite(AtlasTex *pAtlasTex, U32 x, U32 y, U32 auiColors[][4],
	F32 *afScaleX, F32 *afScaleY, F32 *afFinalX, F32 *afFinalY, F32 *afFinalU, F32 *afFinalV, F32 *afInsetU, F32 *afInsetV,
	bool bAdditive, F32 fZ, F32 screenDist, bool is_3D)
{
	if (is_3D)
	{
		display_sprite_3d_ex(pAtlasTex, NULL, NULL, NULL,
			afFinalX[x], afFinalY[y],
			screenDist,
			afScaleX[x], afScaleY[y],
			auiColors[y][x], auiColors[y][x+1], auiColors[y+1][x+1], auiColors[y+1][x],
			afFinalU[x]+afInsetU[x], afFinalV[y]+afInsetV[y], afFinalU[x+1]-afInsetU[x], afFinalV[y+1]-afInsetV[y],
			0, 0, 1, 1,
			0, bAdditive, clipperGetCurrent());
	}
	else
	{
		display_sprite_ex(pAtlasTex, NULL, NULL, NULL,
			afFinalX[x], afFinalY[y],
			fZ,
			afScaleX[x], afScaleY[y],
			auiColors[y][x], auiColors[y][x+1], auiColors[y+1][x+1], auiColors[y+1][x],
			afFinalU[x]+afInsetU[x], afFinalV[y]+afInsetV[y], afFinalU[x+1]-afInsetU[x], afFinalV[y+1]-afInsetV[y],
			0, 0, 1, 1,
			0, bAdditive, clipperGetCurrent());
	}
}

// IF YOU CHANGE THIS FUNCTION, YOU SHOULD CHANGE ui_GenDrawNinePatchAtlas AND display_sprite_NinePatch_test
__forceinline static void DrawNinePatch(UITextureInstance *pTexInt, F32 fZ, F32 fCenterX, F32 fCenterY, F32 fRot, Color4 *pColors, F32 fScaleHorizontal, F32 fScaleVertical, bool bRequestAdditive, F32 screenDist, bool is_3D)
{
	AtlasTex *pAtlasTex = pTexInt->pAtlasTexture;
	const NinePatch *pNinePatch = pTexInt->pNinePatch;
	const F32 fX = pTexInt->Box.lx;
	const F32 fY = pTexInt->Box.ly;
	const F32 fWidth = pTexInt->Box.hx - pTexInt->Box.lx;
	const F32 fHeight = pTexInt->Box.hy - pTexInt->Box.ly;
	F32 afSizeX[3] = {pNinePatch->stretchableX[0] * fScaleHorizontal, -1, (pAtlasTex->width - pNinePatch->stretchableX[1] - 1)  * fScaleHorizontal};
	F32 afSizeY[3] = {pNinePatch->stretchableY[0] * fScaleVertical, -1, (pAtlasTex->height - pNinePatch->stretchableY[1] - 1) * fScaleVertical};
	F32 afScaleX[3];
	F32 afScaleY[3];
	F32 afU[2];
	F32 afV[2];
	const F32 fDeltaU = 0.5f / pAtlasTex->width;
	const F32 fDeltaV = 0.5f / pAtlasTex->height;
	U32 auiColors[4][4]; // vertex color, [Y][X] left to right, top to bottom
	const bool bFlipX = pTexInt->bFlipX;
	const bool bFlipY = pTexInt->bFlipY;
	F32 afFinalX[3];
	F32 afFinalY[3];
	F32 afFinalU[4];
	F32 afFinalV[4];
	F32 afInsetU[3] = {0};
	F32 afInsetV[3] = {0};
	bool bAdditive = pTexInt->bResetAdditive ? pTexInt->bAdditive : (pTexInt->bAdditive || bRequestAdditive);
	if(fWidth <= 0.f || fHeight <= 0.f){
		return;
	}

	// Size
	{
		if (bFlipX)
			SWAPF32(afSizeX[0], afSizeX[2]);
		if (bFlipY)
			SWAPF32(afSizeY[0], afSizeY[2]);

		afSizeX[1] = fWidth - (afSizeX[2] + afSizeX[0]);
		afSizeY[1] = fHeight - (afSizeY[2] + afSizeY[0]);
		MAX1F(afSizeX[1], 0);
		MAX1F(afSizeY[1], 0);
		if (afSizeX[0] + afSizeX[2] > fWidth)
			scaleVec3(afSizeX, fWidth / (float)(afSizeX[0] + afSizeX[2]), afSizeX);
		if (afSizeY[0] + afSizeY[2] > fHeight)
			scaleVec3(afSizeY, fHeight / (float)(afSizeY[0] + afSizeY[2]), afSizeY);
		scaleVec3(afSizeX, 1.f/pAtlasTex->width, afScaleX);
		scaleVec3(afSizeY, 1.f/pAtlasTex->height, afScaleY);
		afFinalX[0] = fX;
		afFinalX[1] = fX + afSizeX[0];
		afFinalX[2] = fX + (afSizeX[0] + afSizeX[1]);
		afFinalY[0] = fY;
		afFinalY[1] = fY + afSizeY[0];
		afFinalY[2] = fY + (afSizeY[0] + afSizeY[1]);
	}

	// UVs
	{
		scaleVec2(pNinePatch->stretchableX, 1.f/pAtlasTex->width, afU);
		afFinalU[0] = 0;
		afFinalU[1] = afU[0];
		afFinalU[2] = afU[1]+1.f/pAtlasTex->width;
		afFinalU[3] = 1;
		afInsetU[1] = fDeltaU;
		if (bFlipX)
		{
			SWAPF32(afFinalU[0], afFinalU[3]);
			SWAPF32(afFinalU[1], afFinalU[2]);
			afInsetU[1] = -fDeltaU;
		}

		scaleVec2(pNinePatch->stretchableY, 1.f/pAtlasTex->height, afV);
		afFinalV[0] = 0;
		afFinalV[1] = afV[0];
		afFinalV[2] = afV[1]+1.f/pAtlasTex->height;
		afFinalV[3] = 1;
		afInsetV[1] = fDeltaV;
		if (bFlipY)
		{
			SWAPF32(afFinalV[0], afFinalV[3]);
			SWAPF32(afFinalV[1], afFinalV[2]);
			afInsetV[1] = -fDeltaV;
		}
	}

	// Colors
	{
		auiColors[0][0] = pColors->uiTopLeftColor;
		auiColors[0][1] = Color4Lerp2D(pColors, afSizeX[0] / fWidth, 0);
		auiColors[0][2] = Color4Lerp2D(pColors, (afSizeX[0] + afSizeX[1]) / fWidth, 0);
		auiColors[0][3] = pColors->uiTopRightColor;

		auiColors[1][0] = Color4Lerp2D(pColors, 0, afSizeY[0] / fHeight);
		auiColors[1][1] = Color4Lerp2D(pColors, afSizeX[0] / fWidth, afSizeY[0] / fHeight);
		auiColors[1][2] = Color4Lerp2D(pColors, (afSizeX[0] + afSizeX[1]) / fWidth, afSizeY[0] / fHeight);
		auiColors[1][3] = Color4Lerp2D(pColors, 1, afSizeY[0] / fHeight);

		auiColors[2][0] = Color4Lerp2D(pColors, 0, (afSizeY[0] + afSizeY[1]) / fHeight);
		auiColors[2][1] = Color4Lerp2D(pColors, afSizeX[0] / fWidth, (afSizeY[0] + afSizeY[1]) / fHeight);
		auiColors[2][2] = Color4Lerp2D(pColors, (afSizeX[0] + afSizeX[1]) / fWidth, (afSizeY[0] + afSizeY[1]) / fHeight);
		auiColors[2][3] = Color4Lerp2D(pColors, 1, (afSizeY[0] + afSizeY[1]) / fHeight);

		auiColors[3][0] = pColors->uiBottomLeftColor;
		auiColors[3][1] = Color4Lerp2D(pColors, afSizeX[0] / fWidth, 1);
		auiColors[3][2] = Color4Lerp2D(pColors, (afSizeX[0] + afSizeX[1]) / fWidth, 1);
		auiColors[3][3] = pColors->uiBottomRightColor;
	}

	if( fRot != 0 ) {
		float cosRot = cosf( fRot );
		float sinRot = sinf( fRot );

#define DISPLAY_NINEPATCH_PATCH_ROT(x, y)								\
	display_ninepatch_rot_sprite(pAtlasTex, x, y, auiColors, afSizeX, afSizeY,	\
		afScaleX, afScaleY, afFinalX, afFinalY, afFinalU, afFinalV, afInsetU, afInsetV,	\
		fCenterX, fCenterY, cosRot, sinRot, fRot, bAdditive, fZ, screenDist, is_3D);

		DISPLAY_NINEPATCH_PATCH_ROT(0, 0);
		DISPLAY_NINEPATCH_PATCH_ROT(1, 0);
		DISPLAY_NINEPATCH_PATCH_ROT(2, 0);
		if (afScaleY[1] > 0)
		{
			DISPLAY_NINEPATCH_PATCH_ROT(0, 1);
			DISPLAY_NINEPATCH_PATCH_ROT(1, 1);
			DISPLAY_NINEPATCH_PATCH_ROT(2, 1);
		}
		DISPLAY_NINEPATCH_PATCH_ROT(0, 2);
		DISPLAY_NINEPATCH_PATCH_ROT(1, 2);
		DISPLAY_NINEPATCH_PATCH_ROT(2, 2);
#undef DISPLAY_NINEPATCH_PATCH_ROT
	} else {
#define DISPLAY_NINEPATCH_PATCH(x, y)												\
	display_ninepatch_sprite(pAtlasTex, x, y, auiColors, \
		afScaleX, afScaleY, afFinalX, afFinalY, afFinalU, afFinalV, afInsetU, afInsetV, \
		bAdditive, fZ, screenDist, is_3D);

		DISPLAY_NINEPATCH_PATCH(0, 0);
		DISPLAY_NINEPATCH_PATCH(1, 0);
		DISPLAY_NINEPATCH_PATCH(2, 0);
		if (afScaleY[1] > 0)
		{
			DISPLAY_NINEPATCH_PATCH(0, 1);
			DISPLAY_NINEPATCH_PATCH(1, 1);
			DISPLAY_NINEPATCH_PATCH(2, 1);
		}
		DISPLAY_NINEPATCH_PATCH(0, 2);
		DISPLAY_NINEPATCH_PATCH(1, 2);
		DISPLAY_NINEPATCH_PATCH(2, 2);
#undef DISPLAY_NINEPATCH_PATCH
	}

}

static void ui_TextureAssemblyDrawInternal(UITextureAssembly *pTexAs, const CBox *pBox, CBox *pOutBox, F32 fCenterX, F32 fCenterY, F32 fRot, F32 fScale, F32 fMinZ, F32 fMaxZ, char chAlpha, Color4 *pTint, bool bRequestAdditive, F32 screenDist, bool is_3D)
{
	Color4 Colors, TintColors;
	S32 i;
	F32 fCountZ = (fMaxZ - fMinZ) / pTexAs->iMaxZ;
	Clipper2D *pClipper = clipperGetCurrent();

	if (pBox->lx == pBox->hx
		|| pBox->ly == pBox->hy
		|| (pTint
			&& pTint->uiTopLeftColor && !(pTint->uiTopLeftColor & 0xFF)
			&& pTint->uiTopRightColor && !(pTint->uiTopRightColor & 0xFF)
			&& pTint->uiBottomLeftColor && !(pTint->uiBottomLeftColor & 0xFF)
			&& pTint->uiBottomRightColor && !(pTint->uiBottomRightColor & 0xFF)))
		return;

	if (fCountZ < 0.000001)
		ErrorFilenamef(pTexAs->pchFilename, "%s: Texture assembly min/max Z is going to cause Z fighting", pTexAs->pchName);

	// If we have a forced multiple, grow until we meet it.
	if (pTexAs->v2chHorizontalMultiple[0] || pTexAs->v2chVerticalMultiple[0])
	{
		CBox CopyBox = *pBox;
		pBox = &CopyBox;

		if (pTexAs->v2chHorizontalMultiple[0])
		{
			S32 iMultiple = round(pTexAs->v2chHorizontalMultiple[0] * fScale);
			S32 iExtra = round(pTexAs->v2chHorizontalMultiple[1] * fScale);
			S32 iDiff = iMultiple - (S32)(CBoxWidth(pBox) - iExtra) % iMultiple;
			if (iDiff < iMultiple)
			{
				CopyBox.lx -= iDiff / 2;
				CopyBox.hx += iDiff / 2;
			}
		}
		if (pTexAs->v2chVerticalMultiple[0])
		{
			S32 iMultiple = round(pTexAs->v2chVerticalMultiple[0] * fScale);
			S32 iExtra = round(pTexAs->v2chVerticalMultiple[1] * fScale);
			S32 iDiff = iMultiple - (S32)(CBoxHeight(pBox) - iExtra) % iMultiple;
			if (iDiff < iMultiple)
			{
				CopyBox.ly -= (iDiff + 1) / 2;
				CopyBox.hy += iDiff / 2;
			}
		}
	}

	if (pTint)
	{
		Color4 TexAsColors;
		TexAsColors.uiTopLeftColor = ui_StyleColorPaletteIndex(pTexAs->Colors.uiTopLeftColor);
		TexAsColors.uiTopRightColor = ui_StyleColorPaletteIndex(pTexAs->Colors.uiTopRightColor);
		TexAsColors.uiBottomLeftColor = ui_StyleColorPaletteIndex(pTexAs->Colors.uiBottomLeftColor);
		TexAsColors.uiBottomRightColor = ui_StyleColorPaletteIndex(pTexAs->Colors.uiBottomRightColor);
		TintColors.uiTopLeftColor = ui_StyleColorPaletteIndex(pTint->uiTopLeftColor);
		TintColors.uiTopRightColor = ui_StyleColorPaletteIndex(pTint->uiTopRightColor);
		TintColors.uiBottomLeftColor = ui_StyleColorPaletteIndex(pTint->uiBottomLeftColor);
		TintColors.uiBottomRightColor = ui_StyleColorPaletteIndex(pTint->uiBottomRightColor);
		pTint = &TintColors;
		Color4Multiply(&TexAsColors, pTint, &Colors);
	}
	else
	{
		Colors.uiTopLeftColor = ui_StyleColorPaletteIndex(pTexAs->Colors.uiTopLeftColor);
		Colors.uiTopRightColor = ui_StyleColorPaletteIndex(pTexAs->Colors.uiTopRightColor);
		Colors.uiBottomLeftColor = ui_StyleColorPaletteIndex(pTexAs->Colors.uiBottomLeftColor);
		Colors.uiBottomRightColor = ui_StyleColorPaletteIndex(pTexAs->Colors.uiBottomRightColor);
	}
	// Colors & pTint have both been looked up in the palette

	// Calculate the CBox for each texture in topological dependency order.
	// Draw them at the same time, since we have pre-calculated Z values.

	PERFINFO_AUTO_START("TextureAssembly: Layout", eaSize(&pTexAs->eaSortedByDependency));
	for (i = 0; i < eaSize(&pTexAs->eaSortedByDependency); i++)
	{
		UITextureInstance *pTexInt = pTexAs->eaSortedByDependency[i];
		AtlasTex *pAtlasTexture = ui_TextureInstanceAtlasTex(pTexInt);
		BasicTexture *pBasicTexture = ui_TextureInstanceBasicTex(pTexInt);
		F32 fTexWidth = pAtlasTexture ? pAtlasTexture->width : pBasicTexture ? pBasicTexture->width : 0;
		F32 fTexHeight = pAtlasTexture ? pAtlasTexture->height : pBasicTexture ? pBasicTexture->height : 0;
		F32 fScaleVertical = (pTexInt->bResetScale ? 1 : fScale) * pTexInt->fScaleVertical;
		F32 fScaleHorizontal = (pTexInt->bResetScale ? 1 : fScale) * pTexInt->fScaleHorizontal;
		F32 fOffset;
		const CBox *pOffsetBox;

		// By default, subassemblies are full-sized (unlike textures which are zero-sized).
		if (pTexInt->bIsSubAssembly)
			pTexInt->Box = *pBox;

		if (pTexInt->pFallback)
		{
			FindFallback(pTexInt->pFallback,
				CBoxWidth(pBox), CBoxHeight(pBox), fScale,
				fTexWidth, fTexHeight, pTexInt->bIsAtlas,
				&pAtlasTexture, &pBasicTexture);
			fTexWidth = pAtlasTexture ? pAtlasTexture->width : pBasicTexture ? pBasicTexture->width : 0;
			fTexHeight = pAtlasTexture ? pAtlasTexture->height : pBasicTexture ? pBasicTexture->height : 0;
		}

		if ((fTexWidth == 0 || fTexHeight == 0) && !pTexInt->bIsSubAssembly)
			continue;

		// Figure out the locations of specified sides.
		if (pTexInt->LeftFrom.pchRelative)
		{
			if (pTexInt->LeftFrom.pRelative)
				pOffsetBox = &pTexInt->LeftFrom.pRelative->Box;
			else
				pOffsetBox = pBox;
			fOffset = OFFSET_TO_POS(pTexInt->LeftFrom.fOffset, CBoxWidth(pOffsetBox), fScale);
			if (pTexInt->LeftFrom.eDirection == UILeft)
				pTexInt->Box.lx = pOffsetBox->lx + fOffset;
			else if (pTexInt->LeftFrom.eDirection == UIRight)
				pTexInt->Box.lx = pOffsetBox->hx + fOffset;
			else
				pTexInt->Box.lx = (pOffsetBox->lx + pOffsetBox->hx) / 2 + fOffset;
		}

		if (pTexInt->RightFrom.pchRelative)
		{
			if (pTexInt->RightFrom.pRelative)
				pOffsetBox = &pTexInt->RightFrom.pRelative->Box;
			else
				pOffsetBox = pBox;
			fOffset = OFFSET_TO_POS(pTexInt->RightFrom.fOffset, CBoxWidth(pOffsetBox), fScale);
			if (pTexInt->RightFrom.eDirection == UILeft)
				pTexInt->Box.hx = pOffsetBox->lx - fOffset;
			else if (pTexInt->RightFrom.eDirection == UIRight)
				pTexInt->Box.hx = pOffsetBox->hx - fOffset;
			else
				pTexInt->Box.hx = (pOffsetBox->lx + pOffsetBox->hx) / 2 - fOffset;
		}

		if (pTexInt->TopFrom.pchRelative)
		{
			if (pTexInt->TopFrom.pRelative)
				pOffsetBox = &pTexInt->TopFrom.pRelative->Box;
			else
				pOffsetBox = pBox;
			fOffset = OFFSET_TO_POS(pTexInt->TopFrom.fOffset, CBoxHeight(pOffsetBox), fScale);
			if (pTexInt->TopFrom.eDirection == UITop)
				pTexInt->Box.ly = pOffsetBox->ly + fOffset;
			else if (pTexInt->TopFrom.eDirection == UIBottom)
				pTexInt->Box.ly = pOffsetBox->hy + fOffset;
			else
				pTexInt->Box.ly = (pOffsetBox->ly + pOffsetBox->hy) / 2 + fOffset;
		}

		if (pTexInt->BottomFrom.pchRelative)
		{
			if (pTexInt->BottomFrom.pRelative)
				pOffsetBox = &pTexInt->BottomFrom.pRelative->Box;
			else
				pOffsetBox = pBox;
			fOffset = OFFSET_TO_POS(pTexInt->BottomFrom.fOffset, CBoxHeight(pOffsetBox), fScale);
			if (pTexInt->BottomFrom.eDirection == UITop)
				pTexInt->Box.hy = pOffsetBox->ly - fOffset;
			else if (pTexInt->BottomFrom.eDirection == UIBottom)
				pTexInt->Box.hy = pOffsetBox->hy - fOffset;
			else
				pTexInt->Box.hy = (pOffsetBox->ly + pOffsetBox->hy) / 2 - fOffset;
		}

		if (pTexInt->HorizontalCenterFrom.pchRelative)
		{
			F32 fWidth = fTexWidth * fScaleHorizontal;
			if (pTexInt->HorizontalCenterFrom.pRelative)
				pOffsetBox = &pTexInt->HorizontalCenterFrom.pRelative->Box;
			else
				pOffsetBox = pBox;
			fOffset = OFFSET_TO_POS(pTexInt->HorizontalCenterFrom.fOffset, CBoxWidth(pOffsetBox), fScale);
			if (pTexInt->HorizontalCenterFrom.eDirection == UILeft)
			{
				pTexInt->Box.lx = pOffsetBox->lx + fOffset - fWidth / 2;
				pTexInt->Box.hx = pOffsetBox->lx + fOffset + fWidth / 2;
			}
			else if (pTexInt->HorizontalCenterFrom.eDirection == UIRight)
			{
				pTexInt->Box.lx = pOffsetBox->hx - fOffset - fWidth / 2;
				pTexInt->Box.hx = pOffsetBox->hx - fOffset + fWidth / 2;
			}
			else
			{
				pTexInt->Box.lx = (pOffsetBox->lx + pOffsetBox->hx) / 2 + fOffset - fWidth / 2;
				pTexInt->Box.hx = (pOffsetBox->lx + pOffsetBox->hx) / 2 + fOffset + fWidth / 2;
			}
		}

		if (pTexInt->VerticalCenterFrom.pchRelative)
		{
			F32 fHeight = fTexHeight * fScaleVertical;
			if (pTexInt->VerticalCenterFrom.pRelative)
				pOffsetBox = &pTexInt->VerticalCenterFrom.pRelative->Box;
			else
				pOffsetBox = pBox;
			fOffset = OFFSET_TO_POS(pTexInt->VerticalCenterFrom.fOffset, CBoxHeight(pOffsetBox), fScale);
			if (pTexInt->VerticalCenterFrom.eDirection == UITop)
			{
				pTexInt->Box.ly = pOffsetBox->ly + fOffset - fHeight / 2;
				pTexInt->Box.hy = pOffsetBox->ly + fOffset + fHeight / 2;
			}
			else if (pTexInt->VerticalCenterFrom.eDirection == UIBottom)
			{
				pTexInt->Box.ly = pOffsetBox->hy - fOffset - fHeight / 2;
				pTexInt->Box.hy = pOffsetBox->hy - fOffset + fHeight / 2;
			}
			else
			{
				pTexInt->Box.ly = (pOffsetBox->ly + pOffsetBox->hy) / 2 - fOffset - fHeight / 2;
				pTexInt->Box.hy = (pOffsetBox->ly + pOffsetBox->hy) / 2 - fOffset + fHeight / 2;
			}
		}

		if (!pTexInt->bIsSubAssembly)
		{
			// If only one side was specified, use the texture size to fill in the other.
			if (pTexInt->LeftFrom.pchRelative && !pTexInt->RightFrom.pchRelative)
				pTexInt->Box.hx = pTexInt->Box.lx + fTexWidth * fScaleHorizontal;
			else if (pTexInt->RightFrom.pchRelative && !pTexInt->LeftFrom.pchRelative)
				pTexInt->Box.lx = pTexInt->Box.hx - fTexWidth * fScaleHorizontal;

			if (pTexInt->TopFrom.pchRelative && !pTexInt->BottomFrom.pchRelative)
				pTexInt->Box.hy = pTexInt->Box.ly + fTexHeight * fScaleVertical;
			else if (!pTexInt->TopFrom.pchRelative && pTexInt->BottomFrom.pchRelative)
				pTexInt->Box.ly = pTexInt->Box.hy - fTexHeight * fScaleVertical;
		}
	}

	PERFINFO_AUTO_STOP();

	for (i = 0; i < eaSize(&pTexAs->eaSortedByDrawZ); i++)
	{
		UITextureInstance *pTexInt = pTexAs->eaSortedByDrawZ[i];
		AtlasTex *pAtlasTexture = ui_TextureInstanceAtlasTex(pTexInt);
		BasicTexture *pBasicTexture = ui_TextureInstanceBasicTex(pTexInt);
		F32 fTexWidth = pAtlasTexture ? pAtlasTexture->width : pBasicTexture ? pBasicTexture->width : 0;
		F32 fTexHeight = pAtlasTexture ? pAtlasTexture->height : pBasicTexture ? pBasicTexture->height : 0;
		F32 fScaleVertical = (pTexInt->bResetScale ? 1 : fScale) * pTexInt->fScaleVertical;
		F32 fScaleHorizontal = (pTexInt->bResetScale ? 1 : fScale) * pTexInt->fScaleHorizontal;
		U32 uiTopLeftColor = ColorRGBAMultiplyAlpha(ui_StyleColorPaletteIndex(pTexInt->uiTopLeftColor), chAlpha);
		U32 uiTopRightColor = ColorRGBAMultiplyAlpha(ui_StyleColorPaletteIndex(pTexInt->uiTopRightColor), chAlpha);
		U32 uiBottomRightColor = ColorRGBAMultiplyAlpha(ui_StyleColorPaletteIndex(pTexInt->uiBottomRightColor), chAlpha);
		U32 uiBottomLeftColor = ColorRGBAMultiplyAlpha(ui_StyleColorPaletteIndex(pTexInt->uiBottomLeftColor), chAlpha);
		F32 fZ = fMinZ + (pTexInt->iZ * fCountZ);
		Color4 *pColors = &Colors;
		bool bAdditive = pTexInt->bResetAdditive ? pTexInt->bAdditive : (pTexInt->bAdditive || bRequestAdditive);

		PERFINFO_AUTO_START("TextureAssembly: Drawing", 1);

		if (pTexInt->pFallback)
		{
			FindFallback(pTexInt->pFallback,
				CBoxWidth(pBox), CBoxHeight(pBox), fScale,
				fTexWidth, fTexHeight, pTexInt->bIsAtlas,
				&pAtlasTexture, &pBasicTexture);
			fTexWidth = pAtlasTexture ? pAtlasTexture->width : pBasicTexture ? pBasicTexture->width : 0;
			fTexHeight = pAtlasTexture ? pAtlasTexture->height : pBasicTexture ? pBasicTexture->height : 0;
		}

		if ((fTexWidth == 0 || fTexHeight == 0) && !pTexInt->bIsSubAssembly)
			continue;

		if (pTexInt->bClip)
			clipperPushRestrict(pBox);

		// Ignore parent coloring, but not override coloring.
		if (pTexInt->bResetColor)
			pColors = pTint;

		// If we have a global color fade, use it.
		if (pColors && pColors->uiTopLeftColor)
		{
			F32 fTop = CLAMP((pTexInt->Box.ly - pBox->ly) / CBoxHeight(pBox), 0, 1);
			F32 fBottom = CLAMP((pTexInt->Box.hy - pBox->ly) / CBoxHeight(pBox), 0, 1);
			F32 fLeft = CLAMP((pTexInt->Box.lx - pBox->lx) / CBoxWidth(pBox), 0, 1);
			F32 fRight = CLAMP((pTexInt->Box.hx - pBox->lx) / CBoxWidth(pBox), 0, 1);

			// The final top left color is a lerp of the top left and top right,
			// lerped with a lerp of top left and bottom left. Similarly for the
			// other corners.
			uiTopLeftColor = ColorMultiply(uiTopLeftColor,
				ColorLerp2D(
				pColors->uiTopLeftColor, pColors->uiTopRightColor,
				pColors->uiBottomLeftColor, pColors->uiBottomRightColor,
				fLeft, fTop));
			uiTopRightColor = ColorMultiply(uiTopRightColor,
				ColorLerp2D(
				pColors->uiTopLeftColor, pColors->uiTopRightColor,
				pColors->uiBottomLeftColor, pColors->uiBottomRightColor,
				fRight, fTop));
			uiBottomLeftColor =  ColorMultiply(uiBottomLeftColor,
				ColorLerp2D(
				pColors->uiTopLeftColor, pColors->uiTopRightColor,
				pColors->uiBottomLeftColor, pColors->uiBottomRightColor,
				fLeft, fBottom));
			uiBottomRightColor = ColorMultiply(uiBottomRightColor,
				ColorLerp2D(
				pColors->uiTopLeftColor, pColors->uiTopRightColor,
				pColors->uiBottomLeftColor, pColors->uiBottomRightColor,
				fRight, fBottom));
		}

		if (pTexInt->bIsSubAssembly)
		{
			UITextureAssembly *pSubAs = GET_REF(pTexInt->hSubAssembly);
			// Stop counters here to avoid a tree display in the profiler.
			PERFINFO_AUTO_STOP();
			if (pSubAs)
			{
				F32 fSubMinZ = fZ;
				F32 fSubMaxZ = fZ + 0.95 * fCountZ;
				Color4 SubColors = { uiTopLeftColor, uiTopRightColor, uiBottomRightColor, uiBottomLeftColor };
				ui_TextureAssemblyDrawInternal(pSubAs, &pTexInt->Box, NULL, fCenterX, fCenterY, fRot, pTexInt->bResetScale ? 1 : fScale, fSubMinZ, fSubMaxZ, chAlpha, &SubColors, bAdditive, screenDist, is_3D);
			}
		}
		else if (pTexInt->pNinePatch)
		{
			Color4 SubColors = { uiTopLeftColor, uiTopRightColor, uiBottomRightColor, uiBottomLeftColor };
			DrawNinePatch(pTexInt, fZ, fCenterX, fCenterY, fRot, &SubColors, fScaleHorizontal, fScaleVertical, bAdditive, screenDist, is_3D);
			PERFINFO_AUTO_STOP();
		}
		else
		{
			fScaleHorizontal *= pTexInt->bFlipX ? -1 : 1;
			fScaleVertical *= pTexInt->bFlipY ? -1 : 1;
			PERFINFO_AUTO_STOP();
			if( fRot ) {
				float cosRot = cosf( fRot );
				float sinRot = sinf( fRot );
				float boxHalfWidth = (pTexInt->Box.hx - pTexInt->Box.lx) / 2;
				float boxHalfHeight = (pTexInt->Box.hy - pTexInt->Box.ly) / 2;
				float relativeTexCenterX = pTexInt->Box.lx + boxHalfWidth - fCenterX;
				float relativeTexCenterY = pTexInt->Box.ly + boxHalfHeight - fCenterY;
				float rotatedTexCenterX = cosRot * relativeTexCenterX - sinRot * relativeTexCenterY;
				float rotatedTexCenterY = sinRot * relativeTexCenterX + cosRot * relativeTexCenterY;

				if (is_3D)
				{
					display_sprite_3d_ex(
						pAtlasTexture,
						pBasicTexture,
						NULL, NULL,
						rotatedTexCenterX + fCenterX - boxHalfWidth,
						rotatedTexCenterY + fCenterY - boxHalfHeight,
						screenDist,
						(CBoxWidth(&pTexInt->Box) / fTexWidth),
						(CBoxHeight(&pTexInt->Box) / fTexHeight),
						uiTopLeftColor,
						uiTopRightColor,
						uiBottomRightColor,
						uiBottomLeftColor,
						!!pTexInt->bFlipX, !!pTexInt->bFlipY,
						(pTexInt->eModeHorizontal == UITextureModeTiled) ? (CBoxWidth(&pTexInt->Box) / (fTexWidth * fScaleHorizontal)) : 1 - !!pTexInt->bFlipX,
						(pTexInt->eModeVertical == UITextureModeTiled) ? (CBoxHeight(&pTexInt->Box) / (fTexHeight * fScaleVertical)) : 1 - !!pTexInt->bFlipY,
						0, 0, 1, 1,
						UI_ANGLE_TO_RAD(pTexInt->Rotation) + fRot, 
						bAdditive,
						pClipper);
				}
				else
				{
					display_sprite_ex(
						pAtlasTexture,
						pBasicTexture,
						NULL, NULL,
						rotatedTexCenterX + fCenterX - boxHalfWidth,
						rotatedTexCenterY + fCenterY - boxHalfHeight,
						fZ,
						(CBoxWidth(&pTexInt->Box) / fTexWidth),
						(CBoxHeight(&pTexInt->Box) / fTexHeight),
						uiTopLeftColor,
						uiTopRightColor,
						uiBottomRightColor,
						uiBottomLeftColor,
						!!pTexInt->bFlipX, !!pTexInt->bFlipY,
						(pTexInt->eModeHorizontal == UITextureModeTiled) ? (CBoxWidth(&pTexInt->Box) / (fTexWidth * fScaleHorizontal)) : 1 - !!pTexInt->bFlipX,
						(pTexInt->eModeVertical == UITextureModeTiled) ? (CBoxHeight(&pTexInt->Box) / (fTexHeight * fScaleVertical)) : 1 - !!pTexInt->bFlipY,
						0, 0, 1, 1,
						UI_ANGLE_TO_RAD(pTexInt->Rotation) + fRot, 
						bAdditive,
						pClipper);
				}
			} else {
				if (is_3D)
				{
					display_sprite_3d_ex(
						pAtlasTexture,
						pBasicTexture,
						NULL, NULL,
						pTexInt->Box.lx, pTexInt->Box.ly,
						screenDist,
						(CBoxWidth(&pTexInt->Box) / fTexWidth),
						(CBoxHeight(&pTexInt->Box) / fTexHeight),
						uiTopLeftColor,
						uiTopRightColor,
						uiBottomRightColor,
						uiBottomLeftColor,
						!!pTexInt->bFlipX, !!pTexInt->bFlipY,
						(pTexInt->eModeHorizontal == UITextureModeTiled) ? (CBoxWidth(&pTexInt->Box) / (fTexWidth * fScaleHorizontal)) : 1 - !!pTexInt->bFlipX,
						(pTexInt->eModeVertical == UITextureModeTiled) ? (CBoxHeight(&pTexInt->Box) / (fTexHeight * fScaleVertical)) : 1 - !!pTexInt->bFlipY,
						0, 0, 1, 1,
						UI_ANGLE_TO_RAD(pTexInt->Rotation), 
						bAdditive,
						pClipper);
				}
				else
				{
					display_sprite_ex(
						pAtlasTexture,
						pBasicTexture,
						NULL, NULL,
						pTexInt->Box.lx, pTexInt->Box.ly,
						fZ,
						(CBoxWidth(&pTexInt->Box) / fTexWidth),
						(CBoxHeight(&pTexInt->Box) / fTexHeight),
						uiTopLeftColor,
						uiTopRightColor,
						uiBottomRightColor,
						uiBottomLeftColor,
						!!pTexInt->bFlipX, !!pTexInt->bFlipY,
						(pTexInt->eModeHorizontal == UITextureModeTiled) ? (CBoxWidth(&pTexInt->Box) / (fTexWidth * fScaleHorizontal)) : 1 - !!pTexInt->bFlipX,
						(pTexInt->eModeVertical == UITextureModeTiled) ? (CBoxHeight(&pTexInt->Box) / (fTexHeight * fScaleVertical)) : 1 - !!pTexInt->bFlipY,
						0, 0, 1, 1,
						UI_ANGLE_TO_RAD(pTexInt->Rotation), 
						bAdditive,
						pClipper);
				}
			}
		}

		if (pTexInt->bClip)
			clipperPop();
	}

	if (pOutBox)
	{
		*pOutBox = *pBox;
		pOutBox->lx += pTexAs->iPaddingLeft * fScale;
		pOutBox->hx -= pTexAs->iPaddingRight * fScale;
		pOutBox->ly += pTexAs->iPaddingTop * fScale;
		pOutBox->hy -= pTexAs->iPaddingBottom * fScale;
	}
}

void ui_TextureAssemblyDrawEx(UITextureAssembly *pTexAs, const CBox *pBox, CBox *pOutBox, F32 fScale, F32 fMinZ, F32 fMaxZ, char chAlpha, Color4 *pTint, F32 screenDist, bool is_3D)
{
	PERFINFO_AUTO_START_FUNC();
	if (pTexAs && chAlpha && !gbNoGraphics)
		ui_TextureAssemblyDrawInternal(pTexAs, pBox, pOutBox, 0, 0, 0, fScale, fMinZ, fMaxZ, chAlpha, pTint, false, screenDist, is_3D);
	PERFINFO_AUTO_STOP_FUNC();
}

void ui_TextureAssemblyDrawRot(UITextureAssembly *pTexAs, const CBox *pBox, float centerX, float centerY, float rot, CBox *pOutBox, F32 fScale, F32 fMinZ, F32 fMaxZ, char chAlpha, Color4 *pTint)
{
	PERFINFO_AUTO_START_FUNC();
	if (pTexAs && chAlpha && !gbNoGraphics)
		ui_TextureAssemblyDrawInternal(pTexAs, pBox, pOutBox, centerX, centerY, rot, fScale, fMinZ, fMaxZ, chAlpha, pTint, false, 0.0, false);
	PERFINFO_AUTO_STOP_FUNC();
}

UITextureInstance *ui_TextureAssemblyGetMouseRegion(UITextureAssembly *pTexAs)
{
	int i;
	if (pTexAs)
	{
		for (i = eaSize(&pTexAs->eaTextures)-1; i >= 0; i--)
		{
			UITextureInstance *pInstance = pTexAs->eaTextures[i];
			if (pInstance->iMouseRegion)
				return pInstance;
		}
	}
	return NULL;
}


static DictionaryHandle s_TexAsDict;

static int TextureAssemblyValidate(enumResourceValidateType eType, const char *pDictName, const char *pTexAsName, UITextureAssembly *pTexAs, U32 iUserID)
{
	switch (eType)
	{
	case RESVALIDATE_POST_TEXT_READING:
		TextureAssemblyPostTextRead(pTexAs);
		return VALIDATE_HANDLED;
	case RESVALIDATE_POST_BINNING:
		TextureAssemblyPostBinning(pTexAs);
		return VALIDATE_HANDLED;
	case RESVALIDATE_CHECK_REFERENCES:
		TextureAssemblyCheckReferences(pTexAs);
		break;
	}
	return VALIDATE_NOT_HANDLED;
}

AUTO_RUN;
void ui_TextureAssemblyRegister(void)
{
	s_TexAsDict = RefSystem_RegisterSelfDefiningDictionary("UITextureAssembly", false, parse_UITextureAssembly, true, true, NULL);
	s_pchContent = allocAddStaticString("_Content");
}

void ui_StyleLoadColorPalettes(void);

AUTO_STARTUP(UITextureAssembly) ASTRT_DEPS(Colors UISize GraphicsLib);
void ui_TextureAssemblyLoad(void)
{
	if(!gbNoGraphics)
	{
		ui_StyleLoadColorPalettes();
		resDictManageValidation(s_TexAsDict, TextureAssemblyValidate);
		resLoadResourcesFromDisk(s_TexAsDict, "ui/assembly/", ".texas", NULL, RESOURCELOAD_USEOVERLAYS);
	}
}

//////////////////////////////////////////////////////////////////////////
// Assembly Debugging tool / browser.

typedef struct TextureAssemblyPane
{
	UIWidget widget;
	REF_TO(UITextureAssembly) hDef;
	UISlider *pScale;
	UISlider *pAlpha;
	UIColorButton *pTopLeft;
	UIColorButton *pTopRight;
	UIColorButton *pBottomRight;
	UIColorButton *pBottomLeft;
} TextureAssemblyPane;

static void TextureAssemblyPaneFree(TextureAssemblyPane *pPane)
{
	REMOVE_HANDLE(pPane->hDef);
	ui_WidgetFreeInternal(UI_WIDGET(pPane));
}

static void TextureAssemblyPaneDraw(TextureAssemblyPane *pPane, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(pPane);
	UITextureAssembly *pDef = GET_REF(pPane->hDef);
	UI_DRAW_EARLY(pPane);
	if (pDef)
	{
		F32 fMinZ = UI_GET_Z();
		F32 fMaxZ = UI_GET_Z();
		CBox InnerBox = {box.lx + w * 0.25, box.ly + h * 0.25, box.hx - w * 0.25, box.hy - h * 0.25};
		CBox ClipBox;
		static F32 fTime = 0;
		Color4 Tint = {
			ui_ColorButtonGetColorInt(pPane->pTopLeft),
			ui_ColorButtonGetColorInt(pPane->pTopRight),
			ui_ColorButtonGetColorInt(pPane->pBottomRight),
			ui_ColorButtonGetColorInt(pPane->pBottomLeft)
		};
		fTime += g_ui_State.timestep;
		CBoxFloor(&InnerBox);
		ClipBox = InnerBox;
		ClipBox.lx -= pDef->iClipMarginLeft * scale;
		ClipBox.ly -= pDef->iClipMarginTop * scale;
		ClipBox.hx += pDef->iClipMarginRight * scale;
		ClipBox.hy += pDef->iClipMarginBottom * scale;
		clipperPushRestrict(&ClipBox);
		if (point_cbox_clsn(g_ui_State.mouseX, g_ui_State.mouseY, &InnerBox))
		{
			Color c = ColorLerp(ColorBlue, ColorWhite, fTime - round(fTime));
			Color c2 = ColorLerp(ColorRed, ColorWhite, fTime - round(fTime));
			Color c3 = ColorLerp(ColorGreen, ColorWhite, fTime - round(fTime));
			F32 fScale = ui_SliderGetValue(pPane->pScale);
			CBox Padded = {
				InnerBox.lx + pDef->iPaddingLeft * fScale,
				InnerBox.ly + pDef->iPaddingTop * fScale,
				InnerBox.hx - pDef->iPaddingRight * fScale,
				InnerBox.hy - pDef->iPaddingBottom * fScale,
			};
			clipperPush(NULL);
			ui_TextureAssemblyDraw(pDef, &InnerBox, NULL, fScale, fMinZ, fMaxZ, 255 * ui_SliderGetValue(pPane->pAlpha), &Tint);
			// Don't pad right/bottom as those values are already exclusive for the box.
			gfxDrawBox(InnerBox.lx - 1, InnerBox.ly - 1, InnerBox.hx, InnerBox.hy, UI_INFINITE_Z, c);
			gfxDrawBox(Padded.lx - 1, Padded.ly - 1, Padded.hx, Padded.hy, UI_INFINITE_Z, c2);
			gfxDrawBox(ClipBox.lx - 1, ClipBox.ly - 1, ClipBox.hx, ClipBox.hy, UI_INFINITE_Z, c3);
			clipperPop();
		}
		else
			ui_TextureAssemblyDraw(pDef, &InnerBox, NULL, ui_SliderGetValue(pPane->pScale), fMinZ, fMaxZ, 255 * ui_SliderGetValue(pPane->pAlpha), &Tint);
		clipperPop();
	}
	UI_DRAW_LATE(pPane);
}

static void TextureAssemblyPaneTick(TextureAssemblyPane *pPane, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(pPane);
	ui_SliderTick(pPane->pScale, UI_MY_VALUES);
	ui_SliderTick(pPane->pAlpha, UI_MY_VALUES);
	ui_ButtonTick(UI_BUTTON(pPane->pTopLeft), UI_MY_VALUES);
	ui_ButtonTick(UI_BUTTON(pPane->pTopRight), UI_MY_VALUES);
	ui_ButtonTick(UI_BUTTON(pPane->pBottomRight), UI_MY_VALUES);
	ui_ButtonTick(UI_BUTTON(pPane->pBottomLeft), UI_MY_VALUES);
}

static TextureAssemblyPane *TextureAssemblyPaneCreate(void)
{
	TextureAssemblyPane *pPane = calloc(1, sizeof(TextureAssemblyPane));
	Vec4 v4White = {1, 1, 1, 1};
	ui_WidgetInitialize(UI_WIDGET(pPane), TextureAssemblyPaneTick, TextureAssemblyPaneDraw, TextureAssemblyPaneFree, NULL, NULL);
	pPane->pScale = ui_SliderCreate(0, 0, 1, 0, 4, 1);
	pPane->pAlpha = ui_SliderCreate(0, 20, 1, 0, 1, 1);
	UI_WIDGET(pPane->pScale)->widthUnit = UIUnitPercentage;
	UI_WIDGET(pPane->pAlpha)->widthUnit = UIUnitPercentage;
	ui_WidgetAddChild(UI_WIDGET(pPane), UI_WIDGET(pPane->pScale));
	ui_WidgetAddChild(UI_WIDGET(pPane), UI_WIDGET(pPane->pAlpha));

	pPane->pTopLeft = ui_ColorButtonCreate(0, 0, v4White);
	pPane->pTopRight = ui_ColorButtonCreate(0, 0, v4White);
	pPane->pBottomRight = ui_ColorButtonCreate(0, 0, v4White);
	pPane->pBottomLeft = ui_ColorButtonCreate(0, 0, v4White);
	UI_WIDGET(pPane->pTopLeft)->offsetFrom = UIBottomLeft;
	UI_WIDGET(pPane->pTopRight)->offsetFrom = UIBottomLeft;
	UI_WIDGET(pPane->pBottomRight)->offsetFrom = UIBottomLeft;
	UI_WIDGET(pPane->pBottomLeft)->offsetFrom = UIBottomLeft;
	UI_WIDGET(pPane->pTopLeft)->width = 0.25;
	UI_WIDGET(pPane->pTopRight)->width = 0.25;
	UI_WIDGET(pPane->pTopRight)->xPOffset= 0.25;
	UI_WIDGET(pPane->pBottomRight)->width = 0.25;
	UI_WIDGET(pPane->pBottomRight)->xPOffset= 0.5;
	UI_WIDGET(pPane->pBottomLeft)->width = 0.25;
	UI_WIDGET(pPane->pBottomLeft)->xPOffset= 0.75;
	UI_WIDGET(pPane->pTopLeft)->widthUnit = UIUnitPercentage;
	UI_WIDGET(pPane->pTopRight)->widthUnit = UIUnitPercentage;
	UI_WIDGET(pPane->pBottomRight)->widthUnit = UIUnitPercentage;
	UI_WIDGET(pPane->pBottomLeft)->widthUnit = UIUnitPercentage;
	ui_WidgetAddChild(UI_WIDGET(pPane), UI_WIDGET(pPane->pTopLeft));
	ui_WidgetAddChild(UI_WIDGET(pPane), UI_WIDGET(pPane->pTopRight));
	ui_WidgetAddChild(UI_WIDGET(pPane), UI_WIDGET(pPane->pBottomRight));
	ui_WidgetAddChild(UI_WIDGET(pPane), UI_WIDGET(pPane->pBottomLeft));

	return pPane;
}

static void TextureAssemblyPaneSetRef(UIList *pList, TextureAssemblyPane *pPane)
{
	UITextureAssembly *pDef = ui_ListGetSelectedObject(pList);
	if (pDef)
		SET_HANDLE_FROM_STRING(s_TexAsDict, pDef->pchName, pPane->hDef);
}


static void TextureAssemblyOpenAssembly(UIList *pList, TextureAssemblyPane *pPane)
{
	UITextureAssembly *pDef = ui_ListGetSelectedObject(pList);
	if (pDef)
	{
		char achResolved[CRYPTIC_MAX_PATH];
		fileLocateWrite(pDef->pchFilename, achResolved);
		fileOpenWithEditor(achResolved);
	}
}


// View all currently loaded texture assembly definitions.
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Debug);
void TextureAssemblyBrowser(void)
{
	UIWindow *pWindow = ui_WindowCreate("Texture Assemblies", 0, 0, 500, 400);
	DictionaryEArrayStruct *pTexAsArray = resDictGetEArrayStruct(s_TexAsDict);
	UIList *pList = ui_ListCreate(parse_UITextureAssembly, &pTexAsArray->ppReferents, 20);
	TextureAssemblyPane *pPane = TextureAssemblyPaneCreate();
	ui_ListAppendColumn(pList, ui_ListColumnCreate(UIListPTName, "Name", (intptr_t)"Name", NULL));
	ui_WidgetSetDimensionsEx(UI_WIDGET(pList), 0.33f, 1.f, UIUnitPercentage, UIUnitPercentage);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pPane), 0.67f, 1.f, UIUnitPercentage, UIUnitPercentage);
	UI_WIDGET(pPane)->xPOffset = 0.33f;

	ui_WindowAddChild(pWindow, pList);
	ui_WindowAddChild(pWindow, pPane);
	ui_ListSetSelectedCallback(pList, TextureAssemblyPaneSetRef, pPane);
	ui_ListSetActivatedCallback(pList, TextureAssemblyOpenAssembly, pPane);
	ui_WindowSetCloseCallback(pWindow, ui_WindowFreeOnClose, NULL);
	ui_WindowShow(pWindow);
}

AUTO_FIXUPFUNC;
TextParserResult ui_TextureAssemblyFixup(UITextureAssembly *pTexAs, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
	case FIXUPTYPE_DESTRUCTOR:
		eaDestroy(&pTexAs->eaSortedByDependency);
		eaDestroy(&pTexAs->eaSortedByDrawZ);
	}
	return PARSERESULT_SUCCESS;
}

AUTO_FIXUPFUNC;
TextParserResult ui_TextureInstanceFallbackFixup(UITextureInstanceFallback *pFallback, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
	case FIXUPTYPE_DESTRUCTOR:
		eaDestroy(&pFallback->eaTextures);
	}
	return PARSERESULT_SUCCESS;
}

#include "UITextureAssembly_h_ast.c"
