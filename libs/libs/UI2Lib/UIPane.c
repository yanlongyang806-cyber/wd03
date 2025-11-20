/***************************************************************************



***************************************************************************/

#include "Color.h"

#include "inputMouse.h"
#include "inputLib.h"

#include "GfxClipper.h"
#include "GfxSpriteText.h"
#include "StringCache.h"

#include "UIPane.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

#define UI_GET_PANE_COORDINATES(pane) \
	F32 scale = pScale * UI_WIDGET(pane)->scale; \
	F32 x = floorf(ui_WidgetXPosition(UI_WIDGET(pane), pX, pW, pScale)); \
	F32 y = floorf(ui_WidgetYPosition(UI_WIDGET(pane), pY, pH, pScale)); \
	F32 z = (++g_ui_State.drawZ); \
	F32 w = ((pane)->viewportPane & UI_PANE_VP_LEFT) ? g_ui_State.viewportMax[0] - x : floorf(ui_WidgetWidth(UI_WIDGET(pane), pW, pScale)); \
	F32 h = ((pane)->viewportPane & UI_PANE_VP_TOP) ? g_ui_State.viewportMax[1] - y : floorf(ui_WidgetHeight(UI_WIDGET(pane), pH, pScale)); \
	CBox pBox = {pX, pY, pX + pW, pY + pH}; \
	CBox box = {x, y, x + w, y + h};		\
	CBox* oldWidgetBox = NULL

UIPane *ui_PaneCreateEx(F32 x, F32 y, F32 width, F32 height, UIUnitType widthUnit, UIUnitType heightUnit, U8 viewportPane MEM_DBG_PARMS)
{
	UIPane *pane = calloc(1, sizeof(UIPane));
	ui_PaneInitialize(pane, x, y, width, height, widthUnit, heightUnit, viewportPane MEM_DBG_PARMS_CALL);
	return pane;
}

void ui_PaneInitialize(UIPane *pane, F32 x, F32 y, F32 width, F32 height, UIUnitType widthUnit, UIUnitType heightUnit, U8 viewportPane MEM_DBG_PARMS)
{
	ui_WidgetInitializeEx(UI_WIDGET(pane), ui_PaneTick, ui_PaneDraw, ui_PaneFreeInternal, NULL, NULL MEM_DBG_PARMS_CALL);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pane), width, height, widthUnit, heightUnit);
	ui_WidgetSetPosition(UI_WIDGET(pane), x, y);
	pane->minW = pane->minH = 10.f;
	pane->viewportPane = viewportPane;
}

static void ui_PaneFillDrawingDescription( UIPane* pane, UIDrawingDescription* desc )
{
	UISkin* skin = UI_GET_SKIN( pane );

	if( pane->astrStyleOverride_USE_ACCESSOR ) {
		if( pane->bOverrideUseTextureAssemblies_USE_ACCESSOR && RefSystem_ReferentFromString( "UITextureAssembly", pane->astrStyleOverride_USE_ACCESSOR )) {
			if( pane->bOverrideUseLegacyColor_USE_ACCESSOR ) {
				desc->textureAssemblyNameUsingLegacyColor = pane->astrStyleOverride_USE_ACCESSOR;
			} else {
				desc->textureAssemblyName = pane->astrStyleOverride_USE_ACCESSOR;
			}
		} else {
			if( pane->bOverrideUseLegacyColor_USE_ACCESSOR ) {
				desc->styleBorderNameUsingLegacyColor = pane->astrStyleOverride_USE_ACCESSOR;
			} else {
				desc->styleBorderName = pane->astrStyleOverride_USE_ACCESSOR;
			}
		}
	} else if( skin->bUseStyleBorders || skin->bUseTextureAssemblies ) {
		if( skin->bUseTextureAssemblies && RefSystem_ReferentFromString( "UITextureAssembly", skin->astrPaneStyle )) {
			desc->textureAssemblyName = skin->astrPaneStyle;
		} else {
			desc->styleBorderName = skin->astrPaneStyle;
		}
	} else {
		desc->styleBorderNameUsingLegacyColor = "default_capsule_filled";
	}
}

