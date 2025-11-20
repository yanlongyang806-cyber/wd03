
#include "GfxClipper.h"
#include "inputMouse.h"
#include "UISliderTextEntry.h"
#include "UITextEntry.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););


static void UpdateSliderAndCallback(UITextEntry *pTextEntry, UISliderTextEntry *pEntry)
{
	const char *pcText = ui_TextEntryGetText(pTextEntry);
	F64 fValue = (pEntry->isPercentage ? atof(pcText)/100.0f : atof(pcText));

	if (pEntry->bNoSnapFromText) {
		F64 fNewValue = CLAMP(fValue, pEntry->pSlider->min, pEntry->pSlider->max);
		F64 fStep = 0.0f;

		// temporarily disable the step value on the slider when setting from the text entry widget
		if (pEntry->pSlider) {
			fStep = pEntry->pSlider->step;
			pEntry->pSlider->step = 0.0f;
		}
		ui_SliderSetValue(pEntry->pSlider, fNewValue);
		if (pEntry->pSlider) {
			pEntry->pSlider->step = fStep;
		}

		// manually post back the text entry value if it differs from the one set to the slider
		if (fValue != fNewValue) {
			char buf[128];
			if (pEntry->isPercentage) {
				sprintf(buf, "%g%%", fNewValue*100.0f);
			}
			else {
				sprintf(buf, "%g", fNewValue);
			}
			ui_TextEntrySetText(pTextEntry, buf);
		}
	}
	else
		ui_SliderSetValueAndCallbackEx(pEntry->pSlider, 0, fValue, true, pEntry->isOutOfRangeAllowed);

	if (pEntry->cbChanged)
		pEntry->cbChanged(pEntry, true, pEntry->pChangedData);
}


static void UpdateTextEntryAndCallback(UISlider *pSlider, bool bFinished, UISliderTextEntry *pEntry)
{
	F64 fValue = ui_SliderGetValue(pSlider);
	char buf[128];

	if(pEntry->isPercentage)
		sprintf(buf, "%g%%", fValue*100.0f);
	else
		sprintf(buf, "%g", fValue);

	ui_TextEntrySetText(pEntry->pEntry, buf);
	if (pEntry->cbChanged)
		pEntry->cbChanged(pEntry, bFinished, pEntry->pChangedData);
}


static void ui_SliderTextEntryContextProxy(UITextEntry *pText, UISliderTextEntry *pEntry)
{
	if (pEntry->widget.contextF)
		pEntry->widget.contextF(pEntry, pEntry->widget.contextData);	
}

UISliderTextEntry *ui_SliderTextEntryCreate(const char *pcText, F64 min, F64 max, F32 x, F32 y, F32 width)
{
	UISliderTextEntry *pEntry = calloc(1, sizeof(UISliderTextEntry));
	ui_WidgetInitialize(UI_WIDGET(pEntry), ui_SliderTextEntryTick, ui_SliderTextEntryDraw, ui_SliderTextEntryFreeInternal, NULL, NULL);
	pEntry->pEntry = ui_TextEntryCreate("", 0, 0);
	pEntry->pEntry->forceLeftAligned = true;
	pEntry->pSlider = ui_SliderCreate(x, y, 1, min, max, 0);
	ui_SliderTextEntrySetTextAndCallback(pEntry, pcText);
	ui_WidgetSetPosition(UI_WIDGET(pEntry), x, y);

	ui_TextEntrySetFinishedCallback(pEntry->pEntry, UpdateSliderAndCallback, pEntry);
	ui_WidgetSetContextCallback(UI_WIDGET(pEntry->pEntry), ui_SliderTextEntryContextProxy, pEntry);
	ui_SliderSetChangedCallback(pEntry->pSlider, UpdateTextEntryAndCallback, pEntry);

	ui_WidgetSetPositionEx(UI_WIDGET(pEntry->pSlider), 0, 0, 0, 0, UITopLeft);
	ui_WidgetSetPositionEx(UI_WIDGET(pEntry->pEntry), 0, 0, 0, 0, UITopRight);
	ui_WidgetSetDimensions(UI_WIDGET(pEntry), width, 20);
	ui_WidgetSetHeightEx(UI_WIDGET(pEntry->pEntry), 1.f, UIUnitPercentage);
	ui_WidgetSetHeightEx(UI_WIDGET(pEntry->pSlider), 1.f, UIUnitPercentage);
	ui_WidgetSetWidthEx(UI_WIDGET(pEntry->pSlider),0.65, UIUnitPercentage);
	ui_WidgetSetWidthEx(UI_WIDGET(pEntry->pEntry), 0.35, UIUnitPercentage);
	ui_WidgetAddChild(UI_WIDGET(pEntry), UI_WIDGET(pEntry->pEntry));
	ui_WidgetAddChild(UI_WIDGET(pEntry), UI_WIDGET(pEntry->pSlider));

	pEntry->isPercentage = false;
	pEntry->bNoSnapFromText = false;
	return pEntry;
}

UISliderTextEntry *ui_SliderTextEntryCreateWithNoSnap(const char *pcText, F64 min, F64 max, F32 x, F32 y, F32 width)
{
	UISliderTextEntry* pEntry = ui_SliderTextEntryCreate(pcText, min, max, x, y, width);
	if (pEntry) {
		pEntry->bNoSnapFromText = true;
	}
	return pEntry;
}

