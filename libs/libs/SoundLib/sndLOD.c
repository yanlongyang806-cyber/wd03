#ifndef STUB_SOUNDLIB

#include "sndLOD.h"

// Standard C Types
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

// Cryptic Structs
#include "earray.h"
#include "MemoryBudget.h"
#include "StashTable.h"
#include "timing.h"

// Audio
#include "soundLib.h"
#include "event_sys.h"
#include "sndSource.h"
#include "sndLibPrivate.h"

#include "sndMixer.h"

extern SoundMixer *gSndMixer;

// Defines
#define AMBIENT_CLIP_DISTANCE_START (250.0)
#define AMBIENT_CLIP_FEET_PER_SEC (25.0)

// Globals
SndLOD gSndLOD;

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Audio););

///////////////////////////////////////////////////////////////////////////////
// Functions
///////////////////////////////////////////////////////////////////////////////

void sndLODInit(SndLOD *lod)
{
	lod->waitToRaiseDurationInSecs = 5.0;
	lod->threshold = 0.75;
	lod->state = SND_LOD_HIGH;

	lod->aboveThreshold = false;
	lod->enabled = true;

	lod->stoppedSources = NULL;

	lod->belowThresholdTicks = lod->lastStateChangeTicks = timerCpuTicks();
	lod->lastStateDurationInSecs = -1;
}

void sndLODSetIsEnabled(SndLOD *lod, bool enabled)
{
	lod->enabled = enabled;
}

bool sndLODIsEnabled(SndLOD *lod)
{
	return lod->enabled;
}

void sndLODSetThreshold(SndLOD *lod, F32 threshold)
{
	// keep it in reasonable range
	CLAMP(threshold, 0.1, 0.95);

	lod->threshold = threshold;
}

F32 sndLODThreshold(SndLOD *lod)
{
	return lod->threshold;
}

SndLODState sndLODState(SndLOD *lod)
{
	return lod->state;
}

const char *sndLODStateAsString(SndLODState state)
{
	static const char *strs[] = { "Lowest", "Medium", "Highest", "Error" };
	int index = (int)state;

	if(index < SND_LOD_LOW) index = SND_LOD_UNKNOWN; 
	if(index >= SND_LOD_UNKNOWN) index = SND_LOD_UNKNOWN;

	return strs[index];
}
F32 sndLODDurationAtCurrentState(SndLOD *lod)
{
	return timerSeconds(timerCpuTicks() - lod->lastStateChangeTicks);
}

SndLODState sndLODLastState(SndLOD *lod)
{
	return lod->lastState;
}

F32 sndLODDurationAtLastState(SndLOD *lod)
{
	return lod->lastStateDurationInSecs;
}

F32 sndLODDurationAboveThreshold(SndLOD *lod)
{
	return 0.0;
}

void sndLODSetWaitDuration(SndLOD *lod, F32 wait)
{
	// keep it in reasonable range
	CLAMP(wait, 0.1, 20.0);

	lod->waitToRaiseDurationInSecs = wait;
}

F32 sndLODWaitDuration(SndLOD *lod)
{
	return lod->waitToRaiseDurationInSecs;
}

F32 sndLODDurationBelowThreshold(SndLOD *lod)
{
	return timerSeconds(timerCpuTicks() - lod->belowThresholdTicks);
}

void sndLODGetStoppedSources(SndLOD *lod, SoundSource ***stoppedSources)
{
	*stoppedSources = lod->stoppedSources;
}

void sndLODReviveSources(SoundSource **stoppedSources)
{
	int i;
	for(i = eaSize(&stoppedSources)-1; i >= 0; i--)
	{
		SoundSource *source = stoppedSources[i];

	}

}

void sndLODStopActiveSourcesByType(SoundType type, SoundSource ***stoppedSources)
{
	static void **events;
	int numEvents;
	int i;

	eaClear(&events);
	fmodEventSystemGetPlaying(&events);
	numEvents = eaSize(&events);

	for(i = 0; i < numEvents; i++)
	{
		FMOD_RESULT result;
		int systemId;
		void *fmod_event = events[i];

		result = FMOD_EventSystem_GetSystemID(fmod_event, &systemId);
		if(!result)
		{
			SoundSourceGroup *group;

			if(stashIntFindPointer(g_audio_state.sndSourceGroupTable, systemId, &group))
			{
				if( group->emd->type & type ) // check the type
				{
					// check active sources for ptr
					int numActive = eaSize(&group->active_sources);
					int j;

					for(j = 0; j < numActive; j++)
					{
						SoundSource *source = group->active_sources[j];
						if(source->fmod_event == fmod_event)
						{
							source->hidden = 1;
							eaPush(stoppedSources, source);
						}
					}
				}
			}
		}
	}
}

void sndLODLowerDetailToState(SndLOD *lod)
{
	switch(lod->state)
	{
		case SND_LOD_LOW:
			break;

		case SND_LOD_MEDIUM:
			lod->ambientClipDistance = AMBIENT_CLIP_DISTANCE_START;
			break;

		case SND_LOD_HIGH: // nothing
			break;
	}
}

void sndLODRaiseDetailToState(SndLOD *lod)
{
	switch(lod->state)
	{
		case SND_LOD_LOW:
			break;

		case SND_LOD_MEDIUM:
			lod->ambientClipDistance = 0;
			break;

		case SND_LOD_HIGH: 
			// begin playing the stopped sources
			//sndLODReviveSources(lod->stoppedSources);
			break;
	}
}

