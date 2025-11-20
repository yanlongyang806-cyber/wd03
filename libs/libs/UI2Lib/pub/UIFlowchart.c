/***************************************************************************



***************************************************************************/

#include "earray.h"


#include "inputMouse.h"

#include "GfxClipper.h"
#include "GfxPrimitive.h"
#include "GfxSprite.h"
#include "GfxTexAtlas.h"

#include "UIButton.h"
#include "UIFlowchart.h"
#include "UILabel.h"
#include "UIPane.h"
#include "UISprite.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

#define YDIFF_FOR_LOOP 50.0f
#define LOOP_WIDTH_FACTOR 0.3f

static void FillControlPoints(Vec2 controlPoints[4], F32 x1, F32 y1, F32 x2, F32 y2)
{
	F32 d = CLAMP(0.85 * ABS(x2 - x1), 15, 256);
	F32 dy = 0.0f;
	if (x1 > x2 && fabsf(y1 - y2) < YDIFF_FOR_LOOP)
	{
		dy = fabsf(x2 - x1) * LOOP_WIDTH_FACTOR * (YDIFF_FOR_LOOP - fabsf(y1-y2)) / YDIFF_FOR_LOOP;
	}
	controlPoints[0][0] = x1;
	controlPoints[0][1] = y1;
	controlPoints[3][0] = x2;
	controlPoints[3][1] = y2;
	controlPoints[1][1] = y1 + dy;
	controlPoints[2][1] = y2 + dy;
	controlPoints[1][0] = x1 + d;
	controlPoints[2][0] = x2 - d;
}

void ui_FlowchartButtonDrawSpline(UIFlowchartButton *fbutton, F32 drawnX, F32 drawnY, F32 z, F32 scale)
{
	S32 i;
	fbutton->bDrawn = true;
	fbutton->drawnX = drawnX;
	fbutton->drawnY = drawnY;
	for (i = 0; i < eaSize(&fbutton->connected); i++)
	{
		UIFlowchartButton *pOtherButton = fbutton->connected[i];
		static const Color ColorDefault = {0, 0x7F, 0, 0xFF};
		Color lineColor = ColorDefault;
		Vec2 controlPoints[4];
		if (pOtherButton->bDrawn)
		{
			UIFlowchartButton *pTo = fbutton->output ? pOtherButton : fbutton;
			UIFlowchartButton *pFrom = fbutton->output ? fbutton : pOtherButton;
			FillControlPoints(controlPoints, pFrom->drawnX, pFrom->drawnY, pTo->drawnX, pTo->drawnY);
			clipperPush(&fbutton->flow->clip);
			if (ui_IsActive(UI_WIDGET(fbutton)) && unfilteredMouseCollision(&fbutton->flow->clip) && gfxDrawBezierCollides(controlPoints, scale*5, g_ui_State.mouseX, g_ui_State.mouseY))
				lineColor = ColorRed;
			gfxDrawBezier(controlPoints, z + 200.1, lineColor, lineColor, scale);
			clipperPop();
		}
	}
}

void ui_FlowchartButtonDraw(UIFlowchartButton *fbutton, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(fbutton);
	AtlasTex *expander = (fbutton->open ? (g_ui_Tex.minus) : (g_ui_Tex.plus));
	CBox exBox = {0, 0, 0, 0}, buttonBox = box;
	Color c;

	if (fbutton->type == UIFlowchartHasChildrenInternal)
	{
		if (fbutton->output)
		{
			BuildCBox(&exBox, x, y, w/2, h);
			BuildCBox(&buttonBox, x + w/2, y, w/2, h);
		}
		else
		{
			BuildCBox(&buttonBox, x, y, w/2, h);
			BuildCBox(&exBox, x + w/2, y, w/2, h);
		}
		c = ui_WidgetButtonColor(UI_WIDGET(fbutton), false, false);
		display_sprite_box(expander, &exBox, z, RGBAFromColor(c));
		c = ui_WidgetButtonColor(UI_WIDGET(fbutton), false, !!fbutton->flow->connecting);
		ui_DrawCapsule(&buttonBox, z, c, scale);
	}
	else
	{
		c = ui_WidgetButtonColor(UI_WIDGET(fbutton), false, !!fbutton->flow->connecting);
		ui_DrawCapsule(&box, z, c, scale);
	}

	ui_FlowchartButtonDrawSpline(fbutton, fbutton->output ? (x + w) : x, y + h / 2, z, scale);

	if (fbutton->flow && fbutton->flow->connecting == fbutton)
	{
		static const Color ColorCreating = {0, 0x7F, 0x7F, 0xFF};
		Vec2 controlPoints[4];
		if (fbutton->output) {
			FillControlPoints(controlPoints, fbutton->drawnX, fbutton->drawnY, g_ui_State.mouseX, g_ui_State.mouseY);
		} else {
			FillControlPoints(controlPoints, g_ui_State.mouseX, g_ui_State.mouseY, fbutton->drawnX, fbutton->drawnY);
		}
		clipperPush(NULL);
		gfxDrawBezier(controlPoints, UI_INFINITE_Z, ColorCreating, ColorCreating, scale);
		clipperPop();
	}

	if (fbutton->label)
	{
		UILabel *label = fbutton->label;
		label->widget.y = fbutton->widget.y + fbutton->widget.height/2 - label->widget.height/2;
		ui_LabelDraw(label, UI_PARENT_VALUES);
	}
}

