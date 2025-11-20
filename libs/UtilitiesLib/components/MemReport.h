#ifndef _MEMREPORT_H
#define _MEMREPORT_H

#include "MemTrack.h"

// Various global MemTrack counters, for values since the last reading
// realloc() reports as a malloc() and a free()
typedef struct MemOpCounters
{
	MemTrackType outstanding_bytes_high;	// High water mark of total outstanding allocation sizes, in bytes
	MemTrackType outstanding_bytes_low;		// Low water mark of total outstanding allocation sizes, in bytes
	MemTrackType outstanding_count_high;	// High water mark of outstanding allocation count
	MemTrackType outstanding_count_low;		// Low water mark of outstanding allocation count
	MemTrackType delta_alloc_bytes;			// Total allocated bytes
	MemTrackType delta_alloc_count;			// Allocation count
	MemTrackType delta_free_bytes;			// Total freed bytes
	MemTrackType delta_free_count;			// Free count
} MemOpCounters;

typedef void (*OutputHandler)(char *appendMe, void *userdata);
void printMem(void);
void memMonitorDisplayStatsInternal(OutputHandler cb, void *user, int maxlines);
void memMonitorPerLineStatsInternal(OutputHandler cb, void *user, SA_PARAM_OP_STR const char *search, int maxlines, int skip_user_alloc);
void mmpl(void);
void mmplShort(void);
void mmdsShort(void);
int updateMemoryBudgets(void);

void includeMemoryReportInCrashDump();

// This can be called to minimize memory usage in extreme optimization situations, such as test clients.
// The consequence of calling this is that the mmpl on out-of-memory might not work.
void freeEmergencyMemory(void);

S32 memMonitorGetVideoMemUseEstimate(void);
S32 memMonitorPhysicalUntracked(void);
bool filterVideoMemory(const char *moduleName);

void memTrackUpdateWorkingSet(void);

// Read and reset the MemOp counters.
void readMemOpCounters(MemOpCounters *counters);

#endif
