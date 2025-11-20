/***************************************************************************



***************************************************************************/
// Author - Greg Thompson

#pragma once

#ifndef _SNDFX_H
#define _SNDFX_H
GCC_SYSTEM

#include "stdtypes.h"
#include "ReferenceSystem.h"

typedef struct DynFx DynFx;
typedef struct SoundSource SoundSource;
typedef struct SoundDSPEffect SoundDSPEffect;

typedef enum {
	SNDFX_STANDARD_EVENT,
	SNDFX_TRAVEL_POWER,
	SNDFX_REPLACE_HIT_FIX,
	SNDFX_STATIONARY,		// sound does not move
	SNDFX_DSP,
} SoundFxCommandType;

typedef struct SoundFx {
	int ident;
	REF_TO(DynFx) hDynFx;
	SoundFxCommandType command;
	int timestamp;

	SoundSource **eaSoundSources;
	char *eventPath;

	SoundDSPEffect *effect;

	U32 needsUpdate : 1;
	U32 bLocalPlayer : 1; // copies flag from DynFxManager
} SoundFx;

void sndFxSetupCallbacks();

void sndFxInit();
void sndFxExecuteCommandQueue();
void sndFxOncePerFrame();

#endif