static int ui_FlowchartReflow(UIFlowchartNode* pNode, UIFlowchartButton ***buttons)
{
	int i, j = 0;
	bool show = true;
    int baseY = ui_WidgetGetHeight(UI_WIDGET(pNode->beforePane)) + 2;

	for (i = 0; i < eaSize(buttons); i++)
	{
		UIWidgetGroup *group = (*buttons)[0]->widget.group;
		UIFlowchartButton *button = (*buttons)[i];
		if (button->type != UIFlowchartIsChild)
			button->widget.y = baseY + 20 * j++;
		else if (button->type == UIFlowchartIsChild && (show || eaSize(&button->connected)))
		{
			button->widget.y = baseY + 20 * j++;
			if (!button->widget.group)
				ui_WidgetGroupAdd(group, (UIWidget *)button);
		}
		else if (button->widget.group)
			ui_WidgetGroupRemove(button->widget.group, (UIWidget *)button);

		if (button->type == UIFlowchartHasChildrenInternal)
			show = button->open;
	}

	return 20 * j;
}

void ui_FlowchartButtonTick(UIFlowchartButton *fbutton, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(fbutton);
	AtlasTex *expander = (fbutton->open ? (g_ui_Tex.minus) : (g_ui_Tex.plus));
	CBox exBox = {0, 0, 0, 0}, buttonBox;

	fbutton->bDrawn = false;

	UI_TICK_EARLY(fbutton, false, true);

	buttonBox = box;

	if (fbutton->type == UIFlowchartHasChildrenInternal)
	{
		if (fbutton->output)
		{
			BuildCBox(&exBox, x, y, w/2, h);
			BuildCBox(&buttonBox, x + w/2, y, w/2, h);
		}
		else
		{
			BuildCBox(&buttonBox, x, y, w/2, h);
			BuildCBox(&exBox, x + w/2, y, w/2, h);
		}
		CBoxClipTo(&pBox, &exBox);
		CBoxClipTo(&pBox, &buttonBox);
	}

	if (mouseDownHit(MS_LEFT, &buttonBox))
	{
		if (fbutton->flow && fbutton->flow->connecting)
		{
			UIFlowchartButton *sibling = fbutton->flow->connecting;
			fbutton->flow->connecting = NULL;
			if (fbutton->output == sibling->output)
			{
				if (fbutton->node == sibling->node)
					ui_FlowchartSwap(fbutton, sibling);
			}
			else if (fbutton->output)
				ui_FlowchartLink(fbutton, sibling);
			else
				ui_FlowchartLink(sibling, fbutton);
		}
		else if (fbutton->flow)
		{
			bool connect = true;
			if (fbutton->flow->startLinkF)
				if (fbutton->output)
					connect = fbutton->flow->startLinkF(fbutton->flow, fbutton, NULL, false, fbutton->flow->startLinkData);
				else
					connect = fbutton->flow->startLinkF(fbutton->flow, NULL, fbutton, false, fbutton->flow->startLinkData);
			if (connect)
				fbutton->flow->connecting = fbutton;
			if (fbutton->bSingleConnection)
				ui_FlowchartButtonUnlink(fbutton, false);
		}
		inpHandled();
	}
	else if (mouseDownHit(MS_LEFT, &exBox))
	{
		fbutton->open = !fbutton->open;
		inpHandled();
	}
	else if (mouseDown(MS_RIGHT) && fbutton->flow->connecting == fbutton)
	{
		fbutton->flow->connecting = false;
		inpHandled();
	}
	else if (fbutton->output)
	{
		int i;
		UIFlowchartButton **copied = NULL;
		eaCopy(&copied, &fbutton->connected);
		mouseClipPush(NULL);
		for (i = 0; i < eaSize(&copied); i++)
		{
			UIFlowchartButton *input = copied[i];
			Vec2 controlPoints[4];
			FillControlPoints(controlPoints, fbutton->drawnX, fbutton->drawnY, input->drawnX, input->drawnY);
			if (mouseDownHit(MS_RIGHT, &fbutton->flow->clip) && gfxDrawBezierCollides(controlPoints, scale*5, g_ui_State.mouseX, g_ui_State.mouseY))
			{
				ui_FlowchartUnlink(fbutton, input, false);
				inpHandled();
			}
		}
		mouseClipPop();
		eaDestroy(&copied);
	}

	UI_TICK_LATE(fbutton);
}

