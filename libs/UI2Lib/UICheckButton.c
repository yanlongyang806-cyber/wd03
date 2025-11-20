/***************************************************************************



***************************************************************************/

#include "Color.h"

#include "inputText.h"
#include "inputMouse.h"

#include "GfxSprite.h"
#include "GfxSpriteText.h"
#include "GfxTexAtlas.h"
#include "GfxClipper.h"

#include "UICheckButton.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

bool ui_CheckButtonInput(UICheckButton *check, KeyInput *input)
{
	if (input->type != KIT_EditKey)
		return false;
	if ((input->scancode == INP_SPACE || input->scancode == INP_RETURN) && ui_IsActive(UI_WIDGET(check)))
	{
		ui_CheckButtonToggle(check);
		return true;
	}
	return false;
}

void ui_CheckButtonTick(UICheckButton *check, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(check);
	UI_TICK_EARLY(check, false, true);
	if (mouseClickHit(MS_LEFT, &box) || mouseDoubleClickHit(MS_LEFT, &box) || 
		(mouseUpHit(MS_LEFT, &box) && check->pressed))
	{
		check->pressed = false;
		ui_SetFocus(check);
		inpHandled();
		ui_CheckButtonToggle(check);
	}
	else if (mouseDownHit(MS_LEFT, &box))
	{
		check->pressed = true;
		ui_SetFocus(check);
		inpHandled();
	}
	else if (!mouseIsDown(MS_LEFT))
	{
		check->pressed = false;
	}
	UI_TICK_LATE(check);
}

static AtlasTex* ui_CheckButtonGetTex( UICheckButton* check )
{
	UISkin* skin = UI_GET_SKIN( check );

	if( skin->bUseTextureAssemblies || skin->bUseStyleBorders ) {
		if( ui_CheckButtonGetState( check )) {
			return atlasLoadTexture( skin->pchCheckBoxChecked );
		} else if( skin->pchCheckBoxHighlight && ui_IsHovering( UI_WIDGET( check ))) {
			return atlasLoadTexture( skin->pchCheckBoxHighlight );
		} else {
			return atlasLoadTexture( skin->pchCheckBoxUnchecked );
		}
	} else {
		if( ui_CheckButtonGetState( check )) {
			return atlasLoadTexture( "eui_tickybox_checked_8x8" );
		} else {
			return atlasLoadTexture( "eui_tickybox_unchecked_8x8" );
		}
	}
}

void ui_CheckButtonDraw(UICheckButton *check, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(check);
	UISkin* pSkin = UI_GET_SKIN( check );
	CBox checkBox;
	UIStyleFont *font = ui_WidgetGetFont(UI_WIDGET(check));
	F32 textOffset;
	AtlasTex* checkTex = ui_CheckButtonGetTex( check );
	const char* widgetText = ui_WidgetGetText( UI_WIDGET( check ));
	Color c;

	if (!pSkin)
		c = check->widget.color[1];
	else if (!ui_IsActive(UI_WIDGET(check)))
		c = pSkin->button[3];
	else if (ui_IsHovering(UI_WIDGET(check)) || ui_IsFocused(check))
	{
		if (ui_IsChanged(UI_WIDGET(check)))
			c = ColorLerp(pSkin->button[1], pSkin->button[4], 0.5);
		else if (ui_IsInherited(UI_WIDGET(check)))
			c = ColorLerp(pSkin->button[1], pSkin->button[5], 0.5);
		else
			c = pSkin->button[1];
	}
	else if (ui_IsChanged(UI_WIDGET(check)))
		c = pSkin->button[4];
	else if (ui_IsInherited(UI_WIDGET(check)))
		c = pSkin->button[5];
	else
		c = pSkin->button[0];
	
	UI_DRAW_EARLY(check);
	textOffset = (y + h/2);
	BuildIntCBox(&checkBox, x, textOffset - checkTex->height * scale / 2, checkTex->width * scale, checkTex->height * scale);
	if( pSkin->bUseTextureAssemblies || pSkin->bUseStyleBorders ) {
		display_sprite_box(checkTex, &checkBox, z, -1);
	} else {
		display_sprite_box(checkTex, &checkBox, z, RGBAFromColor(c));
	}
	ui_StyleFontUse(font, false, UI_WIDGET(check)->state);
	gfxfont_Printf(x + (checkTex->width + UI_HSTEP) * scale, textOffset, z, scale, scale, CENTER_Y, "%s", widgetText);
	UI_DRAW_LATE(check);
}


