/***************************************************************************
 
 
 
 ***************************************************************************/

#ifndef UI_COMMON_H
#define UI_COMMON_H
GCC_SYSTEM

#include "ReferenceSystem.h"

//////////////////////////////////////////////////////////////////////////
// Forward-declarations used by the UI
typedef struct RdrDevice RdrDevice;
typedef struct GfxFont GfxFont;
typedef struct UIDnDPayload UIDnDPayload;
typedef struct UIFocusPath UIFocusPath;
typedef struct UIScrollbar UIScrollbar;
typedef struct KeyInput KeyInput;
typedef struct AtlasTex AtlasTex;
typedef struct CBox CBox;
typedef struct StashTableImp * StashTable;
typedef void ** EArrayHandle;
typedef const void ** cEArrayHandle;	// non-const pointer to a non-const list of const pointers
typedef struct DynNode DynNode;
typedef struct DynFx DynFx;
typedef struct Message Message;
typedef struct MessageEditor MessageEditor;

// Forward-declarations for the UI
typedef struct UISizer UISizer;
typedef struct UIWidget UIWidget;
typedef struct UIWidget ** UIWidgetGroup;
typedef struct UIWindow UIWindow;
typedef struct UIWindow ** UIWindowGroup;
typedef struct UIFocusPath UIFocusPath;
typedef struct UICursor UICursor;
typedef struct UIDeviceState UIDeviceState;
typedef struct UITextureAssembly UITextureAssembly;
typedef void UIAnyWidget;

typedef EArrayHandle * UIModel;
typedef cEArrayHandle * cUIModel;
typedef struct UISMFView UISMFView;

extern struct StaticDefineInt ColorEnum[];

#define NN_UIAnyWidget SA_PRE_NN_BYTES(sizeof(UIWidget)) SA_POST_NN_VALID UIAnyWidget
#define OPT_UIAnyWidget SA_PRE_OP_BYTES(sizeof(UIWidget)) SA_POST_OP_VALID UIAnyWidget

//////////////////////////////////////////////////////////////////////////
// Declare enum types before other includes
AUTO_ENUM;
typedef enum UITextureMode
{
	UITextureModeNone, ENAMES(None)
	UITextureModeStretched, ENAMES(Stretched Stretch)
	UITextureModeStretchedToLayer, ENAMES(StretchedLayer StretchLayer)
	UITextureModeScaled, ENAMES(Scaled Scale)
	UITextureModeCentered, ENAMES(Centered Center)
	UITextureModeTiled, ENAMES(Tiled Tile)
	UITextureModeHeadshotScaled, ENAMES(HeadshotScaled HeadshotScale)
	UITextureModeFilled, ENAMES(Filled Fill)
	UITextureModeNinePatch, ENAMES(NinePatch)
	UITextureMode_MAX, EIGNORE
} UITextureMode;
extern StaticDefineInt UITextureModeEnum[];
#define UITextureMode_NUMBITS 5
STATIC_ASSERT(UITextureMode_MAX <= (1 << (UITextureMode_NUMBITS - 1)));

// A list of possible modified states
AUTO_ENUM;
typedef enum UIWidgetModifier
{
	kWidgetModifier_None      = 0,

	// Set when the widget is not interactable.
	kWidgetModifier_Inactive  = 1 << 0,

	// Set when the widget is actively "pressed down".
	kWidgetModifier_Pressed   = 1 << 1,

	// Set when the widget currently has focus.
	kWidgetModifier_Focused   = 1 << 2,

	// Set when the the mouse is over the widget.
	kWidgetModifier_Hovering = 1 << 3,

	// Set when the widget's contents have changed in some way.
	kWidgetModifier_Changed   = 1 << 4,

	// Set when the widget's contents are "inherited" in the textparser
	// sense. It could also be used for any "special" state.
	kWidgetModifier_Inherited = 1 << 5,
} UIWidgetModifier;
extern StaticDefineInt UIWidgetModifierEnum[];

// Used for setting widget offsets, among other things.
AUTO_ENUM;
typedef enum UIDirection
{
	UINoDirection = 0, ENAMES(NoDirection Center)
	UIBottom = 1 << 0, ENAMES(Bottom Below)
	UILeft   = 1 << 1,
	UIRight  = 1 << 2,
	UITop    = 1 << 3, ENAMES(Top Above)
	UITopLeft = UITop | UILeft,
	UIBottomLeft = UIBottom | UILeft,
	UITopRight = UITop | UIRight,
	UIBottomRight = UIBottom | UIRight,
	UIHeight = UITop | UIBottom,
	UIWidth = UILeft | UIRight,
	UIVertical = UITop | UIBottom, ENAMES(Vertical CenterVertical)
	UIHorizontal = UILeft | UIRight, ENAMES(Horizontal CenterHorizontal)
	UIAnyDirection = UITop | UILeft | UIBottom | UIRight, ENAMES(AnyDirection AllDirections CenterOut)
} UIDirection;
extern StaticDefineInt UIDirectionEnum[];
#define UIDirection_NUMBITS 5

// This is separate just for file organization purposes, it's really as
// much core UI as anything else in the file.
#include "UIStyle.h"

#define UI_INHERIT_FROM(types) union { types }
#define UI_WIDGET_TYPE UIWidget widget;

#define UI_TEXTURE_WIDTH(pTex) ((pTex) ? (pTex)->width : 0)
#define UI_TEXTURE_HEIGHT(pTex) ((pTex) ? (pTex)->height : 0)

//////////////////////////////////////////////////////////////////////////
// Spacing and positioning constants

// Any UI drawing will occur at Z values of at least this (and go
// arbitrarily high, depending on the number of widgets). If you
// need to draw something guaranteed to be above the UI, you need
// to draw it after the UI main loop and refer to g_ui_State.drawZ,
// plus some value.
#define UI_TOP_Z 12010.f
#define UI_PANE_Z 4010.f
#define UI_UI2LIB_Z 2010.f
#define UI_MAIN_Z 10.f

#define UI_GET_Z() (++g_ui_State.drawZ)

#define UI_INFINITE_Z 20000.f

#define UI_SAFE_AREA_OFFSET 0.075

// Do not make this bigger than UIWidget.priority can hold!
#define UI_HIGHEST_PRIORITY 232

// These are the standard distances between UI elements.
#define UI_STEP  (8.f)
#define UI_DSTEP (UI_STEP * 2.f)
#define UI_HSTEP (UI_STEP / 2.f)

#define UI_SCALE(var) ((var) * scale)
#define UI_FSCALE(var) ((var) * fScale)

#define UI_DSTEP_SC UI_SCALE(UI_DSTEP)
#define UI_STEP_SC  UI_SCALE(UI_STEP)
#define UI_HSTEP_SC UI_SCALE(UI_HSTEP)

#define UI_DSTEP_FSC UI_FSCALE(UI_DSTEP)
#define UI_STEP_FSC  UI_FSCALE(UI_STEP)
#define UI_HSTEP_FSC UI_FSCALE(UI_HSTEP)

// Calculate how much a highlighted widget should fade in the elapsed time.
#define UI_HIGHLIGHT_TIME 3.f

//////////////////////////////////////////////////////////////////////////
// Common UI texture names.
#define UI_XBOX_AB "A_Button_38"
#define UI_XBOX_TINY_AB "Xbox_Tiny_AB"
#define UI_XBOX_BB "B_Button_38"
#define UI_XBOX_TINY_BB "Xbox_Tiny_BB"
#define UI_XBOX_XB "XButton_38"
#define UI_XBOX_TINY_XB "Xbox_Tiny_XB"
#define UI_XBOX_YB "Y_Button_38"
#define UI_XBOX_TINY_YB "Xbox_Tiny_YB"
#define UI_XBOX_LT "Left_Trigger"
#define UI_XBOX_TINY_LT "Xbox_Tiny_LTrigger"
#define UI_XBOX_RT "Right_Trigger"
#define UI_XBOX_TINY_RT "Xbox_Tiny_RTrigger"
#define UI_XBOX_LB "Left_Shoulder"
#define UI_XBOX_TINY_LB "Xbox_Tiny_LB"
#define UI_XBOX_RB "Right_Shoulder"
#define UI_XBOX_TINY_RB "Xbox_Tiny_RB"
#define UI_XBOX_TINY_DPAD_UP "Xbox_Tiny_DPad_Up"
#define UI_XBOX_TINY_DPAD_DOWN "Xbox_Tiny_DPad_Down"
#define UI_XBOX_TINY_DPAD_LEFT "Xbox_Tiny_DPad_Left"
#define UI_XBOX_TINY_DPAD_RIGHT "Xbox_Tiny_DPad_Right"
#define UI_XBOX_START "Start_Button"
#define UI_XBOX_TINY_START "Xbox_Tiny_Start"
#define UI_XBOX_BACK "Back_Button"
#define UI_XBOX_TINY_BACK "Xbox_Tiny_Back"
#define UI_XBOX_TINY_LSTICK "Xbox_Tiny_LStick"
#define UI_XBOX_TINY_RSTICK "Xbox_Tiny_RStick"

//////////////////////////////////////////////////////////////////////////
// Used in the UI main loop functions to save typing.
#define UI_PARENT_ARGS F32 pX, F32 pY, F32 pW, F32 pH, F32 pScale
#define UI_PARENT_VALUES pX, pY, pW, pH, pScale
#define UI_MY_VALUES x, y, w, h, scale
#define UI_MY_ARGS F32 x, F32 y, F32 w, F32 h, F32 scale

// Type-safe casting to a UIWidget.
#define UI_WIDGET(w) (&(w)->widget)

// A UI main loop functions, used for drawing and ticking widgets.
typedef void (*UILoopFunction)(UIAnyWidget *, UI_PARENT_ARGS);

// A destructor call.
typedef void (*UIFreeFunction)(UIAnyWidget *);

typedef void (*UIFocusFunction)(UIAnyWidget *, UIAnyWidget *);

// Used for e.g. button clicking, selection changing, or any time
// a widget needs to trigger some user-specified behavior.
typedef void (*UIActivationFunc)(UIAnyWidget *, UserData);
typedef void (*UIActivation2Func)(UIAnyWidget *, UserData, UserData);

// Used for determining if activation of the widget is allowed at this time
typedef bool (*UIPreActivationFunc)(UIAnyWidget *, UserData);
typedef bool (*UIPreActivation2Func)(UIAnyWidget *, UserData, UserData);

// Used for closing a window.  The return value is checked to
// determine whether the close should be allowed.
typedef bool (*UICloseFunc)(UIAnyWidget *, UserData);

// Called for all keyboard input, on the focused widget. If it returns
// false, the widget's parent should handle the input.
typedef bool (*UIInputFunction)(UIAnyWidget *, struct KeyInput *);

// Called when drags are accepted on the widget and the source.
typedef void (*UIDragFunction)(UIAnyWidget *, UIAnyWidget *, UIDnDPayload *, UserData);

// Called when a scrollbar scrolls.
typedef void (*UIScrollFunc)(UIScrollbar *, UserData);

typedef void (*UIProcessKeyboardInput)(F32 fElapsed, RdrDevice *pDevice);

typedef bool (*UIPlayAudioByName)(const char *pchSound, const char *pchFileContext);

typedef void (*MessageEditorApplyCallback)(MessageEditor *pEditor, Message *pMessage, void *pUserData);

typedef void (*MessageEditorCloseCallback)(MessageEditor *pEditor, void *pUserData);

typedef bool (*UIFilterListFunc)(UIAnyWidget *, const char *, UserData);

typedef void (*UIExternLoopFunc)(void);

typedef void (*UIAddWidgetFunc)(UIAnyWidget *, UIAnyWidget *);

typedef void (*UIPostCameraUpdateFunc)(F32 fElapsed);

