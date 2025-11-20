/***************************************************************************



***************************************************************************/
// Author - Greg Thompson


#pragma once

#ifndef _SNDMIXER_H
#define _SNDMIXER_H
GCC_SYSTEM

#include "stdtypes.h"
#include "ReferenceSystem.h"

/*
	All world sounds (i.e., non-UI) belong to a SoundSpace.  A SoundSpace is essentially a room.  
	If a sound exists outside of a room, it belongs to the Nullspace which is itself a SoundSpace.

	All SoundSpaces (with the exception of the Nullspace) are bounded by a box.  Sound may travel
	from one SoundSpace to another by means of a SoundSpaceConnector.  SoundSpaces may be nested within
	one another.

	The SoundMixer handles mixing the various SoundSpaces.  The SoundMixer manages a collection of 
	SoundMixerChannel objects.  
	
	A SoundMixerChannel encapsulates all of the actual audio signal processing & sub-mixing.  
	Internally, it manages a FMOD ChannelGroup (which is a collection FMOD Channels).
	
	A SoundMixerChannel is created and added to the mixer when a SoundSpace is created. 
	The SoundMixerChannel exists throughout the life of the SoundSpace, however, the SoundMixer 
	decides whether to process the channel based on the listener's position in the SoundSpace graph.

	An optional DSP may be added to the SoundMixerChannel.
*/

// forward declarations
typedef struct SoundSpace SoundSpace;
typedef struct SoundSource SoundSource;
typedef struct SoundDSPEffect SoundDSPEffect;

//typedef struct SoundSpaceConnectorGraphEdge
//{
//	SoundSpaceConnectorGraphNode *node1;
//	SoundSpaceConnectorGraphNode *node2;
//	F32 cost;
//};
//
//typedef struct SoundSpaceConnectorGraphNode
//{
//	SoundSpaceConnector *soundSpaceConnector;
//	SoundSpaceConnector **edges;
//
//	U32 visited : 1;
//};

//typedef struct SoundFmodConnection
//{
//	void *fmodSourceCG; // the source of the sound's channel group
//	void **fmodTargetCG; // a ptr to the target of the sound's channel group
//};

/////////////////////////////////////////////////////////////////////////////// 
// SoundMixerChannel
///////////////////////////////////////////////////////////////////////////////
typedef struct SoundMixer SoundMixer;

typedef struct SoundMixerChannel {
	SoundMixer *soundMixer; // owner mixer
	SoundSpace *soundSpace; // the SoundSpace it is mixing for
	F32 audibility;

	StashTable stSources; // no longer appears to be used

	U32 isMuted : 1; // if muted, all DSP is disabled 
	U32 sendToReverb : 1; // determines whether it sends to reverb
} SoundMixerChannel;

//! create a mixer channel to manage the SoundSpace's audio
SoundMixerChannel *sndMixerChannelCreate(SoundSpace *soundSpace);

//! initialize the properties
void sndMixerChannelInit(SoundMixerChannel *soundMixerChannel, SoundSpace *soundSpace);

//! destroy a mixer channel
void sndMixerChannelDestroy(SoundMixerChannel *soundMixerChannel);

//! set the mute
void sndMixerChannelSetMute(SoundMixerChannel *soundMixerChannel, bool mute);

//! is it muted?
bool sndMixerChannelIsMuted(SoundMixerChannel *soundMixerChannel);

//! add a SoundSource to be processed on this channel
void sndMixerChannelAddSource(SoundMixerChannel *soundMixerChannel, SoundSource *source);

//! connect the source's fmod event to the graph
void sndMixerChannelConnectSource(SoundMixerChannel *soundMixerChannel, SoundSource *source);

//! remove a SoundSource
void sndMixerChannelRemoveSource(SoundMixerChannel *soundMixerChannel, SoundSource *source);

//! get the SoundSpace
SoundSpace* sndMixerChannelSoundSpace(SoundMixerChannel *soundMixerChannel);




///////////////////////////////////////////////////////////////////////////////
// SoundMixer
///////////////////////////////////////////////////////////////////////////////
typedef struct FmodEventTracker FmodEventTracker; 

