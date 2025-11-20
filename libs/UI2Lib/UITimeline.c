/***************************************************************************



***************************************************************************/

#include "UITimeline.h"

#include "textparser.h"
#include "inputLib.h"
#include "GfxSprite.h"
#include "GfxPrimitive.h"
#include "GfxSpriteText.h"
#include "UIScrollBar.h"

#include "inputMouse.h"
#include "inputText.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

#define UI_TIMELINE_TIME_TO_SCREEN(x) ((x)/10.0f)
#define UI_TIMELINE_SCREEN_TO_TIME(x) ((x)*10.0f)

#define SCRUB_HANDLE_SIZE 25.0f
#define KEY_FRAME_SIZE 8.0f
#define MIN_LENGTH_SIZE 10.0f

void ui_TimelineFreeLink(UITimelineLink *link);
void ui_TimelineFreeGroup(UITimelineGroup *group);

//////////////////////////////////////////////////////////////////////////
// Helpers
//////////////////////////////////////////////////////////////////////////

static F32 ui_TimelineGetTimeFromX(int x, F32 time_scale, F32 scale, F32 x_start, F32 scroll_x)
{
	return (UI_TIMELINE_SCREEN_TO_TIME((x + scroll_x - x_start)/scale)/time_scale);
}

static F32 ui_TimelineGetXFromTime(int time, F32 time_scale, F32 scale, F32 x_start, F32 scroll_x)
{
	return (x_start + UI_TIMELINE_TIME_TO_SCREEN(time*time_scale)*scale - scroll_x);
}

//////////////////////////////////////////////////////////////////////////
// Time Line Key Frame
//////////////////////////////////////////////////////////////////////////

UITimelineKeyFrame *ui_TimelineKeyFrameCreate()
{
	UITimelineKeyFrame *frame = calloc(1, sizeof(*frame));

	frame->point_scale = 1.0f;
	frame->color = ColorBlack;
	frame->allow_resize = false;

	return frame;
}

void ui_TimelineKeyFrameFree(UITimelineKeyFrame *frame)
{
	UITimelineTrack *track = frame->track;
	UITimeline *timeline = track->timeline;
	ui_TimelineTrackRemoveFrame(track, frame);
	ui_TimelineKeyFrameUnlink(frame);
	ui_TimelineKeyFrameSetSelected(frame, false);
	free(frame);
}

void ui_TimelineKeyFrameSetPreChangedCallback(UITimelineKeyFrame *frame, UIPreActivationFunc func, UserData data)
{
	frame->pre_changed_func = func;
	frame->pre_changed_data = data;
}

void ui_TimelineKeyFrameSetChangedCallback(UITimelineKeyFrame *frame, UIActivationFunc func, UserData data)
{
	frame->changed_func = func;
	frame->changed_data = data;
}

void ui_TimelineKeyFrameSetRightClickCallback(UITimelineKeyFrame *frame, UIActivationFunc func, UserData data)
{
	frame->rightclick_func = func;
	frame->rightclick_data = data;
}

static void ui_TimelineKeyFrameDraw(UITimelineKeyFrame *frame, F32 x, F32 y, F32 w, F32 h, F32 z, F32 scale, bool point)
{
	UITimelineTrack *track = frame->track;
	UITimeline *timeline = track->timeline;
	UISkin *skin = UI_GET_SKIN(timeline);
	Color color = frame->color;
	U8 alpha = 175;
	CBox draw_box;

	assert(track && timeline);

	if(frame->hovering) {
		z += 0.3f;
		alpha = 255;
	}

	if(point) {
		F32 half_size = (KEY_FRAME_SIZE/2.0f)*scale;
		BuildCBox(&draw_box, x-half_size, y+h/2.0f-half_size, KEY_FRAME_SIZE*scale, KEY_FRAME_SIZE*scale);
		if(frame->hovering)
			color = ColorInvert(color);
		display_sprite_box(g_ui_Tex.white, &draw_box, z+0.06f, RGBAFromColor(color));
	} else {
		BuildCBox(&draw_box, x, y+h/10.0f, MAX(w, MIN_LENGTH_SIZE*scale), h*8.0f/10.0f);
		if(frame->hovering)
			color = ColorInvert(color);
		ui_StyleBorderDraw(NULL, &draw_box, RGBAFromColor(color), RGBAFromColor(color), z+0.06f, scale, alpha);
	}

	draw_box.ly = y;
	draw_box.hy = y+h;

	if(frame->selected)
	{
		Color sel_color = skin->background[1];
		ui_StyleBorderDraw(NULL, &draw_box, RGBAFromColor(sel_color), RGBAFromColor(sel_color), z+0.03f, scale, 255);
	}
}

void ui_TimelineKeyFrameUnlink(UITimelineKeyFrame *frame)
{
	int i;
	UITimelineTrack *track = frame->track;
	UITimeline *timeline = track->timeline;
	for ( i=0; i < eaSize(&timeline->links); i++ ) {
		UITimelineLink *link = timeline->links[i];
		int idx = eaFindAndRemove(&link->items, frame);
		if(idx >= 0) {
			if(eaSize(&link->items) <= 1) {
				eaFindAndRemove(&timeline->links, link);
				ui_TimelineFreeLink(link);
			}
			break;
		}
	}
}

static UITimelineGroup* ui_TimelineFindFrameGroup(UITimelineKeyFrame *frame)
{
	int i;
	UITimelineTrack *track = frame->track;
	UITimeline *timeline = track->timeline;
	for ( i=0; i < eaSize(&timeline->groups); i++ ) {
		UITimelineGroup *group = timeline->groups[i];
		int idx = eaFind(&group->items, frame);
		if(idx >= 0) {
			return group;
		}
	}
	return NULL;
}

void ui_TimelineKeyFrameUngroup(UITimelineKeyFrame *frame)
{
	int i;
	UITimelineTrack *track = frame->track;
	UITimeline *timeline = track->timeline;
	for ( i=0; i < eaSize(&timeline->groups); i++ ) {
		UITimelineGroup *group = timeline->groups[i];
		int idx = eaFindAndRemove(&group->items, frame);
		if(idx >= 0) {
			if(eaSize(&group->items) <= 1) {
				eaFindAndRemove(&timeline->groups, group);
				ui_TimelineFreeGroup(group);
			}
			break;
		}
	}
}

void ui_TimelineKeyFrameSetSelected(UITimelineKeyFrame *frame, bool selected)
{
	int i, j;
	UITimelineTrack *track = frame->track;
	UITimeline *timeline = track->timeline;
	UITimelineGroup *group = ui_TimelineFindFrameGroup(frame);
	for ( i=0; i < eaSize(&timeline->links); i++ ) {
		UITimelineLink *link = timeline->links[i];
		if(eaFind(&link->items, frame) >= 0) {
			for ( j=0; j < eaSize(&link->items); j++ ) {
				UITimelineKeyFrame *linked_frame = link->items[j];
				linked_frame->selected = selected;
				if(selected)
					eaPushUnique(&timeline->selection, linked_frame);
				else 
					eaFindAndRemove(&timeline->selection, linked_frame);
			}
		}
	}
	frame->selected = selected;
	if(group) {
		if(selected) {
			bool all_selected = true;
			for ( i=0; i < eaSize(&group->items); i++ ) {
				if(!group->items[i]->selected) {
					all_selected = false;
					break;
				}
			}
			group->selected = all_selected;
		} else {
			group->selected = false;
		}
	}
	if(selected)
		eaPushUnique(&timeline->selection, frame);
	else 
		eaFindAndRemove(&timeline->selection, frame);
}

