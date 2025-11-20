/***************************************************************************



***************************************************************************/

#include "earray.h"
#include "gclCommandParse.h"

#include "GfxClipper.h"
#include "GfxSprite.h"
#include "GfxSpriteText.h"
#include "GfxTexAtlas.h"
#include "GfxPrimitive.h"

#include "inputMouse.h"
#include "inputText.h"
#include "Message.h"

#include "UIInternal.h"
#include "UIMenu.h"
#include "UISprite.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

void MenuItemCmdParseCallback(UIMenuItem *button, const char *cmd)
{
	globCmdParse(cmd);
}

static UIStyleFont* ui_MenuGetFont( UIMenu* menu )
{
	UISkin* skin = UI_GET_SKIN( menu );
	UIStyleFont* font = NULL;

	if( skin->bUseStyleBorders || skin->bUseTextureAssemblies ) {
		if( menu->opened ) {
			font = GET_REF( skin->hMenuBarButtonFontOpened );
		} else if( ui_IsHovering( UI_WIDGET( menu ))) {
			font = GET_REF( skin->hMenuBarButtonFontHighlight );
		} else {
			font = GET_REF( skin->hMenuBarButtonFont );
		}
	}

	if( !font ) {
		font = ui_WidgetGetFont( UI_WIDGET( menu ));
	}

	return font;
}

static UIStyleFont* ui_MenuItemGetFont( UIMenu* menu, bool bSelected, bool bActive )
{
	UISkin* skin = UI_GET_SKIN( menu );
	UIStyleFont* font = NULL;

	if( skin->bUseStyleBorders || skin->bUseTextureAssemblies ) {
		if( !bActive ) {
			font = GET_REF( skin->hMenuPopupFontDisabled );
		} else if( bSelected ) {
			font = GET_REF( skin->hMenuPopupFontHighlight );
		} else {
			font = GET_REF( skin->hMenuPopupFont );
		}
	}

	if( !font ) {
		font = ui_WidgetGetFont( UI_WIDGET( menu ));
	}

	return font;
}

//in the UIActivationFunc(UIWidget widget, void* userData), data is userData and  
//clickedData is in ((UIMenuItem*) widget)->data.voidPtr.
UIMenuItem *ui_MenuItemCreate(const char *text, UIMenuItemType type, UIActivationFunc callback, UserData clickedData, void *data)
{
	UIMenuItem *item = (UIMenuItem *)calloc(1, sizeof(UIMenuItem));
	ui_MenuItemSetTextString( item, text );
	item->type = type;
	item->clickedF = callback;
	item->clickedData = clickedData;
	item->active = true;
	switch (type)
	{
	case UIMenuCallback:
		item->data.voidPtr = data;
		break;
	case UIMenuCommand:
		item->clickedF = MenuItemCmdParseCallback;
		item->clickedData = strdup((char *)clickedData);
		break;
	case UIMenuCheckRefButton:
		item->data.statePtr = data;
		break;
	case UIMenuCheckButton:
		item->data.state = !!(intptr_t)data;
		break;
	case UIMenuSubmenu:
		item->data.menu = (UIMenu *)data;
		break;
	}
	return item;
}

UIMenuItem *ui_MenuItemCreateMessage(const char *text, UIMenuItemType type, UIActivationFunc callback, UserData clickedData, void *data)
{
	UIMenuItem* item = ui_MenuItemCreate( "", type, callback, clickedData, data );
	ui_MenuItemSetTextMessage( item, text );
	return item;
}

SA_RET_NN_VALID UIMenuItem *ui_MenuItemCreate2(SA_PARAM_NN_STR const char *text, SA_PARAM_OP_STR const char* rtext, UIMenuItemType type,
										   UIActivationFunc callback, UserData clickedData, void *data)
{
	UIMenuItem* accum = ui_MenuItemCreate( text, type, callback, clickedData, data );
	if( rtext ) {
		ui_MenuItemSetRightText( accum, rtext );
	}

	return accum;
}

void ui_MenuItemFree(UIMenuItem *item)
{
	SAFE_FREE(item->text_USEACCESSOR);
	REMOVE_HANDLE( item->message_USEACCESSOR );
	SAFE_FREE(item->rightText);
	if (item->type == UIMenuCommand)
		free(item->clickedData);
	else if (item->type == UIMenuSubmenu && item->data.menu)
		ui_MenuFreeInternal(item->data.menu);
	free(item);
}

void ui_MenuItemSetTextString(UIMenuItem *item, const char* text )
{
	REMOVE_HANDLE( item->message_USEACCESSOR );
	if( !text ) {
		SAFE_FREE( item->text_USEACCESSOR );
	} else {
		SAFE_FREE( item->text_USEACCESSOR );
		item->text_USEACCESSOR = strdup( text );
	}
}

void ui_MenuItemSetTextMessage(UIMenuItem *item, const char* text )
{
	SAFE_FREE( item->text_USEACCESSOR );
	if( !text ) {
		REMOVE_HANDLE( item->message_USEACCESSOR );
	} else {
		SET_HANDLE_FROM_STRING( "Message", text, item->message_USEACCESSOR );
	}
}

const char *ui_MenuItemGetTextMessage(UIMenuItem *item)
{
	if( IS_HANDLE_ACTIVE( item->message_USEACCESSOR )) {
		return GET_REF(item->message_USEACCESSOR)->pcMessageKey;
	} else {
		return item->text_USEACCESSOR;
	}
}

const char *ui_MenuItemGetText(const UIMenuItem *item)
{
	if( IS_HANDLE_ACTIVE( item->message_USEACCESSOR )) {
		return TranslateMessageRef( item->message_USEACCESSOR );
	} else {
		return item->text_USEACCESSOR;
	}
}

void ui_MenuItemSetRightText(UIMenuItem *item, const char *rightText)
{
	item->rightText = rightText ? strdup(rightText) : NULL;
}

void ui_MenuItemSetCheckState(UIMenuItem *item, bool state)
{
	assertmsg(item->type == UIMenuCheckButton, "This function only supported for checkbutton items");
	item->data.state = state;
}

