/***************************************************************************



***************************************************************************/

#include "gclCommandParse.h"

#include "inputLib.h"
#include "inputMouse.h"
#include "inputText.h"
#include "inputKeybind.h"

#include "Color.h"
#include "GfxClipper.h"
#include "GfxSprite.h"
#include "GfxSpriteText.h"
#include "GfxTexAtlas.h"
#include "StringUtil.h"

#include "UIButton.h"
#include "UISprite.h"
#include "UITextureAssembly.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

bool ui_ButtonInput(UIButton *button, KeyInput *input)
{
	if (input->type != KIT_EditKey)
		return false;
	if (input->scancode == INP_RETURN && ui_IsActive(UI_WIDGET(button)))
	{
		ui_ButtonClick(button);
		return true;
	}
	else
		return false;
}

static const char* ui_ButtonGetDescNameFromSkin(UIButton *button, UISkin *skin)
{
	UIWidget *widget = UI_WIDGET(button);
	if (!ui_IsActive(widget)) {
		return skin->astrButtonStyleDisabled;		
	} else if(button->down || (button->pressed && ui_IsHovering(widget))) {
		return skin->astrButtonStylePressed;
	} else if (ui_IsHovering(widget)) {
		return skin->astrButtonStyleHighlight;		
	} else if( ui_IsFocused(widget) ) {
		return skin->astrButtonStyleFocused;
	} else {
		return skin->astrButtonStyle;
	}
}

static AtlasTex* ui_ButtonGetImage( UIButton* button )
{
	// This should have the same branching logic as ui_ButtonGetDescNameFromSkin()
	UIWidget *widget = UI_WIDGET(button);
	if (!ui_IsActive(widget)) {
		return button->disabledImage;		
	} else if(button->down || (button->pressed && ui_IsHovering(widget))) {
		return button->pressedImage;
	} else if (ui_IsHovering(widget)) {
		return button->highlightImage;
	} else if( ui_IsFocused(widget) ) {
		return button->focusedImage;
	} else {
		return button->normalImage;
	}
}

static UIStyleFont* ui_ButtonGetFont( UIButton* button )
{
	UISkin* skin = UI_GET_SKIN( button );
	UIStyleFont* font = NULL;
	
	// This should have the same branching logic as ui_ButtonGetDescNameFromSkin()
	UIWidget *widget = UI_WIDGET(button);
	if (!ui_IsActive(widget)) {
		font = GET_REF( skin->hButtonFontDisabled );
	} else if(button->down || (button->pressed && ui_IsHovering(widget))) {
		font = GET_REF( skin->hButtonFontPressed );
	} else if (ui_IsHovering(widget)) {
		font = GET_REF( skin->hButtonFontHighlight );
	} else if( ui_IsFocused(widget) ) {
		font = GET_REF( skin->hButtonFontFocused );
	} else {
		font = GET_REF( skin->hButtonFont );
	}

	if( !font ) {
		font = ui_WidgetGetFont( UI_WIDGET( button ));
	}

	return font;
}

static void ui_ButtonFillDrawingDescription( UIButton* button, UIDrawingDescription* desc )
{
	UISkin* skin = UI_GET_SKIN( button );
		
	if( IS_HANDLE_ACTIVE( button->hBorderOverride )) {
		desc->styleBorderNameUsingLegacyColor = REF_STRING_FROM_HANDLE( button->hBorderOverride );
	} else if( skin->bUseStyleBorders || skin->bUseTextureAssemblies ) {
		const char* descName = ui_ButtonGetDescNameFromSkin( button, skin );
		if( skin->bUseTextureAssemblies && RefSystem_ReferentFromString( "UITextureAssembly", descName )) {
			desc->textureAssemblyName = descName;
		} else {
			desc->styleBorderName = descName;
		}
	} else {
		desc->styleBorderNameUsingLegacyColor = "Default_Capsule_Filled";
	}
}

