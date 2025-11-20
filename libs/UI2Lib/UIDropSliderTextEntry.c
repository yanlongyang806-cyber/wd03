
#include "GfxClipper.h"
//#include "GfxTexAtlas.h"
#include "inputMouse.h"
#include "UIDropSliderTextEntry.h"
#include "UISprite.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

static F32 ui_DropSliderRemoveBias(UISlider *slider, F32 val_in, F32 min, F32 diff)
{
	F32 val_out = CLAMP(val_in - min, 0, diff) / diff;
	if(slider->bias != 0.0f)
	{
		if(val_out < slider->bias_offset)
		{
			val_out = (slider->bias_offset - val_out) / (slider->bias_offset);
			val_out = pow(val_out, 1.0f/slider->bias);
			val_out = slider->bias_offset - val_out * (slider->bias_offset);
		}
		else
		{
			val_out = (val_out - slider->bias_offset) / (1.0f - slider->bias_offset);
			val_out = pow(val_out, 1.0f/slider->bias);
			val_out = slider->bias_offset + val_out * (1.0f - slider->bias_offset);
		}
	}
	return val_out;
}

static void UpdateDropSliderAndCallback(UITextEntry *pTextEntry, UIDropSliderTextEntry *pEntry)
{
	const char *pcText = ui_TextEntryGetText(pTextEntry);
	F64 fValue = atof(pcText);

	if(pEntry->isPercentage)
		ui_SliderSetValueAndCallback(pEntry->pSlider, fValue/100.0f);
	else
		ui_SliderSetValueAndCallback(pEntry->pSlider, fValue);

	if (pEntry->cbChanged)
		pEntry->cbChanged(pEntry, pEntry->pChangedData);
}


static void UpdateDropSliderTextEntryAndCallback(UISlider *pSlider, bool bFinished, UIDropSliderTextEntry *pEntry)
{
	F64 fValue = ui_SliderGetValue(pSlider);
	char buf[128];

	if(pEntry->isPercentage)
		sprintf(buf, "%g%%", fValue*100.0f);
	else
		sprintf(buf, "%g", fValue);

	ui_TextEntrySetText(pEntry->pEntry, buf);
	if (pEntry->cbChanged)
		pEntry->cbChanged(pEntry, pEntry->pChangedData);
}


static void ui_DropSliderTextEntryContextProxy(UITextEntry *pText, UIDropSliderTextEntry *pEntry)
{
	if (pEntry->widget.contextF)
		pEntry->widget.contextF(pEntry, pEntry->widget.contextData);	
}

static void ui_DropSliderPopupWindowCB(UIButton *button, UIDropSliderTextEntry *pEntry)
{
	if(UI_WIDGET(pEntry->pPane)->group && !pEntry->pPane->invisible)
		pEntry->pPane->invisible = true;
	else if(!UI_WIDGET(pEntry->pPane)->group && pEntry->pPane->invisible)
		pEntry->pPane->invisible = false;
}

void ui_DropSliderPaneDraw(UIPane *pane, UI_PARENT_ARGS)
{
	bool old_state = pane->invisible;
	pane->invisible = false;
	ui_PaneDraw(pane, UI_PARENT_VALUES);
	pane->invisible = old_state;
}

void ui_DropSliderPaneTick(UIPane *pane, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(pane);

	if(mouseDown(MS_LEFT) && !mouseDownHit(MS_LEFT, &box))
	{
		pane->invisible = true;
		return;
	}
	ui_PaneTick(pane, UI_PARENT_VALUES);
}

