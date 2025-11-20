/***************************************************************************



***************************************************************************/


#include "GfxSpriteText.h"
#include "inputMouse.h"
#include "GfxSprite.h"
#include "Message.h"

#include "UILabel.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

UIStyleFont *ui_LabelGetFont(UILabel* label)
{
	UISkin* skin = UI_GET_SKIN( label );
	UIStyleFont* font = NULL;

	UIWidget* widget = UI_WIDGET( label );

	if( GET_REF( widget->hOverrideFont )) {
		return GET_REF( widget->hOverrideFont );
	}
	
	if( !ui_IsActive( widget )) {
		font = GET_REF( skin->hLabelFontDisabled );
	} else {
		font = GET_REF( skin->hLabelFont );
	}

	if( !font ) {
		font = ui_WidgetGetFont( UI_WIDGET( label ));
	}
	
	return font;
}

static void ui_LabelFillDrawingDescription( UILabel* label, UIDrawingDescription* desc )
{
	UISkin* skin = UI_GET_SKIN( label );
		
	if( skin->bUseStyleBorders || skin->bUseTextureAssemblies ) {
		const char* descName = skin->astrLabelStyle;
		if( skin->bUseTextureAssemblies && RefSystem_ReferentFromString( "UITextureAssembly", descName )) {
			desc->textureAssemblyName = descName;
		} else {
			desc->styleBorderName = descName;
		}
	} else if( label->bOpaque ) {
		desc->textureNameUsingLegacyColor = "white";
	} else {
		// nothing!
	}
}

static void ui_LabelUpdateDimensions(UILabel *label)
{
	CBox box = { 0 };
	UIStyleFont *font = ui_LabelGetFont(label);
	UIDrawingDescription desc = { 0 };
	ui_LabelFillDrawingDescription( label, &desc );
		
	box.hx = ui_StyleFontWidth(font, label->widget.scale, ui_WidgetGetText(&label->widget)) / label->widget.scale;
	box.hy = ui_StyleFontLineHeight(font, 1.f);
	ui_DrawingDescriptionOuterBox( &desc, &box, 1 );

	if( label->bRotateCCW ) {
		if (UI_WIDGET(label)->heightUnit == UIUnitFixed) {
			ui_WidgetSetWidth( UI_WIDGET( label ), CBoxHeight( &box ));
		}
		if (UI_WIDGET(label)->widthUnit == UIUnitFixed && !label->bNoAutosizeWidth) {
			ui_WidgetSetHeight( UI_WIDGET( label ), CBoxWidth( &box ));
		}
	} else {
		if (UI_WIDGET(label)->heightUnit == UIUnitFixed) {
			ui_WidgetSetHeight( UI_WIDGET( label ), CBoxHeight( &box ));
		}
		if (UI_WIDGET(label)->widthUnit == UIUnitFixed && !label->bNoAutosizeWidth) {
			ui_WidgetSetWidth( UI_WIDGET( label ), CBoxWidth( &box ));
		}
	}
}

void ui_LabelUpdateDimensionsForWidth(UILabel *pLabel, F32 fWidth)
{
	pLabel->pLastFont = ui_LabelGetFont(pLabel);
	pLabel->fLastWidth = fWidth;
	if (!pLabel->bWrap)
		ui_LabelUpdateDimensions(pLabel);
	else
	{
		CBox box = { 0 };
		const char* widgetText = ui_WidgetGetText(UI_WIDGET(pLabel));
		UIStyleFont *pFont = pLabel->pLastFont;
		UIDrawingDescription desc = { 0 };
		float fLines;
		ui_LabelFillDrawingDescription( pLabel, &desc );

		ui_StyleFontUse(pFont, false, UI_WIDGET(pLabel)->state);
		if( widgetText ) {
			fLines = gfxfont_PrintWrapped(0, 0, 0, fWidth - ui_DrawingDescriptionWidth( &desc ), 1.f, 1.f, 0, false, widgetText);
		} else {
			fLines = 1;
		}
		box.hx = fWidth;
		box.hy = fLines * ui_StyleFontLineHeight(pFont, 1.f);
		ui_DrawingDescriptionOuterBox( &desc, &box, 1 );
		
		if (UI_WIDGET(pLabel)->heightUnit == UIUnitFixed)
			ui_WidgetSetHeight(UI_WIDGET(pLabel), CBoxHeight( &box ));
		if (UI_WIDGET(pLabel)->widthUnit == UIUnitFixed)
			ui_WidgetSetWidth(UI_WIDGET(pLabel), CBoxWidth( &box ));
	}
}