void ui_ButtonTick(UIButton *button, UI_PARENT_ARGS)
{
	UIDrawingDescription desc = { 0 };
	UI_GET_COORDINATES(button);

	ui_ButtonFillDrawingDescription( button, &desc );
	if( !button->bChildrenOverlapBorder ) {
		ui_DrawingDescriptionInnerBoxCoords( &desc, &x, &y, &w, &h, scale );
	}

	UI_TICK_EARLY(button, true, true);

	if (mouseClickHit(MS_LEFT, &box))
	{
		button->pressed = false;
		if (button->bFocusOnClick)
			ui_SetFocus(button);
		inpHandled();
		
		// full click; call any defined callbacks
		ui_ButtonDown(button);
		ui_ButtonUp(button);
		ui_ButtonClick(button);
	}
	else if (mouseUpHit(MS_LEFT, &box) && button->pressed)
	{
		button->pressed = false;
		if (button->bFocusOnClick)
			ui_SetFocus(button);
		inpHandled();
		
		// mouse-up following a mouse-down; call up and click callbacks
		ui_ButtonUp(button);
		ui_ButtonClick(button);
	}
	else if (mouseDownHit(MS_LEFT, &box))
	{
		button->pressed = true;
		if (button->bFocusOnClick)
			ui_SetFocus(button);
		inpHandled();
		
		// mouse-down; call down callback
		ui_ButtonDown(button);
	}
	else if (!mouseIsDown(MS_LEFT) && button->pressed) {
		button->pressed = false;
		
		// mouse-up following a mouse-down, but the cursor was dragged off the button; up only, not a click
		ui_ButtonUp(button);
	}

	UI_TICK_LATE(button);
}

Color ui_WidgetButtonColor(UIWidget *widget, bool down, bool pressed)
{
	Color c;
	UISkin* widgetSkin = ui_WidgetGetSkin(widget);
	if (widgetSkin)
	{
		// Default(0), highlight(1), press(2), disabled(3), changed(4), parented(5).
		if (down || (pressed && ui_IsHovering(widget)))
		{
			if (ui_IsChanged(widget))
				c = ColorLerp(widgetSkin->button[2], widgetSkin->button[4], 0.5);
			else if (ui_IsInherited(widget))
				c = ColorLerp(widgetSkin->button[2], widgetSkin->button[5], 0.5);
			else
				c = widgetSkin->button[2];
		}
		else if (ui_IsHovering(widget) || ui_IsFocused(widget))
		{
			if (ui_IsChanged(widget))
				c = ColorLerp(widgetSkin->button[1], widgetSkin->button[4], 0.5);
			else if (ui_IsInherited(widget))
				c = ColorLerp(widgetSkin->button[1], widgetSkin->button[5], 0.5);
			else
				c = widgetSkin->button[1];
		}
		else if (!ui_IsActive(widget))
			c = widgetSkin->button[3];
		else if (ui_IsChanged(widget))
			c = widgetSkin->button[4];
		else if (ui_IsInherited(widget))
			c = widgetSkin->button[5];
		else
			c = widgetSkin->button[0];
	}
	else
	{
		if (ui_IsFocused(widget))
			c = ColorLighten(widget->color[0], 50);
		else if (ui_IsHovering(widget))
			c = ColorLighten(widget->color[0], 50);
		else
			c = widget->color[0];
	}
	return c;
}