UIDropSliderTextEntry *ui_DropSliderTextEntryCreate(const char *pcText, F64 min, F64 max, F64 step, F32 x, F32 y, F32 width, F32 height, F32 drop_width, F32 drop_height)
{
	//Create DropSliderTextEntry
	UIDropSliderTextEntry *pEntry = calloc(1, sizeof(UIDropSliderTextEntry));
	ui_WidgetInitialize(UI_WIDGET(pEntry), ui_DropSliderTextEntryTick, ui_DropSliderTextEntryDraw, ui_DropSliderTextEntryFreeInternal, NULL, NULL);
	ui_WidgetSetDimensions(UI_WIDGET(pEntry), width, height);
	ui_WidgetSetPosition(UI_WIDGET(pEntry), x, y);

	//Create Button and Text
	pEntry->pEntry = ui_TextEntryCreate(pcText, 0, 0);
	pEntry->pEntry->forceLeftAligned = true;
	pEntry->pButton = ui_ButtonCreateImageOnly("eui_arrow_large_down", 0, 0, ui_DropSliderPopupWindowCB, pEntry);
	ui_WidgetSetPositionEx(UI_WIDGET(pEntry->pEntry), 0, 0, 0, 0, UITopLeft);
	ui_WidgetSetPositionEx(UI_WIDGET(pEntry->pButton), 0, 0, 0, 0, UITopRight);
	ui_WidgetSetDimensions(UI_WIDGET(pEntry->pEntry), width-15, height);
	ui_WidgetSetDimensions(UI_WIDGET(pEntry->pButton), 15, height);
	ui_ButtonSetImageStretch(pEntry->pButton, true);
	ui_WidgetAddChild(UI_WIDGET(pEntry), UI_WIDGET(pEntry->pEntry));
	ui_WidgetAddChild(UI_WIDGET(pEntry), UI_WIDGET(pEntry->pButton));
	UI_WIDGET(pEntry->pButton)->focusF = NULL;

	//Create Container for Slider
	pEntry->pPane = ui_PaneCreate(x, y+height, drop_width, drop_height, UIUnitFixed, UIUnitFixed, 0);
	UI_WIDGET(pEntry->pPane)->tickF = ui_DropSliderPaneTick;
	UI_WIDGET(pEntry->pPane)->drawF = ui_DropSliderPaneDraw;
	pEntry->pPane->invisible = true;

	//Create Slider
	pEntry->pSlider = ui_SliderCreate(0, 0, 1, min, max, 0);
	ui_SliderSetPolicy(pEntry->pSlider, UISliderContinuous);
	ui_WidgetSetHeightEx(UI_WIDGET(pEntry->pSlider), 1.f, UIUnitPercentage);
	ui_WidgetSetWidthEx(UI_WIDGET(pEntry->pSlider), 1.f, UIUnitPercentage);
	ui_SliderSetRange(pEntry->pSlider, min, max, step);
	ui_WidgetAddChild(UI_WIDGET(pEntry->pPane), UI_WIDGET(pEntry->pSlider));

	//Setup Callbacks
	ui_TextEntrySetFinishedCallback(pEntry->pEntry, UpdateDropSliderAndCallback, pEntry);
	ui_WidgetSetContextCallback(UI_WIDGET(pEntry->pEntry), ui_DropSliderTextEntryContextProxy, pEntry);
	ui_SliderSetChangedCallback(pEntry->pSlider, UpdateDropSliderTextEntryAndCallback, pEntry);

	pEntry->isPercentage = false;
	return pEntry;
}

void ui_DropSliderTextEntrySetAsPercentage(SA_PARAM_NN_VALID UIDropSliderTextEntry *pEntry, bool isPercentage)
{
	pEntry->isPercentage = isPercentage;
}

void ui_DropSliderTextEntryFreeInternal(UIDropSliderTextEntry *pEntry)
{
	ui_WidgetFreeInternal(UI_WIDGET(pEntry));
}

