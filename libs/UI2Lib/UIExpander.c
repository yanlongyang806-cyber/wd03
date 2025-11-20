/***************************************************************************



***************************************************************************/

#include "earray.h"

#include "inputMouse.h"
#include "inputText.h"

#include "Color.h"
#include "GfxClipper.h"
#include "GfxSprite.h"
#include "GfxSpriteText.h"
#include "GfxTexAtlas.h"

#include "UIExpander.h"
#include "UIScrollbar.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

void ui_ExpanderDraw(UIExpander *expand, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(expand);
	if (!expand->hidden && clipperIntersects(&box))
	{
		F32 eff_x=x;
		F32 labelWidth, labelHeight;
		AtlasTex *toggle = !expand->widget.childrenInactive ? g_ui_Tex.minus : g_ui_Tex.plus;
		UIStyleFont *font;
		Color buttonColor;
		CBox headerBox;
		F32 headerHeight;
		const char* widgetText = ui_WidgetGetText( UI_WIDGET( expand ));

		UI_DRAW_EARLY(expand);

		if (!UI_GET_SKIN(expand))
		{
			font = GET_REF(g_ui_State.font);
			buttonColor = ColorWhite;
		}
		else if (!ui_IsActive(UI_WIDGET(expand)))
		{
			font = GET_REF(UI_GET_SKIN(expand)->hNormal);
			buttonColor = UI_GET_SKIN(expand)->button[3];
		}
		else
		{
			font = GET_REF(UI_GET_SKIN(expand)->hNormal);
			buttonColor = UI_GET_SKIN(expand)->button[0];
		}

		ui_StyleFontUse(font, false, UI_WIDGET(expand)->state);
		headerHeight = (ui_StyleFontLineHeight(font, scale) + UI_STEP_SC);
		ui_WidgetGroupGetDimensions(&expand->labelChildren, &labelWidth, &labelHeight, UI_MY_VALUES);
		MAX1(headerHeight, labelHeight + UI_HSTEP_SC);

		BuildCBox(&headerBox, x, y, w, headerHeight);
		// Roll-over highlight
		if ((ui_IsHovering(UI_WIDGET(expand)) || ui_IsFocused(expand)) && UI_GET_SKIN(expand))
		{
			Color c = ColorLighten(UI_GET_SKIN(expand)->background[0], 32);
			display_sprite_box(g_ui_Tex.white, &headerBox, z, RGBAFromColor(c));
		}

		// Toggle button
		display_sprite(toggle, x + UI_HSTEP_SC, floorf(y + headerHeight / 2 - toggle->height * scale / 2), z + 0.01, scale, scale,
			(UI_GET_SKIN(expand) ? RGBAFromColor(buttonColor) : 0xFFFFFFFF));
		eff_x += (UI_STEP + toggle->width) * scale;

		// Label
		if (widgetText && widgetText[0])
			gfxfont_Printf(eff_x, y + headerHeight/2, z + 0.01, scale, scale, CENTER_Y, "%s", widgetText);

		// Description
		if (expand->pchDescription)
			gfxfont_Printf(eff_x + expand->iDescriptionOffset * scale, y + headerHeight/2, z + 0.01, scale, scale, CENTER_Y, "%s", expand->pchDescription);

		// Other widgets added as a label (e.g. SMF)
		// TODO: Unify Description and Label as just being generic widgets in this list.
		//   (need to be able to color tint child widgets some how though!)
		ui_WidgetGroupDraw(&expand->labelChildren, eff_x, y, w - (eff_x - x), h, scale);

		if (!expand->widget.childrenInactive)
		{
			y += headerHeight;
			h -= headerHeight;
			ui_WidgetGroupDraw(&expand->widget.children, UI_MY_VALUES);
		}
		ui_DrawAndDecrementOverlay(UI_WIDGET(expand), &box, z);
		clipperPop();
	}
}

