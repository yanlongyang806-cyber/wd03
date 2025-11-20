#include "earray.h"
#include "UIGenColorChooser.h"
#include "UITextureAssembly.h"
#include "UIGen_h_ast.h"
#include "UICore_h_ast.h"
#include "StringCache.h"
#include "GfxTextures.h"
#include "Clipper.h"
#include "UIGenPrivate.h"
#include "timing.h"
#include "rgb_hsv.h"


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

// RGBA integer packed color
static const char *s_pcColorData;
static int s_iColorDataHandle;

// Float [0, 255] unpacked color
static const char *s_pcColorRed;
static int s_iColorRedHandle;
static const char *s_pcColorGreen;
static int s_iColorGreenHandle;
static const char *s_pcColorBlue;
static int s_iColorBlueHandle;
static const char *s_pcColorAlpha;
static int s_iColorAlphaHandle;

static void ui_GenComputeDrawMatrix(UIGen *pGen, UIGenColorChooser *pColorChooser, UIGenColorChooserState *pState)
{
	devassert(pGen && pColorChooser && pState);

	// Fixed size
	if (pColorChooser->iRows && pColorChooser->iColumns)
	{
		pState->iDrawRows = pColorChooser->iRows;
		pState->iDrawCols = pColorChooser->iColumns;
		return;
	}

	// Compute how many columns there needs to be
	pState->iDrawCols = pColorChooser->iColumns ? pColorChooser->iColumns : pColorChooser->iRows ? pState->iModelSize / pColorChooser->iRows + ((pState->iModelSize % pColorChooser->iRows) ? 1 : 0) : (int)(sqrtf(pState->iModelSize) + 1);

	if (pState->iDrawCols > pColorChooser->iRecentColors && pState->iDrawCols > pState->iModelSize)
	{
		pState->iDrawCols = max(pColorChooser->iRecentColors, pState->iModelSize);

		if (pState->iDrawCols == 0)
		{
			pState->iDrawCols = 1;
		}
	}

	// Determine recent rows
	if (pColorChooser->iRecentColors > 0)
	{
		pState->iRecentRows = eaiSize(&pState->eaiRecentModel) / pState->iDrawCols + ((eaiSize(&pState->eaiRecentModel) % pState->iDrawCols) ? 1 : 0);
	}
	else
	{
		pState->iRecentRows = 0;
	}

	pState->iDrawRows = pColorChooser->iRows ? pColorChooser->iRows : (pState->iRecentRows + pState->iModelSize / pState->iDrawCols + ((pState->iModelSize % pState->iDrawCols) ? 1 : 0));
}

static void ui_GenSelectColor(SA_PARAM_NN_VALID UIGenColorChooserState *pState, int iIndex)
{
	devassert(pState);

	MIN1(iIndex, pState->iModelSize - 1);
	if (iIndex < 0 && eaiSize(&pState->eaiRecentModel) + iIndex >= 0)
	{
		// Change the selected index
		pState->iSelectedIndex = iIndex;
	}
	else if (pState->iSelectedIndex != iIndex && 0 <= iIndex && iIndex < eaiSize(&pState->eaiModel))
	{
		// Change the selected index
		pState->iSelectedIndex = iIndex;
	}
}

int **ui_GenGetColorList(UIGen *pGen)
{
	UIGenColorChooserState *pState = UI_GEN_STATE(pGen, ColorChooser);
	devassert(pState);

	return &pState->eaiModel;
}

int **ui_GenGetRecentColorList(UIGen *pGen)
{
	UIGenColorChooserState *pState = UI_GEN_STATE(pGen, ColorChooser);
	devassert(pState);

	return &pState->eaiRecentModel;
}

