/***************************************************************************



***************************************************************************/

#ifndef ui_WidgetTree_H
#define ui_WidgetTree_H
GCC_SYSTEM

#include "UICore.h"

//////////////////////////////////////////////////////////////////////////
// A tree view driven by a ParseTable.
// A widget tree is comprised of tree nodes that are lines of widgets, instead
//   of the normal tree which is just text.  Slower, but better and more usable.

typedef struct UIWidgetTreeNode UIWidgetTreeNode;
typedef struct UIWidgetTree UIWidgetTree;
typedef struct UIPane UIPane;
typedef struct UIButton UIButton;
typedef struct UILabel UILabel;

typedef bool (*UIWidgetTreeNodeIsVisibleFunc)(UIWidgetTreeNode *child, UserData contents);
typedef void (*UIWidgetTreeChildrenFunc)(UIWidgetTreeNode *parent, UserData fillData);
typedef void (*UIWidgetTreeDisplayFunc)(UIWidgetTreeNode *node, UIPane *pane, UserData displayData);
typedef void (*UIWidgetTreeDragFunc)(UIWidgetTree *tree, UIWidgetTreeNode *node, UIWidgetTreeNode *dragFromParent, UIWidgetTreeNode *dragToParent, int dragToIndex, UserData dragData);

//////////////////////////////////////////////////////////////////////////
// A tree node is one row of the tree. It contains information on
// how to create its children if necessary.
typedef struct UIWidgetTreeNode
{
	UIWidget widget;
	// The tree this node belongs to.
	UIWidgetTree *tree;

	// Whether the node is open or not.
	bool open : 1;

	// The CRC of the node, passed in at creation time.
	U32 crc;

	// A pane to put widgets for display, will always have at least a label and button
	UIButton *expand;
	UILabel *title;
	UIPane *info_pane;
	UIPane *node_pane;
	
	// A pane for the user to fill out in his display func
	UIPane *user_pane;
	F32 user_padding;

	// The parent node, for layout purposes
	UIWidgetTreeNode *parent;
	// A list of child nodes. This will be empty if the node is closed.
	UIWidgetTreeNode **children;

	// The contents of this node, and the parse table to interpret it if
	// necessary.
	UserData contents;
	ParseTable *table;

	// The height of just this node (not including children).
	F32 height;

	// A function to call when the user requests that this node be expanded.
	// If NULL, the node is presented as a leaf node. This function should
	// add children to the node.
	UIWidgetTreeChildrenFunc fillF;
	UserData fillData;

	// This function is called whenever a node is expanded
	UIWidgetTreeChildrenFunc expandF;
	UserData expandData;

	// This function is called whenever a node is collapsed
	UIWidgetTreeChildrenFunc collapseF;
	UserData collapseData;

	// Called to display the data in the row.
	UIWidgetTreeDisplayFunc displayF;
	UserData displayData;

	// Called when the parent node is collapsed, to free any user data.
	UIFreeFunction freeF;

	// Called to determine if the node should be drawn
	UIWidgetTreeNodeIsVisibleFunc isvisibleF;
} UIWidgetTreeNode;

SA_RET_NN_VALID UIWidgetTreeNode *ui_WidgetTreeNodeCreate(	SA_PARAM_NN_VALID UIWidgetTree *tree, 
														const char *name, 
														U32 crc, 
														SA_PARAM_OP_VALID ParseTable *table, 
														SA_PARAM_OP_VALID void *contents,
														UIWidgetTreeChildrenFunc fill, 
														UserData fillData,
														SA_PARAM_OP_VALID UIWidgetTreeDisplayFunc display, 
														UserData displayData,
														F32 height);

