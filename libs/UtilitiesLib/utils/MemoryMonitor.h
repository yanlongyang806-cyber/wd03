#ifndef MEMORYMONITOR_H
#define MEMORYMONITOR_H
#pragma once
GCC_SYSTEM

C_DECLARATIONS_BEGIN

typedef struct MemoryBudget MemoryBudget;

#include "MemTrack.h"
#include "MemoryBudget.h"

//-------------------------------------------------------------------------------------
// Memory Monitor
//-------------------------------------------------------------------------------------

AUTO_STRUCT;
typedef struct ModuleMemOperationStats
{
	const char* moduleName;
	MemoryBudget *parentBudget;
	U64 size; // Size of outstanding allocations
	U64 sizeTraffic; // Total size of all current and previous allocations
	S32 count; // Count of outstanding allocations
	S32 countTraffic; // Total count of all current and previous allocations
	U64 workingSetSize;
	S32 workingSetCount;
	bool bShared;
	bool bShortTerm; // Short-lived stats object, used internally
	U8 frame_id;
} ModuleMemOperationStats;

extern ParseTable parse_ModuleMemOperationStats[];
#define TYPE_parse_ModuleMemOperationStats ModuleMemOperationStats
extern char memMonitorBreakOnAlloc[MAX_PATH];
extern int memMonitorBreakOnAllocLine;
extern bool memMonitorBreakOnAllocDisableReset;

void memMonitorInit(void);

#define MM_ALLOC 1
#define MM_REALLOC 0
#define MM_FREE -1

void memMonitorUpdateStatsShared(const char *moduleName, MemTrackType size);

void memMonitorTrackUserMemory(const char *moduleName, int staticModuleName, MemTrackType sizeDelta, MemTrackType countDelta);

//-------------------------------------------------------------------------------------
// Memory Monitor stat dumping
//-------------------------------------------------------------------------------------

typedef int (__cdecl *MMOSSortCompare)(const ModuleMemOperationStats** elem1, const ModuleMemOperationStats** elem2);
void memMonitorDisplayStatsInternal(OutputHandler handler, void *userdata, int maxlines);
void memMonitorDisplayStats(void);

// frame counters
void memMonitorResetFrameCounters(void);

size_t memMonitorGetTotalTrackedMemory(void); // Slowish
size_t memMonitorGetTotalTrackedMemoryEstimate(void); // Very fast

int memTestRange(void *p, size_t size); // returns non-zero if bad

C_DECLARATIONS_END

#endif
