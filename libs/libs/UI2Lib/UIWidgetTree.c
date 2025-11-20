/***************************************************************************



***************************************************************************/


#include "textparser.h"
#include "MemoryPool.h"

#include "inputMouse.h"

#include "GfxClipper.h"

#include "UIButton.h"
#include "UILabel.h"
#include "UIPane.h"
#include "UIScrollbar.h"
#include "UIWidgetTree.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

void ui_WidgetTreeSetSelected(UIWidgetTree *tree, SA_PARAM_OP_VALID UIWidgetTreeNode *node);

UILabel* ui_WidgetTreeNodeGetTextLabel(SA_PARAM_NN_VALID UIWidgetTreeNode *node)
{
	return node->title;
}

F32 ui_WidgetTreeNodeGetHeight(UIWidgetTreeNode *node)
{
	int i;
	F32 height = node->height;
	if (node->open)
		for (i = 0; i < eaSize(&node->children); i++)
			height += ui_WidgetGetHeight(UI_WIDGET(node->children[i]));
	return height;
}

bool ui_WidgetTreeNodeGetPositionInternal(UIWidgetTreeNode *searching, UIWidgetTreeNode *goal, F32 *height)
{
	int i;
	if (goal == searching)
		return true;
	*height += searching->height;
	for (i = 0; i < eaSize(&searching->children); i++)
		if (ui_WidgetTreeNodeGetPositionInternal(searching->children[i], goal, height))
			return true;
	return false;
}

F32 ui_WidgetTreeNodeGetPosition(UIWidgetTree *tree, UIWidgetTreeNode *node)
{
	F32 height = UI_HSTEP;
	if (ui_WidgetTreeNodeGetPositionInternal(tree->root, node, &height))
		return height;
	else
		return -1;
}

F32 ui_WidgetTreeGetHeight(UIWidgetTree *tree)
{
	return ui_WidgetTreeNodeGetHeight(tree->root) + UI_STEP;
}

void ui_WidgetTreeDraw(UIWidgetTree *tree, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(tree);
	UI_DRAW_EARLY(tree);
	UI_DRAW_LATE(tree);
}

void ui_WidgetTreeNodeDraw(UIWidgetTreeNode *node, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(node);
	UI_DRAW_EARLY(node);
	UI_DRAW_LATE(node);
}

UIWidgetTreeNode *ui_WidgetTreeNodeFindInternal(UIWidgetTreeNode *node, F32 y, F32 *height)
{
	*height += node->height;
	if (*height >= y && node != node->tree->root)
		return node;
	else if (node->children && node->open)
	{
		int i;
		for (i = 0; i < eaSize(&node->children); i++)
		{
			UIWidgetTreeNode *n = ui_WidgetTreeNodeFindInternal(node->children[i], y, height);
			if (n) return n;
		}
	}
	return NULL;
}

// Find the TreeNode currently at the given y offset from the tree itself.
UIWidgetTreeNode *ui_WidgetTreeNodeFind(UIWidgetTree *tree, F32 y)
{
	//F32 height = -(tree->root.height - UI_HSTEP);
	//return ui_WidgetTreeNodeFindInternal(tree->root, y, &height);
	return NULL;
}

F32 ui_WidgetTreeNodeGetYPosInternal(UIWidgetTreeNode *curNode, UIWidgetTreeNode *node, F32 *curY)
{
	if (node == curNode && curNode != curNode->tree->root)
		return *curY;
	*curY += curNode->height;
	if (curNode->children && curNode->open)
	{
		int i;
		for (i = 0; i < eaSize(&curNode->children); i++)
		{
			F32 y = ui_WidgetTreeNodeGetYPosInternal(curNode->children[i], node, curY);
			if (y >= 0.0f) return y;
		}
	}
	return -1.0f;
}

F32 ui_WidgetTreeNodeGetYPos(UIWidgetTree *tree, UIWidgetTreeNode *node)
{
	//F32 curY = -(tree->root.height - UI_HSTEP);
	//return ui_WidgetTreeNodeGetYPosInternal(tree->root, node, &curY);
	return 0;
}

