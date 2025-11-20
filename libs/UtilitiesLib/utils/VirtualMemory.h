#pragma once
GCC_SYSTEM

typedef struct MemLog MemLog;

#define MAX_UNMAPPED_BLOCKS 5
typedef struct VirtualMemStats
{
	U64 unmappedMem;
	U64 mappedMem;
	U64 executableMem;
	int mappedCount;
	int freecount;
	U64 wastedMem;
	int wastedCount;
	int executableCount;
	U64 reservedMem;
	int reservedCount;
	U64 largestUnmappedBlocks[MAX_UNMAPPED_BLOCKS];
} VirtualMemStats;

extern VirtualMemStats g_vmStats;

// See virtualMemoryMakeStatsString; this is the required buffer length, assuming all variable-length
// fields need the maximum length, with some additional padding.
#define VIRTUAL_MEMORY_STATS_STRING_MAX_LENGTH 600

void virtualMemoryMakeStatsString(const VirtualMemStats *vm_stats, char *strStats, size_t strStats_size);
void virtualMemoryMakeShortStatsString(const VirtualMemStats *vm_stats, char *strStats, size_t strStats_size);
void virtualMemoryMemlogStats(MemLog *destMemLog, const char *strMarker, VirtualMemStats *vm_stats);
void virtualMemoryAnalyzeAndMemlogStats(MemLog *destMemLog, const char *strMarker, VirtualMemStats *vm_stats);
void virtualMemoryAnalyzeStats(VirtualMemStats *vm_stats);
void virtualMemoryUpdateDebugStats();
void virtualMemoryAnalyze();
void generateVirtualMemoryLayout();

