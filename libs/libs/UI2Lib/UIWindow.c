/***************************************************************************



***************************************************************************/


#include "earray.h"
#include "Color.h"
#include "StringUtil.h"

#include "GfxClipper.h"
#include "GfxSprite.h"
#include "GfxTexAtlas.h"
#include "GfxSpriteText.h"
#include "GraphicsLib.h"

#include "inputLib.h"
#include "inputMouse.h"
#include "inputKeyBind.h"
#include "inputText.h"

#include "UIDnD.h"
#include "UIFocus.h"
#include "UITextureAssembly.h"
#include "UIWindow.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

KeyBindProfile g_ui_KeyBindModal;

UIWindow** g_eaWindows = NULL;

static void ui_WindowCloseButtonCallback(UIWindow *window, UserData dummy)
{
	ui_WindowClose(window);
}

static void ui_WindowNextDeviceButtonCallback(UIWindow *pWindow, UserData dummy)
{
	RdrDevice *pNext = gfxNextDevice(g_ui_State.device);
	if (pNext)
	{
		ui_WindowRemoveFromGroup(pWindow);
		ui_WindowAddToDevice(pWindow, pNext);
	}
}

static void ui_WindowFillDrawingDescription( UIWindow* pWindow, UIDrawingDescription* desc )
{
	UISkin* skin = UI_GET_SKIN( pWindow );

	if( pWindow->astrStyleOverride ) {
		desc->styleBorderName = pWindow->astrStyleOverride;
	} else if( skin->bUseStyleBorders || skin->bUseTextureAssemblies ) {
		if( skin->bUseTextureAssemblies && RefSystem_ReferentFromString( "UITextureAssembly", skin->astrWindowStyle )) {
			desc->textureAssemblyName = skin->astrWindowStyle;
		} else {
			desc->styleBorderName = skin->astrWindowStyle;
		}
	} else {
		desc->styleBorderNameUsingLegacyColor = "Default_WindowFrame";
	}
}

static void ui_WindowButtonFillDrawingDescription( UIWindow* pWindow, UIDrawingDescription* desc, bool mouseOverBox, bool mouseIsDown )
{
	UISkin* skin = UI_GET_SKIN( pWindow );

	if( skin->bUseStyleBorders || skin->bUseTextureAssemblies ) {
		const char* descName;

		if( mouseOverBox && mouseIsDown ) {
			descName = skin->astrWindowButtonStylePressed;
		} else if( mouseOverBox ) {
			descName = skin->astrWindowButtonStyleHighlight;
		} else {
			descName = skin->astrWindowButtonStyle;
		}
		
		if( skin->bUseTextureAssemblies && RefSystem_ReferentFromString( "UITextureAssembly", descName )) {
			desc->textureAssemblyName = descName;
		} else {
			desc->styleBorderName = descName;
		}
	}
}

static void ui_WindowTitleFillDrawingDescription( UIWindow* pWindow, UIDrawingDescription* desc )
{
	UISkin* skin = UI_GET_SKIN( pWindow );

	if( skin->bUseStyleBorders || skin->bUseTextureAssemblies ) {
		const char* style;
		if( !nullStr( skin->astrWindowTitleStyleFlash ) && fmodf( pWindow->flashSeconds, 0.25f ) > 0.125 ) {
			style = skin->astrWindowTitleStyleFlash;
		} else {
			style = skin->astrWindowTitleStyle;
		}
		
		if( skin->bUseTextureAssemblies && RefSystem_ReferentFromString( "UITextureAssembly", style )) {
			desc->textureAssemblyName = style;
		} else {
			desc->styleBorderName = style;
		}
	} else {
		desc->styleBarName = REF_STRING_FROM_HANDLE( skin->hTitlebarBar );
	}
}

static UIStyleFont* ui_WindowGetTitleFont( UIWindow* window )
{
	UISkin* skin = UI_GET_SKIN( window );
	UIStyleFont* font = NULL;

	// For legacy purposes this is not gated by the skin using style
	// borders or texture assemblies.
	font = GET_REF( skin->hWindowTitleFont );

	if( !font ) {
		font = ui_WidgetGetFont( UI_WIDGET( window ));
	}

	return font;
}

static void ui_WindowTitleFillBox( UIWindow* window, CBox* out_box, UI_MY_ARGS )
{
	UISkin* skin = UI_GET_SKIN( window );
	UIDrawingDescription windowDesc = { 0 };
	UIDrawingDescription titleDesc = { 0 };
	float titleBoxBottom;
	
	ui_WindowFillDrawingDescription( window, &windowDesc );
	ui_WindowTitleFillDrawingDescription( window, &titleDesc );

	titleBoxBottom = y - ui_DrawingDescriptionTopSize( &windowDesc ) * scale;
	
	out_box->lx = x - ui_DrawingDescriptionLeftSize( &windowDesc ) * scale;
	out_box->ly = titleBoxBottom - ui_DrawingDescriptionHeight( &titleDesc ) * scale;
	// MJF Oct/24/2012 -- Legacy support for UIStyleBar.  UIStyleBar
	// ignored the height of the font, and would scale the text to fit
	// in it.
	if( !titleDesc.styleBarName ) {
		out_box->ly -= ui_StyleFontLineHeight( ui_WindowGetTitleFont( window ), scale );
	} else {
		UIStyleBar* styleBar = RefSystem_ReferentFromString( "UIStyleBar", titleDesc.styleBarName );
		if( styleBar ) {
			out_box->ly -= ui_TextureAssemblyHeight( GET_REF( styleBar->hFilled ));
		}
	}
	out_box->hx = x + w + ui_DrawingDescriptionRightSize( &windowDesc ) * scale;
	out_box->hy = titleBoxBottom;
}

static UITitleButton s_CloseButton = {"eui_button_close", ui_WindowCloseButtonCallback, NULL };
static UITitleButton s_NextDeviceButton = {"eui_button_cycle", ui_WindowNextDeviceButtonCallback, NULL };