void ui_CheckButtonResize(UICheckButton* check)
{
	const char* text = ui_WidgetGetText( UI_WIDGET( check ));
	UIStyleFont *font = ui_WidgetGetFont( UI_WIDGET( check ));
	F32 fontHeight = ui_StyleFontLineHeight(font, 1.f);
	AtlasTex* checkTex = ui_CheckButtonGetTex( check );
	ui_WidgetSetDimensions( UI_WIDGET( check ),
							checkTex->width + (text[0] ? UI_HSTEP : 0) + ui_StyleFontWidth(font, 1.f, text),
							MAX(fontHeight, checkTex->height) );
}

UICheckButton *ui_CheckButtonCreate(F32 x, F32 y, const char *text, bool state)
{
	UICheckButton *check = (UICheckButton *)calloc(1, sizeof(UICheckButton));
	ui_CheckButtonInitialize(check, x, y, text, state);
	return check;
}

void ui_CheckButtonInitialize(UICheckButton *check, F32 x, F32 y, const char *text, bool state)
{
	ui_WidgetInitialize(UI_WIDGET(check),
		ui_CheckButtonTick, ui_CheckButtonDraw, ui_CheckButtonFreeInternal, ui_CheckButtonInput, ui_WidgetDummyFocusFunc);
	ui_WidgetSetPosition(UI_WIDGET(check), x, y);
	ui_CheckButtonSetText(check, text);
	check->state = state;
	check->statePtr = NULL;
}

void ui_CheckButtonFreeInternal(UICheckButton *check)
{
	ui_WidgetFreeInternal(UI_WIDGET(check));
}

void ui_CheckButtonSetText(UICheckButton *check, const char *text)
{
	ui_WidgetSetTextString(UI_WIDGET(check), text);
	ui_CheckButtonResize(check);
}

void ui_CheckButtonSetState(UICheckButton *check, bool state)
{
	if (check->statePtr)
		*check->statePtr = state;
	check->state = state;
}


int ui_CheckButtonWidthNoText(void)
{
	return atlasLoadTexture( ui_GetActiveSkin()->pchCheckBoxChecked )->width;
}

int ui_CheckButtonHeightNoText(void)
{
    return atlasLoadTexture( ui_GetActiveSkin()->pchCheckBoxChecked )->height;
}

void ui_CheckButtonSetStateAndCallback(UICheckButton *check, bool state)
{
	bool bChanged = false;
	if ((check->statePtr && *check->statePtr != state) || check->state != state)
		bChanged = true;
	ui_CheckButtonSetState(check, state);
	if (bChanged && check->toggledF)
		check->toggledF(check, check->toggledData);
}

void ui_CheckButtonToggle(UICheckButton *check)
{
	if (check->statePtr)
		ui_CheckButtonSetStateAndCallback(check, !(*check->statePtr));
	else
		ui_CheckButtonSetStateAndCallback(check, !check->state);
}

bool ui_CheckButtonGetState(UICheckButton *check)
{
	if (check->statePtr)
		check->state = *check->statePtr;
	return check->state;
}

void ui_CheckButtonSetToggledCallback(UICheckButton *check, UIActivationFunc toggledF, UserData toggledData)
{
	check->toggledF = toggledF;
	check->toggledData = toggledData;
}