UIFlowchartButton *ui_FlowchartButtonCreate(UIFlowchart *flow, UIFlowchartButtonType type,
											UILabel *label, UserData userData)
{
	UIFlowchartButton *fbutton = (UIFlowchartButton *)calloc(1, sizeof(UIFlowchartButton));
	ui_FlowchartButtonInitialize(fbutton, flow, type, label, userData);
	return fbutton;
}

void ui_FlowchartButtonInitialize(UIFlowchartButton *fbutton, UIFlowchart *flow, UIFlowchartButtonType type,
							  UILabel *label, UserData userData)
{
	ui_WidgetInitialize((UIWidget *)fbutton, ui_FlowchartButtonTick, ui_FlowchartButtonDraw,
		ui_FlowchartButtonFree, NULL, NULL);
	ui_WidgetSetDimensions((UIWidget *)fbutton, 16 + (type == UIFlowchartHasChildrenInternal ? 16 : 0), 16);
	fbutton->flow = flow;
	fbutton->type = type;
	fbutton->userData = userData;
	fbutton->label = label;
}

void ui_FlowchartButtonFree(UIFlowchartButton *fbutton)
{
	ui_FlowchartButtonUnlink(fbutton, true);
	eaDestroy(&fbutton->connected);
	if (fbutton->label)
		ui_LabelFreeInternal(fbutton->label);
	ui_WidgetFreeInternal((UIWidget *)fbutton);
}

void ui_FlowchartButtonUnlink(SA_PARAM_NN_VALID UIFlowchartButton *fbutton, bool force)
{
	int i;
	if (fbutton->output)
		for (i=eaSize(&fbutton->connected)-1; i>=0; i--) 
			ui_FlowchartUnlink(fbutton, fbutton->connected[i], force);
	else
		for (i=eaSize(&fbutton->connected)-1; i>=0; i--) 
			ui_FlowchartUnlink(fbutton->connected[i], fbutton, force);

	if (force)
		assert(eaSize(&fbutton->connected)==0);
}

