/***************************************************************************



***************************************************************************/


#include "textparser.h"
#include "MemoryPool.h"

#include "inputLib.h"
#include "inputMouse.h"
#include "inputText.h"

#include "GfxClipper.h"
#include "GfxSprite.h"
#include "GfxSpriteText.h"
#include "GfxTexAtlas.h"
#include "GfxPrimitive.h"

#include "UIScrollbar.h"
#include "UITree.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

#define DROP_CENTER_RANGE  0.25
#define AUTO_SCROLL_RATE   120

void ui_TreeSelectAllInternal(UITree *tree, UITreeNode *node);

static bool ui_TreeNodeIsSelectable( UITreeNode* node )
{
	return node != &node->tree->root && node->allow_selection;
}

F32 ui_TreeNodeGetHeight(UITreeNode *node)
{
	int i;
	F32 height = node->height;
	if (node->open)
		for (i = 0; i < eaSize(&node->children); i++)
			height += ui_TreeNodeGetHeight(node->children[i]);
	return height;
}

bool ui_TreeNodeGetPositionInternal(UITreeNode *searching, UITreeNode *goal, F32 *height)
{
	int i;
	if (goal == searching)
		return true;
	*height += searching->height;
	for (i = 0; i < eaSize(&searching->children); i++)
		if (ui_TreeNodeGetPositionInternal(searching->children[i], goal, height))
			return true;
	return false;
}

F32 ui_TreeNodeGetPosition(UITree *tree, UITreeNode *node)
{
	F32 height = UI_HSTEP;
	if (ui_TreeNodeGetPositionInternal(&tree->root, node, &height))
		return height;
	else
		return -1;
}

F32 ui_TreeNodeGetYPosInternal(UITreeNode *curNode, UITreeNode *node, F32 *curY)
{
	if (node == curNode && curNode != &curNode->tree->root)
		return *curY;
	*curY += curNode->height;
	if (curNode->children && curNode->open)
	{
		int i;
		for (i = 0; i < eaSize(&curNode->children); i++)
		{
			F32 y = ui_TreeNodeGetYPosInternal(curNode->children[i], node, curY);
			if (y >= 0.0f) return y;
		}
	}
	return -1.0f;
}

F32 ui_TreeNodeGetYPos(UITree *tree, UITreeNode *node)
{
	F32 curY = -(tree->root.height);
	return ui_TreeNodeGetYPosInternal(&tree->root, node, &curY);
}

F32 ui_TreeGetHeight(UITree *tree)
{
	return ui_TreeNodeGetHeight(&tree->root) + UI_STEP;
}

static bool ui_TreeMouseOverNodeCenter(F32 yPos, F32 height, F32 scale, bool isOpen)
{
	if(isOpen) {
		if(	g_ui_State.mouseY >= yPos - height * scale * DROP_CENTER_RANGE && 
			g_ui_State.mouseY <  yPos + height * scale * 0.5)
			return true;
	} else {
		if(	g_ui_State.mouseY >= yPos - height * scale * DROP_CENTER_RANGE && 
			g_ui_State.mouseY <= yPos + height * scale * DROP_CENTER_RANGE)
			return true;
	}
	return false;
}

static void ui_TreeSelectedFillDrawingDescription( UITree* tree, UIDrawingDescription* desc )
{
	UISkin* skin = UI_GET_SKIN( tree );
		
	if( skin->bUseStyleBorders || skin->bUseTextureAssemblies ) {
		const char* descName = skin->astrTreeStyleSelectedItem;
		if( skin->bUseTextureAssemblies && RefSystem_ReferentFromString( "UITextureAssembly", descName )) {
			desc->textureAssemblyName = descName;
		} else {
			desc->styleBorderName = descName;
		}
	} else {
		desc->textureNameUsingLegacyColor = "white";
	}
}

UIStyleFont* ui_TreeItemGetFont( UITree* pTree, bool bSelected, bool bHover )
{
	UISkin* skin = UI_GET_SKIN( pTree );
	UIStyleFont* pFont = NULL;

	if( bSelected || bHover ) {
		pFont = GET_REF( skin->hTreeItemFontSelectedItem );
	} else {
		pFont = GET_REF( skin->hTreeItemFont );
	}

	if( !pFont ) {
		pFont = ui_WidgetGetFont( UI_WIDGET( pTree ));
	}

	return pFont;
}

static UIStyleFont* ui_TreeDefaultGetFont( UITree* tree )
{
	UISkin* skin = UI_GET_SKIN( tree );
	UIStyleFont* pFont = NULL;

	pFont = GET_REF( skin->hTreeDefaultFont );

	if( !pFont ) {
		pFont = ui_WidgetGetFont( UI_WIDGET( tree ));
	}

	return pFont;
}

static Color ui_TreeGetLineColor(UITree *tree)
{
	UISkin *skin = UI_GET_SKIN(tree);
	if(skin->bUseStyleBorders || skin->bUseTextureAssemblies) {
		return skin->cTreeLine;
	}
	return ColorHalfBlack;
}

static Color ui_TreeGetLineDragColor(UITree *tree)
{
	UISkin *skin = UI_GET_SKIN(tree);
	if(skin->bUseStyleBorders || skin->bUseTextureAssemblies) {
		return skin->cTreeLineDrag;
	}
	return colorFromRGBA(0x00000080);
}

// Returns true if all children of this node have no children
//
// In this case, those nodes should draw in "list" mode, without the padding for the +/- icons
static bool ui_TreeNodeOnlyOneLevel( UITreeNode* node )
{
	int i;
	for( i = 0; i != eaSize( &node->children ); ++i ) {
		if( node->children[i]->fillF ) {
			return false;
		}
	}

	return true;
}