void ui_ExpanderTick(UIExpander *expand, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(expand);
	F32 eff_x=x;
	F32 labelWidth, labelHeight;
	UIStyleFont *font = GET_REF(UI_GET_SKIN(expand)->hNormal);
	AtlasTex *toggle = !expand->widget.childrenInactive ? (g_ui_Tex.minus) : (g_ui_Tex.plus);
	F32 headerHeight;
	CBox headerBox;
	bool clicked=false;

	if (!ui_IsActive(UI_WIDGET(expand)))
		return;

	ui_StyleFontUse(font, false, UI_WIDGET(expand)->state);
	headerHeight = (ui_StyleFontLineHeight(font, scale) + UI_STEP_SC);
	ui_WidgetGroupGetDimensions(&expand->labelChildren, &labelWidth, &labelHeight, UI_MY_VALUES);
	MAX1(headerHeight, labelHeight + UI_HSTEP_SC);

	BuildCBox(&headerBox, x, y, w, headerHeight);
	CBoxClipTo(&pBox, &headerBox);

	if (mouseDownHit(MS_LEFT, &box) || mouseDownHit(MS_RIGHT, &box))
		// Make sure this expander draws its children above others.
		ui_WidgetGroupSteal(expand->widget.group, UI_WIDGET(expand));

	eff_x += (UI_STEP + toggle->width) * scale;

	ui_WidgetGroupTick(&expand->labelChildren, eff_x, y, w - (eff_x - x), h, scale);

	if (mouseClickHit(MS_LEFT, &headerBox) || mouseDoubleClickHit(MS_LEFT, &headerBox))
	{
		ui_ExpanderToggle(expand);

		if (expand->widget.childrenInactive == 0 && expand->autoScroll)
		{
			// If we just opened an expander and are in a scroll area, scroll to show new item
			ui_ScrollbarParentScrollTo(x, y + UI_WIDGET(expand)->height * scale);
			ui_ScrollbarParentScrollTo(x, y);
		}
		inpHandled();
		clicked = true;
	}

	if (!expand->widget.childrenInactive)
	{
		y += headerHeight;
		h -= headerHeight;
		ui_WidgetGroupTick(&expand->widget.children, UI_MY_VALUES);
	}

	// After ticking children, otherwise tooltips on children don't work!
	UI_TICK_EARLY(expand, false, true);

	ui_SetHovering(UI_WIDGET(expand), clicked || mouseCollision(&headerBox));

	if (expand->headerContextF && mouseClickHit(MS_RIGHT, &headerBox))
	{
		expand->headerContextF(expand, expand->headerContextData);
		inpHandled();
	}

	// Close expander on middle click (a la CoH debug menus)
	if (!expand->widget.childrenInactive && mouseClickHit(MS_MID, &box))
	{
		ui_ExpanderToggle(expand);
		inpHandled();
	}

	UI_TICK_LATE(expand);
}

UIExpander *ui_ExpanderCreate(const char *title, F32 openedHeight)
{
	UIExpander *expand = (UIExpander *)calloc(1, sizeof(UIExpander));
	ui_ExpanderInitialize(expand, title, openedHeight);
	return expand;
}


bool ui_ExpanderInput(UIExpander *expand, KeyInput *input)
{
	if (KIT_EditKey == input->type && ui_IsActive(UI_WIDGET(expand)))
	{
		if (input->scancode == INP_LEFT)
		{
			ui_ExpanderSetOpened(expand, false);
			return true;
		}
		else if (input->scancode == INP_RIGHT)
		{
			ui_ExpanderSetOpened(expand, true);
			return true;
		}
		else if (input->scancode == INP_SPACE || input->scancode == INP_RETURN)
		{
			ui_ExpanderToggle(expand);
		}
	}
	return false;
}

void ui_ExpanderInitialize(UIExpander *expand, const char *title, F32 openedHeight) 
{
	ui_WidgetInitialize(UI_WIDGET(expand), ui_ExpanderTick, ui_ExpanderDraw, ui_ExpanderFreeInternal, ui_ExpanderInput, ui_WidgetDummyFocusFunc);
	expand->openedHeight = openedHeight;
	expand->widget.childrenInactive = true; // Default to closed
	ui_WidgetSetTextString(UI_WIDGET(expand), title);
	ui_ExpanderReflow(expand);
	expand->iDescriptionOffset = 150; //default offset value
	expand->bFirstRefresh = true;
}

