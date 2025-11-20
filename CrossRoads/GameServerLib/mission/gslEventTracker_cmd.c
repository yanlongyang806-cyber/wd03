/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "GameEvent.h"
#include "gslEventTracker.h"
#include "gslPartition.h"


// ----------------------------------------------------------------------------------
// Static Data
// ----------------------------------------------------------------------------------

// Turns on debugging information for the event system
U32 g_EventLogDebug = 0;
AUTO_CMD_INT(g_EventLogDebug, eventlog_Debug);

U32 g_EventLogTest = 0;
AUTO_CMD_INT(g_EventLogTest, eventlog_Test);

extern GameEvent *s_EventLogDebugFilter;

// ----------------------------------------------------------------------------------
// Debug Commands
// ----------------------------------------------------------------------------------

AUTO_COMMAND ACMD_SERVERCMD ACMD_NAME(EventLogSetFilter);
void eventtracker_EventLogSetFilter(const char *pcEventAsString)
{
	if (s_EventLogDebugFilter) {
		StructDestroy(parse_GameEvent, s_EventLogDebugFilter);
	}
	
	s_EventLogDebugFilter = gameevent_EventFromString(pcEventAsString);
	if (s_EventLogDebugFilter)
	{
		ANALYSIS_ASSUME(s_EventLogDebugFilter);
		if (s_EventLogDebugFilter->iPartitionIdx == 0) {
			s_EventLogDebugFilter->iPartitionIdx = PARTITION_ANY;
		}

		printf("New EventLogDebug filter:\n");
		eventtracker_DebugPrint(s_EventLogDebugFilter, 0);
	}
	else
	{
		printf("Unknown event filter string\n");
	}
}