void ui_GenAddRecentColor(UIGen *pGen, U32 uiRGBAColor)
{
	UIGenColorChooser *pColorChooser = UI_GEN_RESULT(pGen, ColorChooser);
	UIGenColorChooserState *pState = UI_GEN_STATE(pGen, ColorChooser);
	int iPos;

	devassert(pState);

	for (iPos = eaiSize(&pState->eaiRecentModel) - 1; iPos >= 0; iPos--)
	{
		if (pState->eaiRecentModel[iPos] == uiRGBAColor)
		{
			eaiRemove(&pState->eaiRecentModel, iPos);
			break;
		}
	}

	if (eaiSize(&pState->eaiRecentModel) >= pColorChooser->iRecentColors)
	{
		eaiPop(&pState->eaiRecentModel);
	}

	eaiInsert(&pState->eaiRecentModel, uiRGBAColor, 0);
}

//////////////////////////////////////////////////////////////////////////////

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ColorChooserTest);
void ui_exprColorChooserTest(SA_PARAM_NN_VALID ExprContext *pContext)
{
	UIGen *pGen = exprContextGetUserPtr(pContext, parse_UIGen);
	int **peaiColorList = ui_GenGetColorList(pGen);
	int r, g, b;

	eaiClear(peaiColorList);

	PERFINFO_AUTO_START_FUNC();

	for (b = 0xFF; b >= 0; b -= 0x33)
	{
		for (g = 0xFF; g >= 0; g -= 0x33)
		{
			for (r = 0xFF; r >= 0; r -= 0x33)
			{
				int rgba = (r << 24) | (g << 16) | (b << 8) | 0xFF;
				eaiPush(peaiColorList, rgba);
			}
		}
	}

	PERFINFO_AUTO_STOP();
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ColorChooserAddRecentColorData);
void ui_exprColorChooserAddRecentColorData(SA_PARAM_NN_VALID ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, int iColor)
{
	ui_GenAddRecentColor(pGen, iColor);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ColorChooserAddRecentColor);
