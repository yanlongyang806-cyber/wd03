#pragma once
GCC_SYSTEM

#include "UICore.h"

typedef struct UIButton UIButton;
typedef struct UIPane UIPane;
typedef struct UIAccordion UIAccordion;

typedef struct UIAccordionExpander
{
	UIAccordion *pParent;
	UIButton *pButton;
	UIPane *pPane;
	F32 fAnimatingHeight;
} UIAccordionExpander;

typedef struct UIAccordion
{
	UIWidget widget;

	int iButtonHeight;
	int iExpanderSelected;
	UIAccordionExpander **eaExpanders;

	UISkin *pButtonSkin;

	UIActivationFunc cbChanged;
	UserData pChangedData;

} UIAccordion;

SA_RET_NN_VALID UIAccordion *ui_AccordionCreate(F32 x, F32 y, F32 width, F32 height, int button_height);
void ui_AccordionFreeInternal(SA_PRE_NN_VALID SA_POST_P_FREE UIAccordion *pAccordion);
void ui_AccordionTick(SA_PARAM_NN_VALID UIAccordion *pAccordion, UI_PARENT_ARGS);

UIAccordionExpander *ui_AccordionCreateExpander(SA_PARAM_NN_VALID UIAccordion *pAccordion, const char *title);

int ui_AccordionGetSelected(SA_PARAM_NN_VALID UIAccordion *pAccordion);
void ui_AccordionSetSelected(SA_PARAM_NN_VALID UIAccordion *pAccordion, int expander);

void ui_AccordionSetChangedCallback(SA_PARAM_NN_VALID UIAccordion *pAccordion, UIActivationFunc cbChanged, UserData pChangedData);