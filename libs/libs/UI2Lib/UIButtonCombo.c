#include "EArray.h"
#include "GfxClipper.h"
#include "GfxSprite.h"
#include "GfxTexAtlas.h"
#include "inputMouse.h"
#include "UIButtonCombo.h"
#include "UISprite.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

static void uiButtonComboInvalidate(UIButtonCombo *pButtonCombo)
{
	if(pButtonCombo->selected_item)
	{
		ui_WidgetRemoveFromGroup(UI_WIDGET(pButtonCombo->selected_item->sprite));
		if(pButtonCombo->selected_item->active_sprite)
			ui_WidgetRemoveFromGroup(UI_WIDGET(pButtonCombo->selected_item->active_sprite));
		if(pButtonCombo->selected_item->active_label)
			ui_WidgetRemoveFromGroup(UI_WIDGET(pButtonCombo->selected_item->active_label));
	}
}

static void uiButtonComboRefresh(UIButtonCombo *pButtonCombo)
{
	if(pButtonCombo->selected_item)
	{
		if(pButtonCombo->active && pButtonCombo->selected_item->active_sprite)
			ui_ButtonAddChild(pButtonCombo->pButton, UI_WIDGET(pButtonCombo->selected_item->active_sprite));
		else
			ui_ButtonAddChild(pButtonCombo->pButton, UI_WIDGET(pButtonCombo->selected_item->sprite));
		if (pButtonCombo->selected_item->active_label)
			ui_ButtonAddChild(pButtonCombo->pButton, UI_WIDGET(pButtonCombo->selected_item->active_label));
		ui_WidgetSetTooltipString(UI_WIDGET(pButtonCombo), ui_WidgetGetTooltip(UI_WIDGET(pButtonCombo->selected_item->button)));
	}
}

void uiButtonComboSelectItem(UIButtonCombo *pButtonCombo, UIButtonComboItem *pButtonComboItem)
{
	uiButtonComboInvalidate(pButtonCombo);
	pButtonCombo->selected_item = pButtonComboItem;
	uiButtonComboRefresh(pButtonCombo);
}

void ui_ButtonComboItemClickCB(UIButton *pButton, UIButtonComboItem *pButtonComboItem)
{
	uiButtonComboSelectItem(pButtonComboItem->parent, pButtonComboItem);
	pButtonComboItem->parent->pPane->invisible = true;

	if(pButtonComboItem->clickedF)
		pButtonComboItem->clickedF(pButton, pButtonComboItem->clickedData);
}


UIButton* ui_ButtonComboAddItem(UIButtonCombo *pButtonCombo, const char *texture, const char *active_texture, const char *label, UIActivationFunc clickedF, UserData clickedData)
{
	return ui_ButtonComboAddOrderedItem(pButtonCombo, texture, active_texture, label, 0, clickedF, clickedData);
}

UIButton* ui_ButtonComboAddOrderedItem(UIButtonCombo *pButtonCombo, const char *texture, const char *active_texture, const char *label, U8 order, UIActivationFunc clickedF, UserData clickedData)
{
	int i;
	UIButtonComboItem *new_item = calloc(1, sizeof(UIButtonComboItem));

	new_item->button = ui_ButtonCreateImageOnly(texture, 0, 0, ui_ButtonComboItemClickCB, new_item);
	new_item->sprite = ui_SpriteCreate(0, 0, -1.f, -1.f, texture);
	new_item->sprite->widget.uClickThrough = true;
	if(active_texture) {
		new_item->active_sprite = ui_SpriteCreate(0, 0, -1.f, -1.f, active_texture);
		new_item->active_sprite->widget.uClickThrough = true;
	} else
		new_item->active_sprite = NULL;
	new_item->clickedF = clickedF;
	new_item->clickedData = clickedData;
	new_item->parent = pButtonCombo;
	new_item->order = order;
	new_item->icon_width = 10;

	ui_WidgetSetDimensions(UI_WIDGET(new_item->button), UI_WIDGET(pButtonCombo)->width, UI_WIDGET(pButtonCombo)->height);
	if (new_item->active_sprite)
	{
		F32 width = (((F32)UI_WIDGET(new_item->active_sprite)->width)/UI_WIDGET(new_item->active_sprite)->height) * UI_WIDGET(pButtonCombo)->height;
		ui_WidgetSetDimensions(UI_WIDGET(new_item->active_sprite), width, UI_WIDGET(pButtonCombo)->height);
		new_item->icon_width = MAX(new_item->icon_width, width);
	}
	if (new_item->sprite)
	{
		F32 width = (((F32)UI_WIDGET(new_item->sprite)->width)/UI_WIDGET(new_item->sprite)->height) * UI_WIDGET(pButtonCombo)->height;
		ui_WidgetSetDimensions(UI_WIDGET(new_item->sprite), width, UI_WIDGET(pButtonCombo)->height);
		ui_ButtonSetImageStretch(new_item->button, true);
		new_item->icon_width = MAX(new_item->icon_width, width);
	}

	ui_WidgetAddChild(UI_WIDGET(pButtonCombo->pPane), UI_WIDGET(new_item->button));
	if (label)
	{
		new_item->label = ui_LabelCreate(label, new_item->icon_width+5, 0);
		new_item->active_label = ui_LabelCreate(label, new_item->icon_width+5, 0);
		ui_WidgetAddChild(UI_WIDGET(new_item->button), UI_WIDGET(new_item->label));
	}

	if(eaSize(&pButtonCombo->items) == 0)
		uiButtonComboSelectItem(pButtonCombo, new_item);
	else if(new_item->order < pButtonCombo->selected_item->order)
		uiButtonComboSelectItem(pButtonCombo, new_item);

	for(i=0; i < eaSize(&pButtonCombo->items); i++)
	{
		if(new_item->order < pButtonCombo->items[i]->order)
			break;
	}
	eaInsert(&pButtonCombo->items, new_item, i);

	return new_item->button;
}