void ui_exprColorChooserAddRecentColorRGBA(SA_PARAM_NN_VALID ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, float r, float g, float b, float a)
{
	ui_exprColorChooserAddRecentColorData(pContext, pGen, ((int)r << 24) | ((int)g << 16) | ((int)b << 8) | ((int)a));
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ColorChooserSelect);
void ui_exprColorChooserSelect(SA_PARAM_NN_VALID ExprContext *pContext, int iIndex)
{
	UIGen *pGen = exprContextGetUserPtr(pContext, parse_UIGen);
	UIGenColorChooserState *pState = pGen ? UI_GEN_STATE(pGen, ColorChooser) : NULL;

	if (pState)
	{
		ui_GenSelectColor(pState, iIndex);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ColorChooserSelectColorData);
void ui_exprColorChooserSelectColorData(SA_PARAM_NN_VALID ExprContext *pContext, int iColor)
{
	UIGen *pGen = exprContextGetUserPtr(pContext, parse_UIGen);
	UIGenColorChooserState *pState = pGen ? UI_GEN_STATE(pGen, ColorChooser) : NULL;

	if (pState)
	{
		int i;

		// Search for matching color
		for (i = 0; i < eaiSize(&pState->eaiRecentModel); i++)
		{
			if (pState->eaiRecentModel[i] == iColor)
			{
				ui_GenSelectColor(pState, i - eaiSize(&pState->eaiRecentModel));
				break;
			}
		}
		for (i = 0; i < pState->iModelSize; i++)
		{
			if (pState->eaiModel[i] == iColor)
			{
				ui_GenSelectColor(pState, i);
				break;
			}
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ColorChooserSelectColor);
void ui_exprColorChooserSelectColorRGBA(SA_PARAM_NN_VALID ExprContext *pContext, float r, float g, float b, float a)
{
	ui_exprColorChooserSelectColorData(pContext, ((int)r << 24) | ((int)g << 16) | ((int)b << 8) | ((int)a));
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ColorChooserMoveUp);
void ui_exprColorChooserMoveUp(SA_PARAM_NN_VALID ExprContext *pContext)
{
	UIGen *pGen = exprContextGetUserPtr(pContext, parse_UIGen);
	UIGenColorChooserState *pState = pGen ? UI_GEN_STATE(pGen, ColorChooser) : NULL;

	if (pState)
	{
		ui_GenSelectColor(pState, pState->iSelectedIndex - pState->iDrawCols);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ColorChooserMoveDown);
void ui_exprColorChooserMoveDown(SA_PARAM_NN_VALID ExprContext *pContext)
{
	UIGen *pGen = exprContextGetUserPtr(pContext, parse_UIGen);
	UIGenColorChooserState *pState = pGen ? UI_GEN_STATE(pGen, ColorChooser) : NULL;

	if (pState)
	{
		ui_GenSelectColor(pState, pState->iSelectedIndex + pState->iDrawCols);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ColorChooserMoveLeft);
void ui_exprColorChooserMoveLeft(SA_PARAM_NN_VALID ExprContext *pContext)
{
	UIGen *pGen = exprContextGetUserPtr(pContext, parse_UIGen);
	UIGenColorChooserState *pState = pGen ? UI_GEN_STATE(pGen, ColorChooser) : NULL;

	if (pState)
	{
		ui_GenSelectColor(pState, pState->iSelectedIndex - 1);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ColorChooserMoveRight);
void ui_exprColorChooserMoveRight(SA_PARAM_NN_VALID ExprContext *pContext)
{
	UIGen *pGen = exprContextGetUserPtr(pContext, parse_UIGen);
	UIGenColorChooserState *pState = pGen ? UI_GEN_STATE(pGen, ColorChooser) : NULL;

	if (pState)
	{
		ui_GenSelectColor(pState, pState->iSelectedIndex + 1);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ColorChooserGetSpectrum);
void ui_exprColorChooserGetSpectrum(SA_PARAM_NN_VALID ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, int iLow, int iHigh, int iStep)
{
	int **peaiColorList = ui_GenGetColorList(pGen);
	eaiClearFast(peaiColorList);
	if (iStep > 0)
	{
		Vec3 v3hsv;
		Vec3 v3rgb;
		U32 uiColor;
		int i;
		for (i = iLow; i <= iHigh; i+=iStep)
		{
			v3hsv[0] = i;
			v3hsv[1] = 1.0f;
			v3hsv[2] = 1.0f;
			hsvToRgb(v3hsv, v3rgb);
			uiColor = ((U32)(v3rgb[0]*255) << 24) | ((U32)(v3rgb[1]*255) << 16) | ((U32)(v3rgb[2]*255) << 8) | 255;
			eaiPush(peaiColorList, uiColor);
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ColorChooserGetSelectedColorIndex);
int ui_exprColorChooserGetSelectedColorIndex(SA_PARAM_NN_VALID ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	UIGenColorChooserState *pState = pGen ? UI_GEN_STATE(pGen, ColorChooser) : NULL;
	return SAFE_MEMBER(pState, iSelectedIndex);
}

//////////////////////////////////////////////////////////////////////////////


bool ui_GenValidateColorChooser(UIGen *pGen, UIGenInternal *pInt, const char *pchDescriptor)
{
	//UIGenColorChooser *pBase = (UIGenColorChooser*)pGen->pBase;
	//UIGenColorChooser *pChooser = (UIGenColorChooser*)pInt;
	//UIGenStateDef *pClicked = eaIndexedGetUsingInt(&pGen->eaStates, kUIGenStateMouseClick);
	//UIGenStateDef *pHover = eaIndexedGetUsingInt(&pGen->eaStates, kUIGenStateMouseOver);

	//UI_GEN_WARN_IF(pGen, pChooser->pOnClicked && SAFE_MEMBER(pClicked, pOnEnter),
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

void ui_GenLayoutEarlyColorChooser(UIGen *pGen)
{
	UIGenColorChooser *pColorChooser = UI_GEN_RESULT(pGen, ColorChooser);
	UIGenColorChooserState *pState = UI_GEN_STATE(pGen, ColorChooser);
	MultiVal result;

	PERFINFO_AUTO_START_FUNC();

	if (pColorChooser && pColorChooser->pColorModel)
	{
		ui_GenTimeEvaluate(pGen, pColorChooser->pColorModel, &result, "ColorModel");
		pState->iModelSize = eaiSize(&pState->eaiModel);

		if (pState->iSelectedIndex >= pState->iModelSize)
		{
			pState->iSelectedIndex = 0;
		}
	}

	ui_GenComputeDrawMatrix(pGen, pColorChooser, pState);
	ui_GenBundleTextureUpdate(pGen, &pColorChooser->HoveredOverlayTextureBundle, &pState->HoveredOverlayTextureState);
	ui_GenBundleTextureUpdate(pGen, &pColorChooser->TextureBundle, &pState->TextureState);

	PERFINFO_AUTO_STOP();
}

void ui_GenTickEarlyColorChooser(UIGen *pGen)
{
	UIGenColorChooser *pColorChooser = UI_GEN_RESULT(pGen, ColorChooser);
	UIGenColorChooserState *pState = UI_GEN_STATE(pGen, ColorChooser);
	CBox *pBox = &pGen->ScreenBox;
	bool bClick = ui_GenInState(pGen, kUIGenStateLeftMouseClick) || (ui_GenInState(pGen, kUIGenStateLeftMouseUp) && ui_GenInState(pGen, kUIGenStatePressed));

	PERFINFO_AUTO_START_FUNC();

	// Handle event
	if ((pState->iEventIndex != pState->iSelectedIndex || bClick) && ((eaiSize(&pState->eaiRecentModel) + pState->iSelectedIndex >= 0) || !pState->iRecentRows && 0 <= pState->iSelectedIndex) && pState->iSelectedIndex < pState->iModelSize)
	{
		// Check for clicking
		if (bClick)
		{
			ui_GenRunAction(pGen, pColorChooser->pOnClicked);
		}
		else
		{
			ui_GenRunAction(pGen, pColorChooser->pOnHovered);
		}

		// Don't run the event again
		pState->iEventIndex = pState->iSelectedIndex;
	}

	// Select tile by mouse if it's moved
	if (ui_GenInState(pGen, kUIGenStateMouseOver) && (g_ui_State.mouseX != pState->iMouseX || g_ui_State.mouseY != pState->iMouseY))
	{
		float fMouseX, fMouseY;
		float fX = (pColorChooser->iTileLeftMargin + pColorChooser->iTileWidth + pColorChooser->iTileRightMargin) * pGen->fScale;
		float fY = (pColorChooser->iTileTopMargin + pColorChooser->iTileHeight + pColorChooser->iTileBottomMargin) * pGen->fScale;
		int iCol, iRow, iMouse;

		// Update cached coords so that it doesn't change
		pState->iMouseX = g_ui_State.mouseX;
		pState->iMouseY = g_ui_State.mouseY;

		// Compute tile row/col
		fMouseX = pState->iMouseX - pBox->lx + pColorChooser->iTileLeftMargin;
		fMouseY = pState->iMouseY - pBox->ly + pColorChooser->iTileTopMargin;

		// Is the mouse in the padding
		if (pState->iRecentRows && fMouseY >= fY * pState->iRecentRows && fMouseY < fY * pState->iRecentRows + pColorChooser->iRecentColorsBottomMargin)
		{
			// Ignore it
		}
		else
		{
			// Handle recent row(s)
			if (pState->iRecentRows && fMouseY > fY * pState->iRecentRows)
			{
				// Add correction for the extra spacing
				fMouseY -= pColorChooser->iRecentColorsBottomMargin;
			}

			// Compute hovered row/col
			iRow = (int)(fMouseY / fY);
			iCol = (int)(fMouseX / fX);

			// Compute selected
			if (0 <= iCol && iCol < pState->iDrawCols && 0 <= iRow && iRow < pState->iDrawRows)
			{
				if (iRow < pState->iRecentRows)
				{
					iMouse = -eaiSize(&pState->eaiRecentModel) + (iCol + pState->iDrawCols * iRow);
					if (iMouse < 0 && eaiSize(&pState->eaiRecentModel) + iMouse >= 0)
					{
						ui_GenSelectColor(pState, iMouse);
					}
				}
				else
				{
					iMouse = iCol + pState->iDrawCols * (iRow - pState->iRecentRows);
					if (0 <= iMouse && iMouse < pState->iModelSize)
					{
						ui_GenSelectColor(pState, iMouse);
					}
				}
			}
		}
	}

	PERFINFO_AUTO_STOP();
}

void ui_GenDrawEarlyColorChooser(UIGen *pGen)
{
	UIGenColorChooser *pColorChooser = UI_GEN_RESULT(pGen, ColorChooser);
	UIGenColorChooserState *pState = UI_GEN_STATE(pGen, ColorChooser);
	ExprContext *pContext = pGen ? ui_GenGetContext(pGen) : NULL;
	float fHalfTileWidth = pColorChooser->iTileWidth * pGen->fScale / 2;
	float fHalfTileHeight = pColorChooser->iTileHeight * pGen->fScale / 2;
	float fOverlayWidth = pColorChooser->iHoveredOverlayWidth * pGen->fScale;
	float fOverlayHeight = pColorChooser->iHoveredOverlayHeight * pGen->fScale;
	float fBorderMinZ = UI_GET_Z();
	float fBorderMaxZ = UI_GET_Z();
	UITextureAssembly *pAssembly = ui_GenTextureAssemblyGetAssembly(pGen, pColorChooser->pHoveredAssembly);
	bool bOverlay = false;
	float x, y;
	float dx, dy, x0;
	int i, n, col, row;
	int iSelectedColor;
	CBox SelectedBox;
	CBox SelectedTileBox;
	S32 fMouseX = 0, fMouseY = 0;

	PERFINFO_AUTO_START_FUNC();

	n = pState ? pState->iModelSize : 0;

	if (!fOverlayWidth)
	{
		fOverlayWidth = pColorChooser->iTileWidth * pGen->fScale;
	}
	if (!fOverlayHeight)
	{
		fOverlayHeight = pColorChooser->iTileHeight * pGen->fScale;
	}

	x0 = x = pGen->ScreenBox.lx + (pColorChooser->iTileLeftMargin + pColorChooser->iTileWidth / 2) * pGen->fScale;
	y = pGen->ScreenBox.ly + (pColorChooser->iTileTopMargin + pColorChooser->iTileHeight / 2) * pGen->fScale;
	dx = (pColorChooser->iTileLeftMargin + pColorChooser->iTileWidth + pColorChooser->iTileRightMargin) * pGen->fScale;
	dy = (pColorChooser->iTileTopMargin + pColorChooser->iTileHeight + pColorChooser->iTileBottomMargin) * pGen->fScale;
	row = col = 0;

	if (!eaiSize(&pState->eaiRecentModel) && pState->iRecentRows)
	{
		y += dy + pColorChooser->iRecentColorsBottomMargin;
	}

	for (i = pState->iRecentRows ? -eaiSize(&pState->eaiRecentModel) : 0; i < n && row < pState->iDrawRows; i++)
	{
		CBox TileBox = { x - fHalfTileWidth, y - fHalfTileHeight, x + fHalfTileWidth, y + fHalfTileHeight };
		int iColor = i < 0 ? (U32) pState->eaiRecentModel[eaiSize(&pState->eaiRecentModel) + i] : pState->eaiModel[i];

		// Draw the overlay
		if (i == pState->iSelectedIndex)
		{
			BuildCBox(&SelectedTileBox, x - fOverlayWidth / 2, y - fOverlayHeight / 2, fOverlayWidth, fOverlayHeight);

			if (!pColorChooser->bDrawHoveredAssemblyAbove)
			{
				if (pAssembly)
				{
					CBox TexAsBox = {
						TileBox.lx - ui_TextureAssemblyLeftSize(pAssembly),
						TileBox.ly - ui_TextureAssemblyTopSize(pAssembly),
						TileBox.hx + ui_TextureAssemblyRightSize(pAssembly),
						TileBox.hy + ui_TextureAssemblyBottomSize(pAssembly)
					};

					ui_TextureAssemblyDraw(pAssembly, &TexAsBox, &TileBox, pGen->fScale, fBorderMinZ, fBorderMaxZ, pGen->chAlpha, &pColorChooser->pHoveredAssembly->Colors);
				}

				// Draw the tile
				pColorChooser->TextureBundle.uiTopLeftColor = iColor;
				pColorChooser->TextureBundle.uiTopRightColor = iColor;
				pColorChooser->TextureBundle.uiBottomLeftColor = iColor;
				pColorChooser->TextureBundle.uiBottomRightColor = iColor;
				ui_GenBundleTextureDraw(pGen, pGen->pResult, &pColorChooser->TextureBundle, &TileBox, 0, 0, false, false, &pState->TextureState, NULL); // This probably could be optimized, but it's still faster than the original Gen, so it'll do for now.
			}
			else
			{
				SelectedBox = TileBox;
				iSelectedColor = iColor;
			}

			// Draw the overlay
			bOverlay = true;

			// Set the overlay color
			if (pColorChooser->bHoveredOverlayAutoColor)
			{
				pColorChooser->HoveredOverlayTextureBundle.uiTopLeftColor = iColor;
				pColorChooser->HoveredOverlayTextureBundle.uiTopRightColor = iColor;
				pColorChooser->HoveredOverlayTextureBundle.uiBottomLeftColor = iColor;
				pColorChooser->HoveredOverlayTextureBundle.uiBottomRightColor = iColor;
			}
		}
		else if (CBoxIntersects(&TileBox, &pGen->ScreenBox))
		{
			// Draw the tile
			pColorChooser->TextureBundle.uiTopLeftColor = iColor;
			pColorChooser->TextureBundle.uiTopRightColor = iColor;
			pColorChooser->TextureBundle.uiBottomLeftColor = iColor;
			pColorChooser->TextureBundle.uiBottomRightColor = iColor;
			ui_GenBundleTextureDraw(pGen, pGen->pResult, &pColorChooser->TextureBundle, &TileBox, 0, 0, false, false, &pState->TextureState, NULL); // This probably could be optimized, but it's still faster than the original Gen, so it'll do for now.
		}

		// Update the position
		if (++col == pState->iDrawCols || i == -1)
		{
			col = 0;
			x = x0;
			y += dy;
			y += (i == -1 ? pColorChooser->iRecentColorsBottomMargin : 0);
		}
		else
		{
			x += dx;
		}
	}

	// Draw the selected tile
	if (bOverlay)
	{
		if (pColorChooser->bDrawHoveredAssemblyAbove)
		{
			if (pAssembly)
			{
				CBox TexAsBox = {
					SelectedBox.lx - ui_TextureAssemblyLeftSize(pAssembly),
					SelectedBox.ly - ui_TextureAssemblyTopSize(pAssembly),
					SelectedBox.hx + ui_TextureAssemblyRightSize(pAssembly),
					SelectedBox.hy + ui_TextureAssemblyBottomSize(pAssembly)
				};

				// Reset the Z
				fBorderMinZ = UI_GET_Z();
				fBorderMaxZ = UI_GET_Z();

				ui_TextureAssemblyDraw(pAssembly, &TexAsBox, &SelectedBox, pGen->fScale, fBorderMinZ, fBorderMaxZ, pGen->chAlpha, &pColorChooser->pHoveredAssembly->Colors);
			}

			// Draw the tile
			pColorChooser->TextureBundle.uiTopLeftColor = iSelectedColor;
			pColorChooser->TextureBundle.uiTopRightColor = iSelectedColor;
			pColorChooser->TextureBundle.uiBottomLeftColor = iSelectedColor;
			pColorChooser->TextureBundle.uiBottomRightColor = iSelectedColor;
			ui_GenBundleTextureDraw(pGen, pGen->pResult, &pColorChooser->TextureBundle, &SelectedBox, 0, 0, false, false, &pState->TextureState, NULL); // This probably could be optimized, but it's still faster than the original Gen, so it'll do for now.
		}

		// Draw the overlay texture
		ui_GenBundleTextureDraw(pGen, pGen->pResult, &pColorChooser->HoveredOverlayTextureBundle, &SelectedTileBox, 0, 0, false, false, &pState->HoveredOverlayTextureState, NULL);
	}

	PERFINFO_AUTO_STOP();
}

void ui_GenFitContentsSizeColorChooser(UIGen *pGen, UIGenColorChooser *pColorChooser, CBox *pBox)
{
	UIGenColorChooserState *pState = UI_GEN_STATE(pGen, ColorChooser);

	ui_GenComputeDrawMatrix(pGen, pColorChooser, pState);

	CBoxSetHeight(pBox, pState->iDrawRows * pColorChooser->iTileHeight + pState->iDrawRows * (pColorChooser->iTileTopMargin + pColorChooser->iTileBottomMargin) + (pState->iRecentRows ? pColorChooser->iRecentColorsBottomMargin : 0));
	CBoxSetWidth(pBox, pState->iDrawCols * pColorChooser->iTileWidth + pState->iDrawCols * (pColorChooser->iTileLeftMargin + pColorChooser->iTileRightMargin));
}

void ui_GenUpdateContextColorChooser(UIGen *pGen, ExprContext *pContext, UIGen *pFor)
{
	UIGenColorChooserState *pState = UI_GEN_STATE(pGen, ColorChooser);
	U32 iColor = 0xFF;

	if (pState && (pState->iRecentRows && (eaiSize(&pState->eaiRecentModel) + pState->iSelectedIndex >= 0) || !pState->iRecentRows && 0 <= pState->iSelectedIndex) && pState->iSelectedIndex < pState->iModelSize)
	{
		iColor = pState->iSelectedIndex < 0 ? (U32) pState->eaiRecentModel[eaiSize(&pState->eaiRecentModel) + pState->iSelectedIndex] : (U32) pState->eaiModel[pState->iSelectedIndex];
	}

	exprContextSetIntVarPooledCached(pContext, s_pcColorData, iColor, &s_iColorDataHandle);
	exprContextSetFloatVarPooledCached(pContext, s_pcColorRed, (float)((iColor >> 24) & 0xFF), &s_iColorRedHandle);
	exprContextSetFloatVarPooledCached(pContext, s_pcColorGreen, (float)((iColor >> 16) & 0xFF), &s_iColorGreenHandle);
	exprContextSetFloatVarPooledCached(pContext, s_pcColorBlue, (float)((iColor >> 8) & 0xFF), &s_iColorBlueHandle);
	exprContextSetFloatVarPooledCached(pContext, s_pcColorAlpha, (float)((iColor >> 0) & 0xFF), &s_iColorAlphaHandle);
}

AUTO_RUN;
void ui_GenRegisterColorChooser(void)
{
	s_pcColorData = allocAddStaticString("ColorData");
	s_pcColorRed = allocAddStaticString("ColorRed");
	s_pcColorGreen = allocAddStaticString("ColorGreen");
	s_pcColorBlue = allocAddStaticString("ColorBlue");
	s_pcColorAlpha = allocAddStaticString("ColorAlpha");

	ui_GenInitIntVar(s_pcColorData, 0);
	ui_GenInitFloatVar(s_pcColorRed);
	ui_GenInitFloatVar(s_pcColorGreen);
	ui_GenInitFloatVar(s_pcColorBlue);
	ui_GenInitFloatVar(s_pcColorAlpha);

	ui_GenRegisterType(kUIGenTypeColorChooser, 
		ui_GenValidateColorChooser,
		UI_GEN_NO_POINTERUPDATE,
		UI_GEN_NO_UPDATE,
		ui_GenLayoutEarlyColorChooser,
		UI_GEN_NO_LAYOUTLATE,
		ui_GenTickEarlyColorChooser,
		UI_GEN_NO_TICKLATE,
		ui_GenDrawEarlyColorChooser,
		ui_GenFitContentsSizeColorChooser,
		UI_GEN_NO_FITPARENTSIZE,
		UI_GEN_NO_HIDE,
		UI_GEN_NO_INPUT,
		ui_GenUpdateContextColorChooser,
		UI_GEN_NO_QUEUERESET);
}

#include "UIGenColorChooser_h_ast.c"
