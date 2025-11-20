/***************************************************************************



***************************************************************************/

#ifndef UI_GEN_H
#define UI_GEN_H
GCC_SYSTEM

// UIGen is a data-driven widget set designed to be loaded entirely from
// textparser files.

#include "UICore.h"
#include "CBox.h"
#include "Color.h"
#include "RdrEnums.h"
#include "earray.h"
#include "timing.h"
#include "GlobalTypes.h"
#include "textparser.h"

typedef struct Message Message;
typedef struct Expression Expression;
typedef struct DefineContext DefineContext;
typedef struct UIStyleFont UIStyleFont;
typedef struct AtlasTex AtlasTex;
typedef struct UIGenInternal UIGenInternal;
typedef struct UIGenWidget UIGenWidget;
typedef struct UIGen UIGen;
typedef struct UIGenRequiredBorrow UIGenRequiredBorrow;
typedef struct UIGenCodeInterface UIGenCodeInterface;
typedef struct UIGenTutorialInfo UIGenTutorialInfo;
typedef struct ExprContext ExprContext;
typedef struct MultiVal MultiVal;
typedef const MultiVal CMultiVal;
typedef struct KeyInput KeyInput;
typedef struct KeyBind KeyBind;
typedef struct UIGenBackgroundImage UIGenBackgroundImage;
typedef struct UIPosition UIPosition;
typedef struct UIColumn UIColumn;
typedef struct BasicTexture BasicTexture;
typedef struct UITextureAssembly UITextureAssembly;
typedef struct GfxSpriteList GfxSpriteList;
typedef struct SpriteProperties SpriteProperties;

typedef struct UIGenBundleTexture UIGenBundleTexture;
typedef struct UIGenBundleTextureState UIGenBundleTextureState;

typedef struct UIGenBundleStyleBar UIGenBundleStyleBar;
typedef struct UIGenBundleStyleBarState UIGenBundleStyleBarState;

extern struct ParseTable parse_UIGen[];
#define TYPE_parse_UIGen UIGen
extern struct ParseTable parse_UIGenBackgroundImage[];
#define TYPE_parse_UIGenBackgroundImage UIGenBackgroundImage

typedef bool (*UIGenValidateFunc)(UIGen *pGen, UIGenInternal *pInt, const char *pchDescriptor);
typedef bool (*UIGenUserFunc)(UIGen *pGen, UserData pData);
typedef void (*UIGenInternalUserFunc)(UIGen *pGen, UIGenInternal *pInt, UserData pData);
typedef void (*UIGenLoopFunc)(UIGen *pGen);
typedef void (*UIGenFitSizeFunc)(UIGen *pGen, void *pInternal, CBox *pOut);
typedef bool (*UIGenInputFunc)(UIGen *pGen, KeyInput *pKey);

// Prepare the given expression context for pFor, using data available in pGen (which
// is probably either pFor or a parent of pFor).
typedef void (*UIGenUpdateContextFunc)(UIGen *pGen, ExprContext *pContext, UIGen *pFor);

// Used for UIGenMovableBox to remember positions.
typedef bool (*UIGenGetPosition)(const char *pchName, UIPosition *pPosition, S32 iVersion, U8 chClone, U8 *pchPriority, const char **ppchContents);
typedef bool (*UIGenSetPosition)(const char *pchName, const UIPosition *pPosition, S32 iVersion, U8 chClone, U8 chPriority, const char *pchContents);

typedef const char *(*UIGenGetValue)(const char *pchKey);
typedef bool (*UIGenSetValue)(const char *pchKey, const char *pchValue);

typedef bool (*UIGenOpenWindows)(const char *pchName, U32 uMaxOpenWindows, U32 *pOpenWindows);
typedef bool (*UIGenGetOpenWindows)(const char ***peaNames);

// Used for UIGenList to remember column ordering and width
typedef void (*UIGenGetListOrder)(const char *pchName, UIColumn ***peaCols, S32 iVersion);
typedef void (*UIGenSetListOrder)(const char *pchName, UIColumn ***peaCols, S32 iVersion);

typedef bool UIGenFilterProfanityForPlayerFunc(void);

#define UI_GEN_NON_NULL(p) (p) // this used to be more complicated...
#define UI_GEN_READY(p) (UI_GEN_NON_NULL(p) && (p)->pResult)

#define UI_GEN_IS_TYPE(p, t) (UI_GEN_NON_NULL(p) && ((p)->eType == (t) || (t) == kUIGenTypeNone))

// Get a casted pResult variable off a UIGen, with type-checking.
#define UI_GEN_RESULT(pGen, Type) (devassertmsg(UI_GEN_IS_TYPE(pGen, kUIGenType##Type), "Getting the wrong kind of gen result!") ? (UIGen##Type *)(pGen)->pResult : NULL)

// Get a casted pState variable off a UIGen, with type-checking.
#define UI_GEN_STATE(pGen, Type) (devassertmsg(UI_GEN_IS_TYPE(pGen, kUIGenType##Type), "Getting the wrong kind of gen state!") ? (UIGen##Type##State *)(pGen)->pState : NULL)

// Get a casted pInternal from a provided UIGenInternal, with type-checking.
#define UI_GEN_INTERNAL(pInternal, Type) (devassertmsg((pInternal)->eType == kUIGenType##Type, "Getting the wrong kind of internal!") ? (UIGen##Type *)(pInternal) : NULL)

#define UI_GEN_TEXTURE(pch) (((pch) && *(pch)) ? atlasLoadTexture(pch) : NULL)

#define UI_GEN_IS_TEXTURE(pch, pTex) (((pch) && *(pch)) ? ((pTex) && ((pTex)->name == (pch) || !stricmp((pTex)->name, (pch)))) : ((pTex) == NULL))

#define UI_GEN_LOAD_TEXTURE(pch, pTex) { \
	if (!UI_GEN_IS_TEXTURE((pch), (pTex))) \
		(pTex) = UI_GEN_TEXTURE(pch); \
	}

#define UI_GEN_EXPR_TAG "UIGen"

#define UI_GEN_DICTIONARY "UIGen"

// When iterating over children, the different steps need to iterate in different orders.
// This is value passed to the ForEachInPriority functions.
extern bool g_ui_bGenLayoutOrder;

#define UI_GEN_UPDATE_ORDER (!g_ui_bGenLayoutOrder)
#define UI_GEN_POINTER_UPDATE_ORDER (UI_GEN_UPDATE_ORDER)
#define UI_GEN_LAYOUT_ORDER (UI_GEN_UPDATE_ORDER)

// Draw needs to be in reverse tick order
#define UI_GEN_DRAW_ORDER (false)
#define UI_GEN_TICK_ORDER (!UI_GEN_DRAW_ORDER)

// The UI should tolerate a massive amount of jitter in float calculations before
// deciding to do anything expensive.
#define UI_GEN_NEARF(fA, fB) ((fA) - (fB) >= -0.1 && (fA) - (fB) <= 0.1)

#define UI_GEN_STATE_BITFIELD 7

#define UI_GEN_USE_MATRIX(pResult) \
	(pResult->Transformation.fScaleX != 1.0f) \
	|| (pResult->Transformation.fScaleY != 1.0f) \
	|| (pResult->Transformation.fShearX != 0.0f) \
	|| (pResult->Transformation.fShearY != 0.0f) \
	|| (pResult->Transformation.Rotation.fAngle != 0.0f)

extern bool g_ui_bGenDisallowExprObjPathing;

#define UI_GEN_ALLOW_OBJECT_PATHS (!gConf.bUIGenDisallowExprObjPathing && !g_ui_bGenDisallowExprObjPathing)

AUTO_ENUM;
typedef enum UIGenLayer
{
	kUIGenLayerRoot,
	kUIGenLayerWindow,
	kUIGenLayerModal,
	kUIGenLayerCutscene,
} UIGenLayer;


// This is the basic list of different gen types. Other parts of the game may
// implement other types.
AUTO_ENUM AEN_WIKI("Basic Gen Types");
typedef enum UIGenType
{
	// This is used only for some special arguments; no Gen should actually be this type.
	kUIGenTypeNone = 0, EIGNORE
	kUIGenTypeInternal = 0, EIGNORE

	// UIGenBox is the simplest type of gen. It has no additional properties, and is just
	// a rectangle on the screen. Boxes are often used to group other gens together.
	kUIGenTypeBox = 1,

	// UIGenText is a formatted text string displayed on the screen, all in the same font.
	// It does not support word-wrapping or multiple typefaces, but is much less expensive
	// than a UIGenSMF (in terms of memory and performance).
	kUIGenTypeText,

	// UIGenSprite is a texture drawn onto the screen, with optional rotation and tinting.
	kUIGenTypeSprite,

	// UIGenButton is a standard button that can perform an action when clicked. It also
	// has some properties to set to conveniently place text and a texture in it, though
	// for more complicated layouts you will need to use normal children.
	kUIGenTypeButton,

	// UIGenSlider is a simple slider or meter. It supports a "value", which is how full
	// the slider is, and a "notch" value, which is a mark on the bar. Sliders can be
	// passive or interactive.
 	kUIGenTypeSlider,

	// UIGenCheckButton is a standard checkbox that is either checked or not checked.
	kUIGenTypeCheckButton,

	// UIGenTextEntry is a single-line text entry the user can type in.
	kUIGenTypeTextEntry,

	// UIGenTextArea is a multi-line text entry the user can type in.
	kUIGenTypeTextArea,

	// UIGenList is a multi-row list box.
	kUIGenTypeList,

	// UIGenListRow is the type of gen that must be within a UIGenList, although the ListRow
	// can have children itself.
	kUIGenTypeListRow,

	// UIGenSMF is a more advanced version of UIGenText that supports some basic HTML-like
	// markup, allowing for word-wrapping, inline images, and multiple typefaces.
	kUIGenTypeSMF,

	// UIGenLayoutBox is a special kind of UIGenBox that tries to automatically lay out
	// the "Template" child within it.
	kUIGenTypeLayoutBox,

	// UIGenMovableBox is a UIGenBox that can be moved by the user and has enough information
	// to remember its size and position.
	kUIGenTypeMovableBox,

	// UIGenListColumn is the type of gen that must be within a UIGenList.
	kUIGenTypeListColumn,

	// UIGenTabGroup is a horizontrol scrolling layout for gens
	kUIGenTypeTabGroup,

	// UIGenColorChooser is a list of colors that a user can choose from
	kUIGenTypeColorChooser,

	// UIGenScrollView is a box with a scrollbar and a virtual child area.
	kUIGenTypeScrollView,

	// UIGenWebView is a box that displays an HTMLViewer
	kUIGenTypeWebView,

	kUIGenType_MAX, EIGNORE
} UIGenType;