typedef void (*UISizeFunc)(UIAnyWidget *, Vec2, UserData);

//////////////////////////////////////////////////////////////////////////
// Types used many places in the UI.

// Control the meaning of measurements for size.
AUTO_ENUM;
typedef enum UIUnitType
{
	// Absolute units, e.g. pixels.
	UIUnitFixed,

	// Percentage of parent's value, e.g. Width 0.8 for 80% of parent's width.
	UIUnitPercentage,

	// Percentage of "native" value, e.g. Width 1.2 for 120% of the texture's real width.
	// For things without a native size, this is the same as Percentage.
	UIUnitFitContents,
	UIUnitFitToContents = UIUnitFitContents,

	// This is a special sizing mode for widgets that closely manage their children, like lists.
	// For widgets without a "fit" size, it is the same as Percentage.
	UIUnitFitParent,
	UIUnitFitToParent = UIUnitFitParent,

	UIUnitRatio,

	UIUnit_MAX, EIGNORE
} UIUnitType;

// Gens support several types of animation during tweening.
AUTO_ENUM AEN_WIKI("Tween Types");
typedef enum UITweenType
{
	// This tween is "instant", i.e. don't do anything until the time is up.
	kUITweenInstant,

	// Move linearly between the old and new values.
	kUITweenLinear,

	// Rapidly move away from the old values and ease int the new ones.
	kUITweenEaseIn,

	// Ease out of the old values and move rapidly to the new values.
	kUITweenEaseOut,

	// Ease out of the old values and then ease into the new values.
	kUITweenEaseOutEaseIn,

	// Move linearly between the old and new values, then back to the old values and repeat. (Cycles)
	kUITweenCycle,

	// Move linearly between the old and new values, then restart immediately at the
	//    old values and repeat. (Cycles)
	kUITweenSawtooth,

	// Move non-linearly between the old and new values, then jump to the old values and repeat. (Cycles)
	kUITweenSine,

	// Cycle non-linearly between the old and new values, easing in and out (Cycles)
	kUITweenCosine,

	kUITween_MAX, EIGNORE
} UITweenType;

#define UI_TWEEN_CYCLIC(eType) (kUITweenCycle <= (eType) && (eType) <= kUITweenCosine)

AUTO_ENUM;
typedef enum UIAngleUnitType
{
	UIAngleUnitRadians, ENAMES(Rad Radian Radians)
	UIAngleUnitDegrees, ENAMES(Deg Degree Degrees)
} UIAngleUnitType;

AUTO_STRUCT;
typedef struct UIAngle
{
	F32 fAngle; AST(REQUIRED STRUCTPARAM)
	UIAngleUnitType eUnit; AST(STRUCTPARAM DEFAULT(UIAngleUnitRadians))
} UIAngle;

// Used to feed a UIAngle to math functions
#define UI_ANGLE_TO_RAD(angle) \
	(((angle).eUnit == UIAngleUnitRadians) ? (angle).fAngle : (RAD((angle).fAngle)))

#define UI_ANGLE_TO_DEG(angle) \
	(((angle).eUnit == UIAngleUnitDegrees) ? (angle).fAngle : (DEG((angle).fAngle)))

typedef enum UIVideoMode
{
	UISD = 0,
	UIHD = 1,
} UIVideoMode;

AUTO_ENUM;
typedef enum UISortType
{
	UISortNone,
	UISortAscending,
	UISortDescending,
	UISortMax, EIGNORE
} UISortType;

// UI cursor settings. Access this through ui_SetCursor.
typedef struct UICursorState
{
	AtlasTex *base;
	AtlasTex *draggedIcon;
	AtlasTex *draggedIcon2;
	AtlasTex *overlay;
	S16 hotX, hotY;
	S16 draggedIconX, draggedIconY;
	U32 color;
	U32 overlayColor;

	const char* dragCursorName;

	U32 draggedIconColor;
	U32 draggedIconCenter : 1;
} UICursorState;
#define UI_DRAG_DIR_CURSOR_SKIN_CNT 9

extern DictionaryHandle s_CursorDict;

// Set the cursor to the given texture name, potentially with an overlaid
// second texture. The cursor must be reset every frame, or it will revert
// back to the simple pointer.
void ui_SetCursor(SA_PARAM_OP_STR const char *pchBase, SA_PARAM_OP_STR const char *pchOverlay, int iHotX, int iHotY, U32 uiColor, U32 uiOverlayColor);
void ui_SetCursorByPointer(SA_PARAM_OP_VALID UICursor *pCursor);
void ui_SetCursorForDeviceState(UIDeviceState *device, SA_PARAM_OP_STR const char *base, SA_PARAM_OP_STR const char *overlay, int hotX, int hotY, U32 uiColor, U32 uiOverlayColor);
void ui_SetCursorForDirection(UIDirection direction);
void ui_SetCursorByName(const char *pchName);
void ui_SetCurrentDefaultCursor(const char *pchName);
void ui_SetCurrentDefaultCursorByPointer(SA_PARAM_OP_VALID UICursor *pCursor);

// Gets a UICursor's name
const char *ui_GetCursorName(UICursor *pCursor);
// Gets the UICursor's texture
const char *ui_GetCursorTexture(UICursor *pCursor);
// Gets the UICursor's hot spot's x coordinate
S16 ui_GetCursorHotSpotX(UICursor *pCursor);
// Gets the UICursor's hot spot's y coordinate
S16 ui_GetCursorHotSpotY(UICursor *pCursor);

// Prevent anything else from changing the cursor this frame.
void ui_CursorLock(void);

