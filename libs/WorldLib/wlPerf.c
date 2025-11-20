#include "wlPerf.h"

#include "timing_profiler.h"
#include "Windows.h"
#include "timing.h"
#include "wlState.h"
#include "TaskProfile.h"

#include "AutoGen/wlPerf_h_ast.c"

WorldPerfInfo world_perf_info;

static S64 frequency = 0;
static F32 msPerTick = 0.0f;

S64 wlPerfGetPerfCyclesPerSecond()
{
	if (!frequency)
	{
		LARGE_INTEGER li;
		QueryPerformanceFrequency(&li);
		frequency = (S64)li.QuadPart;
		msPerTick = 1000.0f / frequency;
	}
	return frequency;
}

F32 wlPerfGetMsPerTick()
{
	wlPerfGetPerfCyclesPerSecond();
	return msPerTick;
}


#define ASSERT_NOT_IN_BUDGET(budget) \
	devassertmsgf(!world_perf_info.assert_on_misnested_budgets || !world_perf_info.in_##budget, "%s called while already in %s budget", __FUNCTION__, #budget);
#define ASSERT_IN_BUDGET(budget) \
	devassertmsgf(!world_perf_info.assert_on_misnested_budgets || world_perf_info.in_##budget, "%s called while not in %s budget", __FUNCTION__, #budget);

#define START_BUDGET(budget)			\
	ASSERT_NOT_IN_BUDGET(budget);	\
	devassertmsg(world_perf_info.pCounts->budget >= 0, "Budget " #budget " invalid at start."); \
	world_perf_info.pCounts->budget -= GetCPUTicks64(); \
	world_perf_info.in_##budget = 1;

#define STOP_BUDGET(budget)			\
	ASSERT_IN_BUDGET(budget);		\
	world_perf_info.pCounts->budget += GetCPUTicks64();	\
	devassertmsg(world_perf_info.pCounts->budget >= 0, "Budget " #budget " invalid at end."); \
	world_perf_info.in_##budget = 0;

static struct {
	PerfInfoGuard* anim;
	PerfInfoGuard* net;
	PerfInfoGuard* waitGPU;
	PerfInfoGuard* draw;
	PerfInfoGuard* ui;
	PerfInfoGuard* queue;
	PerfInfoGuard* queueWorld;
	PerfInfoGuard* cloth;
	PerfInfoGuard* fx;
} piGuard;

void wlPerfSetAssertOnMisnestedBudgets(bool bAssert)
{
	world_perf_info.assert_on_misnested_budgets = bAssert;
}

void wlPerfStartAnimBudget(void)
{
	PERFINFO_AUTO_START_GUARD("AnimBudget", 1, &piGuard.anim);

	ASSERT_IN_BUDGET(time_misc);
	START_BUDGET(time_anim);
}
void wlPerfEndAnimBudget(void)
{
	STOP_BUDGET(time_anim);

	PERFINFO_AUTO_STOP_GUARD(&piGuard.anim);
}

void wlPerfStartClothBudget(WorldPerfFrameCounts * pTimeStamp)
{
//	world_perf_info.in_time_cloth++;
	pTimeStamp->time_cloth -= GetCPUTicks64();
}
void wlPerfEndClothBudget(WorldPerfFrameCounts * pTimeStamp)
{
	pTimeStamp->time_cloth += GetCPUTicks64();
//	world_perf_info.in_time_cloth--;
}

void wlPerfStartSkelBudget(WorldPerfFrameCounts * pTimeStamp)
{
//	world_perf_info.in_time_skel++;
	pTimeStamp->time_skel -= GetCPUTicks64();
}
void wlPerfEndSkelBudget(WorldPerfFrameCounts * pTimeStamp)
{
	pTimeStamp->time_skel += GetCPUTicks64();
//	world_perf_info.in_time_skel--;
}

void wlPerfStartFXBudget(void)
{
	PERFINFO_AUTO_START_GUARD("FXBudget", 1, &piGuard.fx);

	ASSERT_IN_BUDGET(time_misc);
	START_BUDGET(time_fx);
}
void wlPerfEndFXBudget(void)
{
	STOP_BUDGET(time_fx);

	PERFINFO_AUTO_STOP_GUARD(&piGuard.fx);
}


void wlPerfStartNetBudget(void)
{
	PERFINFO_AUTO_START_GUARD("NetBudget", 1, &piGuard.net);

	ASSERT_IN_BUDGET(time_misc);
	START_BUDGET(time_net);
}
void wlPerfEndNetBudget(void)
{
	STOP_BUDGET(time_net);

	PERFINFO_AUTO_STOP_GUARD(&piGuard.net);
}


void wlPerfStartMiscBudget(void)
{
	PERFINFO_AUTO_START("START:MiscBudget", 1);
	PERFINFO_AUTO_STOP();

	START_BUDGET(time_misc);
}
void wlPerfEndMiscBudget(void)
{
	STOP_BUDGET(time_misc);

	PERFINFO_AUTO_START("END:MiscBudget", 1);
	PERFINFO_AUTO_STOP();
}

void wlPerfSortOutAnimTime(void)
{
//	devassert(world_perf_info.in_time_cloth == 0 && world_perf_info.in_time_skel == 0);
	devassert(world_perf_info.pCounts->time_skel >= world_perf_info.pCounts->time_cloth);
	world_perf_info.pCounts->time_skel -= world_perf_info.pCounts->time_cloth;
	//devassert((world_perf_info.pCounts->time_skel+world_perf_info.pCounts->time_cloth)/8 < world_perf_info.pCounts->time_anim);

	//if the timers are messed up, reset them to zero to prevent crashes later on
	MAX1(world_perf_info.pCounts->time_skel,  0);
	MAX1(world_perf_info.pCounts->time_cloth, 0);
}


void wlPerfStartWaitGPUBudget(void)
{
	PERFINFO_AUTO_START_GUARD("WaitGPUBudget", 1, &piGuard.waitGPU);

	ASSERT_IN_BUDGET(time_draw);
	START_BUDGET(time_wait_gpu);
}
void wlPerfEndWaitGPUBudget(void)
{
	STOP_BUDGET(time_wait_gpu);

	PERFINFO_AUTO_STOP_GUARD(&piGuard.waitGPU);
}


void wlPerfStartDrawBudget(void)
{
	PERFINFO_AUTO_START_GUARD("DrawBudget", 1, &piGuard.draw);

	ASSERT_IN_BUDGET(time_misc);
	START_BUDGET(time_draw);
}
void wlPerfEndDrawBudget(void)
{
	STOP_BUDGET(time_draw);

	PERFINFO_AUTO_STOP_GUARD(&piGuard.draw);
}

void wlPerfStartUIBudget(void)
{
	PERFINFO_AUTO_START_GUARD("UIBudget", 1, &piGuard.ui);

	ASSERT_IN_BUDGET(time_misc);
	START_BUDGET(time_ui);
}
void wlPerfEndUIBudget(void)
{
	STOP_BUDGET(time_ui);

	PERFINFO_AUTO_STOP_GUARD(&piGuard.ui);
}

void wlPerfStartQueueBudget(void)
{
	PERFINFO_AUTO_START_GUARD("QueueBudget", 1, &piGuard.queue);

	ASSERT_IN_BUDGET(time_misc);
	START_BUDGET(time_queue);
}
void wlPerfEndQueueBudget(void)
{
	STOP_BUDGET(time_queue);

	PERFINFO_AUTO_STOP_GUARD(&piGuard.queue);
}

void wlPerfStartQueueWorldBudget(void)
{
	PERFINFO_AUTO_START_GUARD("QueueWorldBudget", 1, &piGuard.queueWorld);

	ASSERT_IN_BUDGET(time_queue);
	START_BUDGET(time_queue_world);
}
void wlPerfEndQueueWorldBudget(void)
{
	STOP_BUDGET(time_queue_world);

	PERFINFO_AUTO_STOP_GUARD(&piGuard.queueWorld);
}

void wlPerfStartSoundBudget(void)
{
	PERFINFO_AUTO_START_GUARD("SoundBudget", 1, &piGuard.ui);

	START_BUDGET(time_sound);
}
void wlPerfEndSoundBudget(void)
{
	STOP_BUDGET(time_sound);

	PERFINFO_AUTO_STOP_GUARD(&piGuard.ui);
}

void wlPerfStartPhysicsBudget(S64 * time_physics_budget_start)
{
	if (wlIsClient())
	{
		PERFINFO_AUTO_START_GUARD("PhysicsBudget", 1, &piGuard.ui);

		ASSERT_NOT_IN_BUDGET(time_physics);
		*time_physics_budget_start = GetCPUTicks64();
		world_perf_info.in_time_physics = 1;
	}
}
void wlPerfEndPhysicsBudget(S64 time_physics_budget_start)
{
	if (wlIsClient())
	{
		ASSERT_IN_BUDGET(time_physics);
		world_perf_info.in_time_physics = 0;
		world_perf_info.pCounts->time_physics = GetCPUTicks64() - time_physics_budget_start;

		PERFINFO_AUTO_STOP_GUARD(&piGuard.ui);
	}
}

#undef STOP_BUDGET
#undef START_BUDGET