void ui_ExpanderSetName(UIExpander *expand, const char *pchText)
{
	ui_WidgetSetTextString(UI_WIDGET(expand), pchText);
}

void ui_ExpanderFreeInternal(UIExpander *expand)
{
	ui_WidgetGroupFreeInternal(&expand->labelChildren);
	SAFE_FREE(expand->pchDescription);
	ui_WidgetFreeInternal(UI_WIDGET(expand));
}

void ui_ExpanderAddChild(UIExpander *expand, UIAnyWidget *child)
{
	ui_WidgetGroupAdd(&expand->widget.children, (UIWidget *)child);
}

void ui_ExpanderRemoveChild(UIExpander *expand, UIAnyWidget *child)
{
	ui_WidgetGroupRemove(&expand->widget.children, (UIWidget *)child);
}

void ui_ExpanderAddLabel(UIExpander *expand, UIWidget *child)
{
	ui_WidgetGroupAdd(&expand->labelChildren, child);
}

void ui_ExpanderRemoveLabel(UIExpander *expand, UIWidget *child)
{
	ui_WidgetGroupRemove(&expand->labelChildren, child);
}

void ui_ExpanderToggle(UIExpander *expand)
{
	expand->widget.childrenInactive ^= true;
	ui_ExpanderReflow(expand);
	if (expand->expandF)
		expand->expandF(expand, expand->expandData);
}

void ui_ExpanderSetExpandCallback(UIExpander *expand, UIActivationFunc expandF, UserData expandData)
{
	expand->expandF = expandF;
	expand->expandData = expandData;
}

void ui_ExpanderSetHeaderContextCallback(UIExpander *expand, UIActivationFunc rclickF, UserData rclickData)
{
	expand->headerContextF = rclickF;
	expand->headerContextData = rclickData;
}


void ui_ExpanderSetHeight(UIExpander *expand, F32 openedHeight)
{
	expand->openedHeight = openedHeight;
	ui_ExpanderReflow(expand);
}

void ui_ExpanderReflow(UIExpander *expand)
{
	F32 totalHeight;
	UIStyleFont *font = GET_REF(UI_GET_SKIN(expand)->hNormal);

	ui_StyleFontUse(font, false, UI_WIDGET(expand)->state);
	totalHeight = ui_StyleFontLineHeight(font, 1.f) + UI_STEP;
	if (!expand->widget.childrenInactive)
	{
		totalHeight += expand->openedHeight;
		if (expand->openedWidth)
		{
			expand->widget.width = expand->openedWidth;
			expand->widget.widthUnit = UIUnitFixed;
		}

	}
	else if (expand->openedWidth)
	{
		expand->widget.width = 1.f;
		expand->widget.widthUnit = UIUnitPercentage;
	}

	expand->widget.heightUnit = UIUnitFixed;
	expand->widget.height = totalHeight;

	if (expand->group)
		ui_ExpanderGroupReflow(expand->group);
}

bool ui_ExpanderIsOpened(UIExpander *expand)
{
	return !expand->widget.childrenInactive;
}

void ui_ExpanderSetOpened(UIExpander *expand, bool opened)
{
	// Cannot compare "opened" directly with "expand->widget.childrenInactive" since
	// comparing a bitfield to a bool doesn't work like you think it might
	if ((expand->widget.childrenInactive && opened) ||
		(!expand->widget.childrenInactive && !opened))
		ui_ExpanderToggle(expand);
}