//////////////////////////////////////////////////////////////////////////
// Skins are global color/font settings applied to a widget. There is
// a default skin (g_ui_State.skin) which all widgets receive by default.
// Per-widget skins can be set using ui_WidgetSkin.
AUTO_STRUCT AST_IGNORE( UISkin );
typedef struct UISkin
{
	const char* astrName; AST(NAME("Name") KEY POOL_STRING)
	const char* filename; AST(CURRENTFILE)
	
	// Default, highlight (e.g. selected menu item)
	Color background[2]; AST(RGBA, INDEX(0, "Background:") INDEX(1, "Highlight:"))

						 // Default, highlight, press, disabled, changed, parented.
	Color button[6]; AST(RGBA, INDEX(0, "Button:") INDEX(1, "Button(Highlight):"), INDEX(2, "Button(Pressed):"), INDEX(3, "Button(Disabled):"), INDEX(4, "Button(Changed):"), , INDEX(5, "Button(Parented):"))

					 // Active, inactive. Windows, menus, etc.
	Color border[2]; AST(RGBA, INDEX(0, "Border:"), INDEX(1, "Border(Inactive):"))

					 // Thin border, for menus, combo boxes, text entries.
	Color thinBorder[1]; AST(RGBA, INDEX(0, "ThinBorder:"))

						 // Active, inactive.
	Color titlebar[2]; AST(RGBA, INDEX(0, "TitleBar:"), INDEX(1, "TitleBar(Inactive):"))

					   // Normal, pressed (scrollbar, slider)
	Color trough[2]; AST(RGBA, INDEX(0, "Trough:"), INDEX(1, "Trough(Pressed):"))

					 // Text entry areas; Normal, active, disabled, changed, parented.
	Color entry[5]; AST(RGBA, INDEX(0, "Entry:"), INDEX(1, "Entry(Active):"), INDEX(2, "Entry(Disabled):"), INDEX(3, "Entry(Changed):"), INDEX(3, "Entry(Parented):"))

	REF_TO(UIStyleFont) hNormal; AST(NAME("NormalFont:", "hNormal", "DefaultFont:"))
	REF_TO(UIStyleBar) hTitlebarBar; AST(NAME("TitlebarBar:"))

	//////////////////////////////////////////////////////////////////////////
	//	Instead of using colors we can set style borders, or texture
	//	assemblies and per-widget fonts
	bool bUseStyleBorders;									AST(NAME("UseStyleBorders"))
	bool bUseTextureAssemblies;								AST(NAME("UseTextureAssemblies"))

	// UILabel
	const char* astrLabelStyle;								AST(NAME("LabelStyle:") POOL_STRING)
	REF_TO(UIStyleFont) hLabelFont;							AST(NAME("LabelFont:") POOL_STRING)
	REF_TO(UIStyleFont) hLabelFontDisabled;					AST(NAME("LabelFont(Disabled):") POOL_STRING)

	// UIButton 
	const char* astrButtonStyle;							AST(NAME("ButtonStyle:") POOL_STRING)
	const char* astrButtonStyleHighlight;					AST(NAME("ButtonStyle(Highlight):") POOL_STRING)
	const char* astrButtonStyleFocused;						AST(NAME("ButtonStyle(Focused):") POOL_STRING)
	const char* astrButtonStylePressed;						AST(NAME("ButtonStyle(Pressed):") POOL_STRING)
	const char* astrButtonStyleDisabled;					AST(NAME("ButtonStyle(Disabled):") POOL_STRING)
	REF_TO(UIStyleFont) hButtonFont;						AST(NAME("ButtonFont:"))
	REF_TO(UIStyleFont) hButtonFontHighlight;				AST(NAME("ButtonFont(Highlight):"))
	REF_TO(UIStyleFont) hButtonFontFocused;					AST(NAME("ButtonFont(Focused):"))
	REF_TO(UIStyleFont) hButtonFontPressed;					AST(NAME("ButtonFont(Pressed):"))
	REF_TO(UIStyleFont) hButtonFontDisabled;				AST(NAME("ButtonFont(Disabled):"))

	// UIPane
	const char* astrPaneStyle;								AST(NAME("PaneStyle:") POOL_STRING)
	const char* astrPaneTitleStyle;							AST(NAME("PaneTitleStyle:") POOL_STRING)
	REF_TO(UIStyleFont) hPaneTextFont;						AST(NAME("PaneTextFont:"))

	// UIWindow
	const char* astrWindowStyle;							AST(NAME("WindowStyle:") POOL_STRING)
	const char* astrWindowTitleStyle;						AST(NAME("WindowTitleStyle:") POOL_STRING)
	const char* astrWindowTitleStyleFlash;					AST(NAME("WindowTitleStyle(Flash):") POOL_STRING)
	REF_TO(UIStyleFont) hWindowTitleFont;					AST(NAME("WindowTitleFont:", "TitlebarFont:", "hTitlebar"))
	const char* astrWindowCloseButton;						AST(RESOURCEDICT(Texture) NAME("WindowTitleCloseButton:") POOL_STRING)
	const char* astrWindowCloseButtonHighlight;				AST(RESOURCEDICT(Texture) NAME("WindowTitleCloseButton(Highlight):") POOL_STRING)
	const char* astrWindowCloseButtonPressed;				AST(RESOURCEDICT(Texture) NAME("WindowTitleCloseButton(Pressed):") POOL_STRING)
	const char* astrWindowButtonStyle;						AST(NAME("WindowButtonStyle:"))
	const char* astrWindowButtonStyleHighlight;				AST(NAME("WindowButtonStyle(Highlight):"))
	const char* astrWindowButtonStylePressed;				AST(NAME("WindowButtonStyle(Pressed):"))
	bool bWindowButtonInsideTitle;							AST(NAME("WindowButtonInsideTitle:") DEFAULT(true))
	UIDirection eWindowTitleTextAlignment;					AST(NAME("WindowTitleTextAlignment:") DEFAULT(UILeft))
	U32 iWindowModalBackgroundColor;						AST(NAME("WindowModalBackgroundColor:") DEFAULT(80))
	bool bModalDialogUsesBackgroundColor;					AST(NAME("ModalDialogUsesBackgroundColor:"))

	// UIMenu
	const char* astrMenuBarStyle;							AST(NAME("MenuBarStyle:") POOL_STRING)
	const char* astrMenuBarButtonStyle;						AST(NAME("MenuBarButtonStyle:") POOL_STRING)
	const char* astrMenuBarButtonStyleHighlight;			AST(NAME("MenuBarButtonStyle(Highlight):") POOL_STRING)
	const char* astrMenuBarButtonStyleOpened;				AST(NAME("MenuBarButtonStyle(Opened):") POOL_STRING)
	REF_TO(UIStyleFont) hMenuBarButtonFont;					AST(NAME("MenuBarButtonFont:"))
	REF_TO(UIStyleFont) hMenuBarButtonFontHighlight;		AST(NAME("MenuBarButtonFont(Highlight):"))
	REF_TO(UIStyleFont) hMenuBarButtonFontOpened;			AST(NAME("MenuBarButtonFont(Opened):"))
	int iMenuBarSpacing;									AST(NAME("MenuBarSpacing:") POOL_STRING)
	const char* astrMenuPopupStyle;							AST(NAME("MenuPopupStyle:") POOL_STRING)
	const char* astrMenuPopupStyleHighlight;				AST(NAME("MenuPopupStyle(Highlight):") POOL_STRING)
	const char* astrMenuPopupSeparatorStyle;				AST(NAME("MenuPopupSeparatorStyle:") POOL_STRING)
	REF_TO(UIStyleFont) hMenuPopupFont;						AST(NAME("MenuPopupFont:"))
	REF_TO(UIStyleFont) hMenuPopupFontHighlight;			AST(NAME("MenuPopupFont(Highlight):"))
	REF_TO(UIStyleFont) hMenuPopupFontDisabled;				AST(NAME("MenuPopupFont(Disabled):"))
	const char* astrMenuItemHasSubmenuTexture;				AST(RESOURCEDICT(Texture) NAME("MenuItemHasSubmenuTexture:") POOL_STRING)
	int iMenuPopupOffsetY;									AST(NAME("MenuPopupOffsetY:"))

	// UITextEntry
	const char* astrTextEntryStyle;							AST(NAME("TextEntryStyle:"))
	const char* astrTextEntryStyleHighlight;				AST(NAME("TextEntryStyle(Highlight):"))
	const char* astrTextEntryStyleDisabled;					AST(NAME("TextEntryStyle(Disabled):"))
	const char* astrTextEntryStyleSelectedText;				AST(NAME("TextEntryStyle(SelectedText):"))
	REF_TO(UIStyleFont) hTextEntryTextFont;					AST(NAME("TextEntryTextFont:"))
	REF_TO(UIStyleFont) hTextEntryHighlightTextFont;		AST(NAME("TextEntryHighlightTextFont:"))
	REF_TO(UIStyleFont) hTextEntryDefaultTextFont;			AST(NAME("TextEntryDefaultTextFont:"))
	REF_TO(UIStyleFont) hTextEntryDisabledFont;				AST(NAME("TextEntryDisabledFont:"))

	// UIComboBox
	const char* astrComboBoxStyle;							AST(NAME("ComboBoxStyle:"))
	const char* astrComboBoxStyleHighlight;					AST(NAME("ComboBoxStyle(Highlight):"))
	const char* astrComboBoxStyleFocused;					AST(NAME("ComboBoxStyle(Focused):"))
	const char* astrComboBoxStyleOpened;					AST(NAME("ComboBoxStyle(Opened):"))
	const char* astrComboBoxStyleDisabled;					AST(NAME("ComboBoxStyle(Disabled):"))
	const char* astrComboBoxListStyle;						AST(NAME("ComboBoxListStyle:"))
	const char* astrComboBoxListStyleSelectedItem;			AST(NAME("ComboBoxListStyle(SelectedItem):"))
	REF_TO(UIStyleFont) hComboBoxFont;						AST(NAME("ComboBoxFont:"))
	REF_TO(UIStyleFont) hComboBoxFontHighlight;				AST(NAME("ComboBoxFont(Highlight):"))
	REF_TO(UIStyleFont) hComboBoxFontFocused;				AST(NAME("ComboBoxFont(Focused):"))
	REF_TO(UIStyleFont) hComboBoxFontOpened;				AST(NAME("ComboBoxFont(Opened):"))
	REF_TO(UIStyleFont) hComboBoxFontDisabled;				AST(NAME("ComboBoxFont(Disabled):"))
	int iComboBoxPopupBottomOffsetY;						AST(NAME("ComboBoxPopupBottomOffsetY:"))
	int iComboBoxPopupTopOffsetY;							AST(NAME("ComboBoxPopupTopOffsetY:"))

	// UIList
	const char* astrListHeaderStyle;						AST(NAME("ListHeaderStyle:"))
	const char* astrListInsideStyle;						AST(NAME("ListInsideStyle:") NAME("ListStyle:"))
	const char* astrListOutsideStyle;						AST(NAME("ListOutsideStyle:"))
	const char* astrListStyleSelectedItem;					AST(NAME("ListStyle(SelectedItem):"))
	int iListContentExtraPaddingRight;						AST(NAME("ListContentExtraPaddingRight"))
	REF_TO(UIStyleFont) hListHeaderFont;					AST(NAME("ListHeaderFont:"))
	REF_TO(UIStyleFont) hListItemFont;						AST(NAME("ListItemFont:"))
	REF_TO(UIStyleFont) hListItemFontSelectedItem;			AST(NAME("ListItemFont(SelectedItem):"))
	REF_TO(UIStyleFont) hListDefaultFont;					AST(NAME("ListDefaultFont:"))

	// UITabs
	const char* astrTabGroupStyle;							AST(NAME("TabGroupStyle:"))
	const char* astrTabGroupStyleSelected;					AST(NAME("TabGroupStyle(Selected):"))
	const char* astrTabGroupVerticalStyle;					AST(NAME("VerticalTabGroupStyle:"))
	const char* astrTabGroupVerticalStyleSelected;			AST(NAME("VerticalTabGroupStyle(Selected):"))
	REF_TO(UIStyleFont) hTabGroupFont;						AST(NAME("TabGroupFont:"))
	REF_TO(UIStyleFont) hTabGroupFontSelected;				AST(NAME("TabGroupFont(Selected):"))

	// UIScrollbar
	const char *pchScrollArrowUp;							AST(RESOURCEDICT(Texture) NAME("ScrollbarArrowUp:") POOL_STRING)
	const char *pchScrollArrowUpHighlight;					AST(RESOURCEDICT(Texture) NAME("ScrollbarArrowUp(Highlight):") POOL_STRING)
	const char *pchScrollArrowUpPressed;					AST(RESOURCEDICT(Texture) NAME("ScrollbarArrowUp(Pressed):") POOL_STRING)
	const char *pchScrollArrowUpDisabled;					AST(RESOURCEDICT(Texture) NAME("ScrollbarArrowUp(Disabled):") POOL_STRING)
	const char *pchScrollArrowDown;							AST(RESOURCEDICT(Texture) NAME("ScrollbarArrowDown:") POOL_STRING)
	const char *pchScrollArrowDownHighlight;				AST(RESOURCEDICT(Texture) NAME("ScrollbarArrowDown(Highlight):") POOL_STRING)
	const char *pchScrollArrowDownPressed;					AST(RESOURCEDICT(Texture) NAME("ScrollbarArrowDown(Pressed):") POOL_STRING)
	const char *pchScrollArrowDownDisabled;					AST(RESOURCEDICT(Texture) NAME("ScrollbarArrowDown(Disabled):") POOL_STRING)
	const char *pchScrollArrowLeft;							AST(RESOURCEDICT(Texture) NAME("ScrollbarArrowLeft:") POOL_STRING)
	const char *pchScrollArrowLeftHighlight;				AST(RESOURCEDICT(Texture) NAME("ScrollbarArrowLeft(Highlight):") POOL_STRING)
	const char *pchScrollArrowLeftPressed;					AST(RESOURCEDICT(Texture) NAME("ScrollbarArrowLeft(Pressed):") POOL_STRING)
	const char *pchScrollArrowLeftDisabled;					AST(RESOURCEDICT(Texture) NAME("ScrollbarArrowLeft(Disabled):") POOL_STRING)
	const char *pchScrollArrowRight;						AST(RESOURCEDICT(Texture) NAME("ScrollbarArrowRight:") POOL_STRING)
	const char *pchScrollArrowRightHighlight;				AST(RESOURCEDICT(Texture) NAME("ScrollbarArrowRight(Highlight):") POOL_STRING)
	const char *pchScrollArrowRightPressed;					AST(RESOURCEDICT(Texture) NAME("ScrollbarArrowRight(Pressed):") POOL_STRING)
	const char *pchScrollArrowRightDisabled;				AST(RESOURCEDICT(Texture) NAME("ScrollbarArrowRight(Disabled):") POOL_STRING)
	const char* astrScrollVTrough;							AST(NAME("ScrollbarVTroughStyle:") POOL_STRING)
	const char* astrScrollVTroughDisabled;					AST(NAME("ScrollbarVTroughStyle(Disabled):") POOL_STRING)
	const char* astrScrollVHandle;							AST(NAME("ScrollbarVHandleStyle:") POOL_STRING)
	const char* astrScrollVHandleHighlight;					AST(NAME("ScrollbarVHandleStyle(Highlight):") POOL_STRING)
	const char* astrScrollVHandlePressed;					AST(NAME("ScrollbarVHandleStyle(Pressed):") POOL_STRING)
	const char* astrScrollVHandleDisabled;					AST(NAME("ScrollbarVHandleStyle(Disabled):") POOL_STRING)
	const char* astrScrollHTrough;							AST(NAME("ScrollbarHTroughStyle:") POOL_STRING)
	const char* astrScrollHTroughDisabled;					AST(NAME("ScrollbarHTroughStyle(Disabled):") POOL_STRING)
	const char* astrScrollHHandle;							AST(NAME("ScrollbarHHandleStyle:") POOL_STRING)
	const char* astrScrollHHandleHighlight;					AST(NAME("ScrollbarHHandleStyle(Highlight):") POOL_STRING)
	const char* astrScrollHHandlePressed;					AST(NAME("ScrollbarHHandleStyle(Pressed):") POOL_STRING)
	const char* astrScrollHHandleDisabled;					AST(NAME("ScrollbarHHandleStyle(Disabled):") POOL_STRING)
	const char* astrScrollZoomMin;							AST(NAME("ScrollbarZoomMin:") POOL_STRING)
	const char* astrScrollZoomMax;							AST(NAME("ScrollbarZoomMax:") POOL_STRING)
	const char* astrScrollareaDragCursor;					AST(NAME("ScrollareaDragCursor:") POOL_STRING)

	// UISpinner
	const char *pchSpinnerArrowUp;							AST(RESOURCEDICT(Texture) NAME("SpinnerArrowUp:") POOL_STRING)
	const char *pchSpinnerArrowDown;						AST(RESOURCEDICT(Texture) NAME("SpinnerArrowDown:") POOL_STRING)

	// UITree
	const char* astrTreeStyle;								AST(NAME("TreeStyle:") POOL_STRING)
	const char* astrTreeStyleSelectedItem;					AST(NAME("TreeStyle(SelectedItem):") POOL_STRING)
	REF_TO(UIStyleFont) hTreeItemFont;						AST(NAME("TreeItemFont:"))
	REF_TO(UIStyleFont) hTreeItemFontSelectedItem;			AST(NAME("TreeItemFont(SelectedItem):"))
	REF_TO(UIStyleFont) hTreeDefaultFont;					AST(NAME("TreeDefaultFont:"))
	Color cTreeLine;										AST(RGBA, NAME("TreeLineColor:"))
	Color cTreeLineDrag;									AST(RGBA, NAME("TreeLineColor(Dragging):"))

	// UITreechart
	const char* astrTreechartBGStyle;						AST(NAME("TreechartBGStyle:") POOL_STRING)
	const char* astrTreechartPlaceholderStyle;				AST(NAME("TreechartPlaceholderStyle:") POOL_STRING)
	const char* astrTreechartGroupContentBGStyle;			AST(NAME("TreechartGroupContentBGStyle:") POOL_STRING)
	const char* astrTreechartGroupBottomStyle;				AST(NAME("TreechartGroupBottomStyle:") POOL_STRING)
	const char* astrTreechartFullWidthContentBGStyle;		AST(NAME("TreechartFullWidthContentBGStyle:") POOL_STRING)
	const char* astrTreechartNodeHighlightStyle;			AST(NAME("TreechartNodeHighlightStyle:") POOL_STRING)
	const char* astrTreechartNodeSelectedStyle;				AST(NAME("TreechartNodeSelectedStyle:") POOL_STRING)
	const char* astrTreechartArrowStyle;					AST(NAME("TreechartArrowStyle:") POOL_STRING)
	const char* astrTreechartArrowHighlightStyle;			AST(NAME("TreechartArrowHighlightStyle:") POOL_STRING)
	const char* astrTreechartArrowHighlightStyleActive;		AST(NAME("TreechartArrowHighlightStyle(Active):") POOL_STRING)
	const char* astrTreechartArrowStyleAlternate;			AST(NAME("TreechartArrowStyle(Alternate):") POOL_STRING)
	const char* astrTreechartArrowStyleGrabbable;			AST(NAME("TreechartArrowStyle(Grabbable):") POOL_STRING)
	const char* astrTreechartArrowStyleNoHead;				AST(NAME("TreechartArrowStyle(NoHead):") POOL_STRING)
	const char* astrTreechartBranchArrowStyle;				AST(NAME("TreechartBranchArrowStyle:") POOL_STRING)
	bool bTreechartInsertionPlusIsCentered;					AST(NAME("TreechartInsertionPlusIsCentered:"))
	const char *pchTreechartInsertionPlus;					AST(RESOURCEDICT(Texture) NAME("TreechartInsertionPlus:") POOL_STRING)
	const char *pchTreechartInsertionPlusHover;				AST(RESOURCEDICT(Texture) NAME("TreechartInsertionPlusHover:") POOL_STRING)
	const char *pchTreechartInsertionPlusPressed;			AST(RESOURCEDICT(Texture) NAME("TreechartInsertionPlusPressed:") POOL_STRING)
	int iTreechartArrowHeight;								AST(NAME("TreechartArrowHeight:"))
	const char *astrTreechartPredragCursor;					AST(NAME("TreechartPredragCursor:") POOL_STRING)
	const char *astrTreechartDragCursor;					AST(NAME("TreechartDragCursor:") POOL_STRING)
	const char* astrTreechartTrash;							AST(RESOURCEDICT(Texture) NAME("TreechartTrash:") POOL_STRING)
	const char* astrTreechartTrashHighlight;				AST(RESOURCEDICT(Texture) NAME("TreechartTrash(Highlight):") POOL_STRING)
	bool bTreechartArrowHighlightOnTop;						AST(NAME("TreechartArrowHighlightOnTop:"))

	// UIAccordion 
	const char* astrAccordionButtonStyle;					AST(NAME("AccordionButtonStyle:"))
	const char* astrAccordionButtonStyleHighlight;			AST(NAME("AccordionButtonStyle(Highlight):"))
	const char* astrAccordionButtonStyleFocused;			AST(NAME("AccordionButtonStyle(Focused):"))
	const char* astrAccordionButtonStylePressed;			AST(NAME("AccordionButtonStyle(Pressed):"))
	const char* astrAccordionButtonStyleDisabled;			AST(NAME("AccordionButtonStyle(Disabled):"))
	REF_TO(UIStyleFont) hAccordionButtonFont;				AST(NAME("AccordionButtonFont:"))
	REF_TO(UIStyleFont) hAccordionButtonFontHighlight;		AST(NAME("AccordionButtonFont(Highlight):"))
	REF_TO(UIStyleFont) hAccordionButtonFontFocused;		AST(NAME("AccordionButtonFont(Focused):"))
	REF_TO(UIStyleFont) hAccordionButtonFontPressed;		AST(NAME("AccordionButtonFont(Pressed):"))
	REF_TO(UIStyleFont) hAccordionButtonFontDisabled;		AST(NAME("AccordionButtonFont(Disabled):"))

	// UIChat
	const char* astrChatLogStyle;							AST(NAME("ChatLogStyle:"))
	REF_TO(UIStyleFont) hChatLogFont;						AST(NAME("ChatLogFont:"))



	const char *pchDefaultCursor;							AST(NAME("DefaultCursor:") POOL_STRING)
	const char *pchTextCursor;								AST(NAME("TextCursor:") POOL_STRING)
	const char *pchDragDirCursors[UI_DRAG_DIR_CURSOR_SKIN_CNT];AST(RESOURCEDICT(UICursor) POOL_STRING INDEX(0, "DragCursorBottomLeft:") INDEX(1, "DragCursorBottomRight:") INDEX(2, "DragCursorTopLeft:") INDEX(3, "DragCursorTopRight:") INDEX(4, "DragCursorBottom:") INDEX(5, "DragCursorLeft:") INDEX(6, "DragCursorRight:") INDEX(7, "DragCursorTop:") INDEX(8, "DragCursorAnyDirection:"))

	// UIPaneTree
	const char* astrPaneTreePaneStyle;						AST(NAME("PaneTreePane:"))

	// UISeparator
	const char* astrHorizontalSeparator;					AST(NAME("HorizontalSeparator:"))
	const char* astrVerticalSeparator;						AST(NAME("VerticalSeparator:"))

	// UISlider
	const char *astrSliderHandle;							AST(RESOURCEDICT(Texture) NAME("SliderHandle:") POOL_STRING)
	const char *astrSliderHandleHighlight;					AST(RESOURCEDICT(Texture) NAME("SliderHandle(Highlight):") POOL_STRING)
	const char *astrSliderHandlePressed;					AST(RESOURCEDICT(Texture) NAME("SliderHandle(Pressed):") POOL_STRING)
	const char *astrSliderHandleDisabled;					AST(RESOURCEDICT(Texture) NAME("SliderHandle(Disabled):") POOL_STRING)
	const char *astrSliderTrough;							AST(NAME("SliderTrough:") POOL_STRING)
	const char *astrSliderTroughDisabled;					AST(NAME("SliderTrough(Disabled):") POOL_STRING)

	// UIProgressBar
	const char* astrProgressBarTrough;						AST(NAME("ProgressBarTrough:") POOL_STRING)
	const char* astrProgressBarFilled;						AST(NAME("ProgressBarFilled:") POOL_STRING)
	int iProgressBarHeight;									AST(NAME("ProgressBarHeight:") DEFAULT(16))

	// UIMinimap
	const char* astrMinimapSelectedStyle;					AST(NAME("MinimapSelectedStyle:") POOL_STRING)
	float fMinimapIconScale;								AST(NAME("MinimapIconScale:") DEFAULT(1))
	
	// Other Texture Overrides (Not all of these work if you are setting the skin directly on the widget)
	const char* astrTooltipStyle;							AST(NAME("TooltipStyle:") POOL_STRING)
	const char *pchArrowDropDown;							AST(RESOURCEDICT(Texture) NAME("ArrowDropDown:") POOL_STRING)
	const char *pchMinus;									AST(RESOURCEDICT(Texture) NAME("Minus:") POOL_STRING)
	const char *pchPlus;									AST(RESOURCEDICT(Texture) NAME("Plus:") POOL_STRING)
	const char *pchCheckBoxChecked;							AST(RESOURCEDICT(Texture) NAME("CheckBoxChecked:") POOL_STRING)
	const char *pchCheckBoxUnchecked;						AST(RESOURCEDICT(Texture) NAME("CheckBoxUnchecked:") POOL_STRING)
	const char *pchCheckBoxHighlight;						AST(RESOURCEDICT(Texture) NAME("CheckBoxHighlight:") POOL_STRING)
	const char *pchRadioBoxChecked;							AST(RESOURCEDICT(Texture) NAME("RadioBoxChecked:") POOL_STRING)
	const char *pchRadioBoxUnchecked;						AST(RESOURCEDICT(Texture) NAME("RadioBoxUnchecked:") POOL_STRING)
	const char *pchRadioBoxHighlight;						AST(RESOURCEDICT(Texture) NAME("RadioBoxHighlight:") POOL_STRING)
} UISkin;
extern ParseTable parse_UISkin[];
#define TYPE_parse_UISkin UISkin

