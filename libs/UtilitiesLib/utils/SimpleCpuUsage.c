#include "SimpleCpuUsage.h"
#include "SimpleCpuUsage_h_ast.h"

#include "timing.h"
#include "mutex.h"
#include "earray.h"

#include "ThreadSafeMemoryPool.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

static SimpleCpuThreadData **s_eaSimpleCpuThreadData = NULL;

static CrypticalSection s_eaSimpleCpuThreadDataCS;

bool g_SimpleCpuEnabled = false;

void simpleCpu_SetEnabled(bool enabled)
{
	csEnter(&s_eaSimpleCpuThreadDataCS); // must enter critical section because threads clocking data read the enabled flag to early out.
	{
		if(g_SimpleCpuEnabled != enabled)
		{
			if(g_SimpleCpuEnabled)
			{
				// we are turning it off; destroy currently clocked data
				eaDestroy(&s_eaSimpleCpuThreadData);
			}

			g_SimpleCpuEnabled = enabled;
		}
	}
	csLeave(&s_eaSimpleCpuThreadDataCS);
}

bool simpleCpu_IsEnabled(void)
{
	return g_SimpleCpuEnabled;
}

void simpleCpu_ThreadClock(SimpleCpuUsageThread thread, S64 s64TimerCpuTicksStart, S64 s64TimerCpuTicksEnd)
{
	// Invalid values that create incorrect spikes in the graph
	if(s64TimerCpuTicksStart == 0 || s64TimerCpuTicksEnd == 0 || s64TimerCpuTicksStart > s64TimerCpuTicksEnd)
		return;

	csEnter(&s_eaSimpleCpuThreadDataCS);
	{
		if(g_SimpleCpuEnabled)
		{
			if(eaSize(&s_eaSimpleCpuThreadData) < 512) // don't grow buffer forever
			{
				SimpleCpuThreadData *pSimpleCpuThreadData = StructCreate(parse_SimpleCpuThreadData);

				pSimpleCpuThreadData->thread = thread;
				pSimpleCpuThreadData->s64TimerCpuTicks = s64TimerCpuTicksEnd - s64TimerCpuTicksStart;

				eaPush(&s_eaSimpleCpuThreadData, pSimpleCpuThreadData);
			}
		}
	}
	csLeave(&s_eaSimpleCpuThreadDataCS);
}

void simpleCpu_CaptureFrames(SimpleCpuThreadData ***peaSimpleCpuThreadDataOut)
{
	devassertmsg(!(*peaSimpleCpuThreadDataOut), __FUNCTION__ "Capturing data into an already existing array?");

	(*peaSimpleCpuThreadDataOut) = NULL;

	csEnter(&s_eaSimpleCpuThreadDataCS);
	{
		*peaSimpleCpuThreadDataOut = s_eaSimpleCpuThreadData;

		s_eaSimpleCpuThreadData = NULL;
	}
	csLeave(&s_eaSimpleCpuThreadDataCS);
}

TSMP_DEFINE(SimpleCpuThreadData);

AUTO_RUN;
void simpleCpu_tsmInit(void)
{
	TSMP_CREATE(SimpleCpuThreadData, 2^14);
}

#include "SimpleCpuUsage_h_ast.c"