bool ui_MenuItemGetCheckState(UIMenuItem *item)
{
	assertmsg(item->type == UIMenuCheckButton, "This function only supported for checkbutton items");
	return item->data.state;
}

/*
 * Keep ui_MenuPopup a static function because menus should always have their positions calculated correctly by the caller. There are only a few caller use cases:
 *
 * 1) Top-level menus in the menu bar - position is always straight down from menu bar menu, but constrained to edges of screen
 * 2) Menu Buttons - use cursor position and go left or right, depending on where there is screen room.
 * 3) Context menus - use cursor position and go left or right, depending on where there is screen room.
 * 4) Sub-menus - use parent menu item position and go left or right, depending on where there is screen room.
 *
 * In all cases, screen dimensions are accounted for.
 */
static void ui_MenuPopup(UIMenu *menu, F32 x, F32 y)
{
	F32 openY = g_ui_State.viewportMax[1] - (menu->itemHeight * eaSize(&menu->items) * menu->widget.scale * g_ui_State.scale);
	F32 openX = g_ui_State.screenWidth - ((menu->itemWidth + (openY < 0 ? ui_ScrollbarWidth(UI_WIDGET(menu)->sb) : 0)) * menu->widget.scale * g_ui_State.scale);
	bool was_open = menu->opened;
	menu->opened = true;
	menu->submenu = NULL;
	menu->openedX = CLAMP(openX, 0, x);
	menu->openedY = CLAMP(openY, 0, y);
	menu->widget.sb->xpos = 0;
	menu->widget.sb->ypos = 0;

	if (!was_open)
	{
		// Call preopen function if one is registered
		if (menu->preopenF)
			menu->preopenF(menu, menu->preopenData);
		ui_MenuCalculateWidth(menu);
	}
}

void ui_MenuPopupAtBox(UIMenu *menu, SA_PARAM_NN_VALID CBox* box)
{
	menu->submenu = NULL;
	menu->widget.sb->xpos = 0;
	menu->widget.sb->ypos = 0;

	if(!menu->opened)
	{
		menu->opened = true;

		// Call preopen function if one is registered
		if (menu->preopenF)
			menu->preopenF(menu, menu->preopenData);

		ui_MenuCalculateWidth(menu);
	}

	{
		CBox toPlace = { 0, 0 };
		float menuHeight = menu->itemHeight * eaSize(&menu->items) * menu->widget.scale * g_ui_State.scale;
		toPlace.hx = menu->itemWidth * menu->widget.scale * g_ui_State.scale;
		toPlace.hy = menuHeight;

		if( menuHeight > g_ui_State.viewportMax[ 1 ]) {
			toPlace.hx += ui_ScrollbarWidth(UI_WIDGET(menu)->sb) * menu->widget.scale * g_ui_State.scale;
		}

		ui_PlaceBoxNextToBox( &toPlace, box );		
		ui_MenuPopup(menu, toPlace.lx, toPlace.ly);
	}

	menu->type = UIMenuTransient;
	menu->widget.priority = UI_HIGHEST_PRIORITY;	
	ui_TopWidgetAddToDevice(&menu->widget, NULL);
	ui_WidgetGroupSteal(menu->widget.group, UI_WIDGET(menu));
}

void ui_MenuPopupAtCursor(UIMenu *menu)
{
	CBox mouseBox = { g_ui_State.mouseX, g_ui_State.mouseY, g_ui_State.mouseX, g_ui_State.mouseY };
	ui_MenuPopupAtBox( menu, &mouseBox );
}

void ui_MenuPopupAtCursorOrWidgetBox(UIMenu* menu)
{
	if( g_ui_State.widgetBox ) {
		ui_MenuPopupAtBox( menu, g_ui_State.widgetBox );
	} else {
		ui_MenuPopupAtCursor( menu );
	}
}

void ui_MenuClose(UIMenu* menu)
{
	menu->opened = false;
	if (menu->type == UIMenuTransient)
		ui_WidgetRemoveFromGroup(UI_WIDGET(menu));
}

static void ui_PopupMenuItemFillDrawingDescription( UIMenu* menu, UIDrawingDescription* desc )
{
	UISkin* skin = UI_GET_SKIN( menu );
		
	if( skin->bUseStyleBorders || skin->bUseTextureAssemblies ) {
		const char* descName = skin->astrMenuPopupStyleHighlight;
		if( skin->bUseTextureAssemblies && RefSystem_ReferentFromString( "UITextureAssembly", descName )) {
			desc->textureAssemblyName = descName;
		} else {
			desc->styleBorderName = descName;
		}
	} else {
		desc->textureNameUsingLegacyColor = "white";
	}
}

static void ui_PopupMenuSeparatorFillDrawingDescription( UIMenu* menu, UIDrawingDescription* desc )
{
	UISkin* skin = UI_GET_SKIN( menu );

	if( skin->bUseStyleBorders || skin->bUseTextureAssemblies ) {
		const char* descName = skin->astrMenuPopupSeparatorStyle;
		if( skin->bUseTextureAssemblies && RefSystem_ReferentFromString( "UITextureAssembly", descName )) {
			desc->textureAssemblyName = descName;
		} else {
			desc->styleBorderName = descName;
		}
	} else {
		desc->textureAssemblyNameUsingLegacyColor = "Default_MiniFrame_Empty_1px";
	}
}

static void ui_PopupMenuFillDrawingDescription( UIMenu* menu, UIDrawingDescription* desc )
{
	UISkin* skin = UI_GET_SKIN( menu );
		
	if( skin->bUseStyleBorders || skin->bUseTextureAssemblies ) {
		const char* descName = skin->astrMenuPopupStyle;
		if( skin->bUseTextureAssemblies && RefSystem_ReferentFromString( "UITextureAssembly", descName )) {
			desc->textureAssemblyName = descName;
		} else {
			desc->styleBorderName = descName;
		}
	} else {
		desc->textureNameUsingLegacyColor = "white";
		desc->overlayOutlineUsingLegacyColor2 = true;
	}
}