UIFlowchartNode *ui_FlowchartNodeCreate(const char *title, F32 x, F32 y, F32 w, F32 h,
										UIFlowchartButton ***inputButtons, UIFlowchartButton ***outputButtons, UserData userData)
{
	int i;
	F32 minH=0, minW=0;
	UIFlowchartNode *node  = (UIFlowchartNode *)calloc(1, sizeof(UIFlowchartNode));
	ui_WindowInitializeEx(UI_WINDOW(node), title, x, y, w, h MEM_DBG_PARMS_INIT);
	node->widget.drawF = ui_FlowchartNodeDraw;
	node->widget.tickF = ui_FlowchartNodeTick;

	node->inputButtons = *inputButtons;
	*inputButtons = NULL;
	node->outputButtons = *outputButtons;
	*outputButtons = NULL;
	node->userData = userData;

	FOR_EACH_IN_EARRAY(node->inputButtons, UIFlowchartButton, fbutton)
	{
		if (fbutton->type == UIFlowchartIsChild) {
			assert(ifbuttonIndex>0);
			if (node->inputButtons[ifbuttonIndex-1]->type == UIFlowchartNormal)
			{
				node->inputButtons[ifbuttonIndex-1]->type = UIFlowchartHasChildrenInternal;
				node->inputButtons[ifbuttonIndex-1]->widget.width += 16;
			}
		}
		fbutton->output = false;
		fbutton->node = node;
		ui_WidgetSetPositionEx((UIWidget *)fbutton, 2, ifbuttonIndex * 20, 0, 0, UITopLeft);
		ui_WindowAddChild(UI_WINDOW(node), fbutton);
		if (fbutton->label)
		{
			fbutton->label->widget.x = 4 + fbutton->widget.width + fbutton->widget.x;
			fbutton->label->widget.y = fbutton->widget.y;
			fbutton->label->widget.offsetFrom = fbutton->label->widget.offsetFrom;
		}
		MAX1(minH, (ifbuttonIndex + 1) * 20);
	}
	FOR_EACH_END;
	FOR_EACH_IN_EARRAY(node->outputButtons, UIFlowchartButton, fbutton)
	{
		if (fbutton->type == UIFlowchartIsChild) {
			assert(ifbuttonIndex>0);
			if (node->outputButtons[ifbuttonIndex-1]->type == UIFlowchartNormal) {
				node->outputButtons[ifbuttonIndex-1]->type = UIFlowchartHasChildrenInternal;
				node->outputButtons[ifbuttonIndex-1]->widget.width += 16;
			}
		}
		fbutton->output = true;
		fbutton->node = node;
		ui_WidgetSetPositionEx((UIWidget *)fbutton, 2, ifbuttonIndex * 20, 0, 0, UITopRight);
		ui_WindowAddChild(UI_WINDOW(node), fbutton);
		if (fbutton->label)
		{
			fbutton->label->widget.x = 4 + fbutton->widget.width + fbutton->widget.x;
			fbutton->label->widget.y = fbutton->widget.y;
			fbutton->label->widget.offsetFrom = fbutton->widget.offsetFrom;
		}
		MAX1(minH, (ifbuttonIndex + 1) * 20);
	}
	FOR_EACH_END;

	for (i=0; i<max(eaSize(&node->outputButtons), eaSize(&node->inputButtons)); i++)
	{
		UIFlowchartButton *fbuttonLeft = eaGet(&node->inputButtons, i);
		UIFlowchartButton *fbuttonRight = eaGet(&node->outputButtons, i);
		F32 wLeft=0, wRight=0;
		if (fbuttonLeft) {
			UIWidget *widget = fbuttonLeft->label?UI_WIDGET(fbuttonLeft->label):UI_WIDGET(fbuttonLeft);
			wLeft = widget->x + widget->width;
		}
		if (fbuttonRight) {
			UIWidget *widget = fbuttonRight->label?UI_WIDGET(fbuttonRight->label):UI_WIDGET(fbuttonRight);
			wRight = widget->x + widget->width;
		}
		MAX1(minW, wLeft + UI_STEP + wRight);
	}

    node->beforePane = ui_PaneCreate(0, 0, 1, 10, UIUnitPercentage, UIUnitFixed, 0);
    node->beforePane->invisible = true;
    ui_PaneSetInvisible(node->beforePane, true);
    ui_WindowAddChild(UI_WINDOW(node), node->beforePane);
    
	node->afterPane = ui_PaneCreate( 0, minH, 1, 0, UIUnitPercentage, UIUnitFixed, 0 );
	node->afterPane->invisible = true;
	ui_PaneSetInvisible(node->afterPane, true);
	ui_WindowAddChild(UI_WINDOW(node), node->afterPane);

	ui_WindowSetDimensions(UI_WINDOW(node), node->window.widget.width, node->window.widget.height, minW, minH);

	return node;
}

