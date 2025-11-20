#ifndef CRYPTIC_MEMTRACKLOG_H
#define CRYPTIC_MEMTRACKLOG_H

// Individual field of MemOpCounters. 
AUTO_STRUCT;
typedef struct MemTrackOpsStatistic
{
	char *pName;		AST(KEY)
	U64 iValue;
} MemTrackOpsStatistic;

// LogParserized version of MemOpCounters, compatible with EArrayContainingDataPoints.
AUTO_STRUCT;
typedef struct MemTrackOpsInfo
{
	EARRAY_OF(MemTrackOpsStatistic) ppStatistics;
} MemTrackOpsInfo;

// Enable or disable memory ops logging to FRAMEPERF.
void enableMemTrackOpsLogging(void);

void SetAllocationTimeAlertThreshold(F32 threshold);
void SetAllocationTimeAlertThrottle(U32 throttle);
void EnableAllocationTimeAlert(bool enable);
#endif  // CRYPTIC_MEMTRACKLOG_H
