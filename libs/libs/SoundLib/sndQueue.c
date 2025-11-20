#include "sndQueue.h"

// Cryptic Structs
#include "earray.h"
#include "estring.h"
#include "mathutil.h"
#include "timing.h"
#include "MemoryPool.h"

// Sound structs
#include "soundLib.h"
#include "sndLibPrivate.h"
#include "sndSource.h"

SoundQueueManager *g_SoundQueueManager = NULL;

// Memory Budget
AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Audio););


#ifndef STUB_SOUNDLIB

#include "event_sys.h"

//
// SndQueue private functions
//

SoundQueue* sndQueueInit()
{
	SoundQueue *pSoundQueue = calloc(1, sizeof(SoundQueue));

	return pSoundQueue;
}

void sndQueueDestroy(SoundQueue *pSoundQueue)
{
	eaDestroy(&pSoundQueue->ppSoundSources);

	free(pSoundQueue);
}

int sndQueueIsEmpty(SoundQueue *pSoundQueue)
{
	return eaSize(&pSoundQueue->ppSoundSources) == 0;
}

void sndQueueAddSoundSource(SoundQueue *pSoundQueue, SoundSource *pSoundSource)
{
	// mark the source
	if(!pSoundSource->enqueued)
	{
		pSoundSource->enqueued = 1;

		// put it in the queue (so any future sources will know one of theirs is playing)
		eaPush(&pSoundQueue->ppSoundSources, pSoundSource);
	}
}

void sndQueueRemoveSoundSource(SoundQueue *pSoundQueue, SoundSource *pSoundSource)
{
	// make sure the source that finished is the first in the list
	if(!sndQueueIsEmpty(pSoundQueue))
	{
		if(pSoundSource == pSoundQueue->ppSoundSources[0])
		{
			eaRemove(&pSoundQueue->ppSoundSources, 0); // remove the first element
		
			if(!sndQueueIsEmpty(pSoundQueue))
			{
				SoundSource *pNextSoundSource = pSoundQueue->ppSoundSources[0];

				// we have at least one other event in the queue, play it now
				sndSourcePlay(pNextSoundSource);
				pNextSoundSource->needs_channelgroup = 1;
			}
		}
	}
}

//
// SndQueueManager private functions
//

SoundQueue* sndQueueManagerQueueByGroupId(SoundQueueManager *pSoundQueueManager, int iGroupId)
{
	SoundQueue *pSoundQueue = NULL;
	StashElement pElement;

	if(!pSoundQueueManager->stashSoundQueues)
	{
		pSoundQueueManager->stashSoundQueues = stashTableCreateInt(4);
		return NULL; // of course it won't be here
	}

	if(stashIntFindElement(pSoundQueueManager->stashSoundQueues, iGroupId, &pElement))
	{
		pSoundQueue = stashElementGetPointer(pElement);
	}

	return pSoundQueue;
}

SoundQueue* sndQueueManagerAddQueueForGroupId(SoundQueueManager *pSoundQueueManager, int iGroupId)
{
	SoundQueue *pSoundQueue = sndQueueManagerQueueByGroupId(pSoundQueueManager, iGroupId);
	if(!pSoundQueue)
	{
		pSoundQueue = sndQueueInit();
		pSoundQueue->iGroupId = iGroupId;
		pSoundQueue->stashParent = pSoundQueueManager->stashSoundQueues;
		stashIntAddPointer(pSoundQueueManager->stashSoundQueues, iGroupId, pSoundQueue, 0);
	}
	return pSoundQueue;
}

#endif



//
// SndQueueManager public functions
// note: public functions need to be stubbed properly
//

void sndQueueManagerInit()
{
#ifndef STUB_SOUNDLIB

	g_SoundQueueManager = calloc(1, sizeof(SoundQueueManager));

#endif
}

void sndQueueManagerEnqueueSoundSource(SoundQueueManager *pSoundQueueManager, SoundSource *pSoundSource)
{
#ifndef STUB_SOUNDLIB
	
	SoundQueue *pSoundQueue;
	int iGroupId;

	if(!pSoundQueueManager) return;
	if(!pSoundSource) return;

	iGroupId = pSoundSource->emd->queueGroup;

	pSoundQueue = sndQueueManagerQueueByGroupId(pSoundQueueManager, pSoundSource->emd->queueGroup);
	
	if(!pSoundQueue)
	{
		pSoundQueue = sndQueueManagerAddQueueForGroupId(pSoundQueueManager, iGroupId);
	}

	if(pSoundQueue)
	{
		// first attempt to play the sound immediately if the queue is empty
		if(sndQueueIsEmpty(pSoundQueue))
		{
			sndSourcePlay(pSoundSource);
			pSoundSource->needs_channelgroup = 1;
		}
		

		// make sure we add the source regardless
		sndQueueAddSoundSource(pSoundQueue, pSoundSource);
	}

#endif
}

void sndQueueManagerSoundSourceFinished(SoundQueueManager *pSoundQueueManager,  SoundSource *pSoundSource)
{
#ifndef STUB_SOUNDLIB
	
	SoundQueue *pSoundQueue;

	if(!pSoundQueueManager) return;
	if(!pSoundSource) return;

	pSoundQueue = sndQueueManagerQueueByGroupId(pSoundQueueManager, pSoundSource->emd->queueGroup);
	if(pSoundQueue)
	{
		sndQueueRemoveSoundSource(pSoundQueue, pSoundSource);
		pSoundSource->enqueued = 0;
	}

#endif
}