static void ui_PaneTitleFillDrawingDescription( UIPane* pane, UIDrawingDescription* desc )
{
	UISkin* skin = UI_GET_SKIN( pane );

	if( pane->astrTitleStyleOverride_USE_ACCESSOR ) {
		if( pane->bOverrideUseTextureAssemblies_USE_ACCESSOR && RefSystem_ReferentFromString( "UITextureAssembly", pane->astrTitleStyleOverride_USE_ACCESSOR )) {
			if( pane->bOverrideUseLegacyColor_USE_ACCESSOR ) {
				desc->textureAssemblyNameUsingLegacyColor = pane->astrTitleStyleOverride_USE_ACCESSOR;
			} else {
				desc->textureAssemblyName = pane->astrTitleStyleOverride_USE_ACCESSOR;
			}
		} else {
			if( pane->bOverrideUseLegacyColor_USE_ACCESSOR ) {
				desc->styleBorderNameUsingLegacyColor = pane->astrTitleStyleOverride_USE_ACCESSOR;
			} else {
				desc->styleBorderName = pane->astrTitleStyleOverride_USE_ACCESSOR;
			}
		}
	} else if( skin->bUseStyleBorders || skin->bUseTextureAssemblies ) {
		if( skin->bUseTextureAssemblies && RefSystem_ReferentFromString( "UITextureAssembly", skin->astrPaneTitleStyle )) {
			desc->textureAssemblyName = skin->astrPaneTitleStyle;
		} else {
			desc->styleBorderName = skin->astrPaneTitleStyle;
		}
	} else {
		desc->styleBorderNameUsingLegacyColor = "default_capsule_filled";
	}
}

static UIStyleFont* ui_PaneGetTextFont( UIPane* pane )
{
	UISkin* skin = UI_GET_SKIN( pane );
	UIStyleFont* font = NULL;

	if( skin->bUseStyleBorders || skin->bUseTextureAssemblies ) {
		font = GET_REF( skin->hPaneTextFont );
	}

	if( !font ) {
		font = GET_REF( skin->hNormal );
	}

	return font;
}

void ui_PaneFreeInternal(UIPane *pane)
{
	ui_WidgetFreeInternal(UI_WIDGET(pane));
}

void ui_PaneSetStyle(UIPane *pane, const char *pchStyleName, bool bUseTextureAssemblies, bool bUseLegacyColor)
{
	ui_PaneSetStyleEx( pane, pchStyleName, NULL, bUseTextureAssemblies, bUseLegacyColor );
}

void ui_PaneSetStyleEx(UIPane *pane, const char *pchStyleName, const char* pchTitleStyleName, bool bUseTextureAssemblies, bool bUseLegacyColor)
{
	pane->astrStyleOverride_USE_ACCESSOR = allocAddString( pchStyleName );
	pane->astrTitleStyleOverride_USE_ACCESSOR = allocAddString( pchTitleStyleName );
	pane->bOverrideUseTextureAssemblies_USE_ACCESSOR = bUseTextureAssemblies;
	pane->bOverrideUseLegacyColor_USE_ACCESSOR = bUseLegacyColor;
}

void ui_PaneRestrictViewport(UIPane *pane, UI_MY_ARGS)
{
	if (!!(pane->viewportPane & UI_PANE_VP_TOP))
	{
		MIN1(g_ui_State.viewportMax[1], y);
	}

	if (!!(pane->viewportPane & UI_PANE_VP_BOTTOM))
	{
		MAX1(g_ui_State.viewportMin[1], y + h);
	}

	if (!!(pane->viewportPane & UI_PANE_VP_LEFT))
	{
		MIN1(g_ui_State.viewportMax[0], x);
	}

	if (!!(pane->viewportPane & UI_PANE_VP_RIGHT))
	{
		MAX1(g_ui_State.viewportMin[0], x + w);
	}

	if (g_ui_State.viewportMax[0] < g_ui_State.viewportMin[0])
		g_ui_State.viewportMax[0] = g_ui_State.viewportMin[0];

	if (g_ui_State.viewportMax[1] < g_ui_State.viewportMin[1])
		g_ui_State.viewportMax[1] = g_ui_State.viewportMin[1];
}