static void ui_TreeDrawInternal(UITree *tree, UITreeNode *node, F32 *height, F32 startX, F32 startY, F32 endY, F32 width, F32 z, F32 scale, int indent)
{
	// Notes on the unorthodox naming of the parameters:  
	//      "startX,height" correspond to the x,y of the current node (which may not be in the visible part of the tree control)
	//      "startY" and "endY" are the top and bottom of the entire tree control respectively
	//		"width" is the width of tree contents (which may be wider than the actual tree control)
	// startX, startY, endY, width, are already scaled.
	UISkin* pSkin = UI_GET_SKIN(tree);
	AtlasTex *opened = atlasFindTexture(pSkin->pchMinus), *closed = atlasFindTexture(pSkin->pchPlus);
	F32 indentOffset = (UI_STEP + opened->width) * scale;
	F32 drawX = startX + indent * indentOffset;
	F32 drawY = *height;
	bool draw;
	CBox box;
	Color c;

	*height += node->height * scale;
	draw = (*height > startY);
	if (draw)
	{
		node->displayF(node, node->displayData, drawX, drawY, width - (indent * indentOffset), node->height * scale, scale, z+0.01f);
		ui_WidgetGroupDraw(&node->widgets, drawX, drawY, width - (indent * indentOffset), node->height * scale, scale);
		if ((!node->tree->multiselect && node->tree->selected == node) ||
			(node->tree->multiselect && eaFind(&node->tree->multiselected, node) >= 0))
		{
			UIDrawingDescription desc = { 0 };
			CBox selectedBox;
			ui_TreeSelectedFillDrawingDescription( tree, &desc );
			BuildCBox(&selectedBox, startX + 2.0, *height - node->height * scale, width-1, node->height * scale);
			if (UI_GET_SKIN(node->tree))
				c = UI_GET_SKIN(node->tree)->background[1];
			else
				c = ColorOrange;
			ui_DrawingDescriptionDraw( &desc, &selectedBox, scale, z - 0.08, 255, c, ColorBlack );
		}
	}
	BuildCBox(&box, (startX + indentOffset * (indent - 0.5)) - opened->width * scale / 2.0, *height - (node->height * scale / 2 + opened->height * scale / 2), opened->width * scale, opened->height * scale);
	CBoxFloor(&box);

	if (pSkin && !pSkin->bUseStyleBorders && !pSkin->bUseTextureAssemblies)
		c = UI_GET_SKIN(node->tree)->button[0];
	else
		c = ColorWhite;

	if (node->open)
	{
		int i;
		F32 lineStart = drawY + node->height * scale / 2.0;
		F32 lineY = 0.f;
		bool listMode = false;
		if (draw)
			display_sprite_box(opened, &box, z, RGBAFromColor(c));
		for (i = 0; i < eaSize(&node->children); i++)
		{
			lineY = *height + node->children[i]->height * scale / 2.0;
			ui_TreeDrawInternal(tree, node->children[i], height, startX, startY, endY, width, z, scale,
								(listMode ? indent : indent + 1) );
			if (indent && !node->children[i]->hide_line)
				gfxDrawLine(drawX - indentOffset / 2.0, lineY, z - 0.002, drawX + indentOffset / 2.0, lineY, ui_TreeGetLineColor(tree));
			if (tree->dragging && (g_ui_State.mouseX >= startX) && (g_ui_State.mouseX <= startX + width))
			{
				if (g_ui_State.mouseY < lineY - node->children[i]->height * scale * 0.5 ||
					g_ui_State.mouseY > lineY + node->children[i]->height * scale * 0.5)
				{
					// Then outside this line and ignore it
				}
				else if (tree->bAllowDropNode && ui_TreeMouseOverNodeCenter(lineY, node->children[i]->height, scale, node->children[i]->open))
				{
					CBox selectedBox;
					if (UI_GET_SKIN(node->tree))
						c = UI_GET_SKIN(node->tree)->background[1];
					else
						c = ColorOrange;
					BuildCBox(&selectedBox, drawX, *height - node->height * scale, width, node->height * scale);
					display_sprite_box(atlasFindTexture("white"), &selectedBox, z - 0.003, RGBAFromColor(c));
				}
				else if (g_ui_State.mouseY > lineY - node->children[i]->height * scale * 0.5 && 
						 g_ui_State.mouseY < lineY - node->children[i]->height * scale * DROP_CENTER_RANGE)
				{
					F32 lineDrawY = lineY - node->children[i]->height * scale / 2.0;
					gfxDrawLine(drawX, lineDrawY, z - 0.002, drawX + width, lineDrawY, ui_TreeGetLineDragColor(tree));
				}
				else if (g_ui_State.mouseY > lineY + node->children[i]->height * scale * DROP_CENTER_RANGE && 
						 g_ui_State.mouseY < lineY + node->children[i]->height * scale * 0.5)
				{
					F32 lineDrawY = lineY + node->children[i]->height * scale / 2.0;
					gfxDrawLine(drawX, lineDrawY, z - 0.002, drawX + width, lineDrawY, ui_TreeGetLineDragColor(tree));
				}
			}
			if (*height > endY)
				break;
		}

		if (eaSize(&node->children))
		{
			if (i < eaSize(&node->children) - 1)
				lineY += *height;
			gfxDrawLine(drawX - indentOffset / 2.0, lineStart, z - 0.002, drawX - indentOffset / 2.0, lineY, ui_TreeGetLineColor(tree));
		}

	}
	else if (!node->open && node->fillF && draw)
		display_sprite_box(closed, &box, z, RGBAFromColor(c));
}

static void ui_TreeFillDrawingDescription( UITree* tree, UIDrawingDescription* desc )
{
	UISkin* skin = UI_GET_SKIN( tree );
		
	if( skin->bUseStyleBorders || skin->bUseTextureAssemblies ) {
		const char* descName = skin->astrTreeStyle;
		if( skin->bUseTextureAssemblies && RefSystem_ReferentFromString( "UITextureAssembly", descName )) {
			desc->textureAssemblyName = descName;
		} else {
			desc->styleBorderName = descName;
		}
	} else {
		desc->overlayOutlineUsingLegacyColor2 = true;
	}
}

void ui_TreeDraw(UITree *tree, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(tree);
	UISkin* pSkin = UI_GET_SKIN(tree);
	F32 startX, startY, endY, height, width;
//	F32 xSize, xPos, ySize, yPos;
	int i;
	bool bUseColor = true;
	UIDrawingDescription desc = { 0 };
	Color border;
	if (UI_GET_SKIN(tree))
		border = UI_GET_SKIN(tree)->thinBorder[0];
	else
		border = ColorBlack;
	ui_TreeFillDrawingDescription( tree, &desc );
	ui_DrawingDescriptionDraw( &desc, &box, scale, z + 0.0001, 255, ColorBlack, border );
	ui_DrawingDescriptionInnerBoxCoords( &desc, &x, &y, &w, &h, scale );
	
	// Notes on the unorthodox naming of these variables:  
	//      "startX,height" correspond to the x,y of the first node (which may not be in the visible part of the tree control)
	//      "startY" and "endY" are the top and bottom of the entire tree control respectively
	//		"width" is the width of tree contents (which may be wider than the actual tree control)
	startX = x - tree->widget.sb->xpos;
	startY = y;
	endY = y + h;
	height = (y - tree->widget.sb->ypos) + UI_HSTEP_SC;
	width = MAX(tree->width * scale, w);

	w -= ui_ScrollbarWidth(UI_WIDGET(tree)->sb) * scale;
	tree->widget.sb->scrollX = (tree->width > 0);
	if (tree->widget.sb->scrollX && w < tree->width * scale)
		h -= ui_ScrollbarHeight(UI_WIDGET(tree)->sb) * scale;
	BuildCBox(&box, x, y, w, h);
	CBoxClipTo(&pBox, &box);
	clipperPushRestrict(&box);

	if (!eaSize(&tree->root.children))
	{
		const char* widgetText = ui_WidgetGetText( UI_WIDGET( tree ));
		if (widgetText) {
			ui_StyleFontUse( ui_TreeDefaultGetFont( tree ), false, tree->widget.state );
			gfxfont_Printf(x + w / 2, y + h / 2, z + 0.002, scale, scale, CENTER_XY, "%s", widgetText);
		}
	}
	else
	{
		bool listMode = ui_TreeNodeOnlyOneLevel( &tree->root );
		
		for (i = 0; i < eaSize(&tree->root.children); i++)
		{
			UITreeNode *node = tree->root.children[i];
			if (tree->dragging && (g_ui_State.mouseX >= startX) && (g_ui_State.mouseX <= startX + w))
			{	
				if (tree->bAllowDropNode && ui_TreeMouseOverNodeCenter(height + node->height*scale/2.0f, node->height, scale, true))
				{
					Color c;
					CBox selectedBox;
					AtlasTex *opened = atlasFindTexture(pSkin->pchMinus), *closed = atlasFindTexture(pSkin->pchPlus);
					F32 indentOffset = (UI_STEP + opened->width) * scale;
					F32 drawX = startX + 1 * indentOffset;

					if (UI_GET_SKIN(node->tree))
						c = UI_GET_SKIN(node->tree)->background[1];
					else
						c = ColorOrange;
					BuildCBox(&selectedBox, drawX, height, w, node->height * scale);
					display_sprite_box(atlasFindTexture("white"), &selectedBox, z - 0.003, RGBAFromColor(c));
				}
			}
			ui_TreeDrawInternal(tree, tree->root.children[i], &height, startX, startY, endY, width, z + 0.1, scale,
								(listMode ? 0 : 1));
			if (height > endY)
				break;
		}
	}

	clipperPop();
	ui_DrawAndDecrementOverlay(UI_WIDGET(tree), &box, z);
	ui_ScrollbarDraw(tree->widget.sb, x, y, w, h, z, scale, tree->width > 0 ? tree->width * scale : w, ui_TreeGetHeight(tree) * scale);
}