static UITimelineGroup* ui_TimelineGetGroupFromFrame(UITimelineKeyFrame *frame)
{
	int i;
	UITimeline *timeline = frame->track->timeline;
	for ( i=0; i < eaSize(&timeline->groups); i++ ) {
		UITimelineGroup *group = timeline->groups[i];
		if(eaFind(&group->items, frame) >= 0)
			return group;
	}
	return NULL;
}

static void ui_TimelineKeyFrameTick(UITimelineKeyFrame *frame, F32 x, F32 y, F32 w, F32 h, F32 z, F32 scale, bool point, CBox *selection_area_box)
{
	CBox frame_box;
	CBox frame_with_resize_box;
	CBox left_drag_box;
	CBox right_drag_box;
	UITimelineTrack *track = frame->track;
	UITimeline *timeline = track->timeline;
	UITimelineGroup *group = ui_TimelineGetGroupFromFrame(frame);
	bool dragging;

	assert(track && timeline);

	frame->hovering = false;

	if(point) {
		F32 half_size = (KEY_FRAME_SIZE/2.0f)*scale;
		BuildCBox(&frame_box, x-half_size, y+h/2.0f-half_size, KEY_FRAME_SIZE*scale, KEY_FRAME_SIZE*scale);
	} else {
		BuildCBox(&frame_box, x, y+h/10.0f, MAX(w, 10*scale), h*8.0f/10.0f);
	}

	frame_with_resize_box = frame_box;
	left_drag_box = frame_box;
	right_drag_box = frame_box;
	if(frame->allow_resize && track->allow_resize) {
		frame_with_resize_box.left -= 5;
		frame_with_resize_box.right += 5;
	
		left_drag_box.right = left_drag_box.left;
		left_drag_box.left = left_drag_box.right - 5;

		right_drag_box.left = right_drag_box.right;
		right_drag_box.right = right_drag_box.left + 5;
	}

	if(group)
		CBoxCombine(&group->box, &frame_with_resize_box, &group->box);

	dragging = !inpCheckHandled() && mouseDragHit(MS_LEFT, &frame_with_resize_box) && mouseDragHit(MS_LEFT, selection_area_box);
	if(timeline->scrubbing)
		dragging = false;

	if(dragging || (!inpCheckHandled() && mouseClickHit(MS_LEFT, &frame_with_resize_box) && mouseClickHit(MS_LEFT, selection_area_box))) {
		if(frame->selected && !dragging) {
			if(!inpLevelPeek(INP_CONTROL)) {
				ui_TimelineClearSelection(timeline);
				ui_TimelineKeyFrameSetSelected(frame, true);
			} else {
				ui_TimelineKeyFrameSetSelected(frame, false);
			}
		} else if(!frame->selected) {
			if(!inpLevelPeek(INP_CONTROL))
				ui_TimelineClearSelection(timeline);
			ui_TimelineKeyFrameSetSelected(frame, true);
		}

		if(dragging)
		{
			if(!frame->pre_changed_func || frame->pre_changed_func(frame, frame->pre_changed_data))
			{
				timeline->selection_dragging = true;
				if(frame->allow_resize && track->allow_resize) {
					if(mouseCollision(&left_drag_box))
						timeline->selection_left_resizing = true;
					else if(mouseCollision(&right_drag_box))
						timeline->selection_right_resizing = true;
				}
			}
		}

		if(timeline->selection_changed_func)
			timeline->selection_changed_func(timeline, timeline->selection_changed_data);

		inpHandled();
	} else if(!inpCheckHandled() && mouseClickHit(MS_RIGHT, &frame_with_resize_box) && mouseClickHit(MS_RIGHT, selection_area_box)) {
		if (frame->rightclick_func)
			frame->rightclick_func(frame, frame->rightclick_data);
		inpHandled();
	} else if(mouseCollision(&frame_with_resize_box) && mouseCollision(selection_area_box)) {
		if(!mouseIsDown(MS_LEFT)) {
			frame->hovering = true;
			track->hovering = true;

			if(frame->allow_resize && track->allow_resize) {
				if(mouseCollision(&left_drag_box)) {
					ui_SetCursorForDirection(UILeft);
					ui_CursorLock();
				} else if(mouseCollision(&right_drag_box)) {
					ui_SetCursorForDirection(UIRight);
					ui_CursorLock();
				}
			}
		}

		inpHandled();
	}
}

//////////////////////////////////////////////////////////////////////////
// Time Line Track
//////////////////////////////////////////////////////////////////////////

UITimelineTrack *ui_TimelineTrackCreate(const char *name)
{
	UITimelineTrack *track = calloc(1, sizeof(*track));
	track->name = StructAllocString(name);
	track->height = 30;
	track->allow_resize = false;
	return track;
}

void ui_TimelineTrackFree(UITimelineTrack *track)
{
	eaDestroyEx(&track->frames, ui_TimelineKeyFrameFree);
	StructFreeString(track->name);
	free(track);
}

static int ui_TimelineTrackSortFunc(const UITimelineKeyFrame **left, const UITimelineKeyFrame **right)
{
	return ((*left)->time > (*right)->time) ? 1 : -1;
}

void ui_TimelineTrackClearSelection(UITimelineTrack *track)
{
	int i;
	for ( i=0; i < eaSize(&track->frames); i++ ) {
		ui_TimelineKeyFrameSetSelected(track->frames[i], false);
	}
}

void ui_TimelineTrackSortFrames(UITimelineTrack *track)
{
	int i;
	if(!track->dont_sort_frames)
		eaQSort(track->frames, ui_TimelineTrackSortFunc); 
	for ( i=0; i < eaSize(&track->frames); i++ ) {
		if(i>0)
			track->frames[i]->prev = track->frames[i-1];
		if(i<eaSize(&track->frames)-1)
			track->frames[i]->next = track->frames[i+1];
	}
}

void ui_TimelineTrackSetName(UITimelineTrack *track, const char *name)
{
	StructFreeStringSafe(&track->name);
	track->name = StructAllocString(name);
}

void ui_TimelineTrackAddFrame(UITimelineTrack *track, UITimelineKeyFrame *frame)
{
	int frame_cnt = eaSize(&track->frames);
	frame->track = track;
	eaPush(&track->frames, frame);
	ui_TimelineTrackSortFrames(track);
}

UITimelineKeyFrame* ui_TimelineTrackGetFrame(UITimelineTrack *track, int idx)
{
	if(idx < 0 || idx >= eaSize(&track->frames))
		return NULL;
	return track->frames[idx];
}