UIWidgetTreeNode *ui_WidgetTreeNodeFindParentInternal(UIWidgetTreeNode *curNode, UIWidgetTreeNode *node)
{
	if (curNode->children)
	{
		int i;
		for (i = 0; i < eaSize(&curNode->children); i++)
		{
			if (curNode->children[i] == node) 
				return curNode;
			else if (curNode->children[i]->children)
			{
				UIWidgetTreeNode *n = ui_WidgetTreeNodeFindParentInternal(curNode->children[i], node);
				if (n) return n;
			}
		}
	}
	return NULL;
}

// Find the parent to the given TreeNode.
UIWidgetTreeNode *ui_WidgetTreeNodeFindParent(UIWidgetTree *tree, UIWidgetTreeNode *node)
{
	return ui_WidgetTreeNodeFindParentInternal(tree->root, node);
}

void ui_WidgetTreeUnselectAll(SA_PARAM_NN_VALID UIWidgetTree *tree)
{
	bool callSelectedCB = (tree->selected != NULL);
	ui_WidgetTreeSetSelected(tree, NULL);
	eaClear(&tree->multiselected);
	eaSetSize(&tree->multiselected, 0);
	if ( callSelectedCB && tree->selectedF )
		tree->selectedF(tree, tree->selectedData);
}

void ui_WidgetTreeSelectAt(UIWidgetTree *tree, F32 y)
{
	UIWidgetTreeNode *node = ui_WidgetTreeNodeFind(tree, y);
	if (!node)
	{
		ui_WidgetTreeUnselectAll(tree);
		return;
	}
	else if (tree->multiselect)
	{
		if (eaFindAndRemoveFast(&tree->multiselected, node) == -1)
		{
			eaPush(&tree->multiselected, node);
			ui_WidgetTreeSetSelected(tree, node);
		}
	}
	else if (node != tree->selected)
		ui_WidgetTreeSetSelected(tree, node);

	if (tree->selectedF)
		tree->selectedF(tree, tree->selectedData);
}

bool ui_WidgetTreeInput(UIWidgetTree *tree, KeyInput *input)
{
	return false;
}

void ui_WidgetTreeToggle(UIWidgetTree *tree, UIWidgetTreeNode *node)
{
	if (node == NULL || node->fillF == NULL)
		return;
	else if (!node->open)
		ui_WidgetTreeNodeExpandAndCallback(node);
	else
		ui_WidgetTreeNodeCollapseAndCallback(node);
}

void ui_WidgetTreeTick(UIWidgetTree *tree, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(tree);
	UI_TICK_EARLY(tree, true, false);  // All processing is done in child widgets
	UI_TICK_LATE(tree);
}

void ui_WidgetTreeNodeFill(UIWidgetTree *tree, UIWidgetTreeNode *node, F32 height)
{
	node->open = false;
	node->height = height;
	node->tree = tree;
}

void ui_WidgetTreeNodeFreeInternal(UIWidgetTreeNode *node)
{
	if(node->freeF)
	{
		node->freeF(node);
	}
	if(node->tree->selected==node)
	{
		ui_WidgetTreeSetSelected(node->tree, NULL);
	}
	eaDestroy(&node->children);
	ui_WidgetFreeInternal(UI_WIDGET(node));
}

UIWidgetTree *ui_WidgetTreeCreate(F32 x, F32 y, F32 w, F32 h)
{
	UIWidgetTree *tree = (UIWidgetTree *)calloc(1, sizeof(UIWidgetTree));
	ui_WidgetTreeInitialize(tree, x, y, w, h);
	return tree;
}

void ui_WidgetTreeInitialize(UIWidgetTree *tree, F32 x, F32 y, F32 w, F32 h)
{
	ui_WidgetInitialize(UI_WIDGET(tree), ui_WidgetTreeTick, ui_WidgetTreeDraw, ui_WidgetTreeFreeInternal, ui_WidgetTreeInput, ui_WidgetDummyFocusFunc);
	ui_WidgetSetPosition(UI_WIDGET(tree), x, y);
	ui_WidgetSetDimensions(UI_WIDGET(tree), w, h);
	tree->widget.sb = ui_ScrollbarCreate(false, true);
	tree->root = ui_WidgetTreeNodeCreate(tree, "", 0, NULL, NULL, NULL, NULL, NULL, NULL, 0);
	ui_WidgetGroupAdd(&tree->widget.children, UI_WIDGET(tree->root));
}

void ui_WidgetTreeFreeInternal(UIWidgetTree *tree)
{
	eaDestroy(&tree->multiselected);
	ui_WidgetFreeInternal(UI_WIDGET(tree));
}