void ui_FlowchartNodeDraw(UIFlowchartNode *pNode, UI_PARENT_ARGS)
{
	U32 uiBackgroundZ;
	if (pNode->flow)
	{
		const CBox *pClipBox = clipperGetCurrentCBox();
		if (pClipBox)
			pNode->flow->clip = *pClipBox;

		if (UI_WINDOW(pNode)->shaded)
		{
			UI_GET_COORDINATES(pNode);
			UIStyleBorder *pBorder = RefSystem_ReferentFromString("UIStyleBorder", "Default_WindowFrame");
			S32 i;
			F32 yOff = y - (ui_StyleBorderTopSize(pBorder) + g_ui_Tex.windowTitleMiddle->height / 2) * scale;
			for (i = 0; i < eaSize(&pNode->inputButtons); i++)
				ui_FlowchartButtonDrawSpline(pNode->inputButtons[i], x, yOff, z, scale);
			for (i = 0; i < eaSize(&pNode->outputButtons); i++)
				ui_FlowchartButtonDrawSpline(pNode->outputButtons[i], x + w, yOff, z, scale);
		}
	}

	uiBackgroundZ = g_ui_State.drawZ;
	ui_WindowDraw(UI_WINDOW(pNode), UI_PARENT_VALUES);
	if (pNode->backgroundScrollSprite)
	{
		CBox boxWindow = {0};
		CBox boxSprite = {0};
		F32 fScrollX, fScrollW;
		U32 uiNewZ = g_ui_State.drawZ;
		F32 fWidth, fCenter;
		ui_WidgetGetCBox(UI_WIDGET(pNode), &boxWindow);
		fScrollX = pNode->backgroundScrollXPercent?pNode->backgroundScrollXPercent:0.0f;
		fScrollW = pNode->backgroundScrollWidthPercent?pNode->backgroundScrollWidthPercent:1.0f;
		boxSprite.top = boxWindow.top;
		boxSprite.bottom = boxWindow.bottom;
		fWidth = (boxWindow.right - boxWindow.left) * fScrollW;
		fCenter = (boxWindow.right - boxWindow.left) * fScrollX + boxWindow.left;
		boxSprite.left = MAX(fCenter - fWidth*0.5f, boxWindow.left);
		boxSprite.right = MIN(fCenter + fWidth*0.5f, boxWindow.right);
		ui_WidgetSetCBox(UI_WIDGET(pNode->backgroundScrollSprite), &boxSprite);
		g_ui_State.drawZ = uiBackgroundZ;
		ui_SpriteDraw(pNode->backgroundScrollSprite, UI_PARENT_VALUES);
		g_ui_State.drawZ = uiNewZ;
	}
}

void ui_FlowchartNodeTick(UIFlowchartNode *pNode, UI_PARENT_ARGS)
{
	if (pNode->flow)
	{
		const CBox *box = mouseClipGet();
		if (box)
			pNode->flow->clip = *box;
		else
			BuildCBox(&pNode->flow->clip, 0, 0, g_ui_State.screenWidth, g_ui_State.screenHeight);
	}

	ui_FlowchartNodeReflow(pNode);
	ui_WindowTick(UI_WINDOW(pNode), UI_PARENT_VALUES);
}

void ui_FlowchartNodeAddChild(UIFlowchartNode *pNode, UIWidget *pWidget, bool isAfter)
{
    if (!isAfter)
    {
        ui_WidgetAddChild(UI_WIDGET(pNode->beforePane), pWidget);
    }
    else
    {
        ui_WidgetAddChild(UI_WIDGET(pNode->afterPane), pWidget);
    }
}

void ui_FlowchartNodeRemoveChild(UIFlowchartNode *pNode, UIWidget *pWidget)
{
    ui_WidgetRemoveChild(UI_WIDGET(pNode->beforePane), pWidget);
	ui_WidgetRemoveChild(UI_WIDGET(pNode->afterPane), pWidget);
}

void ui_FlowchartNodeRemoveAllChildren(UIFlowchartNode *pNode, bool removeBefore, bool removeAfter)
{
    if (removeBefore)
    {
        while (eaSize(&UI_WIDGET(pNode->beforePane)->children) != 0)
        {
            ui_WidgetRemoveChild(UI_WIDGET(pNode->beforePane), UI_WIDGET(pNode->beforePane)->children[0]);
        }
    }

	if (removeAfter)
	{
		while (eaSize(&UI_WIDGET(pNode->afterPane)->children) != 0)
		{
			ui_WidgetRemoveChild(UI_WIDGET(pNode->afterPane), UI_WIDGET(pNode->afterPane)->children[0]);
		}
	}
}

void ui_FlowchartNodeReflow(UIFlowchartNode *pNode)
{
	int panelY = (ui_WidgetGetHeight(UI_WIDGET(pNode->beforePane)) + 2
                  + MAX(ui_FlowchartReflow(pNode, &pNode->inputButtons),
                        ui_FlowchartReflow(pNode, &pNode->outputButtons)));
	int minH = panelY + ui_WidgetGetHeight(UI_WIDGET(pNode->afterPane));

	ui_WidgetSetPosition(UI_WIDGET(pNode->afterPane), 0, panelY);
	
	ui_WindowSetDimensions(UI_WINDOW(pNode), pNode->window.widget.width,
						   (pNode->autoResize ? minH : pNode->window.widget.height),
						   pNode->window.minW, minH );
}