void ui_TimelineTrackRemoveFrame(UITimelineTrack *track, UITimelineKeyFrame *frame)
{
	int i;
	UITimeline *timeline = track->timeline;

	for ( i=0; i < eaSize(&timeline->selection); i++ ) {
		UITimelineKeyFrame *selection = timeline->selection[i];
		if(selection == frame) {
			ui_TimelineKeyFrameSetSelected(selection, false);
			break;
		}
	}
	eaFindAndRemove(&track->frames, frame);
	ui_TimelineTrackSortFrames(track);
}

void ui_TimelineTrackSetRightClickCallback(UITimelineTrack *track, UITimelineCallback func, UserData data)
{
	track->rc_func = func;
	track->rc_data = data;
}

void ui_TimelineTrackSetLabelRightClickCallback(UITimelineTrack *track, UIActivationFunc func, UserData data)
{
	track->rc_label_func = func;
	track->rc_label_data = data;
}

static void ui_TimelineTrackDraw(UITimelineTrack *track, F32 x, F32 y, F32 w, F32 h, F32 z, F32 scale, bool parity)
{
	int i;
	F32 time_scale;
	UITimeline *timeline = track->timeline;
	UISkin *skin = UI_GET_SKIN(timeline);
	Color frameColor1 = skin->background[0];
	Color frameColor2 = ColorDarkenPercent(frameColor1, 0.95f);
	Color frameColorP = parity ? frameColor2 : frameColor1;
	Color frameColor = track->hovering ? ColorLightenPercent(frameColorP, 0.5f) : frameColorP;
	F32 label_w;

	assert(timeline);
	time_scale = timeline->time_scale;
	label_w = timeline->track_label_width * scale;

	//Draw Track Label
	{
		CBox label_box;

		BuildCBox(&label_box, x, y, label_w, h);
		if(track->draw_background) {
			ui_StyleBorderDraw(NULL, &label_box, RGBAFromColor(frameColor), RGBAFromColor(frameColor), z, scale, 255);
		}

		clipperPushRestrict(&label_box);
		gfxfont_Printf(x + 5, y + h/2, z + 0.1, scale, scale, CENTER_Y, "%s", track->name);
		clipperPop();
	}

	//Draw Track Data
	{
		CBox track_data_box;
		F32 xoffset = UI_WIDGET(timeline)->sb->xpos;
		F32 line_x_start = x;
		F32 line_y_start = y+h/2.0f;

		BuildCBox(&track_data_box, x+label_w, y, w-label_w, h);
		if(track->draw_background) {
			ui_StyleBorderDraw(NULL, &track_data_box, RGBAFromColor(frameColor), RGBAFromColor(frameColor), z, scale, 255);
		}

		clipperPushRestrict(&track_data_box);

		//Draw Points
		for ( i=0; i < eaSize(&track->frames); i++ ) {
			UITimelineKeyFrame *frame = track->frames[i];
			bool point = !frame->length;
			F32 x_start = x + label_w + UI_TIMELINE_TIME_TO_SCREEN(frame->time*time_scale)*scale - xoffset;
			F32 x_end = x_start + UI_TIMELINE_TIME_TO_SCREEN(frame->length*time_scale)*scale;
			F32 point_z = z + 0.03;

			ui_TimelineKeyFrameDraw(frame, x_start, y,  x_end-x_start, h, point_z, scale, point);

			if(track->draw_lines) {
				Color color;
				if(track->hovering)
					color = ColorInvert(frame->color);
				else
					color = frame->color;

				gfxDrawLine(line_x_start, line_y_start, z+0.05, x_start, y + h/2.0f, color);
			}

			point_z += 0.03f;
			if(point_z >= z + 0.3f)
				point_z = z + 0.03f;
			line_x_start = x_end;
			line_y_start = y + h/2.0f;
		}

		clipperPop();
	}
}

static void ui_TimelineTrackTick(UITimelineTrack *track, F32 x, F32 y, F32 w, F32 h, F32 z, F32 scale, CBox *selection_area_box)
{
	int i;
	F32 time_scale;
	UITimeline *timeline = track->timeline;
	UISkin *skin = UI_GET_SKIN(timeline);
	F32 label_w;

	assert(timeline);

	track->hovering = false;

	time_scale = timeline->time_scale;
	label_w = timeline->track_label_width * scale;

	// Tick Track Data
	{
		CBox track_data_box, track_label_box;
		F32 xoffset = UI_WIDGET(timeline)->sb->xpos;

		BuildCBox(&track_data_box, x+label_w, y, w-label_w, h);
		BuildCBox(&track_label_box, x, y, label_w, h);

		// Tick Points
		for ( i=eaSize(&track->frames)-1; i >= 0 ; i-- ) {
			UITimelineKeyFrame *frame = track->frames[i];
			bool point = !frame->length;
			F32 x_start = x + label_w + UI_TIMELINE_TIME_TO_SCREEN(frame->time*time_scale)*scale - xoffset;
			F32 x_end = x_start + UI_TIMELINE_TIME_TO_SCREEN(frame->length*time_scale)*scale;

			ui_TimelineKeyFrameTick(frame, x_start, y,  x_end-x_start, h, z+0.2, scale, point, selection_area_box);
		}

		if(!inpCheckHandled() && track->rc_func && mouseClickHit(MS_RIGHT, &track_data_box)) {
			S32 mx, my;
			int click_time;
			mousePos(&mx, &my);
			click_time = ui_TimelineGetTimeFromX(mx, timeline->time_scale, scale, track_data_box.lx, xoffset);
			track->rc_func(track, click_time, track->rc_data);
			inpHandled();
		} else if (!inpCheckHandled() && track->rc_label_func && mouseClickHit(MS_RIGHT, &track_label_box)) {
			track->rc_label_func(track, track->rc_label_data);
			inpHandled();
		}

		if(!mouseIsDown(MS_LEFT) && (mouseCollision(&track_label_box) || (mouseCollision(&track_data_box) && mouseCollision(selection_area_box))))
			track->hovering = true;
	}
}

//////////////////////////////////////////////////////////////////////////
// Time Line
//////////////////////////////////////////////////////////////////////////

UITimeline *ui_TimelineCreate(F32 x, F32 y, F32 w)
{
	UITimeline *timeline = calloc(1, sizeof(*timeline));
	ui_TimelineInitialize(timeline, x, y, w);
	return timeline;
}

void ui_TimelineFreeInternal(UITimeline *timeline)
{
	ui_TimelineClearSelection(timeline);
	eaDestroyEx(&timeline->links, ui_TimelineFreeLink);
	eaDestroyEx(&timeline->tracks, ui_TimelineTrackFree);
	ui_WidgetFreeInternal(UI_WIDGET(timeline));
}

void ui_TimelineInitialize(UITimeline *timeline, F32 x, F32 y, F32 w)
{
	ui_WidgetInitialize(UI_WIDGET(timeline), ui_TimelineTick, ui_TimelineDraw, ui_TimelineFreeInternal, NULL, NULL);
	UI_WIDGET(timeline)->sb = ui_ScrollbarCreate(true, true);

	timeline->title_bar_height = 30;
	timeline->track_label_width = 130;
	timeline->time_scale = 1.0f;

	timeline->selection_dragging = false;
	timeline->selection_left_resizing = false;
	timeline->selection_right_resizing = false;

	ui_WidgetSetDimensions(UI_WIDGET(timeline), w, timeline->title_bar_height);
	ui_WidgetSetPosition(UI_WIDGET(timeline), x, y);
}

