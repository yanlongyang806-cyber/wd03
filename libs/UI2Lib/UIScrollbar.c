/***************************************************************************



***************************************************************************/

#include "UIScrollbar.h"
#include "inputLib.h"
#include "inputMouse.h"

#include "Color.h"
#include "GfxClipper.h"
#include "GfxSprite.h"
#include "GfxTexAtlas.h"
#include "GraphicsLib.h"

#include "ScratchStack.h"
#include "UIDnD.h"
#include "UISlider.h"
#include "UICore.h"

//#include "UIScrollbar_h_ast.c"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

// BOUNDS CONSTRAINTS MACROS
//
// pos - current scroll position.
// view_size - size of view in pixels.
// cont_size - size of contents in pixels after zoom scale is applied.

#define MIN_KEEP_CONT_IN_VIEW(view_size, cont_size)				0
#define MAX_KEEP_CONT_IN_VIEW(view_size, cont_size)				MAX(cont_size - view_size, 0)
#define PAGES_KEEP_CONT_IN_VIEW(view_size, cont_size)			MAX(1, cont_size / MAX(view_size, 0.01))

#define MIN_ALLOW_CONT_OUT_OF_VIEW(view_size, cont_size)		-view_size
#define MAX_ALLOW_CONT_OUT_OF_VIEW(view_size, cont_size)		cont_size
#define PAGES_ALLOW_CONT_OUT_OF_VIEW(view_size, cont_size)		MAX(1, (cont_size + 2 * view_size) / MAX(view_size, 0.01))

#define MIN_KEEP_CONT_AT_VIEW_CENTER(view_size, cont_size)		(-view_size / 2)
#define MAX_KEEP_CONT_AT_VIEW_CENTER(view_size, cont_size)		(cont_size - view_size / 2)
#define PAGES_KEEP_CONT_AT_VIEW_CENTER(view_size, cont_size)	MAX(1, (cont_size + view_size) / MAX(view_size, 0.01))

#define MIN_SCROLL(type, view_size, cont_size)					((type == UIScrollBounds_KeepContentsInView) \
																	? MIN_KEEP_CONT_IN_VIEW(view_size, cont_size) \
																	: (type == UIScrollBounds_AllowContentsOutOfView) \
																		? MIN_ALLOW_CONT_OUT_OF_VIEW(view_size, cont_size) \
																		: (type == UIScrollBounds_KeepContentsAtViewCenter) \
																			? MIN_KEEP_CONT_AT_VIEW_CENTER(view_size, cont_size) \
																			: devassertmsg(1, "UIScrollbar needs a MIN case for handling this UIScrollBounds."))
#define MAX_SCROLL(type, view_size, cont_size)					((type == UIScrollBounds_KeepContentsInView) \
																	? MAX_KEEP_CONT_IN_VIEW(view_size, cont_size) \
																	: (type == UIScrollBounds_AllowContentsOutOfView) \
																		? MAX_ALLOW_CONT_OUT_OF_VIEW(view_size, cont_size) \
																		: (type == UIScrollBounds_KeepContentsAtViewCenter) \
																			? MAX_KEEP_CONT_AT_VIEW_CENTER(view_size, cont_size) \
																			: devassertmsg(1, "UIScrollbar needs a MAX case for handling this UIScrollBounds."))
#define PAGES_SCROLL(type, view_size, cont_size)				((type == UIScrollBounds_KeepContentsInView) \
																	? PAGES_KEEP_CONT_IN_VIEW(view_size, cont_size) \
																	: (type == UIScrollBounds_AllowContentsOutOfView) \
																		? PAGES_ALLOW_CONT_OUT_OF_VIEW(view_size, cont_size) \
																		: (type == UIScrollBounds_KeepContentsAtViewCenter) \
																			? PAGES_KEEP_CONT_AT_VIEW_CENTER(view_size, cont_size) \
																			: devassertmsg(1, "UIScrollbar needs a PAGES case for handling this UIScrollBounds."))

F32 CLAMP_SCROLL(UIScrollBounds type, F32 pos, F32 view_size, F32 cont_size)
{
	F32 minScroll = MIN_SCROLL(type, view_size, cont_size);
	F32 maxScroll = MAX_SCROLL(type, view_size, cont_size);
	F32 result = CLAMPF32(pos, minScroll, maxScroll);
	return result;
}
F32 RATIO_SCROLL(UIScrollBounds type, F32 pos, F32 view_size, F32 cont_size)
{
	F32 minScroll = MIN_SCROLL(type, view_size, cont_size);
	F32 maxScroll = MAX_SCROLL(type, view_size, cont_size);
	F32 range = maxScroll - minScroll;
	F32 result = range ? (pos - minScroll) / range : 0;
	return result;
}
F32 INV_RATIO_SCROLL(UIScrollBounds type, F32 ratio, F32 view_size, F32 cont_size)
{
	F32 minScroll = MIN_SCROLL(type, view_size, cont_size);
	F32 maxScroll = MAX_SCROLL(type, view_size, cont_size);
	F32 result = ratio * (maxScroll - minScroll) + minScroll;
	return result;
}


UIScrollbar *ui_ScrollbarCreate(bool scrollX, bool scrollY)
{
	UIScrollbar *sb = (UIScrollbar *)calloc(1, sizeof(UIScrollbar));
	sb->alwaysScrollX = sb->scrollX = scrollX;
	sb->alwaysScrollY = sb->scrollY = scrollY;
	sb->color[0] = ColorRed;
	sb->color[1] = ColorBlue;
	sb->color[2] = ColorMagenta;
	return sb;
}

void ui_ScrollbarFree(UIScrollbar *sb)
{
	REMOVE_HANDLE(sb->hOverrideSkin);
	SAFE_FREE(sb);
}

UISkin* ui_ScrollbarGetSkin(UIScrollbar* sb)
{
	if( GET_REF( sb->hOverrideSkin )) {
		return GET_REF( sb->hOverrideSkin );
	} else if( sb->pOverrideSkin ) {
		return sb->pOverrideSkin;
	} else {
		return ui_GetActiveSkin();
	}
}

static AtlasTex* ui_ScrollbarGetArrowUpTex(UIScrollbar *sb)
{
	UISkin* skin = ui_ScrollbarGetSkin(sb);

	if( skin->bUseStyleBorders || skin->bUseTextureAssemblies ) {
		if( sb->disabledVertical ) {
			return UI_TEXTURE( skin->pchScrollArrowUpDisabled );
		} else if( sb->pressedVerticalUpButton ) {
			return UI_TEXTURE( skin->pchScrollArrowUpPressed );
		} else if( sb->hoverVerticalUpButton ) {
			return UI_TEXTURE( skin->pchScrollArrowUpHighlight );
		} else {
			return UI_TEXTURE( skin->pchScrollArrowUp );
		}
	} else {
		return UI_TEXTURE( "eui_arrow_small_up" );
	}
}