UITreeNode *ui_TreeNodeFindInternal(UITreeNode *node, F32 y, F32 *height)
{
	*height += node->height;
	if (*height >= y && node != &node->tree->root)
		return node;
	else if (node->children && node->open)
	{
		int i;
		for (i = 0; i < eaSize(&node->children); i++)
		{
			UITreeNode *n = ui_TreeNodeFindInternal(node->children[i], y, height);
			if (n) return n;
		}
	}
	return NULL;
}

// Find the TreeNode currently at the given y offset from the tree itself.
UITreeNode *ui_TreeNodeFind(UITree *tree, F32 y)
{
	F32 height = -(tree->root.height - UI_HSTEP);
	return ui_TreeNodeFindInternal(&tree->root, y, &height);
}

UITreeNode *ui_TreeNodeFindParentInternal(UITreeNode *curNode, UITreeNode *node)
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
				UITreeNode *n = ui_TreeNodeFindParentInternal(curNode->children[i], node);
				if (n) return n;
			}
		}
	}
	return NULL;
}

// Find the parent to the given TreeNode.
UITreeNode *ui_TreeNodeFindParent(UITree *tree, UITreeNode *node)
{
	return ui_TreeNodeFindParentInternal(&tree->root, node);
}

void ui_TreeUnselectAll(SA_PARAM_NN_VALID UITree *tree)
{
	bool callSelectedCB = (tree->selected != NULL);
	tree->selected = NULL;
	eaClear(&tree->multiselected);
	eaSetSize(&tree->multiselected, 0);
	if ( callSelectedCB && tree->selectedF )
		tree->selectedF(tree, tree->selectedData);
}

void ui_TreeSelectNode(UITree *tree, UITreeNode *node)
{
	if(!ui_TreeNodeIsSelectable(node)) {
		return;
	}
	
	if(tree->multiselect)
	{
		if (eaFindAndRemoveFast(&tree->multiselected, node) == -1)
		{
			eaPush(&tree->multiselected, node);
			tree->selected = node;
		}
	}
	else if(node != tree->selected)
		tree->selected = node;

	if (tree->selectedF)
		tree->selectedF(tree, tree->selectedData);
}

void ui_TreeSelectAt(UITree *tree, F32 y)
{
	UITreeNode *node = ui_TreeNodeFind(tree, y);
	if (!node)
	{
		ui_TreeUnselectAll(tree);
		return;
	}
	
	ui_TreeSelectNode(tree, node);
}

void ui_TreeSelectAllInternal(UITree *tree, UITreeNode *node)
{
	if(node)	
	{
		int i;
		if(ui_TreeNodeIsSelectable(node))
			eaPush(&tree->multiselected, node);

		for(i = 0; i < eaSize(&node->children); i++)
		{
			ui_TreeSelectAllInternal(tree, node->children[i]);
		}
	}
}

static void ui_TreeSelectLeavesInternal(UITree *tree, UITreeNode *node, bool include_sub_nodes)
{
	if(node)	
	{
		int i;
		if(!node->fillF && ui_TreeNodeIsSelectable(node))
			eaPush(&tree->multiselected, node);

		if(include_sub_nodes)
		{
			for(i = 0; i < eaSize(&node->children); i++)
				ui_TreeSelectLeavesInternal(tree, node->children[i], include_sub_nodes);
		}
	}
}

void ui_TreeSelectLeaves(SA_PARAM_NN_VALID UITree *tree, SA_PARAM_NN_VALID UITreeNode *node, bool include_sub_nodes)
{
	if(tree->multiselect)
	{
		int i;
		for(i = 0; i < eaSize(&node->children); i++)
			ui_TreeSelectLeavesInternal(tree, node->children[i], include_sub_nodes);

		// TODO: perhaps add an additional select all callback
		if(tree->selectedF)
			tree->selectedF(tree, tree->selectedData);
	}
}

void ui_TreeSelectAll(UITree *tree)
{
	if(tree->multiselect)
	{
		eaClear(&tree->multiselected);
		ui_TreeSelectAllInternal(tree, &tree->root);

		// TODO: perhaps add an additional select all callback
		if(tree->selectedF)
			tree->selectedF(tree, tree->selectedData);
	}
}

bool ui_TreeInput(UITree *tree, KeyInput *input)
{
	if (input->type == KIT_EditKey)
	{
		if (input->scancode == INP_TAB)
			return false;
		else if (input->scancode == INP_UP && tree->selected)
		{
			F32 current = ui_TreeNodeGetPosition(tree, tree->selected);
			F32 newOffset = current - 1;
			if (newOffset > 0)
			{
				eaClear(&tree->multiselected);
				ui_TreeSelectAt(tree, newOffset);
				tree->scrollToSelected = true;
			}
			return true;
		}
		else if (input->scancode == INP_DOWN && tree->selected)
		{
			F32 current = ui_TreeNodeGetPosition(tree, tree->selected);
			F32 newOffset = current + tree->selected->height + 1;
			if (newOffset > 0 && ui_TreeNodeFind(tree, newOffset))
			{
				eaClear(&tree->multiselected);
				ui_TreeSelectAt(tree, newOffset);
				tree->scrollToSelected = true;
			}
			return true;
		}
		else if (input->scancode == INP_RIGHT && tree->selected)
		{
			F32 current = ui_TreeNodeGetPosition(tree, tree->selected);
			F32 newOffset = current + tree->selected->height + 1;

			if(!eaSize(&tree->selected->children))
				ui_TreeNodeExpandAndCallback(tree->selected);

			if (eaSize(&tree->selected->children) && newOffset > 0 && ui_TreeNodeFind(tree, newOffset))
			{
				eaClear(&tree->multiselected);
				ui_TreeSelectAt(tree, newOffset);
				tree->scrollToSelected = true;
			}
			return true;
		}
		else if (input->scancode == INP_LEFT && tree->selected)
		{
			if(eaSize(&tree->selected->children))
				ui_TreeNodeCollapseAndCallback(tree->selected);
			else if(tree->selected->parent)
				ui_TreeSelectNode(tree, tree->selected->parent);

			return true;
		}
		else if (input->scancode == INP_RETURN && tree->selected)
		{
			if (tree->activatedF)
				tree->activatedF(tree, tree->activatedData);
			return true;
		}
		else if (input->scancode == INP_A && (input->attrib & KIA_CONTROL) && tree->multiselect)
		{
			eaClear(&tree->multiselected);
			ui_TreeSelectAllInternal(tree, &tree->root);

			if (tree->selectedF)
				tree->selectedF(tree, tree->selectedData);

			return true;
		}
		else
			return false;
	}
	else
		return false;
}

