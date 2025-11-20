/***************************************************************************



***************************************************************************/

#ifndef UI_TREE_H
#define UI_TREE_H
GCC_SYSTEM

#include "UICore.h"

//////////////////////////////////////////////////////////////////////////
// A tree view driven by a ParseTable.

typedef struct UITreeNode UITreeNode;
typedef struct UITree UITree;
typedef struct UITreeRefreshNode UITreeRefreshNode;

typedef void (*UITreeChildrenFunc)(UITreeNode *parent, UserData fillData);
typedef void (*UITreeDisplayFunc)(UITreeNode *node, UserData displayData, UI_MY_ARGS, F32 z);
typedef void (*UITreeDragFunc)(UITree *tree, UITreeNode *node, UITreeNode *dragFromParent, UITreeNode *dragToParent, int dragToIndex, UserData dragData);
typedef bool (*UITreeConditionalSelectFunc)(UITreeNode *pTreeNode, void *pUserData);
typedef char *(*UITreeTooltipFunc)(UITreeNode *node, UserData tooltipData); // Returns an allocated string

//////////////////////////////////////////////////////////////////////////
// A tree node is one row of the tree. It contains information on
// how to create its children if necessary.
typedef struct UITreeNode
{
	// The tree this node belongs to.
	UITree *tree;

	// Whether the node is open or not.
	unsigned open : 1;

	// Whether the node should have a line reaching out to it.  Set this to false for separators
	unsigned hide_line : 1;

	// If this node is selectable.  Set this to false to prevent selection
	unsigned allow_selection : 1;

	// The CRC of the node, passed in at creation time.
	U32 crc;

	// The parent node
	UITreeNode *parent;

	// A list of child nodes. This will be empty if the node is closed.
	UITreeNode **children;

	// Widgets to display in the node's box
	UIWidgetGroup widgets;

	// The contents of this node, and the parse table to interpret it if
	// necessary.
	UserData contents;
	ParseTable *table;

	// The height of just this node (not including children).
	F32 height;

	// A function to call when the user requests that this node be expanded.
	// If NULL, the node is presented as a leaf node. This function should
	// add children to the node.
	UITreeChildrenFunc fillF;
	UserData fillData;

	// This function is called whenever a node is expanded
	UITreeChildrenFunc expandF;
	UserData expandData;

	// This function is called whenever a node is collapsed
	UITreeChildrenFunc collapseF;
	UserData collapseData;

	// Called to display the data in the row.
	UITreeDisplayFunc displayF;
	UserData displayData;

	// Called to set the tooltip for the row.
	UITreeTooltipFunc tooltipF;
	UserData tooltipData;

	// Called when the parent node is collapsed, to free any user data.
	UIFreeFunction freeF;
} UITreeNode;

SA_RET_NN_VALID UITreeNode *ui_TreeNodeCreate(SA_PARAM_NN_VALID UITree *tree,
										  U32 crc,
										  SA_PARAM_OP_VALID ParseTable *table, SA_PARAM_OP_VALID void *contents,
										  UITreeChildrenFunc fill, UserData fillData,
										  SA_PARAM_NN_VALID UITreeDisplayFunc display, UserData displayData,
										  F32 height);

void ui_TreeNodeSetFillCallback(SA_PARAM_NN_VALID UITreeNode *node, UITreeChildrenFunc fillF, UserData fillData);
void ui_TreeNodeSetDisplayCallback(SA_PARAM_NN_VALID UITreeNode *node, SA_PARAM_NN_VALID UITreeDisplayFunc displayF, UserData displayData);
void ui_TreeNodeSetFreeCallback(SA_PARAM_NN_VALID UITreeNode *node, SA_PARAM_NN_VALID UIFreeFunction freeF);
void ui_TreeNodeSetExpandCallback(SA_PARAM_NN_VALID UITreeNode *node, SA_PARAM_NN_VALID UITreeChildrenFunc expandF, UserData expandData);
void ui_TreeNodeSetCollapseCallback(SA_PARAM_NN_VALID UITreeNode *node, SA_PARAM_NN_VALID UITreeChildrenFunc collapseF, UserData collapseData);
void ui_TreeNodeExpandEx(SA_PARAM_NN_VALID UITreeNode *node, bool recurse);
#define ui_TreeNodeExpand(node) ui_TreeNodeExpandEx(node, false)
void ui_TreeNodeExpandAndCallbackEx(SA_PARAM_NN_VALID UITreeNode *node, bool recurse);
#define ui_TreeNodeExpandAndCallback(node) ui_TreeNodeExpandAndCallbackEx(node, false)
void ui_TreeNodeCollapse(SA_PARAM_NN_VALID UITreeNode *node);
void ui_TreeNodeCollapseAndCallback(SA_PARAM_NN_VALID UITreeNode *node);

