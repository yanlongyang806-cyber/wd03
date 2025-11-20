#ifndef UI_STYLE_H
#define UI_STYLE_H
GCC_SYSTEM

#include "ReferenceSystem.h"
#include "UICore.h"

typedef struct UIStyleFont UIStyleFont;
typedef struct AtlasTex AtlasTex;
typedef struct GfxFont GfxFont;
typedef struct CBox CBox;
typedef struct Message Message;
typedef struct DefineContext DefineContext;
typedef struct SpriteProperties SpriteProperties;

extern DictionaryHandle g_ui_FontDict;
extern DictionaryHandle g_ui_BorderDict;
extern DictionaryHandle g_ui_BarDict;

#define UI_SET_STYLE_BORDER_NAME(hHandle, pchName) SET_HANDLE_FROM_STRING(g_ui_BorderDict, pchName, hHandle)
#define UI_SET_STYLE_BAR_NAME(hHandle, pchName) SET_HANDLE_FROM_STRING(g_ui_BarDict, pchName, hHandle)
#define UI_SET_STYLE_FONT_NAME(hHandle, pchName) SET_HANDLE_FROM_STRING(g_ui_FontDict, pchName, hHandle)

#define UI_SET_STYLE_BORDER(hHandle, pData) SET_HANDLE_FROM_REFERENT(g_ui_BorderDict, pData, hHandle)
#define UI_SET_STYLE_BAR(hHandle, pData) SET_HANDLE_FROM_REFERENT(g_ui_BarDict, pData, hHandle)
#define UI_SET_STYLE_FONT(hHandle, pData) SET_HANDLE_FROM_REFERENT(g_ui_FontDict, pData, hHandle)

//////////////////////////////////////////////////////////////////////////
// UI Styles
//
// The UI style system is intended to let programmers, designers, and
// artists define the look of standard UI widgets (buttons, lists, and so
// on) with minimal difficulty while still remaining very customizable.
// It lets you mix and match fonts, border "kits", and common UI textures
// such as arrows or check boxes to make a "style", which is then applied
// to an individual widget, or an entire group of them.

