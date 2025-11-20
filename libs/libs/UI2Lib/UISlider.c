/***************************************************************************



***************************************************************************/


#include "earray.h"

#include "GfxClipper.h"
#include "GfxPrimitive.h"
#include "GfxSprite.h"
#include "GfxTexAtlas.h"

#include "inputMouse.h"
#include "inputText.h"

#include "UISlider.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

static F32 ui_SliderBias(UISlider *slider, F32 val_in, F32 min, F32 diff)
{
	F32 val_out = val_in;
	if(slider->bias != 0.0f)
	{
		if(val_out < slider->bias_offset)
		{
			val_out = (slider->bias_offset - val_out) / (slider->bias_offset);
			val_out = pow(val_out, slider->bias);
			val_out = slider->bias_offset - val_out * (slider->bias_offset);
		}
		else if(val_out > slider->bias_offset)
		{
			val_out = (val_out - slider->bias_offset) / (1.0f - slider->bias_offset);
			val_out = pow(val_out, slider->bias);
			val_out = slider->bias_offset + val_out * (1.0f - slider->bias_offset);
		}
	}
	return val_out * diff + min;
}

//if you change this function, change ui_DropSliderRemoveBias as well.
static F32 ui_SliderRemoveBias(UISlider *slider, F32 val_in, F32 min, F32 diff)
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
		else if(val_out > slider->bias_offset)
		{
			val_out = (val_out - slider->bias_offset) / (1.0f - slider->bias_offset);
			val_out = pow(val_out, 1.0f/slider->bias);
			val_out = slider->bias_offset + val_out * (1.0f - slider->bias_offset);
		}
	}
	return val_out;
}

static void ui_SliderEnforceSeparation(UISlider *slider, int idx)
{
	int i;
	F64 separation = slider->minSeparation;
	F64 lastFloatVal;

	lastFloatVal = slider->currentVals[idx];
	for (i = idx-1; i >= 0; --i)
	{
		F64 floatVal = slider->currentVals[i];
		if (floatVal > lastFloatVal - separation)
		{
			floatVal = lastFloatVal - separation;
			slider->currentVals[i] = floatVal;
		}
		lastFloatVal = floatVal;
	}

	lastFloatVal = slider->currentVals[idx];
	for (i = idx+1; i < slider->count; ++i)
	{
		F64 floatVal = slider->currentVals[i];
		if (lastFloatVal + separation > floatVal)
		{
			floatVal = lastFloatVal + separation;
			slider->currentVals[i] = floatVal;
		}
		lastFloatVal = floatVal;
	}
}

static void ui_SliderSetValueInternal(UISlider *slider, int idx, F64 floatVal, bool allowOutOfRange)
{
	F64 minVal = slider->min + idx * slider->minSeparation;
	F64 maxVal = slider->max + (slider->count - idx - 1) * slider->minSeparation;
	if (slider->step)
		floatVal = roundFloatWithPrecision(floatVal, slider->step);
	if (!allowOutOfRange)
		floatVal = CLAMP(floatVal, minVal, maxVal);
	slider->currentVals[idx] = floatVal;
	ui_SliderEnforceSeparation(slider, idx);
}

bool ui_SliderInput(UISlider *slider, KeyInput *key)
{
	if (key->type != KIT_EditKey)
		return false;
	if (ui_IsActive(UI_WIDGET(slider)))
	{
		F64 step = slider->step;
		F64 mv = slider->currentVals[0];
		if (step == 0.0)
			step = (slider->max - slider->min)/20.0;
		switch (key->scancode)
		{
		case INP_LEFT:
		case INP_LSTICK_LEFT:
			step = -step;
			break;
		case INP_LSTICK_RIGHT:
		case INP_RIGHT:
			break;
		default:
			return false;
		}
		mv = mv + step;
		ui_SliderSetValueAndCallback(slider, mv);
		return true;
	}
	else
		return false;
}

static AtlasTex* ui_SliderGetHandleTex( UISlider* slider )
{
	UISkin* skin = UI_GET_SKIN( slider );

	if( skin->bUseStyleBorders || skin->bUseTextureAssemblies ) {
		if( !ui_IsActive( UI_WIDGET( slider ))) {
			return atlasFindTexture( skin->astrSliderHandleDisabled );
		} if( slider->hoverIdx >= 0 ) {
			return atlasFindTexture( skin->astrSliderHandleHighlight );
		} else if( slider->draggingIdx >= 0 ) {
			return atlasFindTexture( skin->astrSliderHandlePressed );
		} else {
			return atlasFindTexture( skin->astrSliderHandle );
		}
	} else {
		return atlasFindTexture( "eui_slider_handle" );
	}
	
}

