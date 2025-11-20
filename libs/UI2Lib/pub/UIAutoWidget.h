/***************************************************************************



***************************************************************************/

#pragma once
GCC_SYSTEM

#include "referencesystem.h"

//////////////////////////////////////////////////////////////////////////
//
// A set of functions to easily create a re-buildable UI while maintaining
// consistent/reasonable flow, easy grouping, and maintaining state when
// rebuilding a UI.
//
// For an example which uses most features, see the TexWordsEditor.
//
// Feel free to add new data types to ui_AutoWidgetAdd() and more options
// to AutoWidgetParams as desired.

// Tags which can be prepended to text strings going into various parts of this system
#define IMAGE_ONLY_TAG "{img}" // For buttons
#define USE_SMF_TAG "{smf}"

// Convenience macro for consistent coloring of hotkeys (maybe make an SMF tag that
//  grabs a color value from a skin eventually?)
#define SMF_HOTKEY(s) " <color Blue>" s "</color>"

// Forward declarations
typedef struct UIExpander UIExpander;
typedef struct UIExpanderGroup UIExpanderGroup;
typedef struct UIScrollArea UIScrollArea;
typedef struct UIWidget UIWidget;
typedef struct UIWidget ** UIWidgetGroup;
typedef struct UILabel UILabel;
typedef struct UIWindow UIWindow;
typedef struct UIButton UIButton;
typedef struct UIRebuildableTree UIRebuildableTree;
typedef struct UIRebuildableTreeNode UIRTNode;
typedef struct ExprContext ExprContext;
typedef void UIAnyWidget;
typedef void (*UIActivationFunc)(UIAnyWidget *, UserData);
typedef void (*RTNodeDataChangedCallback)(UIRTNode *node, UserData userData);

AUTO_ENUM;
typedef enum UIAutoWidgetType {
	AWT_Default,
	AWT_TextEntry,
	AWT_DictionaryTextEntry,
	AWT_Slider,
	AWT_Spinner,
	AWT_TextureEntry,
	AWT_ColorPicker,
} UIAutoWidgetType;

AUTO_STRUCT;
typedef struct UIAutoWidgetParams {
	UIAutoWidgetType type;
	bool NoLabel;						AST( BOOLFLAG )
	bool disabled;						AST( BOOLFLAG ) // Make this field "disabled"
	bool changedUpdate;					AST( BOOLFLAG ) // Constantly make callbacks on every character change in a text field
	int alignTo;						// Align to an offset of this many pixels
	F32 overrideWidth;					// Override the default width

	const char *labelName;				AST( POOL_STRING ) // Text to use for the label
	UIWidgetGroup *parentGroup;			NO_AST // For colorpicker windows

	// For dictionary text entry
	DictionaryHandleOrName dictionary;	NO_AST
	ParseTable *parseTable;				NO_AST
	const char *parseNameField;
	bool filterable;					AST( BOOLFLAG ) // show a filter in the dictionary combo box
	bool editable;						AST( BOOLFLAG NAME("Editable") ) // include an open in editor button

	// For expression widget
	ExprContext *exprContext;			NO_AST

	// For slider and spinner
	F32 min[3]; AST( INDEX(0, min) DEF(0) )
	F32 max[3]; AST( INDEX(0, max) DEF(1) )
	F32 step[3]; AST( INDEX(0, step) DEF(0) )
	UIActivationFunc spinnerStartF; AST(INT)
	UserData spinnerStartData; AST(INT)
	UIActivationFunc spinnerStopF; AST(INT)
	UserData spinnerStopData; AST(INT)

	// For spinner
	int precision;	// number of displayed digits to include after decimal point

	// For HSV color buttons
	bool hsvAddAlpha;
} UIAutoWidgetParams;

typedef struct UIRebuildableTreeNode
{
	UIRebuildableTree *root; // Top-level parent
	char *name;	// Full name
	UIRTNode **children;
	UIExpander *expander;
	UIWidget *groupWidget; // Expander or ScrollArea if the root
	UIWidget *widget1, *widget2; // If not an expander
	F32 x0, y0, x, y;
	F32 h;
	bool newline; // Start this widget on a new line

	// For data-editing fields
	RTNodeDataChangedCallback callbackF;
	UserData callbackData;
	ParseTable *field;
	int index;
	StaticDefineInt *fieldKey; // For TOK_FLAGS
	void *structptr;
	UIAutoWidgetParams params;
	char ***eaModel; // EArray of strings used in a combo box to be freed
} UIRebuildableTreeNode;

typedef enum UIRTOptions
{
	UIRTOptions_Default = 0,
	UIRTOptions_XScroll = 1 << 0,
	UIRTOptions_YScroll = 1 << 1,
} UIRTOptions;

typedef struct UIRebuildableTree
{
	F32 x0, y0;
	UIScrollArea *scrollArea;
	UIRTNode *root;

	UIScrollArea *old_scrollArea;
	UIRTNode *old_root;
} UIRebuildableTree;

UIRebuildableTree *ui_RebuildableTreeCreate(void);

// Call this to create and attach a new root of the UIRT (which will be a UIScrollArea)
//  or call this on an existing UIRT before rebuilding it and it will save the state
//  of any automatically reused widgets.
void ui_RebuildableTreeInit(UIRebuildableTree *ret, UIWidgetGroup *widget_group, F32 x, F32 y, U32 options);

// Call this when you are done adding things to the UIRT
void ui_RebuildableTreeDoneBuilding(UIRebuildableTree *uirt);

void ui_RebuildableTreeDestroy(UIRebuildableTree *uirt);
void ui_RebuildableTreeReflow(UIRebuildableTree *uirt);