extern DefineContext *g_ui_pGenExtraStates;

// Gens are always in some set of _states_, which define what they look like
// and how they act. The states relating to input and the environment the
// game is running in are listed below. Additionally, in the UIGenStates.def
// file in data/ui/gens, you can define any number of additional states which
// may be specific to a game or a particular screen.
AUTO_ENUM AEN_EXTEND_WITH_DYNLIST(g_ui_pGenExtraStates) AEN_WIKI("Gen States");
typedef enum UIGenState
{
	kUIGenStateNone = 0, EIGNORE

	// Gens are in the visible state immediately after they are displayed. It can be
	// used to create an animation during appearance, e.g. *Width 0* in the base,
	// but *Width 100* in the Visible state, along with a tween of some sort, will
	// cause the gen to "expand" into view.
	kUIGenStateVisible,

	// This state should be one of the very basic states.
	kUIGenStateApril1,

	// Set when the client is running on an Xbox 360.
	kUIGenStateXbox,

	// Set when the client is running on Windows.
	kUIGenStateWindows,

	// Set while the client is in Production-Edit mode (for UGC)
	kUIGenStateProductionEdit,

	kUIGenStateJailed,

	// Set when the client is running at less than 800x600.
	kUIGenStateStandardDefinition,

	// Set when the client is running at 800x600 or greater.
	kUIGenStateHighDefinition,

	// Set when the client has a keyboard plugged in (always yes on Windows).
	kUIGenStateKeyboard,

	// Set when the client has a mouse plugged in.
	kUIGenStateMouse,

	// Set when the client has a Xbox 360 gamepad plugged in (always yes on the Xbox 360).
	kUIGenStateGamepad,

	// Set when either the left or right mouse button is pressed anywhere on the screen.
	kUIGenStateMouseDownAnywhere,

	// Set when the left mouse button is pressed anywhere on the screen.
	kUIGenStateLeftMouseDownAnywhere,

	// Set when the right mouse button is pressed anywhere on the screen.
	kUIGenStateRightMouseDownAnywhere,

	// Set when this gen has focus.
	kUIGenStateFocused,

	// Set when this gen should show a tooltip even without mouse interaction.
	kUIGenStateTooltipFocused,

	// Set when an ancestor of this gen has focus
	kUIGenStateFocusedAncestor,

	// Set when a child (not necessarily immediate) of this gen has focus
	kUIGenStateFocusedChild,

	// Set when the mouse is hovering over this gen.
	kUIGenStateMouseInside,
	kUIGenStateMouseOutside,

	// Set when the mouse is hovering over this gen and the input is not yet handled.
	kUIGenStateMouseOver,
	kUIGenStateMouseNotOver,

	// Set when the mouse is hovering over this row.
	kUIGenStateMouseInsideRow,
	kUIGenStateMouseOutsideRow,

	// Set when the mouse is hovering over this row.
	kUIGenStateMouseOverRow,
	kUIGenStateMouseNotOverRow,

	// Set when either mouse button was pressed over this gen, and is still down over this gen.
	kUIGenStateMouseDown,
	kUIGenStateLeftMouseDown,
	kUIGenStateRightMouseDown,

	// Set when either mouse button was released over this gen.
	kUIGenStateMouseUp,
	kUIGenStateLeftMouseUp,
	kUIGenStateRightMouseUp,

	// Set when either mouse button was pressed and released over this gen.
	kUIGenStateMouseClick,
	kUIGenStateLeftMouseClick,
	kUIGenStateRightMouseClick,

	// Set when either mouse button double-clicked on this gen.
	kUIGenStateLeftMouseDoubleClick,
	kUIGenStateRightMouseDoubleClick,

	// Set when the mouse was pressed outside of this gen and is still down.
	kUIGenStateMouseDownOutside,
	kUIGenStateLeftMouseDownOutside,
	kUIGenStateRightMouseDownOutside,

	// Set when a drag started within this gen.
	kUIGenStateLeftMouseDrag,
	kUIGenStateRightMouseDrag,

	// Set when there is a drag-and-drop operation going on and this is
	// a valid drop target.
	kUIGenStateDropTargetValid,

	// Set when there is a drag-and-drop operation going on and this is
	// not a valid drop target.
	kUIGenStateDropTargetInvalid,

	// Set when this is gen is currently selected in a list.
	kUIGenStateSelected,

	// Set when dragging a scrollbar with the mouse
	kUIGenStateDragging,

	// Set when a scrollbar is visible on the gen.
	kUIGenStateScrollbarVisible,

	// Set while the mouse is pressing the "up" arrow on a scrollbar.
	kUIGenStateScrollingUp,

	// Set while the mouse is pressing the "down" arrow on a scrollbar.
	kUIGenStateScrollingDown,

	// A special state that indicates that the mouse was pressed over the area,
	// and has not yet been released, but may not still be over the area.
	kUIGenStatePressed,
	kUIGenStateLeftMousePressed,
	kUIGenStateRightMousePressed,

	kUIGenStateLeftMouseDownStartedOver,
	kUIGenStateRightMouseDownStartedOver,
	
	// This is the first element of e.g. a TabGroup, LayoutBox, or List.
	kUIGenStateFirst,

	// This is the last element of e.g. a TabGroup, LayoutBox, or List.
	// Note that this is not exclusive with being the first element.
	kUIGenStateLast,

	// This is an even numbered element
	kUIGenStateEven,

	// This layout box, list, tab group, or text area/entry is empty.
	kUIGenStateEmpty,

	// This layout box, list, tab group, or text area/entry has at least one thing in it.
	kUIGenStateFilled,

	// The paperdoll texture has not yet finished rendering.
	kUIGenStatePaperdollNotReady,

	// The paperdoll texture has finished rendering.
	kUIGenStatePaperdollReady,

	// Set if a gen is in the selected state, and is focused or its children are focused
	// or its ancestor is focused.
	kUIGenStateSelectedInFocusPath,

	// Set during the frame where a "change" has been made, where change can mean different 
	// things for different gens. e.g. When a text entry changes, or when a slider is moved
	kUIGenStateChanged,

	// True if gendata is set
	kUIGenStateHasGenData,

	// These states have no predefined meaning, and are useful for quickly testing things.
	kUIGenStateUser,
	kUIGenStateUser1,
	kUIGenStateUser2,
	kUIGenStateUser3,
	kUIGenStateUser4,

	// These states have no predefined meaning, but are intended to be used to track
	// timer-based actions.
	kUIGenStateTimer,
	kUIGenStateTimer1,
	kUIGenStateTimer2,
	kUIGenStateTimer3,
	kUIGenStateTimer4,

	// Set this when the specific gen should be disabled, but not completely hidden
	kUIGenStateDisabled,

	// These states are set when the edge of the gen is touching the matching edge.
	kUIGenStateLeftEdge,
	kUIGenStateTopEdge,
	kUIGenStateRightEdge,
	kUIGenStateBottomEdge,

	// These states are set based on which area of the screen the center of the gen
	// is when divided into a 3x3 grid.
	kUIGenStateLeftSide,
	kUIGenStateTopSide,
	kUIGenStateRightSide,
	kUIGenStateBottomSide,
	kUIGenStateHorizontalCenter,
	kUIGenStateVerticalCenter,

	kUIGenState_MAX, EIGNORE
} UIGenState;

extern DefineContext *g_ui_pGenExtraSizes;
AUTO_ENUM AEN_EXTEND_WITH_DYNLIST(g_ui_pGenExtraSizes) AEN_WIKI("Gen Sizes");
typedef enum UISize
{
	kUISizeNone = 0,
	kUISizeCodeHalfStep = 4,
	kUISizeCodeStep = 8,
	kUISizeCodeDoubleStep = 16,
	kUISize_MAX, EIGNORE
} UISize;

AUTO_ENUM;
typedef enum UIGenAnimatedTextureStyle
{
	UIGenAnimatedTextureStyleNone,
	UIGenAnimatedTextureStyleSweepRight,
	UIGenAnimatedTextureStyleSweepLeft,
	UIGenAnimatedTextureStyleFillUp,
	UIGenAnimatedTextureStyleFillDown,
	UIGenAnimatedTextureStyleFillLeft,
	UIGenAnimatedTextureStyleFillRight,
	UIGenAnimatedTextureStyleScrollVertical,
	UIGenAnimatedTextureStyleScrollHorizontal,
} UIGenAnimatedTextureStyle;

// Filter based on the flags of the key, the contents of this enum needs to match KeyInputAttrib
AUTO_ENUM;
typedef enum UIGenCondition
{
	kUIGenConditionNone = 0,
	kUIGenConditionEqual,					ENAMES(Equal Equals Eq "==" "=")
	kUIGenConditionNotEqual,				ENAMES(NotEqual NotEquals Ne "!=")
	kUIGenConditionGreaterThan,				ENAMES(GreaterThan GT ">")
	kUIGenConditionLessThan,				ENAMES(LessThan LT "<")
	kUIGenConditionGreaterThanOrEqual,		ENAMES(GeraterThanOrEquals GeraterThanOrEqual GTE ">=")
	kUIGenConditionLessThanOrEqual,			ENAMES(LessThanOrEquals LessThanOrEqual LTE "<=")
} UIGenCondition;

AUTO_STRUCT;
typedef struct UISizeSpec
{
	F32 fMagnitude; AST(STRUCTPARAM)
	U8 eUnit; AST(STRUCTPARAM SUBTABLE(UIUnitTypeEnum))
} UISizeSpec;

