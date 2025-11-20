/***************************************************************************



***************************************************************************/

#include "EventTimingLog.h"
#include "timing.h"
#include "MemoryPool.h"
#include "earray.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

typedef struct EventLogSystem EventLogSystem;

typedef struct EventLogTime
{
	S64 start, end;
	EventLogType type;
	DWORD thread_id;
} EventLogTime;

typedef struct EventLogEntry
{
	char *name;
	bool in_progress;
	EventLogTime **times;
} EventLogEntry;

struct EventOwner
{
	char *name;
	char *type;
	bool abandoned;
	EventLogSystem *system;
	EventLogEntry **events_last_frame;
	EventLogEntry **events_this_frame;
};

struct EventLogSystem
{
	char *name;
	EventOwner **event_owners;
};

MP_DEFINE(EventLogEntry);
MP_DEFINE(EventLogTime);
MP_DEFINE(EventLogFullEntry);

static bool etl_inited;
static CRITICAL_SECTION etl_critical_section;

bool etl_active;
static bool etl_active_last_frame;

static EventLogSystem **etl_systems;
static EventOwner **etl_owners;

static S64 etl_last_frame_start;
static S64 etl_last_frame_end;

static EventOwner *etl_default_owner;


//////////////////////////////////////////////////////////////////////////

AUTO_RUN_EARLY;
void etlInit(void)
{
	if (etl_inited)
		return;

	InitializeCriticalSection(&etl_critical_section);
	etl_inited = true;
}

void etlSetActive(bool active)
{
	etl_active = active;
}

static EventLogSystem *etlGetSystem(const char *system_name, bool create_if_not_found)
{
	EventLogSystem *system = NULL;
	int i;

	for (i = 0; i < eaSize(&etl_systems); ++i)
	{
		if (stricmp(etl_systems[i]->name, system_name)==0)
		{
			system = etl_systems[i];
			break;
		}
	}

	if (!system && create_if_not_found)
	{
		system = calloc(1, sizeof(EventLogSystem));
		system->name = strdup(system_name);
		eaPush(&etl_systems, system);
	}

	return system;
}

EventOwner *etlCreateEventOwner(const char *event_owner_name, const char *event_owner_type, const char *sytem_name)
{
	EventLogSystem *system;
	EventOwner *event_owner = NULL;
	int i;

	PERFINFO_AUTO_START_FUNC();

	assert(etl_inited);
	EnterCriticalSection(&etl_critical_section);

	system = etlGetSystem(sytem_name, true);
	for (i = 0; i < eaSize(&system->event_owners); ++i)
	{
		if (stricmp(system->event_owners[i]->name, event_owner_name)==0)
		{
			if (system->event_owners[i]->abandoned)
			{
				event_owner = system->event_owners[i];
				break;
			}
			else
			{
				LeaveCriticalSection(&etl_critical_section);
				PERFINFO_AUTO_STOP();
				return NULL;
			}
		}
	}

	if (event_owner)
	{
		SAFE_FREE(event_owner->type);
		event_owner->abandoned = false;
	}
	else
	{
		event_owner = calloc(1, sizeof(EventOwner));
		event_owner->name = strdup(event_owner_name);
		event_owner->system = system;
		eaPush(&system->event_owners, event_owner);
		eaPush(&etl_owners, event_owner);
	}

	event_owner->type = strdup(event_owner_type);

	LeaveCriticalSection(&etl_critical_section);

	PERFINFO_AUTO_STOP();

	return event_owner;
}

static void etlFreeEventLogTime(EventLogTime *event_time)
{
	MP_FREE(EventLogTime, event_time);
}

static void etlFreeEventLogEntry(EventLogEntry *event_entry)
{
	if (!event_entry)
		return;

	SAFE_FREE(event_entry->name);
	eaDestroyEx(&event_entry->times, etlFreeEventLogTime);
	MP_FREE(EventLogEntry, event_entry);
}

void etlFreeEventOwner(EventOwner *event_owner)
{
	if (!event_owner)
		return;

	PERFINFO_AUTO_START_FUNC();

	assert(etl_inited);
	EnterCriticalSection(&etl_critical_section);

	event_owner->abandoned = true;

	LeaveCriticalSection(&etl_critical_section);

	PERFINFO_AUTO_STOP();
}

void etlSetFrameMarker(void)
{
	int i, j;

	PERFINFO_AUTO_START_FUNC();

	assert(etl_inited);
	EnterCriticalSection(&etl_critical_section);

	etl_last_frame_start = etl_last_frame_end;
	etl_last_frame_end = timerCpuTicks64();

	etl_active_last_frame = etl_active;

	for (i = 0; i < eaSize(&etl_owners); ++i)
	{
		EventOwner *event_owner = etl_owners[i];
		EventLogEntry **temp;

		eaClearEx(&event_owner->events_last_frame, etlFreeEventLogEntry);
		temp = event_owner->events_last_frame;
		event_owner->events_last_frame = event_owner->events_this_frame;
		event_owner->events_this_frame = temp;

		for (j = 0; j < eaSize(&event_owner->events_last_frame); ++j)
		{
			EventLogEntry *event_entry = event_owner->events_last_frame[j];
			if (event_entry->in_progress)
			{
				EventLogEntry *new_event_entry;
				EventLogTime *new_event_time;

				// end the event for the last frame
				event_entry->in_progress = false;
				event_entry->times[eaSize(&event_entry->times)-1]->end = etl_last_frame_end;

				// start the event for the next frame
				new_event_entry = MP_ALLOC(EventLogEntry);
				new_event_entry->name = strdup(event_entry->name);
				eaPush(&event_owner->events_this_frame, new_event_entry);

				new_event_time = MP_ALLOC(EventLogTime);
				eaPush(&new_event_entry->times, new_event_time);
				new_event_time->thread_id = event_entry->times[eaSize(&event_entry->times)-1]->thread_id;
				new_event_time->start = etl_last_frame_end;
				new_event_entry->in_progress = true;
			}
		}

		if (event_owner->abandoned && eaSize(&event_owner->events_last_frame)==0)
		{
			eaFindAndRemove(&event_owner->system->event_owners, event_owner);
			eaFindAndRemove(&etl_owners, event_owner);

			SAFE_FREE(event_owner->name);
			SAFE_FREE(event_owner->type);
			eaDestroyEx(&event_owner->events_this_frame, etlFreeEventLogEntry);
			eaDestroyEx(&event_owner->events_last_frame, etlFreeEventLogEntry);
			free(event_owner);
			--i;
		}
	}

	LeaveCriticalSection(&etl_critical_section);

	PERFINFO_AUTO_STOP();
}

