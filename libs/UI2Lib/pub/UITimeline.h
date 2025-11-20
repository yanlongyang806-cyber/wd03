#pragma once
GCC_SYSTEM
/***************************************************************************



***************************************************************************/

#ifndef UI_TIMELINE_H
#define UI_TIMELINE_H

#include "UICore.h"
#include "CBox.h"

typedef struct UITimelineKeyFrame UITimelineKeyFrame;
typedef struct UITimelineTrack UITimelineTrack;
typedef struct UITimeline UITimeline;
typedef struct CBox CBox;

typedef void (*UITimelineCallback)(void *, int time, UserData);

//////////////////////////////////////////////////////////////////////////
// Time Line Key Frame
//////////////////////////////////////////////////////////////////////////

typedef struct UITimelineKeyFrame
{
	int time;
	int length;
	UserData data;

	F32 point_scale;
	Color color;
	bool allow_resize;
	bool hovering;
	bool selected;

	int drag_start_time;
	int drag_start_length;

	UIPreActivationFunc pre_changed_func;
	UserData pre_changed_data;

	UIActivationFunc changed_func;
	UserData changed_data;

	UIActivationFunc rightclick_func;
	UserData rightclick_data;

	UITimelineTrack *track;
	UITimelineKeyFrame *prev;
	UITimelineKeyFrame *next;

} UITimelineKeyFrame;

UITimelineKeyFrame *ui_TimelineKeyFrameCreate();
void ui_TimelineKeyFrameFree(UITimelineKeyFrame *track);
void ui_TimelineKeyFrameUnlink(UITimelineKeyFrame *frame);
void ui_TimelineKeyFrameUngroup(UITimelineKeyFrame *frame);

void ui_TimelineKeyFrameSetSelected(UITimelineKeyFrame *frame, bool selected);
void ui_TimelineTrackSelectAllKeyFrames(UITimelineTrack *track, bool include_bars);

void ui_TimelineKeyFrameSetPreChangedCallback(UITimelineKeyFrame *frame, UIPreActivationFunc func, UserData data);
void ui_TimelineKeyFrameSetChangedCallback(UITimelineKeyFrame *frame, UIActivationFunc func, UserData data);
void ui_TimelineKeyFrameSetRightClickCallback(UITimelineKeyFrame *frame, UIActivationFunc func, UserData data);


//////////////////////////////////////////////////////////////////////////
// Time Line Track
//////////////////////////////////////////////////////////////////////////

typedef struct UITimelineTrack
{
	UITimelineKeyFrame **frames;	//When ever you add to this make sure to prev and next are set

	char *name;
	F32 height;
	bool hovering;

	bool draw_lines;
	bool draw_background;
	bool prevent_order_changes;		//Can't drag an item past each other
	bool allow_overlap;				//If set then items with length can be overlapped when dragging
	bool dont_sort_frames;			//If set then the track will not be automatically sorted for you, sorting is nice because if you have items that overlap then the right items will get click priority
	bool allow_resize;

	int order;						//When inserting into timeline, will determine where it is located

	UITimelineCallback rc_func;
	UserData rc_data;

	UIActivationFunc rc_label_func;
	UserData rc_label_data;

	UITimeline *timeline;

} UITimelineTrack;

UITimelineTrack *ui_TimelineTrackCreate(const char *name);
void ui_TimelineTrackFree(UITimelineTrack *track);

void ui_TimelineTrackAddFrame(UITimelineTrack *track, UITimelineKeyFrame *frame);
UITimelineKeyFrame* ui_TimelineTrackGetFrame(UITimelineTrack *track, int idx);
void ui_TimelineTrackRemoveFrame(UITimelineTrack *track, UITimelineKeyFrame *frame);

void ui_TimelineTrackClearSelection(UITimelineTrack *track);
void ui_TimelineTrackSortFrames(UITimelineTrack *track);

void ui_TimelineTrackSetName(UITimelineTrack *track, const char *name);

void ui_TimelineTrackSetRightClickCallback(UITimelineTrack *track, UITimelineCallback func, UserData data);
void ui_TimelineTrackSetLabelRightClickCallback(UITimelineTrack *track, UIActivationFunc func, UserData data);

