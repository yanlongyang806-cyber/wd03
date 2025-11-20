/***************************************************************************
*     Copyright (c) 2006-2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "referencesystem.h"
#include "stdtypes.h"
//#include "Message.h" // For DisplayMessage
//#include "WorldVariable.h"

#ifndef SHARDEVENTTIMING_COMMON_H
#define SHARDEVENTTIMING_COMMON_H


AUTO_STRUCT;
typedef struct ShardEventTimingEntry
{
	const char *pchStartDate;			AST(NAME(StartDate) STRUCTPARAM)
	const char *pchEndDate;				AST(NAME(EndDate) STRUCTPARAM)

	U32 uDateStart;						AST(NAME(DateStart))
	U32 uTimeEnd;						AST(NAME(TimeEnd))
	U32 uTimeRepeat;					AST(NAME(TimeRepeat))
	U32 uTimeDuration;					AST(NAME(TimeDuration))
	U32 bRandomStartTime : 1;			AST(NAME(RandomStartTime))
} ShardEventTimingEntry;

AUTO_STRUCT;
typedef struct ShardEventTimingDef
{
	ShardEventTimingEntry **ppTimingEntries;	AST(NAME(EventTime, TimingEntry))
	U32	uSeedBase;
} ShardEventTimingDef;


void ShardEventTiming_Validate(ShardEventTimingDef* pShardEventTimingDef, const char* pContextualName);
void ShardEventTiming_Fixup(ShardEventTimingDef* pShardEventTimingDef, U32 uSeedBase);

// Return the starting time of a particular cycle of a repeating timing entry, or the timing entry start date if it is not repeating.
// Ignores the end date.
U32 ShardEventTiming_GetTimingDefStartTimeFromCycle(ShardEventTimingDef* pShardEventTimingDef, int iTimingIndex, U32 uCycleIdx);


// Get useful times surrounding the QueryTime for the given EventDef. See the .c file for more details.
void ShardEventTiming_GetUsefulTimes(ShardEventTimingDef *pShardEventTimingDef, U32 uQueryTime, U32 *puLastStart, U32 *puEndOfLastStart, U32 *puNextStart);

// True if the particular entry in the timing def 'should be on'
bool ShardEventTiming_EntryShouldBeOn(ShardEventTimingDef* pShardEventTimingDef, int iTimingIndex, U32 uQueryTime);

// True if the particular TimingDef 'should be on'. If so, return the index of the entry that is active
bool ShardEventTiming_DefShouldBeOn(ShardEventTimingDef* pShardEventTimingDef, int *iIndexOut, U32 uQueryTime);


#endif