void ui_DropSliderTextEntryTick(UIDropSliderTextEntry *pEntry, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(pEntry);

	if(!UI_WIDGET(pEntry->pPane)->group && !pEntry->pPane->invisible)
	{
		F32 pane_width = UI_WIDGET(pEntry->pPane)->width;
		F32 button_width = UI_WIDGET(pEntry->pButton)->width;
		F32 smin = pEntry->pSlider->min;
		F32 smax = pEntry->pSlider->max;
		F32 sval = ui_SliderGetValue(pEntry->pSlider);
		F32 sx = (x+w)/g_ui_State.scale - button_width - (pane_width-button_width)*ui_DropSliderRemoveBias(pEntry->pSlider, sval, smin, smax-smin);
		F32 sy = (y+h)/g_ui_State.scale;

		ui_WidgetSetPosition(UI_WIDGET(pEntry->pPane), sx, sy);
		UI_WIDGET(pEntry->pPane)->priority = UI_HIGHEST_PRIORITY;	
		ui_TopWidgetAddToDevice(UI_WIDGET(pEntry->pPane), NULL);
	}
	else if(UI_WIDGET(pEntry->pPane)->group && pEntry->pPane->invisible)
	{
		if(mouseDownHit(MS_LEFT, &box))
			pEntry->pPane->invisible = false;
		else
			ui_WidgetRemoveFromGroup(UI_WIDGET(pEntry->pPane));	
	}

	UI_TICK_EARLY(pEntry, true, true);
	UI_TICK_LATE(pEntry);
}

void ui_DropSliderTextEntryDraw(UIDropSliderTextEntry *pEntry, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(pEntry);
	UI_DRAW_EARLY(pEntry);

	// Make sure changed/inherited flags move into text entry
	ui_SetChanged(UI_WIDGET(pEntry->pEntry), ui_IsChanged(UI_WIDGET(pEntry)));
	ui_SetInherited(UI_WIDGET(pEntry->pEntry), ui_IsInherited(UI_WIDGET(pEntry)));

	UI_DRAW_LATE(pEntry);
}

void ui_DropSliderTextEntrySetRange(SA_PARAM_NN_VALID UIDropSliderTextEntry *pEntry, F64 min, F64 max, F64 step)
{
	ui_SliderSetRange(pEntry->pSlider, min, max, step); 
}

void ui_DropSliderTextEntrySetPolicy(SA_PARAM_NN_VALID UIDropSliderTextEntry *pEntry, UISliderPolicy policy)
{
	ui_SliderSetPolicy(pEntry->pSlider, policy); 
}

F32 ui_DropSliderTextEntryGetValue(SA_PARAM_NN_VALID UIDropSliderTextEntry *pEntry)
{
	return ui_SliderGetValue(pEntry->pSlider);
}

const char *ui_DropSliderTextEntryGetText(UIDropSliderTextEntry *pEntry)
{
	return ui_TextEntryGetText(pEntry->pEntry);
}

void ui_DropSliderTextEntrySetText(UIDropSliderTextEntry *pEntry, const char *pcText)
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

void ui_DropSliderTextEntrySetTextAndCallback(UIDropSliderTextEntry *pEntry, const char *pcText)
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
		pEntry->cbChanged(pEntry, pEntry->pChangedData);
}

void ui_DropSliderTextEntrySetValue(UIDropSliderTextEntry *pEntry, F64 fValue)
{
	char buf[128];
	ui_SliderSetValue(pEntry->pSlider, fValue);
	fValue = ui_SliderGetValue(pEntry->pSlider);
	if(pEntry->isPercentage)
		sprintf(buf, "%g%%", fValue*100.0f);
	else
		sprintf(buf, "%g", fValue);
	ui_TextEntrySetText(pEntry->pEntry, buf);
}

void ui_DropSliderTextEntrySetValueAndCallback(UIDropSliderTextEntry *pEntry, F64 fValue)
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
		pEntry->cbChanged(pEntry, pEntry->pChangedData);
}

void ui_DropSliderTextEntrySetChangedCallback(UIDropSliderTextEntry *pEntry, UIActivationFunc cbChanged, UserData pChangedData)
{
	pEntry->cbChanged = cbChanged;
	pEntry->pChangedData = pChangedData;
}