static AtlasTex* ui_MenuItemGetCheckTex( UIMenu* menu, UIMenuItem* item, bool itemState )
{
	UISkin* skin = UI_GET_SKIN( menu );

	if( skin->bUseStyleBorders || skin->bUseTextureAssemblies ) {
		if( itemState ) {
			return atlasLoadTexture( skin->pchCheckBoxChecked );
		} else {
			return atlasLoadTexture( skin->pchCheckBoxUnchecked );
		}
	} else {
		if( itemState ) {
			return atlasLoadTexture( "eui_tickybox_checked_8x8" );
		} else {
			return atlasLoadTexture( "eui_tickybox_unchecked_8x8" );
		}
	}
}

void ui_MenuDrawPopup(UIMenu *menu, UI_MY_ARGS, F32 z)
{
	F32 fItemHeight = menu->itemHeight * scale;
	CBox box;
	int i;
	Color normalBg, selectedBg, border, check, checkActive, checkInactive;
	F32 fHeight;
	F32 fDisplayHeight;
	F32 fWidth;
	F32 fSB;
	UIDrawingDescription desc = { 0 };
	ui_PopupMenuFillDrawingDescription( menu, &desc );

	if (UI_GET_SKIN(menu))
	{
		normalBg = UI_GET_SKIN(menu)->background[0];
		selectedBg = UI_GET_SKIN(menu)->background[1];
		border = UI_GET_SKIN(menu)->thinBorder[0];
		check = UI_GET_SKIN(menu)->button[0];
		checkActive = UI_GET_SKIN(menu)->button[1];
		checkInactive = UI_GET_SKIN(menu)->button[4];
	}
	else
	{
		normalBg = menu->widget.color[0];
		check = menu->widget.color[0];
		checkActive = menu->widget.color[0];
		checkInactive = menu->widget.color[0];
		selectedBg = menu->widget.color[1];
		border = ColorBlack;
	}

	fHeight = eaSize(&menu->items) * fItemHeight + ui_DrawingDescriptionHeight( &desc );
	fDisplayHeight = CLAMP(fHeight, 0, menu->openedY + g_ui_State.viewportMax[1]);
	fWidth = menu->itemWidth * scale + ui_DrawingDescriptionWidth( &desc );
	fSB = fHeight + y > g_ui_State.viewportMax[1] ? ui_ScrollbarWidth(UI_WIDGET(menu)->sb) * scale : 0;

	BuildCBox(&box, menu->openedX, menu->openedY, fWidth, fDisplayHeight);
	clipperPush(&box);
	ui_DrawingDescriptionDraw( &desc, &box, scale, z, 255, normalBg, border );
	
	for (i = 0; i < eaSize(&menu->items); i++)
	{
		UIMenuItem *item = menu->items[i];
		F32 drawX = (menu->openedX + ui_DrawingDescriptionLeftSize( &desc ) * scale);
		F32 drawY = (menu->openedY + i * fItemHeight - menu->widget.sb->ypos
					 + ui_DrawingDescriptionTopSize( &desc ) * scale);
		F32 drawYCenter = drawY + fItemHeight / 2;

		if (item->type == UIMenuSeparator)
		{
			CBox sepBox = { drawX, 0, drawX + menu->itemWidth * scale, 0 };
			UIDrawingDescription sepDesc = { 0 };
			float height;
			ui_PopupMenuSeparatorFillDrawingDescription( menu, &sepDesc );
			height = ui_DrawingDescriptionHeight( &sepDesc );

			// Actually, this should be centered inside the item
			sepBox.ly = drawYCenter - height * scale / 2;
			sepBox.hy = drawYCenter + height * scale / 2;
			
			ui_DrawingDescriptionDraw( &sepDesc, &sepBox, scale, z + 0.1, 255, normalBg, ColorBlack );
		}
		else
		{
			UIWidgetModifier eMods = UI_WIDGET(menu)->state;
			bool bSelected = false;
			UIStyleFont* itemFont;
			BuildCBox(&box, drawX, drawY, menu->itemWidth * scale, fItemHeight);
			if (!item->active)
				eMods |= kWidgetModifier_Inactive;
			if ((point_cbox_clsn(g_ui_State.mouseX, g_ui_State.mouseY, &box) && !menu->submenu) ||
				(item->type == UIMenuSubmenu && item->data.menu && item->data.menu->opened))
				bSelected = true;

			itemFont = ui_MenuItemGetFont( menu, bSelected, item->active );			
			ui_StyleFontUse(itemFont, bSelected && item->active, eMods);
			if (item->active && bSelected)
			{
				UIDrawingDescription activeDesc = { 0 };
				ui_PopupMenuItemFillDrawingDescription( menu, &activeDesc );
				ui_DrawingDescriptionDraw( &activeDesc, &box, scale, z + 0.1, 255, selectedBg, ColorBlack );
			}
			gfxfont_Printf(drawX + 16 * scale, drawYCenter, z + 0.2, scale, scale, CENTER_Y, "%s", ui_MenuItemGetText(item));

			if( item->type == UIMenuSubmenu ) {
				UISkin* skin = UI_GET_SKIN( menu );
				if( skin->astrMenuItemHasSubmenuTexture ) {
					AtlasTex* hasSubmenuTex = atlasLoadTexture( skin->astrMenuItemHasSubmenuTexture );
					display_sprite( hasSubmenuTex,
									drawX + (menu->itemWidth - UI_STEP - hasSubmenuTex->width) * scale,
									drawYCenter - hasSubmenuTex->height / 2 * scale,
									z + 0.1, scale, scale, 0xFFFFFFFF );
				} else {
					float width = ui_StyleFontWidth( itemFont, scale, ">" );
					gfxfont_Printf( drawX + (menu->itemWidth - UI_STEP) * scale - width, drawYCenter,
									z + 0.1, scale, scale, CENTER_Y, ">" );
				}
			} else if (item->rightText) {
				const char *rightText = item->rightText;
				F32 rightWidth = ui_StyleFontWidth(itemFont, scale, rightText);
				gfxfont_Printf( drawX + (menu->itemWidth - UI_STEP) * scale - rightWidth, drawYCenter,
								z + 0.1, scale, scale, CENTER_Y, "%s", rightText);
			}

			if (item->type == UIMenuCheckButton || item->type == UIMenuCheckRefButton)
			{
				AtlasTex *checkTex;
				Color checkColor;
				if (!item->active)
					checkColor = checkInactive;
				else if (point_cbox_clsn(g_ui_State.mouseX, g_ui_State.mouseY, &box))
					checkColor = checkActive;
				else
					checkColor = check;
				if (item->type == UIMenuCheckRefButton)
					checkTex = ui_MenuItemGetCheckTex( menu, item, *item->data.statePtr );
				else
					checkTex = ui_MenuItemGetCheckTex( menu, item, item->data.state );
				display_sprite(checkTex,
							   drawX + (16 - UI_STEP - checkTex->width/2) * scale,
							   drawY + (fItemHeight - checkTex->height) * scale / 2,
							   z + 0.2, scale, scale, RGBAFromColor(checkColor));
			}
		}
	}
	clipperPop();
	clipperPush(NULL);
	if (fSB)
		ui_ScrollbarDraw(menu->widget.sb, menu->openedX, menu->openedY, fWidth, fDisplayHeight, z, scale, fWidth, fHeight);
	clipperPop();

	if (menu->submenu)
		ui_MenuDrawPopup(menu->submenu, UI_MY_VALUES, z + 1);
}