//////////////////////////////////////////////////////////////////////////
// A StyleFont defines the look of text. It specifies the color, face,
// and formatting. It does not necessarily decide the size, but the
// choice of face and formatting does affect the default size.
//
// In general the number of font styles should be kept as small as possible
// since each one can result in large glyph memory overhead. However, if two
// text styles differ *only* in color, then they can share glyph memory.
AUTO_STRUCT WIKI("Style Font Definitions") AST_FOR_ALL(WIKI(AUTO));
typedef struct UIStyleFont
{
	// The name of this text style. It must be unique.
	const char *pchName; AST(STRUCTPARAM, KEY)
	U32 bf[1]; AST(USEDFIELD)

	REF_TO(UIStyleFont) hBorrowFrom; AST(NAME(BorrowFrom) REFDICT(UIStyleFont))

	// eFace is post-processed into a pointer to a global TTDrawContext.
	REF_TO(GfxFont) hFace; AST(NAME(Face) REFDICT(GfxFont) DEFAULT("Default"))

	// Text can be displayed as a gradient from each corner.
	U32 uiTopLeftColor; AST(FORMAT_COLOR SUBTABLE(ColorEnum) NAME(TopLeftColor))
	U32 uiTopRightColor; AST(FORMAT_COLOR SUBTABLE(ColorEnum) NAME(TopRightColor))
	U32 uiBottomLeftColor; AST(FORMAT_COLOR SUBTABLE(ColorEnum) NAME(BottomLeftColor))
	U32 uiBottomRightColor; AST(FORMAT_COLOR SUBTABLE(ColorEnum) NAME(BottomRightColor))

	// Gradient colors for selected text.
	U32 uiTopLeftSelectedColor; AST(FORMAT_COLOR SUBTABLE(ColorEnum) NAME(TopLeftSelectedColor))
	U32 uiTopRightSelectedColor; AST(FORMAT_COLOR SUBTABLE(ColorEnum) NAME(TopRightSelectedColor))
	U32 uiBottomLeftSelectedColor; AST(FORMAT_COLOR SUBTABLE(ColorEnum) NAME(BottomLeftSelectedColor))
	U32 uiBottomRightSelectedColor; AST(FORMAT_COLOR SUBTABLE(ColorEnum) NAME(BottomRightSelectedColor))

	// Gradient colors for selected text.
	U32 uiTopLeftInactiveColor; AST(FORMAT_COLOR SUBTABLE(ColorEnum) NAME(TopLeftInactiveColor))
	U32 uiTopRightInactiveColor; AST(FORMAT_COLOR SUBTABLE(ColorEnum) NAME(TopRightInactiveColor))
	U32 uiBottomLeftInactiveColor; AST(FORMAT_COLOR SUBTABLE(ColorEnum) NAME(BottomLeftInactiveColor))
	U32 uiBottomRightInactiveColor; AST(FORMAT_COLOR SUBTABLE(ColorEnum) NAME(BottomRightInactiveColor))

	// If set, the text is displayed entirely in this color, overriding any
	// gradient colors present.
	U32 uiColor; AST(FORMAT_COLOR SUBTABLE(ColorEnum) NAME(Color))

	// If set, the text is displayed entirely in this color when selected.
	U32 uiSelectedColor; AST(FORMAT_COLOR SUBTABLE(ColorEnum) NAME(SelectedColor))

	// The color of the outline around the glyphs.
	U32 uiOutlineColor; AST(FORMAT_COLOR SUBTABLE(ColorEnum) DEFAULT(0x000000FF) NAME(OutlineColor))

	// The color of the drop shadow
	U32 uiDropShadowColor; AST(FORMAT_COLOR SUBTABLE(ColorEnum) DEFAULT(0x000000FF) NAME(DropShadowColor))

	// The width of any black outline around the text.
	U8 iOutlineWidth;

	// Shadow offset. If positive, the shadow is to the bottom right
	// of the text; if negative, to the bottom left.
	S8 iShadowOffset;

	// Whether or not the font is italicized.
	bool bItalic;

	// Whether or not the font is bold.
	bool bBold;

	// Whether the x advance snaps to the nearest pixel. You normally want this unless the font needs to be scaled smoothly in an animation
	bool bDontSnapToPixels;

	// If true, don't use automatic colors anywhere
	bool bNoAutomaticColors;

	char *pchFilename; AST(CURRENTFILE)
} UIStyleFont;

// Create a new UIStyleFont struct, with some default values.
SA_RET_NN_VALID UIStyleFont *ui_StyleFontCreate(SA_PARAM_NN_STR const char *pchName, GfxFont *pFont, Color color, bool bBold, bool bItalic, S32 iOutlineWidth);

void ui_StyleFontRegister(UIStyleFont* pFont);

SA_RET_OP_VALID UIStyleFont *ui_StyleFontGet(SA_PARAM_NN_STR const char *pchName);

// Return the TTDrawContext appropriate for the font.
SA_RET_NN_VALID GfxFont *ui_StyleFontGetContext(SA_PARAM_NN_VALID UIStyleFont *pFont);

void ui_StyleFontUse(SA_PARAM_OP_VALID UIStyleFont *pFont, bool bSelected, UIWidgetModifier eMods);

// Return the size of the given string, if rendered in this font.
F32 ui_StyleFontWidth(SA_PARAM_OP_VALID UIStyleFont *pFont, F32 fScale, SA_PARAM_OP_STR const char *pchText);
F32 ui_StyleFontWidthNoCache(SA_PARAM_OP_VALID UIStyleFont *pFont, F32 fScale, SA_PARAM_NN_STR const char *pchText);
void ui_StyleFontDimensions(SA_PARAM_OP_VALID UIStyleFont *pFont, F32 fScale, const char *pchText, F32 *pfWidth, F32 *pfHeight, bool cacheable);