F32 ui_TreeNodeGetHeight(SA_PARAM_NN_VALID UITreeNode *node);

// This adds all the children in the list of **contents, and all of them must
// be the same type of structure, described by *table. Use this if all the
// structures you want in the tree, at this level, are the same. If you
// modify **contents after calling this function, those changes won't be
// reflected in the display.
void ui_TreeNodeAddChildren(SA_PARAM_NN_VALID UITreeNode *node, SA_PARAM_OP_VALID ParseTable *table, void **contents,
							SA_PARAM_OP_VALID UITreeChildrenFunc fill, UserData fillData,
							SA_PARAM_NN_VALID UITreeDisplayFunc display, UserData displayData, F32 height);

// Add a single child to the node.
void ui_TreeNodeAddChild(SA_PARAM_NN_VALID UITreeNode *parent, SA_PARAM_NN_VALID UITreeNode *child);
// Remove and free a single node from the node.
bool ui_TreeNodeRemoveChild(SA_PARAM_NN_VALID UITreeNode *parent, SA_PARAM_NN_VALID UITreeNode *child);
bool ui_TreeNodeRemoveChildByContents(SA_PARAM_NN_VALID UITreeNode *parent, UserData contents);
void ui_TreeNodeTruncateChildren(SA_PARAM_NN_VALID UITreeNode *parent, S32 children);

// Sort children
typedef int UITreeNodeComparator(const UITreeNode** child_a, const UITreeNode** child_b);
void ui_TreeNodeSortChildren(SA_PARAM_NN_VALID UITreeNode* parent, UITreeNodeComparator* comparator);

int ui_TreeNodeGetIndent(SA_PARAM_NN_VALID UITreeNode *node);
UITreeNode *ui_TreeNodeFindParent(SA_PARAM_NN_VALID UITree *tree, SA_PARAM_NN_VALID UITreeNode *node);

//////////////////////////////////////////////////////////////////////////
// Some default/example node fill and draw functions.

void ui_TreeNodeDisplaySimple(UITreeNode *node, const char *field, UI_MY_ARGS, F32 z);
void ui_TreeDisplayText(SA_PARAM_NN_VALID UITreeNode *node, SA_PARAM_NN_STR const char *text, UI_MY_ARGS, F32 z);
void ui_TreeDisplayTextIndirect(SA_PARAM_NN_VALID UITreeNode *node, SA_PARAM_NN_STR const char **ppText, UI_MY_ARGS, F32 z);
void ui_TreeDisplaySeparator(SA_PARAM_NN_VALID UITreeNode *node, void *ignored, UI_MY_ARGS, F32 z);

//////////////////////////////////////////////////////////////////////////
// Each UITree has a root node, tree->root. This root node is not drawn, but
// its children are, and form the top level of the visible tree. To set up
// a UITree, set the fill callback on the root, and then expand it
// using ui_TreeNodeExpand(&tree->root).

typedef struct UITree
{
	UIWidget widget;
	UITreeNode root;
	UITreeNode *selected;

	UITreeNode **multiselected;
	unsigned multiselect : 1;
	unsigned bAllowDropNode : 1;

	// The width of the tree. If this is non-zero the tree has a 
	// horizontal scrollbar which scrolls along this width.
	F32 width;

	// When a new node is selected. Both activating and right clicking also
	// change the selection.
	UIActivationFunc selectedF;
	UserData selectedData;

	// When a node is double-left-clicked.
	UIActivationFunc activatedF;
	UserData activatedData;

	// When dragging a node.
	bool itemDraggingEnabled;
	bool dragToNewParent; // Normal drag inserts between nodes.  When this is true you can drop on new parent.
	UITreeNode *dragging;
	UITreeDragFunc dragF;
	UserData dragData;

	// If true, the tree should scroll to the selected row during the
	// next frame.
	bool scrollToSelected : 1;
	bool isInWidgetTick : 1; // Internal flag - we are in the middle of a tick so don't allow freeing nodes
} UITree;

SA_RET_NN_VALID UITree *ui_TreeCreate(F32 x, F32 y, F32 w, F32 h);
void ui_TreeInitialize(SA_PRE_NN_FREE SA_POST_NN_VALID UITree *tree, F32 x, F32 y, F32 w, F32 h);
void ui_TreeFreeInternal(SA_PRE_NN_VALID SA_POST_P_FREE UITree *tree);

SA_RET_OP_VALID UITreeNode *ui_TreeGetSelected(SA_PARAM_NN_VALID UITree *tree);