AUTO_STRUCT;
typedef struct UIPosition
{
	// If you change which field in this structure is first or last,
	// update s_iStart in UIGenLayoutBox.c.
	S16 iX; AST(SUBTABLE(UISizeEnum))
	S16 iY; AST(SUBTABLE(UISizeEnum))
	F32 fPercentX;
	F32 fPercentY;

	UISizeSpec Width;
	UISizeSpec Height;
	UISizeSpec MinimumWidth;
	UISizeSpec MinimumHeight;
	UISizeSpec MaximumWidth;
	UISizeSpec MaximumHeight;

	S16 iLeftMargin; AST(SUBTABLE(UISizeEnum))
	S16 iRightMargin; AST(SUBTABLE(UISizeEnum))
	S16 iTopMargin; AST(SUBTABLE(UISizeEnum))
	S16 iBottomMargin; AST(SUBTABLE(UISizeEnum))

	// This is the absolute scale for this widget, the "real" scale is
	// calculated by multiplying this with the parent's real scale.
	F32 fScale; AST(DEFAULT(1.0))

	U8 eOffsetFrom; AST(DEFAULT(UITopLeft) SUBTABLE(UIDirectionEnum))

	bool bScaleNoGrow : 1;
	bool bScaleNoShrink : 1;
	bool bScaleAsIfWithGlobal : 1;

	// If this is true, fScale is absolute, i.e. it assumes the parent's
	// scale is 1.0 regardless of what it really is.
	bool bResetScale : 1;

	// If true, the screen box calculated will be clamped to the parent's size.
	bool bClipToParent : 1;

	// If true, the screen box calculated will be clamped to the screen's size
	bool bClipToScreen : 1;

	// If true, if the screen box is partly off screen, it will be repositioned to be on screen.
	// If the gen is anchored, then repositioning will occur around the anchor.  Otherwise the 
	// gen will be bumped up/down/right/left as needed to keep it on the screen.
	bool bPositionOnScreen : 1;

	bool bIgnoreParentPadding : 1;

	U16 ausScaleAsIf[2]; AST(NAME(ScaleAsIf))

} UIPosition;

AUTO_STRUCT;
typedef struct UIGenRelativeAtom
{
	const char *pchName; AST(POOL_STRING STRUCTPARAM)
	U16 eOffset; AST(STRUCTPARAM SUBTABLE(UIDirectionEnum))
	S16 iSpacing; AST(STRUCTPARAM DEFAULT(4) SUBTABLE(UISizeEnum))
} UIGenRelativeAtom;

AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL;
typedef struct UIGenRelative
{
	UIGenRelativeAtom TopFrom;
	UIGenRelativeAtom BottomFrom;
	UIGenRelativeAtom LeftFrom;
	UIGenRelativeAtom RightFrom;
} UIGenRelative;

AUTO_STRUCT;
typedef struct UIGenAnchor
{
	// The gen you want this gen anchored to
	const char *pchName; AST(POOL_STRING STRUCTPARAM)

	// How this gen should be oriented, Vertical or Horizontal.
	UIDirection eOrientation; AST(STRUCTPARAM)

	// How the gen should be aligned relative to the anchor.  The primary alignment
	// axis is determined by the orientation.  For example, if the orientation
	// is Vertical, then the gen will first be placed above or below the anchor
	// gen and then the right or left edge will be aligned.
	UIDirection eAlignment; AST(STRUCTPARAM)

	// An optional offset, a negative number moves towards the center of the anchor gen
	// a positive number moves away from the center of the anchor gen.
	S32 iOffset; AST(STRUCTPARAM)
} UIGenAnchor;

AUTO_ENUM AEN_WIKI("Tween Box Modes");
typedef enum UIGenTweenBoxMode {
	kUIGenTweenBoxModeNeither, ENAMES(None Neither)
	kUIGenTweenBoxModeIncrease, ENAMES(Increase Right Bottom Down Wider)
	kUIGenTweenBoxModeDecrease, ENAMES(Decrease Left Top Up Narrower)
	kUIGenTweenBoxModeBoth, ENAMES(Both Vertical Horizontal Either Any)
} UIGenTweenBoxMode;

AUTO_STRUCT WIKI("Tween Box Info");
typedef struct UIGenTweenBoxInfo
{
	U32 eXMode : 2; AST(STRUCTPARAM NAME(X) SUBTABLE(UIGenTweenBoxModeEnum) DEFAULT(3))
	U32 eYMode : 2; AST(STRUCTPARAM NAME(Y) SUBTABLE(UIGenTweenBoxModeEnum) DEFAULT(3))
	U32 eWidthMode : 2; AST(STRUCTPARAM NAME(Width) SUBTABLE(UIGenTweenBoxModeEnum) DEFAULT(3))
	U32 eHeightMode : 2; AST(STRUCTPARAM NAME(Height) SUBTABLE(UIGenTweenBoxModeEnum) DEFAULT(3))
	U32 eOffsetFrom : UIDirection_NUMBITS; AST(NAME(OffsetFrom) SUBTABLE(UIDirectionEnum) DEFAULT(10)) // UITopLeft = 10
} UIGenTweenBoxInfo;

AUTO_STRUCT WIKI("Tween Info");
typedef struct UIGenTweenInfo
{
	UITweenType eType; AST(STRUCTPARAM REQUIRED)
	F32 fTotalTime; AST(STRUCTPARAM REQUIRED)
	F32 fTimeBetweenCycles; AST(STRUCTPARAM)
	UIGenTweenBoxInfo *pTweenBox;
} UIGenTweenInfo;

AUTO_STRUCT;
typedef struct UIGenTweenState
{
	UIGenTweenInfo *pInfo; AST(UNOWNED)
	UIGenInternal *pStart;
	UIGenInternal *pEnd;
	F32 fElapsedTime;
} UIGenTweenState;

typedef struct UIGenTweenBoxCBox
{
	CBox ScreenBox;
	CBox UnpaddedScreenBox;
} UIGenTweenBoxCBox;

AUTO_STRUCT;
typedef struct UIGenTweenBoxState
{
	UIGenTweenInfo *pInfo;
	F32 fElapsedTime; AST(DEFAULT(-1))
	UIGenTweenBoxCBox Start; NO_AST
	UIGenTweenBoxCBox End; NO_AST
	bool bInitialized;
} UIGenTweenBoxState;

AUTO_STRUCT;
typedef struct UIGenBundleText
{
	REF_TO(UIStyleFont) hFont; AST(NAME(Font) NON_NULL_REF)
	REF_TO(Message) hText; AST(NAME(Text) NAME(SMF) NON_NULL_REF)
	Expression *pTextExpr; AST(NAME(TextBlock) REDUNDANT_STRUCT(TextExpr, parse_Expression_StructParam) REDUNDANT_STRUCT(SMFExpr, parse_Expression_StructParam) LATEBIND)

	UIDirection eAlignment : UIDirection_NUMBITS;

	bool bFilterProfanity : 1;

	// If this is true, the text is scaled to fit the box dimensions.
	// If false, the text is rendered at its native scale and clipped
	// or not depending on base.bClip.
	bool bScaleToFit : 1;
	bool bShrinkToFit : 1;

	// Don't do any word wrapping; not all texts wrap, in which case this has no effect.
	bool bNoWrap : 1;
} UIGenBundleText;

AUTO_STRUCT;
typedef struct UIGenBundleTruncateState
{
	const char *pchTruncateString; AST(NAME(Truncatestring) UNOWNED)
	float fTruncateWidth;
	// for pointer comparisons only. 
	void *pPreviousTruncateMessage; NO_AST
} UIGenBundleTruncateState;

AUTO_STRUCT;
typedef struct UIGenBundleTextureAnimation
{
	// The animation style to use
	UIGenAnimatedTextureStyle eStyle;

	// Expression which determines the progress of the animation [0.0 - 1.0]
	Expression *pProgress; AST(NAME(Progress) REDUNDANT_STRUCT(ProgressExpr, parse_Expression_StructParam) LATEBIND)

	// Texture to put on the hotspot of the animation and orient accordingly
	const char* pchArmTexture; AST(RESOURCEDICT(Texture) POOL_STRING)

	// If this is set, then fAnimProgress will wrap to 0.0 - 1.0: i.e. 1.1 = 0.1
	bool bRepeat : 1;

} UIGenBundleTextureAnimation;

AUTO_STRUCT;
typedef struct UIGenBundleTextureAnimationState
{
	AtlasTex *pArmTexture; NO_AST
	F32 fAnimProgress;
} UIGenBundleTextureAnimationState;

AUTO_STRUCT;
typedef struct UIGenTextureCoordinateData
{
	F32 fOffsetU;
	F32 fOffsetV;
	F32 fScaleU; AST(DEFAULT(1.0))
	F32 fScaleV; AST(DEFAULT(1.0))
} UIGenTextureCoordinateData;

AUTO_STRUCT;
typedef struct UIGenBundleTexture
{
	// Texture name, can be a format string.
	const char *pchTexture; AST(RESOURCEDICT(Texture) POOL_STRING)
	const char *pchMask; AST(RESOURCEDICT(Texture) POOL_STRING)

	// Colors for the sprite; either specify one or four corners.
	U32 uiTopLeftColor; AST(NAME(TopLeftColor) NAME(Color) SUBTABLE(ColorEnum) FORMAT_COLOR)
	U32 uiTopRightColor; AST(NAME(TopRightColor) SUBTABLE(ColorEnum) FORMAT_COLOR)
	U32 uiBottomLeftColor; AST(NAME(BottomLeftColor) SUBTABLE(ColorEnum) FORMAT_COLOR)
	U32 uiBottomRightColor; AST(NAME(BottomRightColor) SUBTABLE(ColorEnum) FORMAT_COLOR)

	UIAngle Rotation;

	// Add special animation "effects" to this texture
	UIGenBundleTextureAnimation* pAnimation; AST(NAME(Animation))

	// Modifies the texture coordinates for this texture (only works for tiled textures)
	UIGenTextureCoordinateData TexCoordData; AST(NAME(TextureCoordinateData) EMBEDDED_FLAT)

	// Modifies the mask coordinates for this texture (only works for tiled textures)
	UIGenTextureCoordinateData MaskCoordData; AST(NAME(MaskCoordinateData) EMBEDDED_FLAT(Mask) )

	// How this box should be tiled/stretched to fit into the space.
	UITextureMode eMode : UITextureMode_NUMBITS;

	// Effects to apply to the sprite ("Desaturate" is the only one right now).
	RdrSpriteEffect eEffect : RdrSpriteEffect_NumBits;

	// The alignment of a scaled / "centered" texture in its gen box
	UIDirection eAlignment : UIDirection_NUMBITS;

	// Whether or not the texture uses additive or modulative blend
	bool bAdditive : 1;

	// Mark whether or not the given layer should be treated as a the base layer for compositing
	bool bBackground : 1; AST(NAME(ForceBottomLayer))

	// Enable skinning fixup on the texture & mask names
	bool bSkinningOverride : 1;

	// This sprite should be drawn at the same depth as the targeted entity's depth for stereo projection.
	bool bTargetEntityDepth : 1;
} UIGenBundleTexture;