void sndLODChangeState(SndLOD *lod, SndLODState state)
{
	lod->lastStateDurationInSecs = sndLODDurationAtCurrentState(lod);

	lod->lastState = lod->state;
	lod->state = state;
	lod->lastStateChangeTicks = timerCpuTicks(); 

	if(lod->state < lod->lastState)
	{
		sndLODLowerDetailToState(lod);
	} 
	else if(lod->state > lod->lastState)
	{
		sndLODRaiseDetailToState(lod);
	}
	
}

bool sndLODIsSourceAudible(SndLOD *lod, const SoundSource *source)
{
	// default
	bool result = true;

	// all sounds are audible if LOD is not enabled
	if(!lod->enabled) return result;

	if(source->emd)
	{
		// the source requested to ignore LOD state
		if(source->emd->ignoreLOD) return result;

		switch(lod->state)
		{
			case SND_LOD_LOW:
			case SND_LOD_MEDIUM:
			{
				int typeTest = SND_AMBIENT;
				if(source->emd->type & typeTest)
				{
					if(source->distToListener >= lod->ambientClipDistance)
					{
						result = false;
					}
				}
				break;
			}
			case SND_LOD_HIGH:
				break;
		}

	}
	return result;
}

void sndLODUpdate(SndLOD *lod, F32 elapsed)
{
	int memPoolSize, maxAllocated;
	bool aboveThreshold;
	unsigned long curTicks;
	PerfInfoGuard *guard;

	if(lod == NULL) return;
	if(!lod->enabled) return;
	if(ABS_TIME_SINCE(lod->timeLastMemCheck)<SEC_TO_ABS_TIME(0.25))
		return;

	PERFINFO_AUTO_START_FUNC_GUARD(&guard);

	lod->timeLastMemCheck = ABS_TIME;

	PERFINFO_AUTO_START("GetMemStats",1);
	if(!snd_enable_debug_mem)
	{
		FMOD_EventSystem_GetMemStats(&memPoolSize, &maxAllocated);
	}
	else
	{
		MemoryBudget *budget = memBudgetGetBudgetByName(BUDGET_Audio);

		memPoolSize = budget->current;
		maxAllocated = budget->current;
	}
	PERFINFO_AUTO_STOP();

	// check mem pool size against threshold
	PERFINFO_AUTO_START("Check State",1);
	aboveThreshold = memPoolSize > lod->threshold * (float)soundBufferSize;

	if(gSndMixer)
	{
		// only engage if we go 150% above threshold
		int maxPlaybacks = (int)((float)sndMixerMaxPlaybacks(gSndMixer) * 1.5);

		if( maxPlaybacks > 0 && sndMixerNumEventsPlaying(gSndMixer) >= maxPlaybacks )
		{
			aboveThreshold |= true;
		}
	}

	// get current time
	curTicks = timerCpuTicks();

	if(aboveThreshold) {
		if(!lod->aboveThresholdLast)
		{
			// we switched from below to above
			lod->aboveThresholdTicks = curTicks;
		}
		lod->aboveThresholdDuration = timerSeconds(curTicks - lod->aboveThresholdTicks);
	} else {
		if(lod->aboveThresholdLast)
		{
			// we switched from above to below, so mark the time
			lod->belowThresholdTicks = curTicks;
		}
		lod->belowThresholdDuration = timerSeconds(curTicks - lod->belowThresholdTicks);
	}

	lod->aboveThreshold = aboveThreshold; // update current
	lod->aboveThresholdLast = aboveThreshold; // save previous state
	PERFINFO_AUTO_STOP_START("Switch State",1);
	
	switch(lod->state)
	{
		case SND_LOD_LOW:
			lod->ambientClipDistance = 0.0;

			if(aboveThreshold)
			{
				// still above, watch the time
				if(lod->aboveThresholdDuration > lod->waitToRaiseDurationInSecs)
				{
					// there is no lower level and we're still above the threshold
					// we have a problem

				}
			} else {
				// we are now below the threshold
				if(lod->belowThresholdDuration > lod->waitToRaiseDurationInSecs)
				{
					// since we are increasing the LOD below the threshold - mark this time
					sndLODChangeState(lod, lod->state + 1);
				}
			}
			break;

		case SND_LOD_MEDIUM:
			
			if(aboveThreshold)
			{
				// update ambient clip distance
				lod->ambientClipDistance -= AMBIENT_CLIP_FEET_PER_SEC * elapsed;
				if(lod->ambientClipDistance < 0) lod->ambientClipDistance = 0.0; // keep it bounded

				// still above and we've clipped all ambient sources
				if(lod->ambientClipDistance <= 0.0)
				{
					// decrease detail
					sndLODChangeState(lod, lod->state - 1);
				}

			} else {

				// update ambient clip distance
				lod->ambientClipDistance += AMBIENT_CLIP_FEET_PER_SEC * elapsed;
				if(lod->ambientClipDistance > AMBIENT_CLIP_DISTANCE_START) lod->ambientClipDistance = AMBIENT_CLIP_DISTANCE_START; // keep it bounded

				if(lod->ambientClipDistance >= AMBIENT_CLIP_DISTANCE_START)
				{
					// increase detail
					sndLODChangeState(lod, lod->state + 1);
				}
			}
			break;

		case SND_LOD_HIGH:
		{
			if(aboveThreshold)
			{
				// we crossed the threshold, so lower the state
				sndLODChangeState(lod, lod->state - 1);
			}
			break;
		}
	}
	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_STOP_GUARD(&guard);
}

#endif