static void ui_WidgetTreeNodeExpandNoCollapse(SA_PARAM_NN_VALID UIWidgetTreeNode *node)
{
	if (node->fillF)
	{
		node->fillF(node, node->fillData);
		node->open = true;
	}
}

void ui_WidgetTreeNodeExpandAndCallback(UIWidgetTreeNode *node)
{
	// Clear out existing children, if any.
	if (node->children)
		ui_WidgetTreeNodeCollapse(node);
	ui_WidgetTreeNodeExpandNoCollapse(node);
	if (node->expandF)
		node->expandF(node, node->expandData);
}

void ui_WidgetTreeNodeExpandGatherOpenHelper(UIWidgetTreeNode *node, void ***openContents)
{
	int i;
	for(i=0; i<eaSize(&node->children); i++)
	{
		UIWidgetTreeNode *child = node->children[i];
		if(child->open)
		{
			eaPush(openContents, child->contents);

			ui_WidgetTreeNodeExpandGatherOpenHelper(child, openContents);
		}
	}
}

void ui_WidgetTreeNodeExpandOpenHelper(UIWidgetTreeNode *node, void ***openContents, void *selected)
{
	int i;
	for(i=0; i<eaSize(&node->children); i++)
	{
		UIWidgetTreeNode *child = node->children[i];

		if(eaFind(openContents, child->contents)!=-1)
		{
			ui_WidgetTreeNodeExpandNoCollapse(child);

			ui_WidgetTreeNodeExpandOpenHelper(child, openContents, selected);
		}

		if(child->contents==selected)
		{
			ui_WidgetTreeSetSelected(node->tree, child);
		}
	}
}

void ui_WidgetTreeNodeExpand(UIWidgetTreeNode *node)
{
	void *data = NULL;
	static void **open = NULL;

	ui_WidgetTreeNodeExpandGatherOpenHelper(node, &open);
	if(node->tree->selected)
	{
		data = node->tree->selected->contents;
	}
	// Clear out existing children, if any.
	if (node->children)
		ui_WidgetTreeNodeCollapse(node);
	ui_WidgetTreeNodeExpandNoCollapse(node);

	ui_WidgetTreeNodeExpandOpenHelper(node, &open, data);
	eaClear(&open);
}

void ui_WidgetTreeNodeCollapseAndCallback(UIWidgetTreeNode *node)
{
	if (node->collapseF)
		node->collapseF(node, node->collapseData);
	ui_WidgetTreeNodeCollapse(node);
}

void ui_WidgetTreeNodeCollapse(UIWidgetTreeNode *node)
{
	eaDestroyEx(&node->children, ui_WidgetTreeNodeFreeInternal);
	node->open = false;
}

int ui_WidgetTreeNodeGetIndentInternal(UIWidgetTreeNode *root, UIWidgetTreeNode *goal)
{
	int i;
	for (i = 0; i < eaSize(&root->children); i++)
	{
		if (goal == root->children[i])
			return 1;
		else
		{
			int indent = ui_WidgetTreeNodeGetIndentInternal(root->children[i], goal);
			if (indent >= 0)
				return indent + 1;
		}
	}
	return -1;
}

int ui_WidgetTreeNodeGetIndent(UIWidgetTreeNode *node)
{
	if (node->tree)
		return ui_WidgetTreeNodeGetIndentInternal(node->tree->root, node);
	else
		return -1;
}

/*
void ui_WidgetTreeNodeAddChildren(UIWidgetTreeNode *node, ParseTable *table, void **contents, UIWidgetTreeChildrenFunc fill,
							UserData fillData, UIWidgetTreeDisplayFunc display, UserData displayData, F32 height)
{	int i;
	for (i = 0; i < eaSize(&contents); i++)
	{
		UIWidgetTreeNode *newNode = ui_WidgetTreeNodeCreate(node->tree, 0, table, contents[i], fill, fillData, 
															display, displayData, height);
		ui_WidgetTreeNodeAddChild(node, newNode);
	}
}
*/

void ui_WidgetTreeNodeAddChild(UIWidgetTreeNode *parent, UIWidgetTreeNode *child)
{
	child->parent = parent;
	eaPush(&parent->children, child);
	ui_WidgetGroupAdd(&parent->node_pane->widget.children, UI_WIDGET(child));
}