AUTO_STRUCT;
typedef struct UIGenBundleTextureState
{
	AtlasTex *pTexture; NO_AST
	BasicTexture *pBasicTexture; AST(UNOWNED SUBTABLE(parse_BasicTexture) ADDNAMES(BasicTextureOverride))
	UIGenBundleTextureAnimationState* pAnimState;
} UIGenBundleTextureState;

AUTO_STRUCT;
typedef struct UIGenBundleStyleBar
{
	REF_TO(UIStyleBar) hBar; AST(NAME(Bar))
	Expression *pBarExpr; AST(NAME(BarBlock) REDUNDANT_STRUCT(BarExpr, parse_Expression_StructParam) LATEBIND)
} UIGenBundleStyleBar;

AUTO_STRUCT;
typedef struct UIGenMutateOther
{
	REF_TO(UIGen) hGen; AST(STRUCTPARAM REQUIRED)
	UIGenState *eaiEnterState; AST(NAME(EnterState))
	UIGenState *eaiExitState; AST(NAME(ExitState))
	UIGenState *eaiToggleState; AST(NAME(ToggleState))
	UIGenState *eaiCopyState; AST(NAME(CopyParentState))
} UIGenMutateOther;

AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL;
typedef struct UIGenMessagePacket
{
	const char *pchMessageName; AST(POOL_STRING STRUCTPARAM)
	// The gen to send the message to, if omitted, use Self.
	REF_TO(UIGen) hGen; AST(STRUCTPARAM)
} UIGenMessagePacket;

AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL;
typedef struct UIGenVarTypeGlob
{
	// Has a custom name because I keep typing Var[Blah].Name when I meant Var[Blah].String
	// and I don't want that to validate.
	const char *pchName; AST(KEY STRUCTPARAM POOL_STRING REQUIRED NAME(GlobName))
	union {
		UIGenState eState; AST(STRUCTPARAM SUBTABLE(UIGenStateEnum) NAME(State))
		S32 iInt; AST(REDUNDANTNAME)
	};

	F32 fFloat; AST(STRUCTPARAM)
	char *pchString; AST(ESTRING STRUCTPARAM)
} UIGenVarTypeGlob;

AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL;
typedef struct UIGenVarTypeGlobAndGen
{
	UIGenVarTypeGlob glob; AST(EMBEDDED_FLAT)
	REF_TO(UIGen) hTarget; AST(STRUCTPARAM)
} UIGenVarTypeGlobAndGen;

// Gens contain _actions_ to allow them to manipulate the game (including other gens)
// in various ways. For example, all gens have OnEnter and OnExit actions when entering
// or leaving a state. Some also have others, such as a button's OnClicked action.
//
// An action may have any number of commands, expressions, sounds, state changes, or mutators.
AUTO_STRUCT WIKI("Actions") AST_SINGLETHREADED_MEMPOOL;
typedef struct UIGenAction
{
	// A command to run. These are format strings.
	char **eachCommands; AST(NAME(Command) WIKI(AUTO))

	// An expression to run.
	Expression *pExpression; AST(NAME(ExpressionBlock) REDUNDANT_STRUCT(Expression, parse_Expression_StructParam) LATEBIND WIKI(AUTO))

	// A sound to play.
	const char **eachSounds; AST(NAME(Sound) RESOURCEDICT(Sound) WIKI(AUTO))

	// A state to enter when this action occurs. This is more efficient than using an expression or
	// command to do the same thing.
	UIGenState *eaiEnterState; AST(NAME(EnterState) WIKI(AUTO))

	// A state to exit when this action occurs. This is more efficient than using an expression or
	// command to do the same thing.
	UIGenState *eaiExitState; AST(NAME(ExitState) WIKI(AUTO))

	// A state to toggle when this action occurs. This is more efficient than using an expression or
	// command to do the same thing.
	UIGenState *eaiToggleState; AST(NAME(ToggleState) WIKI(AUTO))

	// A state to copy from the parent when this action occurs. This is more efficient than using an
	// expression or command to do the same thing.
	UIGenState *eaiCopyState; AST(NAME(CopyParentState) WIKI(AUTO))

	// Cause state changes on another gen. This is more efficient than using an expression or
	// command to do the same thing.
	UIGenMutateOther **eaMutate; AST(NAME(Mutate) WIKI(AUTO))

	// Send a message to a gen. This is more efficient than using an expression or
	// command to do the same thing.
	UIGenMessagePacket **eaMessage; AST(NAME(Message) WIKI(AUTO))

	// Set a variable in this or another gen. This is more efficient than using
	// an expression or command to do the same thing.
	UIGenVarTypeGlobAndGen **eaSetter; AST(NAME(Set) WIKI(AUTO) NO_INDEX)

	// Timing information for this action
	PerfInfoStaticData *pPerfInfo; NO_AST

	bool bFocus : 1;
	bool bUnfocus : 1;
	bool bTooltipFocus : 1;
	bool bTooltipUnfocus : 1;
} UIGenAction;

AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL;
typedef struct UIGenMessage
{
	const char *pchName; AST(POOL_STRING STRUCTPARAM KEY REQUIRED)
	UIGenAction Action; AST(EMBEDDED_FLAT)
} UIGenMessage;

AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL;
typedef struct UIGenStateDef
{
	UIGenState eState; AST(KEY STRUCTPARAM REQUIRED)
	UIGenInternal *pOverride;
	UIGenAction *pOnEnter;
	UIGenAction *pOnExit;
} UIGenStateDef;

AUTO_STRUCT;
typedef struct UIGenIntCondition
{
	const char* pchVar; AST(STRUCTPARAM REQUIRED)
	UIGenCondition eCondition; AST(STRUCTPARAM DEFAULT(kUIGenConditionNotEqual))
	S32 iValue; AST(STRUCTPARAM DEFAULT(0))
	const char *pchGen; AST(STRUCTPARAM POOL_STRING DEFAULT("_Self"))
	REF_TO(UIGen) hGen;
} UIGenIntCondition;

AUTO_STRUCT;
typedef struct UIGenFloatCondition
{
	const char* pchVar; AST(STRUCTPARAM REQUIRED)
	UIGenCondition eCondition; AST(STRUCTPARAM DEFAULT(kUIGenConditionNotEqual))
	F32 fValue; AST(STRUCTPARAM DEFAULT(0.0f))
	const char *pchGen; AST(STRUCTPARAM POOL_STRING DEFAULT("_Self"))
	REF_TO(UIGen) hGen;
} UIGenFloatCondition;

AUTO_STRUCT;
typedef struct UIGenStringCondition
{
	const char* pchVar; AST(STRUCTPARAM REQUIRED)
	UIGenCondition eCondition; AST(STRUCTPARAM DEFAULT(kUIGenConditionNotEqual))
	const char* pchValue; AST(STRUCTPARAM DEFAULT(""))
	const char *pchGen; AST(STRUCTPARAM POOL_STRING DEFAULT("_Self"))
	REF_TO(UIGen) hGen;
} UIGenStringCondition;

AUTO_STRUCT;
typedef struct UIGenCondition2
{
	const char* pchVar1; AST(STRUCTPARAM REQUIRED)
	UIGenCondition eCondition; AST(STRUCTPARAM REQUIRED)
	const char* pchVar2; AST(STRUCTPARAM REQUIRED)
	const char *pchGen1; AST(STRUCTPARAM POOL_STRING DEFAULT("_Self"))
	const char *pchGen2; AST(STRUCTPARAM POOL_STRING DEFAULT("_Self"))
	REF_TO(UIGen) hGen1;
	REF_TO(UIGen) hGen2;
} UIGenCondition2;

AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL;
typedef struct UIGenComplexStateDef
{
	Expression *pCondition; AST(NAME(ConditionBlock) REDUNDANT_STRUCT(Condition, parse_Expression_StructParam) LATEBIND)
	UIGenIntCondition **eaIntCondition;
	UIGenFloatCondition **eaFloatCondition;
	UIGenStringCondition **eaStringCondition;
	UIGenCondition2 **eaIntCondition2;
	UIGenCondition2 **eaFloatCondition2;
	UIGenCondition2 **eaStringCondition2;
	UIGenState *eaiInState; AST(NAME(InState))
	UIGenState *eaiNotInState; AST(NAME(NotInState))

	UIGenInternal *pOverride;
	UIGenAction *pOnEnter;
	UIGenAction *pOnExit;
} UIGenComplexStateDef;

AUTO_STRUCT;
typedef struct UIGenTimer
{
	F32 fTime; AST(STRUCTPARAM REQUIRED)
	const char *pchName; AST(STRUCTPARAM KEY)
	UIGenAction OnUpdate; AST(EMBEDDED_FLAT)
	F32 fCurrent;
	bool bPaused;
} UIGenTimer;

AUTO_STRUCT;
typedef struct UIGenPerTypeState
{
	UIGenType eType; AST(POLYPARENTTYPE(32))
} UIGenPerTypeState;

AUTO_STRUCT;
typedef struct UIGenBackgroundImage
{
	const char *pchImage; AST(POOL_STRING REQUIRED STRUCTPARAM)
	UITextureMode eType; AST(STRUCTPARAM DEFAULT(UITextureModeScaled))
	U32 uiTopLeft; AST(SUBTABLE(ColorEnum) FORMAT_COLOR STRUCTPARAM NAME(TopLeft))
	U32 uiBottomLeft; AST(SUBTABLE(ColorEnum) FORMAT_COLOR STRUCTPARAM NAME(BottomLeft))
	U32 uiTopRight; AST(SUBTABLE(ColorEnum) FORMAT_COLOR STRUCTPARAM NAME(TopRight))
	U32 uiBottomRight; AST(SUBTABLE(ColorEnum) FORMAT_COLOR STRUCTPARAM NAME(BottomRight))
} UIGenBackgroundImage;

AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL;
typedef struct UIGenBorrowed
{
	REF_TO(UIGen) hGen; AST(STRUCTPARAM)
	U32 uiComplexStates; NO_AST
} UIGenBorrowed;

AUTO_STRUCT;
typedef struct UIGenTextEntryCompletion
{
	char *pchSuggestion; AST(ESTRING)
	char *pchDisplay; AST(ESTRING)
	S16 iPrefixReplaceFrom;
	S16 iPrefixReplaceTo;
	bool bFinish : 1;
} UIGenTextEntryCompletion;

AUTO_STRUCT;
typedef struct UIGenSpriteCache
{
	S8 iFrameSkip; AST(DEFAULT(1) STRUCTPARAM)
	S8 iAccumulate; NO_AST
	GfxSpriteList *pSprites; NO_AST
	F32 fUsedZ; NO_AST
	UIGen **eaPopupChildren; NO_AST
	bool bTooltipFocus; NO_AST
} UIGenSpriteCache;

AUTO_STRUCT;
typedef struct UIGenPointerUpdate
{
	//ParseTable *pTable; AST(STRUCTPARAM)
	Expression *pExpression; AST(NAME(ExpressionBlock) REDUNDANT_STRUCT(Expression, parse_Expression_StructParam) LATEBIND)
} UIGenPointerUpdate;

//////////////////////////////////////////////////////////////////////////
// Okay, this is a bit hard to follow. The on-screen Gen is actually
// the result of baking several Gens together, e.g. when clicked, we bake
// the base state, the mouse over state, and the mouse click state
// together. Since this is relatively expensive, we only do it in a few
// circumstances e.g. our data was reloaded, our state changed (or is
// in the middle of changing), or our parent was dirty. This is all done
// during the update phase.
AUTO_STRUCT WIKI("UIGen") AST_SINGLETHREADED_MEMPOOL;
typedef struct UIGen
{
	// The name of this gen. The name can be used to reference gens in
	// expressions and commands.
	const char *pchName; AST(KEY STRUCTPARAM POOL_STRING WIKI(AUTO))

	// parsed from file, tree is recursed.
	UIGenBorrowed **eaBorrowTree; AST(NAME(BorrowFrom) STRUCTPARAM)

	// used by gen internal code, flat list. made at runtime.
	UIGenBorrowed **eaBorrowed; AST(NAME(FlatBorrowFrom))

	UIGenRequiredBorrow **eaRequiredBorrows; AST(NAME(RequiresBorrow))

	UIGen **eaBorrowedInlineChildren; AST(STRUCT_NORECURSE)

	UIGenType eType;

	// The base state of the gen.
	UIGenInternal *pBase; AST(WIKI(AUTO))
	UIGenInternal *pCodeOverrideEarly; AST(NO_WRITE)

	// A list of overrides based on simple states.
	UIGenStateDef **eaStates; AST(NAME(StateDef) WIKI(AUTO))

	// A list of overrides based on expression results.
	UIGenComplexStateDef **eaComplexStates; AST(NAME(ComplexStateDef) WIKI(AUTO))
	U32 uiComplexStates;

	UIGenInternal *pLast; AST(WIKI(AUTO))

	// Timer-based actions.
	UIGenTimer **eaTimers; AST(NAME(Timer) WIKI(AUTO) NO_INDEX)

	// Named actions.
	UIGenMessage **eaMessages; AST(NAME(Message) WIKI(AUTO))

	UIGenTweenState *pTweenState; AST(NO_WRITE)
	UIGenTweenBoxState *pBoxTweenState; AST(NO_WRITE)

	// The final "baked" state of the gen.
	UIGenInternal *pResult; AST(NO_WRITE WIKI(AUTO) UNOWNED)

	UIGenCodeInterface *pCode; AST(NO_WRITE)

	// The internal state of the gen, e.g. the text in a text entry.
	UIGenPerTypeState *pState; AST(NO_WRITE NAME(State) NAME(GenState) WIKI(AUTO))

	U32 bfStates[UI_GEN_STATE_BITFIELD];

	UIGenAction **eaTransitions; AST(UNOWNED)

	// Called before the widget is first shown, i.e. the same time as BeforeResult,
	// but only when there's no existing Result.
	UIGenAction *pBeforeCreate; AST(WIKI(AUTO))

	UIGenAction *pAfterCreate; AST(WIKI(AUTO))

	// Called immediately after the pointer update step of the gen.
	UIGenPointerUpdate *pPointerUpdate; AST(WIKI(AUTO))
	
	// Called before the update step of the gen.
	UIGenAction *pBeforeUpdate; AST(WIKI(AUTO))

	// Called whenever we need to regenerate the Result of the gen.
	UIGenAction *pBeforeResult; AST(WIKI(AUTO))

	// Called whenever the gen is removed from the screen or reset while visible.
	UIGenAction *pBeforeHide; AST(WIKI(AUTO))

	UIGen *pParent; AST(UNOWNED WIKI(AUTO))
	const char *pchFilename; AST(CURRENTFILE)

	// A list of variable names and default values.
	UIGenVarTypeGlob **eaVars; AST(NAME(Var) NAME(Vars) WIKI(AUTO))

	UIGenVarTypeGlob **eaCopyVars; AST(NAME(CopyVar) NAME(CopyVars) WIKI(AUTO))

	// The jail cell this gen should go into, if jailed.
	const char *pchJailCell; AST(POOL_STRING STRUCTPARAM)

	// The window template this gen should use.
	const char *pchWindow; AST(POOL_STRING)

	UIGenSpriteCache *pSpriteCache;

	// This is for checking to see if this UIGen has associated tutorial information with it
	UIGenTutorialInfo *pTutorialInfo; NO_AST

	U32 uiTimeLastUpdateInMs; NO_AST

	// In seconds.
	F32 fTimeDelta; NO_AST

	U32 uiFrameLastUpdate; NO_AST

	// Derived values based on the entire widget tree.
	CBox UnpaddedScreenBox; NO_AST
	CBox ScreenBox; NO_AST

	// Will be equal to UnpaddedScreenBox unless matrix transformations have been applied, 
	// in which case it will be the axis aligned bounding box of the translated UnpaddedScreenBox
	CBox BoundingBox; NO_AST
	CBox ChildBoundingBox; NO_AST

	Mat3 TransformationMatrix; NO_AST
	F32 fScale; AST(NAME(FinalScale) DEFAULT(1.0))
	U8 chAlpha; AST(NAME(FinalAlpha) DEFAULT(255))

	U8 chLayer; AST(NAME(Layer))
	U8 chPriority; AST(NAME(Priority) NAME(Priority))
	U8 chClone; AST(NAME(Clone))

	// If true, skip the "real" size calculation and go only on the estimation.
	// This is needed for the scroll view, which has to lie to its standard children.
	bool bUseEstimatedSize : 1;

	// If true, we need to regenerate pResult.
	bool bNeedsRebuild : 1;

	// Pull this gen out of the normal tick/draw loop and into the top level one.
	bool bPopup : 1;

	// Pull this gen's children out of the normal tick/draw loop.
	bool bTopLevelChildren : 1;

	// This gen will steal focus on create the next time it becomes ready.
	bool bNextFocusOnCreate : 1;

	// Used by debugging tools.
	bool bIsRoot : 1;
	bool bIsCutsceneRoot : 1;
	bool bNoHighlight : 1;

	// used to prevent recursion
	bool bRecursionLocked : 1;

} UIGen;

// EArrays of references need an intermediate structure to be serialized properly.
AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL;
typedef struct UIGenChild
{
	REF_TO(UIGen) hChild; AST(STRUCTPARAM REQUIRED)
} UIGenChild;

AUTO_STRUCT;
typedef struct UIGenRequiredBorrow
{
	REF_TO(UIGen) hGen; AST(STRUCTPARAM REQUIRED)
} UIGenRequiredBorrow;

AUTO_STRUCT;
typedef struct UIGenSecondaryTooltip
{
	REF_TO(Message) hTooltip; AST(STRUCTPARAM NON_NULL_REF)
	Expression *pTooltipExpr; AST(NAME(TooltipBlock) REDUNDANT_STRUCT(TooltipExpr, parse_Expression_StructParam) LATEBIND)
	const char *pchAssembly; AST(NAME(Assembly) DEFAULT("Tooltip") RESOURCEDICT(UITextureAssembly) POOL_STRING)
	U16 iMaxWidth; AST(STRUCTPARAM NAME(MaxWidth, MaximumWidth) SUBTABLE(UISizeEnum) DEFAULT(350))
	bool bSafeMode : 1;
	bool bFilterProfanity : 1;
} UIGenSecondaryTooltip;

AUTO_STRUCT;
typedef struct UIGenSecondaryTooltipGroup 
{
	UIGenSecondaryTooltip **eaSecondaryToolTips; AST(NAME(SecondaryToolTip))
	UIDirection eOrientation; AST(DEFAULT(UIHorizontal))
	UIDirection eStackDirection; AST(DEFAULT(UIVertical))
	UIDirection eAlignment; AST(DEFAULT(UINoDirection))
	UIDirection eStackAlignment; AST(DEFAULT(UINoDirection))
	S16 iPrimarySpacing; AST(SUBTABLE(UISizeEnum) DEFAULT(4))
	S16 iSecondarySpacing; AST(SUBTABLE(UISizeEnum) DEFAULT(4))
} UIGenSecondaryTooltipGroup;

AUTO_STRUCT;
typedef struct UIGenTooltip
{
	REF_TO(Message) hTooltip; AST(STRUCTPARAM NON_NULL_REF)
	Expression *pTooltipExpr; AST(NAME(TooltipBlock) REDUNDANT_STRUCT(TooltipExpr, parse_Expression_StructParam) LATEBIND)
	F32 fDelay; AST(STRUCTPARAM DEFAULT(-1))
	const char *pchAssembly; AST(NAME(Assembly) DEFAULT("Tooltip") RESOURCEDICT(UITextureAssembly) POOL_STRING)
	UIGenRelative *pRelative;
	UIGenSecondaryTooltipGroup secondaryTooltipGroup; AST(EMBEDDED_FLAT)
	U16 iMaxWidth; AST(STRUCTPARAM NAME(MaxWidth, MaximumWidth) SUBTABLE(UISizeEnum) DEFAULT(350))
	bool bInheritAlpha : 1;
	bool bInheritScale : 1;
	bool bIgnoreParent : 1;
	bool bMouseAnchor : 1;
	bool bSafeMode : 1;
	bool bFilterProfanity : 1;
	bool bForceTooltipOwnership : 1;	//Assume the mouse is always over this gen.
	// TODO: Add support for having an entirely other gen as a tooltip...
} UIGenTooltip;