typedef struct SoundMixer {

	SoundMixerChannel **channels; // the collection of channels

	SoundSpace **spaces; // maintain a list of known spaces (note: does not own them)

	SoundSpace *currentSpace; // the current space the listener is within
	SoundDSPEffect *spaceEffect;

	void *fmodWetCG; // the reverb channel group
	void *fmodWetDSP; // the reverb unit

	void *fmodDryCG;

	SoundDSPEffect** effects;  

	int maxPlaybacks;
	int numIgnoredEvents;
	int numSkippedDueToMemory;

	FmodEventTracker **eaPlayingEvents;

	F32 fxDuckRate; // measured in percent per sec
	int fxDuckNumEventThreshold; // how many events must be playing before ducking begins
	F32 fxDuckScaleTarget; // the target scale factor - (i.e., minimum scale factor) 0.5 = 50% of original volume
	
	U64 lastUpdatedTime;

	U32 bFxDuckingEnabled : 1;
} SoundMixer;

//! create a SoundMixer
SoundMixer *sndMixerCreate();

//! init the mixer
void sndMixerInit(SoundMixer *soundMixer);

//! clear out all sources (on all channels)
void sndMixerRemoveAllSources(SoundMixer *soundMixer);

//! returns an opaque pointer to the fmod Reverb DSP
void* sndMixerReverbDSP(SoundMixer *soundMixer);

//! return current return level of reverb
F32 sndMixerReverbReturnLevel(SoundMixer *soundMixer);

//! return current return level of reverb
void sndMixerSetReverbReturnLevel(SoundMixer *soundMixer, F32 level);

//! add an existing SoundMixerChannel to be managed by the mixer
void sndMixerAddChannel(SoundMixer *soundMixer, SoundMixerChannel *soundMixerChannel);

//! remove the SoundMixerChannel
void sndMixerRemoveChannel(SoundMixer *soundMixer, SoundMixerChannel *soundMixerChannel);

//! set the maximum number of playbacks
void sndMixerSetMaxPlaybacks(SoundMixer *soundMixer, int maxPlaybacks);

//! get the maximum playbacks
int sndMixerMaxPlaybacks(SoundMixer *soundMixer);

//! register an event to be watched so that it may be stolen if we run out of voices
/* the default mode is that only one-shot sounds will be stolen */
void sndMixerTrackEvent(SoundMixer *soundMixer, void *fmodEvent);

//! the event has stopped playing, so do not track it any longer
void sndMixerStopTrackingEvent(SoundMixer *soundMixer, void *fmodEvent);

//! stop tracking all events (ex: used for reloadAll)
void sndMixerStopTrackingAllEvents(SoundMixer *soundMixer);

//! play an event (steal if necessary)
/* if nothing to steal, it will fail to play */
U32 sndMixerPlayEvent(SoundMixer *soundMixer, SoundSource* source);

//! get the number of events currently playing (i.e., being tracked)
int sndMixerNumEventsPlaying(SoundMixer *soundMixer);

//! how many events have been ignored due to the max playback limitation
// if this number is high, consider raising the maxplaybacks
int sndMixerNumIgnoredEvents(SoundMixer *soundMixer);

void sndMixerIncEventsSkippedDueToMemory(SoundMixer *soundMixer);
int sndMixerEventsSkippedDueToMemory(SoundMixer *soundMixer);

//! Called once per frame to update the state of the mixer
void sndMixerOncePerFrame(SoundMixer *soundMixer, F32 elapsed);

// need to update the distance cache
void sndMixerSetDistanceCacheNeedsUpdate(SoundMixer *soundMixer, bool val);

// Ducking ---
void sndMixerSetFxDuckingEnabled(SoundMixer *soundMixer, bool enabled);
bool sndMixerIsFxDuckingEnabled();

void sndMixerSetFxDuckRate(SoundMixer *soundMixer, F32 rate);
F32 sndMixerFxDuckRate(SoundMixer *soundMixer);

void sndMixerSetDuckNumEventThreshold(SoundMixer *soundMixer, int numEventThreshold);
int sndMixerFxDuckNumEventThreshold(SoundMixer *soundMixer);

void sndMixerSetDuckScaleTarget(SoundMixer *soundMixer, F32 target);
F32 sndMixerFxDuckScaleTarget(SoundMixer *soundMixer);

#endif

