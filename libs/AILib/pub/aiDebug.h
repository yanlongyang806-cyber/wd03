#ifndef AIDEBUG_H
#define AIDEBUG_H

#include "aiStruct.h" // for AI_LOG_COUNT and AILogType, move into own header later?

typedef struct AIDebug			AIDebug;
typedef struct AIDebugLogEntry	AIDebugLogEntry;
typedef struct AIVarsBase		AIVarsBase;
typedef struct Entity			Entity;
typedef U32						EntityRef;

#define AI_MAX_LOG_ENTRIES (40960 / sizeof(AIDebugLogEntry))

void aiThinkTickDebug(Entity *e, AIVarsBase *aib, S64 time);

const char* aiGetJobName(Entity* e);
void aiDebugFillStructEntity(Entity *debugger, Entity *be, AIVarsBase *aib, AIDebug* aid, int dontFillMovement);
void aiDebugUpdate(SA_PARAM_NN_VALID Entity* e, SA_PARAM_NN_VALID AIDebug* aid);

AIDebugLogEntry* aiDebugLogEntryCreate(void);
void aiDebugLogEntryDestroy(AIDebugLogEntry* entry);

AIDebugLogEntry* aiDebugLogEntryGetNext(Entity* e);
void aiDebugLogEntryFill(int partitionIdx, AIDebugLogEntry* entry, AILogType logtype, U32 loglevel, U32 logtag, const char* filename,
						 U32 lineNumber, FORMAT_STR const char* formatStr, ...);

void aiDebugLogFlushEntries(Entity* e, AIVarsBase* aib, AIDebugLogEntry*** logEntries);
void aiDebugLogClear(Entity* e);

extern EntityRef aiDebugEntRef;
extern EntityRef aider;				// Shorthand for the above
extern AITeam* aiDebugTeam;
extern AITeam* aiDebugCombatTeam;
extern U32 logsettings[AI_LOG_COUNT];
extern U32 *logtags[AI_LOG_COUNT];

#define AI_DEBUG_ENABLED(logtype, loglevel) \
	(logsettings[logtype] && logsettings[logtype] & (1 << loglevel) && logtags[logtype] && logtags[logtype][0])

#define AI_DEBUG_TAG_ENABLED(logtype, loglevel, logtag) \
	(AI_DEBUG_ENABLED(logtype, loglevel) && logtags[logtype] && logtags[logtype][logtag])

#define AI_DEBUG_PRINT_INTERNAL(e, logtype, loglevel, logtag, format, ...) \
	{ \
		AIDebugLogEntry* logEntry = aiDebugLogEntryGetNext(e); \
		aiDebugLogEntryFill(entGetPartitionIdx(e), logEntry, logtype, loglevel, logtag, __FILE__, __LINE__, FORMAT_STRING_CHECKED(format), __VA_ARGS__); \
	}

#define AI_DEBUG_PRINT_BG(logtype, loglevel, format, ...) \
	if(AI_DEBUG_ENABLED(logtype, loglevel) && eaSize(&toFG->logEntries) < 1000) \
	{	\
		AIDebugLogEntry* logEntry = aiDebugLogEntryCreate(); \
		aiDebugLogEntryFill(0, logEntry, logtype, loglevel, 0, __FILE__, __LINE__, FORMAT_STRING_CHECKED(format), __VA_ARGS__);\
		eaPush(&toFG->logEntries, logEntry); \
		mrmEnableMsgUpdatedToFG(msg); \
	}

#define AI_DEBUG_PRINT_TAG_BG(logtype, loglevel, logtag, format, ...) \
	if(AI_DEBUG_TAG_ENABLED(logtype, loglevel, logtag) && eaSize(&toFG->logEntries) < 1000) \
	{	\
		AIDebugLogEntry* logEntry = aiDebugLogEntryCreate(); \
		aiDebugLogEntryFill(0, logEntry, logtype, loglevel, logtag, __FILE__, __LINE__, FORMAT_STRING_CHECKED(format), __VA_ARGS__);\
		eaPush(&toFG->logEntries, logEntry); \
		mrmEnableMsgUpdatedToFG(msg); \
	}

#define AI_DEBUG_PRINT(e, logtype, loglevel, format, ...) \
	if(AI_DEBUG_ENABLED(logtype, loglevel)) \
		AI_DEBUG_PRINT_INTERNAL(e, logtype, loglevel, 0, format, __VA_ARGS__)

#define AI_DEBUG_PRINT_COND(cond, e, logtype, loglevel, format, ...) \
	if(AI_DEBUG_ENABLED(logtype, loglevel) && (cond)) \
		AI_DEBUG_PRINT_INTERNAL(e, logtype, loglevel, 0, format, __VA_ARGS__)

#define AI_DEBUG_PRINT_TAG(e, logtype, loglevel, logtag, format, ...) \
	if(AI_DEBUG_TAG_ENABLED(logtype, loglevel, logtag)) \
		AI_DEBUG_PRINT_INTERNAL(e, logtype, loglevel, logtag, format, __VA_ARGS__)

#endif