static void ui_SliderFillTroughDrawingDescription( UISlider* slider, UIDrawingDescription* desc )
{
	UISkin* skin = UI_GET_SKIN( slider );
		
	if( skin->bUseStyleBorders || skin->bUseTextureAssemblies ) {
		const char* descName;

		if( !ui_IsActive( UI_WIDGET( slider ))) {
			descName = skin->astrSliderTroughDisabled;
		} else {
			descName = skin->astrSliderTrough;
		}

		if( skin->bUseTextureAssemblies && RefSystem_ReferentFromString( "UITextureAssembly", descName )) {
			desc->textureAssemblyName = descName;
		} else {
			desc->styleBorderName = descName;
		}
	} else {
		desc->styleBorderNameUsingLegacyColor = "Default_Capsule_Filled";
	}
}

void ui_SliderDraw(UISlider *slider, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(slider);
	UISkin* skin = UI_GET_SKIN( slider );
	F32 midY = y + h/2;
	F64 min = slider->min;
	F32 diff = slider->max- min;
	Color button, buttonActive, buttonUnfiltered, trough;
	int idx;
	AtlasTex* handle = ui_SliderGetHandleTex( slider );
	UIDrawingDescription troughDesc = { 0 };

	ui_SliderFillTroughDrawingDescription( slider, &troughDesc );

	UI_DRAW_EARLY(slider);

	if (!skin)
	{
		button = buttonActive = buttonUnfiltered = slider->widget.color[0];
		trough = slider->widget.color[1];
	}
	else
	{
		if( skin->bUseStyleBorders || skin->bUseTextureAssemblies ) {
			button = buttonActive = buttonUnfiltered = trough = ColorWhite;
		} else {
			buttonActive = UI_GET_SKIN(slider)->button[2];
			if (!ui_IsActive(UI_WIDGET(slider)))
			{
				buttonUnfiltered = button = UI_GET_SKIN(slider)->button[3];
			}
			else
			{
				buttonUnfiltered = UI_GET_SKIN(slider)->button[1];
				if (ui_IsChanged(UI_WIDGET(slider)))
					button = UI_GET_SKIN(slider)->button[4];
				else if (ui_IsInherited(UI_WIDGET(slider)))
					button = UI_GET_SKIN(slider)->button[5];
				else
					button = UI_GET_SKIN(slider)->button[0];
			}
			trough = UI_GET_SKIN(slider)->trough[0];
		}
	}

	{
		CBox troughBox = box;
		float troughHeight = ui_DrawingDescriptionHeight( &troughDesc );
		troughBox.ly = (box.ly + box.hy - troughHeight) / 2;
		troughBox.hy = (box.ly + box.hy + troughHeight) / 2;
		ui_DrawingDescriptionDraw( &troughDesc, &troughBox, scale, z, 255, trough, ColorWhite );
	}
	for (idx = 0; idx < slider->count; ++idx)
	{
		F32 handleLeft = x + (w - handle->width * scale) * ui_SliderRemoveBias(slider, slider->currentVals[idx], min, diff);
		CBox buttonBox = {handleLeft, y + h/2 - handle->height * scale / 2, handleLeft + handle->width * scale, y + h/2 + handle->height * scale / 2};
		display_sprite_box(handle, &buttonBox, z + 0.2, RGBAFromColor((ui_IsFocused(slider) && !(slider->draggingIdx == idx))?buttonActive:button));
	}

	for (idx = 0; idx < eaSize(&slider->eaSpecials); idx++)
	{
		UISliderSpecialValue *val = slider->eaSpecials[idx];
		F32 specialX = x + (w - handle->width * scale) * ui_SliderRemoveBias(slider, val->value, min, diff);
		specialX += handle->width * scale / 2;

		gfxDrawLine(specialX, y, z+0.4, specialX, y+h, val->c);
	}

	UI_DRAW_LATE(slider);
}

void ui_SliderTick(UISlider *slider, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(slider);
	F32 midY = y + h/2;
	AtlasTex *handle = ui_SliderGetHandleTex( slider );
	F64 min = slider->min;
	F64 diff = slider->max - min;
	CBox handleBox;
	int idx;

	UI_TICK_EARLY(slider, false, true);

	slider->hoverIdx = -1;
	for (idx = 0; idx < slider->count; ++idx)
	{
		F32 handleLeft = x + (w - handle->width * scale) * ui_SliderRemoveBias(slider, slider->currentVals[idx], min, diff);

		BuildCBox(&handleBox, handleLeft, y + h/2 - handle->height * scale / 2, handle->width * scale, handle->height * scale);

		if (slider->draggingIdx == idx)
		{
			if (!mouseIsDown(MS_LEFT))
			{
				slider->draggingIdx = -1;
				if (slider->changedF)
					slider->changedF(slider, true, slider->changedData);
			}
			else
			{
				F32 newX = CLAMP((g_ui_State.mouseX - x) - slider->grabbedX, 0, w - handle->width * scale);
				F64 floatVal = ui_SliderBias(slider, (newX / (w - handle->width * scale)), min, diff);
				if (slider->step)
					floatVal = roundFloatWithPrecision(floatVal, slider->step);
				if (floatVal != slider->currentVals[idx])
				{
					ui_SliderSetValueInternal(slider, idx, floatVal, false);
					if (slider->policy == UISliderContinuous && slider->changedF)
						slider->changedF(slider, false, slider->changedData);
				}
				
			}
			ui_SoftwareCursorThisFrame();
			inpHandled();
			ui_CursorLock();
		}
		else if (slider->draggingIdx < 0 && mouseDownHit(MS_LEFT, &handleBox))
		{
			ui_SetFocus(slider);
			slider->draggingIdx = idx;
			slider->grabbedX = g_ui_State.mouseX - handleLeft;
			inpHandled();
		}
		else if (slider->count==1 && mouseClickHit(MS_LEFT, &box))
		{
			F32 newX = CLAMP((g_ui_State.mouseX - x), 0, w - handle->width * scale);
			F64 floatVal = ui_SliderBias(slider, (newX / (w - handle->width * scale)), min, diff);
			
			ui_SetFocus(slider);
			
			if (slider->step)
				floatVal = roundFloatWithPrecision(floatVal, slider->step);
			if (floatVal != slider->currentVals[idx])
			{
				ui_SliderSetValueInternal(slider, idx, floatVal, false);
				if (slider->changedF)
					slider->changedF(slider, true, slider->changedData);
			}
		}
		else if(mouseCollision( &handleBox ))
		{
			slider->hoverIdx = idx;
			inpHandled();
		}
	}

	UI_TICK_LATE(slider);
}

