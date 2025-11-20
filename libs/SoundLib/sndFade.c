/***************************************************************************



***************************************************************************/

#ifndef STUB_SOUNDLIB

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include "earray.h"
#include "timing.h"

#include "event_sys.h"

#include "sndFade.h"
#include "sndObject.h"
#include "sndSource.h"

#include "sndLibPrivate.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Audio););

SoundFadeManager **fadeManagers;

SoundFadeManager* sndFadeManagerCreate(int removeOnComplete)
{
	SoundFadeManager *fadeManager;

	PERFINFO_AUTO_START(__FUNCTION__, 1);
	
	fadeManager = callocStruct(SoundFadeManager);

	fadeManager->removeOnComplete = !!removeOnComplete;

	eaPush(&fadeManagers, fadeManager);

	PERFINFO_AUTO_STOP();

	return fadeManager;
}

void sndFadeManagerDestroy(SoundFadeManager *fadeManager)
{
	int result;
	
	PERFINFO_AUTO_START(__FUNCTION__, 1);

	eaDestroyEx(&fadeManager->instances, NULL);
	result = eaFindAndRemoveFast(&fadeManagers, fadeManager);
	devassert(result!=-1);

	free(fadeManager);

	PERFINFO_AUTO_STOP();
}

static F32 sndFadeInstanceGetVolume(SoundFadeInstance *inst)
{
	switch(inst->type)
	{
		xcase SFT_FLOAT: {
			return *(float*)inst->fade_object;
		}
		xcase SFT_DSPCONNECTION: {
			return fmodDSPConnectionGetVolume(inst->fade_object);
		}
		break;
		case SFT_LOD:
		case SFT_EVENT: {
			SoundSource *source = (SoundSource*)inst->fade_object;
			return source->fade_level;
		}
		xcase SFT_CHANNEL_GROUP: {
			return fmodChannelGroupGetVolume(inst->fade_object);
		}
		xdefault: {
			devassert(0);
		}
	}

	return 0;
}

static void sndFadeInstanceSetVolume(SoundFadeInstance *inst)
{
	switch(inst->type)
	{
		xcase SFT_FLOAT: {
			*(float*)inst->fade_object = inst->volume;
		}
		xcase SFT_DSPCONNECTION: {
			fmodDSPConnectionSetVolume(inst->fade_object, inst->volume);
		}
		break;
		case SFT_LOD:
		case SFT_EVENT: {
			SoundSource *source = (SoundSource*)inst->fade_object;
			source->fade_level = inst->volume;
		}
		xcase SFT_CHANNEL_GROUP: {
			fmodChannelGroupSetVolume(inst->fade_object, inst->volume);
		}
		xdefault: {
			devassert(0);
		}
	}
}

void sndFadeManagerAddEx(SoundFadeManager *fadeManager, void *object, SoundFadeType type, F32 rate, F32 mn, F32 mx, int use_desired_effective)
{
	int i;
	SoundFadeInstance *inst;

	if(!verify(object))
	{
		return;
	}

	if(!fadeManager)
	{
		return;
	}

	for(i=eaSize(&fadeManager->instances)-1; i>=0; i--)
	{
		inst = fadeManager->instances[i];

		if(inst->fade_object==object)
		{
			inst->volume = sndFadeInstanceGetVolume(inst);
			inst->rate = rate;
			return;
		}
	}

	for(i=eaSize(&fadeManager->complete)-1; i>=0; i--)
	{
		inst = fadeManager->complete[i];

		if(inst->fade_object==object)
		{
			if(SIGN(inst->rate)!=SIGN(rate))
			{
				eaRemoveFast(&fadeManager->complete, i);
				eaPush(&fadeManager->instances, inst);

				inst->rate = rate;
				inst->complete = 0;
			}

			return;
		}
	}

	inst = callocStruct(SoundFadeInstance);

	inst->fadeManager = fadeManager;
	inst->fade_object = object;
	inst->type = type;
	inst->rate = rate;
	inst->minvolume = mn;
	inst->maxvolume = mx;
	inst->volume = sndFadeInstanceGetVolume(inst);

	if((inst->rate>0 && inst->volume>1) ||
		(inst->rate<0 && inst->volume<0))
	{
		inst->complete = 1;
		if(fadeManager->removeOnComplete)
		{
			eaPush(&fadeManager->complete, inst);
			
			
		}
		else
		{
			eaPush(&fadeManager->instances, inst);
			
			
		}
	}
	else
	{
		eaPush(&fadeManager->instances, inst);
		
		
	}
}