void ui_SliderTextEntryFreeInternal(UISliderTextEntry *pEntry)
{
	ui_WidgetFreeInternal(UI_WIDGET(pEntry));
}

void ui_SliderTextEntryTick(UISliderTextEntry *pEntry, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(pEntry);
	UI_TICK_EARLY(pEntry, true, true);
	UI_TICK_LATE(pEntry);
}

void ui_SliderTextEntryDraw(UISliderTextEntry *pEntry, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(pEntry);
	UI_DRAW_EARLY(pEntry);

	// Make sure changed/inherited flags move into text entry
	ui_SetChanged(UI_WIDGET(pEntry->pEntry), ui_IsChanged(UI_WIDGET(pEntry)));
	ui_SetInherited(UI_WIDGET(pEntry->pEntry), ui_IsInherited(UI_WIDGET(pEntry)));

	UI_DRAW_LATE(pEntry);
}

void ui_SliderTextEntrySetRange(SA_PARAM_NN_VALID UISliderTextEntry *pEntry, F64 min, F64 max, F64 step)
{
	ui_SliderSetRange(pEntry->pSlider, min, max, step); 
}

void ui_SliderTextEntrySetPolicy(SA_PARAM_NN_VALID UISliderTextEntry *pEntry, UISliderPolicy policy)
{
	ui_SliderSetPolicy(pEntry->pSlider, policy); 
}

void ui_SliderTextEntrySetAsPercentage(SA_PARAM_NN_VALID UISliderTextEntry *pEntry, bool isPercentage)
{
	pEntry->isPercentage = isPercentage;
}

void ui_SliderTextEntrySetIsOutOfRangeAllowed(SA_PARAM_NN_VALID UISliderTextEntry *pEntry, bool isOutOfRangeAllowed)
{
	pEntry->isOutOfRangeAllowed = isOutOfRangeAllowed;
}

void ui_SliderTextEntrySetSelectOnFocus(SA_PARAM_NN_VALID UISliderTextEntry *pEntry, bool select)
{
	ui_TextEntrySetSelectOnFocus(pEntry->pEntry, select);
}

F32 ui_SliderTextEntryGetValue(SA_PARAM_NN_VALID UISliderTextEntry *pEntry)
{
	return ui_SliderGetValue(pEntry->pSlider);
}

const char *ui_SliderTextEntryGetText(UISliderTextEntry *pEntry)
{
	return ui_TextEntryGetText(pEntry->pEntry);
}

void ui_SliderTextEntrySetText(UISliderTextEntry *pEntry, const char *pcText)
{
	char buf[128];
	F64 fValue = atof(pcText);
	ui_SliderSetValue(pEntry->pSlider, fValue);
	fValue = ui_SliderGetValue(pEntry->pSlider);
	if(pEntry->isPercentage)
		sprintf(buf, "%g%%", fValue*100.0f);
	else
		sprintf(buf, "%g", fValue);
	ui_TextEntrySetText(pEntry->pEntry, buf);
}

void ui_SliderTextEntrySetTextAndCallback(UISliderTextEntry *pEntry, const char *pcText)
{
	char buf[128];
	F64 fValue = atof(pcText);
	ui_SliderSetValue(pEntry->pSlider, fValue);
	fValue = ui_SliderGetValue(pEntry->pSlider);
	if(pEntry->isPercentage)
		sprintf(buf, "%g%%", fValue*100.0f);
	else
		sprintf(buf, "%g", fValue);
	ui_TextEntrySetText(pEntry->pEntry, buf);

	if (pEntry->cbChanged)
		pEntry->cbChanged(pEntry, true, pEntry->pChangedData);
}

void ui_SliderTextEntrySetValue(UISliderTextEntry *pEntry, F64 fValue)
{
	char buf[128];
	ui_SliderSetValueAndCallbackEx(pEntry->pSlider, 0, fValue, false, pEntry->isOutOfRangeAllowed);
	fValue = ui_SliderGetValue(pEntry->pSlider);
	if(pEntry->isPercentage)
		sprintf(buf, "%g%%", fValue*100.0f);
	else
		sprintf(buf, "%g", fValue);
	ui_TextEntrySetText(pEntry->pEntry, buf);
}

void ui_SliderTextEntrySetValueAndCallback(UISliderTextEntry *pEntry, F64 fValue)
{
	char buf[128];
	ui_SliderSetValue(pEntry->pSlider, fValue);
	fValue = ui_SliderGetValue(pEntry->pSlider);
	if(pEntry->isPercentage)
		sprintf(buf, "%g%%", fValue*100.0f);
	else
		sprintf(buf, "%g", fValue);
	ui_TextEntrySetText(pEntry->pEntry, buf);
	
	if (pEntry->cbChanged)
		pEntry->cbChanged(pEntry, true, pEntry->pChangedData);
}

void ui_SliderTextEntrySetChangedCallback(UISliderTextEntry *pEntry, UISliderChangeFunc cbChanged, UserData pChangedData)
{
	pEntry->cbChanged = cbChanged;
	pEntry->pChangedData = pChangedData;
}