// There is a global widget family state that decides which widgets get drawn.
// If a widget belongs to any active family, it is drawn. Only 8 families
// are supported at the moment.
#define UI_FAMILY_ALL			0xFF
#define UI_FAMILY_GAME			(1 << 0)
#define UI_FAMILY_EDITOR		(1 << 1)
#define UI_FAMILY_CUTSCENE		(1 << 2)
#define UI_FAMILY_ALWAYS_SHOW	(1 << 7)

#define UI_FAMILY_ALL_BUT(kind) (UI_FAMILY_ALL ^ (kind))


// Game Family Hiders.
//   These are systems which may want the FAMILY_GAME UI hidden. We do not show FAMILY_GAME unless they are all zero.
#define UI_GAME_HIDER_CUTSCENE	(1 << 0)
#define UI_GAME_HIDER_VIDEO		(1 << 1)		// Not escapable
#define UI_GAME_HIDER_EDITOR	(1 << 2)		// Not escapable
#define UI_GAME_HIDER_UGC		(1 << 3)
#define UI_GAME_HIDER_ENTDEBUG	(1 << 4)		// Not escapable
#define UI_GAME_HIDER_CMD		(1 << 5)		// ShowGameUI auto command.
#define UI_GAME_HIDER_CMD_NOESC	(1 << 6)		// ShowGameUINoExtraKeyBinds auto command. Not escapable

//////////////////////////////////////////////////////////////////////////
// This is available as the variable g_ui_State.
typedef struct UIGlobalState
{
	F32 timestep;
		// The time since the last frame.
	U32 totalTimeInMs;
		// Time elapsed so far across all frames.

	// Active rendering device
	RdrDevice *device;

	// Current mouse location
	int mouseX, mouseY;
    DynNode* mouseDynNode;

	// Current screen size
	int screenWidth, screenHeight;

	// Viewport size
	IVec2 viewportMin, viewportMax;

	// If set, screenWidth and screenHeight will never end up less
	// than this, instead the UI will scale
	int minScreenWidth, minScreenHeight;

	// Currently focused widget
	UIWidget *focused;

	// Default font.
	REF_TO(UIStyleFont) font;

	// Global default skin
	REF_TO(UISkin) hActiveSkin;
	UISkin* pActiveSkin;

	// Programatically generated skin -- first skin in the value of g_ui_State.skin
	UISkin default_skin;

	// skin is set (for cmdline support)
	char *skinCmdlineFilename;

	// Tracks the Z order so far this frame. If you don't use UI_GET_COORDINATES
	// you should increment this by one and use it to get the Z for your widget.
	U32 drawZ;

	// Global UI scale.
	F32 scale;

	// Widgets that need to be freed at the end of the main loop.
	UIWidgetGroup freeQueue;

	// Map RdrDevices to top-level UIDeviceStates.
	StashTable states;


	UIVideoMode mode;

	UIProcessKeyboardInput cbKeyInput;

	// Do not set family directly. Use ui_SetActiveFamilies, ui_AddActiveFamilies, or ui_RemoveActiveFamilies.
	U8 family;
	U8 familyChanged;
	U8 prev_family;
	U8 prev_familyChanged;

	U32 uGameUIHiderFlags;	// Various systems can want the Game UI hidden. This U32 must be zero for the Game family to be turned on.
							// UIGameHide and UIGameShow control this. The UI_GAME_HIDER flags are what it contains.

	// If true, nothing else may set the cursor for this frame.
	bool cursorLock;
	bool modal;

	bool bSafeToFree;

	// Is the game currently in editor mode.
	bool bInEditor;
	bool bInUGCEditor;

	// If we want the UI to run the early ZOcclusion (reset when it's started and generally each frame)
	bool bEarlyZOcclusion;

	// Counter of frames until we stop running input events;
	// reset it when new events occur. It needs to be a counter
	// rather than a boolean due to SpriteCaching and input processing
	// that occurs across multiple frames.
	U8 chInputDidAnything;

	UIPlayAudioByName cbPlayAudio;
	UIPlayAudioByName cbValidateAudio;

	UIExternLoopFunc cbOncePerFrameBeforeMainDraw;

	UIPostCameraUpdateFunc cbPostCameraUpdateFunc;

	U32 uiFrameCount;

	// Tracks the last time we loaded a UIStyleFont. This is used by
	// smf_ParseAndFormat as part of its CRC calculation, so SMF
	// layouts out again when fonts reload.
	U32 uiLastFontLoad;

	// If set, this is the screen rect of the widget currently being
	// processed.  This is useful for placing things in callbacks.
	//
	// NOTE: not all widgets support this, so support it being null!
	CBox* widgetBox;

	// If set, windows buttons will not be drawn, even in fullscreen /
	// windowed maximized mode.
	bool forceHideWindowButtons;

	bool graphicsUINeedsUpdate;
} UIGlobalState;