// Filter based on the flags of the key, the contents of this enum needs to match KeyInputAttrib
AUTO_ENUM;
typedef enum UIGenKeyAttribFilter
{
	kUIGenKeyAttribFilter_Control	= 1 << 0,
	kUIGenKeyAttribFilter_Alt		= 1 << 1,
	kUIGenKeyAttribFilter_Shift		= 1 << 2,
	kUIGenKeyAttribFilter_Numlock	= 1 << 3,
} UIGenKeyAttribFilter;

AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL;
typedef struct UIGenKeyAction
{
	const char *pchKey; AST(STRUCTPARAM POOL_STRING KEY REQUIRED)
	UIGenAction action; AST(REQUIRED EMBEDDED_FLAT)
	S32 iKey1 : 15; AST(NO_WRITE)
	S32 iKey2 : 15; AST(NO_WRITE)
	UIGenKeyAttribFilter iAttribInclude : 4; AST(FLAGS)
	UIGenKeyAttribFilter iAttribExclude : 4; AST(FLAGS)
	bool bPassThrough : 1;
	bool bIgnore : 1;
} UIGenKeyAction;

AUTO_STRUCT;
typedef struct UIGenDragDropAction
{
	const char *pchTypeMatch; AST(STRUCTPARAM POOL_STRING)
	UIGenAction OnDropped; AST(EMBEDDED_FLAT)
} UIGenDragDropAction;

//Used by the UIGenList to keep track of its column ordering/size in the entity
AUTO_STRUCT;
typedef struct UIColumn
{
	const char *pchColName;		AST(POOL_STRING)
	F32 fPercentWidth;
} UIColumn;

AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL;
typedef struct UIGenTextureAssembly
{
	const char *pchAssembly; AST(STRUCTPARAM POOL_STRING NAME(Assembly))
	REF_TO(UITextureAssembly) hAssembly; AST(NAME(AssemblyRef) NO_WRITE)
	U32 uiFrameLastUpdate; AST(NO_WRITE)
	Color4 Colors; AST(STRUCT(parse_Color4))
	U8 iPaddingTop; AST(NAME(PaddingTop, TopPadding))
	U8 iPaddingBottom; AST(NAME(PaddingBottom, BottomPadding))
	U8 iPaddingLeft; AST(NAME(PaddingLeft, LeftPadding))
	U8 iPaddingRight; AST(NAME(PaddingRight, RightPadding))
} UIGenTextureAssembly;

AUTO_STRUCT;
typedef struct UIGenTransformation
{
	UISizeSpec CenterX; AST(NAME(CenterX))
	UISizeSpec CenterY; AST(NAME(CenterY))
	UIAngle Rotation; AST(NAME(Rotate))
	F32 fShearX; AST(DEFAULT(0.0f))
	F32 fShearY; AST(DEFAULT(0.0f))
	F32 fScaleX; AST(DEFAULT(1.0f))
	F32 fScaleY; AST(DEFAULT(1.0f))
} UIGenTransformation;

AUTO_STRUCT;
typedef struct UIGenInternal
{
	UIGenType eType; AST(POLYPARENTTYPE(32))

	// Kind of dumb, but keeping this near the top makes some things much faster.
	U32 bf[5]; AST(USEDFIELD)

	UIPosition pos; AST(EMBEDDED_FLAT WIKI(AUTO))

	UIGenRelative *pRelative; AST(WIKI(AUTO))
	UIGenRelative *pPostLayoutRelative; AST(WIKI(AUTO))
	UIGenAnchor *pAnchor; AST(NAME(Anchor) WIKI(AUTO))

	// A list of children, by name.
	UIGenChild **eaChildren; AST(NAME(Child) NAME(Children) WIKI(AUTO))

	// Children declared inline. IMPORTANT: When this GenInternal is a Result,
	// this list contains UNOWNED children; they are owned by their StateDef.
	// That means it cannot be used with StructDestroy normally.
	UIGen **eaInlineChildren; AST(NAME(InlineChild) STRUCT_NORECURSE NO_INDEX WIKI(AUTO))

	// A list of children to *not* show.
	const char **eaSuppressedChildren; AST(NAME(HideChild) WIKI(AUTO) POOL_STRING)

	// A list of children to *not* show, as we go.
	const char **eaRemovedChildren; AST(NAME(RemoveChild) WIKI(AUTO) POOL_STRING)

	// Assembly drawn below the UIGen
	UIGenTextureAssembly *pAssembly; AST(NAME(Assembly))

	// Assembly drawn above the UIGen
	UIGenTextureAssembly *pOverlayAssembly; AST(NAME(OverlayAssembly))

	// Alpha channel multiplier for this gen and its children.
	F32 fAlpha; AST(DEFAULT(1.0))

	UIGenTweenInfo *pTween;

	// Runs before layout of this gen or its children.
	UIGenAction *pBeforeLayout; AST(WIKI(AUTO))

	// Runs after layout of this gen.
	UIGenAction *pAfterLayout; AST(WIKI(AUTO))

	// Runs before ticking this gen but after ticking its children.
	UIGenAction *pBeforeTick; AST(WIKI(AUTO))

	// Actions associated with a key name.
	UIGenKeyAction **eaKeyActions; AST(NAME(KeyAction) WIKI(AUTO))

	UIGenAction *pDefaultKeyAction; AST(WIKI(AUTO))

	// A tooltip to show when the mouse hovers over this gen.
	UIGenTooltip *pTooltip; AST(WIKI(AUTO))

	// A background image to show while this gen is visible.
	UIGenBackgroundImage *pBackground; AST(WIKI(AUTO))

	// Actions to run when things are dropped.
	UIGenDragDropAction **eaDragDrop; AST(NAME(DragDrop))
	UIGenAction *pDragCancelled;

	UIGenMessage **eaMessages; AST(NAME(Message) WIKI(AUTO))

	// An arbitrary transformation to apply to this gen and all of its children
	UIGenTransformation Transformation; AST(NAME(Transform) EMBEDDED_FLAT WIKI(AUTO))

	// A cursor to use when the given is hovered over.
	REF_TO(UICursor) hCursor; AST(NAME(Cursor) NON_NULL_REF WIKI(AUTO))

	// The priority of this gen; higher means it will tick before (and draw after) gens with
	// lower priority.
	U8 chPriority; AST(NAME(Priority) WIKI(AUTO))

	// If true, Alpha is absolute rather than relative to the parent
	bool bResetAlpha : 1; AST(WIKI(AUTO))

	// Clip drawing of this gen and its children to this gen's box.
	bool bClip : 1; AST(WIKI(AUTO))

	// Reset the clip area if a parent set it.
	bool bResetClip : 1; AST(WIKI(AUTO))

	// Capture mouse events (except wheel events) so they do not affect gens below this one.
	bool bCaptureMouse : 1; AST(WIKI(AUTO))

	// Capture wheel events so they do not affect gens below this one.
	bool bCaptureMouseWheel : 1; AST(WIKI(AUTO))

	// Focus this gen if it is clicked on.
	bool bFocusOnClick : 1; AST(WIKI(AUTO))

	// Focus this gen when it's first created.
	bool bFocusOnCreate : 1; AST(WIKI(AUTO))

	// Do not let creation of another gen steal focus
	bool bKeepFocusOnCreate : 1; AST(WIKI(AUTO))

	// Focus this gen if nothing else is focused.
	bool bFocusByDefault : 1; AST(WIKI(AUTO))

	// Focus this gen all the time.
	bool bFocusEveryFrame: 1; AST(WIKI(AUTO))

	// Clip input to this gen's box for its children.
	bool bClipInput : 1; AST(WIKI(AUTO))

	// Reset the input clip area if a parent set it.
	bool bResetInputClip : 1; AST(WIKI(AUTO))

	// Back-propagate Priority values from children.
	bool bCopyChildPriority : 1; AST(WIKI(AUTO))

	// Clip to padded screen box.
	bool bClipToPadding : 1; AST(WIKI(AUTO))

} UIGenInternal;

AUTO_STRUCT;
typedef struct GenEnum
{
	const char* key;	AST(UNOWNED)
	S32 value;
} GenEnum;
extern ParseTable parse_GenEnum[];
#define TYPE_parse_GenEnum GenEnum

void ui_GenPositionToCBox(UIPosition *pChildPos, const CBox *pParentBox, F32 fParentScale, F32 fChildScale, CBox *pResult, const CBox *pFitContents, const CBox *pFitParent, UIGenAnchor *pAnchor);

// Translate the pChild CBox back into pChildPos.
bool ui_GenCBoxToPosition(UIPosition *pChildPos, CBox *pChild, const CBox *pParent, UIDirection eResize, F32 fScale);

bool ui_GenBundleTextGetText(UIGen *pGen, UIGenBundleText *pBundle, const char *pchStaticString, unsigned char **ppchString);
UIStyleFont *ui_GenBundleTextGetFont(UIGenBundleText *pBundle);
#define ui_GenBundleTextHasText(pBundle) ((pBundle)->pTextExpr || GET_REF((pBundle)->hText))
bool ui_GenGetTextFromExprMessage(UIGen *pGen, Expression *pTextExpr, Message *pMessage, const char *pchStaticString, unsigned char **ppchString, bool bFilterProfanity);

bool ui_GenBundleTruncate(UIGenBundleTruncateState *pState, UIStyleFont *pFont, Message *pTruncateMessage, F32 fWidth, F32 fScale, const unsigned char *pchText, unsigned char **ppchString);