void ui_ExpanderGroupTick(UIExpanderGroup *group, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(group);
	int i;
	F32 totalHeight = 0, maxWidth;
	F32 savedHeight;

	CBoxClipTo(&pBox, &box);
	if (group->widget.sb->scrollY)
		w -= ui_ScrollbarWidth(UI_WIDGET(group)->sb) * scale;
	if (group->widget.sb->scrollX)
		h -= ui_ScrollbarHeight(UI_WIDGET(group)->sb) * scale;
	BuildCBox(&box, x, y, w, h);
	CBoxClipTo(&pBox, &box);
	mouseClipPushRestrict(&box);

	maxWidth = w / scale;

	if (mouseDownHit(MS_LEFT, &box) || mouseDownHit(MS_RIGHT, &box))
		ui_WidgetGroupSteal(group->widget.group, UI_WIDGET(group));

	savedHeight = totalHeight;

	for (i = 0; i < eaSize(&group->childrenInOrder); i++)
	{
		totalHeight += group->childrenInOrder[i]->height;
		maxWidth = MAX(maxWidth, group->childrenInOrder[i]->width);
	}

	if (group->widget.sb->scrollX || group->widget.sb->scrollY)
		ui_ScrollbarPushState(group->widget.sb, x, y, w, h, scale, maxWidth * scale, totalHeight * scale);
	ui_WidgetGroupTick(&group->widget.children, x - group->widget.sb->xpos, y - group->widget.sb->ypos, maxWidth * scale, totalHeight * scale, scale);	
	if (group->widget.sb->scrollX || group->widget.sb->scrollY)
		ui_ScrollbarPopState();
	mouseClipPop();

	totalHeight = savedHeight;
	for (i = 0; i < eaSize(&group->childrenInOrder); i++)
	{
		totalHeight += group->childrenInOrder[i]->height;
		maxWidth = MAX(maxWidth, group->childrenInOrder[i]->width);
	}

	ui_ScrollbarTick(group->widget.sb, x, y, w, h, z, scale, maxWidth * scale, totalHeight * scale);
	

}

void ui_ExpanderGroupDraw(UIExpanderGroup *group, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(group);
	F32 totalHeight = 0, maxWidth;
	int i;
	Color borderColor;
	CBox outerbox = box;

	if (group->widget.sb->scrollY)
		w -= ui_ScrollbarWidth(UI_WIDGET(group)->sb) * scale;
	if (group->widget.sb->scrollX)
		h -= ui_ScrollbarHeight(UI_WIDGET(group)->sb) * scale;
	BuildCBox(&box, x, y, w, h);
	CBoxClipTo(&pBox, &box);

	maxWidth = w / scale;

	for (i = 0; i < eaSize(&group->childrenInOrder); i++)
	{
			totalHeight += group->childrenInOrder[i]->height;
			maxWidth = MAX(maxWidth, group->childrenInOrder[i]->width);
	}

	clipperPushRestrict(&box);
	ui_ScrollbarPushState(group->widget.sb, x, y, w, h, scale, maxWidth * scale, totalHeight * scale);
	ui_WidgetGroupDraw(&group->widget.children, x - group->widget.sb->xpos, y - group->widget.sb->ypos, maxWidth * scale, totalHeight * scale, scale);
	ui_ScrollbarPopState();
	clipperPop();
	ui_ScrollbarDraw(group->widget.sb, x, y, w, h, z, scale, maxWidth * scale, totalHeight * scale);

	if (GET_REF(group->hBorder)) 
	{
		if (UI_GET_SKIN(group))
			borderColor = UI_GET_SKIN(group)->thinBorder[0];
		else
			borderColor = ColorBlack;

		ui_StyleBorderDraw(GET_REF(group->hBorder), &outerbox, RGBAFromColor(borderColor), RGBAFromColor(borderColor), g_ui_State.drawZ + UI_EXPANDER_Z_LINES, scale, 255);
	}
}

UIExpanderGroup *ui_ExpanderGroupCreate(void)
{
	UIExpanderGroup *group = (UIExpanderGroup *)calloc(1, sizeof(UIExpanderGroup));
	ui_WidgetInitialize(UI_WIDGET(group), ui_ExpanderGroupTick, ui_ExpanderGroupDraw, ui_ExpanderGroupFreeInternal, NULL, NULL);
	group->widget.sb = ui_ScrollbarCreate(false, true);
	group->widget.sb->alwaysScrollY = false;
	return group;
}