bool ui_WidgetTreeNodeRemoveChild(SA_PARAM_NN_VALID UIWidgetTreeNode *parent, SA_PARAM_NN_VALID UIWidgetTreeNode *child)
{
	bool ret = (eaFindAndRemove(&parent->children, child) >= 0);
	if (ret)
	{
		if(parent->tree->selected==child)
		{
			ui_WidgetTreeSetSelected(parent->tree, NULL);
		}
		ui_WidgetGroupRemove(&parent->node_pane->widget.children, UI_WIDGET(child));
		return true;
	}
	return false;
}

bool ui_WidgetTreeNodeRemoveChildByContents(SA_PARAM_NN_VALID UIWidgetTreeNode *parent, UserData contents)
{
	int i;
	for (i = eaSize(&parent->children)-1; i>=0; i--)
		if (parent->children[i]->contents == contents)
			return ui_WidgetTreeNodeRemoveChild(parent, parent->children[i]);
	return false;
}

void ui_WidgetTreeNodeExpandButton(UIAnyWidget *widget, UIWidgetTreeNode *node)
{
	ui_WidgetTreeToggle(node->tree, node);
	ui_ButtonSetText(node->expand, node->open ? "-" : "+");
}

void ui_WidgetTreeSetSelected(UIWidgetTree *tree, SA_PARAM_OP_VALID UIWidgetTreeNode *node)
{
	if(tree->selected)
	{
		ui_PaneSetInvisible(tree->selected->node_pane, 1);
		tree->selected = NULL;
	}
	tree->selected = node;
	if(tree->selected)
	{
		ui_PaneSetInvisible(tree->selected->node_pane, 0);
	}
}

void ui_WidgetTreeNodeTick(UIWidgetTreeNode *node, UI_PARENT_ARGS)
{
	int i;

	if(!node->isvisibleF || node->isvisibleF(node, node->contents))
	{
		F32 height_needed;
		F32 max_padding = 0;
		UI_GET_COORDINATES(node);

		// Calculate padding for userpanes
		for(i=0; i<eaSize(&node->children); i++)
		{
			UIWidgetTreeNode *child = node->children[i];

			if(child->title)
			{
				MAX1(max_padding, ui_WidgetGetNextX(UI_WIDGET(child->title))+UI_STEP);
			}
		}

		node->user_padding = max_padding;

		UI_TICK_EARLY(node, true, false);  // All processing is done in child widgets
		height_needed = 0;

		if(node->parent)
		{
			MAX1(max_padding, node->parent->user_padding+30);
		}
		/*
		for(i=0; i<eaSize(&node->widget.children); i++)
		{
			UIWidget *widget = node->widget.children[i];

			if(widget->heightUnit==UIUnitFixed)
			{
				MAX1(height_needed, widget->height);
			}
		}
		*/
		if(node->info_pane)
		{
			for(i=0; i<eaSize(&node->info_pane->widget.children); i++)
			{
				UIWidget *widget = node->info_pane->widget.children[i];

				if(widget->heightUnit==UIUnitFixed)
				{
					MAX1(height_needed, widget->height);
				}
			}
		}
		if(node->user_pane)
		{
			for(i=0; i<eaSize(&node->user_pane->widget.children); i++)
			{
				UIWidget *widget = node->user_pane->widget.children[i];

				if(widget->heightUnit==UIUnitFixed)
				{
					MAX1(height_needed, widget->height);
				}
			}
		}

		if(node->info_pane)
		{
			ui_WidgetSetHeightEx(UI_WIDGET(node->info_pane), height_needed, UIUnitFixed);
		}

		for(i=0; i<eaSize(&node->children); i++)
		{
			UIWidgetTreeNode *child = node->children[i];
			ui_WidgetSetPosition(UI_WIDGET(child), 20, height_needed);
			height_needed += ui_WidgetGetHeight(UI_WIDGET(child));
			if(child->user_pane && child->title)
			{
				ui_WidgetSetPaddingEx(UI_WIDGET(child->user_pane), max_padding, 0, 0, 0);
			}
		}

		ui_WidgetSetHeightEx(UI_WIDGET(node), height_needed, UIUnitFixed);
		// Changed stuff, so let's redo it for mouse hits
		
		UI_RECALC_COORDINATES(node);

		if(node->tree->root!=node)
		{
			int handled = 0;
			int doubleClickHit = mouseDoubleClickHit(MS_LEFT, &box);
			if (mouseDownHit(MS_LEFT, &box) || mouseDownHit(MS_RIGHT, &box))
			{
				if(node->tree->selected==node)
				{
					if(!doubleClickHit)
					{
						ui_WidgetTreeSetSelected(node->tree, NULL);
					}
				}
				else
				{
					ui_WidgetTreeSetSelected(node->tree, node);
				}
				handled = 1;
			}
			if(doubleClickHit)
			{
				// node should be selected now?
				if(node->tree->activatedF)
				{
					node->tree->activatedF(node, node->tree->activatedData);
				}
				handled = 1;
			}
			if(mouseDownHit(MS_RIGHT, &box))
			{
				if(node->tree->contextF)
				{
					node->tree->contextF(node, node->tree->contextData);
				}
				handled = 1;
			}
			if(handled)
			{
				inpHandled();
			}
		}

		if(node->displayF)
		{
			node->displayF(node, node->user_pane, node->contents);
		}

		UI_TICK_LATE(node);
	}
	else
	{
		ui_WidgetSetHeightEx(UI_WIDGET(node), 0, UIUnitFixed);
	}
}