static AtlasTex* ui_ScrollbarGetArrowDownTex(UIScrollbar *sb)
{
	UISkin* skin = ui_ScrollbarGetSkin(sb);

	if( skin->bUseStyleBorders || skin->bUseTextureAssemblies ) {
		if( sb->disabledVertical ) {
			return UI_TEXTURE( skin->pchScrollArrowDownDisabled );
		} else if( sb->pressedVerticalDownButton ) {
			return UI_TEXTURE( skin->pchScrollArrowDownPressed );
		} else if( sb->hoverVerticalDownButton ) {
			return UI_TEXTURE( skin->pchScrollArrowDownHighlight );
		} else {
			return UI_TEXTURE( skin->pchScrollArrowDown );
		}
	} else {
		return UI_TEXTURE( "eui_arrow_small_down" );
	}
}

static AtlasTex* ui_ScrollbarGetArrowLeftTex(UIScrollbar *sb)
{
	UISkin* skin = ui_ScrollbarGetSkin(sb);

	if( skin->bUseStyleBorders || skin->bUseTextureAssemblies ) {
		if( sb->disabledHorizontal ) {
			return UI_TEXTURE( skin->pchScrollArrowLeftDisabled );
		} else if( sb->pressedHorizontalLeftButton ) {
			return UI_TEXTURE( skin->pchScrollArrowLeftPressed );
		} else if( sb->hoverHorizontalLeftButton ) {
			return UI_TEXTURE( skin->pchScrollArrowLeftHighlight );
		} else {
			return UI_TEXTURE( skin->pchScrollArrowLeft );
		}
	} else {
		return UI_TEXTURE( "eui_arrow_small_left" );
	}
}

static AtlasTex* ui_ScrollbarGetArrowRightTex(UIScrollbar *sb)
{
	UISkin* skin = ui_ScrollbarGetSkin(sb);

	if( skin->bUseStyleBorders || skin->bUseTextureAssemblies ) {
		if( sb->disabledHorizontal ) {
			return UI_TEXTURE( skin->pchScrollArrowRightDisabled );
		} else if( sb->pressedHorizontalRightButton ) {
			return UI_TEXTURE( skin->pchScrollArrowRightPressed );
		} else if( sb->hoverHorizontalRightButton ) {
			return UI_TEXTURE( skin->pchScrollArrowRightHighlight );
		} else {
			return UI_TEXTURE( skin->pchScrollArrowRight );
		}
	} else {
		return UI_TEXTURE( "eui_arrow_small_right" );
	}
}

