#include "UIAccordion.h"
#include "math.h"
#include "CBox.h"
#include "EArray.h"
#include "UIButton.h"
#include "UIPane.h"
#include "UISkin.h"
#include "GfxClipper.h"
#include "inputMouse.h"
#include "textparser.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

void ui_AccordionFreeInternal(UIAccordion *pAccordion)
{
	ui_WidgetFreeInternal(UI_WIDGET(pAccordion));
}

void ui_AccordionTick(UIAccordion *pAccordion, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(pAccordion);
	int index, index2;
	F32 cur_y = 0;
	UISkin *my_skin = UI_GET_SKIN(pAccordion);

	// copy parent values into skin
	StructCopy(parse_UISkin, my_skin, pAccordion->pButtonSkin, 0, 0, 0);
	if (my_skin->astrAccordionButtonStyle) {
		pAccordion->pButtonSkin->astrButtonStyle = my_skin->astrAccordionButtonStyle;
	}

	if (my_skin->astrAccordionButtonStyleHighlight) {
		pAccordion->pButtonSkin->astrButtonStyleHighlight = my_skin->astrAccordionButtonStyleHighlight;
	}

	if (my_skin->astrAccordionButtonStyleFocused) {
		pAccordion->pButtonSkin->astrButtonStyleFocused = my_skin->astrAccordionButtonStyleFocused;
	}

	if (my_skin->astrAccordionButtonStylePressed) {
		pAccordion->pButtonSkin->astrButtonStylePressed = my_skin->astrAccordionButtonStylePressed;
	}

	if (my_skin->astrAccordionButtonStyleDisabled) {
		pAccordion->pButtonSkin->astrButtonStyleDisabled = my_skin->astrAccordionButtonStyleDisabled;
	}

	if (IS_HANDLE_ACTIVE(my_skin->hAccordionButtonFont)) {
		COPY_HANDLE(pAccordion->pButtonSkin->hButtonFont,my_skin->hAccordionButtonFont);
	}

	if (IS_HANDLE_ACTIVE(my_skin->hAccordionButtonFontHighlight)) {
		COPY_HANDLE(pAccordion->pButtonSkin->hButtonFontHighlight,my_skin->hAccordionButtonFontHighlight);
	}

	if (IS_HANDLE_ACTIVE(my_skin->hAccordionButtonFontPressed)) {
		COPY_HANDLE(pAccordion->pButtonSkin->hButtonFontPressed,my_skin->hAccordionButtonFontPressed);
	}

	if (IS_HANDLE_ACTIVE(my_skin->hAccordionButtonFontDisabled)) {
		COPY_HANDLE(pAccordion->pButtonSkin->hButtonFontDisabled,my_skin->hAccordionButtonFontDisabled);
	}

	for (index = 0; index < eaSize(&pAccordion->eaExpanders); index++)
	{
		UIAccordionExpander *expander = pAccordion->eaExpanders[index];
		if (pAccordion->iExpanderSelected != index)
		{
			expander->fAnimatingHeight *= 0.8f;
			if (expander->fAnimatingHeight < 1)
			{
				expander->fAnimatingHeight = 0;
				if (UI_WIDGET(expander->pPane)->group)
					ui_WidgetRemoveFromGroup(UI_WIDGET(expander->pPane));
			}
		}
	}
	for (index = 0; index < eaSize(&pAccordion->eaExpanders); index++)
	{
		UIAccordionExpander *expander = pAccordion->eaExpanders[index];
		ui_WidgetSetPosition(UI_WIDGET(expander->pButton), 0, cur_y);
		ui_WidgetSetDimensions(UI_WIDGET(expander->pButton), w, pAccordion->iButtonHeight);
		cur_y += pAccordion->iButtonHeight;
		if (pAccordion->iExpanderSelected == index)
		{
			F32 pane_y = cur_y;
			cur_y = h;
			for (index2 = index+1; index2 < eaSize(&pAccordion->eaExpanders); index2++)
				cur_y -= pAccordion->iButtonHeight + pAccordion->eaExpanders[index2]->fAnimatingHeight;
			ui_WidgetSetPosition(UI_WIDGET(expander->pPane), 0, pane_y);
			expander->fAnimatingHeight = cur_y-pane_y;
			ui_WidgetSetDimensions(UI_WIDGET(expander->pPane), w, cur_y-pane_y);
		}
		else
		{
			if (expander->fAnimatingHeight > 0 && UI_WIDGET(expander->pPane)->group)
			{
				ui_WidgetSetPosition(UI_WIDGET(expander->pPane), 0, cur_y);
				ui_WidgetSetDimensions(UI_WIDGET(expander->pPane), w, expander->fAnimatingHeight);
			}
			cur_y += expander->fAnimatingHeight;
		}
	}

	UI_TICK_EARLY(pAccordion, true, false);
	UI_TICK_LATE(pAccordion);
}