void ui_TreeSetMultiselect(SA_PARAM_NN_VALID UITree *tree, bool multiselect);
SA_RET_OP_VALID const UITreeNode * const * const *ui_TreeGetSelectedNodes(SA_PARAM_NN_VALID UITree *tree);
bool ui_TreeIsNodeSelected(SA_PARAM_NN_VALID UITree *tree, SA_PARAM_NN_VALID UITreeNode *node);

void ui_TreeSelectAll(SA_PARAM_NN_VALID UITree *tree);
void ui_TreeSelectLeaves(SA_PARAM_NN_VALID UITree *tree, SA_PARAM_NN_VALID UITreeNode *node, bool include_sub_nodes);
void ui_TreeSetSelectedCallback(SA_PARAM_NN_VALID UITree *tree, UIActivationFunc selectedF, UserData selectedData);
void ui_TreeSetActivatedCallback(SA_PARAM_NN_VALID UITree *tree, UIActivationFunc activatedF, UserData activatedData);
void ui_TreeSetDragCallback(SA_PARAM_NN_VALID UITree *tree, UITreeDragFunc dragF, UserData dragData);
void ui_TreeSetContextCallback(SA_PARAM_NN_VALID UITree *tree, UIActivationFunc contextF, UserData contextData);
void ui_TreeEnableDragAndDrop(SA_PARAM_NN_VALID UITree *tree);
void ui_TreeDisableDragAndDrop(SA_PARAM_NN_VALID UITree *tree);

F32 ui_TreeGetHeight(SA_PARAM_NN_VALID UITree *tree);
F32 ui_TreeNodeGetPosition(SA_PARAM_NN_VALID UITree *tree, SA_PARAM_NN_VALID UITreeNode *node);
void ui_TreeDraw(SA_PARAM_NN_VALID UITree *tree, UI_PARENT_ARGS);
void ui_TreeTick(SA_PARAM_NN_VALID UITree *tree, UI_PARENT_ARGS);
SA_RET_OP_VALID UITreeNode *ui_TreeNodeFind(SA_PARAM_NN_VALID UITree *tree, F32 y);
void ui_TreeSelectAt(SA_PARAM_NN_VALID UITree *tree, F32 y);
void ui_TreeUnselectAll(SA_PARAM_NN_VALID UITree *tree);
bool ui_TreeInput(SA_PARAM_NN_VALID UITree *tree, SA_PARAM_NN_VALID KeyInput *input);
void ui_TreeToggle(SA_PARAM_NN_VALID UITree *tree, F32 yClicked);
void ui_TreeRefresh(SA_PARAM_NN_VALID UITree *tree);
void ui_TreeExpandAndSelect(UITree *tree, const void **expand_list, bool call_callback);

typedef struct UITreeRefreshNode
{
	U32 nodeCrc;
	bool open;
	UITreeRefreshNode **children;
} UITreeRefreshNode;

SA_RET_NN_VALID UITreeRefreshNode *ui_TreeRefreshNodeCreate(SA_PARAM_NN_VALID UITreeNode *tree_node);
void ui_TreeRefreshNodeDestroy(SA_PARAM_OP_VALID UITreeRefreshNode *refresh_node);
void ui_TreeRefreshEx(SA_PARAM_NN_VALID UITree *tree, SA_PARAM_OP_VALID UITreeRefreshNode *refresh_root);

typedef struct UITreeIterator
{
	UITree *tree;
	int *nodeIdxs;
	U32 cycle : 1;
	U32 expandNodes : 1;
} UITreeIterator;

SA_RET_NN_VALID UITreeIterator *ui_TreeIteratorCreate(SA_PARAM_NN_VALID UITree *tree, bool cycle, bool expandNodes);
SA_RET_NN_VALID UITreeIterator *ui_TreeIteratorCreateFromNode(SA_PARAM_NN_VALID UITreeNode *node, bool cycle, bool expandNodes);
void ui_TreeIteratorFree(SA_PARAM_OP_VALID UITreeIterator *iterator);
SA_RET_OP_VALID UITreeNode *ui_TreeIteratorNext(SA_PARAM_NN_VALID UITreeIterator *iterator);
SA_RET_OP_VALID UITreeNode *ui_TreeIteratorCurr(SA_PARAM_NN_VALID UITreeIterator *iterator);
SA_RET_OP_VALID UITreeNode *ui_TreeIteratorPrev(SA_PARAM_NN_VALID UITreeIterator *iterator);

void ui_TreeSelectFromBranchWithCondition(UITree *pTree, UITreeNode *pTreeNode, UITreeConditionalSelectFunc pFunc, void *pUserData);
UIStyleFont* ui_TreeItemGetFont( UITree* pTree, bool bSelected, bool bHover );

#endif