static void ui_MenuPopupSubmenu(UIMenu *menu, S32 i, F32 fParentItemHeight, F32 fParentWidth)
{
	UIMenuItem *item = menu->items[i];
	UIMenu *submenu = item->data.menu;
	menu->submenu = submenu;

	submenu->submenu = NULL;
	submenu->widget.sb->xpos = 0;
	submenu->widget.sb->ypos = 0;

	if(!submenu->opened)
	{
		submenu->opened = true;

		// Call preopen function if one is registered
		if (submenu->preopenF)
			submenu->preopenF(submenu, submenu->preopenData);

		ui_MenuCalculateWidth(submenu);
	}

	{
		F32 fChildX = menu->openedX + fParentWidth;
		F32 fChildY = menu->openedY + i * fParentItemHeight;
		F32 fChildHeight = submenu->itemHeight * eaSize(&submenu->items) * menu->widget.scale * g_ui_State.scale;
		F32 fChildSB = fChildHeight > g_ui_State.viewportMax[1] ? ui_ScrollbarWidth(UI_WIDGET(submenu)->sb) : 0;
		F32 fChildWidth = (submenu->itemWidth + fChildSB) * submenu->widget.scale * g_ui_State.scale;
		if(fChildX + 3 + fChildWidth >= g_ui_State.screenWidth)
			ui_MenuPopup(submenu, fChildX - fChildWidth - fParentWidth, fChildY);
		else
			ui_MenuPopup(submenu, fChildX, fChildY);
	}
}

void ui_MenuTickPopup(UIMenu *menu, UI_MY_ARGS)
{
	F32 fItemHeight = menu->itemHeight * scale;
	CBox box;
	F32 fHeight;
	F32 fDisplayHeight;
	F32 fWidth;
	F32 fSB;
	UIDrawingDescription desc = { 0 };
	
	ui_PopupMenuFillDrawingDescription( menu, &desc );
	fHeight = eaSize(&menu->items) * fItemHeight + ui_DrawingDescriptionHeight( &desc );
	fDisplayHeight = CLAMP(fHeight, 0, menu->openedY + g_ui_State.viewportMax[1]);
	fWidth = menu->itemWidth * scale + ui_DrawingDescriptionWidth( &desc );
	fSB = fHeight + y > g_ui_State.viewportMax[1] ? ui_ScrollbarWidth(UI_WIDGET(menu)->sb) * scale : 0;

	if (eaSize(&menu->items) <= 0)
		return;

	BuildCBox(&box, menu->openedX, menu->openedY, fWidth, fDisplayHeight);
	if (menu->submenu)
	{
		if (menu->submenu->opened)
			ui_MenuTickPopup(menu->submenu, UI_MY_VALUES);
		else
		{
			menu->submenu = NULL;
			ui_MenuClose(menu);
		}
	}

	// Needs to be *after* we tick the submenu but *before* we check
	// internal collisions. If it ticks before the submenu, the submenu
	// doesn't get events that happen over the scrollbar.
	mouseClipPushRestrict(NULL);
	if (fSB)
		ui_ScrollbarTick(menu->widget.sb, menu->openedX, menu->openedY, fWidth, fDisplayHeight, 0, scale, fWidth, fHeight);

	if (mouseDownAnyButton() && !mouseDownAnyButtonHit(&box))
	{
		ui_MenuClose(menu);
		mouseClipPop();
		return;
	}
	else if (mouseUpHit(MS_LEFT, &box) || mouseUpHit(MS_RIGHT, &box))
	{
		S32 i;
		CBox itemBox;
		BuildCBox(&itemBox,
				  menu->openedX + ui_DrawingDescriptionLeftSize( &desc ),
				  menu->openedY + ui_DrawingDescriptionTopSize( &desc ) - menu->widget.sb->ypos,
				  menu->itemWidth * scale, fItemHeight);

		if (menu->ignoreNextUp)
			menu->ignoreNextUp = false;
		else
		{
			bool handled = false;
			for (i = 0; i < eaSize(&menu->items) && !handled; i++)
			{
				UIMenuItem *item = menu->items[i];
				bool left_clicked = mouseUpHit(MS_LEFT, &itemBox);
				bool right_clicked = mouseUpHit(MS_RIGHT, &itemBox);
				if(item->type != UIMenuCheckButton && item->type != UIMenuCheckRefButton)
					right_clicked = false;
				if (left_clicked || right_clicked)
				{
					if (item->active && item->type == UIMenuCheckButton)
					{
						item->data.state = !item->data.state;
						handled = true;
					}
					if (item->active && item->type == UIMenuCheckRefButton)
					{
						*item->data.statePtr = !(*item->data.statePtr);
						handled = true;
					}
					if (item->active && item->clickedF)
					{
						item->clickedF(item, item->clickedData);
						handled = true;
					}
					if(right_clicked)
						handled = false;
				}
				itemBox.ly += fItemHeight;
				itemBox.hy += fItemHeight;
			}

			if (handled)
			{
				ui_MenuClose(menu);
			}
		}
	}

	if (mouseCollision(&box))
	{
		S32 i;
		CBox itemBox;

		BuildCBox(&itemBox,
				  menu->openedX + ui_DrawingDescriptionLeftSize( &desc ),
				  menu->openedY + ui_DrawingDescriptionTopSize( &desc ) - menu->widget.sb->ypos,
				  menu->itemWidth * scale, fItemHeight);

		for (i = 0; i < eaSize(&menu->items); i++)
		{
			UIMenuItem *item = menu->items[i];
			if (mouseCollision(&itemBox))
			{
				if (item->type == UIMenuSubmenu && item->data.menu && item->data.menu != menu->submenu && item->active)
				{
					if (menu->submenu)
						ui_MenuClose(menu->submenu);

					ui_MenuPopupSubmenu(menu, i, fItemHeight, fWidth + fSB);
				}
				else if (item->type != UIMenuSubmenu && menu->submenu)
				{
					ui_MenuClose(menu->submenu);
					menu->submenu = NULL;
				}
			}
			itemBox.ly += fItemHeight;
			itemBox.hy += fItemHeight;
		}
		inpHandled();
	}
	mouseClipPop();
}