AtlasTex* ui_WindowGetButtonTex(UIWindow *window, UITitleButton *button)
{
	AtlasTex *tex=NULL;
	if(button == &s_CloseButton) {
		UISkin *skin = UI_GET_SKIN(window);

		if( button->bIsPressed ) {
			tex = UI_TEXTURE(skin->astrWindowCloseButtonPressed);
		} else if( button->bIsHovering ) {
			tex = UI_TEXTURE(skin->astrWindowCloseButtonHighlight);
		} else {
			tex = UI_TEXTURE(skin->astrWindowCloseButton);
		}

		if( !tex ) {
			tex = UI_TEXTURE(skin->astrWindowCloseButton);
		}
	}
	if(!tex)
		tex = atlasLoadTexture(button->textureName);
	return tex;
}

bool ui_WindowInput(UIWindow *window, KeyInput *key)
{
	UIWidget *newFocus = NULL;
	if (key->type == KIT_EditKey)
	{
		if (eaSize(&UI_WIDGET(window)->children) == 0)
			return false;
		else if (key->scancode == INP_TAB)
		{
			if (key->attrib & KIA_SHIFT)
				newFocus = ui_FocusPrevInGroup(&window->widget, g_ui_State.focused);
			else
				newFocus = ui_FocusNextInGroup(&window->widget, g_ui_State.focused);
		}
		else if (key->scancode == INP_UP || key->scancode == INP_LSTICK_UP)
			newFocus = ui_FocusUp(g_ui_State.focused, &UI_WIDGET(window)->children, UI_WIDGET(window)->width, UI_WIDGET(window)->height);
		else if (key->scancode == INP_DOWN || key->scancode == INP_LSTICK_DOWN)
			newFocus = ui_FocusDown(g_ui_State.focused, &UI_WIDGET(window)->children, UI_WIDGET(window)->width, UI_WIDGET(window)->height);
		else if (key->scancode == INP_LEFT || key->scancode == INP_LSTICK_LEFT)
			newFocus = ui_FocusLeft(g_ui_State.focused, &UI_WIDGET(window)->children, UI_WIDGET(window)->width, UI_WIDGET(window)->height);
		else if (key->scancode == INP_RIGHT || key->scancode == INP_LSTICK_RIGHT)
			newFocus = ui_FocusRight(g_ui_State.focused, &UI_WIDGET(window)->children, UI_WIDGET(window)->width, UI_WIDGET(window)->height);
	}

	if (newFocus && newFocus != g_ui_State.focused)
	{
		ui_SetFocus(newFocus);
		return true;
	}
	else
		return false;
}

static F32 ui_WindowGetAbsMinWidth(UIWindow *window)
{
	const char* widgetText = NULL_TO_EMPTY( ui_WidgetGetText( UI_WIDGET( window )));
	F32 minW = 1.f;
	int i;

	if(widgetText)
	{
		UIStyleFont *pFont = ui_WindowGetTitleFont( window );
		minW = MAX(ui_StyleFontWidth(pFont, 1.f, widgetText), minW);
	}

	for(i = 0; i < eaSize(&window->buttons); i++)
	{
		UITitleButton *tbutton = window->buttons[i];
		AtlasTex *texture = ui_WindowGetButtonTex(window, tbutton);
		minW += texture->width * UI_WIDGET(window)->scale;
	}

	return minW;
}