// Return the height of this font, if rendered at this Y scale.
F32 ui_StyleFontLineHeight(SA_PARAM_OP_VALID UIStyleFont *pFont, F32 fScale);

// Return the height of this block of text, if word-wrapped to fit within this
// width and at this X/Y scale.
F32 ui_StyleFontHeightWrapped(UIStyleFont *pFont, F32 fWidth, F32 fScale, const char *pchText);

// Return the "base color" of the font, i.e. either its normal color, or
// the color of its top-left corner.
Color ui_StyleFontGetColor(SA_PARAM_OP_VALID UIStyleFont *pFont);
U32 ui_StyleFontGetColorValue(SA_PARAM_OP_VALID UIStyleFont *pFont);

unsigned int ui_StyleFontCountGlyphsInArea(UIStyleFont *pFont, F32 fScale, const char *pchText, Vec2 v2AllowedArea);

GfxFont *ui_StyleFontGetProjectContext(UIStyleFont *pFont);

//////////////////////////////////////////////////////////////////////////
// A StyleBorder is a set of border graphics and a background (which may be
// stretched or repeated). Any graphic may be not present, in which case
// nothing is drawn for that corner or edge.
AUTO_STRUCT WIKI("Style Border Kits") AST_FOR_ALL(WIKI(AUTO));
typedef struct UIStyleBorder
{
	// The name of this border. It must be unique.
	const char *pchName; AST(STRUCTPARAM, KEY)

	const char *pchTop; AST(RESOURCEDICT(Texture) NAME(Top) NAME(hTop) POOL_STRING)
	const char *pchBottom; AST(RESOURCEDICT(Texture) NAME(Bottom) NAME(hBottom) POOL_STRING)
	const char *pchLeft; AST(RESOURCEDICT(Texture) NAME(Left) NAME(hLeft) POOL_STRING)
	const char *pchRight; AST(RESOURCEDICT(Texture) NAME(Right) NAME(hRight) POOL_STRING)
	const char *pchTopLeft; AST(RESOURCEDICT(Texture) NAME(TopLeft) NAME(hTopLeft) POOL_STRING)
	const char *pchTopRight; AST(RESOURCEDICT(Texture) NAME(TopRight) NAME(hTopRight) POOL_STRING)
	const char *pchBottomLeft; AST(RESOURCEDICT(Texture) NAME(BottomLeft) NAME(hBottomLeft) POOL_STRING)
	const char *pchBottomRight; AST(RESOURCEDICT(Texture) NAME(BottomRight) NAME(hBottomRight) POOL_STRING)

	const char *pchBackground; AST(RESOURCEDICT(Texture) NAME(Background) NAME(hBackground) POOL_STRING)
	const char *pchPattern; AST(RESOURCEDICT(Texture) NAME(Pattern) POOL_STRING)

	AtlasTex *pTop; NO_AST
	AtlasTex *pBottom; NO_AST
	AtlasTex *pLeft; NO_AST
	AtlasTex *pRight; NO_AST
	AtlasTex *pTopLeft; NO_AST
	AtlasTex *pTopRight; NO_AST
	AtlasTex *pBottomLeft; NO_AST
	AtlasTex *pBottomRight; NO_AST
	AtlasTex *pBackground; NO_AST
	AtlasTex *pPattern; NO_AST

	UITextureMode eBackgroundType;

	// Borders can also be tiled instead of stretched.
	UITextureMode eBorderType;

	// Either the border has two colors, one for the background and one for the
	// outline, or it has four colors; the corners get that color solidly, and
	// the borders and middle get a gradient.
	U32 uiOuterColor; AST(FORMAT_COLOR SUBTABLE(ColorEnum) NAME(OuterColor))
	U32 uiInnerColor; AST(FORMAT_COLOR SUBTABLE(ColorEnum) NAME(InnerColor))

	U32 uiTopLeftColor; AST(FORMAT_COLOR SUBTABLE(ColorEnum) NAME(TopLeftColor))
	U32 uiTopRightColor; AST(FORMAT_COLOR SUBTABLE(ColorEnum) NAME(TopRightColor))
	U32 uiBottomLeftColor; AST(FORMAT_COLOR SUBTABLE(ColorEnum) NAME(BottomLeftColor))
	U32 uiBottomRightColor; AST(FORMAT_COLOR SUBTABLE(ColorEnum) NAME(BottomRightColor))

	// Padding specifies how much space to leave from the edge of the border
	// when placing children.

#define BORDER_PADDING_NOT_SET -99999
	// AST DEFAULT requires the macro to be available at too large a scope, so I hard code it here
	F32 fPaddingTop; AST(DEFAULT(-99999))
	F32 fPaddingBottom; AST(DEFAULT(-99999))
	F32 fPaddingLeft; AST(DEFAULT(-99999))
	F32 fPaddingRight; AST(DEFAULT(-99999))

	// Whether or not the background should be drawn under the frame. This is
	// desired when the frame should be alpha-blended with the background, but
	// not desired when the frame has rounded or textured edges.
	bool bDrawUnder : 1;

	U32 bfUsedBits[2]; AST(USEDFIELD)

	const char *pchFilename; AST(CURRENTFILE)
} UIStyleBorder;