bool ui_WidgetTreeNodeInput(UIWidgetTree *tree, KeyInput *input)
{
	return false;
}

void ui_WidgetTreeNodeInitialize(UIWidgetTreeNode *node, F32 x, F32 y, F32 w, F32 h)
{
	ui_WidgetInitialize(UI_WIDGET(node), ui_WidgetTreeNodeTick, ui_WidgetTreeNodeDraw, ui_WidgetTreeNodeFreeInternal, ui_WidgetTreeNodeInput, ui_WidgetDummyFocusFunc);
	ui_WidgetSetPosition(UI_WIDGET(node), x, y);
	ui_WidgetSetDimensions(UI_WIDGET(node), w, h);
}

void ui_WidgetTreeNodeFreeCallback(UIWidgetTreeNode *node)
{
	if(node->tree->selected==node)
	{
		ui_WidgetTreeSetSelected(node->tree, NULL);
	}
}

UIWidgetTreeNode *ui_WidgetTreeNodeCreate(	UIWidgetTree *tree,
											const char *name, 
											U32 crc, 
											ParseTable *table, 
											void *contents,
											UIWidgetTreeChildrenFunc fill, 
											UserData fillData,
											UIWidgetTreeDisplayFunc display, 
											UserData displayData,
											F32 height)
{
	UIWidgetTreeNode *node = (UIWidgetTreeNode *)calloc(1, sizeof(UIWidgetTreeNode));
	ui_WidgetTreeNodeInitialize(node, 0, 0, 0, 0);
	ui_WidgetSetFreeCallback(UI_WIDGET(node), ui_WidgetTreeNodeFreeCallback);
	ui_WidgetSetDimensionsEx(UI_WIDGET(node), 1, 1, UIUnitPercentage, UIUnitFitContents);
	ui_WidgetTreeNodeFill(tree, node, height);
	node->crc = crc;
	node->table = table;
	node->contents = contents;
	node->fillF = fill;
	node->fillData = fillData;
	node->displayF = display;
	node->displayData = displayData;

	node->node_pane = ui_PaneCreate(0, 0, 1.0, 1.0, UIUnitPercentage, UIUnitPercentage, 0);
	ui_PaneSetInvisible(node->node_pane, 1);
	ui_WidgetSetClickThrough(UI_WIDGET(node->node_pane), 1);

	if(name && name[0])
	{
		node->title = ui_LabelCreate(name, 20, 0);
		node->expand = ui_ButtonCreate("+", 0, 0, ui_WidgetTreeNodeExpandButton, node);
		if(node->displayF)
		{
			node->user_pane = ui_PaneCreate(0, 0, 1.0, 1.0, UIUnitPercentage, UIUnitPercentage, 0);
			ui_WidgetSetClickThrough(UI_WIDGET(node->user_pane), 1);
			ui_WidgetSetPaddingEx(UI_WIDGET(node->user_pane), 100, 0, 0, 0);
			node->displayF(node, node->user_pane, node->contents);
		}

		node->info_pane = ui_PaneCreate(0, 0, 1.0, 1.0, UIUnitPercentage, UIUnitPercentage, 0);
		ui_WidgetSetClickThrough(UI_WIDGET(node->info_pane), 1);
		ui_PaneSetInvisible(node->info_pane, 1);

		ui_WidgetGroupAdd(&node->info_pane->widget.children, UI_WIDGET(node->title));
		ui_WidgetGroupAdd(&node->info_pane->widget.children, UI_WIDGET(node->expand));
		if(node->user_pane)
		{
			ui_WidgetGroupAdd(&node->info_pane->widget.children, UI_WIDGET(node->user_pane));
		}

		ui_WidgetGroupAdd(&node->node_pane->widget.children, UI_WIDGET(node->info_pane));
	}

	// Add pane to self
	ui_WidgetGroupAdd(&node->widget.children, UI_WIDGET(node->node_pane));

	return node;
}