static void ui_MenuFillDrawingDescription( UIMenu* menu, UIDrawingDescription* desc )
{
	UISkin* skin = UI_GET_SKIN( menu );
		
	if( skin->bUseStyleBorders || skin->bUseTextureAssemblies ) {
		const char* descName;
		if( menu->opened ) {
			descName = skin->astrMenuBarButtonStyleOpened;
		} else if( ui_IsHovering( UI_WIDGET( menu ))) {
			descName = skin->astrMenuBarButtonStyleHighlight;
		} else {
			descName = skin->astrMenuBarButtonStyle;
		}
		
		if( skin->bUseTextureAssemblies && RefSystem_ReferentFromString( "UITextureAssembly", descName )) {
			desc->textureAssemblyName = descName;
		} else {
			desc->styleBorderName = descName;
		}
	} else {
		desc->textureNameUsingLegacyColor = "white";
	}
}

static void ui_MenuBarFillDrawingDescription( UIMenuBar* item, UIDrawingDescription* desc )
{
	UISkin* skin = UI_GET_SKIN( (UIWidgetWidget*)item );
		
	if( skin->bUseStyleBorders || skin->bUseTextureAssemblies ) {
		const char* descName = skin->astrMenuBarStyle;		
		if( skin->bUseTextureAssemblies && RefSystem_ReferentFromString( "UITextureAssembly", descName )) {
			desc->textureAssemblyName = descName;
		} else {
			desc->styleBorderName = descName;
		}
	} else {
		desc->textureNameUsingLegacyColor = "white";
	}
}

void ui_MenuDraw(UIMenu *menu, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(menu);
	Color c = UI_GET_SKIN(menu)->background[menu->opened ? 1 : 0];
	const char* widgetText = ui_WidgetGetText( UI_WIDGET( menu ));

	if (menu->opened)
		ui_MenuDrawPopup(menu, UI_MY_VALUES, g_ui_State.drawZ + 2000);

	if (widgetText && menu->type != UIMenuTransient)
	{
		UIStyleFont *font = ui_MenuGetFont( menu );
		UIDrawingDescription desc = { 0 };
		ui_MenuFillDrawingDescription( menu, &desc );
		ui_DrawingDescriptionDraw( &desc, &box, scale, z, 255, c, ColorBlack );
		ui_StyleFontUse(font, menu->opened, UI_WIDGET(menu)->state);
		gfxfont_Printf(x + w/2, y + h/2, z + 0.01, scale, scale, CENTER_XY, "%s", widgetText);
	}
}

void ui_MenuTick(UIMenu *menu, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(menu);

	UI_TICK_EARLY(menu, false, true);

	if (menu->opened)
		ui_MenuTickPopup(menu, UI_MY_VALUES);
	else if (mouseDownHit(MS_LEFT, &box))
	{
		if (!menu->opened)
		{
			ui_MenuPopup(menu, x, y + h);
			if ((menu->openedY >= y && menu->openedY < y + h) ||
				(menu->openedX > x && menu->openedX < x + w))
				menu->ignoreNextUp = true;
		}
		else
			ui_MenuClose(menu);
		ui_SetFocus(menu);

		// The menu should *NOT* claim it handled input. The menu bar will do it for it.
	}
}

static int cmpMenuItemName(const void *a, const void *b)
{
	UIMenuItem *itema = *(UIMenuItem**)a;
	UIMenuItem *itemb = *(UIMenuItem**)b;
	return stricmp(ui_MenuItemGetText(itema), ui_MenuItemGetText(itemb));
}

void ui_MenuSort(SA_PARAM_NN_VALID UIMenu *menu)
{
	eaQSort(menu->items, cmpMenuItemName);
}


void ui_MenuSetPreopenCallback(SA_PARAM_NN_VALID UIMenu *menu, UIActivationFunc callback, UserData preopenData)
{
	menu->preopenF = callback;
	menu->preopenData = preopenData;
}


UIMenu *ui_MenuCreate(const char *title)
{
	UIMenu *menu = (UIMenu *)calloc(1, sizeof(UIMenu));
	ui_MenuInitialize(menu, title, false);
	return menu;
}

UIMenu *ui_MenuCreateMessage(const char* titleMessage)
{
	UIMenu *menu = (UIMenu *)calloc(1, sizeof(UIMenu));
	ui_MenuInitialize(menu, titleMessage, true);
	return menu;
}