void ui_TreeToggle(UITree *tree, F32 yClicked)
{
	UITreeNode *node = ui_TreeNodeFind(tree, yClicked);
	if (node == NULL || node->fillF == NULL)
		return;
	else if (!node->open)
		ui_TreeNodeExpandAndCallback(node);
	else
		ui_TreeNodeCollapseAndCallback(node);
}

void ui_TreeTickInternal(UITree *tree, UITreeNode *node, F32 *height, F32 startX, F32 startY, F32 endY, F32 width, F32 scale, int indent)
{
	// startX, startY, endY, width, are already scaled.
	UISkin* pSkin = UI_GET_SKIN( tree );
	AtlasTex *opened = atlasFindTexture(pSkin->pchMinus);
	F32 indentOffset = (UI_STEP + opened->width) * scale;
	F32 tickX = startX + indent * indentOffset;
	F32 tickY = *height;
	CBox box;

	*height += node->height * scale;
	BuildCBox(&box, tickX, tickY, width - (indent * indentOffset), node->height * scale);
	CBoxFloor(&box);
	mouseClipPushRestrict(&box);
	tree->isInWidgetTick = true;
	ui_WidgetGroupTick(&node->widgets, tickX, tickY, width - (indent * indentOffset), node->height * scale, scale);
	tree->isInWidgetTick = false;
	mouseClipPop();

	if (node->open)
	{
		int i;
		for (i = 0; i < eaSize(&node->children); i++)
			ui_TreeTickInternal(tree, node->children[i], height, startX, startY, endY, width, scale, indent + 1);
	}
}

void ui_TreeTick(UITree *tree, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(tree);
	UISkin* pSkin = UI_GET_SKIN( tree );
	AtlasTex *opened = atlasFindTexture(pSkin->pchMinus);
	bool bUseColor = true;
	F32 indentOffset = (UI_STEP + opened->width) * scale;
	F32 yClicked, startX, startY, endY, height;
	int i;
	UIDrawingDescription desc = { 0 };
	ui_TreeFillDrawingDescription( tree, &desc );
	ui_DrawingDescriptionInnerBoxCoords( &desc, &x, &y, &w, &h, scale );

	yClicked = (g_ui_State.mouseY - y) + tree->widget.sb->ypos;
	startX = x - tree->widget.sb->xpos;
	startY = y;
	endY = y + h;
	height = (y - tree->widget.sb->ypos) + UI_HSTEP_SC;

	w -= ui_ScrollbarWidth(UI_WIDGET(tree)->sb) * scale;
	tree->widget.sb->scrollX = (tree->width > 0);
	if (tree->widget.sb->scrollX && w < tree->width * scale)
		h -= ui_ScrollbarHeight(UI_WIDGET(tree)->sb) * scale;

	BuildCBox(&box, x, y, w, h);

	mouseClipPushRestrict(&box);
	for (i = 0; i < eaSize(&tree->root.children); i++)
		ui_TreeTickInternal(tree, tree->root.children[i], &height, startX, startY, endY, w, scale, 1);
	mouseClipPop();

	if (tree->scrollToSelected)
	{
		F32 pos = ui_TreeNodeGetPosition(tree, tree->selected) * scale;
		F32 bottom = tree->widget.sb->ypos + h;
		if (pos < tree->widget.sb->ypos)
			tree->widget.sb->ypos = pos;
		else if (pos + tree->selected->height * scale > bottom)
			tree->widget.sb->ypos = (pos + tree->selected->height * scale) - h;
		tree->scrollToSelected = false;
	}
	height = ui_TreeGetHeight(tree);
	ui_ScrollbarTick(tree->widget.sb, x, y, w, h, z, scale, tree->width > 0 ? tree->width * scale : w, height * scale);

	UI_TICK_EARLY(tree, false, true);

	if (mouseDoubleClickHit(MS_LEFT, &box))
	{
		// This is kind of inefficient, we end up scanning the tree three times for the node.
		UITreeNode *node = ui_TreeNodeFind(tree, yClicked / scale);
		if (node)
		{
			S32 indent = ui_TreeNodeGetIndent(node);
			ui_SetFocus(tree);
			BuildCBox(&box, ((x - UI_WIDGET(tree)->sb->xpos) + indentOffset * (indent - 0.5)) - opened->width * scale / 2, y, opened->width * scale, h);
			if (mouseDoubleClickHit(MS_LEFT, &box) && node->fillF) // Within +/- toggle bounds
			{
				if (!node->open)
					ui_TreeNodeExpandAndCallback(node);
				else
					ui_TreeNodeCollapseAndCallback(node);
			}
			else if (tree->activatedF)
			{
				eaClear(&tree->multiselected);
				ui_TreeSelectAt(tree, yClicked / scale);
				tree->activatedF(tree, tree->activatedData);
			}
		}
		inpHandled();
	}
	else if (mouseDownHit(MS_LEFT, &box))
	{
		// This is kind of inefficient, we end up scanning the tree three times for the node.
		UITreeNode *node = ui_TreeNodeFind(tree, yClicked / scale);
		if (node)
		{
			S32 indent = ui_TreeNodeGetIndent(node);
			ui_SetFocus(tree);
			BuildCBox(&box, ((x - UI_WIDGET(tree)->sb->xpos) + indentOffset * (indent - 0.5)) - opened->width * scale / 2, y, opened->width * scale, h);
			if (mouseDownHit(MS_LEFT, &box)) // Within +/- toggle bounds
				ui_TreeToggle(tree, yClicked / scale);
			else
			{
				if (!inpLevelPeek(INP_CONTROL))
					eaClear(&tree->multiselected);
				ui_TreeSelectAt(tree, yClicked / scale);
			}
		}
		else
			ui_TreeUnselectAll(tree);
		inpHandled();
	}
	else if (mouseDownHit(MS_RIGHT, &box))
	{
		UITreeNode *node;
		ui_SetFocus(tree);
		node = ui_TreeNodeFind(tree, yClicked / scale);
		if(!node)
		{
			eaClear(&tree->multiselected);
		}
		else if(!ui_TreeIsNodeSelected(tree, node))
		{
			if (!inpLevelPeek(INP_CONTROL))
				eaClear(&tree->multiselected);
			ui_TreeSelectAt(tree, yClicked / scale);
		}
		
		if (tree->widget.contextF)
			tree->widget.contextF(tree, tree->widget.contextData);
		inpHandled();
	}
	else if (tree->dragging && mouseIsDown(MS_LEFT) &&  mouseCollision(&box))
	{
		if (g_ui_State.mouseY - y < UI_STEP)
		{
			tree->widget.sb->ypos -= g_ui_State.timestep * AUTO_SCROLL_RATE;
			if (tree->widget.sb->ypos < 0)
				tree->widget.sb->ypos = 0;
		}
		else if (y + h - g_ui_State.mouseY < UI_STEP)
		{
			tree->widget.sb->ypos += g_ui_State.timestep * AUTO_SCROLL_RATE;
		}
		inpHandled();
	}
	else if (tree->itemDraggingEnabled && mouseDrag(MS_LEFT) && mouseCollision(&box))
	{
		tree->dragging = tree->selected;
		inpHandled();
	}
	else if (tree->itemDraggingEnabled && tree->dragging && tree->dragF && !mouseIsDown(MS_LEFT) && mouseCollision(&box))
	{
		UITreeNode *node = ui_TreeNodeFind(tree, yClicked / scale);
		if (node)
		{
			if (tree->dragging == node) 
			{
				tree->dragging = NULL;
			}
			else
			{
				UITreeNode *draggingParent = ui_TreeNodeFindParent(tree, tree->dragging);
				UITreeNode *dragToParent = NULL;
				int insertIdx;
				F32 newNodeY = y - tree->widget.sb->ypos + (ui_TreeNodeGetYPos(tree, node) + node->height * 0.5 + UI_HSTEP) * scale;
				F32 dragNodeY = y - tree->widget.sb->ypos + (ui_TreeNodeGetYPos(tree, tree->dragging) + node->height * 0.5 + UI_HSTEP) * scale;

				if (tree->dragToNewParent) 
				{
					if (tree->bAllowDropNode && ui_TreeMouseOverNodeCenter(newNodeY, node->height, scale, node->open))
					{
						dragToParent = node;
						insertIdx = eaSize(&dragToParent->children);
						dragToParent->open = true;
					}
				}

				if (!dragToParent)
				{
					dragToParent = ui_TreeNodeFindParent(tree, node);
					insertIdx = eaFind(&dragToParent->children, node);
					if ((newNodeY > dragNodeY && draggingParent == dragToParent) && 
						(g_ui_State.mouseY <= newNodeY))
					{
						// When dragging down in same parent, and in upper half, account for removal of self
						insertIdx--;
					}
					else if ((newNodeY < dragNodeY || draggingParent != dragToParent) && 
							 (g_ui_State.mouseY > newNodeY))
					{
						// When in lower half of node, add after instead of before
						insertIdx++;
					}
					if (insertIdx < 0)
						insertIdx = 0;
				}
				

				// If not dragged to itself, perform drag callback
				tree->dragF(tree, tree->dragging, draggingParent, dragToParent, insertIdx, tree->dragData);
				tree->dragging = NULL;
			}
		}
		inpHandled();
	}
	else if (tree->dragging && tree->dragF && !mouseIsDown(MS_LEFT) && !mouseCollision(&box))
	{
		// Cancel drag if end it outside of collision area
		tree->dragging = NULL;
	}
	else if (mouseCollision(&box))
	{
		UITreeNode *node = ui_TreeNodeFind(tree, yClicked / scale);
		ui_WidgetSetTooltipString(UI_WIDGET(tree), NULL);
		if (node && node->tooltipF)
		{
			char* str = node->tooltipF(node, node->tooltipData);
			ui_WidgetSetTooltipString(UI_WIDGET(tree), str);
			free(str);
			ui_TooltipsSetActive(UI_WIDGET(tree), y, y+h);
		}
	}

	UI_TICK_LATE(tree);
}