void ui_WidgetTreeNodeSetFillCallback(SA_PARAM_NN_VALID UIWidgetTreeNode *node, UIWidgetTreeChildrenFunc fillF, UserData fillData);
void ui_WidgetTreeNodeSetDisplayCallback(SA_PARAM_NN_VALID UIWidgetTreeNode *node, SA_PARAM_NN_VALID UIWidgetTreeDisplayFunc displayF, UserData displayData);
void ui_WidgetTreeNodeSetFreeCallback(SA_PARAM_NN_VALID UIWidgetTreeNode *node, SA_PARAM_NN_VALID UIFreeFunction freeF);
void ui_WidgetTreeNodeSetExpandCallback(SA_PARAM_NN_VALID UIWidgetTreeNode *node, SA_PARAM_NN_VALID UIWidgetTreeChildrenFunc expandF, UserData expandData);
void ui_WidgetTreeNodeSetCollapseCallback(SA_PARAM_NN_VALID UIWidgetTreeNode *node, SA_PARAM_NN_VALID UIWidgetTreeChildrenFunc collapseF, UserData collapseData);
void ui_WidgetTreeNodeSetIsVisibleCallback(SA_PARAM_NN_VALID UIWidgetTreeNode *node, UIWidgetTreeNodeIsVisibleFunc isvizfunc);
void ui_WidgetTreeNodeExpand(SA_PARAM_NN_VALID UIWidgetTreeNode *node);
void ui_WidgetTreeNodeExpandAndCallback(SA_PARAM_NN_VALID UIWidgetTreeNode *node);
void ui_WidgetTreeNodeCollapse(SA_PARAM_NN_VALID UIWidgetTreeNode *node);
void ui_WidgetTreeNodeCollapseAndCallback(SA_PARAM_NN_VALID UIWidgetTreeNode *node);

UILabel* ui_WidgetTreeNodeGetTextLabel(SA_PARAM_NN_VALID UIWidgetTreeNode *node);

F32 ui_WidgetTreeNodeGetHeight(SA_PARAM_NN_VALID UIWidgetTreeNode *node);

// This adds all the children in the list of **contents, and all of them must
// be the same type of structure, described by *table. Use this if all the
// structures you want in the tree, at this level, are the same. If you
// modify **contents after calling this function, those changes won't be
// reflected in the display.
/*
void ui_WidgetTreeNodeAddChildren(SA_PARAM_NN_VALID UIWidgetTreeNode *node, SA_PARAM_OP_VALID ParseTable *table, void **contents,
							SA_PARAM_OP_VALID UIWidgetTreeChildrenFunc fill, UserData fillData,
							SA_PARAM_NN_VALID UIWidgetTreeDisplayFunc display, UserData displayData, F32 height);
*/

// Add a single child to the node.
void ui_WidgetTreeNodeAddChild(SA_PARAM_NN_VALID UIWidgetTreeNode *parent, SA_PARAM_NN_VALID UIWidgetTreeNode *child);
// Remove and free a single node from the node.
bool ui_WidgetTreeNodeRemoveChild(SA_PARAM_NN_VALID UIWidgetTreeNode *parent, SA_PARAM_NN_VALID UIWidgetTreeNode *child);
bool ui_WidgetTreeNodeRemoveChildByContents(SA_PARAM_NN_VALID UIWidgetTreeNode *parent, UserData contents);

int ui_WidgetTreeNodeGetIndent(SA_PARAM_NN_VALID UIWidgetTreeNode *node);

//////////////////////////////////////////////////////////////////////////
// Each UIWidgetTree has a root node, tree->root. This root node is not drawn, but
// its children are, and form the top level of the visible tree. To set up
// a UIWidgetTree, set the fill callback on the root, and then expand it
// using ui_WidgetTreeNodeExpand(&tree->root).

typedef struct UIWidgetTree
{
	UIWidget widget;
	UIWidgetTreeNode *root;
	UIWidgetTreeNode *selected;

	UIWidgetTreeNode **multiselected;
	bool multiselect : 1;

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

	// When a node is right-clicked
	UIActivationFunc contextF;
	UserData contextData;

	// When dragging a node.
	bool itemDraggingEnabled;
	UIWidgetTreeNode *dragging;
	UIWidgetTreeDragFunc dragF;
	UserData dragData;

	// If true, the tree should scroll to the selected row during the
	// next frame.
	bool scrollToSelected : 1;
} UIWidgetTree;

