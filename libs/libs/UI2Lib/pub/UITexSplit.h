#pragma once
GCC_SYSTEM
#ifndef UI_TEX_SPLIT_H
#define UI_TEX_SPLIT_H

//////////////////////////////////////////////////////////////////////////
// Handle texture splitting.

// FIXME: Right now the UI is the only system using this feature so all the
// data structures are maintained here, but there's nothing UI2Lib-specific
// about the texture splitting system. If someone else needs to use it and
// doesn't want to use the UI for some reason, this can all be removed
// and renamed.

typedef struct AtlasTex AtlasTex;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK(End);
typedef struct TexSplitInfo
{
	const char *pchName; AST(KEY)
	IVec2 iv2OrigSize; AST(NAME(OrigSize))
	IVec2 iv2TileSize; AST(NAME(TileSize)) 

	// Calculated during postprocess.
	S32 iWidth; NO_AST
	S32 iHeight; NO_AST
} TexSplitInfo;

AUTO_STRUCT;
typedef struct TexSplitInfos
{
	TexSplitInfo **eaInfos; AST(NAME("TexSplitInfo"))
} TexSplitInfos;

void ui_TexSplitLoad(void);
SA_RET_OP_VALID TexSplitInfo *ui_TexSplitGet(const char *pchName);

// Return the tile corresponding to this X, Y of the original image.
SA_RET_NN_VALID AtlasTex *ui_TexSplitGetTile(SA_PARAM_NN_VALID TexSplitInfo *pInfo, S32 iX, S32 iY);
SA_RET_NN_VALID AtlasTex *ui_TexSplitGetTileAtPos(SA_PARAM_NN_VALID TexSplitInfo *pInfo, S32 iX, S32 iY);

#endif