void ui_PaneCheckResizing(UIPane *pane, UI_PARENT_ARGS, UI_MY_ARGS)
{
	F32 titleHeight = pane->titleHeight;
	F32 leftWidth, rightWidth, bottomHeight, topHeight;
	CBox leftBox, rightBox, bottomBox, blBox, brBox, tlBox, trBox, topBox;
	bool collided = false;
	int mouseX = g_ui_State.mouseX, mouseY = g_ui_State.mouseY;

	if (!pane->resizable)
		return;

	if( !pane->invisible ) {
		UIDrawingDescription desc = { 0 };
		
		ui_PaneFillDrawingDescription( pane, &desc );
		leftWidth = ui_DrawingDescriptionLeftSize(&desc) * scale;
		rightWidth = ui_DrawingDescriptionRightSize(&desc) * scale;
		bottomHeight = ui_DrawingDescriptionBottomSize(&desc) * scale;
		topHeight = ui_DrawingDescriptionTopSize(&desc) * scale;
	} else {
		leftWidth = rightWidth = bottomHeight = topHeight = 0;
	}

	BuildCBox(&leftBox, x - leftWidth, y, leftWidth, h);
	BuildCBox(&rightBox, x + w, y, rightWidth, h);
	BuildCBox(&bottomBox, x, y + h, w, bottomHeight);
	BuildCBox(&blBox, x - leftWidth, y + h, leftWidth, bottomHeight);
	BuildCBox(&brBox, x + w, y + h, rightWidth, bottomHeight);
	BuildCBox(&tlBox, x - leftWidth, y - topHeight, leftWidth, topHeight);
	BuildCBox(&trBox, x + w, y - topHeight, rightWidth, topHeight);
	BuildCBox(&topBox, x, y - topHeight, w, topHeight);


	if (!pane->resizing && h)
	{
		int i;
		bool handled=false;
		struct {
			UIDirection direction;
			CBox *box;
		} collisions[] = {
			{UIBottomLeft, &blBox},
			{UIBottomRight, &brBox},
			{UITopLeft, &tlBox},
			{UITopRight, &trBox},
			{UIBottom, &bottomBox},
			{UILeft, &leftBox},
			{UIRight, &rightBox},
			{UITop, &topBox},
		};
		for (i=0; i<ARRAY_SIZE(collisions) && !handled; i++) {
			bool valid = (collisions[i].direction == (collisions[i].direction & pane->resizable));
			if (valid && mouseDownHit(MS_LEFT, collisions[i].box))
			{
				pane->resizing = collisions[i].direction;
				pane->grabbedX = (mouseX > (x + w/2)) ? (mouseX - (x + w)) : (mouseX - x);
				pane->grabbedY = (mouseY > (y + h/2)) ? (mouseY - (y + h)) : (mouseY - y);
				handled = true;
			}
		}
		for (i=0; i<ARRAY_SIZE(collisions) && !handled; i++) {
			bool valid = (collisions[i].direction == (collisions[i].direction & pane->resizable));
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
		if(pane->resizing != UINoDirection && pane->resizedF)
			pane->resizedF(pane, pane->resizedData);
		pane->resizing = UINoDirection;
	}

	if (pane->resizing != UINoDirection && h)
	{
		F32 maxScaledWidth = (pW - ((x - pX) + rightWidth)),
			minScaledWidth = pane->minW * scale,
			maxScaledHeight = (pH - ((y - pY) + bottomHeight)),
			minScaledHeight = pane->minH * scale;
		F32 maxW = pW - (x + rightWidth);
		F32 maxH = (pH / pScale) - y;
		if (UI_WIDGET(pane)->offsetFrom != UITopLeft) {
			// Set values to offset from UL
			UI_WIDGET(pane)->x = (x - pX) / pScale;
			UI_WIDGET(pane)->y = (y - pY) / pScale;
		}
		switch (pane->resizing) {
		case UILeft:
			mouseX = CLAMP(mouseX - pane->grabbedX, pX + leftWidth, (x + w) - minScaledWidth);
			UI_WIDGET(pane)->x = (mouseX - pX) / pScale;
			UI_WIDGET(pane)->width = ((x + w) - mouseX) / scale;
			break;
		case UIRight:
			UI_WIDGET(pane)->width = CLAMP((mouseX - pane->grabbedX) - x, minScaledWidth, maxScaledWidth) / scale;
			break;
		case UIBottom:
			UI_WIDGET(pane)->height = CLAMP((mouseY - pane->grabbedY) - y, minScaledHeight, maxScaledHeight) / scale;
			break;
		case UITop:
			mouseY = CLAMP(mouseY - pane->grabbedY, pY + topHeight, (y + h) - minScaledHeight);
			UI_WIDGET(pane)->y = (mouseY - pY) / pScale;
			UI_WIDGET(pane)->height = ((y + h) - mouseY) / scale;
			break;
		case UIBottomLeft:
			mouseX = CLAMP(mouseX - pane->grabbedX, pX + leftWidth, (x + w) - minScaledWidth);
			UI_WIDGET(pane)->x = (mouseX - pX) / pScale;
			UI_WIDGET(pane)->width = ((x + w) - mouseX) / scale;
			UI_WIDGET(pane)->height = CLAMP((mouseY - pane->grabbedY) - y, minScaledHeight, maxScaledHeight) / scale;
			break;
		case UIBottomRight:
			UI_WIDGET(pane)->width = CLAMP((mouseX - pane->grabbedX) - x, minScaledWidth, maxScaledWidth) / scale;
			UI_WIDGET(pane)->height = CLAMP((mouseY - pane->grabbedY) - y, minScaledHeight, maxScaledHeight) / scale;
			break;
		case UITopLeft:
			mouseX = CLAMP(mouseX - pane->grabbedX, pX + leftWidth, (x + w) - minScaledWidth);
			UI_WIDGET(pane)->x = (mouseX - pX) / pScale;
			UI_WIDGET(pane)->width = ((x + w) - mouseX) / scale;
			mouseY = CLAMP(mouseY - pane->grabbedY, pY + topHeight, (y + h) - minScaledHeight);
			UI_WIDGET(pane)->y = (mouseY - pY) / pScale;
			UI_WIDGET(pane)->height = ((y + h) - mouseY) / scale;
			break;
		case UITopRight:
			UI_WIDGET(pane)->width = CLAMP((mouseX - pane->grabbedX) - x, minScaledWidth, maxScaledWidth) / scale;
			mouseY = CLAMP(mouseY - pane->grabbedY, pY + topHeight, (y + h) - minScaledHeight);
			UI_WIDGET(pane)->y = (mouseY - pY) / pScale;
			UI_WIDGET(pane)->height = ((y + h) - mouseY) / scale;
			break;
		default:
			// in case something goes wrong, just reset state.
			pane->resizing = UINoDirection;
		}
		// Flip values if we're offset from the bottom or right
		if (UI_WIDGET(pane)->offsetFrom == UITopRight ||
			UI_WIDGET(pane)->offsetFrom == UIBottomRight ||
			UI_WIDGET(pane)->offsetFrom == UIRight)
		{
			UI_WIDGET(pane)->x = pW - ((UI_WIDGET(pane)->x + UI_WIDGET(pane)->width)*scale);
		}
		if (UI_WIDGET(pane)->offsetFrom == UIBottomLeft ||
			UI_WIDGET(pane)->offsetFrom == UIBottomRight ||
			UI_WIDGET(pane)->offsetFrom == UIBottom)
		{
			UI_WIDGET(pane)->y = pH - ((UI_WIDGET(pane)->y + UI_WIDGET(pane)->height)*scale);
		}
		ui_SetCursorForDirection(pane->resizing);
		ui_CursorLock();
		ui_SetFocus(pane);
		inpHandled();
	}
	else if ((mouseDownHit(MS_LEFT, &tlBox) || mouseDownHit(MS_LEFT, &trBox) ||	mouseDownHit(MS_LEFT, &topBox)) && h)
	{
		ui_CursorLock();
		ui_WidgetGroupSteal(UI_WIDGET(pane)->group, UI_WIDGET(pane));
		ui_SetFocus(pane);
		inpHandled();
	}
	else if (collided && h)
	{
		ui_CursorLock();
		inpHandled();
	}

	// Sanity check pane size.
	if ((w + leftWidth + rightWidth) > pW && UI_WIDGET(pane)->widthUnit == UIUnitFixed)
		UI_WIDGET(pane)->width = (pW - (leftWidth + rightWidth)) / scale;
	if ((h + topHeight + bottomHeight + titleHeight) > pH && UI_WIDGET(pane)->heightUnit == UIUnitFixed)
		UI_WIDGET(pane)->height = (pH - (topHeight + bottomHeight + titleHeight)) / scale;
}

void ui_PaneTick(UIPane *pane, UI_PARENT_ARGS)
{
	UI_GET_PANE_COORDINATES(pane);
	F32 titleZ = UI_GET_Z();
	F32 origX = x, origY = y, origW = w, origH = h;

	ui_PaneCheckResizing(pane, UI_PARENT_VALUES, UI_MY_VALUES);

	if(!pane->invisible)
	{
		UIDrawingDescription desc = { 0 };
		ui_PaneFillDrawingDescription( pane, &desc );
		ui_DrawingDescriptionInnerBoxCoords( &desc, &x, &y, &w, &h, scale );
	}

	mouseClipPushRestrict( &box );
	UI_TICK_EARLY(pane, true, false);
	UI_TICK_LATE(pane);
	if ((UI_WIDGET(pane)->state & kWidgetModifier_Hovering) && (!UI_WIDGET(pane)->uClickThrough)) {
		inpScrollHandled();
	}
	mouseClipPop();

	x = origX; y = origY; w = origW; h = origH;
	if (pane->viewportPane)
		ui_PaneRestrictViewport(pane, UI_MY_VALUES);
}

void ui_PaneDraw(UIPane *pane, UI_PARENT_ARGS)
{
	UI_GET_PANE_COORDINATES(pane);
	F32 titleZ = UI_GET_Z();
	Color c = UI_GET_SKIN( pane )->background[0];
	F32 origX = x, origY = y, origW = w, origH = h;
	const char* widgetText = ui_WidgetGetText( UI_WIDGET( pane ));

	// MJF TODO: Maybe should this get moved into UI_DRAW_EARLY?
	if( !clipperIntersects( &box ) && !pane->drawEvenIfInvisible) {
		return;
	}

	UI_DRAW_EARLY(pane);
	if (!pane->invisible)
	{
		UIDrawingDescription desc = { 0 };
		ui_PaneFillDrawingDescription( pane, &desc );
		ui_DrawingDescriptionDraw( &desc, &box, scale, z, 255, c, ColorBlack );

		if (pane->titleHeight > 0)
		{
			CBox title_box = box;
			UIDrawingDescription titleDesc = { 0 };
			ui_PaneTitleFillDrawingDescription( pane, &titleDesc );

			title_box.bottom = title_box.top + pane->titleHeight * scale;
			ui_DrawingDescriptionDraw( &titleDesc, &title_box, scale, titleZ, 255, ColorWhite, ColorBlack );
		}
		ui_DrawingDescriptionInnerBoxCoords( &desc, &x, &y, &w, &h, scale );
	}

	if (widgetText)
	{
		UIStyleFont *font = ui_PaneGetTextFont(pane);
		ui_StyleFontUse(font, false, UI_WIDGET(pane)->state);
		gfxfont_Printf(x + UI_STEP, y + h/2, z + 0.1, scale, scale, CENTER_Y, "%s", widgetText);
	}
	UI_DRAW_LATE(pane);
	x = origX; y = origY; w = origW; h = origH;
	if (pane->viewportPane)
		ui_PaneRestrictViewport(pane, UI_MY_VALUES);
}

void ui_PaneSetInvisible(SA_PARAM_NN_VALID UIPane *pane, bool invisible)
{
	pane->invisible = invisible;
}

void ui_PaneSetTitleHeight(SA_PARAM_NN_VALID UIPane *pane, F32 height)
{
	pane->titleHeight = height;
}

void ui_PaneSetResizable(SA_PARAM_NN_VALID UIPane *pane, UIDirection resizable, F32 minW, F32 minH)
{
	pane->resizable = resizable;
	pane->minH = minH;
	pane->minW = minW;
}

void ui_PaneAddChild(UIPane *pane, UIAnyWidget *child)
{
	ui_WidgetGroupAdd(&UI_WIDGET(pane)->children, (UIWidget *)child);
}

void ui_PaneRemoveChild(UIPane *pane, UIAnyWidget *child)
{
	ui_WidgetGroupRemove(&UI_WIDGET(pane)->children, (UIWidget *)child);
}
