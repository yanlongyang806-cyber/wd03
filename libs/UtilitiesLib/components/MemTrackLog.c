#include "MemReport.h"
#include "MemTrackLog.h"
#include "MemTrackLog_h_ast.h"
#include "TimedCallback.h"
#include "timing.h"
#include "logging.h"
#include "MemAlloc.h"
#include "osdependent.h"

// If set, memtrack ops logging is enabled.
static bool sbLogMemTrackOps = false;

// Emergency option to disable health logging, if it causes some sort of problem.
static bool sbDisableMemTrackLogHealth = 0;
AUTO_CMD_INT(sbDisableMemTrackLogHealth, DisableMemTrackLogHealth) ACMD_CATEGORY(Debug);

// Enable memtrack ops debugging.
AUTO_COMMAND ACMD_COMMANDLINE ACMD_CATEGORY(Debug);
void LogMemTrackOps(void)
{
	enableMemTrackOpsLogging();
}

// Create a MemTrackOpsStatistic entry.
static MemTrackOpsStatistic *makeMemOpsStat(EARRAY_OF(MemTrackOpsStatistic) *pppStatistics, const char *name)
{
	MemTrackOpsStatistic *statistic = StructCreate(parse_MemTrackOpsStatistic);
	eaPush(pppStatistics, statistic);
	statistic->pName = strdup(name);
	return statistic;
}

// Track memory performance.
static void memOpsCallback(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	MemOpCounters counters;

	// Read counters.
	readMemOpCounters(&counters);

	// If something has happened, log it.
	if (counters.delta_alloc_count || counters.delta_free_count)
	{
		static MemTrackOpsInfo info = {0};
		if (!info.ppStatistics)
		{
			makeMemOpsStat(&info.ppStatistics, "outstanding_bytes_high");
			makeMemOpsStat(&info.ppStatistics, "outstanding_bytes_low");
			makeMemOpsStat(&info.ppStatistics, "outstanding_count_high");
			makeMemOpsStat(&info.ppStatistics, "outstanding_count_low");
			makeMemOpsStat(&info.ppStatistics, "delta_alloc_bytes");
			makeMemOpsStat(&info.ppStatistics, "delta_alloc_count");
			makeMemOpsStat(&info.ppStatistics, "delta_free_bytes");
			makeMemOpsStat(&info.ppStatistics, "delta_free_count");
		}
		info.ppStatistics[0]->iValue = counters.outstanding_bytes_high;
		info.ppStatistics[1]->iValue = counters.outstanding_bytes_low;
		info.ppStatistics[2]->iValue = counters.outstanding_count_high;
		info.ppStatistics[3]->iValue = counters.outstanding_count_low;
		info.ppStatistics[4]->iValue = counters.delta_alloc_bytes;
		info.ppStatistics[5]->iValue = counters.delta_alloc_count;
		info.ppStatistics[6]->iValue = counters.delta_free_bytes;
		info.ppStatistics[7]->iValue = counters.delta_free_count;
		servLogWithStruct(LOG_FRAMEPERF, "MemTrackOps", &info, parse_MemTrackOpsInfo);
	}
}

static void LogHeapCounters(Win7x64HeapCounters *counters)
{
	SERVLOG_PAIRS(LOG_FRAMEPERF, "HeapCounters", 
		("TotalMemoryReserved", "%"FORM_LL"u", counters->TotalMemoryReserved)
		("TotalMemoryCommitted", "%"FORM_LL"u", counters->TotalMemoryCommitted)
		("TotalMemoryLargeUCR", "%"FORM_LL"u", counters->TotalMemoryLargeUCR)
		("TotalSizeInVirtualBlocks", "%"FORM_LL"u", counters->TotalSizeInVirtualBlocks)
		("TotalSegments", "%u", counters->TotalSegments)
		("TotalUCRs", "%u", counters->TotalUCRs)
		("CommittOps", "%u", counters->CommittOps)
		("DeCommitOps", "%u", counters->DeCommitOps)
		("LockAcquires", "%u", counters->LockAcquires)
		("LockCollisions", "%u", counters->LockCollisions)
		("CommitRate", "%u", counters->CommitRate)
		("DecommittRate", "%u", counters->DecommittRate)
		("CommitFailures", "%u", counters->CommitFailures)
		("InBlockCommitFailures", "%u", counters->InBlockCommitFailures)
		("CompactedUCRs", "%u", counters->CompactedUCRs)
		("AllocAndFreeOps", "%u", counters->AllocAndFreeOps)
		("InBlockDeccommits", "%u", counters->InBlockDeccommits)
		("InBlockDeccomitSize", "%"FORM_LL"u", counters->InBlockDeccomitSize)
		("HighWatermarkSize", "%"FORM_LL"u", counters->HighWatermarkSize)
		("LastPolledSize", "%"FORM_LL"u", counters->LastPolledSize)
		("LargeFreeListSize", "%u", counters->LargeFreeListSize));
}