void ui_AccordionDraw(UIAccordion *pAccordion, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(pAccordion);
	UI_DRAW_EARLY(pAccordion);
	UI_DRAW_LATE(pAccordion);
}

UIAccordion *ui_AccordionCreate(F32 x, F32 y, F32 width, F32 height, int button_height)
{
	UIAccordion *pAccordion = calloc(1, sizeof(UIAccordion));
	ui_WidgetInitialize(UI_WIDGET(pAccordion), ui_AccordionTick, ui_AccordionDraw, ui_AccordionFreeInternal, NULL, NULL);
	ui_WidgetSetDimensions(UI_WIDGET(pAccordion), width, height);
	ui_WidgetSetPosition(UI_WIDGET(pAccordion), x, y);
	pAccordion->iButtonHeight = button_height;
	pAccordion->iExpanderSelected = -1;

	pAccordion->pButtonSkin = StructCreate(parse_UISkin);

	return pAccordion;
}

static void ui_AccordionSetSelectedCB(UIButton *button, UIAccordionExpander *pExpander)
{
	int idx = eaFind(&pExpander->pParent->eaExpanders, pExpander);
	if (idx >= 0)
		ui_AccordionSetSelected(pExpander->pParent, idx);
}

UIAccordionExpander *ui_AccordionCreateExpander(SA_PARAM_NN_VALID UIAccordion *pAccordion, const char *title)
{
	UIAccordionExpander *pExpander = calloc(1, sizeof(UIAccordionExpander));
	pExpander->pParent = pAccordion;
	pExpander->pButton = ui_ButtonCreate(title, 0, 0, ui_AccordionSetSelectedCB, pExpander);
	ui_WidgetSkin(UI_WIDGET(pExpander->pButton), pAccordion->pButtonSkin);
	pExpander->pPane = ui_PaneCreate(0, 0, 100, 100, UIUnitFixed, UIUnitFixed, 0);
	eaPush(&pAccordion->eaExpanders, pExpander);
	ui_WidgetAddChild(UI_WIDGET(pAccordion), UI_WIDGET(pExpander->pButton));
	if (pAccordion->iExpanderSelected == -1)
		ui_AccordionSetSelected(pAccordion, eaSize(&pAccordion->eaExpanders)-1);
	return pExpander;
}

int ui_AccordionGetSelected(UIAccordion *pAccordion)
{
	return pAccordion->iExpanderSelected;
}

void ui_AccordionSetSelected(UIAccordion *pAccordion, int expander)
{
	if (expander != pAccordion->iExpanderSelected)
	{
		UIAccordionExpander *pExpander = pAccordion->eaExpanders[expander];
		pAccordion->iExpanderSelected = expander;
		if (expander != -1 && UI_WIDGET(pExpander->pPane)->group == NULL) {
			ui_WidgetAddChild(UI_WIDGET(pAccordion), UI_WIDGET(pExpander->pPane));
			ui_WidgetSetDimensions( UI_WIDGET(pExpander->pPane), 0, 0 );
		}
		if (pAccordion->cbChanged)
			pAccordion->cbChanged(pAccordion, pAccordion->pChangedData);
	}
}

void ui_AccordionSetChangedCallback(UIAccordion *pAccordion, UIActivationFunc cbChanged, UserData pChangedData)
{
	pAccordion->cbChanged = cbChanged;
	pAccordion->pChangedData = pChangedData;
}