void ui_GenBundleTextureUpdate(UIGen *pGen, SA_PARAM_NN_VALID const UIGenBundleTexture *pBundle, SA_PARAM_NN_VALID UIGenBundleTextureState *pState);
bool ui_GenBundleTextureFitContentsSize(UIGen *pGen, const UIGenBundleTexture *pBundle, CBox *pOut, SA_PARAM_NN_VALID UIGenBundleTextureState *pState);
void ui_GenBundleTextureDraw(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_NN_VALID UIGenInternal *pInt, SA_PARAM_NN_VALID const UIGenBundleTexture *pBundle, SA_PARAM_OP_VALID CBox *pBox, F32 fX, F32 fY, bool bCenterX, bool bCenterY, SA_PARAM_NN_VALID  UIGenBundleTextureState *pState, SA_PARAM_OP_VALID CBox *pOut);

// Functions to manage widget/global states.
void ui_GenStates(UIGen *pGen, ...);
#define ui_GenState(pGen, eState, bEnable) ui_GenStates((pGen), (UIGenState)(eState), (int)(bEnable), kUIGenStateNone)
#define ui_GenInState(pGen, eState) (TSTB((pGen)->bfStates, (eState)))

#define ui_GenSetState(pGen, eState) { \
	if (!ui_GenInState((pGen), (eState))) \
		ui_GenState((pGen), (eState), true); \
	}

#define ui_GenUnsetState(pGen, eState) { \
	if (ui_GenInState((pGen), (eState))) \
		ui_GenState((pGen), (eState), false); \
	}

void ui_GenSetGlobalState(UIGenState eState, bool bEnable);
void ui_GenSetGlobalStateName(const char *pchState, bool bEnable);
bool ui_GenInGlobalState(UIGenState eState);
bool ui_GenInGlobalStateName(const char *pchState);

#define UI_GEN_NO_VALIDATE NULL
#define UI_GEN_NO_POINTERUPDATE NULL
#define UI_GEN_NO_UPDATE NULL
#define UI_GEN_NO_LAYOUTEARLY NULL
#define UI_GEN_NO_LAYOUTLATE NULL
#define UI_GEN_NO_TICKEARLY NULL
#define UI_GEN_NO_TICKLATE NULL
#define UI_GEN_NO_DRAWEARLY NULL
#define UI_GEN_NO_FITCONTENTSSIZE NULL
#define UI_GEN_NO_FITPARENTSIZE NULL
#define UI_GEN_NO_HIDE NULL
#define UI_GEN_NO_INPUT NULL
#define UI_GEN_NO_UPDATECONTEXT NULL
#define UI_GEN_NO_QUEUERESET NULL

void ui_GenRegisterType(UIGenType eType,
						UIGenValidateFunc cbValidate,
						UIGenLoopFunc cbPointerUpdate,
						UIGenLoopFunc cbUpdate,
						UIGenLoopFunc cbLayoutEarly,
						UIGenLoopFunc cbLayoutLate,
						UIGenLoopFunc cbTickEarly,
						UIGenLoopFunc cbTickLate,
						UIGenLoopFunc cbDrawEarly,
						UIGenFitSizeFunc cbFitContentsSize,
						UIGenFitSizeFunc cbFitParentSize,
						UIGenLoopFunc cbHide,
						UIGenInputFunc cbInput,
						UIGenUpdateContextFunc cbUpdateContext,
						UIGenLoopFunc cbQueueReset);

void ui_GenAddExprFuncs(const char *pchExprTag);

// Safely find a Gen. If the type given does not match the type
// found with the given name, then this returns NULL.
SA_RET_OP_VALID UIGen *ui_GenFind(const char *pchName, UIGenType eType);

// A UIGenWidget is a "canvas" to place a UIGen on, such that it
// runs during the appropriate part of the normal UI2Lib loops.
UIGenWidget *ui_GenWidgetCreate(const char *pchGen, char chLayer);
void ui_GenWidgetSetGen(UIGenWidget *pGenWidget, UIGen *pGen);

void ui_GenInitPointerVar(const char *pchName, ParseTable *pTable);
void ui_GenSetPointerVar(const char *pchName, void *pStruct, ParseTable *pTable);
void ui_GenInitStaticDefineVars(StaticDefineInt *pDefine, const char *pchPrefix);
void ui_GenInitIntVar(const char *pchName, S32 iValue);
void ui_GenInitFloatVar(const char *pchName);
void ui_GenSetStringVar(const char *pchName, const char *pchValue);
void ui_GenSetIntVar(const char *pchName, S32 iValue);
void ui_GenSetFloatVar(const char *pchName, F32 fValue);

void GenSpritePropPop();
void GenSpritePropPush(SpriteProperties *spriteProp);
SpriteProperties *GenSpritePropGetCurrent();

// Return the ParseTable for the GenInternal polytype associated with the UIGen.
ParseTable *ui_GenGetType(SA_PARAM_NN_VALID UIGen *pGen);
ParseTable *ui_GenInternalGetType(SA_PARAM_NN_VALID UIGenInternal *pInt);

// Loop over every child of a UIGenInternal, in the order listed in the file.
static bool ui_GenForEach(UIGen ***peaGens, UIGenUserFunc cbForEach, UserData pData, bool bReverse);
static bool ui_GenInternalForEachChild(UIGenInternal *pInt, UIGenUserFunc cbForEach, UserData pData, bool bReverse);

UIGen*** ui_GenGetInstances(UIGen *pGen);

// Evaluate an expression with the given context; if the context is NULL, then an internal context
// is updated and used.
bool ui_GenEvaluateWithContext(UIGen *pGen, Expression *pExpr, MultiVal *pValue, ExprContext *pContext);
#define ui_GenEvaluate(pGen, pExpr, pValue) ui_GenEvaluateWithContext((pGen), (pExpr), (pValue), ui_GenGetContext(pGen))

// Can be used to update a gen's input state outside of the main loop,
// but please don't.
bool ui_GenTickMouse(UIGen *pGen);

//////////////////////////////////////////////////////////////////////////
// Accessors to change properties of UIGens from code rather than data.

// Get/set pointer data. The code will assert if pTableExpected is not the correct
// ParseTable (it can be NULL). ppTableOut is whatever the UIGen thought its
// ParseTable is.
void *ui_GenGetPointer(SA_PARAM_NN_VALID UIGen *pGen, ParseTable *pTableExpected, ParseTable **ppTableOut);
void ui_GenSetManagedPointer(SA_PARAM_NN_VALID UIGen *pGen, void *pStruct, ParseTable *pTable, bool bManageMemory);
#define ui_GenSetPointer(pGen, pStruct, pTable) ui_GenSetManagedPointer((pGen), (pStruct), (pTable), false)
void *ui_GenGetManagedPointer(UIGen *pGen, ParseTable *pTable);
void ui_GenClearPointer(SA_PARAM_NN_VALID UIGen *pGen);

// Same as Get/SetPointer, but for pointers to earrays. The ParseTable and pointer
// are stored separately from the normal pointers, so you can set a pointer and
// an earray. The gen can also manage the list's memory.

void ***ui_GenGetList(SA_PARAM_NN_VALID UIGen *pGen, ParseTable *pTableExpected, ParseTable **ppTableOut);

// If you want the gen to completely manage the list, use ui_GenGetManagedList with
// the type you want.  Then call ui_GenSetManagedList with the returned list.
void ***ui_GenGetManagedList(UIGen *pGen, ParseTable *pTableExpected);
#define ui_GenGetManagedListSafe(pGen, Type) ((Type ***)ui_GenGetManagedList(pGen, parse_##Type))
bool ui_GenIsListManagingStructures(SA_PARAM_NN_VALID UIGen *pGen);

// This sets the list state of the UIGen.  You can give it any list, however, there are specific
// behaviors depending on the list provided.
//
// If you give it the list returned by ui_GenGetManagedList, then it will manage the contents
// of the list.
//
// If you give it any other list, then it will create a copy of the list.  In this mode, you
// must ensure that the contents of this list will remain unchanged for the remainder of the
// frame.
void ui_GenSetManagedListEx(SA_PARAM_NN_VALID UIGen *pGen, void ***peaList, ParseTable *pTable, bool bManageStructures, const char *pchFunction);
#define ui_GenSetManagedList(pGen, peaList, pTable, bManageStructures) ui_GenSetManagedListEx(pGen, peaList, pTable, bManageStructures, __FUNCTION__)
#define ui_GenSetManagedListSafe(pGen, peaList, Type, bManageStructures) ui_GenSetManagedListEx(pGen, 0? (void ***)(*(Type ****)0 = (peaList)) : (void ***)(peaList), parse_##Type, bManageStructures, __FUNCTION__)
#define ui_GenSetList(pGen, peaList, pTable) ui_GenSetManagedListEx(pGen, peaList, pTable, false, __FUNCTION__)
#define ui_GenSetListSafe(pGen, peaList, Type) ui_GenSetManagedListEx(pGen, 0? (void ***)(*(Type ****)0 = (peaList)) : (void ***)(peaList), parse_##Type, false, __FUNCTION__)

void ui_GenSetFocus(UIGen *pGen);
void ui_GenSetTooltipFocus(UIGen *pGen);
UIGen *ui_GenGetFocus(void);

ExprContext *ui_GenGetContext(UIGen *pGen);

UIGenState ui_GenGetState(const char *pchName);

void ui_GenRunAction(UIGen *pGen, UIGenAction *pAction);

bool ui_GenAddEarlyOverrideChild(UIGen *pGen, UIGen *pChildGen);
bool ui_GenRemoveEarlyOverrideChild(UIGen *pGen, UIGen *pChild);

bool ui_GenAddEarlyOverrideChildTemplate(UIGen *pGen, UIGen *pChildGen);
bool ui_GenRemoveEarlyOverrideChildTemplate(UIGen *pGen, UIGen *pChildGen);

bool ui_GenTickCB(UIGen *pChild, UIGen *pParent);
bool ui_GenPointerUpdateCB(UIGen *pChild, UIGen *pParent);
bool ui_GenUpdateCB(UIGen *pChild, UIGen *pParent);
bool ui_GenLayoutCB(UIGen *pChild, UIGen *pParent);
bool ui_GenDrawCB(UIGen *pChild, UIGen *pParent);
void ui_GenFitContentsSize(UIGen *pGen, UIGenInternal *pInt, CBox *pBox);

UIGenAction *ui_GenFindMessage(UIGen *pGen, const char *pchMessage);
bool ui_GenSendMessage(UIGen *pGen, const char *pchMessage);