void ui_SliderSetCount(UISlider *slider, int count, F64 separation)
{
	F64 *vals = calloc(count, sizeof(F64));

	if (slider->currentVals)
	{
		CopyStructs(vals, slider->currentVals, MIN(slider->count, count));
		free(slider->currentVals);
	}

	slider->currentVals = vals;
	slider->count = count;

	slider->minSeparation = separation;

	ui_SliderSetValueInternal(slider, 0, slider->currentVals[0], false);
}

void ui_SliderFreeInternal(UISlider *slider)
{
	eaDestroyEx(&slider->eaSpecials, NULL);
	free(slider->currentVals);

	ui_WidgetFreeInternal(UI_WIDGET(slider));
}

UISlider *ui_SliderCreate(F32 x, F32 y, F32 width, F64 min, F64 max, F64 current)
{
	UISlider *slider = (UISlider *)calloc(1, sizeof(UISlider));
	UIStyleFont *font = GET_REF(g_ui_State.font);
	F64 step;
	ui_WidgetInitialize(UI_WIDGET(slider), ui_SliderTick, ui_SliderDraw, ui_SliderFreeInternal, ui_SliderInput, ui_WidgetDummyFocusFunc);
	ui_WidgetSetPosition(UI_WIDGET(slider), x, y);
	if (UI_GET_SKIN(slider))
		font = GET_REF(UI_GET_SKIN(slider)->hNormal);
	ui_WidgetSetDimensions(UI_WIDGET(slider), width, ui_StyleFontLineHeight(font, 1.f) + UI_STEP);

	step = 0;
	slider->minSeparation = 0;
	slider->draggingIdx = -1;
	ui_SliderSetCount(slider, 1, step);
	ui_SliderSetRange(slider, min, max, step);
	ui_SliderSetValueAndCallback(slider, current);
	return slider;
}

void ui_SliderSetRange(UISlider *slider, F64 min, F64 max, F64 step)
{
	assert(min || true);
	assert(max || true);
	assert(step || true);
	slider->min = min;
	slider->max = max;
	slider->step = step;
	ui_SliderSetValueInternal(slider, 0,slider->currentVals[0], false);
}

void ui_SliderSetBias(SA_PARAM_NN_VALID UISlider *slider, F32 bias, F32 bias_offset)
{
	slider->bias = bias;
	slider->bias_offset = bias_offset;
}


F64 ui_SliderGetValueEx(UISlider *slider, int idx)
{
	if (idx < 0)
		idx = 0;
	if (idx >= slider->count)
		idx = slider->count - 1;
	return slider->currentVals[idx];
}

void ui_SliderSetValueAndCallbackEx(UISlider *slider, int idx, F64 val, int callChangedCallback, bool allowOutOfRange)
{
	F64 floatVal;

	if (idx < 0)
		idx = 0;
	if (idx >= slider->count)
		idx = slider->count - 1;
	
	floatVal = val;
	ui_SliderSetValueInternal(slider, idx, floatVal, allowOutOfRange);

	if (slider->changedF && callChangedCallback)
		slider->changedF(slider, true, slider->changedData);
}

void ui_SliderSetChangedCallback(UISlider *slider, UISliderChangeFunc changedF, UserData changedData)
{
	slider->changedF = changedF;
	slider->changedData = changedData;
}

void ui_SliderSetPolicy(UISlider *slider, UISliderPolicy policy)
{
	slider->policy = policy;
}

void ui_SliderAddSpecialValue(SA_PARAM_NN_VALID UISlider *slider, F64 value, Color c)
{
	UISliderSpecialValue *val = calloc(1, sizeof(UISliderSpecialValue));

	val->value = value;
	val->c = c;
	eaPush(&slider->eaSpecials, val);
}