UIFlowchartButton *ui_FlowchartNodeFindButtonByName(SA_PARAM_NN_VALID UIFlowchartNode *node, SA_PARAM_NN_STR const char *text, bool isInput)
{
	FOR_EACH_IN_EARRAY(*(isInput?&node->inputButtons:&node->outputButtons), UIFlowchartButton, fbutton)
	{
		const char* widgetText = ui_WidgetGetText(UI_WIDGET(fbutton->label));
		if (stricmp(widgetText, text)==0)
			return fbutton;
	}
	FOR_EACH_END;
	return NULL;
}

void ui_FlowchartButtonSetSingleConnection(UIFlowchartButton *fbutton, bool bSingle)
{
	fbutton->bSingleConnection = bSingle;
}

UIFlowchart *ui_FlowchartCreate(UIFlowchartLinkFunc startLinkF, UIFlowchartLinkFunc linkedF, UIFlowchartLinkFunc unlinkedF, UserData userData)
{
	UIFlowchart *flow = (UIFlowchart *)calloc(1, sizeof(UIFlowchart));
	flow->startLinkF = startLinkF;
	flow->startLinkData = userData;
	flow->linkedF = linkedF;
	flow->linkedData = userData;
	flow->unlinkedF = unlinkedF;
	flow->unlinkedData = userData;
	ui_ScrollAreaInitialize(UI_SCROLLAREA(flow), 0, 0, 0, 0, 0, 0, 1, 1);
	UI_SCROLLAREA(flow)->autosize = 1;
	UI_SCROLLAREA(flow)->scrollPadding = 50;
	UI_WIDGET(flow)->freeF = ui_FlowchartFree;

	return flow;
}

void ui_FlowchartFree(UIFlowchart *flow)
{
	ui_FlowchartClear(flow);
	ui_ScrollAreaFreeInternal(UI_SCROLLAREA(flow));
}

void ui_FlowchartClear(SA_PARAM_NN_VALID UIFlowchart *flow)
{
	UIFlowchartButton *oldConnecting = flow->connecting;
	UIFlowchartLinkFunc oldLinkedF = flow->linkedF;
	UIFlowchartLinkFunc oldStartLinkF = flow->startLinkF;
	UIFlowchartLinkFunc oldUnlinkedF = flow->unlinkedF;
	
	int i;
	flow->connecting = NULL;
	flow->linkedF = NULL;
	flow->startLinkF = NULL;
	flow->unlinkedF = NULL; // Don't call these callbacks while freeing!
		
	for (i=eaSize(&flow->nodeWindows)-1; i>=0; i--) 
	{
		UIWindow *pWindow = UI_WINDOW(flow->nodeWindows[i]);
		ui_WidgetRemoveChild(UI_WIDGET(flow), UI_WIDGET(pWindow));
		ui_WindowFreeInternal(pWindow);
	}
	eaDestroy(&flow->nodeWindows);

	flow->connecting = oldConnecting;
	flow->linkedF = oldLinkedF;
	flow->startLinkF = oldStartLinkF;
	flow->unlinkedF = oldUnlinkedF;
}

void ui_FlowchartSwap(UIFlowchartButton *pButton1, UIFlowchartButton *pButton2)
{
	UIFlowchart *flow = pButton1->flow;
	bool bLink = true;
	UIFlowchartButton **pConnected1 = NULL, **pConnected2 = NULL;
	S32 i;

	devassertmsg(pButton1->flow == pButton2->flow, "Trying to swap two sockets not in the same flowchart");
	devassertmsg(pButton1->output == pButton2->output, "Trying to swap an input with an output socket");

	if (bLink && flow->linkBeginF)
		flow->linkBeginF(flow, pButton1, pButton2, false, flow->linkBeginData);

	// Disconnect everything we can from button 1.
	for (i = eaSize(&pButton1->connected) - 1; i >= 0; i--)
	{
		UIFlowchartButton *pOther = pButton1->connected[i];
		UIFlowchartButton *pOutput = pButton1->output ? pButton1 : pOther;
		UIFlowchartButton *pInput = !pButton1->output ? pButton1 : pOther;
		if (ui_FlowchartUnlink(pOutput, pInput, false))
			eaPush(&pConnected1, pOther);
	}

	// Disconnect everything we can from button 2.
	for (i = eaSize(&pButton2->connected) - 1; i >= 0; i--)
	{
		UIFlowchartButton *pOther = pButton2->connected[i];
		UIFlowchartButton *pOutput = pButton2->output ? pButton2 : pOther;
		UIFlowchartButton *pInput = !pButton2->output ? pButton2 : pOther;
		if (ui_FlowchartUnlink(pOutput, pInput, false))
			eaPush(&pConnected2, pOther);
	}

	// Connect everything that was connected to button 1, to button 2.
	for (i = 0; i < eaSize(&pConnected1); i++)
	{
		UIFlowchartButton *pOther = pConnected1[i];
		UIFlowchartButton *pOutput = pButton2->output ? pButton2 : pOther;
		UIFlowchartButton *pInput = !pButton2->output ? pButton2 : pOther;
		ui_FlowchartLink(pOutput, pInput);
	}

	// Connect everything that was connected to button 2, to button 1.
	for (i = 0; i < eaSize(&pConnected2); i++)
	{
		UIFlowchartButton *pOther = pConnected2[i];
		UIFlowchartButton *pOutput = pButton1->output ? pButton1 : pOther;
		UIFlowchartButton *pInput = !pButton1->output ? pButton1 : pOther;
		ui_FlowchartLink(pOutput, pInput);
	}
	
	if (flow->linkEndF)
		flow->linkEndF(flow, pButton1, pButton2, bLink, flow->linkEndData);
}