static int ui_TimelineGetTickStep(UITimeline *timeline, F32 time_scale, F32 scale)
{
	F32 width;
	int step = 10;
	int cnt = 0;

	if(timeline->time_ticks_in_units) 
		width = gfxfont_StringWidth(gfxfont_GetFont(), scale, scale, "00.00");
	else
		width = gfxfont_StringWidth(gfxfont_GetFont(), scale, scale, "00:00.00");

	while(UI_TIMELINE_TIME_TO_SCREEN(step*time_scale)*scale < width*2.0f) {
		if(step == 1000) {
			step *= 2;
			cnt = 0;
		} else if (step > 1000) {
			if(cnt%5 == 0) {
				step *= 2;
			} else if (cnt%5 == 1) {
				step /= 2;
				step *= 5;
			} else if (cnt%5 == 2) {
				step *= 2;
			} else if (cnt%5 == 3) {
				step *= 3;
			} else {
				step *= 2;
			}
		} else {
			if(cnt%3 == 0) {
				step *= 2;
			} else if (cnt%3 == 1) {
				step /= 2;
				step *= 5;
			} else {
				step *= 2;
			}
		}
		cnt++;
	}
	return step;
}

static void ui_TimelineDrawTitleBar(UITimeline *timeline, F32 x, F32 y, F32 w, F32 h, F32 z, F32 scale)
{
	UISkin *skin = UI_GET_SKIN(timeline);
	Color frameColor = skin->background[0];
	F32 label_w = timeline->track_label_width * scale;

	//Draw Top Left Empty Space
	if(timeline->draw_top_left_empty_space)
	{
		CBox draw_box;
		BuildCBox(&draw_box, x, y, x+label_w, h);
		ui_StyleBorderDraw(NULL, &draw_box, RGBAFromColor(frameColor), RGBAFromColor(frameColor), z, scale, 255);
	}

	//Draw Time Ticks
	{
		CBox ticks_box;
		F32 font_height = gfxfont_FontHeight(gfxfont_GetFont(), scale);
		int scale_step = ui_TimelineGetTickStep(timeline, timeline->time_scale, scale);
		F32 xoffset = UI_WIDGET(timeline)->sb->xpos;
		int time, start_time, end_time;

		BuildCBox(&ticks_box, x+label_w, y, w-label_w, h);
		ui_StyleBorderDraw(NULL, &ticks_box, RGBAFromColor(frameColor), RGBAFromColor(frameColor), z, scale, 255);

		clipperPushRestrict(&ticks_box);

		start_time = ui_TimelineGetTimeFromX(x-w*0.5f, timeline->time_scale, scale, ticks_box.lx, xoffset);
		end_time = ui_TimelineGetTimeFromX(x+w*1.5f, timeline->time_scale, scale, ticks_box.lx, xoffset);
		start_time = round((F32)start_time/scale_step)*scale_step;
		end_time = round((F32)end_time/scale_step)*scale_step;
		for ( time = start_time; time < end_time; time += scale_step) {
			F32 tick_x = ui_TimelineGetXFromTime(time, timeline->time_scale, scale, ticks_box.lx, xoffset);
			if(time != 0)
			{
				gfxDrawLine(tick_x, ticks_box.ly+font_height, z+0.1, tick_x, ticks_box.hy, ColorBlack);
				if (!timeline->time_ticks_in_units)
				{
					int mins = time/60000;
					int secs = (time-(mins*60000))/1000;
					int ms = (time-(mins*60000)-(secs*1000));
					gfxfont_Printf(tick_x, ticks_box.ly+font_height, z+0.11, scale, scale, CENTER_X, "%02d:%02d.%02d", mins, secs, ms/10);
				}
				else
				{
					F32 units = time*0.001f;
					gfxfont_Printf(tick_x, ticks_box.ly+font_height, z+0.11, scale, scale, CENTER_X, "%.2f",units);
				}
			}
		}

		clipperPop();
	}
}

static void ui_TimelineUpdateSize(UITimeline *timeline)
{
	if (timeline->max_time_mode)
	{
		int i;
		F32 height = 0;
		for ( i=0; i < eaSize(&timeline->tracks); i++ ) {
			UITimelineTrack *track = timeline->tracks[i];
			height += track->height;
		}
		timeline->data_height = height;
		timeline->total_time = MAX(timeline->total_time, timeline->current_time);
		timeline->data_width = UI_TIMELINE_TIME_TO_SCREEN((timeline->total_time) * timeline->time_scale);
	}
	else
	{

		int i, j;
		F32 height = 0;
		F32 width = 0;
		int total_time = 0;
		for ( i=0; i < eaSize(&timeline->tracks); i++ ) {
			UITimelineTrack *track = timeline->tracks[i];
			for ( j=0; j < eaSize(&track->frames); j++ ) {
				UITimelineKeyFrame *frame = track->frames[j];
				if(frame->time + frame->length > total_time) {
					width = UI_TIMELINE_TIME_TO_SCREEN((frame->time + frame->length) * timeline->time_scale);
					total_time = frame->time + frame->length;
				}
			}
			height += track->height;
		}
		timeline->data_height = height;
		timeline->data_width = width;
		timeline->total_time = total_time;
	}
}

void ui_TimelineAddTrack(UITimeline *timeline, UITimelineTrack *track)
{
	int i;
	track->timeline = timeline;
	for ( i=0; i < eaSize(&timeline->tracks); i++ ) {
		if(timeline->tracks[i]->order > track->order) {
			eaInsert(&timeline->tracks, track, i);
			return;
		}
	}
	eaPush(&timeline->tracks, track);
}

void ui_TimelineRemoveTrack(UITimeline *timeline, UITimelineTrack *track)
{
	//TODO need to remove selections
	eaFindAndRemove(&timeline->tracks, track);
}


void ui_TimelineSetTimeChangedCallback(UITimeline *timeline, UITimelineCallback func, UserData data)
{
	timeline->time_changed_func = func;
	timeline->time_changed_data = data;
}

void ui_TimelineSetFramePreChangedCallback(UITimeline *timeline, UIPreActivationFunc func, UserData data)
{
	timeline->frame_pre_changed_func = func;
	timeline->frame_pre_changed_data = data;
}

void ui_TimelineSetFrameChangedCallback(UITimeline *timeline, UIActivationFunc func, UserData data)
{
	timeline->frame_changed_func = func;
	timeline->frame_changed_data = data;
}

void ui_TimelineSetSelectionChangedCallback(UITimeline *timeline, UIActivationFunc func, UserData data)
{
	timeline->selection_changed_func = func;
	timeline->selection_changed_data = data;
}

void ui_TimelineSetRightClickCallback(UITimeline *timeline, UIActivationFunc func, UserData data)
{
	timeline->rc_func = func;
	timeline->rc_data = data;	
}

int ui_TimelineGetTime(UITimeline *timeline)
{
	return timeline->current_time;
}

