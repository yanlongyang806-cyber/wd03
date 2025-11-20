#pragma once
GCC_SYSTEM
/***************************************************************************



***************************************************************************/

#ifndef _SNDQUEUE_H
#define _SNDQUEUE_H

#include "stdtypes.h"

typedef struct StashTableImp* StashTable;
typedef struct SoundSource SoundSource;

typedef struct SoundQueue
{
	int iGroupId;
	StashTable stashParent;
	SoundSource **ppSoundSources;
} SoundQueue;

// The sound manager manages all sound queue groups
typedef struct SoundQueueManager
{
	StashTable stashSoundQueues;
} SoundQueueManager;

extern SoundQueueManager *g_SoundQueueManager;

// initialize the queue manager
void sndQueueManagerInit();

void sndQueueManagerEnqueueSoundSource(SoundQueueManager *soundQueues, SoundSource *source);
void sndQueueManagerSoundSourceFinished(SoundQueueManager *soundQueue,  SoundSource *source);



#endif