void ui_WindowCheckResizing(UIWindow *window, UI_PARENT_ARGS, UI_MY_ARGS)
{
	const char* widgetText = NULL_TO_EMPTY( ui_WidgetGetText( UI_WIDGET( window )));
	F32 leftWidth, rightWidth, bottomHeight, topHeight;
	F32 titleHeight = (widgetText ? (g_ui_Tex.windowTitleMiddle)->height * scale : 0);
	CBox leftBox, rightBox, bottomBox, blBox, brBox, tlBox, trBox, topBox;
	bool collided = false;
	int mouseX = g_ui_State.mouseX, mouseY = g_ui_State.mouseY;

	{
		UIDrawingDescription desc = { 0 };
		ui_WindowFillDrawingDescription( window, &desc );
		
		leftWidth = ui_DrawingDescriptionLeftSize(&desc) * scale;
		rightWidth = ui_DrawingDescriptionRightSize(&desc) * scale;
		bottomHeight = ui_DrawingDescriptionBottomSize(&desc) * scale;
		topHeight = ui_DrawingDescriptionTopSize(&desc) * scale;
	}
	BuildCBox(&leftBox, x - leftWidth, y, leftWidth, h);
	BuildCBox(&rightBox, x + w, y, rightWidth, h);
	BuildCBox(&bottomBox, x, y + h, w, bottomHeight);
	BuildCBox(&blBox, x - leftWidth, y + h, leftWidth, bottomHeight);
	BuildCBox(&brBox, x + w, y + h, rightWidth, bottomHeight);
	BuildCBox(&tlBox, x - leftWidth, y - topHeight, leftWidth, topHeight);
	BuildCBox(&trBox, x + w, y - topHeight, rightWidth, topHeight);
	BuildCBox(&topBox, x, y - topHeight, w, topHeight);

	if (!window->resizable && h)
	{
		if (mouseDownHit(MS_LEFT, &leftBox) || mouseDownHit(MS_LEFT, &rightBox) || mouseDownHit(MS_LEFT, &topBox) ||
			mouseDownHit(MS_LEFT, &bottomBox) || mouseDownHit(MS_LEFT, &blBox) || mouseDownHit(MS_LEFT, &brBox) ||
			mouseDownHit(MS_LEFT, &tlBox) || mouseDownHit(MS_LEFT, &trBox))
		{
			ui_WidgetGroupSteal(UI_WIDGET(window)->group, UI_WIDGET(window));
			inpHandled();
		}
		else if ((mouseCollision(&leftBox) || mouseCollision(&rightBox) || mouseCollision(&topBox) ||
				  mouseCollision(&bottomBox) || mouseCollision(&blBox) || mouseCollision(&brBox) ||
				  mouseCollision(&tlBox) || mouseCollision(&trBox)))
		{
			inpHandled();
		}
		return;
	}

	if (!window->resizing && h)
	{
		int i;
		bool handled=false;
		struct {
			UIDirection direction;
			CBox *box;
			UIUnitType check[2];
		} collisions[] = {
			{UIBottomLeft, &blBox, {UI_WIDGET(window)->widthUnit, UI_WIDGET(window)->heightUnit}},
			{UIBottomRight, &brBox, {UI_WIDGET(window)->widthUnit, UI_WIDGET(window)->heightUnit}},
			{UITopLeft, &tlBox, {UI_WIDGET(window)->widthUnit, UI_WIDGET(window)->heightUnit}},
			{UITopRight, &trBox, {UI_WIDGET(window)->widthUnit, UI_WIDGET(window)->heightUnit}},
			{UIBottom, &bottomBox, {UI_WIDGET(window)->heightUnit}},
			{UILeft, &leftBox, {UI_WIDGET(window)->widthUnit}},
			{UIRight, &rightBox, {UI_WIDGET(window)->widthUnit}},
			{UITop, &topBox, {UI_WIDGET(window)->heightUnit}},
		};
		for (i=0; i<ARRAY_SIZE(collisions) && !handled; i++) {
			int j;
			bool valid=true;
			for (j=0; j<ARRAY_SIZE(collisions[i].check); j++) {
				if (collisions[i].check[j] != UIUnitFixed)
					valid = false;
			}
			if (valid && mouseDownHit(MS_LEFT, collisions[i].box))
			{
				window->resizing = collisions[i].direction;
				window->grabbedX = (mouseX > (x + w/2)) ? (mouseX - (x + w)) : (mouseX - x);
				window->grabbedY = (mouseY > (y + h/2)) ? (mouseY - (y + h)) : (mouseY - y);
				handled = true;
			}
		}
		for (i=0; i<ARRAY_SIZE(collisions) && !handled; i++) {
			int j;
			bool valid=true;
			for (j=0; j<ARRAY_SIZE(collisions[i].check); j++) {
				if (collisions[i].check[j] != UIUnitFixed)
					valid = false;
			}
			if (valid && mouseCollision(collisions[i].box))
			{
				ui_SetCursorForDirection(collisions[i].direction);
				ui_CursorLock();
				collided = true;
				handled = true;
			}
		}
	}

	if (!mouseIsDown(MS_LEFT)) 
	{
		if(window->resizing != UINoDirection && window->resizedF)
			window->resizedF(window, window->resizedData);
		window->resizing = UINoDirection;
	}

	if (window->resizing != UINoDirection && h)
	{
		F32 minW = MAX(window->minW, ui_WindowGetAbsMinWidth(window));

		F32 maxScaledWidth = (pW - ((x - pX) + rightWidth)),
			minScaledWidth = minW * scale,
			maxScaledHeight = (pH - ((y - pY) + bottomHeight)),
			minScaledHeight = window->minH * scale;
		F32 maxW = pW - (x + rightWidth);
		F32 maxH = (pH / pScale) - y;
		if (UI_WIDGET(window)->offsetFrom != UITopLeft) {
			// Set values to offset from UL
			UI_WIDGET(window)->x = (x - pX) / pScale;
			UI_WIDGET(window)->y = (y - pY) / pScale;
		}
		switch (window->resizing) {
			case UILeft:
				mouseX = CLAMP(mouseX - window->grabbedX, pX + leftWidth, (x + w) - minScaledWidth);
				UI_WIDGET(window)->x = (mouseX - pX) / pScale;
				UI_WIDGET(window)->width = ((x + w) - mouseX) / scale;
				break;
			case UIRight:
				UI_WIDGET(window)->width = CLAMP((mouseX - window->grabbedX) - x, minScaledWidth, maxScaledWidth) / scale;
				break;
			case UIBottom:
				UI_WIDGET(window)->height = CLAMP((mouseY - window->grabbedY) - y, minScaledHeight, maxScaledHeight) / scale;
				break;
			case UITop:
				mouseY = CLAMP(mouseY - window->grabbedY, pY + topHeight, (y + h) - minScaledHeight);
				UI_WIDGET(window)->y = (mouseY - pY) / pScale;
				UI_WIDGET(window)->height = ((y + h) - mouseY) / scale;
				break;
			case UIBottomLeft:
				mouseX = CLAMP(mouseX - window->grabbedX, pX + leftWidth, (x + w) - minScaledWidth);
				UI_WIDGET(window)->x = (mouseX - pX) / pScale;
				UI_WIDGET(window)->width = ((x + w) - mouseX) / scale;
				UI_WIDGET(window)->height = CLAMP((mouseY - window->grabbedY) - y, minScaledHeight, maxScaledHeight) / scale;
				break;
			case UIBottomRight:
				UI_WIDGET(window)->width = CLAMP((mouseX - window->grabbedX) - x, minScaledWidth, maxScaledWidth) / scale;
				UI_WIDGET(window)->height = CLAMP((mouseY - window->grabbedY) - y, minScaledHeight, maxScaledHeight) / scale;
				break;
			case UITopLeft:
				mouseX = CLAMP(mouseX - window->grabbedX, pX + leftWidth, (x + w) - minScaledWidth);
				UI_WIDGET(window)->x = (mouseX - pX) / pScale;
				UI_WIDGET(window)->width = ((x + w) - mouseX) / scale;
				mouseY = CLAMP(mouseY - window->grabbedY, pY + topHeight, (y + h) - minScaledHeight);
				UI_WIDGET(window)->y = (mouseY - pY) / pScale;
				UI_WIDGET(window)->height = ((y + h) - mouseY) / scale;
				break;
			case UITopRight:
				UI_WIDGET(window)->width = CLAMP((mouseX - window->grabbedX) - x, minScaledWidth, maxScaledWidth) / scale;
				mouseY = CLAMP(mouseY - window->grabbedY, pY + topHeight, (y + h) - minScaledHeight);
				UI_WIDGET(window)->y = (mouseY - pY) / pScale;
				UI_WIDGET(window)->height = ((y + h) - mouseY) / scale;
				break;
			default:
				// in case something goes wrong, just reset state.
				window->resizing = UINoDirection;
		}
		// Flip values if we're offset from the bottom or right
		if (UI_WIDGET(window)->offsetFrom == UITopRight ||
			UI_WIDGET(window)->offsetFrom == UIBottomRight ||
			UI_WIDGET(window)->offsetFrom == UIRight)
		{
			UI_WIDGET(window)->x = pW - UI_WIDGET(window)->x - UI_WIDGET(window)->width;
		}
		if (UI_WIDGET(window)->offsetFrom == UIBottomLeft ||
			UI_WIDGET(window)->offsetFrom == UIBottomRight ||
			UI_WIDGET(window)->offsetFrom == UIBottom)
		{
			UI_WIDGET(window)->y = pH - UI_WIDGET(window)->y - UI_WIDGET(window)->height;
		}
		ui_SoftwareCursorThisFrame();
		ui_SetCursorForDirection(window->resizing);
		ui_CursorLock();
		ui_SetFocus(window);
		inpHandled();
	}
	else if ((mouseDownHit(MS_LEFT, &tlBox) || mouseDownHit(MS_LEFT, &trBox) ||	mouseDownHit(MS_LEFT, &topBox)) && h)
	{
		ui_CursorLock();
		ui_WidgetGroupSteal(UI_WIDGET(window)->group, UI_WIDGET(window));
		ui_SetFocus(window);
		inpHandled();
	}
	else if (collided && h)
	{
		ui_CursorLock();
		inpHandled();
	}

	// Sanity check window size.
	if ((w + leftWidth + rightWidth) > pW && UI_WIDGET(window)->widthUnit == UIUnitFixed)
		UI_WIDGET(window)->width = (pW - (leftWidth + rightWidth)) / scale;
	if ((h + topHeight + bottomHeight + titleHeight) > pH && UI_WIDGET(window)->heightUnit == UIUnitFixed)
		UI_WIDGET(window)->height = (pH - (topHeight + bottomHeight + titleHeight)) / scale;
}