SA_RET_NN_VALID UIStyleBorder *ui_StyleBorderCreate(
	SA_PARAM_NN_STR const char *pchName,
	SA_PARAM_OP_STR const char *pchTopLeft, SA_PARAM_OP_STR const char *pchTop, SA_PARAM_OP_STR const char *pchTopRight,
	SA_PARAM_OP_STR const char *pchLeft, SA_PARAM_OP_STR const char *pchRight,
	SA_PARAM_OP_STR const char *pchBottomLeft, SA_PARAM_OP_STR const char *pchBottom, SA_PARAM_OP_STR const char *pchBottomRight,
	SA_PARAM_OP_STR const char *pchBackground, UITextureMode eType);

SA_RET_OP_VALID UIStyleBorder *ui_StyleBorderGet(SA_PARAM_NN_STR const char *pchName);
void ui_StyleBorderDrawEx(UIStyleBorder *pBorder, const CBox *pBox, SA_PRE_NN_ELEMS(4) SA_POST_OP_VALID U32 aiOuterColors[4], SA_PRE_OP_ELEMS(4) SA_POST_OP_VALID U32 aiInnerColors[4], F32 centerX, F32 centerY, F32 rot, F32 fZ, F32 fScale, U8 chAlpha);
void ui_StyleBorderDraw(UIStyleBorder *pBorder, const CBox *pBox, U32 outerColor, U32 innerColor, F32 fZ, F32 fScale, U8 chAlpha);
void ui_StyleBorderDrawOutside(SA_PARAM_OP_VALID UIStyleBorder *pBorder, SA_PARAM_NN_VALID const CBox *pBox, U32 outerColor, U32 innerColor, F32 fZ, F32 fScale, U8 chAlpha);

// Use the colors from the border definition itself.
void ui_StyleBorderDrawMagic(SA_PARAM_OP_VALID UIStyleBorder *pBorder, SA_PARAM_NN_VALID const CBox *pBox, F32 fZ, F32 fScale, U8 chAlpha);
void ui_StyleBorderDrawMagicRot(SA_PARAM_OP_VALID UIStyleBorder *pBorder, SA_PARAM_NN_VALID const CBox *pBox, F32 centerX, F32 centerY, F32 rot, F32 fZ, F32 fScale, U8 chAlpha);
void ui_StyleBorderDrawMagicOutside(SA_PARAM_OP_VALID UIStyleBorder *pBorder, SA_PARAM_NN_VALID const CBox *pBox, F32 fZ, F32 fScale, U8 chAlpha);