bool ui_FlowchartLink(UIFlowchartButton *source, UIFlowchartButton *dest)
{
	UIFlowchart *flow = source->flow;
	bool bLink = true;
	devassertmsg(source->flow == dest->flow, "Trying to link two nodes not in the same flowchart");
	devassertmsg(source->output && !dest->output, "Source and dest sockets are swapped (or the same)");

	if (bLink && flow->linkBeginF)
		bLink = flow->linkBeginF(flow, source, dest, false, flow->linkBeginData);

	if (bLink && source->bSingleConnection && eaSize(&source->connected))
		bLink = ui_FlowchartUnlink(source, source->connected[0], false);

	if (bLink && dest->bSingleConnection && eaSize(&dest->connected))
		bLink = ui_FlowchartUnlink(dest->connected[0], dest, false);

	if (bLink)
	{
		eaPushUnique(&source->connected, dest);
		eaPushUnique(&dest->connected, source);
		if (flow->linkedF && !flow->linkedF(flow, source, dest, false, flow->linkedData))
		{
			eaFindAndRemove(&source->connected, dest);
			eaFindAndRemove(&dest->connected, source);
			bLink = false;
		}
	}

	if (flow->linkEndF)
		flow->linkEndF(flow, source, dest, bLink, flow->linkEndData);

	return bLink;
}

bool ui_FlowchartUnlink(UIFlowchartButton *source, UIFlowchartButton *dest, bool force)
{
	UIFlowchart *flow = source->flow;
	bool unlink = true;
	devassertmsg(source->flow == dest->flow, "Trying to unlink two nodes not in the same flowchart");
	devassertmsg(source->output && !dest->output, "Source and dest sockets are swapped (or the same)");

	if (flow->unlinkedF)
		unlink = flow->unlinkedF(flow, source, dest, force, flow->unlinkedData);
	if (unlink || force)
	{
		eaFindAndRemove(&source->connected, dest);
		eaFindAndRemove(&dest->connected, source);
	}
	return unlink;
}

static bool FlowchartNodeClosed(UIWindow *window, UIFlowchart *flow)
{
	UIFlowchartNode *node = (UIFlowchartNode*)window;
	assert(node->flow == flow);
	ui_FlowchartRemoveNode(flow, node);
	return node->flow == NULL;
}

void ui_FlowchartAddNode(UIFlowchart *flow, UIFlowchartNode *node)
{
	eaPush(&flow->nodeWindows, node);

	node->flow = flow;

	ui_WindowSetCloseCallback(UI_WINDOW(node), FlowchartNodeClosed, flow);
	ui_WidgetAddChild(UI_WIDGET(flow), UI_WIDGET(node));
}

void ui_FlowchartRemoveNode(UIFlowchart *flow, UIFlowchartNode *window)
{
	int i;
	assert(flow == window->flow);

	if (flow->nodeRemovedF && !flow->nodeRemovedF(flow, window, flow->nodeRemovedData))
	{
		return;
	}

	for (i = 0; i < eaSize(&window->inputButtons); i++)
	{
		if (window->inputButtons[i])
			ui_FlowchartButtonUnlink(window->inputButtons[i], true);
	}
	for (i = 0; i < eaSize(&window->outputButtons); i++)
	{
		if (window->outputButtons[i])
			ui_FlowchartButtonUnlink(window->outputButtons[i], true);
	}

	if (flow->nodeRemovedLateF)
	{
		flow->nodeRemovedLateF(flow, window, flow->nodeRemovedLateData);
	}
	eaFindAndRemove(&flow->nodeWindows, window);
	window->flow = NULL;
	ui_WidgetRemoveChild(UI_WIDGET(flow), UI_WIDGET(window));
}