void sndFadeManagerRemove(SoundFadeManager *fadeManager, void *object)
{
	SoundFadeInstance *inst = NULL;
	int i;

	PERFINFO_AUTO_START(__FUNCTION__, 1);

	for(i=eaSize(&fadeManager->instances)-1; i>=0; i--)
	{
		inst = fadeManager->instances[i];
		if(inst->fade_object == object)
		{
			free(inst);
			eaRemoveFast(&fadeManager->instances, i);
		}
	}

	for(i=eaSize(&fadeManager->complete)-1; i>=0; i--)
	{
		inst = fadeManager->complete[i];
		if(inst->fade_object == object)
		{
			free(inst);
			eaRemoveFast(&fadeManager->complete, i);
		}
	}

	PERFINFO_AUTO_STOP();
}

void sndFadeInstanceUpdate(SoundFadeInstance *inst, F32 deltaTime)
{
	inst->volume += inst->rate * deltaTime;

	MINMAX1(inst->volume, inst->minvolume, inst->maxvolume);
	
	sndFadeInstanceSetVolume(inst);	
	inst->volume = sndFadeInstanceGetVolume(inst);
	
	if((inst->volume==1 && inst->rate>0) || 
		(inst->volume==0 && inst->rate<0))
	{
		inst->complete = 1;
	}

	if(inst->fadeManager->removeOnComplete && inst->complete)
	{
		eaFindAndRemoveFast(&inst->fadeManager->instances, inst);
		eaPush(&inst->fadeManager->complete, inst);
	}
}

void sndFadeManagerUpdate(SoundFadeManager *fadeManager, F32 deltaTime)
{
	int i;
	
	if(fadeManager->touched)
	{
		return;
	}
	fadeManager->touched = 1;

	for(i=eaSize(&fadeManager->instances)-1; i>=0; i--)
	{
		SoundFadeInstance *inst = fadeManager->instances[i];

		sndFadeInstanceUpdate(inst, deltaTime);
	}
}

void sndFadeManagerClearComplete(SoundFadeManager *fadeManager)
{
	eaClearEx(&fadeManager->complete, NULL);
	
	//sndPrintf(3, SNDDBG_MUSIC, "sndFadeManagerClearComplete(%p) : fadeManager->complete\n", fadeManager);
}

U32 sndFadeManagerInProgress(SoundFadeManager *man)
{
	if(man->removeOnComplete)
	{
		return eaSize(&man->instances) > 0;
	}
	else
	{
		int i;

		for(i=0; i<eaSize(&man->instances); i++)
		{
			SoundFadeInstance *inst = man->instances[i];

			if((inst->rate > 0 && inst->volume < 1) ||
				(inst->rate < 0 && inst->volume > 0))
			{
				return 1;
			}
		}

		return 0;
	}

	return 0;
}

void sndUpdateFadeManagers(F32 deltaTime)
{
	int i;

	PERFINFO_AUTO_START_FUNC();

	for(i=eaSize(&fadeManagers)-1; i>=0; i--)
	{
		SoundFadeManager *fadeManager = fadeManagers[i];

		fadeManager->touched = 0;
	}

	for(i=eaSize(&fadeManagers)-1; i>=0; i--)
	{
		SoundFadeManager *fadeManager = fadeManagers[i];

		sndFadeManagerUpdate(fadeManager, deltaTime);
	}

	PERFINFO_AUTO_STOP();
}

#endif