void ui_MenuInitialize(UIMenu *menu, const char *title, bool titleIsMessage)
{
	UIStyleFont *font = ui_MenuGetFont( menu );
	ui_WidgetInitialize(UI_WIDGET(menu), ui_MenuTick, ui_MenuDraw, ui_MenuFreeInternal, NULL, NULL);
	if (title)
	{
		UIDrawingDescription desc = { 0 };
		ui_MenuFillDrawingDescription( menu, &desc );

		if( titleIsMessage ) {
			ui_WidgetSetTextMessage(UI_WIDGET(menu), title);
		} else {
			ui_WidgetSetTextString(UI_WIDGET(menu), title);
		}

		ui_WidgetSetDimensions(UI_WIDGET(menu),
							   ui_StyleFontWidth(font, 1.f, ui_WidgetGetText(UI_WIDGET(menu))) + MAX(8, ui_DrawingDescriptionWidth( &desc )),
							   ui_StyleFontLineHeight(font, 1.f) + MAX(8, ui_DrawingDescriptionHeight( &desc )));
	}
	menu->widget.sb = ui_ScrollbarCreate(false, true);
	{
		UIDrawingDescription desc = { 0 };
		ui_PopupMenuItemFillDrawingDescription( menu, &desc );
		menu->itemHeight = ui_StyleFontLineHeight(font, 1.f) + ui_DrawingDescriptionHeight( &desc );
	}
}

UIMenu *ui_MenuCreateWithItems(const char *title, ...)
{
	UIMenu *menu = ui_MenuCreate(title);
	UIMenuItem *item;
	va_list va;
	va_start(va, title);
	while (item = va_arg(va, UIMenuItem *))
		ui_MenuAppendItem(menu, item);
	va_end(va);
	return menu;
}

void ui_MenuAppendItems(UIMenu *menu, ...)
{
	UIMenuItem *item;
	va_list va;
	va_start(va, menu);
	while (item = va_arg(va, UIMenuItem *))
		ui_MenuAppendItem(menu, item);
	va_end(va);
}

void ui_MenuClear(UIMenu *menu)
{
	eaSetSize(&menu->items, 0);
}

void ui_MenuClearAndFreeItems(UIMenu *menu)
{
	eaClearEx(&menu->items, ui_MenuItemFree);
}

void ui_MenuFreeInternal(UIMenu *menu)
{
	eaDestroyEx(&menu->items, ui_MenuItemFree);
	ui_WidgetFreeInternal(UI_WIDGET(menu));
}

void ui_MenuAppendItem(UIMenu *menu, UIMenuItem *item)
{
	UIDrawingDescription desc = { 0 };
	UIStyleFont *pFont = ui_MenuGetFont( menu );
	F32 width;

	ui_PopupMenuItemFillDrawingDescription( menu, &desc );
	if (UI_GET_SKIN(menu))
		pFont = GET_REF(UI_GET_SKIN(menu)->hNormal);
	width = ui_StyleFontWidth(pFont, 1.f, ui_MenuItemGetText(item));
	if (item->rightText || item->type == UIMenuSubmenu)
		width += (ui_StyleFontWidth(pFont, 1.f, item->rightText ? item->rightText : ">") + 2 * UI_STEP);
	width += (ui_DrawingDescriptionLeftSize( &desc ) + ui_DrawingDescriptionRightSize( &desc ));
	eaPush(&menu->items, item);
	menu->itemWidth = MAX(menu->itemWidth, width + 24);
}

void ui_MenuCalculateWidth(SA_PARAM_NN_VALID UIMenu *menu)
{
	UIDrawingDescription desc = { 0 };
	UIStyleFont *pFont = ui_MenuGetFont( menu );
	F32 fLeft;
	F32 fRight;
	F32 width;
	int i;

	ui_PopupMenuItemFillDrawingDescription( menu, &desc );
	fLeft = ui_DrawingDescriptionLeftSize( &desc );
	fRight = ui_DrawingDescriptionRightSize( &desc );
	menu->itemWidth = 24;
	for ( i = eaSize(&menu->items) - 1; i >= 0; --i)
	{
		width = ui_StyleFontWidth(pFont, 1.f, ui_MenuItemGetText(menu->items[i]));
		if (menu->items[i]->rightText || menu->items[i]->type == UIMenuSubmenu)
			width += (ui_StyleFontWidth(pFont, 1.f, menu->items[i]->rightText ? menu->items[i]->rightText : ">") + 2 * UI_STEP);
		width += (fLeft+fRight);
		menu->itemWidth = MAX(menu->itemWidth, width + 24);
	}
}

void ui_MenuButtonTick(UIMenuButton *pMenuButton, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(pMenuButton);

	UI_TICK_EARLY(pMenuButton, true, true);

	if (mouseClickHit(MS_LEFT, &box) || mouseClickHit(MS_RIGHT, &box) ||
		(mouseUpHit(MS_LEFT, &box) && pMenuButton->pressed) ||
		(mouseUpHit(MS_RIGHT, &box) && pMenuButton->pressed))
	{
		pMenuButton->pressed = false;
		ui_SetFocus(pMenuButton);
		inpHandled();
		pMenuButton->pMenu->widget.scale = pScale/g_ui_State.scale; // Scale menu same as the button
		ui_MenuPopupAtCursorOrWidgetBox(pMenuButton->pMenu);
	}
	else if (mouseDownHit(MS_LEFT, &box) || mouseDownHit(MS_RIGHT, &box))
	{
		pMenuButton->pressed = true;
		ui_SetFocus(pMenuButton);
		inpHandled();
	}
	else if (!mouseIsDown(MS_LEFT) && !mouseIsDown(MS_RIGHT))
		pMenuButton->pressed = false;

	UI_TICK_LATE(pMenuButton);
}