void ui_WindowTitleTick(UIWindow *window, UI_PARENT_ARGS, UI_MY_ARGS, F32 fullHeight)
{
	UISkin* skin = UI_GET_SKIN( window );
	const char* widgetText = NULL_TO_EMPTY( ui_WidgetGetText( UI_WIDGET( window )));
	CBox titleBox;
	CBox contentBox;
	UIDrawingDescription windowDesc = { 0 };
	UIDrawingDescription titleDesc = { 0 };
	F32 fRightSide;

	ui_WindowFillDrawingDescription( window, &windowDesc );
	ui_WindowTitleFillDrawingDescription( window, &titleDesc );
	ui_WindowTitleFillBox( window, &titleBox, UI_MY_VALUES );

	contentBox = titleBox;
	ui_DrawingDescriptionInnerBox( &windowDesc, &contentBox, scale );

	{
		int i;
		float fCenterY = (contentBox.ly + contentBox.hy) / 2;
		if( skin->bWindowButtonInsideTitle ) {
			fRightSide = contentBox.hx;
		} else {
			fRightSide = titleBox.hx;
		}
		for (i = eaSize(&window->buttons) - 1; i >= 0; i--)
		{
			UITitleButton *button = window->buttons[i];
			AtlasTex *texture = ui_WindowGetButtonTex(window, button);
			CBox buttonBox;
			UIDrawingDescription buttonDesc = { 0 };

			// Get a drawing description to do sizing
			ui_WindowButtonFillDrawingDescription( window, &buttonDesc, false, false );

			buttonBox.lx = fRightSide - (texture->width + ui_DrawingDescriptionWidth( &buttonDesc )) * scale;
			buttonBox.hx = fRightSide;
			buttonBox.ly = fCenterY - (texture->height / 2 + ui_DrawingDescriptionTopSize( &buttonDesc )) * scale;
			buttonBox.hy = fCenterY + (texture->height / 2 + ui_DrawingDescriptionBottomSize( &buttonDesc )) * scale;

			button->bIsHovering = false;
			button->bIsPressed = false;
			if (mouseClickHit(MS_LEFT, &buttonBox) && button->callback) {
				button->callback(window, button->callbackData);
				inpHandled();
			} else if (mouseCollision(&buttonBox)) {
				button->bIsHovering = true;
				button->bIsPressed = mouseIsDown( MS_LEFT );
				inpHandled();
				ui_CursorLock();
			}

			fRightSide = buttonBox.lx;
		}
	}

	if (mouseDoubleClickHit(MS_LEFT, &titleBox) && window->shadable && !window->modal)
	{
		ui_WindowToggleShadedAndCallback(window);
		inpHandled();
	}
	else if (window->movable && mouseDownHit(MS_LEFT, &titleBox))
	{
		ui_SetFocus(window);
		window->dragging = true;
		window->grabbedX = g_ui_State.mouseX - x;
		window->grabbedY = g_ui_State.mouseY - y;
		ui_SetCursorForDirection(UIAnyDirection);
		inpHandled();
	}
	else if (window->dragging)
	{
		F32 newX = (g_ui_State.mouseX - pX) - (window->grabbedX),
			newY = (g_ui_State.mouseY - pY) - (window->grabbedY);
		if (!mouseIsDown(MS_LEFT))
		{
			if(window->movedF)
				window->movedF(window, window->movedData);
			window->dragging = false;
		}
		else if (window->movable)
		{
			if (UI_WIDGET(window)->offsetFrom == UITopRight ||
				UI_WIDGET(window)->offsetFrom == UIBottomRight ||
				UI_WIDGET(window)->offsetFrom == UIRight)
			{
				UI_WIDGET(window)->x = (pW - newX - w) / pScale;
			} else {
				UI_WIDGET(window)->x = newX / pScale;
			}
			if (UI_WIDGET(window)->offsetFrom == UIBottomLeft ||
				UI_WIDGET(window)->offsetFrom == UIBottomRight ||
				UI_WIDGET(window)->offsetFrom == UIBottom)
			{
				// Must use fullHeight here in case of window->shaded)
				UI_WIDGET(window)->y = (pH - newY - fullHeight) / pScale;
			} else {
				UI_WIDGET(window)->y = newY / pScale;
			}
			ui_SoftwareCursorThisFrame();
		}
		ui_SetCursorForDirection(UIAnyDirection);
		inpHandled();
	}
	else if (UI_WIDGET(window)->contextF && mouseClickHit(MS_RIGHT, &titleBox))
	{
		UI_WIDGET(window)->contextF(UI_WIDGET(window), UI_WIDGET(window)->contextData);
		inpHandled();
	}
	else if (mouseCollision(&titleBox))
	{
		if (window->movable)
			ui_SetCursorForDirection(UIAnyDirection);
		inpHandled();
	}

	// Sanity check the window position.
	if (window->resizable)
	{
		float leftPad = ui_DrawingDescriptionLeftSize( &windowDesc );
		float rightPad = ui_DrawingDescriptionRightSize( &windowDesc );
		float topPad = ui_DrawingDescriptionTopSize( &windowDesc ) + CBoxHeight( &titleBox );
		float bottomPad = ui_DrawingDescriptionBottomSize( &windowDesc );
		
		// Resizable windows stay so that the title bar is in bounds vertically
		// and so at least some margin is in bounds to the left and right
		if (UI_WIDGET(window)->offsetFrom == UITopRight ||
			UI_WIDGET(window)->offsetFrom == UIBottomRight ||
			UI_WIDGET(window)->offsetFrom == UIRight)
		{
			UI_WIDGET(window)->x = CLAMP(UI_WIDGET(window)->x, (MAX(3*UI_STEP,rightPad) - w) / pScale, (pW - MAX(3*UI_STEP,leftPad)) / pScale);
		} else {
			UI_WIDGET(window)->x = CLAMP(UI_WIDGET(window)->x, (MAX(3*UI_STEP,leftPad) - w) / pScale, (pW - MAX(3*UI_STEP,rightPad)) / pScale);
		}
		if (UI_WIDGET(window)->offsetFrom == UIBottomLeft ||
			UI_WIDGET(window)->offsetFrom == UIBottomRight ||
			UI_WIDGET(window)->offsetFrom == UIBottom)
		{
			UI_WIDGET(window)->y = CLAMP(UI_WIDGET(window)->y, bottomPad / pScale, (pH - (h + topPad)) / pScale);
		} else {
			UI_WIDGET(window)->y = CLAMP(UI_WIDGET(window)->y, topPad / pScale, pH / pScale);
		}
	}
	else
	{
		float topPad = ui_DrawingDescriptionTopSize( &windowDesc ) + CBoxHeight( &titleBox );
		float bottomPad = ui_DrawingDescriptionBottomSize( &windowDesc );
		
		// Non-resizable windows with a title stay entirely without the bounds of the parent
		UI_WIDGET(window)->x = CLAMP(UI_WIDGET(window)->x, 0.f, (pW - w) / pScale);
		UI_WIDGET(window)->y = CLAMP(UI_WIDGET(window)->y, topPad, (pH - h) / pScale);			
	}
}