void ui_TreeNodeFill(UITree *tree, UITreeNode *node, F32 height)
{
	node->open = false;
	node->height = height;
	node->tree = tree;
}

void ui_TreeNodeFree(UITreeNode *node)
{
	assert(!node->tree->isInWidgetTick);
	eaDestroyEx(&node->children, ui_TreeNodeFree);
	//ui_WidgetGroupFreeInternal(&node->widgets);
	if (node->tree)
	{
		eaFindAndRemoveFast(&node->tree->multiselected, node);
		if (node->tree->selected == node)
			node->tree->selected = NULL;
	}
	if (node->freeF)
		node->freeF(node);
	free(node);
}

UITree *ui_TreeCreate(F32 x, F32 y, F32 w, F32 h)
{
	UITree *tree = (UITree *)calloc(1, sizeof(UITree));
	ui_TreeInitialize(tree, x, y, w, h);
	return tree;
}

void ui_TreeInitialize(UITree *tree, F32 x, F32 y, F32 w, F32 h)
{
	ui_WidgetInitialize(UI_WIDGET(tree), ui_TreeTick, ui_TreeDraw, ui_TreeFreeInternal, ui_TreeInput, ui_WidgetDummyFocusFunc);
	ui_WidgetSetPosition(UI_WIDGET(tree), x, y);
	ui_WidgetSetDimensions(UI_WIDGET(tree), w, h);
	tree->widget.sb = ui_ScrollbarCreate(false, true);
	ui_TreeNodeFill(tree, &tree->root, 0);
	tree->bAllowDropNode = true;
	tree->root.open = true;
}

void ui_TreeFreeInternal(UITree *tree)
{
	eaDestroyEx(&tree->root.children, ui_TreeNodeFree);
	eaDestroy(&tree->multiselected);
	ui_WidgetFreeInternal(UI_WIDGET(tree));
}

static void ui_TreeNodeExpandNoCollapse(SA_PARAM_NN_VALID UITreeNode *node)
{
	if (node->fillF)
	{
		node->fillF(node, node->fillData);
		node->open = true;
	}
}

void ui_TreeNodeExpandAndCallbackEx(UITreeNode *node, bool recurse)
{
	// Clear out existing children, if any.
	if (node->children)
		ui_TreeNodeCollapse(node);
	ui_TreeNodeExpandNoCollapse(node);
	if (node->expandF)
		node->expandF(node, node->expandData);

	if(recurse)
	{
		int i;
		for(i = 0; i < eaSize(&node->children); i++)
			ui_TreeNodeExpandAndCallbackEx(node->children[i], true);
	}
}

void ui_TreeNodeExpandEx(UITreeNode *node, bool recurse)
{
	// Clear out existing children, if any.
	if (node->children)
		ui_TreeNodeCollapse(node);
	ui_TreeNodeExpandNoCollapse(node);

	if(recurse)
	{
		int i;
		for(i = 0; i < eaSize(&node->children); i++)
			ui_TreeNodeExpandEx(node->children[i], true);
	}
}

void ui_TreeNodeCollapseAndCallback(UITreeNode *node)
{
	eaDestroyEx(&node->children, ui_TreeNodeFree);
	if (node->collapseF)
		node->collapseF(node, node->collapseData);
	node->open = false;
}

void ui_TreeNodeCollapse(UITreeNode *node)
{
	eaDestroyEx(&node->children, ui_TreeNodeFree);
	node->open = false;
}

int ui_TreeNodeGetIndentInternal(UITreeNode *root, UITreeNode *goal)
{
	int i;
	for (i = 0; i < eaSize(&root->children); i++)
	{
		if (goal == root->children[i])
			return 1;
		else
		{
			int indent = ui_TreeNodeGetIndentInternal(root->children[i], goal);
			if (indent >= 0)
				return indent + 1;
		}
	}
	return -1;
}

int ui_TreeNodeGetIndent(UITreeNode *node)
{
	if (node->tree)
		return ui_TreeNodeGetIndentInternal(&node->tree->root, node);
	else
		return -1;
}

void ui_TreeNodeAddChildren(UITreeNode *node, ParseTable *table, void **contents, UITreeChildrenFunc fill,
							UserData fillData, UITreeDisplayFunc display, UserData displayData, F32 height)
{
	int i;
	for (i = 0; i < eaSize(&contents); i++)
	{
		UITreeNode *newNode = ui_TreeNodeCreate(
			node->tree, 0, table, contents[i], fill, fillData, 
			display, displayData, height);
		ui_TreeNodeAddChild(node, newNode);
	}
}

void ui_TreeNodeAddChild(UITreeNode *parent, UITreeNode *child)
{
	child->parent = parent;
	eaPush(&parent->children, child);
}