int ui_ButtonComboGetSelected(SA_PARAM_NN_VALID UIButtonCombo *pButtonCombo)
{
	return eaFind(&pButtonCombo->items, pButtonCombo->selected_item);
}

void ui_ButtonComboSetDirection(SA_PARAM_NN_VALID UIButtonCombo *pButtonCombo, UIButtonComboDirection direction)
{
	pButtonCombo->direction = direction;
}

void ui_ButtonComboSetActive(SA_PARAM_NN_VALID UIButtonCombo *pButtonCombo, bool active)
{
	pButtonCombo->active = active;
	uiButtonComboInvalidate(pButtonCombo);
	uiButtonComboRefresh(pButtonCombo);
}

void ui_ButtonComboSetSelected(SA_PARAM_NN_VALID UIButtonCombo *pButtonCombo, int button)
{
	if(button < 0 || eaSize(&pButtonCombo->items) <= button)
		return;

	uiButtonComboSelectItem(pButtonCombo, pButtonCombo->items[button]);

	if(!pButtonCombo->selected_item)
		return;

	if(pButtonCombo->selected_item->clickedF)
		pButtonCombo->selected_item->clickedF(pButtonCombo->selected_item->button, pButtonCombo->selected_item->clickedData);
	if(pButtonCombo->cbChanged)
		pButtonCombo->cbChanged(pButtonCombo, pButtonCombo->pChangedData);
}

void ui_ButtonComboSelectNext(SA_PARAM_NN_VALID UIButtonCombo *pButtonCombo)
{
	int idx;
	if(!pButtonCombo->active)
	{
		pButtonCombo->active = true;
		uiButtonComboInvalidate(pButtonCombo);
		uiButtonComboRefresh(pButtonCombo);
	}
	else
	{
		if((idx = eaFind(&pButtonCombo->items, pButtonCombo->selected_item)) < 0)
			return;

		idx++;
		if(idx >= eaSize(&pButtonCombo->items))
			idx = 0;

		uiButtonComboSelectItem(pButtonCombo, pButtonCombo->items[idx]);
	}
	if(!pButtonCombo->selected_item)
		return;

	if(pButtonCombo->selected_item->clickedF)
		pButtonCombo->selected_item->clickedF(pButtonCombo->selected_item->button, pButtonCombo->selected_item->clickedData);
	if(pButtonCombo->cbChanged)
		pButtonCombo->cbChanged(pButtonCombo, pButtonCombo->pChangedData);
}

