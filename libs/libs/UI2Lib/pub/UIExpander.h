/***************************************************************************



***************************************************************************/

#ifndef UI_EXPANDER_H
#define UI_EXPANDER_H
GCC_SYSTEM

#include "UICore.h"

typedef struct UIExpanderGroup UIExpanderGroup;

#define UI_EXPANDER_Z_LINES 0.6

//////////////////////////////////////////////////////////////////////////
// An expander hides some widgets under a horizontal clickable bar.
// Expanders must have fixed vertical sizes; horizontal sizes can be
// adjusted normally.
typedef struct UIExpander
{
	UIWidget widget;
	//bool opened; changed to use widget.childrenInactive
	bool hidden;
	// If set, attempt to scroll parent to keep expanded area in view
	bool autoScroll;

	bool bFirstRefresh;

	F32 openedHeight;
	
	// This is only used when the expander is in an expander group, to
	// calculate the size of the horizontal scroll area.
	F32 openedWidth;

	UIActivationFunc expandF;
	UserData expandData;
	char *pchDescription;
	int iDescriptionOffset;

	UIExpanderGroup *group;

	UIWidgetGroup labelChildren;

	UIActivationFunc headerContextF;
	UserData headerContextData;
} UIExpander;

SA_RET_NN_VALID UIExpander *ui_ExpanderCreate(SA_PARAM_NN_STR const char *title, F32 openedHeight);
void ui_ExpanderInitialize(SA_PRE_NN_FREE SA_POST_NN_VALID UIExpander *expand, SA_PARAM_NN_STR const char *title, F32 openedHeight);
void ui_ExpanderFreeInternal(SA_PRE_NN_VALID SA_POST_P_FREE UIExpander *expand);

void ui_ExpanderAddChild(SA_PARAM_NN_VALID UIExpander *expand, SA_PRE_NN_BYTES(sizeof(UIWidget)) SA_POST_NN_VALID UIAnyWidget *child);
void ui_ExpanderRemoveChild(SA_PARAM_NN_VALID UIExpander *expand, SA_PRE_NN_BYTES(sizeof(UIWidget)) SA_POST_NN_VALID UIAnyWidget *child);

// Things drawn on the label section of the Expander (not hidden when expander is closed)
void ui_ExpanderAddLabel(SA_PARAM_NN_VALID UIExpander *expand, SA_PARAM_NN_VALID UIWidget *child);
void ui_ExpanderRemoveLabel(SA_PARAM_NN_VALID UIExpander *expand, SA_PARAM_NN_VALID UIWidget *child);

void ui_ExpanderToggle(SA_PARAM_NN_VALID UIExpander *expand);
void ui_ExpanderSetHeight(SA_PARAM_NN_VALID UIExpander *expand, F32 openedHeight);
void ui_ExpanderReflow(SA_PARAM_NN_VALID UIExpander *expand);
bool ui_ExpanderIsOpened(SA_PARAM_NN_VALID UIExpander *expand);
void ui_ExpanderSetOpened(SA_PARAM_NN_VALID UIExpander *expand, bool opened);

void ui_ExpanderSetDescriptionText(UIExpander *expand, char *pchText);
void ui_ExpanderSetName(UIExpander *expand, const char *pchText);

void ui_ExpanderSetExpandCallback(UIExpander *expand, UIActivationFunc expandF, UserData expandData);
void ui_ExpanderSetHeaderContextCallback(UIExpander *expand, UIActivationFunc rclickF, UserData rclickData);

void ui_ExpanderTick(UIExpander *expand,UI_PARENT_ARGS);
void ui_ExpanderDraw(UIExpander *expand,UI_PARENT_ARGS);

//////////////////////////////////////////////////////////////////////////
// This is actually a misnomer. An ExpanderGroup contains any kind of
// fixed-height widgets. It has some special cases to deal with
// expanders, but you can put e.g. separators or buttons inside it too.
//
// An expander group is a list of expanders stacked vertically. Expanding
// or collapsing them repositions all of them. The expanders are placed
// within a scroll area.
typedef struct UIExpanderGroup
{
	UIWidget widget;
	UIWidget **childrenInOrder;
	F32 totalHeight;
	F32 spacing;	// Distance between elements when the group reflows

	// Called when the expander group reflows (e.g. a widget is added,
	// removed, or an expander is toggled).
	UIActivationFunc reflowF;
	UserData reflowData;

	REF_TO(UIStyleBorder) hBorder;
} UIExpanderGroup;

SA_RET_NN_VALID UIExpanderGroup *ui_ExpanderGroupCreate(void);
void ui_ExpanderGroupFreeInternal(SA_PRE_NN_VALID SA_POST_P_FREE UIExpanderGroup *group);

void ui_ExpanderGroupAddExpander(SA_PARAM_NN_VALID UIExpanderGroup *group, SA_PARAM_NN_VALID UIExpander *expand);
void ui_ExpanderGroupInsertExpander(SA_PARAM_NN_VALID UIExpanderGroup *group, SA_PARAM_NN_VALID UIExpander *expand, int pos);
void ui_ExpanderGroupRemoveExpander(SA_PARAM_NN_VALID UIExpanderGroup *group, SA_PARAM_OP_VALID UIExpander *expand);

void ui_ExpanderGroupAddWidget(SA_PARAM_NN_VALID UIExpanderGroup *group, SA_PARAM_NN_VALID UIWidget *widget);
void ui_ExpanderGroupInsertWidget(SA_PARAM_NN_VALID UIExpanderGroup *group, SA_PARAM_NN_VALID UIWidget *widget, int pos);
void ui_ExpanderGroupRemoveWidget(SA_PARAM_NN_VALID UIExpanderGroup *group, SA_PARAM_NN_VALID UIWidget *widget);

void ui_ExpanderGroupReflow(SA_PARAM_NN_VALID UIExpanderGroup *group);

// Enabling grow will cause an expander group to resize itself (by resetting
// its widget height) when an expander inside it is expanded or collapsed. It
// also removes the vertical scrollbar.
void ui_ExpanderGroupSetGrow(SA_PARAM_NN_VALID UIExpanderGroup *group, bool grow);

void ui_ExpanderGroupSetSpacing(SA_PARAM_NN_VALID UIExpanderGroup *group, F32 spacing);

void ui_ExpanderGroupSetReflowCallback(SA_PARAM_NN_VALID UIExpanderGroup *group, UIActivationFunc reflowF, UserData reflowData);

#endif