bool ui_TreeNodeRemoveChild(SA_PARAM_NN_VALID UITreeNode *parent, SA_PARAM_NN_VALID UITreeNode *child)
{
	bool ret = (eaFindAndRemove(&parent->children, child) >= 0);
	if (ret != -1)
	{
		child->parent = NULL;
		ui_TreeNodeFree(child);
		return true;
	}
	return false;
}

bool ui_TreeNodeRemoveChildByContents(SA_PARAM_NN_VALID UITreeNode *parent, UserData contents)
{
	int i;
	for (i = 0; i < eaSize(&parent->children); i++)
		if (parent->children[i]->contents == contents)
			return ui_TreeNodeRemoveChild(parent, parent->children[i]);
	return false;
}

void ui_TreeNodeTruncateChildren(SA_PARAM_NN_VALID UITreeNode *parent, S32 children)
{
	while (eaSize(&parent->children) > children)
	{
		UITreeNode *child = eaPop(&parent->children);
		child->parent = NULL;
		ui_TreeNodeFree(child);
	}
}

void ui_TreeNodeSortChildren(SA_PARAM_NN_VALID UITreeNode* parent, UITreeNodeComparator* comparator)
{
	eaQSort(parent->children, comparator);
}

UITreeNode *ui_TreeNodeCreate(UITree *tree, U32 crc, ParseTable *table, void *contents,
							  UITreeChildrenFunc fill, UserData fillData,
							  UITreeDisplayFunc display, UserData displayData,
							  F32 height)
{
	UITreeNode *node = (UITreeNode *)calloc(1, sizeof(UITreeNode));
	ui_TreeNodeFill(tree, node, height);
	node->crc = crc;
	node->table = table;
	node->contents = contents;
	node->fillF = fill;
	node->fillData = fillData;
	node->displayF = display;
	node->displayData = displayData;
	node->allow_selection = true;
	return node;
}

void ui_TreeDisplayText(UITreeNode *node, const char *text, UI_MY_ARGS, F32 z)
{
	UITree* tree = node->tree;
	CBox box = {x, y, x + w, y + h};
	bool bSelected = ui_TreeIsNodeSelected( node->tree, node );
	bool bHover = false;
	UIStyleFont *font = ui_TreeItemGetFont( tree, bSelected, bHover );
	ui_StyleFontUse(font, bSelected, UI_WIDGET(tree)->state);
	clipperPushRestrict(&box);
	gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "%s", text);
	clipperPop();
}

void ui_TreeDisplayTextIndirect(UITreeNode *node, const char **ppText, UI_MY_ARGS, F32 z)
{
	UITree* tree = node->tree;
	CBox box = {x, y, x + w, y + h};
	bool bSelected = ui_TreeIsNodeSelected( node->tree, node );
	bool bHover = false;
	UIStyleFont *font = ui_TreeItemGetFont( tree, bSelected, bHover );
	ui_StyleFontUse(font, bSelected, UI_WIDGET(tree)->state);
	clipperPushRestrict(&box);
	gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "%s", *ppText);
	clipperPop();
}

void ui_TreeDisplaySeparator(SA_PARAM_NN_VALID UITreeNode *node, void *ignored, UI_MY_ARGS, F32 z)
{
	gfxDrawLine(x, y + h / 2, z, x + w, y + h / 2, ui_TreeGetLineColor(node->tree));
}

void ui_TreeNodeDisplaySimple(UITreeNode *node, const char *field, UI_MY_ARGS, F32 z)
{
	if (node->table)
	{
		int i;
		char token[256];
		char message[1024];
		FORALL_PARSETABLE(node->table, i)
		{
			if (node->table[i].name && !(strcmp(node->table[i].name, field)))
			{
				if (TokenToSimpleString(node->table, i, node->contents, SAFESTR(token), 0))
					sprintf(message, "%s: %s", field, token);
				else
					sprintf(message, "%s: unrepresentable contents", field);
				ui_TreeDisplayText(node, message, UI_MY_VALUES, z);
				return;
			}
		}

		sprintf(token, "%s (%s)", field, "not a field");
		ui_TreeDisplayText(node, token, UI_MY_VALUES, z);
	}
	else
		ui_TreeDisplayText(node, field, UI_MY_VALUES, z);
}

void ui_TreeNodeSetFillCallback(UITreeNode *node, UITreeChildrenFunc fillF, UserData fillData)
{
	node->fillF = fillF;
	node->fillData = fillData;
}

void ui_TreeNodeSetDisplayCallback(UITreeNode *node, UITreeDisplayFunc displayF, UserData displayData)
{
	node->displayF = displayF;
	node->displayData = displayData;
}

void ui_TreeNodeSetFreeCallback(SA_PARAM_NN_VALID UITreeNode *node, SA_PARAM_NN_VALID UIFreeFunction freeF)
{
	node->freeF = freeF;
}

void ui_TreeNodeSetExpandCallback(UITreeNode *node, UITreeChildrenFunc expandF, UserData expandData)
{
	node->expandF = expandF;
	node->expandData = expandData;
}

void ui_TreeNodeSetCollapseCallback(UITreeNode *node, UITreeChildrenFunc collapseF, UserData collapseData)
{
	node->collapseF = collapseF;
	node->collapseData = collapseData;
}

void ui_TreeSetActivatedCallback(UITree *tree, UIActivationFunc activatedF, UserData activatedData)
{
	tree->activatedF = activatedF;
	tree->activatedData = activatedData;
}

void ui_TreeSetDragCallback(SA_PARAM_NN_VALID UITree *tree, UITreeDragFunc dragF, UserData dragData)
{
	tree->dragF = dragF;
	tree->dragData = dragData;
}

void ui_TreeSetSelectedCallback(UITree *tree, UIActivationFunc selectedF, UserData selectedData)
{
	tree->selectedF = selectedF;
	tree->selectedData = selectedData;
}

void ui_TreeSetContextCallback(UITree *tree, UIActivationFunc contextF, UserData contextData)
{
	tree->widget.contextF = contextF;
	tree->widget.contextData = contextData;
}

void ui_TreeEnableDragAndDrop(SA_PARAM_NN_VALID UITree *tree)
{
	tree->itemDraggingEnabled = 1;
}

void ui_TreeDisableDragAndDrop(SA_PARAM_NN_VALID UITree *tree)
{
	tree->itemDraggingEnabled = 0;
}

UITreeNode *ui_TreeGetSelected(UITree *tree)
{
	return tree->selected;
}

void ui_TreeSetMultiselect(UITree *tree, bool multiselect)
{
	tree->multiselect = multiselect;
	if (!multiselect)
		eaDestroy(&tree->multiselected);
}

const UITreeNode * const * const *ui_TreeGetSelectedNodes(UITree *tree)
{
	if (!tree->multiselect)
	{
		eaDestroy(&tree->multiselected);
		if (tree->selected)
			eaPush(&tree->multiselected, tree->selected);
	}
	return &tree->multiselected;
}

bool ui_TreeIsNodeSelected(UITree *tree, UITreeNode *node)
{
	if (!tree->multiselect)
		return (tree->selected == node);
	else
		return (eaFind(&tree->multiselected, node) >= 0);
}

//////////////////////////////////////////////////////////////////////////

MP_DEFINE(UITreeRefreshNode);