extern UIGlobalState g_ui_State;

#define UI_GET_SKIN(w) ui_WidgetGetSkin(UI_WIDGET(w))

//////////////////////////////////////////////////////////////////////////
// The "base class" for all widgets. The first element of any UI widget
// is a UIWidget structure (not a pointer to one). This contains size and
// position information and function pointers necessary for the main loop
// and memory management.
//
// Despite the AUTO_STRUCT, Widgets cannot be directly read in via
// Text Parser. Instead, you need to use the functions in UISerialize.h
// to apply a fake widget tree to a real widget tree. You should be
// able to write out real widget trees, though.
AST_PREFIX(WIKI(AUTO));
AUTO_STRUCT WIKI("Widget Layout");
typedef struct UIWidget
{
	//////////////////////////////////////////////////////////////////////////
	// This widget's name. Don't use names starting with "ui", which is
	// used internally by the UI library.
	const char *name; AST(POOL_STRING)

	//A related datum specified in code
	UserData userinfo; NO_AST

	//////////////////////////////////////////////////////////////////////////
	// How widgets are positioned:

	// Widgets are positioned relative to some direction from their
	// parent. By default this is the topleft. It can be Top, Bottom,
	// Left, Right, TopLeft, TopRight, BottomLeft, or BottomRight.
	UIDirection offsetFrom;

	// The absolute horizontal offset from the widget's parent. This
	// matches up child and parent parts. For example, if offsetFrom is
	// UIBottomLeft and x = 3, then the bottom-left of the child is 3
	// pixels from the bottom-left of the parent.
	F32 x;

	// The absolute vertical offset from the widget's parent.
	F32	y;

	// xPOffset and yPOffset work in the same way, but are a percentage
	// of the parent's size. e.g. if xPOffset is 0.5, the child is aligned
	// to the middle of the parent.

	// A relative horizontal offset from the parent. This is a decimal
	// representing a percentage. For example, if this is 0.3, the
	// widget is offset 30% of its parents width. This is not scaled.
	F32 xPOffset;

	// A relative vertical offset from the parent. This is not scaled.
	F32 yPOffset;

	// The widget's width, either in pixels or a fraction of its parent's width.
	F32 width;

	// The widget's height, either in pixels or a fraction of its parent's height.
	F32 height;

	// The unit 'width' is in. Either 'Fixed' for pixels or 'Percentage'
	// to base it off its parents height. Percentages are given using
	// decimals, e.g. 0.3 is 30%.
	UIUnitType widthUnit;

	// The unit 'height' is in.
	UIUnitType heightUnit;

	// leftPad, rightPad, topPad, and bottomPad work similarly. They are
	// counted as part of the widget's width and height for purposes if
	// positioning in the parent, but are not part of the width and height
	// for purposes of drawing the widget.

	// Padding is included in the size of the widget for purposes of
	// positioning, but are not part of its width and height. For example,
	// if a widget has a height of 50% and a topPad of 15, and its parent is
	// 100 pixels high, then the widget is 0.5 * 100 - 15 = 35 pixels high.
	// If the widget is using a fixed size, padding is mostly confusing,
	// so don't use it. Just adjust the x/y/width/height.
	F32 topPad;
	
	// Padding on the bottom of a widget.
	F32 bottomPad;

	// Padding on the left of a widget.
	F32 leftPad;

	// Padding on the right of a widget.
	F32 rightPad;

	// A scaling factor to apply to the widget. If you scale a widget
	// with a percentage-based position, width, or height, it does not
	// apply to those parts of the widget.
	F32 scale;

	// Bringing it all together, here's an example of a widget that is
	// 1/3 of the width of its parent, 50 pixels tall, is positioned
	// halfway across its parent, and has 4 pixels of padding on each
	// side:
	//   UIWidget widget;
	//   ui_SetPositionEx(&widget, 0, 0, 0.5, 0, UITopLeft);
	//   ui_SetDimensionsEx(&widget, 0.3, 50, UIUnitPercentage, UIUnitFixed);
	//   ui_WidgetSetPadding(&widget, 4, 4);

	// An alternative to positioning/sizing that is more like traditional GUI toolkits.
	//
	// Set a Widget's Sizer using ui_WidgetSetSizer(widget, sizer). Once set, this
	// Widget will own the Sizer and destroy it when the Widget itself is destroyed
	// with ui_WidgetFreeInternal.
	//
	// See the documentation for UISizer for a detailed description of Sizers.
	UISizer *pSizer; NO_AST

    // A DynNode that is always at the top-left edge of a widget in
    // screen space.  It is automatically updated by the widget.
    DynNode* dynNode; NO_AST

	U8 family; NO_AST

	// Widget priority determines if it can steal priority or be stolen from
	U8 priority; NO_AST

	UIWidgetModifier state; NO_AST

	// Whether or not the children are inactive.  Used for focus control,
	// need only be set by widgets which disable their children.
	bool childrenInactive : 1; NO_AST

	//////////////////////////////////////////////////////////////////////////
	// Overriding these "subclasses" UIWidget into a useful widget.

	// Called once per frame to handle mouse input and update internal state.
	// A widget can do almost anything it wants in the tick function safely,
	// except free things. It should use ui_WidgetQueueFree.
	UILoopFunction tickF; NO_AST

	// Called once during tick the first frame the widget family is added to
	// the screen. This may be used to perform additional handling when added
	// through the family bits. Like tickF, a widget can do almost anything it
	// wants safely, except free things.
	UILoopFunction familyAddedF; NO_AST

	// Called once during tick the first frame the widget family is removed
	// from the screen. This may be used to perform additional handling when
	// removed through the family bits. Like tickF, a widget can do almost
	// anything it wants safely, except free things.
	UILoopFunction familyRemovedF; NO_AST

	// Called once per frame to draw the widget on the screen. Widgets cannot
	// move themselves around, remove or add widgets, or do much aside from
	// drawing in this function. It is also likely that input is already
	// marked "handled" for this frame, so e.g. point_cbox_clsn should be
	// used instead of mouseCollision.
	UILoopFunction drawF; NO_AST

	// Overlay the widget with an alpha of this intensity (0-1). Currently
	// only supported in expanders to highlight areas.
	F32 highlightPercent; NO_AST

	// Called to free the widget. The widget *must* have a free function,
	// even if it doesn't do anything. This is for internal UI use. For
	// user callbacks, use onFreeF.
	UIFreeFunction freeF; NO_AST

	// Called when the widget is freed, to free user data if necessary.
	UIFreeFunction onFreeF; NO_AST

	// Called to receive keyboard input. If a widget intends to call
	// ui_SetFocus on itself, it should have a focus function.
	UIInputFunction inputF; NO_AST

	// Called when the widget receives focus. This is for internal UI
	// use. For user callbacks, use onFocusF.
	// This existence of this also signals to the UI that this widget *can*
	// receive focus.
	UIFocusFunction focusF; NO_AST

	// Called when focus is set to the widget. This is intended for user
	// functions.
	UIActivationFunc onFocusF; NO_AST
	UserData onFocusData; NO_AST

	// Called when the widget loses focus. FIXME: Type is poorly named.
	// This can be called when the widget is freed, and so not all data may be available.
	UIFocusFunction unfocusF; NO_AST

	UIActivationFunc onUnfocusF; NO_AST
	UserData onUnfocusData; NO_AST

	// Drag-and-drop management.
	UIActivationFunc preDragF; NO_AST
	UserData preDragData; NO_AST
	UIActivationFunc dragF; NO_AST
	UserData dragData; NO_AST
	UIDragFunction dropF; NO_AST
	UserData dropData; NO_AST
	UIDragFunction acceptF; NO_AST
	UserData acceptData; NO_AST

	// Some generics things. Not all widgets support these. If you need a new
	// widget to support them, talk to Alex or Stephen.
	UIActivationFunc mouseOverF; NO_AST
	UserData mouseOverData; NO_AST
	UIActivationFunc mouseLeaveF; NO_AST
	UserData mouseLeaveData; NO_AST

	// Called when a widget is right-clicked.
	UIActivationFunc contextF; NO_AST
	UserData contextData; NO_AST

	//////////////////////////////////////////////////////////////////////////
	// Some widgets have scrollbars; these are attached in a generic way
	// so that skinning, etc. works properly on them.
	UIScrollbar *sb; NO_AST

	//////////////////////////////////////////////////////////////////////////
	// The widget's appearance is controlled via a skin. If a skin is not
	// present, the widget has some internal colors it can use (but they're
	// not as flexible as skins).
	REF_TO(UISkin) hOverrideSkin; NO_AST
	UISkin *pOverrideSkin; NO_AST
	Color color[4]; NO_AST
	REF_TO(UIStyleFont) hOverrideFont; AST(NAME(Font))

	//////////////////////////////////////////////////////////////////////////
	// Many widgets need to store a text string for user display.

	char *text_USEACCESSOR; AST(ESTRING)
	REF_TO(Message) message_USEACCESSOR; NO_AST
	
	U64 u64; NO_AST

	char *tooltip_USEACECSSOR; NO_AST
	REF_TO(Message) tooltipMessage_USEACCESSOR; NO_AST

	//////////////////////////////////////////////////////////////////////////
	// This widget's parent group if any.
	UIWidgetGroup *group; NO_AST

	// Children, if any.
	UIWidgetGroup children; NO_AST

	struct KeyBindProfile *keybinds; NO_AST

	//////////////////////////////////////////////////////////////////////////
	// Used for serialization/synchronization, should not be set on widgets
	// created via code.

	const char *filename; AST(CURRENTFILE)
	U32 bf[1]; AST(USEDFIELD)

	U32 uClickThrough : 1;
	U32 uFocusWhenDisabled : 1;
	U32 uUIGenWidget : 1; // If the widget is actually a UIGen
	U32 bConsumesEscape : 1;

	// Set this to prevent the widget from scrolling on X.
	U32 bNoScrollX : 1;
	// Set this to prevent the widget from scrolling on Y.
	U32 bNoScrollY : 1;
	// Set this to prevent the widget from inheriting childScale  (global scale still applies)
	U32 bNoScrollScale : 1;

	//////////////////////////////////////////////////////////////////////////
	// File and line number of creation (for debugging)
	const char *create_filename;
	int create_line;

} UIWidget;