void etlAddEventInternal(EventOwner *event_owner, const char *event_name, EventLogType event_type, EventLogTimingType event_timing_type)
{
	EventLogEntry *event_entry = NULL;
	EventLogTime *event_time;
	int i;
	S64 cur_time;

	if (!etl_active)
		return;

	PERFINFO_AUTO_START_FUNC();

	assert(etl_inited);
	EnterCriticalSection(&etl_critical_section);

	cur_time = timerCpuTicks64();

	MP_CREATE(EventLogEntry, 512);
	MP_CREATE(EventLogTime, 1024);

	if (!event_owner)
	{
		if (!etl_default_owner)
			etl_default_owner = etlCreateEventOwner("Unknown", "Unknown", "Unknown");
		event_owner = etl_default_owner;
	}

	for (i = 0; i < eaSize(&event_owner->events_this_frame); ++i)
	{
		if (stricmp(event_owner->events_this_frame[i]->name, event_name)==0)
		{
			event_entry = event_owner->events_this_frame[i];
			break;
		}
	}

	switch (event_timing_type)
	{
		xcase ELTT_BEGIN:
		{
			if (!event_entry)
			{
				event_entry = MP_ALLOC(EventLogEntry);
				event_entry->name = strdup(event_name);
				eaPush(&event_owner->events_this_frame, event_entry);
			}

			if (event_entry->in_progress)
			{
				event_time = event_entry->times[eaSize(&event_entry->times)-1];
				event_time->end = cur_time;
			}

			event_time = MP_ALLOC(EventLogTime);
			eaPush(&event_entry->times, event_time);
			event_time->thread_id = GetCurrentThreadId();
			event_time->start = cur_time;
			event_time->type = event_type;
			event_entry->in_progress = true;
		}

		xcase ELTT_END:
		{
			if (event_entry && event_entry->in_progress)
			{
				event_time = event_entry->times[eaSize(&event_entry->times)-1];
				event_time->end = cur_time;
				event_entry->in_progress = false;
			}
			else if (etl_active_last_frame)
			{
				Errorf("Got end for event \"%s\" that was never started!", event_name);
			}
		}

		xcase ELTT_INSTANT:
		{
			if (!event_entry)
			{
				event_entry = MP_ALLOC(EventLogEntry);
				event_entry->name = strdup(event_name);
				eaPush(&event_owner->events_this_frame, event_entry);
			}

			event_time = MP_ALLOC(EventLogTime);
			eaPush(&event_entry->times, event_time);
			event_time->thread_id = GetCurrentThreadId();
			event_time->start = cur_time;
			event_time->end = cur_time;
			event_time->type = event_type;
		}

		xdefault:
		{
			Errorf("Unknown event type found!");
		}
	}

	LeaveCriticalSection(&etl_critical_section);

	PERFINFO_AUTO_STOP();
}

//////////////////////////////////////////////////////////////////////////

U64 etlGetLastFrameTime(void)
{
	if (etl_last_frame_end <= etl_last_frame_start)
		return 0;

	return etl_last_frame_end - etl_last_frame_start;
}

static void freeEventLogFullEntry(EventLogFullEntry *entry)
{
	MP_FREE(EventLogFullEntry, entry);
}

EventLogFullEntry **etlGetLastFrameEvents(void)
{
	EventLogFullEntry **entries = NULL;
	int i, j, k;

	PERFINFO_AUTO_START_FUNC();

	assert(etl_inited);
	EnterCriticalSection(&etl_critical_section);

	MP_CREATE(EventLogFullEntry, 1024);

	for (i = 0; i < eaSize(&etl_owners); ++i)
	{
		EventOwner *owner = etl_owners[i];

		for (j = 0; j < eaSize(&owner->events_last_frame); ++j)
		{
			EventLogEntry *event_log = owner->events_last_frame[j];

			for (k = 0; k < eaSize(&event_log->times); ++k)
			{
				EventLogTime *event_time = event_log->times[k];
				EventLogFullEntry *entry = MP_ALLOC(EventLogFullEntry);

				entry->name = event_log->name;
				entry->thread_id = event_time->thread_id;
				assert(event_time->start >= etl_last_frame_start);
				assert(event_time->end >= etl_last_frame_start);
				entry->start_time = event_time->start - etl_last_frame_start;
				entry->end_time = event_time->end - etl_last_frame_start;
				entry->type = event_time->type;
				entry->system = owner->system->name;
				entry->owner = owner->name;
				eaPush(&entries, entry);
			}
		}
	}

	LeaveCriticalSection(&etl_critical_section);

	PERFINFO_AUTO_STOP();

	return entries;
}

void etlDoneWithFrameEvents(EventLogFullEntry **events)
{
	assert(etl_inited);
	EnterCriticalSection(&etl_critical_section);

	eaDestroyEx(&events, freeEventLogFullEntry);

	LeaveCriticalSection(&etl_critical_section);
}