void ui_FlowchartSetLinkedCallback(UIFlowchart *flow, UIFlowchartLinkFunc linkedF, UserData linkedData)
{
	flow->linkedF = linkedF;
	flow->linkedData = linkedData;
}

void ui_FlowchartSetUnlinkedCallback(UIFlowchart *flow, UIFlowchartLinkFunc unlinkedF, UserData unlinkedData)
{
	flow->unlinkedF = unlinkedF;
	flow->unlinkedData = unlinkedData;
}

void ui_FlowchartSetNodeRemovedCallback(SA_PARAM_NN_VALID UIFlowchart *flow, UIFlowchartNodeFunc nodeRemovedF, UserData nodeRemovedData)
{
	flow->nodeRemovedF = nodeRemovedF;
	flow->nodeRemovedData = nodeRemovedData;
}

void ui_FlowchartSetNodeRemovedLateCallback(SA_PARAM_NN_VALID UIFlowchart *flow, UIFlowchartNodeFunc nodeRemovedLateF, UserData nodeRemovedLateData)
{
	flow->nodeRemovedLateF = nodeRemovedLateF;
	flow->nodeRemovedLateData = nodeRemovedLateData;
}

void ui_FlowchartSetStartLinkCallback(UIFlowchart *flow, UIFlowchartLinkFunc startLinkF, UserData startLinkData)
{
	flow->startLinkF = startLinkF;
	flow->startLinkData = startLinkData;
}

void ui_FlowchartSetLinkBeginCallback(SA_PARAM_NN_VALID UIFlowchart *flow, UIFlowchartLinkFunc linkBeginF, UserData linkBeginData)
{
	flow->linkBeginF = linkBeginF;
	flow->linkBeginData = linkBeginData;
}

void ui_FlowchartSetLinkEndCallback(SA_PARAM_NN_VALID UIFlowchart *flow, UIFlowchartLinkFunc linkEndF, UserData linkEndData)
{
	flow->linkEndF = linkEndF;
	flow->linkEndData = linkEndData;
}

void ui_FlowchartSetReadOnly(UIFlowchart *pFlow, bool bReadOnly)
{
	S32 i;
	for (i = 0; i < eaSize(&pFlow->nodeWindows); i++)
		ui_FlowchartNodeSetReadOnly(pFlow->nodeWindows[i], bReadOnly);
}

void ui_FlowchartNodeSetReadOnly(UIFlowchartNode *pNode, bool bReadOnly)
{
	S32 i;
	for (i = 0; i < eaSize(&pNode->inputButtons); i++)
		ui_SetActive(UI_WIDGET(pNode->inputButtons[i]), !bReadOnly);
	for (i = 0; i < eaSize(&pNode->outputButtons); i++)
		ui_SetActive(UI_WIDGET(pNode->outputButtons[i]), !bReadOnly);
}

UIFlowchartNode *ui_FlowchartFindNode(SA_PARAM_NN_VALID UIFlowchart *flow, SA_PARAM_NN_VALID UserData userData)
{
	FOR_EACH_IN_EARRAY(flow->nodeWindows, UIFlowchartNode, node)
	{
		if (node->userData == userData)
			return node;
	}
	FOR_EACH_END;
	return NULL;
}

UIFlowchartNode *ui_FlowchartFindNodeFromName(SA_PARAM_NN_VALID UIFlowchart *flow, SA_PARAM_NN_VALID const char *pNodeName)
{
	FOR_EACH_IN_EARRAY(flow->nodeWindows, UIFlowchartNode, node)
	{
		const char *pText = ui_WidgetGetText(UI_WIDGET(node));
		if (pText && stricmp(pText, pNodeName) == 0)
			return node;
	}
	FOR_EACH_END;
	return NULL;
}

void ui_FlowchartFindValidPos(SA_PARAM_NN_VALID UIFlowchart *flow, SA_PRE_NN_FREE SA_POST_NN_VALID Vec2 pos, F32 width, F32 height, F32 minx, F32 miny, F32 preferredx, F32 grid_size)
{
	// TODO: find the nearest grid-aligned location satisfying minx, miny
	setVec2(pos, preferredx, miny);
}
