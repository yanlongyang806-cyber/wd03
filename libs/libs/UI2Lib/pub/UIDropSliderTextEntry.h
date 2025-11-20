#ifndef UI_DROP_SLIDER_TEXT_ENTRY_H
#define UI_DROP_SLIDER_TEXT_ENTRY_H
GCC_SYSTEM

#include "UICore.h"
#include "UISlider.h"

typedef struct UITextEntry UITextEntry;
typedef struct UIButton UIButton;
typedef struct UISlider UISlider;
typedef struct UIPane UIPane;

typedef struct UIDropSliderTextEntry
{
	UIWidget widget;

	UITextEntry *pEntry;
	UIButton *pButton;
	UISlider *pSlider;
	UIPane *pPane;

	bool isPercentage : 1;

	UIActivationFunc cbChanged;
	UserData pChangedData;

} UIDropSliderTextEntry;

SA_RET_NN_VALID UIDropSliderTextEntry *ui_DropSliderTextEntryCreate(const char *pcText,  F64 min, F64 max, F64 step, F32 x, F32 y, F32 width, F32 height, F32 drop_width, F32 drop_height);
void ui_DropSliderTextEntryFreeInternal(SA_PRE_NN_VALID SA_POST_P_FREE UIDropSliderTextEntry *pEntry);
void ui_DropSliderTextEntryTick(SA_PARAM_NN_VALID UIDropSliderTextEntry *pEntry, UI_PARENT_ARGS);
void ui_DropSliderTextEntryDraw(SA_PARAM_NN_VALID UIDropSliderTextEntry *pEntry, UI_PARENT_ARGS);

void ui_DropSliderTextEntrySetRange(SA_PARAM_NN_VALID UIDropSliderTextEntry *pEntry, F64 min, F64 max, F64 step);
void ui_DropSliderTextEntrySetPolicy(SA_PARAM_NN_VALID UIDropSliderTextEntry *pEntry, UISliderPolicy policy);
void ui_DropSliderTextEntrySetAsPercentage(SA_PARAM_NN_VALID UIDropSliderTextEntry *pEntry, bool isPercentage);

F32 ui_DropSliderTextEntryGetValue(SA_PARAM_NN_VALID UIDropSliderTextEntry *pEntry);
const char *ui_DropSliderTextEntryGetText(SA_PARAM_NN_VALID UIDropSliderTextEntry *pEntry);
void ui_DropSliderTextEntrySetText(SA_PARAM_NN_VALID UIDropSliderTextEntry *pEntry, SA_PARAM_NN_STR const char *pcText);
void ui_DropSliderTextEntrySetTextAndCallback(SA_PARAM_NN_VALID UIDropSliderTextEntry *pEntry, SA_PARAM_NN_STR const char *pcText);
void ui_DropSliderTextEntrySetValue(SA_PARAM_NN_VALID UIDropSliderTextEntry *pEntry, F64 fValue);
void ui_DropSliderTextEntrySetValueAndCallback(SA_PARAM_NN_VALID UIDropSliderTextEntry *pEntry, F64 fValue);

void ui_DropSliderTextEntrySetChangedCallback(SA_PARAM_NN_VALID UIDropSliderTextEntry *pEntry, UIActivationFunc cbChanged, UserData pChangedData);

#endif
