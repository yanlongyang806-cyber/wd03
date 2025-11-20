/***************************************************************************



***************************************************************************/


#include "GfxClipper.h"

#include "sm_parser.h"

#include "SMF_Render.h"
#include "UISMFView.h"
#include "inputMouse.h"
#include "Color.h"
#include "GfxSprite.h"
#include "smf/smf_format.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

void ui_SMFViewTick(UISMFView *view, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(view);
	
	UI_TICK_EARLY(view, false, false);

	if (view->drawBackground)
	{
		if (mouseCollision(&box))
			inpHandled();

		// modify X, Y, W, H because of padding
		x += UI_STEP_SC;
		w -= 2 * UI_STEP_SC; 
	}

	if (!view->pAttribs)
		view->pAttribs = smf_TextAttribsFromFont(NULL, ui_WidgetGetFont( UI_WIDGET( view )));

	view->pAttribs->ppScale = U32_TO_PTR((U32)(scale * SMF_FONT_SCALE));

	if (view->pTree)
	{
		S32 iHeight = smf_ParseAndFormat(view->pTree, NULL, x, y, z, w, h, false, false, false, view->pAttribs);
		if (UI_WIDGET(view)->heightUnit == UIUnitFixed)
			UI_WIDGET(view)->height = iHeight / scale + UI_WIDGET(view)->topPad + UI_WIDGET(view)->bottomPad;
		smf_Interact(view->pTree, NULL, x, y, NULL, NULL, NULL);

		if (view->reflowF)
			view->reflowF(view, view->reflowData);
	}
}

void ui_SMFViewDraw(UISMFView *view, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(view);

	UI_DRAW_EARLY(view);

	if (view->drawBackground)
	{
		Color c;
		Color border;
		
		if (UI_GET_SKIN(view))
		{
			if (ui_IsFocused(UI_WIDGET(view)))
			{
				if (!ui_IsActive(UI_WIDGET(view)))			
					c = UI_GET_SKIN(view)->entry[0];
				else if (ui_IsChanged(UI_WIDGET(view)))
					c = ColorLerp(UI_GET_SKIN(view)->entry[1], UI_GET_SKIN(view)->entry[3], 0.5);
				else if (ui_IsInherited(UI_WIDGET(view)))
					c = ColorLerp(UI_GET_SKIN(view)->entry[1], UI_GET_SKIN(view)->entry[4], 0.5);
				else
					c = UI_GET_SKIN(view)->entry[1];
			}
			else if (!ui_IsActive(UI_WIDGET(view)))
				c = UI_GET_SKIN(view)->entry[2];
			else if (ui_IsChanged(UI_WIDGET(view)))
				c = UI_GET_SKIN(view)->entry[3];
			else if (ui_IsInherited(UI_WIDGET(view)))
				c = UI_GET_SKIN(view)->entry[4];
			else
				c = UI_GET_SKIN(view)->entry[0];
			border = UI_GET_SKIN(view)->thinBorder[0];
		}
		else
		{
			c = view->widget.color[0];
			border = ColorBlack;
		}
		
		ui_DrawOutline(&box, z + 0.001f, border, scale);
		display_sprite_box((g_ui_Tex.white), &box, z, RGBAFromColor(c));

		// modify X, Y, W, H because of padding
		x += UI_STEP_SC;
		w -= 2 * UI_STEP_SC;
	}
	
	if (view->pTree)
	{
		S32 iHeight = smf_ParseAndDisplay(view->pTree, NULL, x, y, z, w, h, false, false, false, view->pAttribs, view->maxAlpha, NULL, NULL);
		if (UI_WIDGET(view)->heightUnit == UIUnitFixed)
			ui_WidgetSetHeight(UI_WIDGET(view), iHeight / scale + UI_WIDGET(view)->topPad + UI_WIDGET(view)->bottomPad);
	}
	UI_DRAW_LATE(view);
}

void ui_SMFViewSetText(UISMFView *view, const char *text, TextAttribs *state)
{
	if (view->pTree)
		smfblock_Destroy(view->pTree);

	view->pTree	= smfblock_Create();
	smf_ParseAndFormat(view->pTree, text ? text : "", 0, 0, 0, 0, 0, true, false, false, state);
}

void ui_SMFViewSetDrawBackground(SA_PARAM_NN_VALID UISMFView *view, bool drawBackground)
{
	view->drawBackground = drawBackground;
}

bool ui_SMFViewReflow(UISMFView *view, F32 w)
{
	if (view->pTree)
	{
		S32 iHeight = smf_ParseAndFormat(view->pTree, NULL, 0, 0, 0, w, 100000, false, false, false, view->pAttribs);
		F32 scale = view->pAttribs ? PTR_TO_U32(view->pAttribs->ppScale) / SMF_FONT_SCALE : 1;

		if (view->widget.heightUnit == UIUnitFixed)
			view->widget.height = iHeight / scale;
		if (view->reflowF)
			view->reflowF(view, view->reflowData);
		return true;
	}
	return false;
}

UISMFView *ui_SMFViewCreate(U32 x, U32 y, U32 w, U32 h)
{
	UISMFView *view = (UISMFView *)calloc(1, sizeof(UISMFView));
	ui_WidgetInitialize(UI_WIDGET(view), ui_SMFViewTick, ui_SMFViewDraw, ui_SMFViewFreeInternal, NULL, NULL);
	ui_WidgetSetPosition(UI_WIDGET(view), x, y);
	ui_WidgetSetDimensions(UI_WIDGET(view), w, h);
	view->maxAlpha = 255;
	return view;
}

void ui_SMFViewFreeInternal(UISMFView *view)
{
	SAFE_FREE(view->pAttribs);
	if (view->pTree)
		smfblock_Destroy(view->pTree);
	ui_WidgetFreeInternal(UI_WIDGET(view));
}

void ui_SMFViewSetMaxAlpha(UISMFView *view, U8 cAlpha)
{
	view->maxAlpha = cAlpha;
}

void ui_SMFViewSetReflowCallback(UISMFView *view, UIActivationFunc reflowF, UserData reflowData)
{
	view->reflowF = reflowF;
	view->reflowData = reflowData;
}

void ui_SMFViewUpdateDimensions(UISMFView *view) // Updates widget->width, widget->height fields
{
	int width=0, height=0;
	assert(view->pTree);
	height = smf_ParseAndFormat(view->pTree, NULL, 0, 0, 0, 0x7FFF, 0x7FFF, false, false, false, NULL);
	if (view->pTree->pBlock)
		width = view->pTree->pBlock->pos.iMinWidth;
	ui_WidgetSetDimensions(UI_WIDGET(view), width, height);
}

F32 ui_SMFViewGetHeight(UISMFView *pView)
{
	F32 fHeight = 0;
	if (pView->pTree->pBlock)
		fHeight = pView->pTree->pBlock->pos.iHeight;
	return fHeight + UI_WIDGET(pView)->topPad + UI_WIDGET(pView)->bottomPad;
}
