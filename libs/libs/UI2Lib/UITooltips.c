/***************************************************************************



***************************************************************************/

#include "file.h"
#include "earray.h"

#include "GfxPrimitive.h"
#include "GfxTexAtlas.h"
#include "GfxSprite.h"
#include "StringUtil.h"

#include "inputLib.h"

#include "smf_render.h"

#include "UITooltips.h"

static UIWidget *s_active;
static F32 s_duration;
static F32 s_x, s_y, s_top, s_bottom;
static bool s_ticked;
static bool s_noTooltipDelay;
static UIWidget s_widgetForJustText = { 0 };

#define MAX_TOOLTIP_WIDTH 800

void ui_TooltipsSetActive(UIWidget *widget, F32 wtop, F32 wbottom)
{
	const char* tooltipString = ui_WidgetGetTooltip(widget);
	
	if ((s_ticked && s_active != widget) || !widget)
		// Only one widget gets to be active per frame.
		return;
	if ( ((!nullStr(tooltipString) && !StringIsAllWhiteSpace(tooltipString)))
		 || (widget->name && inpLevelPeek(INP_CONTROL) && inpLevelPeek(INP_ALT) && isDevelopmentMode()))
	{
		s_ticked = true;
		s_top = wtop;
		s_bottom = wbottom;
		if (widget != s_active)
		{
			s_active = widget;
			if (widget->group != s_active->group)
				s_duration = 0;
			s_x = s_y = 0;
		}
	}
}

void ui_TooltipsSetActiveText(const char* text, F32 wtop, F32 wbottom)
{
	ui_WidgetSetTooltipString( &s_widgetForJustText, text );
	ui_TooltipsSetActive( &s_widgetForJustText, wtop, wbottom );
}

static void ui_TooltipsFillDrawingDescription( UIDrawingDescription* desc )
{
	UISkin* skin = ui_GetActiveSkin();

	if( skin->bUseStyleBorders || skin->bUseTextureAssemblies ) {
		const char* descName = skin->astrTooltipStyle;
		if( skin->bUseTextureAssemblies && RefSystem_ReferentFromString( "UITextureAssembly", descName )) {
			desc->textureAssemblyName = descName;
		} else {
			desc->styleBorderName = descName;
		}
	} else {
		desc->textureAssemblyName = "Default_MiniFrame_Filled";
	}
}

extern void ui_SMFViewTick(UISMFView *view, UI_PARENT_ARGS);

void ui_TooltipsDisplay(void)
{
	const char* tooltipText = ui_WidgetGetTooltip( s_active );
	static SMFBlock *s_pSMF = NULL;

	UIDrawingDescription desc = { 0 };
	float tooltipWidth;
	float tooltipHeight;
	CBox tooltipBox;
	char tooltipAlpha;

	if( !tooltipText ) {
		return;
	}

	ui_TooltipsFillDrawingDescription( &desc );

	{
		F32 fTime;
		if( s_noTooltipDelay ) {
			fTime = s_duration / UI_TOOLTIP_FADE_DELAY;
		} else {
			fTime = (s_duration - UI_TOOLTIP_DISPLAY_DELAY) / UI_TOOLTIP_FADE_DELAY;
		}
		tooltipAlpha = CLAMP(fTime * 255, 0, 255);
	}

	// Calculate content size (tooltipWidth, tooltipHeight)
	{
		if (!s_pSMF)
			s_pSMF = smfblock_Create();

		tooltipWidth = min(MAX_TOOLTIP_WIDTH, ui_StyleFontWidth(NULL, 1.f, tooltipText));
		smf_ParseAndFormat(s_pSMF, tooltipText, s_x + UI_STEP, s_y + UI_STEP, g_ui_State.drawZ, MAX_TOOLTIP_WIDTH, 100000, false, false, false, NULL);
		tooltipWidth = min(tooltipWidth, smfblock_GetWidth(s_pSMF));
		tooltipHeight = smfblock_GetHeight(s_pSMF);
	}

	// Position the tooltip box
	tooltipWidth += ui_DrawingDescriptionWidth( &desc );
	tooltipHeight += ui_DrawingDescriptionHeight( &desc );
	if( s_x <= 0 ) {
		s_x = MAX( 1, MIN( g_ui_State.mouseX, g_ui_State.screenWidth - tooltipWidth ));
		
		s_y = s_bottom + 10;
		if( s_y + tooltipHeight > g_ui_State.screenHeight ) {
			s_y = s_top - 10 - tooltipHeight;
		}
	}

	// Draw the tooltip box
	BuildCBox( &tooltipBox, s_x, s_y, tooltipWidth, tooltipHeight );
	ui_DrawingDescriptionDraw( &desc, &tooltipBox, 1, UI_GET_Z(), tooltipAlpha, ColorWhite, ColorWhite );

	// Draw the contents
	ui_DrawingDescriptionInnerBox( &desc, &tooltipBox, 1 );
	smf_ParseAndDisplay(s_pSMF, tooltipText, tooltipBox.lx, tooltipBox.ly, UI_GET_Z(), CBoxWidth( &tooltipBox ), CBoxHeight( &tooltipBox ), false, false, false, NULL, tooltipAlpha, NULL, NULL);
}

void ui_TooltipsTick(void)
{
	const char* tooltipString;
	if (!s_ticked)
	{
		s_active = NULL;
		s_duration = 0;
		s_x = 0;
		s_y = 0;
	}
	tooltipString = ui_WidgetGetTooltip(s_active);
	if (s_active && (tooltipString || (s_active->name && isDevelopmentMode())))
	{
		s_duration += g_ui_State.timestep;
		if (s_duration > UI_TOOLTIP_DISPLAY_DELAY || s_noTooltipDelay)
			ui_TooltipsDisplay();
	}
	s_ticked = false;
}

void ui_TooltipsClearActive( void )
{
	// So no other widgets pop up a tooltip this frame
	s_ticked = true;
	s_active = NULL;
	s_x = s_y = 0;
}

void ui_TooltipsSetNoDelay( bool noDelay )
{
	s_noTooltipDelay = noDelay;
}