static void LogSpecialHeapCounters(int special_heap, Win7x64HeapCounters *counters)
{
	SERVLOG_PAIRS(LOG_FRAMEPERF, "SpecialHeapCounters", 
		("heapid", "%d", special_heap)
		("TotalMemoryReserved", "%"FORM_LL"u", counters->TotalMemoryReserved)
		("TotalMemoryCommitted", "%"FORM_LL"u", counters->TotalMemoryCommitted)
		("TotalMemoryLargeUCR", "%"FORM_LL"u", counters->TotalMemoryLargeUCR)
		("TotalSizeInVirtualBlocks", "%"FORM_LL"u", counters->TotalSizeInVirtualBlocks)
		("TotalSegments", "%u", counters->TotalSegments)
		("TotalUCRs", "%u", counters->TotalUCRs)
		("CommittOps", "%u", counters->CommittOps)
		("DeCommitOps", "%u", counters->DeCommitOps)
		("LockAcquires", "%u", counters->LockAcquires)
		("LockCollisions", "%u", counters->LockCollisions)
		("CommitRate", "%u", counters->CommitRate)
		("DecommittRate", "%u", counters->DecommittRate)
		("CommitFailures", "%u", counters->CommitFailures)
		("InBlockCommitFailures", "%u", counters->InBlockCommitFailures)
		("CompactedUCRs", "%u", counters->CompactedUCRs)
		("AllocAndFreeOps", "%u", counters->AllocAndFreeOps)
		("InBlockDeccommits", "%u", counters->InBlockDeccommits)
		("InBlockDeccomitSize", "%"FORM_LL"u", counters->InBlockDeccomitSize)
		("HighWatermarkSize", "%"FORM_LL"u", counters->HighWatermarkSize)
		("LastPolledSize", "%"FORM_LL"u", counters->LastPolledSize)
		("LargeFreeListSize", "%u", counters->LargeFreeListSize));
}

U32 gHeapAllocationAlertGenerated[CRYPTIC_END_HEAP] = {0};
U32 gHeapAllocationAlertCount[CRYPTIC_END_HEAP] = {0};

static void GenerateHeapAllocationAlert(int special_heap, float allocation_time)
{
	ErrorOrCriticalAlert("HeapAllocationsTooSlow", "Many slow allocations on heap %s taking %g seconds", HeapNameFromID(special_heap), allocation_time);
}

F32 gAllocationTimeAlertThreshold = 0.010; // 10 milliseconds
AUTO_CMD_FLOAT(gAllocationTimeAlertThreshold, AllocationTimeAlertThreshold) ACMD_CMDLINE;

void SetAllocationTimeAlertThreshold(F32 threshold)
{
	gAllocationTimeAlertThreshold = threshold;
}

U32 gAllocationTimeAlertCriticalThreshold = 10; // If we hit this 10 times in a row, the alert will be critical
AUTO_CMD_INT(gAllocationTimeAlertCriticalThreshold, AllocationTimeAlertCriticalThreshold) ACMD_CMDLINE;

void SetAllocationTimeAlertCriticalThreshold(U32 threshold)
{
	gAllocationTimeAlertCriticalThreshold = threshold;
}

static bool gAllocationTimeAlertEnabled = false;
AUTO_CMD_INT(gAllocationTimeAlertEnabled, AllocationTimeAlertEnabled) ACMD_CMDLINE;