void ui_WidgetTreeNodeSetFillCallback(UIWidgetTreeNode *node, UIWidgetTreeChildrenFunc fillF, UserData fillData)
{
	node->fillF = fillF;
	node->fillData = fillData;
}

void ui_WidgetTreeNodeSetDisplayCallback(UIWidgetTreeNode *node, UIWidgetTreeDisplayFunc displayF, UserData displayData)
{
	node->displayF = displayF;
	node->displayData = displayData;
}

void ui_WidgetTreeNodeSetFreeCallback(SA_PARAM_NN_VALID UIWidgetTreeNode *node, SA_PARAM_NN_VALID UIFreeFunction freeF)
{
	node->freeF = freeF;
}

void ui_WidgetTreeNodeSetExpandCallback(UIWidgetTreeNode *node, UIWidgetTreeChildrenFunc expandF, UserData expandData)
{
	node->expandF = expandF;
	node->expandData = expandData;
}

void ui_WidgetTreeNodeSetIsVisibleCallback(SA_PARAM_NN_VALID UIWidgetTreeNode *node, UIWidgetTreeNodeIsVisibleFunc isvizfunc)
{
	node->isvisibleF = isvizfunc;
}

void ui_WidgetTreeNodeSetCollapseCallback(UIWidgetTreeNode *node, UIWidgetTreeChildrenFunc collapseF, UserData collapseData)
{
	node->collapseF = collapseF;
	node->collapseData = collapseData;
}

void ui_WidgetTreeSetActivatedCallback(UIWidgetTree *tree, UIActivationFunc activatedF, UserData activatedData)
{
	tree->activatedF = activatedF;
	tree->activatedData = activatedData;
}

void ui_WidgetTreeSetDragCallback(SA_PARAM_NN_VALID UIWidgetTree *tree, UIWidgetTreeDragFunc dragF, UserData dragData)
{
	tree->dragF = dragF;
	tree->dragData = dragData;
}

void ui_WidgetTreeSetSelectedCallback(UIWidgetTree *tree, UIActivationFunc selectedF, UserData selectedData)
{
	tree->selectedF = selectedF;
	tree->selectedData = selectedData;
}

void ui_WidgetTreeSetContextCallback(UIWidgetTree *tree, UIActivationFunc contextF, UserData contextData)
{
	tree->widget.contextF = contextF;
	tree->widget.contextData = contextData;
}

void ui_WidgetTreeEnableDragAndDrop(SA_PARAM_NN_VALID UIWidgetTree *tree)
{
	tree->itemDraggingEnabled = 1;
}

void ui_WidgetTreeDisableDragAndDrop(SA_PARAM_NN_VALID UIWidgetTree *tree)
{
	tree->itemDraggingEnabled = 0;
}

UIWidgetTreeNode *ui_WidgetTreeGetSelected(UIWidgetTree *tree)
{
	return tree->selected;
}

void ui_WidgetTreeSetMultiselect(UIWidgetTree *tree, bool multiselect)
{
	tree->multiselect = multiselect;
	if (!multiselect)
		eaDestroy(&tree->multiselected);
}

const UIWidgetTreeNode * const * const *ui_WidgetTreeGetSelectedNodes(UIWidgetTree *tree)
{
	if (!tree->multiselect)
	{
		eaDestroy(&tree->multiselected);
		if (tree->selected)
			eaPush(&tree->multiselected, tree->selected);
	}
	return &tree->multiselected;
}