void ui_ButtonDraw(UIButton *button, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(button);
	UIDrawingDescription desc = { 0 };
	Color c;
	AtlasTex* activeImage;

	ui_ButtonFillDrawingDescription( button, &desc );

	UI_DRAW_EARLY(button);
	c = ui_WidgetButtonColor(UI_WIDGET(button), button->down, button->pressed);
	if( button->bDrawBorderOutsideRect ) {
		clipperPush( NULL );
		ui_DrawingDescriptionDraw( &desc, &box, scale, z, 255, c, ColorBlack );
		clipperPop();
	} else {
		ui_DrawingDescriptionDraw( &desc, &box, scale, z, 255, c, ColorBlack );
	}

	if (!button->bChildrenOverlapBorder)
	{
		CBox paddedBox = box;
		ui_DrawingDescriptionInnerBox( &desc, &paddedBox, scale );

		x = paddedBox.lx;
		y = paddedBox.ly;
		w = paddedBox.hx - paddedBox.lx;
		h = paddedBox.hy - paddedBox.ly;
	}

	activeImage = ui_ButtonGetImage( button );
	if( !activeImage ) {
		// Drawing style 1 -- just text, centered in button
		UIStyleFont *pFont = ui_ButtonGetFont(button);
		const char* widgetText = NULL_TO_EMPTY( ui_WidgetGetText(UI_WIDGET(button)));
		CBox drawBox;
		BuildCBox( &drawBox, x, y, w, h );
		ui_StyleFontUse(pFont, false, UI_WIDGET(button)->state);
		ui_DrawTextInBoxSingleLine( pFont, widgetText, !button->bNoAutoTruncateText, &drawBox, z + 0.05, scale,
									(button->textOffsetFrom ? button->textOffsetFrom : UINoDirection) );
	} else if( activeImage && nullStr( ui_WidgetGetText( UI_WIDGET( button )))) {
		// Drawing style 2 -- just an image, centered in button
		float drawWidth;
		float drawHeight;
		Color modColor;
		if( button->bImageStretch ) {
			drawWidth = w;
			drawHeight = h;
		} else {
			drawWidth = activeImage->width * scale;
			drawHeight = activeImage->height * scale;
		}

		if( button->spriteInheritsColor ) {
			modColor = c;
		} else {
			modColor = ColorWhite;
		}

		display_sprite( activeImage, floor( x + (w - drawWidth) / 2 ), floor( y + (h - drawHeight) / 2 ), z + 0.05,
						drawWidth / activeImage->width, drawHeight / activeImage->height,
						RGBAFromColor( modColor ));
	} else if( !button->bCenterImageAndText ) {
		// Drawing style 3 -- image on the left, text on the right
		float drawWidth;
		float drawHeight;

		if( button->bImageStretch ) {
			drawHeight = h;
			drawWidth = drawHeight / activeImage->height * activeImage->width;
		} else {
			drawWidth = activeImage->width * scale;
			drawHeight = activeImage->height * scale;
		}

		// draw image
		{
			Color modColor;
			if( button->spriteInheritsColor ) {
				modColor = c;
			} else {
				modColor = ColorWhite;
			}

			display_sprite( activeImage, floor( x ), floor( y + (h - drawHeight) / 2 ), z + 0.05,
							drawWidth / activeImage->width, drawHeight / activeImage->height,
							RGBAFromColor( modColor ));
		}

		// draw text
		{
			UIStyleFont *pFont = ui_ButtonGetFont(button);
			const char* text = NULL_TO_EMPTY( ui_WidgetGetText(UI_WIDGET(button)));
			float textWidth = ui_StyleFontWidth( pFont, scale, text );
			CBox drawBox;

			BuildCBox( &drawBox, x + drawWidth + UI_HSTEP, y, w - drawWidth - UI_HSTEP, h );
			ui_StyleFontUse(pFont, false, UI_WIDGET(button)->state);
			ui_DrawTextInBoxSingleLine( pFont, text, !button->bNoAutoTruncateText, &drawBox, z + 0.05, scale,
										(button->textOffsetFrom ? button->textOffsetFrom : UIRight) );
		}
	} else {
		// Drawing style 4 -- image w/ text next to it, centered
		UIStyleFont* pFont = ui_ButtonGetFont( button );
		const char* text = NULL_TO_EMPTY( ui_WidgetGetText( UI_WIDGET( button )));
		float textWidth = ui_StyleFontWidth( pFont, scale, text );
		float drawWidth;
		float drawHeight;
		float xOffset;
		Color modColor;
		
		if( button->bImageStretch ) {
			drawHeight = h;
			drawWidth = drawHeight / activeImage->height * activeImage->width;
		} else {
			drawWidth = activeImage->width * scale;
			drawHeight = activeImage->height * scale;
		}

		xOffset = MAX( 0, (w - (drawWidth + UI_HSTEP + textWidth)) / 2 );
		if( button->spriteInheritsColor ) {
			modColor = c;
		} else {
			modColor = ColorWhite;
		}

		display_sprite( activeImage, floor( x + xOffset ), floor( y + (h - drawHeight) / 2 ), z + 0.05,
						drawWidth / activeImage->width, drawHeight / activeImage->height,
						RGBAFromColor( modColor ));
		{
			CBox drawBox;
			BuildCBox( &drawBox, x + xOffset + drawWidth + UI_HSTEP, y, w - (xOffset + drawWidth + UI_HSTEP), h );
			ui_StyleFontUse( pFont, false, UI_WIDGET( button )->state );
			ui_DrawTextInBoxSingleLine( pFont, text, !button->bNoAutoTruncateText, &drawBox, z + 0.05, scale, UILeft );
		}
	}
	
	UI_DRAW_LATE(button);
}