static void ui_ButtonComboTogglePaneVisible(UIButtonCombo *pButtonCombo)
{
	if(UI_WIDGET(pButtonCombo->pPane)->group && !pButtonCombo->pPane->invisible)
	{
		pButtonCombo->pPane->invisible = true;
	}
	else if(!UI_WIDGET(pButtonCombo->pPane)->group && pButtonCombo->pPane->invisible && eaSize(&pButtonCombo->items) > 1)
	{
		int i;
		F32 fBorderWidth = 5;
		F32 fBorderHeight = 5;
		int x=0, y=0;
		pButtonCombo->pPane->invisible = false;
		for(i=0; i < eaSize(&pButtonCombo->items); i++)
		{
			ui_WidgetSetPosition(UI_WIDGET(pButtonCombo->items[i]->button), x, y);
			if (pButtonCombo->items[i]->label)
				ui_WidgetSetPosition(UI_WIDGET(pButtonCombo->items[i]->label), pButtonCombo->items[i]->icon_width+5, 0);
			if (pButtonCombo->items[i]->active_label)
				ui_WidgetSetPosition(UI_WIDGET(pButtonCombo->items[i]->active_label), pButtonCombo->items[i]->icon_width+5, 0);
			if(pButtonCombo->direction == POP_LEFT || pButtonCombo->direction == POP_RIGHT)
				x += (UI_WIDGET(pButtonCombo)->width + 3);
			else
				y += (UI_WIDGET(pButtonCombo)->height + 3);

		}
		x+=fBorderWidth;
		y+=fBorderHeight;
		ui_WidgetSetDimensions(UI_WIDGET(pButtonCombo->pPane), MAX((UI_WIDGET(pButtonCombo)->width + fBorderWidth+3), x), MAX((UI_WIDGET(pButtonCombo)->height + fBorderHeight+3), y));
	}
}

static void ui_ButtonComboMainButtonClickedCB(void *unused, UIButtonCombo *pButtonCombo)
{
	if (!pButtonCombo->pComboButton)
	{
		if(pButtonCombo->active || !pButtonCombo->selected_item)
		{
			ui_ButtonComboTogglePaneVisible(pButtonCombo);
		}
		else
		{
			pButtonCombo->active = true;
			uiButtonComboSelectItem(pButtonCombo, pButtonCombo->selected_item);
			if (pButtonCombo->selected_item->clickedF)
				pButtonCombo->selected_item->clickedF(pButtonCombo->selected_item->button, pButtonCombo->selected_item->clickedData);
		}
	}
	else
	{
		if(pButtonCombo->selected_item->clickedF)
			pButtonCombo->selected_item->clickedF(pButtonCombo->selected_item->button, pButtonCombo->selected_item->clickedData);
	}

	if(pButtonCombo->cbChanged)
		pButtonCombo->cbChanged(pButtonCombo, pButtonCombo->pChangedData);
}

static void ui_ButtonComboButtonClickedCB(void *unused, UIButtonCombo *pButtonCombo)
{
	ui_ButtonComboTogglePaneVisible(pButtonCombo);

	if(pButtonCombo->cbChanged)
		pButtonCombo->cbChanged(pButtonCombo, pButtonCombo->pChangedData);
}

void ui_ButtonComboPaneDraw(UIPane *pane, UI_PARENT_ARGS)
{
	bool old_state = pane->invisible;
	pane->invisible = false;
	ui_PaneDraw(pane, UI_PARENT_VALUES);
	pane->invisible = old_state;
}

void ui_ButtonComboPaneTick(UIPane *pane, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(pane);

	if(mouseDown(MS_LEFT) && !mouseDownHit(MS_LEFT, &box))
	{
		pane->invisible = true;
		return;
	}
	ui_PaneTick(pane, UI_PARENT_VALUES);
}

UIButtonCombo *ui_ButtonComboCreate(F32 x, F32 y, F32 width, F32 height, UIButtonComboDirection direction, bool use_combo_button)
{
	int button_width = use_combo_button ? g_ui_Tex.arrowDropDown->width + 10 : 0;
	//Create ButtonCombo
	UIButtonCombo *pButtonCombo = calloc(1, sizeof(UIButtonCombo));
	ui_WidgetInitialize(UI_WIDGET(pButtonCombo), ui_ButtonComboTick, ui_ButtonComboDraw, ui_ButtonComboFreeInternal, NULL, NULL);
	ui_WidgetSetDimensions(UI_WIDGET(pButtonCombo), width, height);
	ui_WidgetSetPosition(UI_WIDGET(pButtonCombo), x, y);

	//Create Button
	pButtonCombo->pButton = ui_ButtonCreate(NULL, 0, 0, ui_ButtonComboMainButtonClickedCB, pButtonCombo);
	pButtonCombo->pButton->spriteInheritsColor = true;
	pButtonCombo->pButton->bChildrenOverlapBorder = true;
	ui_WidgetSetDimensionsEx(UI_WIDGET(pButtonCombo->pButton), width-button_width, 1, UIUnitFixed, UIUnitPercentage);
	ui_WidgetAddChild(UI_WIDGET(pButtonCombo), UI_WIDGET(pButtonCombo->pButton));

	if (use_combo_button)
	{
		//Create Button
		pButtonCombo->pComboButton = ui_ButtonCreateImageOnly(g_ui_Tex.arrowDropDown->name, width-button_width, 0, ui_ButtonComboButtonClickedCB, pButtonCombo);
		pButtonCombo->pComboButton->spriteInheritsColor = true;
		pButtonCombo->pComboButton->bChildrenOverlapBorder = true;
		ui_WidgetSetDimensionsEx(UI_WIDGET(pButtonCombo->pComboButton), button_width, 1, UIUnitFixed, UIUnitPercentage);
		ui_WidgetAddChild(UI_WIDGET(pButtonCombo), UI_WIDGET(pButtonCombo->pComboButton));
	}

	//Create 
	pButtonCombo->pPane = ui_PaneCreate(0, 0, 0, 0, UIUnitFixed, UIUnitFixed, 0);
	UI_WIDGET(pButtonCombo->pPane)->tickF = ui_ButtonComboPaneTick;
	UI_WIDGET(pButtonCombo->pPane)->drawF = ui_ButtonComboPaneDraw;
	pButtonCombo->pPane->invisible = true;

	//Init Vals
	pButtonCombo->selected_item = NULL;
	pButtonCombo->items = NULL;
	pButtonCombo->active = true;
	pButtonCombo->direction = direction;

	return pButtonCombo;
}