// Figure out what area of the CBox would be inside this border, and adjust it.
void ui_StyleBorderInnerCBoxEx(SA_PARAM_OP_VALID UIStyleBorder *pBorder, SA_PARAM_NN_VALID CBox *pBox, F32 fScale, bool bUsePadding);
#define ui_StyleBorderInnerCBox(pBorder, pBox, fScale) ui_StyleBorderInnerCBoxEx(pBorder, pBox, fScale, true)

// Figure out what area of the CBox would be if we put a border around it.
void ui_StyleBorderOuterCBox(SA_PARAM_OP_VALID UIStyleBorder *pBorder, SA_PARAM_NN_VALID CBox *pBox, F32 fScale);

S32 ui_StyleBorderTopSizeEx(SA_PARAM_OP_VALID UIStyleBorder *pBorder, bool bUsePadding);
S32 ui_StyleBorderBottomSizeEx(SA_PARAM_OP_VALID UIStyleBorder *pBorder, bool bUsePadding);
S32 ui_StyleBorderLeftSizeEx(SA_PARAM_OP_VALID UIStyleBorder *pBorder, bool bUsePadding);
S32 ui_StyleBorderRightSizeEx(SA_PARAM_OP_VALID UIStyleBorder *pBorder, bool bUsePadding);

#define ui_StyleBorderTopSize(pBorder) ui_StyleBorderTopSizeEx(pBorder, true)
#define ui_StyleBorderBottomSize(pBorder) ui_StyleBorderBottomSizeEx(pBorder, true)
#define ui_StyleBorderLeftSize(pBorder) ui_StyleBorderLeftSizeEx(pBorder, true)
#define ui_StyleBorderRightSize(pBorder) ui_StyleBorderRightSizeEx(pBorder, true)

S32 ui_StyleBorderWidth(SA_PARAM_OP_VALID UIStyleBorder *pBorder);
S32 ui_StyleBorderHeight(SA_PARAM_OP_VALID UIStyleBorder *pBorder);

void ui_StyleBorderLoadTextures(SA_PARAM_NN_VALID UIStyleBorder *pBorder);

AUTO_ENUM;
typedef enum UIStyleBarDirection
{
	kUIStyleBarNone = 0,
	kUIStyleBarPositive = 1 << 0,
	kUIStyleBarNegative = 1 << 1,
	kUIStyleBarBoth = kUIStyleBarPositive | kUIStyleBarNegative
} UIStyleBarDirection;

AUTO_STRUCT WIKI("Styles") AST_FOR_ALL(WIKI(AUTO));
typedef struct UIStyleBar
{
	const char *pchName; AST(STRUCTPARAM KEY)
	REF_TO(UITextureAssembly) hEmpty; AST(NAME(EmptyAssembly))
	REF_TO(UITextureAssembly) hFilled; AST(NAME(FilledAssembly))
	REF_TO(UITextureAssembly) hDynamicOverlay; AST(NAME(MovingOverlayAssembly, DynamicOverlayAssembly))
	REF_TO(UITextureAssembly) hStaticOverlay; AST(NAME(StaticOverlayAssembly))
	REF_TO(UITextureAssembly) hNotch;  AST(NAME(NotchAssembly))
	UIDirection eFillFrom; AST(DEFAULT(UILeft))
	bool bNotchForceInside; AST(NAME(NotchForceInside))

	UIStyleBarDirection eMovingOverlayDirection; AST(NAME(MovingOverlayDirection))
	F32 fMovingOverlayFadeIn; AST(NAME(MovingOverlayFadeIn))
	F32 fMovingOverlayFadeOut; AST(NAME(MovingOverlayFadeOut))

	const char *pchTick; AST(POOL_STRING RESOURCEDICT(Texture))
	const char *pchTickFilled; AST(POOL_STRING RESOURCEDICT(Texture))
	U32 uiTickColor; AST(SUBTABLE(ColorEnum) FORMAT_COLOR DEFAULT(0xFFFFFFFF) NAME(TickColor))
	U32 uiTickFilledColor; AST(SUBTABLE(ColorEnum) FORMAT_COLOR NAME(TickFilledColor))
	AtlasTex *pTick; NO_AST
	AtlasTex *pTickFilled; NO_AST
	S32 iTickCount; AST(NAME(TickCount))

	// Text can be drawn overlaid on bars.
	REF_TO(UIStyleFont) hFont; AST(NAME(Font))

	// If true, the filled area is drawn by clipping at the appropriate point
	// rather than by drawing in the TexAs in the filled area.
	bool bClipFilledArea : 1;

	bool bTickSnapToPixel : 1; AST(DEFAULT(1))

	bool bScaleTick : 1; AST(DEFAULT(1))
	bool bStretchTick : 1;

	const char *pchFilename; AST(CURRENTFILE)
} UIStyleBar;

