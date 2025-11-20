/***************************************************************************



***************************************************************************/

#pragma once
GCC_SYSTEM

typedef struct DefineContext DefineContext;

extern DefineContext *g_pDefineActivityLogTypes;
AUTO_ENUM AEN_EXTEND_WITH_DYNLIST(g_pDefineActivityLogTypes);
typedef enum ActivityLogEntryType
{
	ActivityLogEntryType_None,		// used for filtering
	ActivityLogEntryType_All,		// used for filtering
	ActivityLogEntryType_LevelUp,
	ActivityLogEntryType_GuildJoin,
	ActivityLogEntryType_GuildLeave,
	ActivityLogEntryType_GuildRankChange,
	ActivityLogEntryType_GuildCreate,
	ActivityLogEntryType_SimpleMissionComplete,
	ActivityLogEntryType_MAX, EIGNORE
} ActivityLogEntryType;