void ui_MenuButtonDraw(UIMenuButton *pMenuButton, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(pMenuButton);
	UIStyleBorder *pBorder = GET_REF(pMenuButton->hBorder);
	UIStyleBorder *pFocusedBorder = GET_REF(pMenuButton->hFocusedBorder);
	Color c;

	UI_DRAW_EARLY(pMenuButton);
	c = ui_WidgetButtonColor(UI_WIDGET(pMenuButton), false, pMenuButton->pressed);

	ui_StyleBorderDraw(pBorder, &box, RGBAFromColor(c), RGBAFromColor(c), z, scale, 255);

	UI_DRAW_LATE(pMenuButton);

	if (ui_IsFocused(pMenuButton) && pFocusedBorder)
		ui_StyleBorderDrawOutside(pFocusedBorder, &box, RGBAFromColor(c), RGBAFromColor(c), z, scale, 255);
}

bool ui_MenuButtonInput(UIMenuButton *pMenuButton, KeyInput *input)
{
	if (input->type != KIT_EditKey)
		return false;
	if ((input->scancode == INP_SPACE || input->scancode == INP_RETURN) && ui_IsActive(UI_WIDGET(pMenuButton)))
	{
		ui_MenuPopupAtCursorOrWidgetBox(pMenuButton->pMenu);
		return true;
	}
	else
		return false;
}

UIMenuButton *ui_MenuButtonCreate(F32 x, F32 y)
{
	UIMenuButton *pMenuButton = (UIMenuButton *)calloc(1, sizeof(UIMenuButton));
	ui_MenuButtonInitialize(pMenuButton, x, y);
	return pMenuButton;
}

void ui_MenuButtonInitialize(UIMenuButton *pMenuButton, F32 x, F32 y)
{
	UIStyleFont *font = GET_REF(g_ui_State.font);
	UISprite *pSprite = ui_SpriteCreate(3,3,10,10,"eui_arrow_dropdown_down.tga");
	pSprite->widget.uClickThrough = true;
	ui_WidgetInitialize(UI_WIDGET(pMenuButton), ui_MenuButtonTick, ui_MenuButtonDraw, ui_MenuButtonFreeInternal, ui_MenuButtonInput, NULL);
	ui_WidgetSetPosition(UI_WIDGET(pMenuButton), x, y);
	ui_WidgetSetDimensions(UI_WIDGET(pMenuButton), 16, 16);
	SET_HANDLE_FROM_STRING(g_ui_BorderDict, "Default_Capsule_Filled", pMenuButton->hBorder);
	if (UI_GET_SKIN(pMenuButton))
		font = GET_REF(UI_GET_SKIN(pMenuButton)->hNormal);
	pMenuButton->pMenu = ui_MenuCreate(NULL);
	ui_WidgetAddChild(UI_WIDGET(pMenuButton), UI_WIDGET(pSprite));
}

UIMenuButton *ui_MenuButtonCreateWithItems(F32 x, F32 y, ...)
{
	UIMenuButton *pMenuButton = ui_MenuButtonCreate(x,y);
	UIMenuItem *item;
	va_list va;
	va_start(va, y);
	while (item = va_arg(va, UIMenuItem *))
		ui_MenuAppendItem(pMenuButton->pMenu, item);
	va_end(va);
	return pMenuButton;
}

void ui_MenuButtonAppendItems(UIMenuButton *pMenuButton, ...)
{
	UIMenuItem *item;
	va_list va;
	va_start(va, pMenuButton);
	while (item = va_arg(va, UIMenuItem *))
		ui_MenuAppendItem(pMenuButton->pMenu, item);
	va_end(va);
}

void ui_MenuButtonAppendItem(UIMenuButton *pMenuButton, UIMenuItem *item)
{
	ui_MenuAppendItem(pMenuButton->pMenu, item);
}

void ui_MenuButtonFreeInternal(UIMenuButton *pMenuButton)
{
	REMOVE_HANDLE(pMenuButton->hBorder);
	REMOVE_HANDLE(pMenuButton->hFocusedBorder);
	ui_WidgetQueueFree(UI_WIDGET(pMenuButton->pMenu));
	ui_WidgetFreeInternal(UI_WIDGET(pMenuButton));
}

void ui_MenuButtonSetPreopenCallback(SA_PARAM_NN_VALID UIMenuButton *pMenuButton, UIActivationFunc callback, UserData preopenData)
{
	ui_MenuSetPreopenCallback(pMenuButton->pMenu, callback, preopenData);
}

void ui_MenuBarDraw(UIMenuBar *menubar, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(menubar);
	UIDrawingDescription desc = { 0 };
	Color c;
	ui_MenuBarFillDrawingDescription( menubar, &desc );
	ui_DrawingDescriptionInnerBoxCoords( &desc, &x, &y, &w, &h, scale );
	
	UI_DRAW_EARLY(menubar);
	if (!UI_GET_SKIN(menubar))
		c = menubar->widget.color[0];
	else
		c = UI_GET_SKIN(menubar)->background[0];
	ui_DrawingDescriptionDraw( &desc, &box, scale, z, 255, c, ColorBlack );
	UI_DRAW_LATE(menubar);
}

void ui_MenuBarTick(UIMenuBar *menubar, UI_PARENT_ARGS)
{
	UIDrawingDescription desc = { 0 };
	UISkin* skin = UI_GET_SKIN( menubar );
	UI_GET_COORDINATES(menubar);
	int i;
	
	ui_MenuBarFillDrawingDescription( menubar, &desc );
	ui_DrawingDescriptionInnerBoxCoords( &desc, &x, &y, &w, &h, scale );

	UI_TICK_EARLY(menubar, true, false);

	if (mouseDownHit(MS_LEFT, &box))
	{
		if (menubar->widget.group)
			ui_WidgetGroupSteal(menubar->widget.group, UI_WIDGET(menubar));
		else
			ui_WidgetGroupSteal(ui_WidgetGroupForDevice(NULL), UI_WIDGET(menubar));
	}

	// Menu rollover for all menus in this menubar.
	if (mouseCollision(&box))
	{
		UIMenu *underMouse = NULL;
		UIMenu *wasOpen = NULL;
		F32 popx, popy;
	
		for (i = 0; i < eaSize(&menubar->widget.children); i++)
		{
			UIMenu *menu = (UIMenu *)menubar->widget.children[i];
			F32 mx = ui_WidgetXPosition(UI_WIDGET(menu), x, w, scale),
				my = ui_WidgetYPosition(UI_WIDGET(menu), y, h, scale),
				mw = ui_WidgetWidth(UI_WIDGET(menu), w, scale),
				mh = ui_WidgetHeight(UI_WIDGET(menu), h, scale);
			CBox mbox = {mx, my, mx + mw, my + mh};
			if (mouseCollision(&mbox))
			{
				underMouse = menu;
				popx = mx;
				popy = my + mh + skin->iMenuPopupOffsetY;
			}
			if (menu->opened)
			{
				wasOpen = menu;
			}
		}
		if (wasOpen && underMouse)
		{
			ui_MenuClose(wasOpen);
			ui_MenuPopup(underMouse, popx, popy);
		}
	}

	UI_TICK_LATE(menubar);
}

