/***************************************************************************



***************************************************************************/

#ifndef UI_FLOWCHART_H
#define UI_FLOWCHART_H
GCC_SYSTEM

#include "UICore.h"
#include "UIWindow.h"
#include "UIScrollbar.h"

#include "CBox.h"

typedef struct UILabel UILabel;
typedef struct UIWindow UIWindow;
typedef struct UIFlowchart UIFlowchart;
typedef struct UIFlowchartNode UIFlowchartNode;
typedef struct UIFlowchartButton UIFlowchartButton;
typedef struct UIPane UIPane;
typedef struct UISprite UISprite;

typedef enum UIFlowchartButtonType
{
	UIFlowchartNormal,
	UIFlowchartHasChildrenInternal, // Auto-determined, just use UIFlowchartNormal
	UIFlowchartIsChild
} UIFlowchartButtonType;

typedef struct UIFlowchartButton
{
	UIWidget widget;
	// State last frame.
	F32 drawnX, drawnY;
	bool bDrawn;

	UIFlowchartButton **connected;
	UIFlowchartNode *node;
	UIFlowchart *flow;

	bool output : 1;
	bool bSingleConnection : 1;

	// If a flowchart button is open, and HasChildren, all flowchart
	// buttons below it that are IsChild are drawn. Otherwise, if it's
	// not open, IsChild elements are only drawn if connected.
	UIFlowchartButtonType type;
	bool open : 1;

	UserData userData;

	UILabel *label;
} UIFlowchartButton;

SA_RET_NN_VALID UIFlowchartButton *ui_FlowchartButtonCreate(SA_PARAM_NN_VALID UIFlowchart *flow, UIFlowchartButtonType type, SA_PARAM_OP_VALID UILabel *label, UserData userData);
void ui_FlowchartButtonInitialize(SA_PARAM_NN_VALID UIFlowchartButton *fbutton, SA_PARAM_NN_VALID UIFlowchart *flow, UIFlowchartButtonType type, SA_PARAM_OP_VALID UILabel *label, UserData userData);
void ui_FlowchartButtonFree(SA_PRE_NN_VALID SA_POST_P_FREE UIFlowchartButton *fbutton);
void ui_FlowchartButtonUnlink(SA_PARAM_NN_VALID UIFlowchartButton *fbutton, bool force);
void ui_FlowchartButtonDraw(SA_PARAM_NN_VALID UIFlowchartButton *fbutton, UI_PARENT_ARGS);
void ui_FlowchartButtonTick(SA_PARAM_NN_VALID UIFlowchartButton *fbutton, UI_PARENT_ARGS);
void ui_FlowchartButtonSetSingleConnection(UIFlowchartButton *fbutton, bool bSingle);

//////////////////////////////////////////////////////////////////////////
typedef struct UIFlowchartNode {
	UI_INHERIT_FROM(UI_WIDGET_TYPE UI_WINDOW_TYPE);
	UserData userData;
	UIFlowchartButton **inputButtons;
	UIFlowchartButton **outputButtons;
	UIFlowchart *flow;
    UIPane *beforePane;
	UIPane *afterPane;
	UISprite* backgroundScrollSprite;
	F32 backgroundScrollXPercent;
	F32 backgroundScrollWidthPercent;

	unsigned autoResize : 1;
} UIFlowchartNode;

// Creates a new UIFlowchartNode (which is also a UIWindow).
// Takes ownership of all buttons passed in (and removes them from the caller's EArray).
// Nodes and labels will be automatically positioned inside the window.
UIFlowchartNode *ui_FlowchartNodeCreate(SA_PARAM_OP_STR const char *title, F32 x, F32 y, F32 w, F32 h,
										SA_PARAM_NN_VALID UIFlowchartButton ***inputButtons, SA_PARAM_NN_VALID UIFlowchartButton ***outputButtons, SA_PARAM_OP_VALID UserData userData);