void ui_WindowTick(UIWindow *window, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(window);
	UI_WINDOW_SANITY_CHECK(window);
	bool bTopModal = false;
	UIDrawingDescription desc = { 0 };
	ui_WindowFillDrawingDescription( window, &desc );
	
	// MJF Jan/7/2012 - Need to rebuild the box since
	// UI_WINDOW_SANITY_CHECK may have adjusted width and height
	BuildCBox(&box, x, y, w, h);

	if (!window->show) {
		// Tick the children still, but put them all offscreen
		ui_WidgetGroupTick(&UI_WIDGET(window)->children, -100, -100, 50, 50, 1 );
		return;
	}
	
	PERFINFO_AUTO_START_FUNC();

	window->flashSeconds = MAX( 0, window->flashSeconds - g_ui_State.timestep);

	if( window->bDimensionsIncludesNonclient ) {
		ui_DrawingDescriptionInnerBox( &desc, &box, scale );
		x = box.lx;
		y = box.ly;
		w = box.hx - box.lx;
		h = box.hy - box.ly;
	}

	ui_WidgetSizerLayout(UI_WIDGET(window), w, h);

	CBoxClipTo(&pBox, &box);
	mouseClipPushRestrict(&box);

	devassertmsg(w < 100 * g_ui_State.screenWidth, "Window width is out of control!");
	devassertmsg(h < 100 * g_ui_State.screenHeight, "Window height is out of control!");

	if (window->modal)
	{
		if (window->widget.group &&
			UI_WIDGET(window) == eaGet(window->widget.group, 0))
		{
			bTopModal = true;
		}
		
		if (inpEdgePeek(INP_ESCAPE) && !inpIsCaptured(INP_ESCAPE)
			&& ((ui_IsFocusedOrChildren(window) && !g_ui_State.focused->bConsumesEscape) || ui_IsFocused(NULL))
			&& eaFind(&window->buttons, &s_CloseButton) >= 0)
		{
			ui_WindowClose(window);
			inpCapture(INP_ESCAPE);
		}

		// increment alphaCur
		if( bTopModal )
		{
			F32 delta = g_ui_State.timestep * 3;
			if( window->modalAlphaCur < 1 ) {
				window->modalAlphaCur = MIN( window->modalAlphaCur + delta, 1 );
			} else {
				window->modalAlphaCur = MAX( window->modalAlphaCur - delta, 1 );
			}
		}
		else
		{
			window->modalAlphaCur = 0;
		}
	}
	else
	{
		window->modalAlphaCur = 0;
	}

	if (window->shaded)
	{
		mouseClipPop();
		ui_WindowTitleTick(window, UI_PARENT_VALUES, x, y, w, 0, scale, h);
		ui_WindowCheckResizing(window, UI_PARENT_VALUES, x, y, w, 0, scale);
		if (bTopModal)
		{
			ui_CursorLock();
			inpHandled();
			inpScrollHandled();
		}
		PERFINFO_AUTO_STOP();
		return;
	}
	else if (mouseDownHit(MS_LEFT, &box) && UI_WIDGET(window)->group && (UIWindow *)(*UI_WIDGET(window)->group)[0] != window)
	{
		ui_WidgetGroupSteal(UI_WIDGET(window)->group, UI_WIDGET(window));
		if (window->raisedF)
			window->raisedF(window, window->raisedData);
	}

	mouseClipPop();

	ui_WindowTitleTick(window, UI_PARENT_VALUES, UI_MY_VALUES, h);
	ui_WindowCheckResizing(window, UI_PARENT_VALUES, UI_MY_VALUES);

	mouseClipPushRestrict(&box);

	ui_WidgetGroupTick(&UI_WIDGET(window)->children, UI_MY_VALUES);
	ui_WidgetCheckStartDrag(UI_WIDGET(window), &box);
	ui_WidgetCheckDrop(UI_WIDGET(window), &box);

	if (mouseDownHit(MS_LEFT, &box) && window->clickedF)
		window->clickedF(window, window->clickedData);

	if (UI_WIDGET(window)->contextF && mouseClickHit(MS_RIGHT, &box))
	{
		UI_WIDGET(window)->contextF(UI_WIDGET(window), UI_WIDGET(window)->contextData);
		inpHandled();
		inpScrollHandled();
	}

	if (mouseCollision(&box))
	{
		ui_CursorLock();
		inpHandled();
		inpScrollHandled();
	}

	mouseClipPop();

	if (bTopModal)
	{
		ui_CursorLock();
		if( mouseDownAnyButton() ) {
			window->flashSeconds = 2;
		}
		inpHandled();
		inpScrollHandled();
	}
	
	PERFINFO_AUTO_STOP();
}