//////////////////////////////////////////////////////////////////////////
// Time Line 
//////////////////////////////////////////////////////////////////////////

typedef struct UITimelineLink
{
	UITimelineKeyFrame **items;
} UITimelineLink;

typedef struct UITimelineGroup
{
	UITimelineKeyFrame **items;
	UserData data;
	CBox box;
	bool selected;
} UITimelineGroup;

typedef struct UITimeline
{
	UIWidget widget;

	UITimelineTrack **tracks;

	F32 title_bar_height;
	F32 track_label_width;
	bool draw_top_left_empty_space;
	bool continuous;		//If set then time changed callback will be called while scrubbing

	UITimelineCallback time_changed_func;
	UserData time_changed_data;
	UIPreActivationFunc frame_pre_changed_func;
	UserData frame_pre_changed_data;
	UIActivationFunc frame_changed_func;
	UserData frame_changed_data;
	UIActivationFunc selection_changed_func;
	UserData selection_changed_data;
	UIActivationFunc rc_func;
	UserData rc_data;

	int current_time;
	int total_time;
	F32 time_scale;
	bool max_time_mode;
	bool limit_zoom_out;
	bool time_ticks_in_units;

	UITimelineKeyFrame **selection;
	UITimelineLink **links;
	UITimelineGroup **groups;

	F32 data_height;
	F32 data_width;
	bool scroll_dragging;
	bool selection_dragging;
	bool selection_left_resizing; // when true, so is selection_dragging; mutually exclusive with selection_right_resizing
	bool selection_right_resizing; // when true, so is selection_dragging; mutually exclusive with selection_left_resizing
	bool scrubbing;
	bool drag_selecting;
	bool zoomed_out;

	F32 drag_start_x;
	F32 drag_start_y;
	int drag_start_time;	//Not the time before we started dragging
} UITimeline;

SA_RET_NN_VALID UITimeline *ui_TimelineCreate(F32 x, F32 y, F32 w);
void ui_TimelineInitialize(SA_PARAM_NN_VALID UITimeline *timeline, F32 x, F32 y, F32 w);
void ui_TimelineFreeInternal(SA_PRE_NN_VALID SA_POST_P_FREE UITimeline *timeline);

void ui_TimelineAddTrack(UITimeline *timeline, UITimelineTrack *track);
void ui_TimelineRemoveTrack(UITimeline *timeline, UITimelineTrack *track);

void ui_TimelineSetTimeChangedCallback(UITimeline *timeline, UITimelineCallback func, UserData data);
void ui_TimelineSetFramePreChangedCallback(UITimeline *timeline, UIPreActivationFunc func, UserData data);
void ui_TimelineSetFrameChangedCallback(UITimeline *timeline, UIActivationFunc func, UserData data);
void ui_TimelineSetSelectionChangedCallback(UITimeline *timeline, UIActivationFunc func, UserData data);
void ui_TimelineSetRightClickCallback(UITimeline *timeline, UIActivationFunc func, UserData data);

void ui_TimelineClearSelection(UITimeline *timeline);
void ui_TimelineClearSelectionAndCallback(UITimeline *timeline);
void ui_TimelineClearLinks(UITimeline *timeline);
void ui_TimelineClearGroups(UITimeline *timeline);

int ui_TimelineGetTime(UITimeline *timeline);
int ui_TimelineGetTotalTime(UITimeline *timeline);

void ui_TimelineSetTime(UITimeline *timeline, int time);
void ui_TimelineSetTimeAndCallback(UITimeline *timeline, int time);

void ui_TimelineLinkFrames(UITimeline *timeline, UITimelineKeyFrame **frames);
void ui_TimelineGroupFrames(UITimeline *timeline, UITimelineKeyFrame **frames);

void ui_TimelineDraw(SA_PARAM_NN_VALID UITimeline *timeline, UI_PARENT_ARGS);
void ui_TimelineTick(SA_PARAM_NN_VALID UITimeline *timeline, UI_PARENT_ARGS);

#endif