void ui_ExpanderGroupFreeInternal(UIExpanderGroup *group)
{
	REMOVE_HANDLE(group->hBorder);
	eaDestroy(&group->childrenInOrder);
	ui_WidgetFreeInternal(UI_WIDGET(group));
}

static void ExpanderReflowGroup(UIExpander *expand, UIExpanderGroup *group)
{
	ui_ExpanderGroupReflow(group);
}

void ui_ExpanderGroupAddExpander(UIExpanderGroup *group, UIExpander *expand)
{
	devassertmsg(expand->group == group || !expand->group, "Expander already has a group");
	if (expand->openedWidth)
		group->widget.sb->scrollX = true;
	expand->group = group;
	expand->widget.width = 1.f;
	expand->widget.widthUnit = UIUnitPercentage;
	ui_ExpanderGroupAddWidget(group, UI_WIDGET(expand));
}

void ui_ExpanderGroupInsertExpander(UIExpanderGroup *group, UIExpander *expand, int pos)
{
	devassertmsg(expand->group == group || !expand->group, "Expander already has a group");
	if (expand->openedWidth)
		group->widget.sb->scrollX = true;
	expand->group = group;
	expand->widget.width = 1.f;
	expand->widget.widthUnit = UIUnitPercentage;
	ui_ExpanderGroupInsertWidget(group, UI_WIDGET(expand), pos);
}

void ui_ExpanderGroupRemoveExpander(UIExpanderGroup *group, UIExpander *expand)
{
	if (expand)
	{
		ui_ExpanderGroupRemoveWidget(group, UI_WIDGET(expand));
		expand->group = NULL;
	}
}

void ui_ExpanderGroupAddWidget(UIExpanderGroup *group, UIWidget *widget)
{
	eaPush(&group->childrenInOrder, widget);
	ui_WidgetGroupAdd(&group->widget.children, widget);
	ui_ExpanderGroupReflow(group);
}

void ui_ExpanderGroupInsertWidget(UIExpanderGroup *group, UIWidget *widget, int pos)
{
	ui_WidgetGroupAdd(&group->widget.children, widget);
	eaInsert(&group->childrenInOrder, widget, pos);
	ui_ExpanderGroupReflow(group);
}

void ui_ExpanderGroupRemoveWidget(UIExpanderGroup *group, UIWidget *widget)
{
	eaFindAndRemove(&group->childrenInOrder, widget);
	ui_WidgetGroupRemove(&group->widget.children, widget);
	ui_ExpanderGroupReflow(group);
}

void ui_ExpanderGroupReflow(UIExpanderGroup *group)
{
	F32 sumHeight = 0;
	int i;
	for (i = 0; i < eaSize(&group->childrenInOrder); i++)
	{
		UIWidget *expand = group->childrenInOrder[i];
		expand->y = sumHeight;
		sumHeight += expand->height + group->spacing;
	}
	group->totalHeight = sumHeight;

	if (!group->widget.sb->scrollY)
	{
		group->widget.height = group->totalHeight;
		group->widget.heightUnit = UIUnitFixed;
	}

	if (group->reflowF)
		group->reflowF(group, group->reflowData);
}

void ui_ExpanderSetDescriptionText(UIExpander *expand, char *pchText)
{
	SAFE_FREE(expand->pchDescription);
	expand->pchDescription = pchText ? strdup(pchText) : NULL;
}

void ui_ExpanderGroupSetGrow(UIExpanderGroup *group, bool grow)
{
	group->widget.sb->scrollY = !grow;
	ui_ExpanderGroupReflow(group);
}

void ui_ExpanderGroupSetSpacing(SA_PARAM_NN_VALID UIExpanderGroup *group, F32 spacing)
{
	group->spacing = spacing;
	ui_ExpanderGroupReflow(group);
}

void ui_ExpanderGroupSetReflowCallback(UIExpanderGroup *group, UIActivationFunc reflowF, UserData reflowData)
{
	group->reflowF = reflowF;
	group->reflowData = reflowData;
}
