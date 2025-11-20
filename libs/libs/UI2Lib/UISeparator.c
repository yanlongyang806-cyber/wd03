/***************************************************************************



***************************************************************************/

#include "Color.h"

#include "GfxPrimitive.h"
#include "GfxClipper.h"

#include "UISeparator.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

static void ui_SeparatorFillDrawingDescription( UISeparator* sep, UIDrawingDescription* desc )
{
	UISkin* skin = UI_GET_SKIN( sep );

	if( skin->bUseStyleBorders || skin->bUseTextureAssemblies ) {
		const char* descName = "";

		if( sep->orientation == UIHorizontal ) {
			descName = skin->astrHorizontalSeparator;
		} else {
			descName = skin->astrVerticalSeparator;
		}

		if( skin->bUseTextureAssemblies && RefSystem_ReferentFromString( "UITextureAssembly", descName )) {
			desc->textureAssemblyName = descName;
		} else {
			desc->styleBorderName = descName;
		}
	} else {
		if( sep->orientation == UIHorizontal ) {
			desc->horzLineUsingLegacyColor = true;
		} else {
			desc->vertLineUsingLegacyColor = true;
		}
	}
}

UISeparator *ui_SeparatorCreate(UIDirection orientation)
{
	UISeparator *sep = (UISeparator *)calloc(1, sizeof(UISeparator));

	sep->orientation = orientation;
	if(sep->orientation != UIHorizontal && sep->orientation != UIVertical)
		sep->orientation = UIHorizontal;

	ui_WidgetInitialize(UI_WIDGET(sep), NULL, ui_SeparatorDraw, ui_SeparatorFreeInternal, NULL, NULL);
	ui_WidgetSetPosition(UI_WIDGET(sep), 0, 0);

	ui_SeparatorResize(sep);
	return sep;
}

void ui_SeparatorFreeInternal(UISeparator *sep)
{
	ui_WidgetFreeInternal(UI_WIDGET(sep));
}

void ui_SeparatorDraw(UISeparator *sep, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(sep);
	UIDrawingDescription desc = { 0 };
	Color c = UI_GET_SKIN(sep)->trough[0];
	UI_DRAW_EARLY(sep);

	ui_SeparatorFillDrawingDescription( sep, &desc );
	
	ui_DrawingDescriptionDraw( &desc, &box, scale, z, 255, c, ColorBlack );
	UI_DRAW_LATE(sep);
}

void ui_SeparatorResize(UISeparator* sep)
{
	UIDrawingDescription desc = { 0 };
	ui_SeparatorFillDrawingDescription( sep, &desc );
		
	if(sep->orientation == UIHorizontal) {
		ui_WidgetSetDimensionsEx(UI_WIDGET(sep), 1.0, ui_DrawingDescriptionHeight( &desc ), UIUnitPercentage, UIUnitFixed);
	} else {
		ui_WidgetSetDimensionsEx(UI_WIDGET(sep), ui_DrawingDescriptionWidth( &desc ), 1.0, UIUnitFixed, UIUnitPercentage);
	}
}