void ui_WindowTitleDraw(UIWindow *window, UI_MY_ARGS, F32 z)
{
	UISkin* skin = UI_GET_SKIN( window );
	const char* widgetText = NULL_TO_EMPTY( ui_WidgetGetText( UI_WIDGET( window )));
	CBox titleBox;
	CBox contentBox;
	UIDrawingDescription windowDesc = { 0 };
	UIDrawingDescription titleDesc = { 0 };
	F32 fRightSide;
	
	ui_WindowFillDrawingDescription( window, &windowDesc );
	ui_WindowTitleFillDrawingDescription( window, &titleDesc );
	ui_WindowTitleFillBox( window, &titleBox, UI_MY_VALUES );

	ui_DrawingDescriptionDraw( &titleDesc, &titleBox, scale, z, 255, ColorWhite, ColorBlack );
	clipperPushRestrict( &titleBox );

	contentBox = titleBox;
	ui_DrawingDescriptionInnerBox( &titleDesc, &contentBox, scale );

	// Draw buttons
	{
		int i;
		float fCenterY = (contentBox.ly + contentBox.hy) / 2;
		if( skin->bWindowButtonInsideTitle ) {
			fRightSide = contentBox.hx;
		} else {
			fRightSide = titleBox.hx;
		}
		for (i = eaSize(&window->buttons) - 1; i >= 0; i--)
		{
			UITitleButton *tbutton = window->buttons[i];
			AtlasTex *texture = ui_WindowGetButtonTex(window, tbutton);
			CBox buttonBox;
			CBox iconBox;
			UIDrawingDescription buttonDesc = { 0 };

			// Get a drawing description to do sizing
			ui_WindowButtonFillDrawingDescription( window, &buttonDesc, false, false );

			buttonBox.lx = fRightSide - (texture->width + ui_DrawingDescriptionWidth( &buttonDesc )) * scale;
			buttonBox.hx = fRightSide;
			buttonBox.ly = fCenterY - (texture->height / 2 + ui_DrawingDescriptionTopSize( &buttonDesc )) * scale;
			buttonBox.hy = fCenterY + (texture->height / 2 + ui_DrawingDescriptionBottomSize( &buttonDesc )) * scale;
			iconBox = buttonBox;
			ui_DrawingDescriptionInnerBox( &buttonDesc, &iconBox, scale );

			// Now get the actual drawing description for drawing
			ui_WindowButtonFillDrawingDescription( window, &buttonDesc, tbutton->bIsHovering, tbutton->bIsPressed );
			ui_DrawingDescriptionDraw( &buttonDesc, &buttonBox, scale, z + 0.2, 255, ColorWhite, ColorWhite );
			display_sprite_box( texture, &iconBox, z + 0.2, -1 );

			fRightSide = buttonBox.lx; 
		}
	}


	// Draw title
	{
		UIStyleFont* font = ui_WindowGetTitleFont( window );
		CBox textBox;
		textBox = contentBox;
		textBox.hx = MIN( textBox.hx, fRightSide );
		ui_StyleFontUse( font, false, kWidgetModifier_None );
		ui_DrawTextInBoxSingleLine( font, NULL_TO_EMPTY( widgetText ), true, &textBox, z + 0.1, scale, skin->eWindowTitleTextAlignment );
	}

	clipperPop();
}