void ui_GenPositionRegisterCallbacks(UIGenGetPosition cbGet, UIGenSetPosition cbSet, UIGenSetPosition cbForget);
void ui_GenListOrderRegisterCallbacks(UIGenGetListOrder cbGetList, UIGenSetListOrder cbSetList);
UIGen *ui_GenCreate(const char *pchName, UIGenType eType);

void ui_GenReset(UIGen *pGen);
bool ui_GenQueueResetChildren(UIGen *pChild, UIGen *pParent);
void ui_GenQueueReset(UIGen *pGen);

bool ui_GenInDictionary(UIGen *pGen);

bool ui_GenAddModalPopup(UIGen *pGen);
bool ui_GenAddWindow(UIGen *pGen);
bool ui_GenAddWindowPos(UIGen *pGen, float fPercentX, float fPercentY);
bool ui_GenAddWindowId(UIGen *pGen, S32 iWindowId);
bool ui_GenAddWindowIdPos(UIGen *pGen, S32 iWindowId, float fPercentX, float fPercentY);
bool ui_GenRemoveWindow(UIGen *pGen, bool bForce);

// Remove pToRemove from pParent's child list. This checks Base, EarlyOverride, and LateOverride.
// If FromResult is true, it also removes it from the Result. However, this is rarely what you
// want, because then the child's BeforeHide / GenReset will never run.
bool ui_GenRemoveChild(UIGen *pParent, UIGen *pToRemove, bool bFromResult);

UIGen *ui_GenClone(UIGen *pGen);

UIGen *ui_GenCreateFromString(const char *pchDef);

bool ui_GenSetListSort(UIGen *pGen, const char *pchField, UISortType eSort);
void ui_GenSetListSortOrder(UIGen *pGen, UISortType eSort);
void ui_GenListSortList(UIGen *pGen, void **eaList, ParseTable *pTable);
#define ui_GenListSortListSafe(pGen, eaList, Type)	ui_GenListSortList(pGen, (void **)(0 ? *(Type ***)0 = (eaList) : (eaList)), parse_##Type)

//routines for accessing Gen vars
bool ui_GenSetVarEx(UIGen *pGen, const char *pchVar, F64 dValue, const char *pchString, bool create);
#define ui_GenSetVar(pGen, pchVar, dValue, pchString) ui_GenSetVarEx(pGen, pchVar, dValue, pchString, true)

// Routines for getting a gen's size
bool ui_GenGetBounds(UIGen *pChild, F32 *v2Size);
bool ui_GenAddBoundsHeight(UIGen *pGen, F32 *v2Size);
bool ui_GenAddBoundsWidth(UIGen *pGen, F32 *v2Size);

void ui_GenInternalDestroySafe(UIGenInternal **ppGenInternal);
UIGenInternal *ui_GenGetBase(UIGen *pGen, bool bCreate);
void ui_GenSetBaseField(UIGen *pGen, const char *pchField, bool bSet, S32 *piColumn);

//////////////////////////////////////////////////////////////////////////
// Kept here for optimization...

__forceinline static bool ui_GenForEachInPriority(UIGen ***peaGens, UIGenUserFunc cbForEach, UserData pData, bool bReverse)
{
	bool bStop = false;
	if (peaGens)
	{
		S32 iSize = eaSize(peaGens);
		S8 *achPriorities = _alloca(iSize);
		S8 chMaxPriority = 0;
		S8 chPriority = 0;
		S32 iDiff = bReverse ? -1 : 1;
		S32 i;

		if (iSize == 0)
			return bStop;

		for (i = 0; i < iSize; i++)
		{
			UIGen *pGen = (*peaGens)[i];
			achPriorities[i] = pGen->chPriority;
			MAX1(chMaxPriority, achPriorities[i]);
		}

		for (chPriority = bReverse ? chMaxPriority : 0; (bReverse ? (chPriority >= 0) : (chPriority <= chMaxPriority)) && !bStop; chPriority += iDiff)
		{
			for (i = bReverse ? (iSize - 1) : 0; (bReverse ? (i >= 0) : (i < iSize)) && !bStop; i += iDiff)
			{
				UIGen *pGen = (*peaGens)[i];
				if (achPriorities[i] == chPriority)
					bStop = cbForEach(pGen, pData);
			}
		}
	}
	return bStop;
}

__forceinline static bool ui_GenChildForEachInPriority(UIGenChild ***peaChildren, UIGenUserFunc cbForEach, UserData pData, bool bReverse)
{
	bool bStop = false;
	if (peaChildren)
	{
		S32 iSize = eaSize(peaChildren);
		S8 *achPriorities = _alloca(iSize);
		S8 chMaxPriority = 0;
		S8 chPriority = 0;
		S32 iDiff = bReverse ? -1 : 1;
		S32 i;

		if (iSize == 0)
			return bStop;

		for (i = 0; i < iSize; i++)
		{
			UIGen *pGen = GET_REF((*peaChildren)[i]->hChild);
			achPriorities[i] = pGen ? pGen->chPriority : 0;
			MAX1(chMaxPriority, achPriorities[i]);
		}

		for (chPriority = bReverse ? chMaxPriority : 0; (bReverse ? (chPriority >= 0) : (chPriority <= chMaxPriority)) && !bStop; chPriority += iDiff)
		{
			for (i = bReverse ? (iSize - 1) : 0; (bReverse ? (i >= 0) : (i < iSize)) && !bStop; i += iDiff)
			{
				UIGen *pGen = GET_REF((*peaChildren)[i]->hChild);
				if (achPriorities[i] == chPriority && pGen)
					bStop = cbForEach(pGen, pData);
			}
		}
	}
	return bStop;
}

__forceinline static bool ui_GenInternalForEachChild(UIGenInternal *pInt, UIGenUserFunc cbForEach, UserData pData, bool bReverse)
{
	S32 i;
	S32 iSize;
	bool bStop = false;
	S32 iDiff = bReverse ? -1 : 1;

	iSize = eaSize(&pInt->eaChildren);
	for (i = bReverse ? (iSize - 1) : 0; bReverse ? (i >= 0) : (i < iSize) && !bStop; i += iDiff)
	{
		UIGenChild *pChild = pInt->eaChildren[i];
		UIGen *pChildGen = GET_REF(pChild->hChild);
		if (pChildGen)
			bStop = cbForEach(pChildGen, pData);
	}

	if (bStop)
		return bStop;

	iSize = eaSize(&pInt->eaInlineChildren);
	for (i = bReverse ? (iSize - 1) : 0; bReverse ? (i >= 0) : (i < iSize) && !bStop; i += iDiff)
		bStop = cbForEach(pInt->eaInlineChildren[i], pData);
	return bStop;
}

__forceinline static bool ui_GenForEach(UIGen ***peaGens, UIGenUserFunc cbForEach, UserData pData, bool bReverse)
{
	S32 i;
	S32 iSize;
	bool bStop = false;
	S32 iDiff = bReverse ? -1 : 1;

	iSize = eaSize(peaGens);
	for (i = bReverse ? (iSize - 1) : 0; bReverse ? (i >= 0) : (i < iSize) && !bStop; i += iDiff)
		bStop = cbForEach((*peaGens)[i], pData);
	return bStop;
}

#define ui_GenMarkDirty(pGen) STATEMENT(ADD_MISC_COUNT(1, "ui_GenMarkDirty");(pGen)->bNeedsRebuild = true;)

void ui_AlignCBox(const CBox *pParent, CBox *pChild, UIDirection eAlignment);

UIGenTimer *ui_GenFindTimer(UIGen *pGen, const char *pchName);
void ui_GenSetPersistedValueCallbacks(UIGenGetValue cbValueGet, UIGenSetValue cbValueSet);
void ui_GenSetOpenWindowsCallbacks(UIGenGetOpenWindows cbOpenWindowsNames, UIGenOpenWindows cbOpenWindowsGet, UIGenOpenWindows cbOpenWindowsSet);
extern UIGenFilterProfanityForPlayerFunc *g_UIGenFilterProfanityForPlayerCB;
extern bool g_bUIGenFilterProfanityThisFrame;
extern struct TimerThreadData * g_CurrentGenTimerTD;
extern bool g_bUsingSpecialTimer;

F32 ui_GenGetBaseScale(void);
void ui_GenSetBaseScale(F32 fScale);

void ui_GenSetSMFNavigateCallback(int (*callback)(const char *));
void ui_GenSetSMFHoverCallback(int (*callback)(const char *, UIGen*));

void ui_GenLayersReset(void);

UIGenInternal *ui_GenGetCodeOverrideEarly(UIGen *pGen, bool bCreate);
void ui_GenSetCodeOverrideEarlyField(UIGen *pGen, const char *pchField, bool bSet, S32 *piColumn);

UITextureAssembly *ui_GenTextureAssemblyGetAssembly(UIGen *pGen, UIGenTextureAssembly *pAssembly);

S32 ui_GenLayoutBoxGetSelectedIndex(UIGen *pGen);
const char *ui_GenTextEntryGetText(UIGen *pEntry, bool bProfanityFiltered);
bool ui_GenTextEntrySetText(UIGen *pEntry, const char *pchText);

bool ui_GenAddTextureSkinOverride(const char *pchTextureSuffix);
bool ui_GenRemoveTextureSkinOverride(const char *pchTextureSuffix);
const char *ui_GenGetTextureSkinOverride(const char *pchTextureName);

// Initialize eaBorrowed from the eaBorrowTree for the UIGen. Mainly useful
// during loading validation.
bool ui_GenInitializeBorrows(UIGen *pGen);

// This should be used to destroy an earray of UIGens when in the state of a UIGen widget.
// Using eaDestroyStruct(peaUIGens, parse_UIGen) can cause a crash in the UpdateContext
// if one of the child gens attempts to run an expression while being reset.
__forceinline void eaDestroyUIGens(UIGen ***peaUIGens)
{
	while (eaSize(peaUIGens) > 0)
		StructDestroy(parse_UIGen, eaPop(peaUIGens));
	eaDestroy(peaUIGens);
}

void ui_GenGetAudioAssets(const char **ppcType, const char ***peaStrings, U32 *puiNumData, U32 *puiNumDataWithAudio);

void ui_GenInitTimer();
void ui_GenConnectTimer(bool bConnected);
void ui_GenStartTimings();
void ui_GenProcessTimings();
// currently requires a 2000 char buffer (200 * 10)
void ui_GenPrintTimingResults(char * pszOutput);

#endif
