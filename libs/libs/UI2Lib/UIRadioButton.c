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

#include "UIRadioButton.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

UIRadioButtonGroup *ui_RadioButtonGroupCreate(void)
{
	UIRadioButtonGroup *group = (UIRadioButtonGroup *)calloc(1, sizeof(UIRadioButtonGroup));
	return group;
}

void ui_RadioButtonGroupFreeInternal(UIRadioButtonGroup *group)
{
	int i;
	for (i = 0; i < eaSize(&group->buttons); i++)
		group->buttons[i]->group = NULL;
	eaDestroy(&group->buttons);
	free(group);
}

void ui_RadioButtonGroupAdd(UIRadioButtonGroup *group, UIRadioButton *radio)
{
	assert(!radio->group);
	eaPush(&group->buttons, radio);
	radio->group = group;
}

void ui_RadioButtonGroupRemove(UIRadioButtonGroup *group, UIRadioButton *radio)
{
	assert(radio->group == group);
	eaFindAndRemove(&group->buttons, radio);
	radio->group = NULL;
}

UIRadioButton *ui_RadioButtonGroupGetActive(UIRadioButtonGroup *group)
{
	int i;
	for (i = 0; i < eaSize(&group->buttons); i++)
	{
		if (group->buttons[i]->state)
			return group->buttons[i];
	}
	return NULL;
}

void ui_RadioButtonGroupSetToggledCallback(SA_PARAM_NN_VALID UIRadioButtonGroup *group, UIActivationFunc toggledF, UserData toggledData)
{
	group->toggledF = toggledF;
	group->toggledData = toggledData;
}

void ui_RadioButtonGroupSetActive(UIRadioButtonGroup *group, UIRadioButton *radio)
{
	int i;
	assert(radio->group == group);
	for (i = 0; i < eaSize(&group->buttons); i++)
	{
		if (group->buttons[i] == radio && !radio->state)
			radio->state = true;
		else if (group->buttons[i] != radio && group->buttons[i]->state)
			group->buttons[i]->state = false;
	}
}

void ui_RadioButtonGroupSetActiveAndCallback(UIRadioButtonGroup *group, UIRadioButton *radio)
{
	int i;
	assert(radio->group == group);
	for (i = 0; i < eaSize(&group->buttons); i++)
	{
		if (group->buttons[i] == radio && !radio->state)
		{
			radio->state = true;
			if (radio->toggledF)
				radio->toggledF(radio, radio->toggledData);
		}
		else if (group->buttons[i] != radio && group->buttons[i]->state)
		{
			group->buttons[i]->state = false;
			if (group->buttons[i]->toggledF)
				group->buttons[i]->toggledF(group->buttons[i], group->buttons[i]->toggledData);
		}
	}
	if(group->toggledF)
		group->toggledF(group, group->toggledData); 
}

F32 ui_RadioButtonGroupSetPosition(UIRadioButtonGroup *group, F32 x, F32 y)
{
	int i;
	for (i = 0; i < eaSize(&group->buttons); i++)
	{
		ui_WidgetSetPosition((UIWidget *)group->buttons[i], x, y);
		y += group->buttons[i]->widget.height + 4;
	}
	return y;
}

UIRadioButton *ui_RadioButtonCreate(F32 x, F32 y, const char *text, UIRadioButtonGroup *group)
{
	UIRadioButton *radio = (UIRadioButton *)calloc(1, sizeof(UIRadioButton));
	ui_RadioButtonInitialize(radio, x, y, text, group);
	return radio;
}

void ui_RadioButtonInitialize(UIRadioButton *radio, F32 x, F32 y, const char *text, UIRadioButtonGroup *group )
{
	ui_WidgetInitialize(UI_WIDGET(radio), ui_RadioButtonTick, ui_RadioButtonDraw, ui_RadioButtonFree, ui_RadioButtonInput, ui_WidgetDummyFocusFunc);
	ui_WidgetSetPosition(UI_WIDGET(radio), x, y);
	ui_RadioButtonSetText(radio, text);
	if (group)
		ui_RadioButtonGroupAdd(group, radio);
}

void ui_RadioButtonFree(UIRadioButton *radio)
{
	if (radio->group)
	{
		UIRadioButtonGroup *group = radio->group;
		ui_RadioButtonGroupRemove(group, radio);
		if (eaSize(&group->buttons) == 0)
			ui_RadioButtonGroupFreeInternal(group);
	}
	ui_WidgetFreeInternal(UI_WIDGET(radio));
}