SA_RET_NN_VALID UIWidgetTree *ui_WidgetTreeCreate(F32 x, F32 y, F32 w, F32 h);
void ui_WidgetTreeInitialize(SA_PRE_NN_FREE SA_POST_NN_VALID UIWidgetTree *tree, F32 x, F32 y, F32 w, F32 h);
void ui_WidgetTreeFreeInternal(SA_PRE_NN_VALID SA_POST_P_FREE UIWidgetTree *tree);

SA_RET_OP_VALID UIWidgetTreeNode *ui_WidgetTreeGetSelected(SA_PARAM_NN_VALID UIWidgetTree *tree);

void ui_WidgetTreeSetMultiselect(SA_PARAM_NN_VALID UIWidgetTree *tree, bool multiselect);
SA_RET_OP_VALID const UIWidgetTreeNode * const * const *ui_WidgetTreeGetSelectedNodes(SA_PARAM_NN_VALID UIWidgetTree *tree);
bool ui_WidgetTreeIsNodeSelected(SA_PARAM_NN_VALID UIWidgetTree *tree, SA_PARAM_NN_VALID UIWidgetTreeNode *node);

void ui_WidgetTreeSetSelectedCallback(SA_PARAM_NN_VALID UIWidgetTree *tree, UIActivationFunc selectedF, UserData selectedData);
void ui_WidgetTreeSetActivatedCallback(SA_PARAM_NN_VALID UIWidgetTree *tree, UIActivationFunc activatedF, UserData activatedData);
void ui_WidgetTreeSetDragCallback(SA_PARAM_NN_VALID UIWidgetTree *tree, UIWidgetTreeDragFunc dragF, UserData dragData);
void ui_WidgetTreeSetContextCallback(SA_PARAM_NN_VALID UIWidgetTree *tree, UIActivationFunc contextF, UserData contextData);
void ui_WidgetTreeEnableDragAndDrop(SA_PARAM_NN_VALID UIWidgetTree *tree);
void ui_WidgetTreeDisableDragAndDrop(SA_PARAM_NN_VALID UIWidgetTree *tree);

F32 ui_WidgetTreeGetHeight(SA_PARAM_NN_VALID UIWidgetTree *tree);
F32 ui_WidgetTreeNodeGetPosition(SA_PARAM_NN_VALID UIWidgetTree *tree, SA_PARAM_NN_VALID UIWidgetTreeNode *node);
void ui_WidgetTreeDrawInternal(SA_PARAM_NN_VALID UIWidgetTree *tree, SA_PARAM_NN_VALID UIWidgetTreeNode *node, F32 *height, F32 startX, F32 startY, F32 endY, F32 width, F32 scale, F32 z, int indent);
void ui_WidgetTreeDraw(SA_PARAM_NN_VALID UIWidgetTree *tree, UI_PARENT_ARGS);
void ui_WidgetTreeTick(SA_PARAM_NN_VALID UIWidgetTree *tree, UI_PARENT_ARGS);
SA_RET_OP_VALID UIWidgetTreeNode *ui_WidgetTreeNodeFind(SA_PARAM_NN_VALID UIWidgetTree *tree, F32 y);
void ui_WidgetTreeSelectAt(SA_PARAM_NN_VALID UIWidgetTree *tree, F32 y);
void ui_WidgetTreeUnselectAll(SA_PARAM_NN_VALID UIWidgetTree *tree);
bool ui_WidgetTreeInput(SA_PARAM_NN_VALID UIWidgetTree *tree, SA_PARAM_NN_VALID KeyInput *input);
void ui_WidgetTreeToggle(UIWidgetTree *tree, UIWidgetTreeNode *node);
void ui_WidgetTreeRefresh(SA_PARAM_NN_VALID UIWidgetTree *tree);
void ui_WidgetTreeExpandAndSelect(UIWidgetTree *tree, const void **expand_list);

#endif
