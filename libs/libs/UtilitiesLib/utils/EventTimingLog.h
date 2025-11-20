#pragma once
GCC_SYSTEM
/***************************************************************************



***************************************************************************/

#ifndef _EVENTTIMINGLOG_H_
#define _EVENTTIMINGLOG_H_

#include "wininclude.h"

typedef struct EventOwner EventOwner;

typedef enum EventLogTimingType
{
	ELTT_BEGIN,
	ELTT_END,
	ELTT_INSTANT,
} EventLogTimingType;

typedef enum EventLogType
{
	ELT_RESOURCE,
	ELT_CODE,
} EventLogType;

void etlSetActive(bool active);

SA_RET_NN_VALID EventOwner *etlCreateEventOwner(SA_PARAM_NN_STR const char *event_owner_name, 
											SA_PARAM_NN_STR const char *event_owner_type, 
											SA_PARAM_NN_STR const char *sytem_name);

void etlFreeEventOwner(SA_PRE_OP_VALID SA_POST_P_FREE EventOwner *event_owner);

void etlSetFrameMarker(void);

extern bool etl_active;

#define etlAddEvent(event_owner, event_name, event_type, event_timing_type)				\
{																						\
	if(etl_active)																		\
		etlAddEventInternal(event_owner, event_name, event_type, event_timing_type);	\
}
void etlAddEventInternal(SA_PARAM_OP_VALID EventOwner *event_owner, SA_PARAM_NN_STR const char *event_name, EventLogType event_type, EventLogTimingType event_timing_type);


//////////////////////////////////////////////////////////////////////////

typedef struct EventLogFullEntry
{
	char *name;
	U64 start_time;
	U64 end_time;
	EventLogType type;
	DWORD thread_id;
	char *system;
	char *owner;
} EventLogFullEntry;

U64 etlGetLastFrameTime(void);
EventLogFullEntry **etlGetLastFrameEvents(void);
void etlDoneWithFrameEvents(EventLogFullEntry **events);

#endif //_EVENTTIMINGLOG_H_