// Utility struct for casting a widget to something UI_WIDGETable
typedef struct UIWidgetWidget
{
	UIWidget widget;
} UIWidgetWidget;

typedef struct UIDeviceState
{
	UIWidgetGroup topgroup;			// can go anywhere in the window, ie for menus
	UIWindowGroup windowgroup;		// only holds UIWindow, can go anywhere in the window.  MUST ONLY BE ADDED TO BY UI_WINDOWSHOW!
	UIWidgetGroup panegroup;		// restricts viewport
	UIWidgetGroup maingroup;		// restricted to viewport

	// Current cursor settings.
	UICursorState cursor;

	// See UIDnD.h for information on this.
	UIDnDPayload *dnd;

} UIDeviceState;

//////////////////////////////////////////////////////////////////////
// Encapulates all the different ways a widget can be drawn
// (UIStyleBoder, UITextureAssembly, UIStyleBar, or Texture, at the moment)
typedef struct UIDrawingDescription
{
	const char* textureAssemblyName;
	const char* textureAssemblyNameUsingLegacyColor;
	const char* styleBorderName;
	const char* styleBorderNameUsingLegacyColor;
	const char* styleBarName;
	const char* textureNameUsingLegacyColor;
	bool horzLineUsingLegacyColor;
	bool vertLineUsingLegacyColor;

	// Legacy color 2 flags -- for overlay
	bool overlayOutlineUsingLegacyColor2;
} UIDrawingDescription;

// Draw based on this description
void ui_DrawingDescriptionDraw( const UIDrawingDescription* drawDesc, const CBox* box, float scale, float z, char alpha, Color legacyColor, Color legacyColor2 );

// Draw based on the description, rotated
void ui_DrawingDescriptionDrawRotated( const UIDrawingDescription* drawDesc, const CBox* box, float rot, float scale, float z, char alpha, Color legacyColor, Color legacyColor2 );

// Go from the outer box to the inner box, based on DRAW-DESC.
void ui_DrawingDescriptionInnerBox( const UIDrawingDescription* drawDesc, CBox* box, float scale );

// Like ui_DrawingDescriptionInnerBox, but takes X,Y,W,H instesad of a box.
void ui_DrawingDescriptionInnerBoxCoords( const UIDrawingDescription* drawDesc, float* x, float* y, float* w, float* h, float scale );

// Go from the inner box to the outer box, based on DRAW-DESC.
void ui_DrawingDescriptionOuterBox( const UIDrawingDescription* drawDesc, CBox* box, float scale );

// Like ui_DrawingDescriptionOuterBox, but takes X,Y,W,H instesad of a box.
void ui_DrawingDescriptionOuterBoxCoords( const UIDrawingDescription* drawDesc, float* x, float* y, float* w, float* h, float scale );

// Get the padding width of a drawing description.
F32 ui_DrawingDescriptionWidth( const UIDrawingDescription* drawDesc );

// Get the padding Height of a drawing description.
F32 ui_DrawingDescriptionHeight( const UIDrawingDescription* drawDesc );

// Get the left padding of a drawing description.
F32 ui_DrawingDescriptionLeftSize( const UIDrawingDescription* drawDesc );

// Get the right padding of a drawing description.
F32 ui_DrawingDescriptionRightSize( const UIDrawingDescription* drawDesc );

// Get the top padding of a drawing description.
F32 ui_DrawingDescriptionTopSize( const UIDrawingDescription* drawDesc );

// Get the bottom padding of a drawing description.
F32 ui_DrawingDescriptionBottomSize( const UIDrawingDescription* drawDesc );

//////////////////////////////////////////////////////////////////////////
// Widget constructors and accessors

// "Subclass" the widget with the given functions and initialize some defaults.
// Any function may be NULL except for freeF.
void ui_WidgetInitializeEx(SA_PARAM_NN_VALID UIWidget *widget, SA_PARAM_OP_VALID UILoopFunction tickF, SA_PARAM_OP_VALID UILoopFunction drawF,
						 SA_PARAM_NN_VALID UIFreeFunction freeF, SA_PARAM_OP_VALID UIInputFunction inputF, SA_PARAM_OP_VALID UIFocusFunction focusF
						 MEM_DBG_PARMS);
#define ui_WidgetInitialize(widget, tickF, drawF, freeF, inputF, focusF) ui_WidgetInitializeEx(widget, tickF, drawF, freeF, inputF, focusF MEM_DBG_PARMS_INIT)

#define ui_WidgetSetPosition(widget, x, y) ui_WidgetSetPositionEx(widget, x, y, 0.f, 0.f, UITopLeft)
void ui_WidgetSetPositionEx(SA_PARAM_NN_VALID UIWidget *widget, F32 x, F32 y, F32 xPOffset, F32 yPOffset, UIDirection offsetFrom);
F32 ui_WidgetGetX(SA_PARAM_NN_VALID UIWidget *widget);
F32 ui_WidgetGetY(SA_PARAM_NN_VALID UIWidget *widget);
//Naive calculation, assumes x,y and pad are not relative
F32 ui_WidgetGetNextY(SA_PARAM_NN_VALID UIWidget *widget);
F32 ui_WidgetGetNextX(SA_PARAM_NN_VALID UIWidget *widget);

#define ui_WidgetSetDimensions(widget, w, h) ui_WidgetSetDimensionsEx(widget, w, h, UIUnitFixed, UIUnitFixed)
void ui_WidgetSetDimensionsEx(SA_PARAM_NN_VALID UIWidget *widget, F32 w, F32 h, UIUnitType wUnit, UIUnitType hUnit);
#define ui_WidgetSetWidth(widget, w) ui_WidgetSetWidthEx(widget, w, UIUnitFixed)
#define ui_WidgetSetHeight(widget, h) ui_WidgetSetHeightEx(widget, h, UIUnitFixed)
void ui_WidgetSetWidthEx(SA_PARAM_NN_VALID UIWidget *widget, F32 w, UIUnitType wUnit);
void ui_WidgetSetHeightEx(SA_PARAM_NN_VALID UIWidget *widget, F32 h, UIUnitType hUnit);
F32 ui_WidgetGetWidth(SA_PARAM_NN_VALID UIWidget *widget);
F32 ui_WidgetGetHeight(SA_PARAM_NN_VALID UIWidget *widget);
const char* ui_WidgetGetName(UIAnyWidget *widget);

#define ui_WidgetSetPadding(widget, xpad, ypad) ui_WidgetSetPaddingEx(widget, xpad, xpad, ypad, ypad)
void ui_WidgetSetPaddingEx(SA_PARAM_NN_VALID UIWidget *widget, F32 left, F32 right, F32 top, F32 bottom);
F32 ui_WidgetGetPaddingTop(SA_PARAM_NN_VALID UIWidget *widget);
F32 ui_WidgetGetPaddingBottom(SA_PARAM_NN_VALID UIWidget *widget);

void ui_WidgetSetSizer(SA_PARAM_NN_VALID UIWidget *pWidget, SA_PARAM_OP_VALID UISizer *pSizer);
void ui_WidgetSizerLayout(SA_PARAM_NN_VALID UIWidget *pWidget, F32 width, F32 height);

void ui_WidgetSetPreDragCallback(SA_PARAM_NN_VALID UIWidget *widget, UIActivationFunc preDragF, UserData preDragData);
void ui_WidgetSetDragCallback(SA_PARAM_NN_VALID UIWidget *widget, UIActivationFunc dragF, UserData dragData);
void ui_WidgetSetDropCallback(SA_PARAM_NN_VALID UIWidget *widget, UIDragFunction dropF, UserData dropData);
void ui_WidgetSetAcceptCallback(SA_PARAM_NN_VALID UIWidget *widget, UIDragFunction acceptF, UserData acceptData);
void ui_WidgetSetFocusCallback(SA_PARAM_NN_VALID UIWidget *widget, UIActivationFunc onfocusF, UserData onfocusFData);
void ui_WidgetSetUnfocusCallback(SA_PARAM_NN_VALID UIWidget *widget, UIActivationFunc onUnfocusF, UserData onUnfocusFData);
void ui_WidgetSetFreeCallback(SA_PARAM_NN_VALID UIWidget *widget, UIFreeFunction onFreeF);
void ui_WidgetSetContextCallback(SA_PARAM_NN_VALID UIWidget *widget, UIActivationFunc contextF, UserData contextData);

void ui_WidgetSetMouseOverCallback(SA_PARAM_NN_VALID UIWidget *widget, UIActivationFunc mouseOverF, UserData mouseOverData);
void ui_WidgetSetMouseLeaveCallback(SA_PARAM_NN_VALID UIWidget *widget, UIActivationFunc mouseLeaveF, UserData mouseLeaveData);

// Calculate the absolute position/size given the parent's.
F32 ui_WidgetXPosition(SA_PARAM_NN_VALID UIWidget *widget, F32 parentX, F32 parentWidth, F32 pScale);
F32 ui_WidgetYPosition(SA_PARAM_NN_VALID UIWidget *widget, F32 parentY, F32 parentHeight, F32 pScale);
F32 ui_WidgetWidth(SA_PARAM_NN_VALID UIWidget *widget, F32 parentWidth, F32 pScale);
F32 ui_WidgetHeight(SA_PARAM_NN_VALID UIWidget *widget, F32 parentHeight, F32 pScale);
F32 ui_WidgetCalcHeight(SA_PARAM_NN_VALID UIWidget *widget);
F32 ui_WidgetCalcWidth(SA_PARAM_NN_VALID UIWidget *widget);

void ui_WidgetSetTextString(SA_PARAM_NN_VALID UIWidget *widget, SA_PARAM_OP_STR const char *text);
void ui_WidgetSetTextMessage(SA_PARAM_NN_VALID UIWidget *widget, SA_PARAM_OP_STR const char* messageKey );
const char *ui_WidgetGetText(SA_PARAM_NN_VALID UIWidget *widget);
void ui_WidgetSetTooltipString(SA_PARAM_NN_VALID UIWidget *widget, SA_PARAM_OP_STR const char *tooltip);
void ui_WidgetSetTooltipMessage(SA_PARAM_NN_VALID UIWidget *widget, SA_PARAM_OP_STR const char *tooltip);
const char *ui_WidgetGetTooltip(SA_PARAM_OP_VALID UIWidget *widget);
void ui_WidgetSetName(SA_PARAM_NN_VALID UIWidget *widget, SA_PARAM_OP_STR const char *name);

// Set the family of a widget.
void ui_WidgetSetFamily(SA_PARAM_NN_VALID UIWidget *widget, U8 family);

// Set click through state of a widget
void ui_WidgetSetClickThrough(SA_PARAM_NN_VALID UIWidget *widget, bool c);

///////////////////////////////////////////////////////////////////////////////////
//
//  UI show/hide control.
//  FAMILY_GAME is specially controlled by ui_GameUIShow and ui_GameUIHide

// Add these families (except GAME)
void ui_AddActiveFamilies(U8 family);

// Remove these families (except GAME)
void ui_RemoveActiveFamilies(U8 family);

void ui_GameUIShow(U32 uHider);
void ui_GameUIHide(U32 uHider);

///////////////////////////////////////////////////////////////////////////////////
	

// scrollF is called when the mouse wheel is scrolled over the widget. Only
// works on widgets with an attached scrollbar.
void ui_WidgetSetScrollCallback(SA_PARAM_NN_VALID UIWidget *widget, UIScrollFunc scrollF, UserData scrollData);

// If a widget is not active, it will not receive mouse or keyboard events.
// It will also probably draw differently.

// This sets the state of the widget and all its children. If children are
// added later, they may not match the activity state of their parent.
void ui_SetActive(SA_PARAM_OP_VALID UIWidget *widget, bool active);
// Useful for adjusting widgets that don't have UIWidget parents, e.g. tabs.
void ui_WidgetGroupSetActive(SA_PARAM_NN_VALID const UIWidgetGroup *group, bool active);

