#include "error.h"

#include "GfxTexAtlas.h"

#include "UITexSplit.h"
#include "UITexSplit_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

static TexSplitInfos s_SplitInfos;

void ui_TexSplitLoad(void)
{
	S32 i;
	loadstart_printf("Loading split texture info... ");
	ParserLoadFiles("texture_library", ".TexSplitInfo", "TexSplitInfo.bin", PARSER_OPTIONALFLAG, parse_TexSplitInfos, &s_SplitInfos);
	for (i = 0; i < eaSize(&s_SplitInfos.eaInfos); i++)
	{
		TexSplitInfo *pInfo = s_SplitInfos.eaInfos[i];
		pInfo->iWidth = ((pInfo->iv2OrigSize[0] - 1) / pInfo->iv2TileSize[0]) + 1;
		pInfo->iHeight = ((pInfo->iv2OrigSize[1] - 1) / pInfo->iv2TileSize[1]) + 1;
	}
	loadend_printf("Done. (%d split textures)", i);
}

static int CompareTexSplitInfos(const TexSplitInfo **pInfo1, const TexSplitInfo **pInfo2)
{
	return stricmp((*pInfo1)->pchName, (*pInfo2)->pchName);
}

TexSplitInfo *ui_TexSplitGet(const char *pchName)
{
	TexSplitInfo dummy = {pchName};
	TexSplitInfo *pDummy = &dummy;
	TexSplitInfo **ppResult = eaBSearch(s_SplitInfos.eaInfos, CompareTexSplitInfos, pDummy);
	return ppResult ? *ppResult : NULL;
}

// Return the tile corresponding to this X, Y of the original image.
AtlasTex *ui_TexSplitGetTileAtPos(TexSplitInfo *pInfo, S32 iX, S32 iY)
{
	S32 iTileX = iX / pInfo->iv2TileSize[0];
	S32 iTileY = iY / pInfo->iv2TileSize[1];
	return ui_TexSplitGetTile(pInfo, iTileX, iTileY);
}

AtlasTex *ui_TexSplitGetTile(TexSplitInfo *pInfo, S32 iX, S32 iY)
{
	char achBuffer[1000];
	devassertmsg(iX < pInfo->iWidth && iY < pInfo->iHeight, "Requesting an out-of-bounds map tile.");
	sprintf(achBuffer, "%s@%d_%d", pInfo->pchName, iX, iY);
	return atlasLoadTexture(achBuffer);
}

#include "UITexSplit_h_ast.c"
