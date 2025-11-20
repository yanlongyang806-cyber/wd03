/***************************************************************************
*     Copyright (c) 2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "EditLibTimingLog.h"
#include "EventTimingLog.h"
#include "UILib.h"
#include "UIAutoWidget.h"
#include "Prefs.h"
#include "timing.h"
#include "ThreadManager.h"
#include "Color.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

#define MAX_TIMELINE_SPRITES 600

typedef struct TimingLogTimelineEntry
{
	F32 start_time;
	F32 end_time;
	F32 *all_start_times;
	char *tooltip;
	Color color;
	UISprite *sprite;
	UISprite **start_sprites;
} TimingLogTimelineEntry;

typedef struct TimingLogTimeline
{
	Color background_color;
	UIPane *pane;
	TimingLogTimelineEntry **entries;
} TimingLogTimeline;

typedef struct TimingLogEvent
{
	char *name;

	TimingLogTimeline timeline;
} TimingLogEvent;

typedef struct TimingLogEventOwner
{
	char *name;

	TimingLogTimeline timeline;
	TimingLogEvent **events;
} TimingLogEventOwner;

typedef struct TimingLogEventSystem
{
	char *name;

	TimingLogTimeline timeline;
	TimingLogEventOwner **event_owners;
} TimingLogEventSystem;

typedef struct TimingLogThread
{
	char *name;
	DWORD thread_id;

	TimingLogTimeline timeline;
	TimingLogEventOwner **event_owners;
} TimingLogThread;

//////////////////////////////////////////////////////////////////////////

AUTO_STRUCT;
typedef struct TimingLogData
{
	F32 alpha;				AST( NAME(Opacity) )
	bool paused;			AST( NAME(Paused) )
	int frames_to_display;	AST( NAME(DisplayFrames) )

	AST_STOP

	U64 *frame_times;
	U64 total_time;

	UIWindow *window;
	UIButton *pause_button;
	UIRebuildableTree *uirt;
	UISkin *skin;

	// system view
	TimingLogEventSystem **event_systems;

	// thread view
	TimingLogThread **event_threads;

} TimingLogData;

#include "EditLibTimingLog_c_ast.c"

static TimingLogData timing_log_data;

//////////////////////////////////////////////////////////////////////////

static Color getColorForSystem(const char *system_name)
{
	if (stricmp(system_name, "RenderLib")==0)
		return CreateColorRGB(75, 0, 0);
	if (stricmp(system_name, "GraphicsLib")==0)
		return CreateColorRGB(0, 75, 0);
	if (stricmp(system_name, "WorldLib")==0)
		return CreateColorRGB(0, 0, 75);
	return CreateColorRGB(0, 0, 0);
}

static Color getColorForType(EventLogType event_type, bool use_type_color)
{
	if (!use_type_color)
		return CreateColorRGB(225, 225, 225);
	if (event_type == ELT_RESOURCE)
		return CreateColorRGB(100, 100, 100);
	if (event_type == ELT_CODE)
		return CreateColorRGB(255, 255, 255);
	return CreateColorRGB(255, 225, 225);
}

static void freeTimelineEntry(TimingLogTimelineEntry *entry)
{
	if (!entry)
		return;

	if (entry->sprite)
		ui_WidgetQueueFree(UI_WIDGET(entry->sprite));
	eaDestroyEx(&entry->start_sprites, ui_WidgetQueueFree);
	eafDestroy(&entry->all_start_times);
	SAFE_FREE(entry->tooltip);
	SAFE_FREE(entry);
}

static void freeTimingLogTimelineData(TimingLogTimeline *timeline)
{
	eaDestroyEx(&timeline->entries, freeTimelineEntry);
}

static void freeTimingLogEvent(TimingLogEvent *event_log)
{
	if (!event_log)
		return;

	SAFE_FREE(event_log->name);
	freeTimingLogTimelineData(&event_log->timeline);
	SAFE_FREE(event_log);
}

static void freeTimingLogEventOwner(TimingLogEventOwner *event_owner)
{
	if (!event_owner)
		return;

	eaDestroyEx(&event_owner->events, freeTimingLogEvent);
	SAFE_FREE(event_owner->name);
	freeTimingLogTimelineData(&event_owner->timeline);
	SAFE_FREE(event_owner);
}

static void freeTimingLogEventSystem(TimingLogEventSystem *event_system)
{
	if (!event_system)
		return;

	eaDestroyEx(&event_system->event_owners, freeTimingLogEventOwner);
	SAFE_FREE(event_system->name);
	freeTimingLogTimelineData(&event_system->timeline);
	SAFE_FREE(event_system);
}

static void freeTimingLogThread(TimingLogThread *event_thread)
{
	if (!event_thread)
		return;

	eaDestroyEx(&event_thread->event_owners, freeTimingLogEventOwner);
	SAFE_FREE(event_thread->name);
	freeTimingLogTimelineData(&event_thread->timeline);
	SAFE_FREE(event_thread);
}

static UISkin *skinForMain(UISkin *base)
{
	if (!timing_log_data.skin) {
		timing_log_data.skin = ui_SkinCreate(base);
		timing_log_data.skin->background[0].a = timing_log_data.alpha * 255;
	}
	return timing_log_data.skin;
}

static void eltlOnDataChanged(UIRTNode *node, UserData userData)
{
	if (timing_log_data.skin) {
		timing_log_data.skin->background[0].a = timing_log_data.alpha * 255;
	}

	GamePrefStoreFloat("EventTimingLog_Opacity", timing_log_data.alpha);
	GamePrefStoreInt("EventTimingLog_DisplayFrames", timing_log_data.frames_to_display);
}

AUTO_COMMAND ACMD_NAME(eventProfilerPause);
void eltlTogglePause(void)
{
	if (!timing_log_data.pause_button)
		return;
	timing_log_data.paused = !timing_log_data.paused;
	if (timing_log_data.paused)
		ui_ButtonSetTextAndResize(timing_log_data.pause_button, "Unpause");
	else
		ui_ButtonSetTextAndResize(timing_log_data.pause_button, "Pause");
}

static void eltlTogglePauseButton(UIButton *button, void *unused)
{
	eltlTogglePause();
}

void eltlStartup(void)
{
	timing_log_data.alpha = GamePrefGetFloat("EventTimingLog_Opacity", 170 * U8TOF32_COLOR);
	timing_log_data.frames_to_display = GamePrefGetInt("EventTimingLog_DisplayFrames", 2);
}

static void updateTimeline(TimingLogTimeline *timeline, F64 multiplier, F32 offset)
{
	int i, j;

	for (i = 0; i < eaSize(&timeline->entries); ++i)
	{
		TimingLogTimelineEntry *entry = timeline->entries[i];
		if (entry->end_time <= offset)
		{
			freeTimelineEntry(entry);
			eaRemove(&timeline->entries, i);
			--i;
		}
		else
		{
			entry->start_time -= offset;
			MAX1(entry->start_time, 0);
			entry->start_time *= multiplier;

			entry->end_time -= offset;
			MAX1(entry->end_time, 0);
			entry->end_time *= multiplier;

			for (j = 0; j < eafSize(&entry->all_start_times); ++j)
			{
				entry->all_start_times[j] -= offset;
				if (entry->all_start_times[j] < 0)
				{
					eafRemove(&entry->all_start_times, j);
					--j;
					continue;
				}
				entry->all_start_times[j] *= multiplier;
			}
		}
	}
}

__forceinline static bool timesOverlap(F32 start1, F32 end1, F32 start2, F32 end2)
{
	return !(end2 < start1 || end1 < start2);
}

static void addEventToTimeline(TimingLogTimeline *timeline, EventLogFullEntry *entry, F32 normalized_start, F32 normalized_end, bool use_type_color)
{
	TimingLogTimelineEntry *new_time;

	FOR_EACH_IN_EARRAY_FORWARDS(timeline->entries, TimingLogTimelineEntry, time)
	{
		if (timesOverlap(time->start_time, time->end_time, normalized_start, normalized_end))
		{
			int i;
			for (i = 0; i < eafSize(&time->all_start_times); ++i)
			{
				if (normalized_start > time->all_start_times[i])
				{
					eafInsert(&time->all_start_times, normalized_start, i);
					break;
				}
				else if (normalized_start == time->all_start_times[i])
					break;
			}
			if (i == eafSize(&time->all_start_times))
				eafPush(&time->all_start_times, normalized_start);
			MIN1(time->start_time, normalized_start);
			MAX1(time->end_time, normalized_end);
			return;
		}
		if (time->start_time > normalized_start)
		{
			new_time = calloc(1, sizeof(TimingLogTimelineEntry));
			new_time->start_time = normalized_start;
			new_time->end_time = normalized_end;
			new_time->color = getColorForType(entry->type, use_type_color);
			eafPush(&new_time->all_start_times, normalized_start);

			FOR_EACH_INSERT_BEFORE_CURRENT(timeline->entries, time, new_time);
			return;
		}
	}
	FOR_EACH_END;

	new_time = calloc(1, sizeof(TimingLogTimelineEntry));
	new_time->start_time = normalized_start;
	new_time->end_time = normalized_end;
	new_time->color = getColorForType(entry->type, use_type_color);
	eafPush(&new_time->all_start_times, normalized_start);

	eaPush(&timeline->entries, new_time);
}

static bool addEventToOwner(TimingLogEventOwner *event_owner, EventLogFullEntry *entry, F32 normalized_start, F32 normalized_end)
{
	TimingLogEvent *matching_event = NULL;
	int insert_idx = -1;
	bool changed = false;

	FOR_EACH_IN_EARRAY_FORWARDS(event_owner->events, TimingLogEvent, event_log)
	{
		int cmp = stricmp(event_log->name, entry->name);
		if (cmp > 0)
		{
			insert_idx = FOR_EACH_IDX(event_owner->events, event_log);
			break;
		}
		else if (cmp == 0)
		{
			matching_event = event_log;
			break;
		}
	}
	FOR_EACH_END;

	if (!matching_event)
	{
		matching_event = calloc(1, sizeof(TimingLogEvent));
		matching_event->timeline.background_color = getColorForSystem(entry->system);
		matching_event->name = strdup(entry->name);
		if (insert_idx >= 0)
			eaInsert(&event_owner->events, matching_event, insert_idx);
		else
			eaPush(&event_owner->events, matching_event);
		changed = true;
	}

	addEventToTimeline(&matching_event->timeline, entry, normalized_start, normalized_end, true);

	addEventToTimeline(&event_owner->timeline, entry, normalized_start, normalized_end, true);

	return changed;
}

static bool addEventToThread(TimingLogThread *event_thread, EventLogFullEntry *entry, F32 normalized_start, F32 normalized_end)
{
	TimingLogEventOwner *matching_owner = NULL;
	int insert_idx = -1;
	bool changed = false;

	FOR_EACH_IN_EARRAY_FORWARDS(event_thread->event_owners, TimingLogEventOwner, event_owner)
	{
		int cmp = stricmp(event_owner->name, entry->owner);
		if (cmp > 0)
		{
			insert_idx = FOR_EACH_IDX(event_thread->event_owners, event_owner);
			break;
		}
		else if (cmp == 0)
		{
			matching_owner = event_owner;
			break;
		}
	}
	FOR_EACH_END;

	if (!matching_owner)
	{
		matching_owner = calloc(1, sizeof(TimingLogEventOwner));
		matching_owner->timeline.background_color = getColorForSystem(entry->system);
		matching_owner->name = strdup(entry->owner);
		if (insert_idx >= 0)
			eaInsert(&event_thread->event_owners, matching_owner, insert_idx);
		else
			eaPush(&event_thread->event_owners, matching_owner);
		changed = true;
	}

	changed = addEventToOwner(matching_owner, entry, normalized_start, normalized_end) || changed;

	addEventToTimeline(&event_thread->timeline, entry, normalized_start, normalized_end, false);

	return changed;
}

static bool addEventToSystem(TimingLogEventSystem *event_system, EventLogFullEntry *entry, F32 normalized_start, F32 normalized_end)
{
	TimingLogEventOwner *matching_owner = NULL;
	int insert_idx = -1;
	bool changed = false;

	FOR_EACH_IN_EARRAY_FORWARDS(event_system->event_owners, TimingLogEventOwner, event_owner)
	{
		int cmp = stricmp(event_owner->name, entry->owner);
		if (cmp > 0)
		{
			insert_idx = FOR_EACH_IDX(event_system->event_owners, event_owner);
			break;
		}
		else if (cmp == 0)
		{
			matching_owner = event_owner;
			break;
		}
	}
	FOR_EACH_END;

	if (!matching_owner)
	{
		matching_owner = calloc(1, sizeof(TimingLogEventOwner));
		matching_owner->timeline.background_color = getColorForSystem(entry->system);
		matching_owner->name = strdup(entry->owner);
		if (insert_idx >= 0)
			eaInsert(&event_system->event_owners, matching_owner, insert_idx);
		else
			eaPush(&event_system->event_owners, matching_owner);
		changed = true;
	}

	changed = addEventToOwner(matching_owner, entry, normalized_start, normalized_end) || changed;

	addEventToTimeline(&event_system->timeline, entry, normalized_start, normalized_end, false);

	return changed;
}

static bool eltlUpdateData(void)
{
	bool changed = false;

	PERFINFO_AUTO_START_FUNC();

	PERFINFO_AUTO_START("Window saving", 1);
	GamePrefStoreInt("EventTimingLog_X", timing_log_data.window->widget.x);
	GamePrefStoreInt("EventTimingLog_Y", timing_log_data.window->widget.y);
	GamePrefStoreInt("EventTimingLog_W", timing_log_data.window->widget.width);
	GamePrefStoreInt("EventTimingLog_H", timing_log_data.window->widget.height);
	PERFINFO_AUTO_STOP();

	MAX1(timing_log_data.frames_to_display , 1);

	if (!timing_log_data.paused)
	{
		U64 new_total_time = timing_log_data.total_time;
		F32 offset = 0;
		F64 multiplier = 0;
		EventLogFullEntry **events;

		// evict old frames and update existing frame times
		if (timing_log_data.total_time)
		{
			while (eai64Size(&timing_log_data.frame_times) > timing_log_data.frames_to_display - 1)
			{
				U64 old_time = eai64Remove(&timing_log_data.frame_times, 0);
				new_total_time -= old_time;
			}

			assert(new_total_time >= 0);
			offset = (F32)((timing_log_data.total_time - new_total_time) / (F64)timing_log_data.total_time);
			new_total_time += etlGetLastFrameTime();
			assert(new_total_time > 0);
			multiplier = timing_log_data.total_time / (F64)new_total_time;

			FOR_EACH_IN_EARRAY_FORWARDS(timing_log_data.event_threads, TimingLogThread, event_thread)
			{
				updateTimeline(&event_thread->timeline, multiplier, offset);

				FOR_EACH_IN_EARRAY_FORWARDS(event_thread->event_owners, TimingLogEventOwner, event_owner)
				{
					updateTimeline(&event_owner->timeline, multiplier, offset);

					FOR_EACH_IN_EARRAY_FORWARDS(event_owner->events, TimingLogEvent, event_log)
					{
						updateTimeline(&event_log->timeline, multiplier, offset);
					}
					FOR_EACH_END;
				}
				FOR_EACH_END;
			}
			FOR_EACH_END;

			FOR_EACH_IN_EARRAY_FORWARDS(timing_log_data.event_systems, TimingLogEventSystem, event_system)
			{
				updateTimeline(&event_system->timeline, multiplier, offset);

				FOR_EACH_IN_EARRAY_FORWARDS(event_system->event_owners, TimingLogEventOwner, event_owner)
				{
					updateTimeline(&event_owner->timeline, multiplier, offset);

					FOR_EACH_IN_EARRAY_FORWARDS(event_owner->events, TimingLogEvent, event_log)
					{
						updateTimeline(&event_log->timeline, multiplier, offset);
					}
					FOR_EACH_END;
				}
				FOR_EACH_END;
			}
			FOR_EACH_END;
		}
		else
		{
			new_total_time += etlGetLastFrameTime();
		}

		eai64Push(&timing_log_data.frame_times, etlGetLastFrameTime());
		timing_log_data.total_time = new_total_time;

		// add new frame's events
		offset = (F32)((new_total_time - etlGetLastFrameTime()) / (F64)new_total_time);
		multiplier = 1.0 / (F64)new_total_time;
		events = etlGetLastFrameEvents();

		FOR_EACH_IN_EARRAY_FORWARDS(events, EventLogFullEntry, entry)
		{
			TimingLogThread *matching_thread = NULL;
			TimingLogEventSystem *matching_system = NULL;
			F32 normalized_start, normalized_end;
			int insert_idx = -1;

			normalized_start = offset + multiplier * entry->start_time;
			normalized_end = offset + multiplier * entry->end_time;

			FOR_EACH_IN_EARRAY_FORWARDS(timing_log_data.event_threads, TimingLogThread, event_thread)
			{
				if (event_thread->thread_id < entry->thread_id)
				{
					insert_idx = FOR_EACH_IDX(timing_log_data.event_threads, event_thread);
					break;
				}
				else if (event_thread->thread_id == entry->thread_id)
				{
					matching_thread = event_thread;
					break;
				}
			}
			FOR_EACH_END;

			if (!matching_thread)
			{
				char buffer[1024];
				const char *thread_name = tmGetThreadNameFromId(entry->thread_id);
				matching_thread = calloc(1, sizeof(TimingLogThread));
				matching_thread->timeline.background_color = CreateColorRGB(0, 0, 0);
				matching_thread->thread_id = entry->thread_id;
				sprintf(buffer, "Thread %d", matching_thread->thread_id);
				if (thread_name && thread_name[0])
					strcatf(buffer, " (%s)", thread_name);
				matching_thread->name = strdup(buffer);
				if (insert_idx >= 0)
					eaInsert(&timing_log_data.event_threads, matching_thread, insert_idx);
				else
					eaPush(&timing_log_data.event_threads, matching_thread);
				changed = true;
			}
			changed = addEventToThread(matching_thread, entry, normalized_start, normalized_end) || changed;

			insert_idx = -1;
			FOR_EACH_IN_EARRAY_FORWARDS(timing_log_data.event_systems, TimingLogEventSystem, event_system)
			{
				int cmp = stricmp(event_system->name, entry->system);
				if (cmp > 0)
				{
					insert_idx = FOR_EACH_IDX(timing_log_data.event_systems, event_system);
					break;
				}
				if (cmp==0)
				{
					matching_system = event_system;
					break;
				}
			}
			FOR_EACH_END;

			if (!matching_system)
			{
				matching_system = calloc(1, sizeof(TimingLogEventSystem));
				matching_system->timeline.background_color = getColorForSystem(entry->system);
				matching_system->name = strdup(entry->system);

				if (insert_idx >= 0)
					eaInsert(&timing_log_data.event_systems, matching_system, insert_idx);
				else
					eaPush(&timing_log_data.event_systems, matching_system);
				changed = true;
			}
			changed = addEventToSystem(matching_system, entry, normalized_start, normalized_end) || changed;
		}
		FOR_EACH_END;

		etlDoneWithFrameEvents(events);

		// clear out empty structs
		FOR_EACH_IN_EARRAY_FORWARDS(timing_log_data.event_threads, TimingLogThread, event_thread)
		{
			FOR_EACH_IN_EARRAY_FORWARDS(event_thread->event_owners, TimingLogEventOwner, event_owner)
			{
				FOR_EACH_IN_EARRAY_FORWARDS(event_owner->events, TimingLogEvent, event_log)
				{
					if (eaSize(&event_log->timeline.entries) == 0)
					{
						changed = true;
						freeTimingLogEvent(event_log);
						FOR_EACH_REMOVE_CURRENT(event_owner->events, event_log);
					}
				}
				FOR_EACH_END;

				if (eaSize(&event_owner->events) == 0)
				{
					changed = true;
					freeTimingLogEventOwner(event_owner);
					FOR_EACH_REMOVE_CURRENT(event_thread->event_owners, event_owner);
				}
			}
			FOR_EACH_END;

			if (eaSize(&event_thread->event_owners) == 0)
			{
				changed = true;
				freeTimingLogThread(event_thread);
				FOR_EACH_REMOVE_CURRENT(timing_log_data.event_threads, event_thread);
			}
		}
		FOR_EACH_END;

		FOR_EACH_IN_EARRAY_FORWARDS(timing_log_data.event_systems, TimingLogEventSystem, event_system)
		{
			FOR_EACH_IN_EARRAY_FORWARDS(event_system->event_owners, TimingLogEventOwner, event_owner)
			{
				FOR_EACH_IN_EARRAY_FORWARDS(event_owner->events, TimingLogEvent, event_log)
				{
					if (eaSize(&event_log->timeline.entries) == 0)
					{
						changed = true;
						freeTimingLogEvent(event_log);
						FOR_EACH_REMOVE_CURRENT(event_owner->events, event_log);
					}
				}
				FOR_EACH_END;

				if (eaSize(&event_owner->events) == 0)
				{
					changed = true;
					freeTimingLogEventOwner(event_owner);
					FOR_EACH_REMOVE_CURRENT(event_system->event_owners, event_owner);
				}
			}
			FOR_EACH_END;

			if (eaSize(&event_system->event_owners) == 0)
			{
				changed = true;
				freeTimingLogEventSystem(event_system);
				FOR_EACH_REMOVE_CURRENT(timing_log_data.event_systems, event_system);
			}
		}
		FOR_EACH_END;
	}

	PERFINFO_AUTO_STOP();

	return changed;
}

#define ALIGN_SPOT 300

static void updateTimelineUI(TimingLogTimeline *timeline)
{
	int i, total_sprites = 0;
	char buffer[1024];

	if (!timeline->pane)
		return;

	FOR_EACH_IN_EARRAY_FORWARDS(timeline->entries, TimingLogTimelineEntry, time)
	{
		if (!time->sprite && total_sprites < MAX_TIMELINE_SPRITES)
		{
			time->sprite = ui_SpriteCreate(0, 0, 1, 1, "white");
			time->sprite->widget.widthUnit = UIUnitPercentage;
			time->sprite->widget.heightUnit = UIUnitPercentage;
			ui_WidgetGroupAdd(&timeline->pane->widget.children, UI_WIDGET(time->sprite));
		}

		if (time->sprite)
		{
			time->sprite->widget.width = time->end_time - time->start_time;
			time->sprite->widget.xPOffset = time->start_time;
			time->sprite->tint = time->color;
			total_sprites++;
		}

		for (i = eafSize(&time->all_start_times); i < eaSize(&time->start_sprites); ++i)
		{
			ANALYSIS_ASSUME(time->start_sprites);
			if (time->start_sprites[i])
				ui_WidgetQueueFree(UI_WIDGET(time->start_sprites[i]));
			time->start_sprites[i] = NULL;
		}

		eaSetSize(&time->start_sprites, eafSize(&time->all_start_times));
		for (i = 0; i < eafSize(&time->all_start_times); ++i)
		{
			F32 diff;
			if (i < eafSize(&time->all_start_times)-1)
				diff = time->all_start_times[i+1] - time->all_start_times[i];
			else
				diff = time->end_time - time->all_start_times[i];

			if (!time->start_sprites[i] && total_sprites < MAX_TIMELINE_SPRITES)
			{
				time->start_sprites[i] = ui_SpriteCreate(0, 0, 1, 1, "white");
				time->start_sprites[i]->widget.widthUnit = UIUnitFixed;
				time->start_sprites[i]->widget.heightUnit = UIUnitPercentage;
				time->start_sprites[i]->tint = CreateColorRGB(0, 0, 0);
				ui_WidgetGroupAdd(&timeline->pane->widget.children, UI_WIDGET(time->start_sprites[i]));
			}

			if (time->start_sprites[i])
			{
				sprintf(buffer, "%d ticks", (U32)(diff * (F64)timing_log_data.total_time));
				ui_WidgetSetTooltipString(UI_WIDGET(time->start_sprites[i]), buffer);
				time->start_sprites[i]->widget.xPOffset = time->all_start_times[i];
				total_sprites++;
			}
		}
	}
	FOR_EACH_END;
}

static void addTimelineUI(TimingLogTimeline *timeline, UIRTNode *group, char *name_key, bool to_expander, int depth)
{
	if (!timeline->pane)
	{
		UISprite *sprite;

		timeline->pane = ui_PaneCreate(0, 2, 1.0f, 20, UIUnitPercentage, UIUnitFixed, false);
		timeline->pane->invisible = true;
		timeline->pane->widget.leftPad = to_expander?(depth * UIAUTOWIDGET_INDENT + ALIGN_SPOT - UIAUTOWIDGET_INDENT - 1):0;
		timeline->pane->widget.rightPad = 2;
		timeline->pane->widget.topPad = 2;
		timeline->pane->widget.bottomPad = 2;

		sprite = ui_SpriteCreate(0, 0, 1.f, 2, "white");
		sprite->widget.widthUnit = UIUnitPercentage;
		sprite->widget.heightUnit = UIUnitFixed;
		ui_WidgetSetPositionEx(UI_WIDGET(sprite), 0, -1, 0, 0, UILeft);
		sprite->tint = timeline->background_color;
		ui_WidgetGroupAdd(&timeline->pane->widget.children, UI_WIDGET(sprite));
	}

	updateTimelineUI(timeline);

	ui_WidgetRemoveFromGroup(UI_WIDGET(timeline->pane));

	if (to_expander)
	{
		ui_ExpanderAddLabel(group->expander, UI_WIDGET(timeline->pane));
	}
	else
	{
		UIAutoWidgetParams params={0};
		ui_RebuildableTreeAddLabel(group, name_key, NULL, true);
		params.alignTo = (depth+1) * UIAUTOWIDGET_INDENT + ALIGN_SPOT;
		ui_RebuildableTreeAddWidget(group, UI_WIDGET(timeline->pane), NULL, false, name_key, &params);
	}
}

static void updateEventUI(TimingLogEvent *event_log, UIRTNode *parent)
{
	addTimelineUI(&event_log->timeline, parent, event_log->name, false, 0);
}

static void updateOwnerUI(TimingLogEventOwner *event_owner, UIRTNode *parent)
{
	UIRTNode *owner_group = ui_RebuildableTreeAddGroup(parent, event_owner->name, event_owner->name, false, NULL);
	addTimelineUI(&event_owner->timeline, owner_group, event_owner->name, true, 1);

	FOR_EACH_IN_EARRAY_FORWARDS(event_owner->events, TimingLogEvent, event_log)
	{
		updateEventUI(event_log, owner_group);
	}
	FOR_EACH_END;
}

static void eltlUpdateUI(bool rebuild)
{
	UIRTNode *group;

	PERFINFO_AUTO_START_FUNC();

	if (!rebuild)
	{
		FOR_EACH_IN_EARRAY_FORWARDS(timing_log_data.event_threads, TimingLogThread, event_thread)
		{
			updateTimelineUI(&event_thread->timeline);

			FOR_EACH_IN_EARRAY_FORWARDS(event_thread->event_owners, TimingLogEventOwner, event_owner)
			{
				updateTimelineUI(&event_owner->timeline);

				FOR_EACH_IN_EARRAY_FORWARDS(event_owner->events, TimingLogEvent, event_log)
				{
					updateTimelineUI(&event_log->timeline);
				}
				FOR_EACH_END;
			}
			FOR_EACH_END;
		}
		FOR_EACH_END;

		FOR_EACH_IN_EARRAY_FORWARDS(timing_log_data.event_systems, TimingLogEventSystem, event_system)
		{
			updateTimelineUI(&event_system->timeline);

			FOR_EACH_IN_EARRAY_FORWARDS(event_system->event_owners, TimingLogEventOwner, event_owner)
			{
				updateTimelineUI(&event_owner->timeline);

				FOR_EACH_IN_EARRAY_FORWARDS(event_owner->events, TimingLogEvent, event_log)
				{
					updateTimelineUI(&event_log->timeline);
				}
				FOR_EACH_END;
			}
			FOR_EACH_END;
		}
		FOR_EACH_END;

		PERFINFO_AUTO_STOP_FUNC();

		return;
	}

	ui_RebuildableTreeInit(timing_log_data.uirt, &timing_log_data.window->widget.children, 0, 30, UIRTOptions_YScroll);

	group = ui_RebuildableTreeAddGroup(timing_log_data.uirt->root, "Threads", "Threads", true, NULL);
	{
		FOR_EACH_IN_EARRAY_FORWARDS(timing_log_data.event_threads, TimingLogThread, event_thread)
		{
			UIRTNode *thread_group = ui_RebuildableTreeAddGroup(group, event_thread->name, event_thread->name, false, NULL);
			addTimelineUI(&event_thread->timeline, thread_group, event_thread->name, true, 2);

			FOR_EACH_IN_EARRAY_FORWARDS(event_thread->event_owners, TimingLogEventOwner, event_owner)
			{
				updateOwnerUI(event_owner, thread_group);
			}
			FOR_EACH_END;
		}
		FOR_EACH_END;
	}

	group = ui_RebuildableTreeAddGroup(timing_log_data.uirt->root, "Systems", "Systems", false, NULL);
	{
		FOR_EACH_IN_EARRAY_FORWARDS(timing_log_data.event_systems, TimingLogEventSystem, event_system)
		{
			UIRTNode *system_group = ui_RebuildableTreeAddGroup(group, event_system->name, event_system->name, false, NULL);
			addTimelineUI(&event_system->timeline, system_group, event_system->name, true, 2);

			FOR_EACH_IN_EARRAY_FORWARDS(event_system->event_owners, TimingLogEventOwner, event_owner)
			{
				updateOwnerUI(event_owner, system_group);
			}
			FOR_EACH_END;
		}
		FOR_EACH_END;
	}

	group = ui_RebuildableTreeAddGroup(timing_log_data.uirt->root, "Options", "Options", false, NULL);
	{
		UIAutoWidgetParams params = {0};

		params.type = AWT_Spinner;
		params.min[0] = 1;
		params.max[0] = 10;
		ui_AutoWidgetAdd(group, parse_TimingLogData, "DisplayFrames", &timing_log_data, true, NULL, NULL, &params, "Number of frames for which to display timing information.");

		params.type = AWT_Slider;
		params.min[0] = 0;
		params.max[0] = 1;
		ui_AutoWidgetAdd(group, parse_TimingLogData, "Opacity", &timing_log_data, true, eltlOnDataChanged, NULL, &params, NULL);
	}

	ui_RebuildableTreeDoneBuilding(timing_log_data.uirt);

	PERFINFO_AUTO_STOP_FUNC();
}

void eltlOncePerFrame(void)
{
	PERFINFO_AUTO_START_FUNC();
	if (timing_log_data.window && ui_WindowIsVisible(timing_log_data.window))
	{
		bool changed = eltlUpdateData();
		eltlUpdateUI(changed);
	}
	PERFINFO_AUTO_STOP_FUNC();
}

AUTO_COMMAND ACMD_NAME(eventProfiler);
void eltlShow(void)
{
	bool changed;

	if (timing_log_data.window)
	{
		if (!ui_WindowIsVisible(timing_log_data.window))
		{
			ui_WindowShow(timing_log_data.window);
			etlSetActive(true);
			changed = eltlUpdateData();
			eltlUpdateUI(changed);
		}
		else
		{
			ui_WindowHide(timing_log_data.window);
			etlSetActive(false);
		}
		return;
	}

	timing_log_data.window = ui_WindowCreate("Event Timing Log",
		GamePrefGetInt("EventTimingLog_X", 0),
		GamePrefGetInt("EventTimingLog_Y", 100),
		GamePrefGetInt("EventTimingLog_W", 525),
		GamePrefGetInt("EventTimingLog_H", 264));
	timing_log_data.window->widget.pOverrideSkin = skinForMain(UI_GET_SKIN(timing_log_data.window));
	ui_WindowShow(timing_log_data.window);

	timing_log_data.pause_button = ui_ButtonCreate("Pause", 5, 5, eltlTogglePauseButton, NULL);
	ui_WindowAddChild(timing_log_data.window, timing_log_data.pause_button);

	timing_log_data.uirt = ui_RebuildableTreeCreate();

	etlSetActive(true);

	changed = eltlUpdateData();
	eltlUpdateUI(changed);
}