void ui_StyleBarDraw(SA_PARAM_OP_VALID const UIStyleBar *pBar, SA_PARAM_NN_VALID const CBox *pBox, F32 fPercentFull, F32 fNotch, S32 iTickCount, F32 fMovingOverlayAlpha, SA_PARAM_OP_STR const char *pchText, F32 fZ, S32 iAlpha, bool bClipToBox, F32 fScale, SpriteProperties *pSpriteProps);
SA_RET_OP_VALID UIStyleBar *ui_StyleBarGet(const char *pchName);

S32 ui_StyleBarGetHeight(SA_PARAM_OP_VALID UIStyleBar *pBar);
S32 ui_StyleBarGetLeftPad(SA_PARAM_OP_VALID UIStyleBar *pBar);
S32 ui_StyleBarGetRightPad(SA_PARAM_OP_VALID UIStyleBar *pBar);
SA_RET_OP_VALID UIStyleFont *ui_StyleBarGetFont(SA_PARAM_OP_VALID UIStyleBar *pBar);

void ui_LoadStyles(void);

extern DefineContext *g_ui_pColorPaletteExtraStates;
#define COLOR_PALETTE_MAX_STATE_BUFFER 4

AUTO_ENUM AEN_EXTEND_WITH_DYNLIST(g_ui_pColorPaletteExtraStates);
typedef enum UIStyleColorPaletteStates
{
	// No state
	kUIStyleColorPaletteStateActivate, ENAMES(Activate)

	kUIStyleColorPaletteState_MAX, EIGNORE
} UIStyleColorPaletteStates;

AUTO_ENUM;
typedef enum UIStyleColorPaletteTweenMode
{
	// Interpolate each component separately
	kUIStyleColorPaletteTweenModeRGBA,

	// Convert colors to HSV then interpolate each component separately,
	// then convert color back to RGBA.
	kUIStyleColorPaletteTweenModeHSVA,

	// Convert colors to HSV then spend the first half the interpolation
	// desaturating the color and the second half resaturating the color.
	// At the midpoint, snap hue changes. Value and Alpha are interplated
	// normally. Colors are then converted back to RGBA.
	kUIStyleColorPaletteTweenModeDesaturateVA,
} UIStyleColorPaletteTweenMode;

AUTO_STRUCT;
typedef struct UIStyleColorPaletteEntry
{
	// The palette entry name
	const char *pchName; AST(STRUCTPARAM KEY POOL_STRING REQUIRED)

	// The RGBA color value
	U32 iColor; AST(SUBTABLE(ColorEnum) FORMAT_COLOR DEFAULT(0xFFFFFFFF) NAME(Color) STRUCTPARAM REQUIRED)

	// The color palette index for this color
	U32 iPaletteIndex; AST(NO_WRITE)

	// The user chosen color value
	U32 iCustomColor; AST(NO_WRITE)
} UIStyleColorPaletteEntry;

AUTO_STRUCT WIKI("Styles") AST_FOR_ALL(WIKI(AUTO));
typedef struct UIStyleColorPalette
{
	// The display name of this color palette
	REF_TO(Message) hDisplayName; AST(STRUCTPARAM NAME(DisplayName) NON_NULL_REF)

	// The defined list of colors sorted by color name
	UIStyleColorPaletteEntry **eaColorDef; AST(NAME(Color))

	// The list of colors sorted by palette index
	UIStyleColorPaletteEntry **eaPalette; AST(NO_INDEX NO_WRITE)

	// True if the user may modify this palette
	bool bCustomizable;
} UIStyleColorPalette;

