#ifndef NO_EDITORS

#include "ProgressOverlay.h"
#include "EditorManager.h"
#include "GfxSpriteText.h"
#include "Color.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

typedef struct ProgressOverlay
{
	U32 id;
	char label[64];
	S32 current_step;
	S32 total_steps;
	bool done;
	UIProgressBar *progress_widget;
	F32 alpha;
} ProgressOverlay;

static ProgressOverlay **progress_events = NULL;

U32 progressOverlayCreate(S32 total_steps, const char *label)
{
	static U32 overlay_id = 1;
	ProgressOverlay *overlay;

	overlay = calloc(1, sizeof(ProgressOverlay));
	overlay->id = overlay_id++;
	strcpy(overlay->label, label);
	overlay->total_steps = total_steps;
	overlay->alpha = 1.f;

	overlay->progress_widget = ui_ProgressBarCreate(0, 0, 200);
	overlay->progress_widget->widget.pOverrideSkin = NULL;
	overlay->progress_widget->widget.width = 300;
	setVec4(overlay->progress_widget->widget.color[0].rgba, 0xff, 0, 0, 0x60);
	setVec4(overlay->progress_widget->widget.color[1].rgba, 0, 0, 0xff, 0x60);
	ui_ProgressBarSet(overlay->progress_widget, 0);

	eaPush(&progress_events, overlay);

	return overlay->id;
}

S32 progressOverlayGetSize(U32 id)
{
	int i;
	for (i = 0; i < eaSize(&progress_events); i++)
		if (progress_events[i]->id == id)
		{
			ProgressOverlay *overlay = progress_events[i];
			return overlay->total_steps;
		}
	return 0;
}

void progressOverlaySetSize(U32 id, S32 total_steps)
{
	int i;
	for (i = 0; i < eaSize(&progress_events); i++)
		if (progress_events[i]->id == id)
		{
			ProgressOverlay *overlay = progress_events[i];
			overlay->total_steps = total_steps;
			if(overlay->total_steps){
				ui_ProgressBarSet(overlay->progress_widget, CLAMP(((F32)overlay->current_step)/overlay->total_steps, 0.f, 1.f));
			}else{
				ui_ProgressBarSet(overlay->progress_widget, 0.f);
			}
			//printf("PR %s: %d/%d\n", overlay->label, overlay->current_step, overlay->total_steps);
			return;
		}
}

static void terEdProgressBarFree(ProgressOverlay *overlay)
{
	ui_WidgetQueueFree(&overlay->progress_widget->widget);
	SAFE_FREE(overlay);
}

void progressOverlaySetValue(U32 id, S32 value)
{
	int i;
	for (i = 0; i < eaSize(&progress_events); i++)
		if (progress_events[i]->id == id)
		{
			ProgressOverlay *overlay = progress_events[i];
			overlay->current_step = value;
			if(overlay->total_steps){
				ui_ProgressBarSet(overlay->progress_widget, CLAMP(((F32)overlay->current_step)/overlay->total_steps, 0.f, 1.f));
			}else{
				ui_ProgressBarSet(overlay->progress_widget, 0.f);
			}
			//printf("PR %s: %d/%d\n", overlay->label, overlay->current_step, overlay->total_steps);
			return;
		}
}

void progressOverlayIncrement(U32 id, S32 steps)
{
	int i;
	for (i = 0; i < eaSize(&progress_events); i++)
		if (progress_events[i]->id == id)
		{
			ProgressOverlay *overlay = progress_events[i];
			overlay->current_step += steps;
			if(overlay->total_steps){
				ui_ProgressBarSet(overlay->progress_widget, CLAMP(((F32)overlay->current_step)/overlay->total_steps, 0.f, 1.f));
			}else{
				ui_ProgressBarSet(overlay->progress_widget, 0.f);
			}
			//printf("PR %s: %d/%d\n", overlay->label, overlay->current_step, overlay->total_steps);
			return;
		}
}

void progressOverlayDraw()
{
	int i;
	F32 w, h, x, y;
	emGetCanvasSize(&x, &y, &w, &h);
	ui_StyleFontUse(NULL, false, kWidgetModifier_None);
	gfxfont_SetColor(ColorWhite, ColorWhite);
	for (i = 0; i < eaSize(&progress_events); i++)
	{
		ProgressOverlay *overlay = progress_events[i];
		if (overlay->done)
		{
			overlay->alpha -= 0.05f;
			setVec4(overlay->progress_widget->widget.color[0].rgba, 0, 0, 0, 0);
			setVec4(overlay->progress_widget->widget.color[1].rgba, 0, 0xff, 0, overlay->alpha*0xff);
		}
		if (overlay->alpha > 0 && i < 6)
		{
			ui_ProgressBarDraw(overlay->progress_widget, (x+w/2)-150, y+16*(i+2), 300, 16, 1.f);
			gfxfont_Printf((x+w/2)-150+5, y+16*(i+2)+8, (++g_ui_State.drawZ), 1.f, 1.f, CENTER_Y, 
				"%s (%d/%d)", overlay->label, overlay->current_step, overlay->total_steps);
		}
		else if (i == 6)
		{
			gfxfont_Printf((x+w/2)-150+5, y+16*(i+2)+8, (++g_ui_State.drawZ), 1.f, 1.f, CENTER_Y, 
				"... (%d more)", eaSize(&progress_events)-6);
		}
	}
	for (i = 0; i < eaSize(&progress_events); )
	{
		ProgressOverlay *overlay = progress_events[i];
		if (overlay->done && overlay->alpha <= 0.f)
		{
			terEdProgressBarFree(overlay);
			eaRemove(&progress_events, i);
		}
		else
		{
			i++;
		}
	}
}

void progressOverlayRelease(U32 id)
{
	int i;
	for (i = 0; i < eaSize(&progress_events); i++)
		if (progress_events[i]->id == id)
		{
			ProgressOverlay *overlay = progress_events[i];
			overlay->done = true;
			ui_ProgressBarSet(overlay->progress_widget, 1.f);
			//printf("PR %s: DONE\n", progress_events[i]->label);
			return;
		}
}

#endif // NO_EDITORS