int ui_TimelineGetTotalTime(UITimeline *timeline)
{
	return timeline->total_time;
}

void ui_TimelineSetTime(UITimeline *timeline, int time)
{
	timeline->current_time = time;
}

static void ui_TimelineFreeLink(UITimelineLink *link)
{
	eaDestroy(&link->items);
	free(link);
}
static void ui_TimelineFreeGroup(UITimelineGroup *group)
{
	eaDestroy(&group->items);
	free(group);
}

void ui_TimelineClearLinks(UITimeline *timeline)
{
	eaDestroyEx(&timeline->links, ui_TimelineFreeLink);
}

void ui_TimelineClearGroups(UITimeline *timeline)
{
	eaDestroyEx(&timeline->groups, ui_TimelineFreeGroup);
}

void ui_TimelineClearSelection(UITimeline *timeline)
{
	int i;
	while(eaSize(&timeline->selection) > 0)
		ui_TimelineKeyFrameSetSelected(timeline->selection[0], false);
	for ( i=0; i < eaSize(&timeline->groups); i++ ) {
		timeline->groups[i]->selected = false;
	}
}

void ui_TimelineClearSelectionAndCallback(UITimeline *timeline)
{
	ui_TimelineClearSelection(timeline);
	if(timeline->selection_changed_func)
		timeline->selection_changed_func(timeline, timeline->selection_changed_data);
}


void ui_TimelineSetTimeAndCallback(UITimeline *timeline, int time)
{	
	ui_TimelineSetTime(timeline, time);
	if(timeline->time_changed_func)
		timeline->time_changed_func(timeline, timeline->current_time, timeline->time_changed_data);
}

void ui_TimelineLinkFrames(UITimeline *timeline, UITimelineKeyFrame **frames)
{
	UITimelineLink *link = calloc(1, sizeof(*link));
	eaCopy(&link->items, &frames);
	eaPush(&timeline->links, link);
}

void ui_TimelineGroupFrames(UITimeline *timeline, UITimelineKeyFrame **frames)
{
	UITimelineGroup *group = calloc(1, sizeof(*group));
	eaCopy(&group->items, &frames);
	eaPush(&timeline->groups, group);
}

static void ui_TimelineGroupSetSelected(UITimelineGroup *group, bool selected)
{
	int i;
	for ( i=0; i < eaSize(&group->items); i++ ) {
		ui_TimelineKeyFrameSetSelected(group->items[i], selected);
	}
	group->selected = selected;
}

void ui_TimelineDraw(UITimeline *timeline, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(timeline);
	UISkin *skin = UI_GET_SKIN(timeline);
	Color frameColor = skin->background[0];
	F32 title_h, data_h, label_w;
	UIStyleFont *pFont = ui_WidgetGetFont(UI_WIDGET(timeline));
	ui_StyleFontUse(pFont, false, kWidgetModifier_None);
	
	title_h = timeline->title_bar_height * scale;
	data_h = timeline->data_height * scale;
	label_w = timeline->track_label_width * scale;

	//Make room for the scroll bars
	h -= ui_ScrollbarHeight(UI_WIDGET(timeline)->sb) * scale;
	if (UI_WIDGET(timeline)->sb->scrollY && h < (title_h + data_h))
		w -= ui_ScrollbarWidth(UI_WIDGET(timeline)->sb) * scale;

	//Draw Scroll Bars
	{
		F32 scroll_x = x + label_w;
		F32 scroll_y = y + title_h;
		F32 scroll_w = w - label_w;
		F32 scroll_h = h - title_h;
		F32 size_w = MAX(timeline->data_width*scale, scroll_w);
		if(!timeline->limit_zoom_out)
			size_w += scroll_w*0.3f;
		if(nearSameF32(scroll_w, size_w))
			size_w = scroll_w;
		ui_ScrollbarDraw(UI_WIDGET(timeline)->sb, scroll_x, scroll_y, scroll_w, scroll_h, z, scale, size_w, data_h);
	}

	box.lx = x; box.ly = y; box.hx = x + w; box.hy = y + h;
	UI_DRAW_EARLY(timeline);

	// Draw Title Bar
	ui_TimelineDrawTitleBar(timeline, x, y, w, title_h, z, scale);

	// Draw Background for Track Labels and Data
	{
		CBox draw_box;

		BuildCBox(&draw_box, x, y + title_h, label_w, h - title_h);
		ui_StyleBorderDraw(NULL, &draw_box, RGBAFromColor(frameColor), RGBAFromColor(frameColor), z, scale, 255);

		BuildCBox(&draw_box, x + label_w, y + title_h, w - label_w, h - title_h);
		ui_StyleBorderDraw(NULL, &draw_box, RGBAFromColor(frameColor), RGBAFromColor(frameColor), z, scale, 255);
	}

	// Draw Tracks and Groups
	{
		int i;
		CBox clip_box;
		F32 trackY = y + title_h - UI_WIDGET(timeline)->sb->ypos;

		BuildCBox(&clip_box, x, y + title_h, w, h - title_h);
		clipperPushRestrict(&clip_box);

		for ( i=0; i < eaSize(&timeline->tracks); i++ ) {
			UITimelineTrack *track = timeline->tracks[i];
			ui_TimelineTrackDraw(track, x, trackY, w, track->height*scale, z+0.1f, scale, /*parity=*/!!(i % 2));
			trackY += track->height*scale;
		}

		clipperPop();
		clip_box.lx += label_w;
		clipperPushRestrict(&clip_box);

		for ( i=0; i < eaSize(&timeline->groups); i++ ) {
			UITimelineGroup *group = timeline->groups[i];
			if(group->box.lx < group->box.hx && group->box.ly < group->box.hy) {
				ui_StyleBorderDraw(NULL, &group->box, 0xB4B4B4FF, 0xB4B4B4FF, z+0.049, scale, 150);
			}
		}

		clipperPop();
	}

	//Draw Drag Selection
	if(timeline->drag_selecting)
	{
		S32 mx, my;
		S32 omx, omy;
		CBox selection_box;
		CBox clip_box;

		BuildCBox(&clip_box, x + label_w, y + title_h, w - label_w, h - title_h);
		clipperPushRestrict(&clip_box);

		mousePos(&mx, &my);
		omx = timeline->drag_start_x - UI_WIDGET(timeline)->sb->xpos;
		omy = timeline->drag_start_y - UI_WIDGET(timeline)->sb->ypos;

		selection_box.lx = MIN(mx, omx);
		selection_box.ly = MIN(my, omy);
		selection_box.hx = MAX(mx, omx);
		selection_box.hy = MAX(my, omy);
		ui_StyleBorderDraw(NULL, &selection_box, 0xB4B4B4FF, 0xB4B4B4FF, z+0.1, scale, 150);

		clipperPop();
	}

	//Draw Scrubber
	{
		CBox clip_box;
		CBox draw_box;
		F32 scrub_x;

		BuildCBox(&clip_box, x + label_w, y, w - label_w, h);
		clipperPushRestrict(&clip_box);
	
		scrub_x = ui_TimelineGetXFromTime(timeline->current_time, timeline->time_scale, scale, x + label_w, UI_WIDGET(timeline)->sb->xpos);
		
		BuildCBox(&draw_box, scrub_x-SCRUB_HANDLE_SIZE/2.0f, y+title_h/2.0f, SCRUB_HANDLE_SIZE, title_h/2.0f);
		ui_StyleBorderDraw(NULL, &draw_box, RGBAFromColor(ColorRed), RGBAFromColor(ColorRed), z+0.7, scale, 120);

		gfxDrawLine(scrub_x, y + title_h*3.0f/4.0f, z+0.7, scrub_x, y+h, ColorRed);

		clipperPop();
	}

	UI_DRAW_LATE(timeline);
	
}