void ui_LabelDraw(UILabel *label, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(label);
	UIStyleFont *font = ui_LabelGetFont(label);
	const char* widgetText = ui_WidgetGetText( UI_WIDGET( label ));
	UIDrawingDescription desc = { 0 };
	ui_LabelFillDrawingDescription( label, &desc );

	if (  (font != label->pLastFont || (label->fLastWidth != w / scale && label->bWrap))
		  && !label->bNoAutosizeWidth)
		ui_LabelUpdateDimensionsForWidth(label, w / scale);

	UI_DRAW_EARLY(label);
	ui_DrawingDescriptionDraw( &desc, &box, scale, z, 255, UI_GET_SKIN(label)->background[0], ColorBlack );
	ui_DrawingDescriptionInnerBoxCoords( &desc, &x, &y, &w, &h, scale );
	
	ui_StyleFontUse(font, false, UI_WIDGET(label)->state);
	if (label->bUseWidgetColor)
	{
		gfxfont_SetColorRGBA4((U32*)UI_WIDGET(label)->color);
	}
	if (widgetText) {
		if( label->bRotateCCW ) {
			CBox drawBox;
			BuildCBox( &drawBox, x, y, w, h );
			assert( !label->bWrap );
			ui_DrawTextInBoxSingleLineRotatedCCW( font, widgetText, label->bNoAutosizeWidth, &drawBox, z + 0.1, scale, label->textFrom );
		} else {
			if (label->bWrap) {
				gfxfont_PrintWrapped(x, y + ui_StyleFontLineHeight(font, scale) / 2, z + 0.1, w, scale, scale, CENTER_Y, true, widgetText);
			} else {
				CBox drawBox;
				BuildCBox( &drawBox, x, y, w, h );
				ui_DrawTextInBoxSingleLine( font, widgetText, label->bNoAutosizeWidth, &drawBox, z + 0.1, scale, label->textFrom );
			}
		}
	}
	UI_DRAW_LATE(label);
}

void ui_LabelSetText(UILabel *label, const char *text)
{
	ui_WidgetSetTextString(UI_WIDGET(label), text);
	label->fLastWidth = -1;
	if (!label->bWrap)
		ui_LabelUpdateDimensions(label);
}

void ui_LabelSetMessage(UILabel *label, const char *message_key)
{
	ui_WidgetSetTextMessage(UI_WIDGET(label), message_key);
	label->fLastWidth = -1;
	if (!label->bWrap)
		ui_LabelUpdateDimensions(label);
}

UILabel *ui_LabelCreate(const char *text, F32 x, F32 y)
{
	UILabel *label = (UILabel *)calloc(1, sizeof(UILabel));
	ui_LabelInitialize(label, text, x, y);
	return label;
}

UILabel *ui_LabelCreateWithMessage(const char *message_key, F32 x, F32 y)
{
	UILabel *label = (UILabel *)calloc(1, sizeof(UILabel));
	ui_LabelInitializeWithMessage(label, message_key, x, y);
	return label;
}

void ui_LabelInitialize(UILabel *label, const char *text, F32 x, F32 y)
{
	ui_WidgetInitialize(UI_WIDGET(label), NULL, ui_LabelDraw, ui_LabelFreeInternal, NULL, NULL);
	ui_WidgetSetPosition(UI_WIDGET(label), x, y);
	ui_LabelSetText(label, text);
	label->textFrom = UITopLeft;
}

void ui_LabelInitializeWithMessage(UILabel *label, const char *message_key, F32 x, F32 y)
{
	ui_WidgetInitialize(UI_WIDGET(label), NULL, ui_LabelDraw, ui_LabelFreeInternal, NULL, NULL);
	ui_WidgetSetPosition(UI_WIDGET(label), x, y);
	ui_LabelSetMessage(label, message_key);
	label->textFrom = UITopLeft;
}

void ui_LabelSetFont(UILabel *label, UIStyleFont *font)
{
	ui_WidgetSetFont(UI_WIDGET(label), font ? font->pchName : NULL);
	label->fLastWidth = -1;
	if (!label->bWrap)
		ui_LabelUpdateDimensions(label);
}

void ui_LabelFreeInternal(UILabel *label)
{
	ui_WidgetFreeInternal(UI_WIDGET(label));
}

void ui_LabelSetWordWrap(UILabel *pLabel, bool bWrap)
{
	pLabel->bWrap = bWrap;
	pLabel->fLastWidth = -1;
	if (!pLabel->bWrap)
		ui_LabelUpdateDimensions(pLabel);
}

void ui_LabelSetWidthNoAutosize(SA_PARAM_NN_VALID UILabel *pLabel, F32 w, UIUnitType wUnit)
{
	ui_WidgetSetWidthEx(UI_WIDGET(pLabel), w, wUnit);
	pLabel->bNoAutosizeWidth = true;
}

void ui_LabelResize(SA_PARAM_NN_VALID UILabel* pLabel)
{
	pLabel->bNoAutosizeWidth = false;
	ui_WidgetSetWidth(UI_WIDGET(pLabel), 1);
	ui_LabelUpdateDimensions(pLabel);
}

void ui_LabelForceAutosize(SA_PARAM_NN_VALID UILabel* pLabel)
{
	ui_LabelResize( pLabel );
}

void ui_LabelNonDefaultTick(UILabel *pLabel, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(pLabel);
	UI_TICK_EARLY(pLabel, false, true);
	UI_TICK_LATE(pLabel);
}

void ui_LabelEnableTooltips(UILabel *pLabel)
{
	pLabel->widget.tickF = ui_LabelNonDefaultTick;
}
