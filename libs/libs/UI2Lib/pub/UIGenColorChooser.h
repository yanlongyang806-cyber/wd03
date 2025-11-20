#pragma once
GCC_SYSTEM
#ifndef UI_GEN_COLORCHOOSER_H
#define UI_GEN_COLORCHOOSER_H

#include "UIGen.h"
#include "Expression.h"

// A ColorChooser gen accepts an EArray of colors. It doesn't use the standard
// list, instead it uses a custom list where each index is an RGBA integer.
// 
// The hovered tile supports a single overlay texture with all the standard texture
// options by using "HoveredOverlay*" parameters. The overlay is drawn centered
// over the color tile and will be properly drawn if bigger than the color tile.
// 
// It supports multiple borders around the hovered tile through using "HoveredBorder".
// 
// The texture is a standard UIGen texture as one might expect to use in a UIGenSprite.
AUTO_STRUCT;
typedef struct UIGenColorChooser
{
	UIGenInternal polyp; AST(POLYCHILDTYPE(kUIGenTypeColorChooser))

	// The per tile texture
	UIGenBundleTexture TextureBundle; AST(EMBEDDED_FLAT(Tile))

	// The selected overlay textures
	UIGenBundleTexture HoveredOverlayTextureBundle; AST(EMBEDDED_FLAT(HoveredOverlay))

	// The width of the overlay, it defaults to the color tile width
	S16 iHoveredOverlayWidth; AST(SUBTABLE(UISizeEnum))

	// The height of the overlay, it defaults to the color tile height
	S16 iHoveredOverlayHeight; AST(SUBTABLE(UISizeEnum))

	// The number of recent colors
	S16 iRecentColors;

	// The extra spacing between recent colors and the real colors
	S16 iRecentColorsBottomMargin; AST(SUBTABLE(UISizeEnum))

	// The number of columns it should limit to (zero means no limit)
	S16 iColumns;

	// The number of rows it should limit to (zero means no limit)
	S16 iRows;

	// The width of a color tile
	S16 iTileWidth; AST(SUBTABLE(UISizeEnum) DEFAULT(16))

	// The height of a color tile
	S16 iTileHeight; AST(SUBTABLE(UISizeEnum) DEFAULT(16))

	// The margin of each borders around a tile
	S16 iTileLeftMargin; AST(SUBTABLE(UISizeEnum))
	S16 iTileRightMargin; AST(SUBTABLE(UISizeEnum))
	S16 iTileTopMargin; AST(SUBTABLE(UISizeEnum))
	S16 iTileBottomMargin; AST(SUBTABLE(UISizeEnum))

	// The texture assembly that's used to indicate the selected tile
	UIGenTextureAssembly *pHoveredAssembly; AST(NAME(HoveredAssembly))

	// If true it will set the color of the overlay to the same color as selected
	bool bHoveredOverlayAutoColor : 1;

	// Draw the hovered texture assembly above the other color tiles
	bool bDrawHoveredAssemblyAbove : 1;

	// The color list model
	Expression *pColorModel; AST(NAME(ColorModelBlock) REDUNDANT_STRUCT(ColorModel, parse_Expression_StructParam) LATEBIND)

	// The active color changed
	UIGenAction *pOnHovered;

	// The active color clicked
	UIGenAction *pOnClicked;

} UIGenColorChooser;

AUTO_STRUCT;
typedef struct UIGenColorChooserState
{
	UIGenPerTypeState polyp; AST(POLYCHILDTYPE(kUIGenTypeColorChooser))

	// The currently selected color
	S16 iSelectedIndex;
	S16 iEventIndex; AST(DEFAULT(-1))

	// The stored mouse position
	S16 iMouseX;
	S16 iMouseY;

	// The color list model
	int *eaiModel;

	// The recent color list
	int *eaiRecentModel;

	S16 iModelSize;	

	// The current dimensions
	S16 iDrawRows;
	S16 iRecentRows;
	S16 iDrawCols;

	// The state of the texture
	UIGenBundleTextureState TextureState; 

	// The state of the texture
	UIGenBundleTextureState HoveredOverlayTextureState; 

} UIGenColorChooserState;

int **ui_GenGetColorList(UIGen *pGen);
int **ui_GenGetRecentColorList(UIGen *pGen);
void ui_GenAddRecentColor(UIGen *pGen, U32 uiRGBAColor);

#endif