static void ui_ScrollbarVTroughFillDrawingDescription(UIScrollbar* sb, UIDrawingDescription* desc)
{
	UISkin* skin = ui_ScrollbarGetSkin(sb);
	if(!skin)
		skin = ui_GetActiveSkin();
		
	if( skin->bUseStyleBorders || skin->bUseTextureAssemblies ) {
		const char* descName;

		if( sb->disabledVertical ) {
			descName = skin->astrScrollVTroughDisabled;
		} else {
			descName = skin->astrScrollVTrough;
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

static void ui_ScrollbarVHandleFillDrawingDescription(UIScrollbar* sb, UIDrawingDescription* desc, bool bScrollAllowed)
{
	UISkin* skin = ui_ScrollbarGetSkin(sb);
	if(!skin)
		skin = ui_GetActiveSkin();
		
	if( skin->bUseStyleBorders || skin->bUseTextureAssemblies ) {
		const char* descName;

		if( sb->disabledVertical ) {
			descName = skin->astrScrollVHandleDisabled;
		} else if( sb->pressedVerticalHandle ) {
			descName = skin->astrScrollVHandlePressed;
		} else if( sb->hoverVerticalHandle ) {
			descName = skin->astrScrollVHandleHighlight;
		} else {
			descName = skin->astrScrollVHandle;
		}
		
		if( skin->bUseTextureAssemblies && RefSystem_ReferentFromString( "UITextureAssembly", descName )) {
			desc->textureAssemblyName = descName;
		} else {
			desc->styleBorderName = descName;
		}
	} else {
		if( bScrollAllowed ) {
			desc->textureAssemblyNameUsingLegacyColor = "Default_Capsule_Filled";
		} else {
			// No scrollbar, so fill nothing in
		}
	}
}

static void ui_ScrollbarHTroughFillDrawingDescription(UIScrollbar* sb, UIDrawingDescription* desc)
{
	UISkin* skin = ui_ScrollbarGetSkin(sb);
	if(!skin)
		skin = ui_GetActiveSkin();
		
	if( skin->bUseStyleBorders || skin->bUseTextureAssemblies ) {
		const char* descName;

		if( sb->disabledHorizontal ) {
			descName = skin->astrScrollHTroughDisabled;
		} else {
			descName = skin->astrScrollHTrough;
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

static void ui_ScrollbarHHandleFillDrawingDescription(UIScrollbar* sb, UIDrawingDescription* desc, bool bScrollAllowed)
{
	UISkin* skin = ui_ScrollbarGetSkin(sb);
	if(!skin)
		skin = ui_GetActiveSkin();
		
	if( skin->bUseStyleBorders || skin->bUseTextureAssemblies ) {
		const char* descName;

		if( sb->disabledHorizontal ) {
			descName = skin->astrScrollHHandleDisabled;
		} else if( sb->pressedHorizontalHandle ) {
			descName = skin->astrScrollHHandlePressed;
		} else if( sb->hoverHorizontalHandle ) {
			descName = skin->astrScrollHHandleHighlight;
		} else {
			descName = skin->astrScrollHHandle;
		}

		if( skin->bUseTextureAssemblies && RefSystem_ReferentFromString( "UITextureAssembly", descName )) {
			desc->textureAssemblyName = descName;
		} else {
			desc->styleBorderName = descName;
		}
	} else {
		if( bScrollAllowed ) {
			desc->textureAssemblyNameUsingLegacyColor = "Default_Capsule_Filled";
		} else {
			// no scrollbar -- so fill nothing in
		}
	}
}

F32 ui_ScrollbarWidth(UIScrollbar *sb)
{
	AtlasTex *texture = ui_ScrollbarGetArrowUpTex(sb);
	UIDrawingDescription desc = { 0 };
	ui_ScrollbarVHandleFillDrawingDescription( sb, &desc, true );
	if (texture)
		return texture->width;
	else
		return ui_DrawingDescriptionWidth( &desc );
}

F32 ui_ScrollbarHeight(UIScrollbar *sb)
{
	AtlasTex *texture = ui_ScrollbarGetArrowLeftTex(sb);
	UIDrawingDescription desc = { 0 };
	ui_ScrollbarHHandleFillDrawingDescription( sb, &desc, true );
	if (texture)
		return texture->height;
	else
		return ui_DrawingDescriptionHeight( &desc );
}

void ui_ScrollbarYScroll(UIScrollbar *sb, F32 x, F32 y, F32 w, F32 h, F32 scale, F32 xSize, F32 ySize)
{
	AtlasTex *upArrow = ui_ScrollbarGetArrowUpTex(sb);
	AtlasTex *downArrow = ui_ScrollbarGetArrowDownTex(sb);
	CBox box;
	CBox topBox, bottomBox, buttonBox, trough;
	F32 scrollbarScaled = ui_ScrollbarWidth(sb) * scale;
	F32 sbHeight = h - (SAFE_MEMBER( upArrow, height ) + SAFE_MEMBER( downArrow, height )) * scale;
	F32 pages = PAGES_SCROLL(sb->scrollBoundsY, h, ySize);
	F32 buttonHeight = MAX(scrollbarScaled, sbHeight / pages);
	F32 usableBarSpace = max(0.01, sbHeight - buttonHeight);
	// Convert from attached widget sizes to on-screen sizes.
	F32 buttonY = y + (upArrow ? scrollbarScaled : 0) + RATIO_SCROLL(sb->scrollBoundsY, sb->ypos, h, ySize) * usableBarSpace;

	BuildCBox(&box, x, y, w + scrollbarScaled, h);

	BuildCBox(&topBox, x + w, y, scrollbarScaled, scrollbarScaled);
	BuildCBox(&bottomBox, x + w, y + h - scrollbarScaled, scrollbarScaled, scrollbarScaled);
	BuildCBox(&buttonBox, x + w, buttonY, scrollbarScaled, buttonHeight);
	BuildCBox(&trough, x + w, y + scrollbarScaled, scrollbarScaled, h - 2 * scrollbarScaled);

	if( upArrow ) {
		sb->hoverVerticalUpButton = mouseCollision( &topBox );
	}
	sb->hoverVerticalHandle = mouseCollision( &buttonBox );
	if( downArrow ) {
		sb->hoverVerticalDownButton = mouseCollision( &bottomBox );
	}

	if( !sb->pressedVerticalHandle ) { 
		sb->pressedVerticalUpButton = false;
		sb->pressedVerticalHandle = false;
		sb->pressedVerticalDownButton = false;
		if (mouseIsDown(MS_LEFT) && sb->hoverVerticalUpButton)
		{
			inpHandled();
			sb->pressedVerticalUpButton = true;
			sb->ypos -= g_ui_State.timestep * 400;
		}
		else if (mouseIsDown(MS_LEFT) && sb->hoverVerticalDownButton)
		{
			inpHandled();
			sb->pressedVerticalDownButton = true;
			sb->ypos += g_ui_State.timestep * 400;
		}
		else if (mouseDownHit(MS_LEFT, &buttonBox))
		{
			inpHandled();
			sb->pressedVerticalHandle = true;
			sb->dragOffset = g_ui_State.mouseY - buttonY;
		}
		else if (mouseDownHit(MS_LEFT, &trough))
		{
			inpHandled();
			if (g_ui_State.mouseY < buttonY)
				sb->ypos -= usableBarSpace * 0.75;
			else
				sb->ypos += usableBarSpace * 0.75;
		}
	} else {
		if( !mouseIsDown( MS_LEFT )) {
			sb->pressedVerticalHandle = false;
		}
		sb->pressedVerticalUpButton = false;
		sb->pressedVerticalDownButton = false;
	}
	sb->disabledVertical = false;

	if (sb->pressedVerticalHandle && usableBarSpace)
	{
		ui_SoftwareCursorThisFrame();
		inpHandled();
		buttonY = g_ui_State.mouseY - sb->dragOffset;
		sb->ypos = INV_RATIO_SCROLL(sb->scrollBoundsY, (buttonY - y - (upArrow ? scrollbarScaled : 0)) / usableBarSpace, h, ySize);
	}

	if (!sb->disableScrollWheel && mouseScrollHit(&box))
	{
		if (sb->needCtrlToScroll) {
			if (inpLevelPeek(INP_CONTROL))
			{
				sb->ypos -= 60 * scale * mouseZ();
				inpScrollHandled();
			}
		} else {
			if (!(inpLevelPeek(INP_CONTROL) || inpLevelPeek(INP_SHIFT) || inpLevelPeek(INP_ALT)))
			{
				if(mouseZ() < 0 && sb->ypos < ySize - h ||
					mouseZ() > 0 && sb->ypos > 0)
				{
					sb->ypos -= 60 * scale * mouseZ();
					inpScrollHandled();
				}
			}
		}
	}

	if (mouseCollision(&topBox) || mouseCollision(&bottomBox) || mouseCollision(&trough))
		inpHandled();

	sb->ypos = CLAMP_SCROLL(sb->scrollBoundsY, sb->ypos, h, ySize);
}

void ui_ScrollbarXScroll(UIScrollbar *sb, F32 x, F32 y, F32 w, F32 h, F32 scale, F32 xSize, F32 ySize)
{
	AtlasTex *leftArrow = ui_ScrollbarGetArrowLeftTex(sb);
	AtlasTex *rightArrow = ui_ScrollbarGetArrowRightTex(sb);
	CBox leftBox, rightBox, buttonBox, trough;
	F32 scrollbarScaled = ui_ScrollbarHeight(sb) * scale;
	F32 sbWidth = w - (SAFE_MEMBER( leftArrow, width ) + SAFE_MEMBER( rightArrow, width )) * scale;
	F32 pages = PAGES_SCROLL(sb->scrollBoundsX, w, xSize);
	F32 buttonWidth = MAX(scrollbarScaled, sbWidth / pages);
	F32 usableBarSpace = sbWidth - buttonWidth;
	// Convert from attached widget sizes to on-screen sizes.
	F32 buttonX = x + (leftArrow ? scrollbarScaled : 0) + RATIO_SCROLL(sb->scrollBoundsX, sb->xpos, w, xSize) * usableBarSpace;
	 
	BuildCBox(&leftBox, x, y + h, scrollbarScaled, scrollbarScaled);
	BuildCBox(&rightBox, x + w - scrollbarScaled, y + h, scrollbarScaled, scrollbarScaled);
	BuildCBox(&buttonBox, buttonX, y + h, buttonWidth, scrollbarScaled);
	BuildCBox(&trough, x + scrollbarScaled, y + h, w - 2 * scrollbarScaled, scrollbarScaled);

	if( leftArrow ) {
		sb->hoverHorizontalLeftButton = mouseCollision( &leftBox );
	}
	sb->hoverHorizontalHandle = mouseCollision( &buttonBox );
	if( rightArrow ) {
		sb->hoverHorizontalRightButton = mouseCollision( &rightBox );
	}

	if( !sb->pressedHorizontalHandle ) {
		sb->pressedHorizontalLeftButton = false;
		sb->pressedHorizontalHandle = false;
		sb->pressedHorizontalRightButton = false;
		if (mouseIsDown(MS_LEFT) && sb->hoverHorizontalLeftButton)
		{
			inpHandled();
			sb->pressedHorizontalLeftButton = true;
			sb->xpos -= g_ui_State.timestep * 400;
		}
		else if (mouseIsDown(MS_LEFT) && sb->hoverHorizontalRightButton)
		{
			inpHandled();
			sb->pressedHorizontalRightButton = true;
			sb->xpos += g_ui_State.timestep * 400;
		}
		else if (mouseDownHit(MS_LEFT, &buttonBox))
		{
			inpHandled();
			sb->pressedHorizontalHandle = true;
			sb->dragOffset = g_ui_State.mouseX - buttonX;
		}
		else if (mouseDownHit(MS_LEFT, &trough))
		{
			inpHandled();
			if (g_ui_State.mouseX < buttonX)
				sb->xpos -= usableBarSpace * 0.75;
			else
				sb->xpos += usableBarSpace * 0.75;
		}
	} else {
		if( !mouseIsDown( MS_LEFT )) {
			sb->pressedHorizontalHandle = false;
		}
		sb->pressedHorizontalLeftButton = false;
		sb->pressedHorizontalRightButton = false;
	}
	sb->disabledHorizontal = false;

	if (sb->pressedHorizontalHandle && usableBarSpace)
	{
		buttonX = g_ui_State.mouseX - sb->dragOffset;
		ui_SoftwareCursorThisFrame();
		inpHandled();
		sb->xpos = INV_RATIO_SCROLL(sb->scrollBoundsX, (buttonX - x - (leftArrow ? scrollbarScaled : 0)) / usableBarSpace, w, xSize);
	}

	sb->xpos = CLAMP_SCROLL(sb->scrollBoundsX, sb->xpos, w, xSize);
}

void ui_ScrollbarTick(UIScrollbar *sb, F32 x, F32 y, F32 w, F32 h, F32 z, F32 scale, F32 xSize, F32 ySize)
{
	if (sb->scrollY && MAX_SCROLL(sb->scrollBoundsY, h, ySize) > MIN_SCROLL(sb->scrollBoundsY, h, ySize))
		ui_ScrollbarYScroll(sb, x, y, w, h, scale, xSize, ySize);
	else
	{
		sb->ypos = 0;
		sb->disabledVertical = true;
	}

	if (sb->scrollX && MAX_SCROLL(sb->scrollBoundsX, w, xSize) > MIN_SCROLL(sb->scrollBoundsX, w, xSize))
		ui_ScrollbarXScroll(sb, x, y, w, h, scale, xSize, ySize);
	else
	{
		sb->xpos = 0;
		sb->disabledHorizontal = true;
	}
}

typedef struct UIScrollbarStack
{
	UIScrollbar *pBar;
	F32 x;
	F32 y;
	F32 w;
	F32 h;
	F32 scale;
	F32 xSize;
	F32 ySize;
} UIScrollbarStack;

UIScrollbarStack **s_ScrollbarStack;


// Functions used inside ticks to push/pop the scrollbar state, for focus change
void ui_ScrollbarPushState(UIScrollbar *pBar, F32 x, F32 y, F32 w, F32 h, F32 scale, F32 xSize, F32 ySize)
{
	UIScrollbarStack *pStack = ScratchAlloc(sizeof(UIScrollbarStack));
	pStack->pBar = pBar;
	pStack->x = x;
	pStack->y = y;
	pStack->w = w;
	pStack->h = h;
	pStack->scale = scale;
	pStack->xSize = xSize;
	pStack->ySize = ySize;

	eaPush(&s_ScrollbarStack, pStack);
}

void ui_ScrollbarPopState(void)
{
	UIScrollbarStack *pStack = eaPop(&s_ScrollbarStack);
	if (pStack)
	{
		ScratchFree(pStack);
	}
}

void ui_ScrollbarParentSetScrollPos(F32 xPos, F32 yPos)
{
	UIScrollbarStack *pStack = eaGet(&s_ScrollbarStack, eaSize(&s_ScrollbarStack) - 1);
	if (pStack)
	{
		if (pStack->pBar->scrollX)
		{
			pStack->pBar->xpos = CLAMP_SCROLL(pStack->pBar->scrollBoundsX, xPos, 0, pStack->xSize);
		}
		if (pStack->pBar->scrollY)
		{
			pStack->pBar->ypos = CLAMP_SCROLL(pStack->pBar->scrollBoundsY, yPos, 0, pStack->ySize);
		}
	}
}

void ui_ScrollbarParentScrollTo(F32 x, F32 y)
{
	UIScrollbarStack *pStack = eaGet(&s_ScrollbarStack, eaSize(&s_ScrollbarStack) - 1);
	if (pStack)
	{
		F32 startX = pStack->x;
		F32 startY = pStack->y;

		if (pStack->pBar->scrollX)
		{		
			if (x < startX)
			{
				pStack->pBar->xpos -= (startX - x);
			}
			else if (x > startX + pStack->w)
			{
				pStack->pBar->xpos += (x - (startX + pStack->w));
			}
		}
		if (pStack->pBar->scrollY)
		{		
			if (y < startY)
			{
				pStack->pBar->ypos -= (startY - y);
			}
			else if (y > startY + pStack->h)
			{
				pStack->pBar->ypos += (y - (startY + pStack->h));
			}
		}
	}
}




void ui_ScrollbarYDraw(UIScrollbar *sb, F32 x, F32 y, F32 w, F32 h, F32 z, F32 scale, F32 xSize, F32 ySize)
{
	AtlasTex *upArrow = ui_ScrollbarGetArrowUpTex(sb);
	AtlasTex *downArrow = ui_ScrollbarGetArrowDownTex(sb);
	CBox buttonBox, troughBox;
	F32 scrollbarScaled = ui_ScrollbarWidth(sb) * scale;
	F32 sbHeight = h - (SAFE_MEMBER( upArrow, height ) + SAFE_MEMBER( downArrow, height )) * scale;
	F32 pages = PAGES_SCROLL(sb->scrollBoundsY, h, ySize);
	F32 buttonHeight = MAX(scrollbarScaled, sbHeight / pages);
	F32 usableBarSpace = sbHeight - buttonHeight;
	// Convert from attached widget sizes to on-screen sizes.
	F32 diff = ySize - h;
	F32 buttonY = y + SAFE_MEMBER( upArrow, height ) * scale;
	Color button, handle, trough;
	F32 buttonYScale = scale;
	UIDrawingDescription troughDesc = { 0 };
	UIDrawingDescription handleDesc = { 0 };
	ui_ScrollbarVTroughFillDrawingDescription( sb, &troughDesc );
	ui_ScrollbarVHandleFillDrawingDescription( sb, &handleDesc, pages > 1 );

	if (ui_ScrollbarGetSkin(sb))
	{
		UISkin* skin = ui_ScrollbarGetSkin(sb);
		if( skin->bUseTextureAssemblies || skin->bUseStyleBorders ) {
			button = ColorWhite;
			handle = ColorWhite;
			trough = ColorWhite;
		} else {
			button = skin->button[0];
			handle = skin->button[sb->pressedVerticalHandle ? 1 : 0];
			trough = skin->trough[0];
		}
	}
	else
	{
		button = sb->color[0];
		trough = sb->color[1];
		handle = sb->color[2];
	}

	if (sbHeight < 0 && upArrow && downArrow)
	{
		// This means there isn't enough room for 2 buttons so scale buttons down
		sbHeight = 0;
		buttonHeight = 0;
		usableBarSpace = 0;
		buttonYScale = h / (upArrow->height + downArrow->height);
	}

	buttonY += RATIO_SCROLL(sb->scrollBoundsY, sb->ypos, h, ySize) * usableBarSpace;

	if( upArrow && downArrow ) {
		BuildCBox(&buttonBox, x + w, buttonY, SAFE_MEMBER( upArrow, width ) * scale, buttonHeight);
		BuildCBox(&troughBox, x + w, y + SAFE_MEMBER( upArrow, height ) * scale, SAFE_MEMBER( upArrow, width ) * scale, sbHeight);
	} else {
		BuildCBox(&buttonBox, x + w, buttonY, ui_DrawingDescriptionWidth( &handleDesc ) * scale, buttonHeight );
		BuildCBox(&troughBox, x + w, y, ui_DrawingDescriptionWidth( &handleDesc ) * scale, sbHeight);
	}

	ui_DrawingDescriptionDraw( &troughDesc, &troughBox, scale, z + 0.01, 255, trough, ColorBlack );
	ui_DrawingDescriptionDraw( &handleDesc, &buttonBox, scale, z + 0.1, 255, handle, ColorBlack );
	if( upArrow && downArrow ) {
		display_sprite(upArrow, x + w, y, z + 0.02, scale, buttonYScale, RGBAFromColor(button));
		display_sprite(downArrow, x + w, y + h - downArrow->height * buttonYScale, z + 0.02, scale, buttonYScale, RGBAFromColor(button));
	}
}

void ui_ScrollbarXDraw(UIScrollbar *sb, F32 x, F32 y, F32 w, F32 h, F32 z, F32 scale, F32 xSize, F32 ySize)
{
	CBox buttonBox, troughBox;
	AtlasTex *leftArrow = ui_ScrollbarGetArrowLeftTex(sb);
	AtlasTex *rightArrow = ui_ScrollbarGetArrowRightTex(sb);
	F32 scrollbarScaled = ui_ScrollbarHeight(sb) * scale;
	F32 sbWidth = w - (SAFE_MEMBER( leftArrow, width ) + SAFE_MEMBER( rightArrow, width )) * scale;
	F32 pages = PAGES_SCROLL(sb->scrollBoundsX, w, xSize);
	F32 buttonWidth = MAX(scrollbarScaled, sbWidth / pages);
	F32 usableBarSpace = sbWidth - buttonWidth;
	// Convert from attached widget sizes to on-screen sizes.
	F32 buttonX = x + SAFE_MEMBER( leftArrow, width ) * scale;
	Color button, handle, trough;
	UIDrawingDescription troughDesc = { 0 };
	UIDrawingDescription handleDesc = { 0 };
	ui_ScrollbarHTroughFillDrawingDescription( sb, &troughDesc );
	ui_ScrollbarHHandleFillDrawingDescription( sb, &handleDesc, pages > 1 );
	 
	if (ui_ScrollbarGetSkin(sb))
	{
		UISkin* skin = ui_ScrollbarGetSkin(sb);
		if( skin->bUseStyleBorders || skin->bUseTextureAssemblies ) {
			button = ColorWhite;
			handle = ColorWhite;
			trough = ColorWhite;
		} else {
			button = skin->button[0];
			handle = skin->button[sb->pressedHorizontalHandle ? 1 : 0];
			trough = skin->trough[0];
		}
	}
	else
	{
		button = sb->color[0];
		trough = sb->color[1];
		handle = sb->color[2];
	}

	buttonX += RATIO_SCROLL(sb->scrollBoundsX, sb->xpos, w, xSize) * usableBarSpace;

	if( leftArrow && rightArrow ) {
		BuildCBox(&buttonBox, buttonX, y + h, buttonWidth, leftArrow->height * scale);
		BuildCBox(&troughBox, x + leftArrow->width * scale, y + h, sbWidth, leftArrow->height * scale);
	} else {
		BuildCBox(&buttonBox, buttonX, y + h, buttonWidth, 0);
		BuildCBox(&troughBox, x , y + h, sbWidth, ui_DrawingDescriptionHeight( &handleDesc ) * scale);
	}

	ui_DrawingDescriptionDraw( &troughDesc, &troughBox, scale, z + 0.01, 255, trough, ColorBlack );
	ui_DrawingDescriptionDraw( &handleDesc, &buttonBox, scale, z + 0.1, 255, handle, ColorBlack );
	if( leftArrow && rightArrow ) {
		display_sprite(leftArrow, x, y + h, z + 0.02, scale, scale, RGBAFromColor(button));
		display_sprite(rightArrow, x + w - rightArrow->width * scale, y + h, z + 0.02, scale, scale, RGBAFromColor(button));
	}
}
 
void ui_ScrollbarDraw(UIScrollbar *sb, F32 x, F32 y, F32 w, F32 h, F32 z, F32 scale, F32 xSize, F32 ySize)
{
	// MJF TODO: Remove alwaysScrollX?
	if (sb->alwaysScrollX || (sb->scrollX && MAX_SCROLL(sb->scrollBoundsX, w, xSize) > MIN_SCROLL(sb->scrollBoundsX, w, xSize)))
		ui_ScrollbarXDraw(sb, x, y, w, h, z, scale, xSize, ySize);
	if (sb->alwaysScrollY || (sb->scrollY && MAX_SCROLL(sb->scrollBoundsY, h, ySize) > MIN_SCROLL(sb->scrollBoundsY, h, ySize)))
		ui_ScrollbarYDraw(sb, x, y, w, h, z, scale, xSize, ySize);
}

void ui_ScrollAreaDraw(UIScrollArea *scrollarea, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(scrollarea);
	F32 xSize, xPos, ySize, yPos;
	const char* widgetText = ui_WidgetGetText( UI_WIDGET( scrollarea ));

	if (scrollarea->widget.sb->scrollX)
		h -= ui_ScrollbarHeight(UI_WIDGET(scrollarea)->sb) * scale;
	if (scrollarea->widget.sb->scrollY)
		w -= ui_ScrollbarWidth(UI_WIDGET(scrollarea)->sb) * scale;

	xSize = (scrollarea->widget.sb->scrollX ? MAX(w - ui_ScrollbarWidth(UI_WIDGET(scrollarea)->sb) * scale, scrollarea->xSize) : w);
	xPos = (scrollarea->widget.sb->scrollX ? x - scrollarea->widget.sb->xpos : x);
	ySize = (scrollarea->widget.sb->scrollY ? MAX(h - ui_ScrollbarHeight(UI_WIDGET(scrollarea)->sb) * scale, scrollarea->ySize) : h);
	yPos = (scrollarea->widget.sb->scrollY ? y - scrollarea->widget.sb->ypos : y);

	clipperPushRestrict(&box);
	ui_ScrollbarDraw(scrollarea->widget.sb, x, y, w, h, z, scale, scrollarea->xSize, scrollarea->ySize);
	clipperPop();

	BuildCBox(&box, x, y, w, h);
	CBoxClipTo(&pBox, &box);
	clipperPushRestrict(&box);
	if (scrollarea->widget.sb->scrollX || scrollarea->widget.sb->scrollY)
		ui_ScrollbarPushState(scrollarea->widget.sb, x, y, w, h, scale, scrollarea->xSize, scrollarea->ySize);
	ui_WidgetGroupDrawEx(&scrollarea->widget.children, xPos, yPos, xSize, ySize, scale * scrollarea->childScale, x, y, w, h, scale);
	if (scrollarea->widget.sb->scrollX || scrollarea->widget.sb->scrollY)
		ui_ScrollbarPopState();

	if (widgetText)
	{
		ui_DrawTextInBoxSingleLine( GET_REF(UI_GET_SKIN(scrollarea)->hNormal), widgetText, true, &box, UI_GET_Z(), scale, UINoDirection );
	}

	if(scrollarea->zoom_slider) {
		UISkin* skin = UI_GET_SKIN( scrollarea );
		scrollarea->zoom_slider->widget.drawF(scrollarea->zoom_slider, x, y, w, h, scale);
		if (skin->astrScrollZoomMin)
		{
			display_sprite(atlasLoadTexture( skin->astrScrollZoomMin ), x + 10, y + 10, UI_GET_Z(), 1, 1, -1 );
		}
		if (skin->astrScrollZoomMax)
		{
			display_sprite(atlasLoadTexture( skin->astrScrollZoomMax ), x + 130, y + 10, UI_GET_Z(), 1, 1, -1 );
		}
	}
	
	clipperPop();
}

void ui_ScrollAreaFindSize(UIScrollArea *scrollarea)
{
	int i;
	int xSize = 0;
	int ySize = 0;

	for(i=eaSize(&scrollarea->widget.children)-1;i>=0;i--)
	{
		if(xSize < scrollarea->widget.children[i]->x + scrollarea->widget.children[i]->width + scrollarea->scrollPadding)
			xSize = scrollarea->widget.children[i]->x + scrollarea->widget.children[i]->width + scrollarea->scrollPadding;
		if(ySize < scrollarea->widget.children[i]->y + scrollarea->widget.children[i]->height + scrollarea->scrollPadding)
			ySize = scrollarea->widget.children[i]->y + scrollarea->widget.children[i]->height + scrollarea->scrollPadding;
	}

	scrollarea->xSize = xSize*scrollarea->childScale;
	scrollarea->ySize = ySize*scrollarea->childScale;
}

void ui_ScrollAreaUpdateSize(SA_PARAM_NN_VALID UIScrollArea *scrollarea)
{
	if(scrollarea->autosize)
		ui_ScrollAreaFindSize(scrollarea);
	else if(scrollarea->sizeF)
	{
		Vec2 scrollarea_size;
		scrollarea->sizeF(scrollarea, scrollarea_size, scrollarea->sizeData);
		ui_ScrollAreaSetSize(scrollarea, scrollarea_size[0], scrollarea_size[1]);
	}
}

void ui_ScrollAreaScrollToPosition(SA_PARAM_NN_VALID UIScrollArea *scrollarea, F32 x, F32 y)
{
	scrollarea->scrollToTargetRemaining = 0.5;
	setVec2(scrollarea->scrollToTargetPos, x, y);
	scrollarea->autoScrollCenter = 0;
}

void ui_ScrollAreaZoomToScale(SA_PARAM_NN_VALID UIScrollArea *scrollarea, F32 scale, int dir)
{
	scrollarea->scrollToTargetZoom = scale;
	scrollarea->scrollToTargetZoomDir = dir;
}

void ui_ScrollAreaSetAutoZoomRate(SA_PARAM_NN_VALID UIScrollArea *scrollarea, F32 rate)
{
	scrollarea->defaultZoomRate = rate;
}

static int minElem2NoNeg( int a, int b, int* index )
{
	if( a >= 0 && a <= b ) {
		*index = 0;
		return a;
	} else if( b >= 0 && b <= a) {
		*index = 1;
		return b;
	}

	*index = -1;
	return -1;
}

// currentCenter and targetCenter are in child (unscaled) units
static void ui_ScrollAreaUpdatePositionAndZoom(UIScrollArea *scrollarea, F32 x, F32 y, F32 w, F32 h, Vec2 targetCenter, F32 targetZoom)
{
	scrollarea->widget.sb->xpos = targetCenter[0]*targetZoom - w/2;
	scrollarea->widget.sb->ypos = targetCenter[1]*targetZoom - h/2;
 	ui_ScrollAreaSetChildScale(scrollarea, targetZoom);

	if (scrollarea->zoom_slider)
		ui_SliderSetValue( scrollarea->zoom_slider, scrollarea->childScale );
}

F32 ui_ScrollAreaCalculateZoom(UIScrollArea *scrollarea, F32 x, F32 y, F32 w, F32 h, CBox *box, F32 targetZoom)
{
	static F32 initial_rate = 1.03f;
	static F32 decay_rate = 0.85f;
	F32 min_zoom_scale, max_zoom_scale;
	UIScrollbar *sb = UI_WIDGET(scrollarea)->sb;

	// Calculate min/max zoom
	if (scrollarea->maxZoomScale)
		max_zoom_scale = scrollarea->maxZoomScale;
	else
		max_zoom_scale = 4.f;
	if (scrollarea->minZoomScale)
		min_zoom_scale = scrollarea->minZoomScale;
	else if (scrollarea->xSize != 0 && scrollarea->ySize != 0)
	{
		min_zoom_scale = MIN(1, MIN(w / (scrollarea->xSize/scrollarea->childScale), h / (scrollarea->ySize/scrollarea->childScale))) * 0.9f;
		if (min_zoom_scale < scrollarea->minZoomScale)
			min_zoom_scale = scrollarea->minZoomScale;
	}
	else
		min_zoom_scale = 0;

	// Update slider range
	if (scrollarea->zoom_slider) {
		ui_SliderSetRange( scrollarea->zoom_slider, min_zoom_scale, max_zoom_scale, 0 );
	}

	// Slider input overrides whatever we have going on
	if( scrollarea->zoom_slider_queued_child_scale) {
 		F32 zoom = CLAMP( scrollarea->zoom_slider_queued_child_scale, min_zoom_scale, max_zoom_scale );
		scrollarea->zoom_slider_queued_child_scale = 0;
		scrollarea->scrollToTargetZoom = -1;
		return zoom;
	}

	if( scrollarea->draggable && (scrollarea->enableDragOnLeftClick ? !inpLevelPeek(INP_CONTROL) : inpLevelPeek(INP_CONTROL))) {
		// Look for mouse scroll
		if( mouseScrollHit( box ) ) {
			F32 scroll_delta = mouseZ();
			ui_SetFocus( UI_WIDGET( scrollarea ));
			if (scroll_delta != 0)
			{
				scrollarea->scrollToTargetZoomRate = pow(initial_rate, scroll_delta);
			}
			inpScrollHandled();
			scrollarea->scrollToTargetZoom = -1;
		}
		if (scrollarea->scrollToTargetZoom >= 0)
		{
			if (scrollarea->scrollToTargetZoom > scrollarea->childScale && scrollarea->scrollToTargetZoomDir >= 0)
				scrollarea->scrollToTargetZoomRate = scrollarea->defaultZoomRate;
			else if (scrollarea->scrollToTargetZoom < scrollarea->childScale && scrollarea->scrollToTargetZoomDir <= 0)
				scrollarea->scrollToTargetZoomRate = 1.f/scrollarea->defaultZoomRate;
			else
			{
				scrollarea->scrollToTargetZoom = -1;
				scrollarea->scrollToTargetZoomRate = 0;
			}
		}

		// Update zoom scale
		if (scrollarea->scrollToTargetZoomRate > 0 &&
			(scrollarea->scrollToTargetZoomRate > 1.0001f || scrollarea->scrollToTargetZoomRate < 0.9999f))
		{
			targetZoom *= scrollarea->scrollToTargetZoomRate;

			// Snap to 1
			if ((targetZoom > 1 && scrollarea->childScale < 1) ||
				(targetZoom < 1 && scrollarea->childScale > 1))
			{
				targetZoom = 1.f;
				scrollarea->scrollToTargetZoomRate = 1.f;
			}

			scrollarea->scrollToTargetZoomRate = scrollarea->scrollToTargetZoomRate * decay_rate + (1-decay_rate);
		}
	}

	// Clamp zoom to min/max
	if (targetZoom < min_zoom_scale)
	{
		targetZoom = min_zoom_scale;
		scrollarea->scrollToTargetZoom = -1;
	}
	else if (targetZoom > max_zoom_scale)
	{
		targetZoom = max_zoom_scale;
		scrollarea->scrollToTargetZoom = -1;
	}

	return targetZoom;
}

void ui_ScrollAreaTick(UIScrollArea *scrollarea, UI_PARENT_ARGS)
{
	UISkin* skin = UI_GET_SKIN(scrollarea);
	UI_GET_COORDINATES(scrollarea);
	F32 xSize, xPos, ySize, yPos;
	F32 frame_time = g_ui_State.timestep;

	ui_WidgetSizerLayout(UI_WIDGET(scrollarea), w, h);

	if (scrollarea->dragging)
	{
		if (mouseIsDown(scrollarea->dragbutton))
		{
			S32 dx, dy;
			mouseDiff(&dx, &dy);
			if (scrollarea->widget.sb->scrollX)
				scrollarea->widget.sb->xpos -= dx;
			if (scrollarea->widget.sb->scrollY)
				scrollarea->widget.sb->ypos -= dy;
			if( skin->astrScrollareaDragCursor ) {
				ui_SetCursorByName( skin->astrScrollareaDragCursor );
			} else {
				ui_SetCursorForDirection(UIAnyDirection);
			}
			ui_CursorLock();
		}
		else
			scrollarea->dragging = false;
	}
	else if(scrollarea->draggable && mouseDragHit(MS_MID, &box))
	{
		ui_SetFocus( UI_WIDGET( scrollarea ));
		scrollarea->dragbutton = MS_MID;
		scrollarea->dragging = true;

		inpHandled();
	}

	if (scrollarea->dragging)
	{
		// Cancel all other scrolling & zooming
		scrollarea->scrollToTargetWait = 0;
		scrollarea->scrollToTargetRemaining = 0;
	}

	if(scrollarea->enableAutoEdgePan && (ui_DragIsActive() || scrollarea->forceAutoEdgePan))
	{
		int mouseDelta[2];
		int mouse[2];
		int distFromTop;
		int distFromBottom;
		int distFromLeft;
		int distFromRight;
		int minX, minXDir;
		int minY, minYDir;
		
		mousePos( &mouse[0], &mouse[1] );
		mouseDiff( &mouseDelta[0], &mouseDelta[1] );
		
		if(mouseDelta[0] == 0 && mouseDelta[1] == 0) 
		{
			F32 scrollDist;
			distFromTop = mouse[1] - y;
			distFromBottom = y + h - ui_ScrollbarHeight(UI_WIDGET(scrollarea)->sb) * scale - mouse[1];
			distFromLeft = mouse[0] - x;
			distFromRight = x + w - ui_ScrollbarWidth(UI_WIDGET(scrollarea)->sb) * scale - mouse[0];

			minX = minElem2NoNeg( distFromLeft, distFromRight, &minXDir );
			minY = minElem2NoNeg( distFromTop, distFromBottom, &minYDir );
			
			scrollDist = 400 * frame_time;
			if( minX > 0 && minX < MIN(30, w / 10) ) {
				scrollarea->widget.sb->xpos += (minXDir ? 1 : -1) * scrollDist;
			}
			if( minY > 0 && minY < MIN(30, h / 10) ) {
				scrollarea->widget.sb->ypos += (minYDir ? 1 : -1) * scrollDist;
			}

			// Cancel all other scrolling & zooming
			scrollarea->scrollToTargetWait = 0;
			scrollarea->scrollToTargetRemaining = 0;
		}
	}

	if (scrollarea->zoom_slider)
		scrollarea->zoom_slider->widget.tickF(scrollarea->zoom_slider, x, y, w, h, scale );

	ui_ScrollAreaUpdateSize(scrollarea);

	if (scrollarea->widget.sb->scrollX)
		h -= ui_ScrollbarHeight(UI_WIDGET(scrollarea)->sb) * scale;
	if (scrollarea->widget.sb->scrollY)
		w -= ui_ScrollbarWidth(UI_WIDGET(scrollarea)->sb) * scale;
	
	xSize = (scrollarea->widget.sb->scrollX ? MAX(w - ui_ScrollbarWidth(UI_WIDGET(scrollarea)->sb) * scale, scrollarea->xSize) : w);
	xPos = (scrollarea->widget.sb->scrollX ? x - scrollarea->widget.sb->xpos : x);
	ySize = (scrollarea->widget.sb->scrollY ? MAX(h - ui_ScrollbarHeight(UI_WIDGET(scrollarea)->sb) * scale, scrollarea->ySize) : h);
	yPos = (scrollarea->widget.sb->scrollY ? y - scrollarea->widget.sb->ypos : y);

	BuildCBox(&box, x, y, w, h);
	CBoxClipTo(&pBox, &box);
	mouseClipPushRestrict(&box);

	ui_ScrollbarPushState(scrollarea->widget.sb, x, y, w, h, scale, scrollarea->xSize, scrollarea->ySize);
	ui_WidgetGroupTickEx(&scrollarea->widget.children, xPos, yPos, xSize, ySize, scale * scrollarea->childScale, x, y, w, h, scale);
	ui_ScrollbarPopState();

	mouseClipPop();

	if (!scrollarea->dragging)
	{
		Vec2 currentCenterPos, targetCenterPos;
		F32 targetZoom;

		// Calculate current center position (in world coordinates)
		setVec2(currentCenterPos,
			(scrollarea->widget.sb->xpos + (w/2)) / scrollarea->childScale,
			(scrollarea->widget.sb->ypos + (h/2)) / scrollarea->childScale);
		copyVec2(currentCenterPos, targetCenterPos);
		targetZoom = scrollarea->childScale;

		if (scrollarea->scrollToTargetWait <= 0 && scrollarea->scrollToTargetRemaining > 0)
		{
			Vec2 offset = { 0 };
			F32 ratio = saturate(1.0f - pow(0.75, frame_time*30));

			if(scrollarea->scrollToTargetRemaining < frame_time)
				ratio = 1.0f;

			if (!scrollarea->autoScrollCenter)
			{
				setVec2(offset, w/(2*scrollarea->childScale), h/(2*scrollarea->childScale));
			}

			setVec2(targetCenterPos,
				(scrollarea->scrollToTargetPos[0]-offset[0])*ratio + currentCenterPos[0]*(1-ratio),
				(scrollarea->scrollToTargetPos[1]-offset[1])*ratio + currentCenterPos[1]*(1-ratio));

			scrollarea->scrollToTargetRemaining -= frame_time;
		}
		else if (scrollarea->scrollToTargetRemaining <= 0)
		{
			setVec2(scrollarea->scrollToTargetPos, 0, 0);
		}

		targetZoom = ui_ScrollAreaCalculateZoom(scrollarea, x, y, w, h, &box, targetZoom);

		ui_ScrollAreaUpdatePositionAndZoom(scrollarea, x, y, w, h, targetCenterPos, targetZoom);
	}

	if (scrollarea->beforeDragF) {
		scrollarea->beforeDragF( scrollarea, scrollarea->beforeDragData );
	}

	// Check mouse drag after child widgets ticked
	if (scrollarea->draggable && !scrollarea->dragging && mouseDragHit(MS_LEFT, &box))
	{
		ui_SetFocus( scrollarea );
		scrollarea->dragging = true;
		scrollarea->dragbutton = MS_LEFT;
		inpHandled();
	}

	// We must tick the scroll bar *after* the children, in case a pop up box
	// wants to handle some events. However, we don't want the children to
	// be taking over input meant for the scroll bar -- so we need to make
	// sure everything obeys the inputClip area.
	// Also want it after ->dragging check, since it does the clamping of the scroll area
	ui_ScrollbarTick(scrollarea->widget.sb, x, y, w, h, z, scale, scrollarea->xSize, scrollarea->ySize);

	if(scrollarea->scrollToTargetWait > 0)
		scrollarea->scrollToTargetWait--;

	UI_TICK_LATE(scrollarea);
}

static void ui_ScrollbarZoomSliderSetScale(UISlider* slider, bool finished, UIScrollArea* scrollarea)
{
	scrollarea->zoom_slider_queued_child_scale = ui_SliderGetValue(slider);
}

void ui_ScrollAreaInitialize(UIScrollArea *scrollarea, F32 x, F32 y, F32 w, F32 h, F32 xSize, F32 ySize, bool xScroll, bool yScroll)
{
	ui_WidgetInitialize((UIWidget *)scrollarea, ui_ScrollAreaTick, ui_ScrollAreaDraw, ui_ScrollAreaFreeInternal, NULL, NULL);
	ui_WidgetSetPosition((UIWidget *)scrollarea, x, y);
	ui_WidgetSetDimensions((UIWidget *)scrollarea, w, h);
	scrollarea->widget.sb = ui_ScrollbarCreate(xScroll, yScroll);
	scrollarea->xSize = xSize;
	scrollarea->ySize = ySize;
	scrollarea->childScale = 1;
	scrollarea->autosize = false;
	scrollarea->enableAutoEdgePan = true;
	scrollarea->scrollPadding = 0;
	scrollarea->scrollToTargetZoom = -1;
	scrollarea->defaultZoomRate = 1.15f;
}

UIScrollArea *ui_ScrollAreaCreate(F32 x, F32 y, F32 w, F32 h, F32 xSize, F32 ySize, bool xScroll, bool yScroll)
{
	UIScrollArea *scrollarea = (UIScrollArea *)calloc(1, sizeof(UIScrollArea));
	ui_ScrollAreaInitialize(scrollarea, x, y, w, h, xSize, ySize, xScroll, yScroll);
	return scrollarea;
}

void ui_ScrollAreaFreeInternal(UIScrollArea *scrollarea)
{
	if (scrollarea->zoom_slider)
		scrollarea->zoom_slider->widget.freeF(scrollarea->zoom_slider);
	ui_WidgetFreeInternal(UI_WIDGET(scrollarea));
}

void ui_ScrollAreaSetSize(UIScrollArea *scrollarea, F32 xSize, F32 ySize)
{
	scrollarea->xSize = xSize;
	scrollarea->ySize = ySize;
}

void ui_ScrollAreaSetZoomSlider(SA_PARAM_NN_VALID UIScrollArea *scrollarea, bool enable)
{
	if( enable ) {
		if( !scrollarea->zoom_slider ) {
			scrollarea->zoom_slider = ui_SliderCreate(28, 10, 100, 0, 1, 1 );
			ui_SliderSetChangedCallback(scrollarea->zoom_slider, ui_ScrollbarZoomSliderSetScale, scrollarea);
			ui_SliderSetPolicy(scrollarea->zoom_slider, UISliderContinuous);
			ui_SliderSetBias(scrollarea->zoom_slider, 2, 0);
		}
	} else {
		if( scrollarea->zoom_slider ) {
			ui_WidgetQueueFreeAndNull( &scrollarea->zoom_slider );
		}
	}
}

void ui_ScrollAreaSetChildScale(SA_PARAM_NN_VALID UIScrollArea *scrollarea, F32 scale)
{
	scrollarea->childScale = scale;
	
	ui_ScrollAreaUpdateSize(scrollarea);
}

void ui_ScrollAreaAddChild(UIScrollArea *scrollarea, UIAnyWidget *child)
{
	ui_WidgetGroupAdd(&scrollarea->widget.children, (UIWidget *)child);
}

void ui_ScrollAreaRemoveChild(UIScrollArea *scrollarea, UIAnyWidget *child)
{
	ui_WidgetGroupRemove(&scrollarea->widget.children, (UIWidget *)child);
}

void ui_ScrollAreaSetDraggable(SA_PARAM_NN_VALID UIScrollArea *scrollarea, bool draggable)
{
	scrollarea->draggable = draggable;
	UI_WIDGET(scrollarea)->sb->disableScrollWheel = !draggable;
}

void ui_ScrollAreaSetNoCtrlDraggable(SA_PARAM_NN_VALID UIScrollArea *scrollarea, bool draggable)
{
	scrollarea->draggable = draggable;
	scrollarea->enableDragOnLeftClick = draggable;
	UI_WIDGET(scrollarea)->sb->needCtrlToScroll = draggable;
}
