/***************************************************************************



***************************************************************************/


/* This file contains the public interface to the sound library */

#pragma once

#ifndef _SNDLOD_H
#define _SNDLOD_H
GCC_SYSTEM

#include "stdtypes.h"

typedef struct SoundSource SoundSource;

typedef enum {
	SND_LOD_LOW,
	SND_LOD_MEDIUM,
	SND_LOD_HIGH,
	SND_LOD_UNKNOWN,
} SndLODState;

typedef struct SndLOD {
	SndLODState state; // current state
	SndLODState lastState; // current state

	F32 threshold; // memory threshold (0 to 1)
	
	F32 waitToRaiseDurationInSecs; // how much time must pass below the threshold before the system attempts to raise the LOD

	F32 lastStateDurationInSecs;

	unsigned long lastStateChangeTicks; // how long since the state changed
	unsigned long belowThresholdTicks; // how long have been under the threshold
	unsigned long aboveThresholdTicks; // above the threshold
	S64 timeLastMemCheck;

	F32 aboveThresholdDuration; // dur in secs above threshold
	F32 belowThresholdDuration; // dur in secs below threshold

	F32 ambientClipDistance; // distance to clip ambient sources

	SoundSource **stoppedSources;

	U32 aboveThreshold : 1;
	U32 aboveThresholdLast : 1; // last state
	U32 enabled : 1;

} SndLOD;

extern SndLOD gSndLOD;

void sndLODInit(SndLOD *lod);
void sndLODUpdate(SndLOD *lod, F32 elapsed);

bool sndLODIsSourceAudible(SndLOD *lod, const SoundSource *source);

void sndLODSetIsEnabled(SndLOD *lod, bool enabled);
bool sndLODIsEnabled(SndLOD *lod);

const char *sndLODStateAsString(SndLODState state);

void sndLODSetThreshold(SndLOD *lod, F32 threshold);
F32 sndLODThreshold(SndLOD *lod);

SndLODState sndLODState(SndLOD *lod);
F32 sndLODDurationAtCurrentState(SndLOD *lod);

SndLODState sndLODLastState(SndLOD *lod);
F32 sndLODDurationAtLastState(SndLOD *lod);

void sndLODSetWaitDuration(SndLOD *lod, F32 wait);
F32 sndLODWaitDuration(SndLOD *lod);

F32 sndLODDurationAboveThreshold(SndLOD *lod);
F32 sndLODDurationBelowThreshold(SndLOD *lod);

void sndLODGetStoppedSources(SndLOD *lod, SoundSource ***stoppedSources);

#endif