UIButton *ui_ButtonCreateEx(const char *text, F32 x, F32 y, UIActivationFunc clickedF, UserData clickedData MEM_DBG_PARMS)
{
	UIButton *button = (UIButton *)calloc(1, sizeof(UIButton));
	ui_ButtonInitialize(button, text, x, y, clickedF, clickedData MEM_DBG_PARMS_CALL);
	return button;
}

UIButton *ui_ButtonCreateWithIconEx(const char *text, const char *texture, UIActivationFunc clickedF, UserData clickedData MEM_DBG_PARMS)
{
	UIButton *button = ui_ButtonCreateEx(NULL, 0, 0, clickedF, clickedData MEM_DBG_PARMS_CALL);
	ui_ButtonSetTextAndIconAndResize(button, text, texture);
	return button;
}

UIButton* ui_ButtonCreateWithDownUpEx(SA_PARAM_OP_STR const char *text, F32 x, F32 y, UIActivationFunc downF, UserData downData, UIActivationFunc upF, UserData upData MEM_DBG_PARMS)
{
	UIButton* button = ui_ButtonCreateEx(text, x, y, NULL, NULL MEM_DBG_PARMS_CALL);
	ui_ButtonSetDownCallback(button, downF, downData);
	ui_ButtonSetUpCallback(button, upF, upData);
	return button;
}

void ui_ButtonSetTextAndIconAndResize(UIButton *pButton, const char *pText, const char *pIcon)
{
	ui_ButtonSetText( pButton, pText );
	if( pIcon ) {
		ui_ButtonSetImage( pButton, pIcon );
	} else {
		ui_ButtonClearImage( pButton );
	}
	ui_ButtonResize(pButton);
}

UIButton *ui_ButtonCreateImageOnlyEx(const char *texture, F32 x, F32 y, UIActivationFunc clickedF, UserData clickedData MEM_DBG_PARMS)
{
	UIButton *button = ui_ButtonCreateEx(NULL, x, y, clickedF, clickedData MEM_DBG_PARMS_CALL);
	ui_ButtonSetImage(button, texture);
	ui_ButtonResize(button);
	button->bChildrenOverlapBorder = true;
	return button;
}

void ui_ButtonInitialize(UIButton *button, const char *text, F32 x, F32 y, UIActivationFunc clickedF, UserData clickedData MEM_DBG_PARMS)
{
	ui_WidgetInitializeEx(UI_WIDGET(button), ui_ButtonTick, ui_ButtonDraw, ui_ButtonFreeInternal, ui_ButtonInput, ui_WidgetDummyFocusFunc MEM_DBG_PARMS_CALL);
	ui_WidgetSetPosition(UI_WIDGET(button), x, y);
	if (text)
		ui_ButtonSetTextAndResize(button, text);
	ui_ButtonSetDownCallback(button, NULL, NULL);
	ui_ButtonSetUpCallback(button, NULL, NULL);
	ui_ButtonSetCallback(button, clickedF, clickedData);
	button->bFocusOnClick = true;
}

void ui_ButtonClearImage(UIButton *pButton)
{
	devassert(pButton);
	pButton->normalImage = NULL;
	pButton->highlightImage = NULL;
	pButton->focusedImage = NULL;
	pButton->pressedImage = NULL;
	pButton->disabledImage = NULL;
}

void ui_ButtonSetImage(UIButton *pButton, const char *pTexture)
{
	devassert(pButton);
	devassert(pTexture);

	ui_ButtonSetImageEx( pButton, pTexture, pTexture, pTexture, pTexture, pTexture );
}