UIStyleFont *ui_WidgetGetFont(UIWidget *pWidget);
void ui_WidgetSetFont(UIWidget *pWidget, const char *pchFont);

// It's very possible we'll need/want to convert these to macros later.
bool ui_IsActive(SA_PARAM_NN_VALID UIWidget *widget);
bool ui_IsChanged(SA_PARAM_NN_VALID UIWidget *widget);
bool ui_IsInherited(SA_PARAM_NN_VALID UIWidget *widget);
bool ui_IsHovering(SA_PARAM_NN_VALID UIWidget *widget);
bool ui_IsVisible(SA_PARAM_NN_VALID UIWidget *widget);
void ui_SetChanged(SA_PARAM_NN_VALID UIWidget *pWidget, bool bChanged);
void ui_SetInherited(SA_PARAM_NN_VALID UIWidget *pWidget, bool bInherited);
void ui_SetHovering(SA_PARAM_NN_VALID UIWidget *pWidget, bool bHovering);

// Find a child widget. 
UIWidget *ui_WidgetFindChild(SA_PARAM_NN_VALID UIWidget *widget, SA_PARAM_NN_STR const char *childName);

// Return if toFind is somewhere in the widget tree starting at treeRoot.
bool ui_WidgetSearchTree(UIWidget* treeRoot, UIWidget* toFind);

// Free something at the end of this main loop. This is safe to use anywhere,
// even in a callback for the widget you're freeing.
void ui_WidgetForceQueueFree(UIWidget *widget);

// Queue a free if the UI main loop is running, destroy immediately otherwise.
// This isn't safe to use from widget callbacks.
void ui_WidgetQueueFree(SA_PARAM_OP_VALID UIWidget *widget);

#define ui_WidgetQueueFreeAndNull(ppWidget) { if (*(ppWidget)) { ui_WidgetQueueFree(UI_WIDGET(*(ppWidget))); *(ppWidget) = NULL; } }

// Set the focus to this widget. This also moves the widget to the front
// of its widget group, if it has one.

void ui_SetFocusEx(SA_PARAM_OP_VALID UIAnyWidget *anyWidget, const char* file, int line, const char* reason);
#define ui_SetFocus(widget) ui_SetFocusEx(widget, __FILE__, __LINE__, "#define call")
bool ui_IsFocused(SA_PARAM_OP_VALID UIAnyWidget *widget);
bool ui_IsFocusedOrChildren(SA_PARAM_OP_VALID UIAnyWidget *widget);
bool ui_IsFocusedWidgetGroup(SA_PARAM_NN_VALID UIWidgetGroup *group);

// Unfocus an entire widget and its children / a widget group. This also
// removes any blocking keybinds (which a SetFocus(NULL) does not do by
// itself).
void ui_WidgetGroupUnfocus(UIWidgetGroup *group);
void ui_WidgetUnfocusAll(UIWidget *widget);

//////////////////////////////////////////////////////////////////////////
// Widget groups are just an EArray of widgets, with a nicer name.

// Use these to add/remove widgets to/from groups. Don't push or remove
// the widgets directly, since some widgets need special handling.
void ui_WidgetGroupAdd(SA_PARAM_NN_VALID UIWidgetGroup *group, SA_PARAM_OP_VALID UIWidget *widget);
void ui_WidgetGroupMove(SA_PARAM_NN_VALID UIWidgetGroup *group, SA_PARAM_OP_VALID UIWidget *widget);
bool ui_WidgetGroupRemove(SA_PARAM_NN_VALID UIWidgetGroup *group, SA_PARAM_OP_VALID UIWidget *widget);
void ui_WidgetGroupSort(SA_PARAM_NN_VALID UIWidgetGroup *group);
void ui_WidgetAddChild(SA_PARAM_NN_VALID UIWidget *parent, SA_PARAM_OP_VALID UIWidget *child);
bool ui_WidgetRemoveChild(SA_PARAM_NN_VALID UIWidget *parent, SA_PARAM_OP_VALID UIWidget *child);

void ui_WidgetGroupGetDimensions(SA_PARAM_NN_VALID UIWidgetGroup *group, SA_PRE_OP_FREE SA_POST_OP_VALID F32 *width, SA_PRE_OP_FREE SA_POST_OP_VALID F32 *height, UI_PARENT_ARGS);

// Call the tick function of each the widgets, with the given parent values.
void ui_WidgetGroupTick(SA_PARAM_NN_VALID UIWidgetGroup *widgets, UI_PARENT_ARGS);

// Call the tick function of each the widgets, with different values for noscroll widgets
void ui_WidgetGroupTickEx(SA_PARAM_NN_VALID UIWidgetGroup *widgets, UI_PARENT_ARGS, F32 pXNoScroll, F32 pYNoScroll, F32 pWNoScroll, F32 pHNoScroll, F32 pScaleNoScroll);

// Call the draw function of each the widgets, with the given parent values.
void ui_WidgetGroupDraw(SA_PARAM_NN_VALID UIWidgetGroup *widgets, UI_PARENT_ARGS);

// Call the draw function of each the widgets, with different values for noscroll widgets
void ui_WidgetGroupDrawEx(SA_PARAM_NN_VALID UIWidgetGroup *widgets, UI_PARENT_ARGS, F32 pXNoScroll, F32 pYNoScroll, F32 pWNoScroll, F32 pHNoScroll, F32 pScaleNoScroll);

// Queue the free calls for each widget.
void ui_WidgetGroupQueueFree(SA_PRE_NN_NN_VALID UIWidgetGroup *widgets);

// Queue free calls, and remove the widgets
void ui_WidgetGroupQueueFreeAndRemove(SA_PRE_NN_NN_VALID UIWidgetGroup *widgets);

// Take event priority within this group (if in it). You probably
// don't need to call this yourself; it's called when a widget is
// focused.
void ui_WidgetGroupSteal(SA_PARAM_NN_VALID UIWidgetGroup *group, SA_PARAM_OP_VALID UIWidget *widget);

// Remove the widget from the group it's in.
void ui_WidgetRemoveFromGroup(UIWidget *widget);

// Manage per-device top-level widget groups. If NULL is given as the
// device, the current active device is used.
void ui_WidgetAddToDevice(SA_PARAM_OP_VALID UIWidget *widget, SA_PARAM_OP_VALID RdrDevice *device);
void ui_TopWidgetAddToDevice(SA_PARAM_OP_VALID UIWidget *widget, SA_PARAM_OP_VALID RdrDevice *device);
void ui_WindowAddToDevice(UIWindow *window, RdrDevice *device);
SA_RET_NN_VALID UIWidgetGroup *ui_PaneWidgetGroupForDevice(SA_PARAM_OP_VALID RdrDevice *device);
void ui_PaneWidgetAddToDevice(SA_PARAM_OP_VALID UIWidget *widget, SA_PARAM_OP_VALID RdrDevice *device);

// Returns the next higest priority, for stacking modal
int ui_TopWidgetGetNextPriority(void);

// Tells whether or not a modal window is currently displayed
bool ui_IsModalShowing(void);

// Free all widgets associated with this device. This is useful to call when
// the device itself is being destroyed (e.g. closed).
void ui_StateFreeForDevice(RdrDevice *device);

void ui_TooltipsSetActive(SA_PARAM_NN_VALID UIWidget *widget, F32 wtop, F32 wbottom);
void ui_TooltipsSetActiveText(const char* text, F32 wtop, F32 wbottom);

void ui_WidgetCheckStartDrag(SA_PARAM_NN_VALID UIWidget *widget, SA_PARAM_NN_VALID CBox *box);
void ui_WidgetCheckDrop(SA_PARAM_NN_VALID UIWidget *widget, SA_PARAM_NN_VALID CBox *box);

void ui_DrawAndDecrementOverlay(SA_PARAM_NN_VALID UIWidget *widget, SA_PARAM_NN_VALID CBox *box, F32 z);

void ui_WidgetDummyFocusFunc(SA_PARAM_NN_VALID UIWidget *widget, UIAnyWidget *focusitem);

// Free the children, free the list itself, then calls free(widget).
void ui_WidgetFreeInternal(SA_PRE_NN_VALID SA_POST_P_FREE UIWidget *widget);

// Call the free function for each widget.
void ui_WidgetGroupFreeInternal(SA_PARAM_NN_VALID UIWidgetGroup *widgets);

// Queue a widget to be destroyed and NULL its pointer. This isn't safe
// to use during widget callbacks.
void ui_WidgetDestroy(SA_PRE_NN_VALID SA_POST_FREE UIAnyWidget **ppWidget);

extern int g_traceFocus;
extern int g_traceInput;
void inpHandledEx(const char* name, const char* file, int line, const char* reason);
#define inpHandled() inpHandledEx(NULL, __FILE__, __LINE__, "manualCall")
#define inpHandledW(widget, reason) inpHandledEx(ui_WidgetGetName(widget), __FILE__, __LINE__, reason)
void inpResetMouseTime(void);

void ui_SetDeviceKeyboardTextInputEnable(bool bEnable);
void ui_StartDeviceKeyboardTextInput();
void ui_StopDeviceKeyboardTextInput();

void ui_ProcessKeyboardInput(F32 fElapsed, RdrDevice *pDevice);

void ui_WidgetSetCBox(SA_PARAM_NN_VALID UIWidget *pWidget, SA_PARAM_NN_VALID CBox *pBox);
// Doesn't do padding
void ui_WidgetGetCBox(SA_PARAM_NN_VALID UIWidget *pWidget, SA_PARAM_NN_VALID CBox *pBox);  
void ui_WidgetInternalUpdateDynNode(SA_PARAM_NN_VALID UIWidget* pWidget, UI_PARENT_ARGS);
DynFx* ui_WidgetAttachFx( SA_PARAM_NN_VALID UIWidget* widget, SA_PARAM_NN_STR const char* fxName, SA_PARAM_OP_VALID DynNode* targetNode );
DynFx* ui_MouseAttachFx( SA_PARAM_NN_STR const char* fxName, SA_PARAM_OP_VALID DynNode* targetNode );

void ui_ScreenGetBounds(S32 *piLeft, S32 *piRight, S32 *piTop, S32 *piBottom);

void ui_SoftwareCursorThisFrame( void );
void ui_PlaceBoxNextToBox( CBox* toPlace, const CBox* box );

void ui_DrawTextInBoxSingleLine( UIStyleFont *pFont, const char* text, bool truncate, const CBox* box, float z, float scale, UIDirection dir );
void ui_DrawTextInBoxSingleLineRotatedCCW( UIStyleFont *pFont, const char* text, bool truncate, const CBox* box, float z, float scale, UIDirection dir );
//Would be useful: void ui_DrawTextInBoxMultiLine( UIStyleFont *pFont, const char* text, bool truncate, const CBox* box, float z, float scale, UIDirection dir );
	
//////////////////////////////////////////////////////////////////////////
// This macro is insanely useful for writing UI main loop functions, if
// you've used UI_PARENT_ARGS.
#define UI_GET_COORDINATES(widget)  \
	F32 scale = pScale * UI_WIDGET(widget)->scale; \
	F32 x = floorf(ui_WidgetXPosition(UI_WIDGET(widget), pX, pW, pScale)); \
	F32 y = floorf(ui_WidgetYPosition(UI_WIDGET(widget), pY, pH, pScale)); \
	F32 z = UI_GET_Z(); \
	F32 w = ceilf(ui_WidgetWidth(UI_WIDGET(widget), pW, pScale)); \
	F32 h = ceilf(ui_WidgetHeight(UI_WIDGET(widget), pH, pScale)); \
	CBox pBox = {pX, pY, pX + pW, pY + pH}; \
	CBox box = {x, y, x + w, y + h};		\
	CBox* oldWidgetBox = NULL