UIMenuBar *ui_MenuBarCreate(UIMenu **menus)
{
	UIMenuBar *menubar = (UIMenuBar *)calloc(1, sizeof(UIMenuBar));
	ui_MenuBarInitialize(menubar, menus);
	return menubar;
}

void ui_MenuBarInitialize(UIMenuBar *menubar, UIMenu **menus)
{
	int i;
	ui_WidgetInitialize(UI_WIDGET(menubar), ui_MenuBarTick, ui_MenuBarDraw, ui_MenuBarFree, NULL, ui_WidgetDummyFocusFunc);
	for (i = 0; i < eaSizeSlow(&menus); i++)
		ui_MenuBarAppendMenu(menubar, menus[i]);
}

void ui_MenuBarResize(SA_PARAM_OP_VALID UIMenuBar *menubar)
{
	UISkin* skin = UI_GET_SKIN( menubar );
	float widthAccum = 0;
	float heightAccum = 0;
	int i;
	
	UIDrawingDescription desc = { 0 };
	ui_MenuBarFillDrawingDescription( menubar, &desc );
	for (i = 0; i < eaSize(&menubar->widget.children); i++) {
		widthAccum += menubar->widget.children[i]->width + skin->iMenuBarSpacing;
		heightAccum = MAX( heightAccum, menubar->widget.children[i]->height );
	}
	widthAccum -= skin->iMenuBarSpacing;

	widthAccum += ui_DrawingDescriptionWidth( &desc );
	heightAccum += ui_DrawingDescriptionHeight( &desc );
	ui_WidgetSetDimensions( UI_WIDGET( menubar ), widthAccum, heightAccum );
}

void ui_MenuBarRemoveMenu(UIMenuBar *menubar, UIMenu *menu)
{
	int i;
	ui_WidgetRemoveFromGroup(UI_WIDGET(menu));
	for(i = eaSize(&menubar->menus) - 1; i>=0; --i)
		if (menubar->menus[i] == menu)
			eaRemove(&menubar->menus, i);
}

void ui_MenuBarRemoveAllMenus(UIMenuBar *menubar)
{
	S32 i;
	for (i = eaSize(&menubar->widget.children) - 1; i >= 0; i--)
		ui_WidgetRemoveFromGroup(menubar->widget.children[i]);
	eaClear(&menubar->menus);
}

void ui_MenuBarAppendMenu(UIMenuBar *menubar, UIMenu *menu)
{
	UIDrawingDescription desc = { 0 };
	F32 newX = 0;
	int i;
	UISkin* skin = UI_GET_SKIN( menubar );
	ui_MenuBarFillDrawingDescription( menubar, &desc );

	for (i = 0; i < eaSize(&menubar->widget.children); i++)
		newX += menubar->widget.children[i]->width + skin->iMenuBarSpacing;
	menubar->widget.height = MAX(menubar->widget.height, menu->widget.height + ui_DrawingDescriptionHeight( &desc ));
	menu->widget.x = newX;
	menu->type = UIMenuInMenuBar;
	eaPush(&menubar->menus, menu);
	ui_WidgetAddChild(UI_WIDGET(menubar), UI_WIDGET(menu));
}

void ui_MenuBarFree(UIMenuBar *menubar)
{
	eaDestroy(&menubar->menus);
	ui_WidgetFreeInternal(UI_WIDGET(menubar));
}

UIMenuItem *ui_MenuFindItem(UIMenu *menu, const char *item_name) // Can be explicit ("File|Save") or implicit ("Save") name
{
	int i;
	char short_name[256];
	char *s;
	strcpy(short_name, item_name);
	if (s=strchr(short_name, '|'))
		*s = '\0';
	for (i=eaSize(&menu->items)-1; i>=0; i--) {
		UIMenuItem *item = menu->items[i];
		const char* itemText = ui_MenuItemGetText(item);
		if (itemText && stricmp(itemText, short_name)==0) {
			if (s) {
				// Need to look at children
				if (item->type == UIMenuSubmenu) {
					return ui_MenuFindItem(item->data.menu, s+1);
				}
			} else {
				return item;
			}
		}
		if (!s && item->type == UIMenuSubmenu) {
			item = ui_MenuFindItem(item->data.menu, item_name);
			if (item)
				return item;
		}
	}
	return NULL;
}

UIMenuItem *ui_MenuListFindItem(UIMenu **menus, const char *item_name) // Can be explicit ("File|Save") or implicit ("Save") name
{
	int i;
	char short_name[256];
	char *s;
	strcpy(short_name, item_name);
	if ( (s=strchr(short_name, '|')) )
		*s = '\0';
	for (i=eaSize(&menus)-1; i>=0; i--) {
		const char* widgetText = ui_WidgetGetText( UI_WIDGET( menus[ i ]));
		UIMenuItem *ret = NULL;
		if (s) {
			if (widgetText && stricmp(widgetText, short_name)==0) {
				ret = ui_MenuFindItem(menus[i], s+1);
			}
		} else {
			ret = ui_MenuFindItem(menus[i], item_name);
		}
		if (ret)
			return ret;
	}
	return NULL;
}