void ui_ButtonSetImageEx(UIButton* pButton, const char* normalTex, const char* highlightTex, const char* focusedTex, const char* pressedTex, const char* disabledTex )
{
	pButton->normalImage = atlasFindTexture( normalTex );
	pButton->highlightImage = atlasFindTexture( highlightTex );
	pButton->focusedImage = atlasFindTexture( focusedTex );
	pButton->pressedImage = atlasFindTexture( pressedTex );
	pButton->disabledImage = atlasFindTexture( disabledTex );
}

void ui_ButtonSetImageStretch(UIButton* pButton, bool value)
{
	pButton->bImageStretch = value;
}

void ui_ButtonSetText(UIButton *button, const char *text)
{
	if (!text)
		text = "";
	ui_WidgetSetTextString(UI_WIDGET(button), text);
}

void ui_ButtonSetMessage(UIButton* button, const char* text)
{
	if (!text)
		text = "";
	ui_WidgetSetTextMessage(UI_WIDGET(button), text);
}

void ui_ButtonSetTextAndResize(UIButton *button, const char *text)
{
	ui_ButtonSetText(button, text);
	ui_ButtonResize(button);
}

void ui_ButtonSetMessageAndResize(UIButton *button, const char *text)
{
	ui_ButtonSetMessage(button, text);
	ui_ButtonResize(button);
}

void ui_ButtonResize(UIButton* button)
{
	UIStyleFont *font = ui_ButtonGetFont( button );
	const char* text = ui_ButtonGetText(button);
	UIDrawingDescription desc = { 0 };
	CBox box;

	ui_ButtonFillDrawingDescription( button, &desc );

	if( !button->normalImage ) {
		// Drawing style 1 -- just text, centered in button
		BuildCBox(&box, 0, 0, ui_StyleFontWidth(font, 1.f, text), ui_StyleFontLineHeight(font, 1.f));
	} else if( button->normalImage && nullStr( ui_WidgetGetText( UI_WIDGET( button )))) {
		// Drawing style 2 -- just an image, centered in button
		BuildCBox(&box, 0, 0, button->normalImage->width, button->normalImage->height);
	} else {
		// Drawing style 3 -- image on the left, text on the right
		float drawHeight;
		float drawWidth;

		if( button->bImageStretch ) {
			drawHeight = ui_StyleFontLineHeight(font, 1.f);
			drawWidth = drawHeight / button->normalImage->height * button->normalImage->width;
		} else {
			drawHeight = button->normalImage->height;
			drawWidth = button->normalImage->width;
		}
		
		BuildCBox(&box, 0, 0, ui_StyleFontWidth(font, 1.f, text) + UI_HSTEP + drawWidth, ui_StyleFontLineHeight(font, 1.f));
	}
	
	ui_DrawingDescriptionOuterBox(&desc, &box, 1.f);
	ui_WidgetSetDimensions(UI_WIDGET(button), CBoxWidth(&box), CBoxHeight(&box));
}

const char* ui_ButtonGetText(SA_PARAM_NN_VALID UIButton *button)
{
	return ui_WidgetGetText(UI_WIDGET(button));
}

void ui_ButtonFreeInternal(UIButton *button)
{
	REMOVE_HANDLE(button->hBorderOverride);
	ui_WidgetFreeInternal(UI_WIDGET(button));
}

static void ButtonCmdParseCallback(UIButton *button, const char *cmd)
{
	globCmdParse(cmd);
}

void ui_ButtonSetCallback(UIButton *button, UIActivationFunc clickedF, UserData clickedData)
{
	if (button->clickedF == ButtonCmdParseCallback)
		SAFE_FREE(button->clickedData);
	button->clickedF = clickedF;
	button->clickedData = clickedData;
	
	// clear any separate down/up callbacks if a click callback is set
	if (clickedF) {
		ui_ButtonSetDownCallback(button, NULL, NULL);
		ui_ButtonSetUpCallback(button, NULL, NULL);
	}
}