#define UI_RECALC_COORDINATES(widget) \
	scale = pScale * UI_WIDGET(widget)->scale; \
	x = floorf(ui_WidgetXPosition(UI_WIDGET(widget), pX, pW, pScale)); \
	y = floorf(ui_WidgetYPosition(UI_WIDGET(widget), pY, pH, pScale)); \
	z = UI_GET_Z(); \
	w = floorf(ui_WidgetWidth(UI_WIDGET(widget), pW, pScale)); \
	h = floorf(ui_WidgetHeight(UI_WIDGET(widget), pH, pScale)); \
	pBox.lx = pX; pBox.ly = pY; pBox.hx = pX + pW; pBox.hy = pY + pH; \
	box.lx = x; box.ly = y; box.hx = x + w; box.hy = y + h;

// And this is used for after variable declarations to do some
// stock stuff. The order is important: Things need to be handled
// after child ticks, in case the child wants to handle it.
#define UI_TICK_EARLY(widget, tickChildren, returnIfInactive) \
	ui_WidgetSizerLayout(UI_WIDGET(widget), w, h); \
	UI_TICK_EARLY_WITH(widget, tickChildren, returnIfInactive, UI_MY_VALUES)

#define UI_TICK_EARLY_WITH(widget, tickChildren, returnIfInactive, ...) \
	assert(UI_WIDGET(widget)->priority <= UI_HIGHEST_PRIORITY);			\
	CBoxClipTo(&pBox, &box);                                            \
	if (mouseCollision(&box))                                           \
	{                                                                   \
		if (!(UI_WIDGET(widget)->state & kWidgetModifier_Hovering) && UI_WIDGET(widget)->mouseOverF) \
			UI_WIDGET(widget)->mouseOverF(widget, UI_WIDGET(widget)->mouseOverData); \
		ui_TooltipsSetActive(UI_WIDGET(widget), y, y + h);              \
		UI_WIDGET(widget)->state |= kWidgetModifier_Hovering;           \
	}                                                                   \
	else                                                                \
	{                                                                   \
		if ((UI_WIDGET(widget)->state & kWidgetModifier_Hovering) && UI_WIDGET(widget)->mouseLeaveF) \
			UI_WIDGET(widget)->mouseLeaveF(widget, UI_WIDGET(widget)->mouseLeaveData); \
		UI_WIDGET(widget)->state &= ~kWidgetModifier_Hovering;          \
	}                                                                   \
	if (tickChildren && &UI_WIDGET(widget)->children)                   \
		ui_WidgetGroupTick(&UI_WIDGET(widget)->children, __VA_ARGS__);  \
	if (returnIfInactive && !ui_IsActive(UI_WIDGET(widget)))            \
	{                                                                   \
		if (UI_WIDGET(widget)->state & kWidgetModifier_Hovering)        \
			if(g_traceInput)											\
				inpHandledW(widget, "UI_TICK_EARLY inactive hover");	\
			else														\
				inpHandled();                                           \
		return;                                                         \
	}                                                                   \
	ui_WidgetCheckStartDrag(UI_WIDGET(widget), &box);                   \
	ui_WidgetCheckDrop(UI_WIDGET(widget), &box);                        \
    ui_WidgetInternalUpdateDynNode(UI_WIDGET(widget), UI_PARENT_VALUES); \
	oldWidgetBox = g_ui_State.widgetBox;								\
	g_ui_State.widgetBox = &box

#define UI_TICK_LATE(widget)																\
	g_ui_State.widgetBox = oldWidgetBox;													\
	if (mouseDownHit(MS_RIGHT, &box) && UI_WIDGET(widget)->contextF)						\
	{																						\
		UI_WIDGET(widget)->contextF(widget, UI_WIDGET(widget)->contextData);				\
		if(g_traceInput)																	\
			inpHandledW(widget, "UI_TICK_LATE context");									\
		else																				\
			inpHandled();																	\
	}																						\
	if ((UI_WIDGET(widget)->state & kWidgetModifier_Hovering) && (!UI_WIDGET(widget)->uClickThrough)) \
		if(g_traceInput)																	\
			inpHandledW(widget, "UI_TICK_LATE hover");										\
		else																				\
			inpHandled();																	\


#define UI_DRAW_EARLY(widget) \
	clipperPushRestrict(&box);

#define UI_DRAW_LATE(widget) \
	ui_WidgetGroupDraw(&UI_WIDGET(widget)->children, UI_MY_VALUES); \
	clipperPop();

#define UI_DRAW_LATE_IF(widget, bDrawChildren) \
	if (bDrawChildren) \
		ui_WidgetGroupDraw(&UI_WIDGET(widget)->children, UI_MY_VALUES); \
	clipperPop();

#define UI_TEXTURE(pch) (((pch) && *(pch)) ? atlasLoadTexture(pch) : NULL)

void ui_DrawCapsule(SA_PARAM_NN_VALID const CBox *box, F32 z, Color color, F32 scale);
void ui_DrawOutline(SA_PARAM_NN_VALID const CBox *box, F32 z, Color color, F32 scale);

void ui_LayOutVertically(SA_PRE_NN_NN_VALID UIWidgetGroup *peaGroup);

// Call like ui_LayOutHBox(UITopLeft, y, yP, fX1, pWidget1, fX2, pWidget2, ..., 0, NULL) to
// layout the widgets horizontally. The given float is the distance between
// the widgets.
void ui_LayOutHBox(UIDirection eFrom, F32 fStartY, F32 fYPOffset, ...);

const char *ui_ControllerButtonToTexture(S32 iButton);
const char *ui_ControllerButtonToBigTexture(S32 iButton);
const char *ui_ButtonForCommand(const char *pchCommand);

void ui_SetPlayAudioFunc(UIPlayAudioByName cbPlayAudio, UIPlayAudioByName cbValidateAudio);

void ui_LoadGens(void);

bool ui_LoadTexture(const char *pchName, AtlasTex **ppTex, const char *pchFilename, const char *pchResourceName);
#define UI_LOAD_TEXTURE_FOR(pThing, Texture) ui_LoadTexture((pThing)->pch##Texture, &(pThing)->p##Texture, (pThing)->pchFilename, (pThing)->pchName)

void ui_SetBeforeMainDraw(UIExternLoopFunc cbOncePerFrameBeforeMainDraw);
void ui_DrawWindowButtons(void);

void ui_SetPostCameraUpdateFunc(UIPostCameraUpdateFunc func);

// Texture lookup cache for UI elements.
struct UITextureCache
{
	// Widget parts.
	AtlasTex *windowTitleLeft;
	AtlasTex *windowTitleMiddle;
	AtlasTex *windowTitleRight;

	AtlasTex *arrowSmallUp;
	AtlasTex *arrowSmallDown;
	AtlasTex *arrowSmallLeft;
	AtlasTex *arrowSmallRight;

	AtlasTex *arrowDropDown;

	AtlasTex *minus;
	AtlasTex *plus;

	AtlasTex *white;

	REF_TO(UIStyleBorder) hCapsule;
	REF_TO(UIStyleBorder) hMiniFrame;

	REF_TO(UITextureAssembly) hWindowFrameAssembly;
	REF_TO(UITextureAssembly) hCapsuleAssembly;
	REF_TO(UITextureAssembly) hCapsuleDisabledAssembly;
	REF_TO(UITextureAssembly) hMiniFrameAssembly;

};

void ui_SetGlobalValuesFromActiveSkin(void);


__forceinline UISkin* ui_GetActiveSkin(void)
{
	if( GET_REF( g_ui_State.hActiveSkin )) {
		return GET_REF( g_ui_State.hActiveSkin );
	} else if( g_ui_State.pActiveSkin ) {
		return g_ui_State.pActiveSkin;
	} else {
		return &g_ui_State.default_skin;
	}
}

__forceinline UISkin* ui_WidgetGetSkin(UIWidget* w)
{
	if( GET_REF( w->hOverrideSkin )) {
		return GET_REF( w->hOverrideSkin );
	} else if( w->pOverrideSkin ) {
		return w->pOverrideSkin;
	} else {
		return ui_GetActiveSkin();
	}
}

__forceinline const char* ui_GetTextCursor(UIWidget* w)
{
	UISkin* skin = ui_WidgetGetSkin( w );

	if( skin && skin->pchTextCursor && skin->pchTextCursor[0] ) {
		return skin->pchTextCursor;
	} else {
		return "eui_pointer_text_entry";
	}
}

__forceinline static F32 ui_TweenGetParam(UITweenType eType, F32 fTotalTime, F32 fTimeBetweenCycles, F32 fElapsedTime, F32 *pfNewElapsedTime)
{
	if (fTotalTime < 0.00001)
		return 2.f;
	switch (eType)
	{
	case kUITweenInstant:
		if (pfNewElapsedTime)
			*pfNewElapsedTime = fElapsedTime;
		return 2.f;

	case kUITweenCycle:
		while (fElapsedTime >= 2 * fTotalTime + fTimeBetweenCycles)
			fElapsedTime -= 2 * fTotalTime + fTimeBetweenCycles;
		if (pfNewElapsedTime)
			*pfNewElapsedTime = fElapsedTime;
		if (fElapsedTime < fTotalTime)
			return fElapsedTime / fTotalTime;
		else
			return 1 - min((*pfNewElapsedTime - fTotalTime) / fTotalTime, 1.0);

	case kUITweenSine:
		while (fElapsedTime >= 2*fTotalTime + fTimeBetweenCycles)
			fElapsedTime -= 2*fTotalTime + fTimeBetweenCycles;
		if (pfNewElapsedTime)
			*pfNewElapsedTime = fElapsedTime;
		return sin((PI / 2) * min(fElapsedTime / fTotalTime, 2.0));

	case kUITweenSawtooth:
		while (fElapsedTime >= fTotalTime + fTimeBetweenCycles)
			fElapsedTime -= fTotalTime + fTimeBetweenCycles;
		if (pfNewElapsedTime)
			*pfNewElapsedTime = fElapsedTime;
		return min(fElapsedTime / fTotalTime, 1.0);

	case kUITweenCosine:
		while (fElapsedTime >= 2 * fTotalTime+fTimeBetweenCycles)
			fElapsedTime -= 2 * fTotalTime+fTimeBetweenCycles;
		if (pfNewElapsedTime)
			*pfNewElapsedTime = fElapsedTime;
		return (cos(PI*(min(*pfNewElapsedTime/fTotalTime, 2.0)) - PI)+1.0f)/2.0f;

	case kUITweenEaseIn:
		{
			F32 f = fElapsedTime/fTotalTime - 1.0f;
			if (pfNewElapsedTime)
				*pfNewElapsedTime = fElapsedTime;
			return 1.0f + f*f*f;
		}

	case kUITweenEaseOut:
		{
			F32 f = fElapsedTime/fTotalTime;
			if (pfNewElapsedTime)
				*pfNewElapsedTime = fElapsedTime;
			return f*f*f;
		}

	case kUITweenEaseOutEaseIn:
		{
			F32 f = fElapsedTime/fTotalTime*2;
			if(f <= 1)
			{
				f = f*f*f/2.0f;
			}
			else
			{
				f -= 2.0f; // one to start at zero, and another one to invert the curve
				f = 0.5f + (1.0f + f*f*f)/2;
			}
			if (pfNewElapsedTime)
				*pfNewElapsedTime = fElapsedTime;
			return f;
		}

	default:
		// If we don't know what to do, do a linear tween.
		if (pfNewElapsedTime)
			*pfNewElapsedTime = fElapsedTime;
		return fElapsedTime / fTotalTime;
	}
}

extern struct UITextureCache g_ui_Tex;

extern struct KeyBindProfile g_ui_KeyBindModal;

#endif
