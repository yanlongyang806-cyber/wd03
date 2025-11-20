// Statistical calculations

#pragma once
GCC_SYSTEM

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Here are some special-purpose statistical functions that are reused
// across a few different clients and servers.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Calculate general statistics for a F32 block earray, and add them to a log line.
#define statisticsLogStatsF32(estrLogLine, stat, prefix) statisticsLogStatsF32_dbg(estrLogLine, stat, prefix, __FILE__, __LINE__)
void statisticsLogStatsF32_dbg(char **estrLogLine, F32 *stat, const char *prefix, const char *caller_fname, int line);

// Calculate general statistics for a U64 block earray, and add them to a log line.
// Duplicate of statisticsLogStatsF32() except for different type
#define statisticsLogStatsU64(estrLogLine, stat, prefix) statisticsLogStatsU64_dbg(estrLogLine, stat, prefix, __FILE__, __LINE__)
void statisticsLogStatsU64_dbg(char **estrLogLine, U64 *stat, const char *prefix, const char *caller_fname, int line);

// Calculate general statistics for a U32 block earray, and add them to a log line.
// Duplicate of statisticsLogStatsF32() except for different type
#define statisticsLogStatsU32(estrLogLine, stat, prefix) statisticsLogStatsU32_dbg(estrLogLine, stat, prefix, __FILE__, __LINE__)
void statisticsLogStatsU32_dbg(char **estrLogLine, U32 *stat, const char *prefix, const char *caller_fname, int line);

// Compute a frequency histogram.
void statisticsHistogramF32(F32 **centers, U32 **counts, const F32 *data, int bins, F32 optional_right_limit);

// Get a percentile of sorted data.
// WARNING: This function does no verification that the data is, in fact, sorted.
F32 statisticsSortedPercentile(const F32 *data, int count, F32 percentile);

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Here are some general-purpose statistical functions.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Normal distribution
F32 statisticsNormalDist(F32 z);

// Inverse of normal distribution
#define statisticsPNormalDist(qn) statisticsPNormalDist_dbg(qn, __FILE__, __LINE__)
F32 statisticsPNormalDist_dbg(F32 qn, const char *caller_fname, int line);