void ui_ButtonSetDownCallback(SA_PARAM_NN_VALID UIButton *button, UIActivationFunc downF, UserData downData)
{
	if (button->downF == ButtonCmdParseCallback)
		SAFE_FREE(button->downData);
	button->downF = downF;
	button->downData = downData;
	
	// clear any click callback if a down callback is set
	if (downF) {
		ui_ButtonSetCallback(button, NULL, NULL);
	}
}

void ui_ButtonSetUpCallback(SA_PARAM_NN_VALID UIButton *button, UIActivationFunc upF, UserData upData)
{
	if (button->upF == ButtonCmdParseCallback)
		SAFE_FREE(button->upData);
	button->upF = upF;
	button->upData = upData;
	
	// clear any click callback if an up callback is set
	if (upF) {
		ui_ButtonSetCallback(button, NULL, NULL);
	}
}

void ui_ButtonSetTooltip(UIButton *pButton, const char *pchTooltip)
{
	char achText[1000];
	if (pchTooltip && pButton->clickedF == ButtonCmdParseCallback)
	{
		KeyBind *pBind = keybind_BindForCommand(pButton->clickedData, false, true);
		if (pBind)
		{
			sprintf(achText, "%s (%s)", pchTooltip, pBind->pchKey);
			pchTooltip = achText;
		}
	}
	ui_WidgetSetTooltipString(UI_WIDGET(pButton), pchTooltip);
}

void ui_ButtonSetCommand(UIButton *button, const char *cmd)
{
	ui_ButtonSetCallback(button, ButtonCmdParseCallback, strdup(cmd));
}

void ui_ButtonSetDownCommand(SA_PARAM_NN_VALID UIButton *button, SA_PARAM_NN_STR const char *cmd)
{
	ui_ButtonSetDownCallback(button, ButtonCmdParseCallback, strdup(cmd));
}

void ui_ButtonSetUpCommand(SA_PARAM_NN_VALID UIButton *button, SA_PARAM_NN_STR const char *cmd)
{
	ui_ButtonSetUpCallback(button, ButtonCmdParseCallback, strdup(cmd));
}

void ui_ButtonClick(UIButton *button)
{
	if (ui_IsActive(UI_WIDGET(button)))
	{
		if(button->clickedF)
			button->clickedF(button, button->clickedData);
		if(button->toggle)
		{
			button->down = !button->down;
			if(button->toggledF)
				button->toggledF(button, button->toggledData);
		}
	}
}

void ui_ButtonDown(UIButton *button)
{
	if (ui_IsActive(UI_WIDGET(button)) && button->downF)
		button->downF(button, button->downData);
}

void ui_ButtonUp(UIButton *button)
{
	if (ui_IsActive(UI_WIDGET(button)) && button->upF)
		button->upF(button, button->upData);
}

void ui_ButtonAddChild(UIButton *button, UIWidget *child)
{
	ui_WidgetGroupAdd(&UI_WIDGET(button)->children, child);
}

void ui_ButtonRemoveChild(UIButton *button, UIWidget *child)
{
	ui_WidgetGroupRemove(&UI_WIDGET(button)->children, child);
}

void ui_ButtonSetBorder(UIButton *pButton, const char *pchBorder)
{
	const char* widgetText = ui_WidgetGetText( UI_WIDGET( pButton ));
	
	UI_SET_STYLE_BORDER_NAME(pButton->hBorderOverride, pchBorder);
	if (widgetText)
		ui_ButtonSetTextAndResize(pButton, widgetText);
}

UIButton *ui_ButtonCreateForCommand(const char *pchText, const char *pchCommand)
{
	UIButton *pButton = NULL;
	if (inpHasGamepad())
	{
		KeyBind *pBind = keybind_BindForCommand(pchCommand, true, true);
		if (pBind)
			pButton = ui_ButtonCreateWithIcon(pchText, ui_ControllerButtonToTexture(pBind->iKey1), NULL, NULL);
	}

	if (!pButton)
		pButton = ui_ButtonCreate(pchText, 0, 0, NULL, NULL);
	ui_ButtonSetCommand(pButton, pchCommand);
	return pButton;
}