void ui_FlowchartNodeDraw(UIFlowchartNode *pNode, UI_PARENT_ARGS);
void ui_FlowchartNodeTick(UIFlowchartNode *pNode, UI_PARENT_ARGS);
void ui_FlowchartNodeAddChild(UIFlowchartNode *pNode, UIWidget *pWidget, bool isAfter);
void ui_FlowchartNodeRemoveChild(UIFlowchartNode *pNode, UIWidget *pWidget);
void ui_FlowchartNodeRemoveAllChildren(UIFlowchartNode *pNode, bool removeBefore, bool removeAfter);
__forceinline static void ui_FlowchartNodeSetAutoResize(UIFlowchartNode *pNode, bool autoResize)
{
	pNode->autoResize = !!autoResize;
}
void ui_FlowchartNodeReflow(UIFlowchartNode *pNode);

UIFlowchartButton *ui_FlowchartNodeFindButtonByName(SA_PARAM_NN_VALID UIFlowchartNode *node, SA_PARAM_NN_STR const char *text, bool isInput);

//////////////////////////////////////////////////////////////////////////
// A flow chart is a special type of Scroll Area that also has additional
// special information about "nodes" inside it. Each node has some buttons and
// labels, which can be linked up.

// If the callback returns false, the link or unlink may not happen, unless
// force is true. This is the case when the entire node is being removed or
// freed. Links are never forced.
typedef bool (*UIFlowchartLinkFunc)(UIFlowchart *flow, UIFlowchartButton *source, UIFlowchartButton *dest, bool force, UserData userData);

// If the callback returns false, the node action may not happen.
typedef bool (*UIFlowchartNodeFunc)(UIFlowchart *flow, UIFlowchartNode *node, UserData userData);

typedef struct UIFlowchart
{
	UI_INHERIT_FROM(UI_WIDGET_TYPE UI_SCROLLAREA_TYPE);

	UIFlowchartButton *connecting;

	// When this function is called, the player has clicked a button to select
	// the start of a link. If it returns false, the link will not start.
	// If the node is an input node, output will be NULL, and vice versa.
	UIFlowchartLinkFunc startLinkF;
	UserData startLinkData;

	// This function starts a bound on a sequence of calls to linkedF
	// and unlinkedF that result from one atomic UI operation.  No
	// links have been made yet.
	//
	// If it returs false, no links will be made.
	UIFlowchartLinkFunc linkBeginF;
	UserData linkBeginData;

	// This function end a bound on a sequence of calls to linkedF
	// and unlinkedF that result from one atomic UI operation.
	//
	// If FORCE is set, then the link succesfully happend, if unset,
	// the link failed to happen.
	UIFlowchartLinkFunc linkEndF;
	UserData linkEndData;

	// When this function is called, the link has already happened. If it returns false,
	// the link will be undone, but unlinkedF will not be called.
	UIFlowchartLinkFunc linkedF;
	UserData linkedData;

	// When this function is called, the link is still present. If it returns false,
	// the link will remain.
	UIFlowchartLinkFunc unlinkedF;
	UserData unlinkedData;

	// In other words, nodes are linked whenever the callback is called.

	// Child nodes which have been added, get destroyed when freeing the flowchart
	UIFlowchartNode **nodeWindows;

	// When this function is called, the node is still present and its
	// links are all connected.  If it returns false, the node will
	// remain.
	UIFlowchartNodeFunc nodeRemovedF;
	UserData nodeRemovedData;

	// When this function is called, the node is still present but all
	// its links are broken.  The node WILL BE REMOVED.
	UIFlowchartNodeFunc nodeRemovedLateF;
	UserData nodeRemovedLateData;

	CBox clip;
} UIFlowchart;