// Detaches all of the widgets owned by UIRT from whatever widget they were
//  originally attached to.  You should only need to call this if you want to
//  ui_WidgetGroupQueueFree() to free a bunch of widgets in the same group as
//  the UIRT (otherwise, this is called automatically when you call 
//  ui_RebuildableTreeInit())
void ui_RebuildableTreeDeinitForRebuild(UIRebuildableTree *uirt);


SA_ORET_NN_VALID UIRTNode *ui_RebuildableTreeAddWidget(SA_PARAM_NN_VALID UIRTNode *parent, SA_PARAM_NN_VALID UIWidget *widget1, SA_PARAM_OP_VALID UIWidget *widget2, bool newline, SA_PARAM_NN_STR const char *name_key, SA_PARAM_OP_VALID UIAutoWidgetParams *params);
SA_ORET_NN_VALID UIRTNode *ui_RebuildableTreeAddGroup(SA_PARAM_NN_VALID UIRTNode *parent, SA_PARAM_NN_STR const char *name, SA_PARAM_OP_STR const char *name_key, bool defaultOpen, SA_PARAM_OP_STR const char *tooltip); // Sets open/closed on the expander
SA_ORET_NN_VALID UIButton *ui_AutoWidgetAddButton(SA_PARAM_NN_VALID UIRTNode *parent, SA_PARAM_NN_STR const char *name, UIActivationFunc clickedF, UserData clickedData, bool newline, SA_PARAM_OP_STR const char *tooltip, SA_PARAM_OP_VALID UIAutoWidgetParams *params);
UIWidget *ui_AutoWidgetAdd(SA_PARAM_NN_VALID UIRTNode *parent, SA_PARAM_NN_VALID ParseTable *table, SA_PARAM_NN_STR const char *fieldName, SA_PARAM_NN_VALID void *structptr, bool newline, RTNodeDataChangedCallback onDataChangedCallback, UserData userData, SA_PARAM_OP_VALID UIAutoWidgetParams *params, SA_PARAM_OP_STR const char *tooltip);
UIWidget *ui_AutoWidgetAddKeyed(SA_PARAM_NN_VALID UIRTNode *parent, SA_PARAM_NN_VALID ParseTable *table, SA_PARAM_NN_STR const char *fieldName, SA_PARAM_OP_STR const char *name_key, SA_PARAM_NN_VALID void *structptr, bool newline, RTNodeDataChangedCallback onDataChangedCallback, UserData userData, SA_PARAM_OP_VALID UIAutoWidgetParams *params, SA_PARAM_OP_STR const char *tooltip);
void ui_AutoWidgetAddAllFlags(SA_PARAM_NN_VALID UIRTNode *parent, SA_PARAM_NN_VALID ParseTable *table, SA_PARAM_NN_STR const char *fieldName, SA_PARAM_NN_VALID void *structptr, bool newline, RTNodeDataChangedCallback onDataChangedCallback, UserData userData, SA_PARAM_OP_VALID UIAutoWidgetParams *params, SA_PARAM_OP_STR const char *tooltip);
SA_RET_NN_VALID UIAutoWidgetParams *ui_AutoWidgetParamsFromString(SA_PARAM_NN_STR const char *str);

SA_ORET_NN_VALID UILabel *ui_RebuildableTreeAddLabel(SA_PARAM_NN_VALID UIRTNode *parent, SA_PARAM_OP_STR const char *labelText, SA_PARAM_OP_VALID UIAutoWidgetParams *params, bool newline);
SA_ORET_NN_VALID UILabel *ui_RebuildableTreeAddLabelWithTooltip(SA_PARAM_NN_VALID UIRTNode *parent, SA_PARAM_OP_STR const char *labelText, const char* tooltipText, SA_PARAM_OP_VALID UIAutoWidgetParams *params, bool newline);
SA_ORET_NN_VALID UILabel *ui_RebuildableTreeAddLabelKeyed(SA_PARAM_NN_VALID UIRTNode *parent, SA_PARAM_OP_STR const char *labelText, SA_PARAM_NN_STR const char *labelKey, SA_PARAM_OP_VALID UIAutoWidgetParams *params, bool newline);
SA_ORET_NN_VALID UILabel *ui_RebuildableTreeAddLabelKeyedWithTooltip(SA_PARAM_NN_VALID UIRTNode *parent, SA_PARAM_OP_STR const char *labelText, const char* tooltipText, SA_PARAM_NN_STR const char *labelKey, SA_PARAM_OP_VALID UIAutoWidgetParams *params, bool newline);

// Call this function when you are rebuilding the tree if you had previously added a custom widget
//   and you wish to re-use it (saving it's UI state, etc).
// fullname is the full name of groups in the heirarchy that specify this widget
// Pass the return of this to ui_RebuildableTreeAddWidget() to re-add it.
SA_RET_OP_VALID UIWidget *ui_RebuildableTreeGetOldWidget(SA_PARAM_NN_VALID UIRebuildableTree *uirt, SA_PARAM_NN_STR const char *fullname);

// Get a widget by name (for changing flags, etc)
SA_RET_OP_VALID UIWidget *ui_RebuildableTreeGetWidgetByName(SA_PARAM_NN_VALID UIRebuildableTree *uirt, SA_PARAM_NN_STR const char *fullname);

#define UIAUTOWIDGET_INDENT 10

// Not valid until after calling ui_RebuildableTreeDoneBuilding
#define ui_RebuildableTreeGetCurrentHeight(uirt) ((uirt)->scrollArea->ySize)