AUTO_STRUCT;
typedef struct UIStyleColorPaletteTween
{
	// The type of the tween
	U32 eType; AST(STRUCTPARAM SUBTABLE(UITweenTypeEnum) REQUIRED)

	// The time for the tween to take place
	F32 fTotalTime; AST(STRUCTPARAM REQUIRED)

	// The interval between cycles
	F32 fTimeBetweenCycles; AST(STRUCTPARAM)

	// The type of color tween
	UIStyleColorPaletteTweenMode eMode; AST(NAME(Mode))
} UIStyleColorPaletteTween;

AUTO_STRUCT;
typedef struct UIStyleColorPaletteTweenState
{
	// The tween parameters
	UIStyleColorPaletteTween *pTween; AST(UNOWNED)

	// The current time of the tween
	F32 fTweenTime;

	// The initial color values
	UIStyleColorPalette InitialPalette;
	UIStyleColorPalette *pInitial; AST(UNOWNED)

	// The final color values
	UIStyleColorPalette FinalPalette;
	UIStyleColorPalette *pFinal; AST(UNOWNED)

	// The output entries
	UIStyleColorPalette *pOutput;
} UIStyleColorPaletteTweenState;

AUTO_STRUCT;
typedef struct UIStyleColorPaletteOverrideSequence
{
	// The name of this sequence
	const char *pchName; AST(STRUCTPARAM POOL_STRING)

	// Tween into this palette def
	UIStyleColorPaletteTween *pTween; AST(NAME(Tween))

	// The next override to sequence
	const char *pchNext; AST(POOL_STRING NAME(next))

	// Borrow colors from the named color palette
	const char *pchBorrowFrom; AST(POOL_STRING)

	// The base palette contents
	UIStyleColorPalette Palette; AST(EMBEDDED_FLAT)
} UIStyleColorPaletteOverrideSequence;

AUTO_STRUCT;
typedef struct UIStyleColorPaletteStateDef
{
	// The states in which this override applies
	const char **eapchInState; AST(NAME(InState) STRUCTPARAM POOL_STRING)

	// The states in which this override applies
	UIStyleColorPaletteStates *eaiStates; AST(NO_WRITE)

	// The sequence of color overrides (complex tweening)
	UIStyleColorPaletteOverrideSequence **eaSequence; AST(NAME(Sequence))

	// The tweening to use when this state def is exiting
	UIStyleColorPaletteTween *pExitTween; AST(NAME(ExitTween))
} UIStyleColorPaletteStateDef;

AUTO_STRUCT;
typedef struct UIStyleColorPaletteOverrideState
{
	// The state def
	UIStyleColorPaletteStateDef *pStateDef; AST(UNOWNED)

	// The current tween state
	UIStyleColorPaletteTweenState *pTweenState;

	// The current step in the sequence (-1 = entering)
	S32 iCurrentSequence; AST(DEFAULT(-1))

	// The next step in the sequence (-1 = nothing)
	S32 iNextSequence; AST(DEFAULT(0))
} UIStyleColorPaletteOverrideState;

AUTO_STRUCT WIKI("Styles") AST_FOR_ALL(WIKI(AUTO));
typedef struct UIStyleColorPaletteDef
{
	// The name of this color palette def
	const char *pchName; AST(STRUCTPARAM KEY POOL_STRING REQUIRED)

	// Tween into this palette def
	UIStyleColorPaletteTween *pTween; AST(NAME(Tween))

	// Borrow colors from another color palette
	const char *pchBorrowFrom; AST(POOL_STRING)

	// The base palette contents
	UIStyleColorPalette BasePalette; AST(EMBEDDED_FLAT)

	// The list of state overrides
	UIStyleColorPaletteStateDef **eaStateDef; AST(NAME(StateDef))
} UIStyleColorPaletteDef;

