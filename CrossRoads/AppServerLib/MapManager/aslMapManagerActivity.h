/***************************************************************************
*     Copyright (c) 2005-2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "referencesystem.h"
#include "ActivityCommon.h"

typedef struct ActivityDef ActivityDef;
typedef struct EventDef EventDef;

AUTO_STRUCT;
typedef struct ActiveActivityEntry
{
	const char* pchActivityName;		AST(POOL_STRING)
	ActivityDef *pActivityDef;			AST(UNOWNED)

	U32 uActivityTimeStart;		// These times are copied from our parent event or set (if unowned) when the ActiveActivityEntry is created.
	U32 uActivityTimeEnd;		// 0xffffffff if unending
}ActiveActivityEntry;

AUTO_STRUCT;
typedef struct ActiveEventEntry
{
	REF_TO(EventDef) hEvent; AST( REFDICT ( Event ) NAME ( Event ))

	ActiveActivityEntry **ppActivities;
	ActiveActivityEntry **ppDelayActivities;

	U32 uEventTimeStart;
	U32 uEventTimeEnd;
	int iTimingIndex;
}ActiveEventEntry;

void aslMapManagerEvent_Tick(void);
void aslMapManager_MapInitActivities(U32 iServerID);

///////////////////////////////////////////////////////////////////////////////////////////////////
//  Monitor sutff

void aslMapManagerActivity_InitEventList(void);

AUTO_STRUCT AST_FORMATSTRING(HTML_DEF_FIELDS_TO_SHOW = "Status, NextHappeningStatus, LastStartDatePAC, EndDatePAC, NextStartDatePAC, LastStartDateUTC, EndDateUTC, NextStartDateUTC, TurnOn, UseSchedule, TurnOff");
typedef struct EventList
{
	const char *pEventName; AST(POOL_STRING KEY)
		 
	const char *pStatus;	AST(POOL_STRING)

	char *pNextHappeningStatus;	AST(ESTRING)
		 
	char *pLastStartDatePAC;	AST(ESTRING)
	char *pEndDatePAC;			AST(ESTRING)
	char *pNextStartDatePAC;	AST(ESTRING)

	char *pLastStartDateUTC;	AST(ESTRING)
	char *pEndDateUTC;			AST(ESTRING)
	char *pNextStartDateUTC;	AST(ESTRING)
		 

	AST_COMMAND("TurnOn", "aslMapManager_SetEventOn $FIELD(EventName) $NORETURN")
	AST_COMMAND("UseSchedule", "aslMapManager_SetEventAuto $FIELD(EventName) $NORETURN")
	AST_COMMAND("TurnOff", "aslMapManager_SetEventOff $FIELD(EventName) $NORETURN")

} EventList;

