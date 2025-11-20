#include "wlTime.h"
#include "wlTimePrivate.h"
#include "wlState.h"
#include "WorldGrid.h"
#include "timing.h"

AUTO_COMMAND;
void wlTimeSet(F32 time)
{
	int iv;
	wlTimeInit(); // In case it's on the command line
	iv = (int)(time / 24);
	wl_state.time = time - iv*24.f;
	while (wl_state.time < 0)
		wl_state.time += 24.0f;
}

void wlTimeSetForce(F32 time)
{
	wlTimeSet(time);
	wl_state.time_is_forced = true;
}

F32 wlTimeGet(void)
{
	return wl_state.time;
}

// start time cycle at 18 instead of 0
#define TIME_CYCLE_START 18

__forceinline static ZoneMapTimeBlock *getTimeBlock(void)
{
	ZoneMapTimeBlock **time_blocks = zmapInfoGetTimeBlocks(NULL);

	if (eaSize(&time_blocks))
	{
		F32 desired_time = time_blocks[0]->time;
		F32 total_time = time_blocks[0]->duration;
		F32 wl_time = wl_state.time;
		int i;

		if (wl_time >= TIME_CYCLE_START)
			wl_time -= 24;
		total_time -= 24 - TIME_CYCLE_START;

		for (i = 1; i < eaSize(&time_blocks); ++i)
		{
			if (wl_time < total_time)
				break;
			desired_time = time_blocks[i]->time;
			total_time += time_blocks[i]->duration;
		}

		return time_blocks[i-1];
	}

	return NULL;
}

const char *wlTimeGetTag(void)
{
	ZoneMapTimeBlock *time_block = getTimeBlock();
	return SAFE_MEMBER(time_block, tag);
}

F32 wlTimeGetClientTime(void)
{
	ZoneMapTimeBlock *time_block = getTimeBlock();
	if (time_block)
		return time_block->time;
	return wl_state.time;
}

AUTO_COMMAND;
void wlTimeSetScale(F32 timescale)
{
	wlTimeInit(); // In case it's on the command line
	wl_state.timescale = timescale;
}

F32 wlTimeGetScale(void)
{
	return wl_state.timescale;
}

bool wlTimeIsForced(void)
{
	return wl_state.time_is_forced;
}

void wlTimeClearIsForced(void)
{
	wl_state.time_is_forced = false;
}

AUTO_COMMAND;
void wlTimeSetStepScaleDebug(F32 timestepscale)
{
	wlTimeInit(); // In case it's on the command line
	wl_state.timeStepScaleDebug = timestepscale;
}

AUTO_COMMAND;
void wlTimeSetStepScaleGame(F32 timestepscale)
{
	wlTimeInit(); // In case it's on the command line
	wl_state.timeStepScaleGame = timestepscale;
}

AUTO_COMMAND;
void wlTimeSetStepScaleLocal(F32 timestepscale)
{
	wlTimeInit(); // In case it's on the command line
	wl_state.timeStepScaleLocal = timestepscale;
}

F32 wlTimeGetStepScale(void)
{
	return wl_state.timeStepScaleDebug * wl_state.timeStepScaleGame * wl_state.timeStepScaleLocal;
}

F32 wlTimeGetStepScaleDebug(void)
{
	return wl_state.timeStepScaleDebug;
}

F32 wlTimeGetStepScaleGame(void)
{
	return wl_state.timeStepScaleGame;
}

F32 wlTimeGetStepScaleLocal(void)
{
	return wl_state.timeStepScaleLocal;
}

//Sets the difference in time between the client and server
void wlTimeUpdateServerTimeDiff(F32 serverTime)
{
	F32 timeDiff = serverTime - wl_state.time;
	if(timeDiff >  12.0f)
		timeDiff -= 24.0f;
	if(timeDiff < -12.0f)
		timeDiff += 24.0f;
	wl_state.serverTimeDiff = timeDiff;
}

void wlTimeUpdate(void)
{
	F32 timeStep = wl_state.timescale * wl_state.timerate * wl_state.frame_time;

	if(IsClient() && wl_state.serverTimeDiff)
	{
		//If the difference is not too large
		if(fabs(wl_state.serverTimeDiff) < 1.0f)
		{
			// Get the amount we want to move towards the server time
			F32 serverDiffTimeStep = wl_state.frame_time * wl_state.serverTimeDiff/10;
			// Make sure we don't move past the server time
			serverDiffTimeStep = (wl_state.serverTimeDiff < 0 ? MAX(serverDiffTimeStep, wl_state.serverTimeDiff) : MIN(serverDiffTimeStep, wl_state.serverTimeDiff));
			// Make sure we are still going forwards in time and not more than double speed
			serverDiffTimeStep = CLAMP(serverDiffTimeStep, 0.000001-timeStep, timeStep*2);
			// Move towards server time and update the difference
			timeStep += serverDiffTimeStep;
			wl_state.serverTimeDiff -= serverDiffTimeStep;
			if(fabs(wl_state.serverTimeDiff) < 0.000001)
				wl_state.serverTimeDiff = 0;
		}
		// Otherwise just set the time
		else
		{
			timeStep += wl_state.serverTimeDiff;
			wl_state.serverTimeDiff = 0;
		}
	}

	wl_state.time += timeStep;
	while (wl_state.time >= 24.0f)
		wl_state.time -= 24.0f;
}

void wlTimeInit(void)
{
	static bool inited=false;
	if (inited)
		return;
	inited = true;
	wl_state.timerate = 14*24 / ((F32)(60*60*24)); // 2 weeks per day.  Unit is hours per second.
	wl_state.timescale = 1;
	wl_state.time = (timeSecondsSince2000() % (24 * 60)) / 60.f;
	wl_state.timeStepScaleDebug = 1.f;
	wl_state.timeStepScaleGame = 1.f;
	wl_state.timeStepScaleLocal = 1.f;
	wl_state.serverTimeDiff = 0.0f;
}