void ui_ButtonComboFreeInternal(SA_PRE_NN_VALID SA_POST_P_FREE UIButtonCombo *pButtonCombo)
{
	ui_WidgetFreeInternal(UI_WIDGET(pButtonCombo->pPane));
	ui_WidgetFreeInternal(UI_WIDGET(pButtonCombo));
}

void ui_ButtonComboTick(SA_PARAM_NN_VALID UIButtonCombo *pButtonCombo, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(pButtonCombo);

	if(!UI_WIDGET(pButtonCombo->pPane)->group && !pButtonCombo->pPane->invisible)
	{
		F32 sx=0, sy=0;
		switch(pButtonCombo->direction)
		{
		xcase POP_UP:
			sx = x - 4;
			sy = y - UI_WIDGET(pButtonCombo->pPane)->height - 2;
		xcase POP_DOWN:
			sx = x - 4;
			sy = y + h + 2;
		xcase POP_LEFT:
			sx = x - UI_WIDGET(pButtonCombo->pPane)->width - 2;
			sy = y - 4;
		xcase POP_RIGHT:
			sx = x + w + 2;
			sy = y - 4;
		}

		sx /= g_ui_State.scale;
		sy /= g_ui_State.scale;

		ui_WidgetSetPosition(UI_WIDGET(pButtonCombo->pPane), sx, sy);
		UI_WIDGET(pButtonCombo->pPane)->priority = UI_HIGHEST_PRIORITY;	
		ui_TopWidgetAddToDevice(UI_WIDGET(pButtonCombo->pPane), NULL);
	}
	else if(UI_WIDGET(pButtonCombo->pPane)->group && pButtonCombo->pPane->invisible)
	{
		if(mouseDownHit(MS_LEFT, &box))
			pButtonCombo->pPane->invisible = false;
		else
			ui_WidgetRemoveFromGroup(UI_WIDGET(pButtonCombo->pPane));	
	}

	UI_TICK_EARLY(pButtonCombo, true, true);
	UI_TICK_LATE(pButtonCombo);
}

void ui_ButtonComboDraw(SA_PARAM_NN_VALID UIButtonCombo *pButtonCombo, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(pButtonCombo);
	UI_DRAW_EARLY(pButtonCombo);
	if(!ui_IsActive(UI_WIDGET(pButtonCombo)))
		display_sprite(white_tex_atlas, x, y, z+2.5, w, h, 0x777777BB);
	if(eaSize(&pButtonCombo->items) > 1 && !pButtonCombo->pComboButton)
	{
		display_sprite(white_tex_atlas, x+w-w*0.275, y+h-h*0.175, z+2.5, w*0.2 / white_tex_atlas->width, h*0.05 / white_tex_atlas->height, 0x000000FF);
		if(!UI_WIDGET(pButtonCombo->pPane)->group)
			display_sprite(white_tex_atlas, x+w-w*0.20, y+h-h*0.25, z+2.5, w*0.05 / white_tex_atlas->width, h*0.2 / white_tex_atlas->height, 0x000000FF);
	}
	UI_DRAW_LATE(pButtonCombo);
}

void ui_ButtonComboSetChangedCallback(UIButtonCombo *pButtonCombo, UIActivationFunc cbChanged, UserData pChangedData)
{
	pButtonCombo->cbChanged = cbChanged;
	pButtonCombo->pChangedData = pChangedData;
}