void ui_WindowDraw(UIWindow *window, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(window);
	UI_WINDOW_SANITY_CHECK(window);
	const char* widgetText = NULL_TO_EMPTY( ui_WidgetGetText( UI_WIDGET( window )));
	bool use_color=true;
	UIDrawingDescription desc = { 0 };
	F32 bottom_y = h + y;
	F32 right_x = w + x;
	int mouseX = g_ui_State.mouseX, mouseY = g_ui_State.mouseY;
	
	// MJF Jan/7/2012 - Need to rebuild the box since
	// UI_WINDOW_SANITY_CHECK may have adjusted width and height
	BuildCBox(&box, x, y, w, h);

	ui_WindowFillDrawingDescription( window, &desc );

	if (!window->show)
		return;

	if (window->shaded)
	{
		ui_WindowTitleDraw(window, UI_MY_VALUES, z);
		return;
	}

	if( window->bDimensionsIncludesNonclient ) {
		ui_DrawingDescriptionInnerBox( &desc, &box, scale );
		x = box.lx;
		y = box.ly;
		w = box.hx - box.lx;
		h = box.hy - box.ly;
	}

	if (window->modal && window->modalAlphaCur > 0)
	{
		int width, height;
		U32 rgba = window->iOverrideModalBackgroundColor ? window->iOverrideModalBackgroundColor : UI_GET_SKIN( window )->iWindowModalBackgroundColor;
		U8 alpha = CLAMP( (int)(GET_COLOR_ALPHA(rgba) * window->modalAlphaCur), 0, 255 );
		rgba = COLOR_ALPHA(rgba, alpha);
		
		gfxGetActiveSurfaceSize(&width, &height);
			
		display_sprite(white_tex_atlas, 0, 0, z-0.001, width/(F32)white_tex_atlas->width, height/(F32)white_tex_atlas->height, rgba);
	}

	{
		CBox drawBox = box;
		ui_DrawingDescriptionOuterBox( &desc, &drawBox, scale );
		ui_DrawingDescriptionDraw( &desc, &drawBox, scale, z, 255, UI_GET_SKIN(window)->background[0], ColorBlack );
	}

	ui_WindowTitleDraw(window, UI_MY_VALUES, z);

	clipperPushRestrict(&box);
	ui_WidgetGroupDraw(&UI_WIDGET(window)->children, UI_MY_VALUES);
	clipperPop();
}

UIWindow *ui_WindowCreateEx(const char *title, F32 x, F32 y, F32 w, F32 h MEM_DBG_PARMS)
{
	UIWindow *window = (UIWindow *)calloc(1, sizeof(UIWindow));
	ui_WindowInitializeEx(window, title, x, y, w, h MEM_DBG_PARMS_CALL);

	eaPush( &g_eaWindows, window );
	return window;
}

void ui_WindowInitializeEx(UIWindow *window, const char *title, F32 x, F32 y, F32 w, F32 h MEM_DBG_PARMS)
{
	ui_WidgetInitializeEx(UI_WIDGET(window), ui_WindowTick, ui_WindowDraw, ui_WindowFreeInternal, ui_WindowInput, NULL MEM_DBG_PARMS_CALL);
	ui_WidgetSetPosition(UI_WIDGET(window), x, y);
	ui_WidgetSetDimensions(UI_WIDGET(window), w, h);
	ui_WindowSetResizable(window, true);
	ui_WindowSetMovable(window, title ? true : false);
	ui_WindowSetClosable(window, true);
	ui_WindowSetShadable(window, true);
	window->minH = 1.f;
	window->show = true;
	ui_WidgetSetTextString(UI_WIDGET(window), title);

	window->minW = ui_WindowGetAbsMinWidth(window);
}

void ui_WindowFreeInternal(UIWindow *window)
{
	eaFindAndRemove( &g_eaWindows, window );
	
	if (window->modal)
		keybind_PopProfileEx(&g_ui_KeyBindModal, InputBindPriorityBlock);
	eaDestroy(&window->buttons);
	ui_WidgetFreeInternal(UI_WIDGET(window));
}

void ui_WindowAddChild(UIWindow *window, UIAnyWidget *child)
{
	ui_WidgetGroupAdd(&UI_WIDGET(window)->children, (UIWidget *)child);
}

void ui_WindowRemoveChild(UIWindow *window, UIAnyWidget *child)
{
	ui_WidgetGroupRemove(&UI_WIDGET(window)->children, (UIWidget *)child);
}

UITitleButton *ui_WindowTitleButtonCreate(const char *texture, UIActivationFunc clickedF, UserData clickedData)
{
	UITitleButton *button = calloc(1, sizeof(UITitleButton));
	button->callback = clickedF;
	button->callbackData = clickedData;
	button->textureName = texture;
	return button;
}

void ui_WindowAddTitleButton(UIWindow *window, UITitleButton *button)
{
	eaInsert(&window->buttons, button, 0);
}

void ui_WindowRemoveTitleButton(UIWindow *window, UITitleButton *button)
{
	eaFindAndRemove(&window->buttons, button);
}

void ui_WindowSetModal(UIWindow *window, bool modal)
{
	window->modal = modal;
	if( modal ) {
		window->widget.priority = UI_HIGHEST_PRIORITY;
	} else {
		window->widget.priority = 0;
	}
}

void ui_WindowSetResizable(UIWindow *window, bool resizable)
{
	window->resizable = resizable;
}

void ui_WindowSetMovable(UIWindow *window, bool movable)
{
	window->movable = movable;
}

void ui_WindowSetClosable(UIWindow *window, bool closable)
{
	if (closable && eaFind(&window->buttons, &s_CloseButton) == -1)
		eaPush(&window->buttons, &s_CloseButton);
	else if (!closable)
		eaFindAndRemove(&window->buttons, &s_CloseButton);
}

void ui_WindowSetShadable(SA_PARAM_NN_VALID UIWindow *window, bool shadable)
{
	window->shadable = shadable;
}

void ui_WindowSetDimensions(UIWindow *window, F32 w, F32 h, F32 minW, F32 minH)
{
	window->minH = MAX(minH, 1.f);
	window->minW = MAX(minW, ui_WindowGetAbsMinWidth(window));

	UI_WIDGET(window)->width = max(window->minW, w);
	UI_WIDGET(window)->height = max(window->minH, h);
}

void ui_WindowAutoSetDimensions(UIWindow *window)
{
	int i;
	F32 w = 5.0f;
	F32 h = 5.0f;
	UIWidgetGroup children = UI_WIDGET(window)->children;
	for ( i=0; i < eaSize(&children); i++ )
	{
		UIWidget *child = children[i];
		h = MAX(h, ui_WidgetGetNextY(child));
		w = MAX(w, ui_WidgetGetNextX(child));
	}
	ui_WindowSetDimensions(window, w, h, w, h);
}

void ui_WindowSetCloseCallback(UIWindow *window, UICloseFunc closeF, UserData closeData)
{
	window->closeF = closeF;
	window->closeData = closeData;
}

void ui_WindowSetClickedCallback(UIWindow *window, UIActivationFunc clickedF, UserData clickedData)
{
	window->clickedF = clickedF;
	window->clickedData = clickedData;
}

