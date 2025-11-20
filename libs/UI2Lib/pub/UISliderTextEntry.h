#ifndef UI_SLIDER_TEXT_ENTRY_H
#define UI_SLIDER_TEXT_ENTRY_H
GCC_SYSTEM

#include "UICore.h"
#include "UISlider.h"

typedef struct UITextEntry UITextEntry;
typedef struct UIWindow UIWindow;

typedef struct UISliderTextEntry
{
	UIWidget widget;

	UITextEntry *pEntry;
	UISlider *pSlider;

	bool isPercentage : 1;
	bool isOutOfRangeAllowed : 1;
	bool bNoSnapFromText : 1;

	UISliderChangeFunc cbChanged;
	UserData pChangedData;

} UISliderTextEntry;

SA_RET_NN_VALID UISliderTextEntry *ui_SliderTextEntryCreate(const char *pcText,  F64 min, F64 max, F32 x, F32 y, F32 width);
SA_RET_NN_VALID UISliderTextEntry *ui_SliderTextEntryCreateWithNoSnap(const char *pcText,  F64 min, F64 max, F32 x, F32 y, F32 width);
void ui_SliderTextEntryFreeInternal(SA_PRE_NN_VALID SA_POST_P_FREE UISliderTextEntry *pEntry);
void ui_SliderTextEntryTick(SA_PARAM_NN_VALID UISliderTextEntry *pEntry, UI_PARENT_ARGS);
void ui_SliderTextEntryDraw(SA_PARAM_NN_VALID UISliderTextEntry *pEntry, UI_PARENT_ARGS);

void ui_SliderTextEntrySetRange(SA_PARAM_NN_VALID UISliderTextEntry *pEntry, F64 min, F64 max, F64 step);
void ui_SliderTextEntrySetPolicy(SA_PARAM_NN_VALID UISliderTextEntry *pEntry, UISliderPolicy policy);
void ui_SliderTextEntrySetAsPercentage(SA_PARAM_NN_VALID UISliderTextEntry *pEntry, bool isPercentage);
void ui_SliderTextEntrySetIsOutOfRangeAllowed(SA_PARAM_NN_VALID UISliderTextEntry *pEntry, bool isOutOfRangeAllowed);
void ui_SliderTextEntrySetSelectOnFocus(SA_PARAM_NN_VALID UISliderTextEntry *pEntry, bool select);

F32 ui_SliderTextEntryGetValue(SA_PARAM_NN_VALID UISliderTextEntry *pEntry);
const char *ui_SliderTextEntryGetText(SA_PARAM_NN_VALID UISliderTextEntry *pEntry);
void ui_SliderTextEntrySetText(SA_PARAM_NN_VALID UISliderTextEntry *pEntry, SA_PARAM_NN_STR const char *pcText);
void ui_SliderTextEntrySetTextAndCallback(SA_PARAM_NN_VALID UISliderTextEntry *pEntry, SA_PARAM_NN_STR const char *pcText);
void ui_SliderTextEntrySetValue(SA_PARAM_NN_VALID UISliderTextEntry *pEntry, F64 fValue);
void ui_SliderTextEntrySetValueAndCallback(SA_PARAM_NN_VALID UISliderTextEntry *pEntry, F64 fValue);

void ui_SliderTextEntrySetChangedCallback(SA_PARAM_NN_VALID UISliderTextEntry *pEntry, UISliderChangeFunc cbChanged, UserData pChangedData);

#endif