bool ui_WidgetTreeIsNodeSelected(UIWidgetTree *tree, UIWidgetTreeNode *node)
{
	if (!tree->multiselect)
		return (tree->selected == node);
	else
		return (eaFind(&tree->multiselected, node) >= 0);
}

//////////////////////////////////////////////////////////////////////////

typedef struct UIWidgetTreeRefreshNode UIWidgetTreeRefreshNode;
struct UIWidgetTreeRefreshNode
{
	U32 nodeCrc;
	bool open;
	UIWidgetTreeRefreshNode **children;
};

MP_DEFINE(UIWidgetTreeRefreshNode);

static UIWidgetTreeRefreshNode *createRefreshNode(SA_PARAM_NN_VALID UIWidgetTreeNode *tree_node)
{
	UIWidgetTreeRefreshNode *refresh_node;
	int i;

	MP_CREATE(UIWidgetTreeRefreshNode, 256);
	refresh_node = MP_ALLOC(UIWidgetTreeRefreshNode);
	refresh_node->nodeCrc = tree_node->crc;
	refresh_node->open = tree_node->open;

	for (i = 0; i < eaSize(&tree_node->children); ++i)
	{
		UIWidgetTreeRefreshNode *child;
		if (tree_node->children[i] && tree_node->children[i]->crc)
		{
			child = createRefreshNode(tree_node->children[i]);
			if (child)
				eaPush(&refresh_node->children, child);
		}
	}

	return refresh_node;
}

static void destroyRefreshNode(SA_PARAM_OP_VALID UIWidgetTreeRefreshNode *refresh_node)
{
	if (!refresh_node)
		return;
	eaDestroyEx(&refresh_node->children, destroyRefreshNode);
	MP_FREE(UIWidgetTreeRefreshNode, refresh_node);
}

static void openNodeChildren(SA_PARAM_OP_VALID UIWidgetTreeNode *tree_node, SA_PARAM_OP_VALID UIWidgetTreeRefreshNode *refresh_node)
{
	int i, j;

	if (!tree_node || !refresh_node)
		return;

	for (i = 0; i < eaSize(&tree_node->children); ++i)
	{
		UIWidgetTreeNode *child_node = tree_node->children[i];
		for (j = 0; j < eaSize(&refresh_node->children); ++j)
		{
			UIWidgetTreeRefreshNode *child_refresh_node = refresh_node->children[j];
			if (child_node->children || (child_node->crc == child_refresh_node->nodeCrc && child_refresh_node->open))
			{
				ui_WidgetTreeNodeExpandNoCollapse(child_node);
				openNodeChildren(child_node, child_refresh_node);
				break;
			}
		}
	}
}

void ui_WidgetTreeRefresh(UIWidgetTree *tree)
{
	UIWidgetTreeRefreshNode *refresh_root;

	// create a parallel tree of open nodes
	refresh_root = createRefreshNode(tree->root);

	// close tree
	ui_WidgetTreeNodeCollapse(tree->root);
	ui_WidgetTreeNodeExpand(tree->root);

	// open nodes that are in the refresh tree
	openNodeChildren(tree->root, refresh_root);

	// destroy refresh tree
	destroyRefreshNode(refresh_root);
}

void ui_WidgetTree_expand_and_select_helper(UIWidgetTree *tree, UIWidgetTreeNode *tree_node, const void **expand_list)
{
	if (!tree_node->open)
		ui_WidgetTreeNodeExpand(tree_node); // Only calling it !open so that we don't close siblings.  Not calling ui_WidgetTreeNodeExpandNoCollapse because it would fill twice.
	if (expand_list && expand_list[0])
	{
		int i;
		for (i = 0; i < eaSize(&tree_node->children); ++i)
		{
			if (tree_node->children[i]->contents == expand_list[0])
			{
				ui_WidgetTree_expand_and_select_helper(tree, tree_node->children[i], &expand_list[1]);
				return;
			}
		}
	}

	if (tree_node != tree->root)
		ui_WidgetTreeSetSelected(tree, tree_node);
	else
		ui_WidgetTreeSetSelected(tree, NULL);

	if (tree->selectedF)
		tree->selectedF(tree, tree->selectedData);
	return;
}

void ui_WidgetTreeExpandAndSelect(UIWidgetTree *tree, const void **expand_list)
{
	ui_WidgetTree_expand_and_select_helper(tree, tree->root, expand_list);
}