SA_RET_NN_VALID UIFlowchart *ui_FlowchartCreate(SA_PARAM_OP_VALID UIFlowchartLinkFunc startLinkF, SA_PARAM_OP_VALID UIFlowchartLinkFunc linkedF, SA_PARAM_OP_VALID UIFlowchartLinkFunc unlinkedF, SA_PARAM_OP_VALID UserData userData);
void ui_FlowchartFree(SA_PRE_NN_VALID SA_POST_P_FREE UIFlowchart *flow);
void ui_FlowchartClear(SA_PARAM_NN_VALID UIFlowchart *flow);

bool ui_FlowchartLink(SA_PARAM_NN_VALID UIFlowchartButton *source, SA_PARAM_NN_VALID UIFlowchartButton *dest);
bool ui_FlowchartUnlink(SA_PARAM_NN_VALID UIFlowchartButton *source, SA_PARAM_NN_VALID UIFlowchartButton *dest, bool force);

// Swaps connections between two buttons of the same type.
void ui_FlowchartSwap(SA_PARAM_NN_VALID UIFlowchartButton *pButton1, SA_PARAM_NN_VALID UIFlowchartButton *pButton2);

// Add a node to this flowchart, it will be freed when the flowchart is freed
// This also overrides closeF; closing the window will now call ui_FlowchartRemoveNode.
void ui_FlowchartAddNode(SA_PARAM_NN_VALID UIFlowchart *flow, SA_PARAM_NN_VALID UIFlowchartNode *node);

// Turn a flowchart node back into a normal window. Removes flowchart buttons
// and labels, and any links between them. The window will not be freed or hidden.
void ui_FlowchartRemoveNode(SA_PARAM_NN_VALID UIFlowchart *flow, SA_PARAM_NN_VALID UIFlowchartNode *window);

void ui_FlowchartSetStartLinkCallback(SA_PARAM_NN_VALID UIFlowchart *flow, UIFlowchartLinkFunc startLinkF, UserData startLinkData);
void ui_FlowchartSetLinkBeginCallback(SA_PARAM_NN_VALID UIFlowchart *flow, UIFlowchartLinkFunc linkBeginF, UserData linkBeginData);
void ui_FlowchartSetLinkEndCallback(SA_PARAM_NN_VALID UIFlowchart *flow, UIFlowchartLinkFunc linkEndF, UserData linkEndData);
void ui_FlowchartSetLinkedCallback(SA_PARAM_NN_VALID UIFlowchart *flow, UIFlowchartLinkFunc linkedF, UserData linkedData);
void ui_FlowchartSetUnlinkedCallback(SA_PARAM_NN_VALID UIFlowchart *flow, UIFlowchartLinkFunc unlinkedF, UserData unlinkedData);
void ui_FlowchartSetNodeRemovedCallback(SA_PARAM_NN_VALID UIFlowchart *flow, UIFlowchartNodeFunc nodeRemovedF, UserData nodeRemovedData);
void ui_FlowchartSetNodeRemovedLateCallback(SA_PARAM_NN_VALID UIFlowchart *flow, UIFlowchartNodeFunc nodeRemovedLateF, UserData nodeRemovedLateData);

UIFlowchartNode *ui_FlowchartFindNode(SA_PARAM_NN_VALID UIFlowchart *flow, SA_PARAM_NN_VALID UserData userData);

UIFlowchartNode *ui_FlowchartFindNodeFromName(SA_PARAM_NN_VALID UIFlowchart *flow, SA_PARAM_NN_VALID const char *pNodeName);

void ui_FlowchartFindValidPos(SA_PARAM_NN_VALID UIFlowchart *flow, SA_PRE_NN_FREE SA_POST_NN_VALID Vec2 pos, F32 width, F32 height, F32 minx, F32 miny, F32 preferredx, F32 grid_size);

// Sets all the nodes in the flowchart to be inactive (or active).
void ui_FlowchartSetReadOnly(UIFlowchart *pFlow, bool bReadOnly);
void ui_FlowchartNodeSetReadOnly(UIFlowchartNode *pNode, bool bReadOnly);

#endif
