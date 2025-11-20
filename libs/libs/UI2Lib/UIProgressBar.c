/***************************************************************************



***************************************************************************/


#include "Cbox.h"

#include "GfxClipper.h"

#include "UIProgressBar.h"
#include "Color.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

void ui_ProgressBarTroughFillDrawingDescription( UIProgressBar* pbar, UIDrawingDescription* desc )
{
	UISkin* skin = UI_GET_SKIN( pbar );
	if(!skin)
		skin = ui_GetActiveSkin();
		
	if( skin->bUseStyleBorders || skin->bUseTextureAssemblies ) {
		const char* descName = skin->astrProgressBarTrough;
		if( skin->bUseTextureAssemblies && RefSystem_ReferentFromString( "UITextureAssembly", descName )) {
			desc->textureAssemblyName = descName;
		} else {
			desc->styleBorderName = descName;
		}
	} else {
		desc->textureNameUsingLegacyColor = "Default_Capsule_Filled";
	}
}

void ui_ProgressBarFilledFillDrawingDescription( UIProgressBar* pbar, UIDrawingDescription* desc )
{
	UISkin* skin = UI_GET_SKIN( pbar );
	if(!skin)
		skin = ui_GetActiveSkin();
		
	if( skin->bUseStyleBorders || skin->bUseTextureAssemblies ) {
		const char* descName = skin->astrProgressBarFilled;
		if( skin->bUseTextureAssemblies && RefSystem_ReferentFromString( "UITextureAssembly", descName )) {
			desc->textureAssemblyName = descName;
		} else {
			desc->styleBorderName = descName;
		}
	} else {
		desc->textureNameUsingLegacyColor = "Default_Capsule_Filled";
	}
}

void ui_ProgressBarDraw(UIProgressBar *pbar, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(pbar);
	Color full = pbar->widget.color[0], complete = pbar->widget.color[1];
	F32 progX;
	CBox fullBox;
	CBox completeBox;
	UIDrawingDescription troughDesc = { 0 };
	UIDrawingDescription filledDesc = { 0 };
	ui_ProgressBarTroughFillDrawingDescription( pbar, &troughDesc );
	ui_ProgressBarFilledFillDrawingDescription( pbar, &filledDesc );

	progX = MAX( w * pbar->progress, ui_DrawingDescriptionWidth( &filledDesc ));

	CBoxSet( &fullBox, x, y, x + w, y + h );
	CBoxSet( &completeBox, x, y, x + progX, y + h );

	UI_DRAW_EARLY(pbar);
	if (UI_GET_SKIN(pbar))
	{
		full = UI_GET_SKIN(pbar)->trough[0];
		complete = UI_GET_SKIN(pbar)->button[0];
	}

	ui_DrawingDescriptionDraw( &troughDesc, &fullBox, scale, z, 255, full, ColorWhite );
	ui_DrawingDescriptionDraw( &filledDesc, &completeBox, scale, z, 255, full, ColorWhite );
	UI_DRAW_LATE(pbar);
}

UIProgressBar *ui_ProgressBarCreate(F32 x, F32 y, F32 w)
{
	UIProgressBar *pbar = (UIProgressBar *)calloc(1, sizeof(UIProgressBar));
	ui_WidgetInitialize(UI_WIDGET(pbar), NULL, ui_ProgressBarDraw, ui_ProgressBarFreeInternal, NULL, NULL);
	ui_WidgetSetPosition(UI_WIDGET(pbar), x, y);
	ui_WidgetSetDimensions(UI_WIDGET(pbar), w, UI_GET_SKIN( pbar )->iProgressBarHeight );
	return pbar;
}

void ui_ProgressBarFreeInternal(UIProgressBar *pbar)
{
	ui_WidgetFreeInternal(UI_WIDGET(pbar));
}

void ui_ProgressBarSet(UIProgressBar *pbar, F32 progress)
{
	pbar->progress = CLAMPF32(progress, 0.f, 1.f);
}