void ui_RadioButtonActivate(UIRadioButton *radio)
{
	if (!ui_IsActive(UI_WIDGET(radio)))
		return;
	else if (radio->group)
		ui_RadioButtonGroupSetActiveAndCallback(radio->group, radio);
	else if (!radio->state)
	{
		radio->state = true;
		if (radio->toggledF)
			radio->toggledF(radio, radio->toggledData);
	}
}

static AtlasTex* ui_RadioButtonGetTex( UIRadioButton* radio )
{
	UISkin* skin = UI_GET_SKIN( radio );

	if( skin->bUseTextureAssemblies || skin->bUseStyleBorders ) {
		if( radio->state ) {
			return atlasLoadTexture( skin->pchCheckBoxChecked );
		} else if( skin->pchRadioBoxHighlight && ui_IsHovering( UI_WIDGET( radio ))) {
			return atlasLoadTexture( skin->pchCheckBoxHighlight );
		} else {
			return atlasLoadTexture( skin->pchCheckBoxUnchecked );
		}
	} else {
		if( radio->state ) {
			return atlasLoadTexture( "eui_tickybox_checked_8x8" );
		} else {
			return atlasLoadTexture( "eui_tickybox_unchecked_8x8" );
		}
	}
}

void ui_RadioButtonSetText(UIRadioButton *radio, const char *text)
{
	AtlasTex *checkTex = ui_RadioButtonGetTex( radio );
	F32 fWidth;
	UIStyleFont *font = GET_REF(UI_GET_SKIN(radio)->hNormal);
	ui_WidgetSetTextString(UI_WIDGET(radio), text);
	fWidth = checkTex->width + UI_HSTEP + ui_StyleFontWidth(font, 1.f, ui_WidgetGetText(UI_WIDGET(radio)));
	ui_WidgetSetDimensions(UI_WIDGET(radio), fWidth, ui_StyleFontLineHeight(font, 1.f) + UI_HSTEP);
}

void ui_RadioButtonSetMessage(UIRadioButton *radio, const char *message)
{
	AtlasTex *checkTex = ui_RadioButtonGetTex( radio );
	F32 fWidth;
	UIStyleFont *font = GET_REF(UI_GET_SKIN(radio)->hNormal);
	ui_WidgetSetTextMessage(UI_WIDGET(radio), message);
	fWidth = checkTex->width + UI_HSTEP + ui_StyleFontWidth(font, 1.f, ui_WidgetGetText(UI_WIDGET(radio)));
	ui_WidgetSetDimensions(UI_WIDGET(radio), fWidth, ui_StyleFontLineHeight(font, 1.f) + UI_HSTEP);
}

void ui_RadioButtonSetToggledCallback(UIRadioButton *radio, UIActivationFunc toggledF, UserData toggledData)
{
	radio->toggledF = toggledF;
	radio->toggledData = toggledData;
}

bool ui_RadioButtonInput(UIRadioButton *radio, KeyInput *input)
{
	if (input->type != KIT_EditKey)
		return false;
	if (input->scancode == INP_SPACE || input->scancode == INP_RETURN)
	{
		ui_RadioButtonActivate(radio);
		return true;
	}
	return false;
}

void ui_RadioButtonTick(UIRadioButton *radio, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(radio);
	UI_TICK_EARLY(radio, false, true);

	if (mouseClickHit(MS_LEFT, &box))
	{
		ui_SetFocus(radio);
		ui_RadioButtonActivate(radio);
		inpHandled();
	}
	UI_TICK_LATE(radio);
}

void ui_RadioButtonDraw(UIRadioButton *radio, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(radio);
	CBox checkBox;
	UIStyleFont *font = GET_REF(UI_GET_SKIN(radio)->hNormal);
	AtlasTex *checkTex = ui_RadioButtonGetTex( radio );
	Color c;
	UISkin* skin = UI_GET_SKIN( radio );

	UI_DRAW_EARLY(radio);

	if (!UI_GET_SKIN(radio))
		c = radio->widget.color[1];
	else if (skin->bUseStyleBorders || skin->bUseTextureAssemblies)
		c = ColorWhite;
	else if (!ui_IsActive(UI_WIDGET(radio)))
		c = UI_GET_SKIN(radio)->button[3];
	else
		c = UI_GET_SKIN(radio)->button[0];

	ui_StyleFontUse(font, false, UI_WIDGET(radio)->state);
	BuildCBox(&checkBox, x, (y + h/2) - checkTex->height * scale/2, checkTex->width * scale, checkTex->height * scale);
	display_sprite_box(checkTex, &checkBox, z, RGBAFromColor(c));
	gfxfont_Printf(x + checkTex->width * scale + UI_HSTEP_SC, y + h / 2, z, scale, scale, CENTER_Y, "%s", ui_WidgetGetText(UI_WIDGET(radio)));

	UI_DRAW_LATE(radio);
}