// time_diff is difference in time since we started dragging
static void ui_TimelineApplyDragOffset(UITimeline *timeline, int time_diff)
{
	int i;

	// Clamp Time Difference
	for ( i=0; i < eaSize(&timeline->selection); i++ ) {
		UITimelineKeyFrame *cur_frame = timeline->selection[i];
		UITimelineKeyFrame *prev_frame = cur_frame->prev;
		UITimelineKeyFrame *next_frame = cur_frame->next;
		UITimelineTrack *track = cur_frame->track;

		if(time_diff < 0 && prev_frame && !prev_frame->selected) {
			if(track->prevent_order_changes) {
				int max_time_diff = (prev_frame->time - cur_frame->drag_start_time) + 1;
				if(track->allow_overlap) {
					int max_time_diff_length = max_time_diff + prev_frame->length - cur_frame->length;
					MAX1(max_time_diff, max_time_diff_length);
				} else {
					max_time_diff += prev_frame->length;
				}
				MAX1(time_diff, max_time_diff);
				MIN1(time_diff, 0);
			}
		} else if(time_diff > 0 && next_frame && !next_frame->selected) {
			if(track->prevent_order_changes) {
				int max_time_diff = (next_frame->time - cur_frame->drag_start_time) - 1;
				if(track->allow_overlap) {
					int max_time_diff_length = max_time_diff + next_frame->length - cur_frame->length;
					MIN1(max_time_diff, max_time_diff_length);
				} else {
					max_time_diff -= cur_frame->length;
				}
				MIN1(time_diff, max_time_diff);
				MAX1(time_diff, 0);
			}
		}
		
		if(time_diff < 0) {
			MAX1(time_diff, -cur_frame->drag_start_time);
		}
	}

	// Apply Time Difference
	for ( i=0; i < eaSize(&timeline->selection); i++ ) {
		UITimelineKeyFrame *frame = timeline->selection[i];
		frame->time = frame->drag_start_time + time_diff;
	}
}

// time_diff is difference in time since we started dragging
static void ui_TimelineApplyLeftResizeOffset(UITimeline *timeline, int time_diff)
{
	int i;

	// Clamp Time Difference
	for ( i=0; i < eaSize(&timeline->selection); i++ ) {
		UITimelineKeyFrame *cur_frame = timeline->selection[i];
		UITimelineKeyFrame *prev_frame = cur_frame->prev;
		UITimelineTrack *track = cur_frame->track;

		if(cur_frame->allow_resize && track->allow_resize) {
			if(time_diff > 0) {
				MIN1(time_diff, cur_frame->drag_start_length); // prevent length from going negative (dragging left side past right side)
			} else if(time_diff < 0) {
				if(track->prevent_order_changes && prev_frame) { // prevent left side from being dragged beyond previous frame
					int max_time_diff = (prev_frame->time - cur_frame->drag_start_time) + 1;
					if(track->allow_overlap) {
						int max_time_diff_length = max_time_diff + prev_frame->length - cur_frame->length;
						MAX1(max_time_diff, max_time_diff_length);
					} else {
						max_time_diff += prev_frame->length;
					}
					MAX1(time_diff, max_time_diff);
					MIN1(time_diff, 0);
				} else {
					MAX1(time_diff, -cur_frame->drag_start_time); // prevent time from being dragged below zero
				}
			}
		}
	}

	// Apply Time Difference
	for ( i=0; i < eaSize(&timeline->selection); i++ ) {
		UITimelineKeyFrame *frame = timeline->selection[i];
		UITimelineTrack *track = frame->track;

		if(frame->allow_resize && frame->track->allow_resize) {
			frame->time = frame->drag_start_time + time_diff;
			frame->length = frame->drag_start_length - time_diff;
		}
	}
}

// time_diff is difference in time since we started dragging
static void ui_TimelineApplyRightResizeOffset(UITimeline *timeline, int time_diff)
{
	int i;

	// Clamp Time Difference
	for ( i=0; i < eaSize(&timeline->selection); i++ ) {
		UITimelineKeyFrame *cur_frame = timeline->selection[i];
		UITimelineKeyFrame *next_frame = cur_frame->next;
		UITimelineTrack *track = cur_frame->track;

		if(cur_frame->allow_resize && track->allow_resize) {
			if(time_diff < 0) {
				MAX1(time_diff, -cur_frame->drag_start_length); // prevent length from going negative (dragging right side past left side)
			} else if(time_diff > 0) {
				if(track->prevent_order_changes && next_frame) {
					int max_time_diff = (next_frame->time - (cur_frame->drag_start_time + cur_frame->drag_start_length)) - 1;
					if(track->allow_overlap) {
						int max_time_diff_length = max_time_diff + next_frame->length;
						MIN1(max_time_diff, max_time_diff_length);
					}
					MIN1(time_diff, max_time_diff);
					MAX1(time_diff, 0);
				}
			}
		}
	}

	// Apply Time Difference
	for ( i=0; i < eaSize(&timeline->selection); i++ ) {
		UITimelineKeyFrame *frame = timeline->selection[i];
		UITimelineTrack *track = frame->track;

		if(frame->allow_resize && frame->track->allow_resize)
			frame->length = frame->drag_start_length + time_diff;
	}
}

void ui_TimelineTrackSelectAllKeyFrames(UITimelineTrack *track, bool include_bars)
{
	int i;
	for ( i=0; i < eaSize(&track->frames); i++ ) {
		UITimelineKeyFrame *frame = track->frames[i];
		if(include_bars || frame->length == 0)
			ui_TimelineKeyFrameSetSelected(frame, true);
	}
}

static void ui_TimelineSelectFramesInTimeRange(UITimelineTrack *track, F32 start_time, F32 end_time, bool include_bars)
{
	int i;
	for ( i=0; i < eaSize(&track->frames); i++ ) {
		UITimelineKeyFrame *frame = track->frames[i];
		if(	frame->time > start_time && frame->time + frame->length < end_time ) {
			if(include_bars || frame->length == 0)
				ui_TimelineKeyFrameSetSelected(frame, true);
		}
	}
}

static void ui_TimelineAutoScrollX(UITimeline *timeline, S32 mx, S32 my, const CBox *scroll_box)
{
	if(UI_WIDGET(timeline)->sb->scrollX)
	{
		if(mx < scroll_box->left + 20)
		{
			S32 dx = (scroll_box->left + 20 - mx) / 4.0f;
			if(dx > UI_WIDGET(timeline)->sb->xpos)
				dx = UI_WIDGET(timeline)->sb->xpos;

			if(dx > 0)
			{
				UI_WIDGET(timeline)->sb->xpos -= dx;
				timeline->drag_start_x += dx;
			}
		}
		else if(mx > scroll_box->right - 20)
		{
			S32 dx = (mx - (scroll_box->right - 20)) / 4.0f;

			if(dx > 0)
			{
				UI_WIDGET(timeline)->sb->xpos += dx;
				timeline->drag_start_x -= dx;
			}
		}
	}
}

