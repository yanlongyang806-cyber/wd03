#pragma once
GCC_SYSTEM

// fileLoaderStats.h
// Summary statistics definitions for client file streaming system. See fileLoader.h.


typedef struct FileLoaderActionTracking
{
	U64 startTime;
	U64 endTime;
	U64 sizeBytes;

	U32 actionType;
	U32 pad;
} FileLoaderActionTracking;

#define MAX_ACTION_HIST 256

typedef struct FileLoaderSummaryStats
{
	// Summary, read-only in main thread
	// Average loader actions per second during the last MAX_ACTION_HIST actions.
	F32 actionsPerSec;
	// Average loaded K per second during the time span of last MAX_ACTION_HIST actions.
	F32 loadKBPerSec;
	// Average loaded K per second during the time span of last MAX_ACTION_HIST actions, excluding idles.
	F32 loadKBPerSecNonIdle;
	// Average decompression K per second during the time span of last MAX_ACTION_HIST actions.
	F32 decompKBPerSec;
	// Percent of time executing non-disk actions during the time span of last MAX_ACTION_HIST actions.
	F32 idleDiskPerSec;
} FileLoaderSummaryStats;

typedef struct FileLoaderStats
{
	// Summary, read-only in main thread
	FileLoaderSummaryStats summary;

	// History to use to build summaries
	int historyPos;
	FileLoaderActionTracking actionHist[MAX_ACTION_HIST]; // Only accessed/modified from fileLoaderThread
} FileLoaderStats;

void fileLoaderStartLoad();
void fileLoaderCompleteLoad(U64 sizeBytes);

void fileLoaderStatsAddFileLoad(U64 startTime, U64 endTime, U64 sizeBytes);
void fileLoaderStatsAddFileDecompress(U64 startTime, U64 endTime, U64 sizeBytes);
void fileLoaderStatsAddExec(U64 startTime, U64 endTime);

void fileLoaderGetSummaryStats(FileLoaderSummaryStats * stats_out);
void fileLoaderGetStats(FileLoaderStats * stats_out);