void ui_WindowSetRaisedCallback(UIWindow *window, UIActivationFunc raisedF, UserData raisedData)
{
	window->raisedF = raisedF;
	window->raisedData = raisedData;
}

void ui_WindowRemoveFromGroup(UIWindow *window)
{
	if (window->modal)
		keybind_PopProfileEx(&g_ui_KeyBindModal, InputBindPriorityBlock);
	ui_WidgetRemoveFromGroup(UI_WIDGET(window));
}

void ui_WindowShow(UIWindow *window)
{
	ui_WindowShowEx(window, false);
}

void ui_WindowShowEx(UIWindow *window, bool forceWindowGroup)
{
	if( forceWindowGroup || window->modal ) {
		ui_WindowAddToDevice(window, NULL);
	} else {
		ui_WidgetAddToDevice(UI_WIDGET(window), NULL);
	}
	window->show = true;
	if(window->modal)
	{
		ui_SetFocus(UI_WIDGET(window));
	}
}

void ui_WindowHide(UIWindow *window)
{
	ui_WindowRemoveFromGroup(window);
	if (ui_IsFocusedOrChildren(window))
		ui_SetFocus(NULL);
}

void ui_WindowPresent(UIWindow *window)
{
	ui_WindowPresentEx(window, false);
}

void ui_WindowPresentEx(UIWindow *window, bool forceWindowGroup)
{
	ui_WindowShowEx(window, forceWindowGroup);
	ui_WidgetGroupSteal(UI_WIDGET(window)->group, UI_WIDGET(window));
}

bool ui_WindowIsVisible(UIWindow *window)
{
	return (UI_WIDGET(window)->group != NULL) && window->show;
}

void ui_WindowSetTitle(UIWindow *window, const char *title)
{
	ui_WidgetSetTextString(UI_WIDGET(window), title);
}

void ui_WindowClose(UIWindow *window)
{
	if (!window->closeF || window->closeF(window, window->closeData))
		ui_WindowHide(window);
}

void ui_WindowSetShadedCallback(UIWindow *window, UIActivationFunc shadedF, UserData shadedData)
{
	window->shadedF = shadedF;
	window->shadedData = shadedData;
}

void ui_WindowSetMovedCallback(UIWindow *window, UIActivationFunc movedF, UserData movedData)
{
	window->movedF = movedF;
	window->movedData = movedData;
}

void ui_WindowSetResizedCallback(UIWindow *window, UIActivationFunc resizedF, UserData resizedData)
{
	window->resizedF = resizedF;
	window->resizedData = resizedData;
}

void ui_WindowToggleShadedAndCallback(UIWindow *window)
{
	window->shaded = !window->shaded;
	if (window->shadedF)
		window->shadedF(window, window->shadedData);
}

bool ui_WindowFreeOnClose(UIWindow *window, UserData dummy)
{
	ui_WidgetForceQueueFree(UI_WIDGET(window));
	return true;
}

bool ui_WindowSetShowToFalseOnClose(UIWindow *window, UserData ignored)
{
	window->show = false;
	return false;
}

void ui_WindowSetCycleBetweenDisplays(UIWindow *pWindow, bool bCyclable)
{
	if (bCyclable)
		ui_WindowAddTitleButton(pWindow, &s_NextDeviceButton);
	else
		ui_WindowRemoveTitleButton(pWindow, &s_NextDeviceButton);
}

F32 ui_WindowGetModalAlpha(SA_PARAM_NN_VALID UIWindow *window)
{
	return window->modalAlphaCur;
}

/// This utility function has to exist because a window's "box" is
/// actualy just for its client area.  This function figures out the
/// client + title area and places that relative to BOX.
static void ui_WindowPlaceAtBox( UIWindow* window, const CBox* box )
{
	CBox windowClientBox = { 0 };
	CBox windowDrawBox = { 0 };
	CBox titleBox = { 0 };
	CBox windowAndTitleBox = { 0 };
	float xOffset;
	float yOffset;
	
	windowClientBox.hx = ui_WidgetGetWidth( UI_WIDGET( window ));
	windowClientBox.hy = ui_WidgetGetHeight( UI_WIDGET( window ));
	ui_WindowTitleFillBox( window, &titleBox, windowClientBox.lx, windowClientBox.ly,
						   windowClientBox.hx - windowClientBox.lx, windowClientBox.hy - windowClientBox.ly,
						   window->widget.scale );
	{
		UIDrawingDescription desc = { 0 };
		ui_WindowFillDrawingDescription( window, &desc );
		windowDrawBox = windowClientBox;
		ui_DrawingDescriptionOuterBox( &desc, &windowDrawBox, window->widget.scale );
	}
	
	windowAndTitleBox.lx = MIN( windowDrawBox.lx, titleBox.lx );
	windowAndTitleBox.ly = MIN( windowDrawBox.ly, titleBox.ly );
	windowAndTitleBox.hx = MAX( windowDrawBox.hx, titleBox.hx );
	windowAndTitleBox.hy = MAX( windowDrawBox.hy, titleBox.hy );
	xOffset = windowClientBox.lx - windowAndTitleBox.lx;
	yOffset = windowClientBox.ly - windowAndTitleBox.ly;

	ui_PlaceBoxNextToBox( &windowAndTitleBox, box );
	ui_WidgetSetPosition( UI_WIDGET( window ), windowAndTitleBox.lx + xOffset, windowAndTitleBox.ly + yOffset );
}

void ui_WindowPlaceAtCursorOrWidgetBox( UIWindow* window )
{
	if( g_ui_State.widgetBox ) {
		ui_WindowPlaceAtBox( window, g_ui_State.widgetBox );
	} else {
		CBox mouseBox = { g_ui_State.mouseX, g_ui_State.mouseY, g_ui_State.mouseX, g_ui_State.mouseY };
		ui_WindowPlaceAtBox( window, &mouseBox );
	}
}

/// Close all open windows
void ui_WindowCloseAll( void )
{
	FOR_EACH_IN_EARRAY( g_eaWindows, UIWindow, window ) {
		ui_WindowClose( window );
	} FOR_EACH_END;
}