void EnableAllocationTimeAlert(bool enable)
{
	gAllocationTimeAlertEnabled = enable;
}

static bool gDebugForceSlowAllocation = false;
// This forces the slow allocation check to think the allocation was slow. It does not actually make anything happen slowly.
AUTO_CMD_INT(gDebugForceSlowAllocation, DebugForceSlowAllocation) ACMD_CATEGORY(Debug);

static void CheckForSlowAllocation(int special_heap, U32 now, float allocation_time)
{
	// Do no work if this is disabled
	if(gAllocationTimeAlertEnabled)
	{
		// If the allocation time is above the threshold, we should alert when the throttle elapses
		if(allocation_time > gAllocationTimeAlertThreshold || gDebugForceSlowAllocation)
		{
			++gHeapAllocationAlertCount[special_heap];
		}
		else
		{
			gHeapAllocationAlertCount[special_heap] = 0;
		}

		// We have passed the throttle time for the alert, so check if we should send an alert.
		if(gHeapAllocationAlertCount[special_heap] >= gAllocationTimeAlertCriticalThreshold)
		{
			GenerateHeapAllocationAlert(special_heap, allocation_time);
		}
	}
}
// Log some statistics related to heap health.
static void heapHealthCallback(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	int i;
	void *p;
	volatile S64 start, stop;
	float allocation_time;
	const int test_size = 18*1024;
	U32 now = timeSecondsSince2000();

	if (sbDisableMemTrackLogHealth)
		return;

	// Count segments.
	servLog(LOG_FRAMEPERF, "MemTrackHeapSegments", "segments %d", memTrackCountInternalHeapSegments(0));

	// Time an allocation.
	start = timerCpuTicks64();
	p = malloc(test_size);
	free(p);
	stop = timerCpuTicks64();
	allocation_time = timerSeconds64(stop - start);
	servLog(LOG_FRAMEPERF, "MemTrackAllocationTime", "size %d segments %f", test_size, allocation_time);
	CheckForSlowAllocation(0, now, allocation_time);

	// Get largest free block.
	servLog(LOG_FRAMEPERF, "MemTrackLargestFreeBlock", "size %"FORM_LL"u", (U64)memTrackGetInternalLargestFreeBlockSize(0));

	if(IsUsingWin7() && IsUsingX64())
	{
		Win7x64HeapCounters counters = {0};
		memTrackGetWin7InternalHeapData(0, &counters);
		LogHeapCounters(&counters);
	}

	for(i = CRYPTIC_FIRST_HEAP + 1; i <= CRYPTIC_LAST_HEAP; ++i)
	{
		if(IsHeapInitialized(i))
		{
			servLog(LOG_FRAMEPERF, "MemTrackSpecialHeapSegments", "heapid %d, segments %d", i, memTrackCountInternalHeapSegments(i));

			// Time an allocation.
			start = timerCpuTicks64();
			p = malloc_special_heap(test_size, i);
			free(p);
			stop = timerCpuTicks64();
			allocation_time = timerSeconds64(stop - start);
			servLog(LOG_FRAMEPERF, "MemTrackSpecialHeapAllocationTime", "heapid %d size %d segments %f", i, test_size, allocation_time);

			CheckForSlowAllocation(i, now, allocation_time);

			// Get largest free block.
			servLog(LOG_FRAMEPERF, "MemTrackSpecialHeapLargestFreeBlock", "heapid %d size %"FORM_LL"u", i, (U64)memTrackGetInternalLargestFreeBlockSize(i));

			if(IsUsingWin7() && IsUsingX64())
			{
				Win7x64HeapCounters counters = {0};
				memTrackGetWin7InternalHeapData(i, &counters);
				LogSpecialHeapCounters(i, &counters);
			}
		}
	}
}

// Enable or disable memory ops logging to FRAMEPERF.
void enableMemTrackOpsLogging()
{
	if (!sbLogMemTrackOps)
	{
		sbLogMemTrackOps = true;
		TimedCallback_Add(memOpsCallback, NULL, 1);
		TimedCallback_Add(heapHealthCallback, NULL, 60);
	}
}

#include "MemTrackLog_h_ast.c"
