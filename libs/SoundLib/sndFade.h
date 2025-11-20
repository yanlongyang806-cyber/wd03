/***************************************************************************



***************************************************************************/

/* This file contains the public interface to the sound library */

#pragma once

#ifndef _SNDFADE_H
#define _SNDFADE_H
GCC_SYSTEM

#include "stdtypes.h"

#define SND_STANDARD_FADE 2.f 

typedef struct SoundFadeManager SoundFadeManager;

// Basically telling SFIs what function to use to fade
typedef enum {
	SFT_FLOAT,
	SFT_CHANNEL_GROUP,
	SFT_DSPCONNECTION,
	SFT_EVENT,
	SFT_LOD
} SoundFadeType;

typedef struct SoundFadeInstance {
	SoundFadeType type;
	SoundFadeManager *fadeManager;
	void *fade_object;

	F32 minvolume;
	F32 maxvolume;
	F32 volume;
	F32 effective_volume;
	F32 desired_volume;
	F32 rate;

	// State flags
	U32 complete : 1;
} SoundFadeInstance;

typedef struct SoundFadeManager {
	SoundFadeInstance **instances;
	SoundFadeInstance **complete;

	// Operational flags
	U32 removeOnComplete : 1;

	// Status flags
	U32 touched : 1;
} SoundFadeManager;

SoundFadeManager* sndFadeManagerCreate(int removeOnComplete);
void sndFadeManagerDestroy(SoundFadeManager *man);
void sndFadeManagerAddEx(SoundFadeManager *man, void *object, SoundFadeType type, F32 rate, F32 mn, F32 mx, int use_desired_effective);
#define sndFadeManagerAdd(m, o, t, r) sndFadeManagerAddEx((m), (o), (t), (r), 0, 1, 0)
#define sndFadeManagerAddMinMax(m, o, t, r, mn, mx) sndFadeManagerAddEx((m), (o), (t), (r), (mn), (mx), 0)
void sndFadeManagerRemove(SoundFadeManager *man, void *object);
void sndFadeManagerUpdate(SoundFadeManager *man, F32 deltaTime);
void sndFadeManagerClearComplete(SoundFadeManager *man);

U32 sndFadeManagerInProgress(SoundFadeManager *man);

void sndUpdateFadeManagers(F32 deltaTime);

#endif