AUTO_STRUCT;
typedef struct UIStyleColorPaletteEntryDef
{
	// The palette entry name
	const char *pchName; AST(STRUCTPARAM KEY POOL_STRING)

	// The display name of this color palette entry
	REF_TO(Message) hDisplayName; AST(STRUCTPARAM NAME(DisplayName) NON_NULL_REF)

	// True if the user may modify this color
	bool bCustomizable;
} UIStyleColorPaletteEntryDef;

AUTO_STRUCT;
typedef struct UIStyleColorPaletteStateName
{
	// The name of the color palette
	const char *pchName; AST(STRUCTPARAM KEY POOL_STRING)

	// The state disable flag
	S32 iDisableFlag;

	// The state index
	S32 iState; NO_AST
} UIStyleColorPaletteStateName;

AUTO_STRUCT;
typedef struct UIStyleColorPaletteLoading
{
	// The list of color names in the palette
	UIStyleColorPaletteEntryDef **eaColors; AST(NAME(Color) POOL_STRING)

	// The list of state names
	UIStyleColorPaletteStateName **eaStateNames; AST(NAME(State) POOL_STRING)

	// The list of color palettes
	UIStyleColorPaletteDef **eaPalettes; AST(NAME(Palette))
} UIStyleColorPaletteLoading;

AUTO_STRUCT;
typedef struct UIStyleColorPaletteState
{
	// The active palette
	const char *pchCurrentPalette; AST(POOL_STRING)

	// The next palette
	const char *pchNextPalette; AST(POOL_STRING)

	// The tween state between palette changes
	UIStyleColorPaletteTweenState *pPaletteChange;

	// The active override states in the palette
	UIStyleColorPaletteOverrideState **eaActiveOverrides;

	// The final list of colors in the palette
	UIStyleColorPalette *pFinalPalette;

	// The last frame this palette was updated
	U32 uiLastFrame;

	// The data version of the palette
	U32 uiDataVersion;

	// The states
	U32 uiStates[COLOR_PALETTE_MAX_STATE_BUFFER];

	// The last states
	U32 uiLastStates[COLOR_PALETTE_MAX_STATE_BUFFER];
} UIStyleColorPaletteState;

extern UIStyleColorPaletteLoading g_ColorPalettes;

typedef enum UIStyleColorPalettePriority
{
	kUIStyleColorPalettePriority_Default,
	kUIStyleColorPalettePriority_User,
	kUIStyleColorPalettePriority_Override,
} UIStyleColorPalettePriority;

// Use alpha value of 3 as a magic key for color palette index
#define UI_STYLE_PALETTE_KEY	0x03
#define UI_STYLE_PALETTE_MASK	0xFF
#define UI_STYLE_PALETTE_FILE	"ui/ColorPalettes.def"

typedef U32 (*ui_StyleColorPaletteGetDisableFlagsCB)(void);
void ui_StyleColorPaletteSetDisableFlags(ui_StyleColorPaletteGetDisableFlagsCB cbDisableFlags);

// Toggle a palette state on
void ui_StyleColorPaletteSetState(UIStyleColorPaletteState *pPalette, UIStyleColorPaletteStates eState, bool bToggle);
void ui_StyleColorPaletteSetStateName(UIStyleColorPaletteState *pPalette, const char *pchState, bool bToggle);

// Perform a palette table lookup for a given color entry
U32 ui_StyleColorPaletteIndexEx(U32 uColor, UIStyleColorPaletteState *pPalette);
U32 ui_StyleColorPaletteIndex(U32 uColor);

const char *ui_StyleColorPaletteCurrent(void);
const char *ui_StyleColorPalettePriorityCurrent(S32 iPriority);
bool ui_StyleColorPaletteExists(const char *palette);
void ui_StyleColorPaletteSwitch(const char *palette);
void ui_StyleColorPaletteSwitchPriority(const char *palette, S32 iPriority);

#endif