void ui_TimelineTick(UITimeline *timeline, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(timeline);
	int i;
	F32 title_h, data_h, label_w;
	CBox selection_area_box;

	//////////////////////////////////////////////////////////////////////////
	// Setup Area and Tick Scroll Bars

	if(inpLevelPeek(INP_CONTROL)) {
		F32 orig_time_scale = timeline->time_scale;
		if( mouseClickHit( MS_WHEELDOWN, &box )) {
			if(timeline->time_scale > 1.0f) {
				timeline->time_scale = MAX( 1.0, timeline->time_scale * 0.85 );
			} else {
				timeline->time_scale = MAX( 0.001, timeline->time_scale * 0.85 );
			}
			inpScrollHandled();
		}
		if( mouseClickHit( MS_WHEELUP, &box )) {
			timeline->zoomed_out = false;
			if(timeline->time_scale < 1.0f) {
				timeline->time_scale = MIN( 1.0, timeline->time_scale / 0.85 );
			} else {
				timeline->time_scale = MIN( 300, timeline->time_scale / 0.85 );
			}
			inpScrollHandled();
		}
		if(orig_time_scale != timeline->time_scale) {
			F32 half_dist = (w - (timeline->track_label_width * scale))/2.0f;
			F32 orig_time = ui_TimelineGetTimeFromX(UI_WIDGET(timeline)->sb->xpos+half_dist, orig_time_scale, scale, 0, 0);
			UI_WIDGET(timeline)->sb->xpos = ui_TimelineGetXFromTime(orig_time, timeline->time_scale, scale, 0, 0)-half_dist;
		}
	}

	ui_TimelineUpdateSize(timeline);
	title_h = timeline->title_bar_height * scale;
	data_h = timeline->data_height * scale;
	label_w = timeline->track_label_width * scale;
	BuildCBox(&selection_area_box, x+label_w, y+title_h, w-label_w, h-title_h);

	// Dragging Scroll Area from Center
	if (timeline->scroll_dragging) {
		if (mouseIsDown(MS_MID)) {
			S32 dx, dy;
			mouseDiff(&dx, &dy);
			if (UI_WIDGET(timeline)->sb->scrollX)
				UI_WIDGET(timeline)->sb->xpos -= dx;
			if (UI_WIDGET(timeline)->sb->scrollY)
				UI_WIDGET(timeline)->sb->ypos -= dy;
			ui_SetCursorForDirection(UIAnyDirection);
			ui_CursorLock();
		} else {
			timeline->scroll_dragging = false;
		}
	} else if (mouseDragHit(MS_MID, &box)) {
		timeline->scroll_dragging = true;
		inpHandled();
	}

	// Resize window to fit scroll bars
	h -= ui_ScrollbarHeight(UI_WIDGET(timeline)->sb) * scale;
	if (UI_WIDGET(timeline)->sb->scrollY && h < (title_h + data_h))
		w -= ui_ScrollbarWidth(UI_WIDGET(timeline)->sb) * scale;

	if (timeline->drag_selecting) {
		S32 mx, my;
		mousePos(&mx, &my);
		if (UI_WIDGET(timeline)->sb->scrollX) {
			if(mx < x+label_w)
				UI_WIDGET(timeline)->sb->xpos -= (x+label_w)-mx;
			if(mx > x+w)
				UI_WIDGET(timeline)->sb->xpos += mx-(x+w);
		}
		if (UI_WIDGET(timeline)->sb->scrollY) {
			if(my < y+title_h)
				UI_WIDGET(timeline)->sb->ypos -= (y+title_h)-my;
			if(my > y+h)
				UI_WIDGET(timeline)->sb->ypos += my-(y+h);
		}
	}

	// Tick Scroll
	{
		F32 scroll_x = x + label_w;
		F32 scroll_y = y + title_h;
		F32 scroll_w = w - label_w;
		F32 scroll_h = h - title_h;

		if (scroll_w && timeline->limit_zoom_out && timeline->data_width > 0.0f)
		{
			F32 min_timeline_scale =  scroll_w / (timeline->data_width*scale/timeline->time_scale);
			if (timeline->time_scale < min_timeline_scale || timeline->zoomed_out)
			{
				timeline->time_scale = min_timeline_scale;
				timeline->zoomed_out = true;
			}
		}
		ui_ScrollbarTick(UI_WIDGET(timeline)->sb, scroll_x, scroll_y, scroll_w, scroll_h, z, scale, MAX(timeline->data_width*scale, scroll_w) + (timeline->limit_zoom_out?0.0f:scroll_w*0.3f), data_h);
	}

	//////////////////////////////////////////////////////////////////////////
	// Actual Tick Starts Here

	box.lx = x; box.ly = y; box.hx = x + w; box.hy = y + h;
	UI_TICK_EARLY(timeline, true, true);

	// Selection Dragging
	if (timeline->selection_dragging) {
		if (mouseIsDown(MS_LEFT)) {
			S32 mx, my;
			int time_diff;

			mousePos(&mx, &my);

			time_diff = ui_TimelineGetTimeFromX(mx - timeline->drag_start_x, timeline->time_scale, scale, 0, 0);

			if(timeline->selection_left_resizing) {
				ui_SetCursorForDirection(UILeft);
				ui_CursorLock();

				ui_TimelineApplyLeftResizeOffset(timeline, time_diff);
			} else if(timeline->selection_right_resizing) {
				ui_SetCursorForDirection(UIRight);
				ui_CursorLock();

				ui_TimelineApplyRightResizeOffset(timeline, time_diff);
			} else {
				ui_TimelineApplyDragOffset(timeline, time_diff);
			}

			ui_TimelineAutoScrollX(timeline, mx, my, &selection_area_box);
		} else {
			timeline->selection_dragging = false;
			timeline->selection_left_resizing = false;
			timeline->selection_right_resizing = false;
			for ( i=0; i < eaSize(&timeline->tracks); i++ )
				ui_TimelineTrackSortFrames(timeline->tracks[i]);
			for ( i=0; i < eaSize(&timeline->selection); i++ ) {
				UITimelineKeyFrame *frame = timeline->selection[i];
				if(frame->changed_func)
					frame->changed_func(frame, frame->changed_data);
			}
			if(timeline->frame_changed_func)
				timeline->frame_changed_func(timeline, timeline->frame_changed_data);
		}
		inpHandled();
	}

	// Tick Title Bar
	{
		CBox tick_box, scrubber_box;
		F32 scrub_x = ui_TimelineGetXFromTime(timeline->current_time, timeline->time_scale, scale, x + label_w, UI_WIDGET(timeline)->sb->xpos);

		BuildCBox(&scrubber_box, scrub_x-SCRUB_HANDLE_SIZE/2.0f, y+title_h/2.0f, SCRUB_HANDLE_SIZE, title_h/2.0f);
		BuildCBox(&tick_box, x+label_w, y, w-label_w, title_h);

		if (timeline->scrubbing) {
			if (mouseIsDown(MS_LEFT)) {
				S32 mx, my;
				F32 time_diff;
				mousePos(&mx, &my);
				// If we clicked on the scrubber box, then we don't want cur time to pop to our cursor
				// So, get the difference in mouse position and convert that to time to get
				// the difference in time.
				time_diff = ui_TimelineGetTimeFromX(mx - timeline->drag_start_x, timeline->time_scale, scale, 0, 0);

				ui_TimelineAutoScrollX(timeline, mx, my, &tick_box);

				timeline->current_time = CLAMP(timeline->drag_start_time + time_diff, 0, timeline->total_time);
				if(timeline->time_changed_func && timeline->continuous)
					timeline->time_changed_func(timeline, timeline->current_time, timeline->time_changed_data);
			} else {
				timeline->scrubbing = false;
				if(timeline->time_changed_func)
					timeline->time_changed_func(timeline, timeline->current_time, timeline->time_changed_data);
			}
			inpHandled();
		} else if(mouseDownHit(MS_LEFT, &tick_box)) {
			if (mouseDownHit(MS_LEFT, &scrubber_box)) {
				S32 mx, my;
				mousePos(&mx, &my);
				timeline->scrubbing = true;
				timeline->drag_start_x = mx;
				timeline->drag_start_time = timeline->current_time;
				inpHandled();
			} else {
				S32 mx, my;
				mousePos(&mx, &my);
				timeline->scrubbing = true;
				timeline->drag_start_x = mx;
				// If we didn't click the scrubber box, then we want cur time to pop to our cursor
				timeline->drag_start_time = ui_TimelineGetTimeFromX(mx, timeline->time_scale, scale, x + label_w, UI_WIDGET(timeline)->sb->xpos);
				inpHandled();
			}
		}
	}

	// Tick Tracks
	{
		F32 trackY = y + title_h - UI_WIDGET(timeline)->sb->ypos;
		bool was_dragging_selection = timeline->selection_dragging;

		for ( i=0; i < eaSize(&timeline->groups); i++ ) {
			CBox *group_box = &timeline->groups[i]->box;
			group_box->lx = FLT_MAX;
			group_box->ly = FLT_MAX;
			group_box->hx = -FLT_MAX;
			group_box->hy = -FLT_MAX;
		}

		for ( i=0; i < eaSize(&timeline->tracks); i++ ) {
			UITimelineTrack *track = timeline->tracks[i];
			ui_TimelineTrackTick(track, x, trackY, w, track->height*scale, z+0.1f, scale, &selection_area_box);
			trackY += track->height*scale;
		}

		if(!timeline->selection_dragging && !timeline->scrubbing) {
			for ( i=0; i < eaSize(&timeline->groups); i++ ) {
				UITimelineGroup *group = timeline->groups[i];
				bool dragging = !inpCheckHandled() && mouseDragHit(MS_LEFT, &group->box) && mouseDragHit(MS_LEFT, &selection_area_box);
				if(dragging)
					timeline->drag_selecting = false;
				if(dragging || (!inpCheckHandled() && mouseClickHit(MS_LEFT, &group->box) && mouseClickHit(MS_LEFT, &selection_area_box))) {
					if(group->selected && !dragging) {
						if(!inpLevelPeek(INP_CONTROL)) {
							ui_TimelineClearSelection(timeline);
							ui_TimelineGroupSetSelected(group, true);
						} else {
							ui_TimelineGroupSetSelected(group, false);
						}
					} else if(!group->selected) {
						if(!inpLevelPeek(INP_CONTROL))
							ui_TimelineClearSelection(timeline);
						ui_TimelineGroupSetSelected(group, true);
					}
					if(timeline->selection_changed_func)
						timeline->selection_changed_func(timeline, timeline->selection_changed_data);
					if(dragging)
					{
						if(!timeline->frame_pre_changed_func || timeline->frame_pre_changed_func(timeline, timeline->frame_pre_changed_data))
							timeline->selection_dragging = true;
					}
					inpHandled();
				}
			}
		}

		// Set these value here because we don't have the data in the the key frame tick
		if(!was_dragging_selection && timeline->selection_dragging) {
			S32 mx, my;
			mousePos(&mx, &my);
			timeline->drag_start_x = mx;
			for ( i=0; i < eaSize(&timeline->selection); i++ ) {
				UITimelineKeyFrame *selection = timeline->selection[i];
				selection->drag_start_time = selection->time;
				selection->drag_start_length = selection->length;
			}
		}
	}

	//Click Drag Selecting
	{
		if(timeline->drag_selecting) {
			S32 mx, my;
			mousePos(&mx, &my);
			if(!mouseIsDown(MS_LEFT)) {
				S32 omx = timeline->drag_start_x - UI_WIDGET(timeline)->sb->xpos;
				S32 omy = timeline->drag_start_y - UI_WIDGET(timeline)->sb->ypos;
				F32 start_time = ui_TimelineGetTimeFromX(omx, timeline->time_scale, scale, x+label_w, UI_WIDGET(timeline)->sb->xpos);
				F32 end_time = ui_TimelineGetTimeFromX(mx, timeline->time_scale, scale, x+label_w, UI_WIDGET(timeline)->sb->xpos);
				F32 track_y = y + title_h - UI_WIDGET(timeline)->sb->ypos;
				
				if(end_time < start_time)
					SWAPF32(start_time, end_time);
				if(my < omy)
					SWAPF32(omy, my);

				if(!inpLevelPeek(INP_CONTROL))
					ui_TimelineClearSelection(timeline);

				for ( i=0; i < eaSize(&timeline->tracks); i++ ) {
					UITimelineTrack *track = timeline->tracks[i];
					F32 track_height = track->height*scale;
					F32 track_mid = track_y + track_height/2.0f;
					if( track_mid > omy && track_mid  < my ) {
						F32 track_buff = 4*track_height/10.0f;
						bool include_bars = (track_mid-track_buff > omy && track_mid+track_buff  < my);
						ui_TimelineSelectFramesInTimeRange(track, start_time, end_time, include_bars);
					}
					track_y += track_height;
				}

				if(timeline->selection_changed_func)
					timeline->selection_changed_func(timeline, timeline->selection_changed_data);
				timeline->drag_selecting = false;
			}
		} else if(!inpCheckHandled() && mouseDownHit(MS_LEFT, &selection_area_box)) {
			S32 mx, my;
			mousePos(&mx, &my);
			timeline->drag_selecting = true;
			timeline->drag_start_x = mx + UI_WIDGET(timeline)->sb->xpos;
			timeline->drag_start_y = my + UI_WIDGET(timeline)->sb->ypos;
			inpHandled();
		}
	}

	if(!inpCheckHandled() && timeline->rc_func && mouseClickHit(MS_RIGHT, &box)) {
		timeline->rc_func(timeline, timeline->rc_data);
		inpHandled();
	}

	UI_TICK_LATE(timeline);
}