SA_RET_NN_VALID UITreeRefreshNode *ui_TreeRefreshNodeCreate(SA_PARAM_NN_VALID UITreeNode *tree_node)
{
	UITreeRefreshNode *refresh_node;
	int i;

	MP_CREATE(UITreeRefreshNode, 256);
	refresh_node = MP_ALLOC(UITreeRefreshNode);
	refresh_node->nodeCrc = tree_node->crc;
	refresh_node->open = tree_node->open;

	for (i = 0; i < eaSize(&tree_node->children); ++i)
	{
		UITreeRefreshNode *child;
		if (tree_node->children[i] && tree_node->children[i]->crc)
		{
			child = ui_TreeRefreshNodeCreate(tree_node->children[i]);
			if (child)
				eaPush(&refresh_node->children, child);
		}
	}

	return refresh_node;
}

void ui_TreeRefreshNodeDestroy(SA_PARAM_OP_VALID UITreeRefreshNode *refresh_node)
{
	if (!refresh_node)
		return;
	eaDestroyEx(&refresh_node->children, ui_TreeRefreshNodeDestroy);
	MP_FREE(UITreeRefreshNode, refresh_node);
}

static void ui_TreeRefreshMain(SA_PARAM_OP_VALID UITreeNode *tree_node, SA_PARAM_OP_VALID UITreeRefreshNode *refresh_node)
{
	int i, j;
	StashTable stCRCToRefreshNode = NULL;

	if(	!tree_node ||
		!refresh_node ||
		!eaSize(&tree_node->children) ||
		!eaSize(&refresh_node->children))
	{
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	// Build a local CRC lookup table.

	stCRCToRefreshNode = stashTableCreateInt(100);
	for (j = 0; j < eaSize(&refresh_node->children); ++j){
		UITreeRefreshNode *child_refresh_node = refresh_node->children[j];
		// andrewa - added nodeCrc check against zero. it's possible to have a zero CRC, but that is an invalid stash table key. [COR-15474]
		if(	child_refresh_node->open &&
			(child_refresh_node->nodeCrc == 0 || !stashIntAddPointer(stCRCToRefreshNode,
																	 child_refresh_node->nodeCrc,
																	 child_refresh_node,
																	 0)))
		{
			// Duped (might be impossible) or Zero (definitely possible) CRC, use the old slow search.
			stashTableDestroySafe(&stCRCToRefreshNode);
			break;
		}
	}

	if(!stCRCToRefreshNode){
		// The old slow O(n^2) way.

		for (i = 0; i < eaSize(&tree_node->children); ++i)
		{
			UITreeNode *child_node = tree_node->children[i];
			for (j = 0; j < eaSize(&refresh_node->children); ++j)
			{
				UITreeRefreshNode *child_refresh_node = refresh_node->children[j];
				if (child_node->children || (child_node->crc == child_refresh_node->nodeCrc && child_refresh_node->open))
				{
					ui_TreeNodeExpandNoCollapse(child_node);
					ui_TreeRefreshMain(child_node, child_refresh_node);
					break;
				}
			}
		}
	}else{
		// The new fast O(n) way.

		for (i = 0; i < eaSize(&tree_node->children); ++i)
		{
			UITreeNode *child_node = tree_node->children[i];
			UITreeRefreshNode *child_refresh_node;

			if(child_node->children){
				child_refresh_node = refresh_node->children[0];
			}
			else if(!stashIntFindPointer(stCRCToRefreshNode, child_node->crc, &child_refresh_node)){
				continue;
			}

			ui_TreeNodeExpandNoCollapse(child_node);
			ui_TreeRefreshMain(child_node, child_refresh_node);
		}

		stashTableDestroySafe(&stCRCToRefreshNode);
	}

	PERFINFO_AUTO_STOP();
}

void ui_TreeRefreshEx(SA_PARAM_NN_VALID UITree *tree, SA_PARAM_OP_VALID UITreeRefreshNode *refresh_root)
{
	U32 *selected_crcs = NULL;
	if (tree->multiselect)
	{
		FOR_EACH_IN_EARRAY(tree->multiselected, UITreeNode, node)
		{
			eaiPush(&selected_crcs, node->crc);
		}
		FOR_EACH_END;
	}
	else if (tree->selected)
	{
		eaiPush(&selected_crcs, tree->selected->crc);
	}
	// close tree
	ui_TreeNodeCollapse(&tree->root);
	ui_TreeNodeExpand(&tree->root);

	// open nodes that are in the refresh tree
	ui_TreeRefreshMain(&tree->root, refresh_root);

	if (eaiSize(&selected_crcs) > 0)
	{
		UITreeIterator *iter = ui_TreeIteratorCreate(tree, false, false);
		UITreeNode *node = ui_TreeIteratorCurr(iter);
		while (node)
		{
			if (eaiFind(&selected_crcs, node->crc) != -1)
			{
				if (tree->multiselect)
				{
					if (eaFindAndRemoveFast(&tree->multiselected, node) == -1)
					{
						eaPush(&tree->multiselected, node);
						tree->selected = node;
					}
				}
				else 
				{
					tree->selected = node;
				}
				break;
			}
			node = ui_TreeIteratorNext(iter);
		}
		free(iter);
	}
}

void ui_TreeRefresh(UITree *tree)
{
	UITreeRefreshNode *refresh_root;

	PERFINFO_AUTO_START_FUNC();

	// create a parallel tree of open nodes
	refresh_root = ui_TreeRefreshNodeCreate(&tree->root);

	// open nodes that are in the refresh tree
	ui_TreeRefreshEx(tree, refresh_root);

	// destroy refresh tree
	ui_TreeRefreshNodeDestroy(refresh_root);

	PERFINFO_AUTO_STOP();
}

void ui_tree_expand_and_select_helper(UITree *tree, UITreeNode *tree_node, const void **expand_list, bool call_callback)
{
	if (!tree_node->open && expand_list[0])
		ui_TreeNodeExpand(tree_node); // Only calling it !open so that we don't close siblings.  Not calling ui_TreeNodeExpandNoCollapse because it would fill twice.
	if (expand_list && expand_list[0])
	{
		int i;
		for (i = 0; i < eaSize(&tree_node->children); ++i)
		{
			if (tree_node->children[i]->contents == expand_list[0])
			{
				ui_tree_expand_and_select_helper(tree, tree_node->children[i], &expand_list[1], call_callback);
				return;
			}
		}
	}

	if (tree_node != &tree->root)
		tree->selected = tree_node;
	else
		tree->selected = NULL;

	if (tree->selectedF && call_callback)
		tree->selectedF(tree, tree->selectedData);
	return;
}

void ui_TreeExpandAndSelect(UITree *tree, const void **expand_list, bool call_callback)
{
	ui_tree_expand_and_select_helper(tree, &tree->root, expand_list, call_callback);
}

//////////////////////////////////////////////////////////////////////////

UITreeIterator *ui_TreeIteratorCreate(UITree *tree, bool cycle, bool expandNodes)
{
	UITreeIterator *iter = calloc(1, sizeof(*iter));
	iter->tree = tree;
	iter->cycle = cycle;
	iter->expandNodes = expandNodes;
	return iter;
}

static bool ui_TreeIteratorGetNodeIndexes(UITreeIterator *iterator, UITreeNode *node, UITreeNode *root, int **idxs)
{
	int i;

	// base case
	if (root == node)
		return true;

	// recursion
	for (i = 0; i < eaSize(&root->children); i++)
	{
		if (ui_TreeIteratorGetNodeIndexes(iterator, node, root->children[i], idxs))
		{
			eaiInsert(idxs, i, 0);
			return true;
		}
	}

	return false;
}

UITreeIterator *ui_TreeIteratorCreateFromNode(UITreeNode *node, bool cycle, bool expandNodes)
{
	UITreeNode *parent = NULL;
	UITreeIterator *iter = calloc(1, sizeof(*iter));
	iter->tree = node->tree;
	iter->cycle = cycle;
	iter->expandNodes = expandNodes;

	// initialize indexes to node
	ui_TreeIteratorGetNodeIndexes(iter, node, &node->tree->root, &iter->nodeIdxs);

	return iter;
}

void ui_TreeIteratorFree(UITreeIterator *iterator)
{
	if (iterator)
	{
		eaiDestroy(&iterator->nodeIdxs);
		SAFE_FREE(iterator);
	}
}

static void ui_TreeIteratorCurrInternal(UITreeIterator *iterator, UITreeNode ***nodeList)
{
	UITreeNode *node = &iterator->tree->root;
	int *newNodeIdxs = NULL;
	int i = 0;

	eaPush(nodeList, node);
	for (i = 0; i < eaiSize(&iterator->nodeIdxs); i++)
	{
		// get next node from the hierarchy
		if (iterator->expandNodes && !node->open)
			ui_TreeNodeExpandNoCollapse(node);
		if (iterator->nodeIdxs[i] < eaSize(&node->children))
		{
			node = node->children[iterator->nodeIdxs[i]];
			eaiPush(&newNodeIdxs, iterator->nodeIdxs[i]);
			eaPush(nodeList, node);
		}
		// if such a node is outside the array bounds, take the last descendant
		// of the last node at that level (i.e. the closest node immediately above it in the tree)
		else
		{
			while (eaSize(&node->children) > 0)
			{
				eaiPush(&newNodeIdxs, eaSize(&node->children) - 1);
				node = node->children[eaSize(&node->children) - 1];
				eaPush(nodeList, node);
				if (iterator->expandNodes && !node->open)
					ui_TreeNodeExpandNoCollapse(node);
			}
			break;
		}
	}

	// deepest node is first in the list
	eaReverse(nodeList);

	// transfer new node index list to iterator to ensure it points to valid nodes
	eaiDestroy(&iterator->nodeIdxs);
	iterator->nodeIdxs = newNodeIdxs;
}

UITreeNode *ui_TreeIteratorNext(UITreeIterator *iterator)
{
	UITreeNode *ret = NULL;
	UITreeNode **nodeList = NULL;

	ui_TreeIteratorCurrInternal(iterator, &nodeList);
	assert(eaSize(&nodeList) > 0);

	ret = nodeList[0];

	// if current node has children, return the first one
	if (iterator->expandNodes && !ret->open)
		ui_TreeNodeExpandNoCollapse(ret);
	if (eaSize(&ret->children) > 0)
	{
		eaiPush(&iterator->nodeIdxs, 0);
		ret = ret->children[0];
	}
	// ...otherwise, look for siblings of the current node, traversing upward as necessary
	else
	{
		int i;

		for (i = 1; i < eaSize(&nodeList); i++)
		{
			int idx = eaFind(&nodeList[i]->children, ret);

			// if current node is at the end of parent node's children, go to next parent up
			if (idx == eaSize(&nodeList[i]->children) - 1)
				ret = nodeList[i];
			else
			{
				ret = nodeList[i]->children[idx + 1];
				eaiSetSize(&iterator->nodeIdxs, eaiSize(&iterator->nodeIdxs) - i + 1);
				iterator->nodeIdxs[eaiSize(&iterator->nodeIdxs) - 1] = idx + 1;
				break;
			}
		}
		// if a next node was not found
		if (i == eaSize(&nodeList))
		{
			if (iterator->cycle)
				eaiClear(&iterator->nodeIdxs);
			else
				ret = NULL;
		}
	}
	eaDestroy(&nodeList);
	return ret;
}

UITreeNode *ui_TreeIteratorCurr(UITreeIterator *iterator)
{
	UITreeNode *ret = NULL;
	UITreeNode **nodeList = NULL;

	ui_TreeIteratorCurrInternal(iterator, &nodeList);
	if (eaSize(&nodeList) > 0)
		ret = nodeList[0];
	eaDestroy(&nodeList);
	return ret;
}

UITreeNode *ui_TreeIteratorPrev(UITreeIterator *iterator)
{
	UITreeNode *ret = NULL;
	UITreeNode **nodeList = NULL;

	ui_TreeIteratorCurrInternal(iterator, &nodeList);
	assert(eaSize(&nodeList) > 0);

	ret = nodeList[0];

	// traverse up parent list until a node is found that is not the first in its parent
	if (eaSize(&nodeList) > 1)
	{
		int idx = eaFind(&nodeList[1]->children, ret);

		assert(idx >= 0);
		eaiPop(&iterator->nodeIdxs);

		// if first in parent, return the parent node
		if (idx == 0)
			ret = nodeList[1];
		// otherwise, find the previous sibling in the parent and traverse to its last descendant
		else
		{
			ret = nodeList[1]->children[idx - 1];
			eaiPush(&iterator->nodeIdxs, idx - 1);
			if (!ret->open && iterator->expandNodes)
				ui_TreeNodeExpandNoCollapse(ret);
			while (eaSize(&ret->children) > 0) 
			{
				eaiPush(&iterator->nodeIdxs, eaSize(&ret->children) - 1);
				ret = ret->children[eaSize(&ret->children) - 1];
				if (!ret->open && iterator->expandNodes)
					ui_TreeNodeExpandNoCollapse(ret);
			}
		}
	}
	// if already at root node and cycling, return the last descendant of the root node; otherwise, return NULL
	else
	{
		eaiClear(&iterator->nodeIdxs);

		if (iterator->cycle)
		{
			if (!ret->open && iterator->expandNodes)
				ui_TreeNodeExpandNoCollapse(ret);
			while (eaSize(&ret->children) > 0) 
			{
				eaiPush(&iterator->nodeIdxs, eaSize(&ret->children) - 1);
				ret = ret->children[eaSize(&ret->children) - 1];
				if (!ret->open && iterator->expandNodes)
					ui_TreeNodeExpandNoCollapse(ret);
			}
		}
		else
			ret = NULL;
	}

	eaDestroy(&nodeList);
	return ret;
}


void ui_TreeSelectFromBranchWithConditionInternal(UITree *pTree, UITreeNode *pTreeNode, UITreeConditionalSelectFunc pFunc, void *pUserData)
{
	FOR_EACH_IN_EARRAY(pTreeNode->children, UITreeNode, pChildTreeNode)
	{
		bool result = pFunc(pChildTreeNode, pUserData);
		if(result)
		{
			if(pTree->multiselect)
			{
				eaPush(&pTree->multiselected, pChildTreeNode);
			}
			pTree->selected = pChildTreeNode;

			if (pTree->selectedF)
				pTree->selectedF(pTree, pTree->selectedData);
		}
		ui_TreeSelectFromBranchWithConditionInternal(pTree, pChildTreeNode, pFunc, pUserData);
	}
	FOR_EACH_END;
}

void ui_TreeSelectFromBranchWithCondition(UITree *pTree, UITreeNode *pTreeNode, UITreeConditionalSelectFunc pFunc, void *pUserData)
{
	if(pTree->multiselect)
	{
		eaClear(&pTree->multiselected);	
	}

	if(pFunc)
	{
		ui_TreeSelectFromBranchWithConditionInternal(pTree, pTreeNode, pFunc, pUserData);
	}
}
