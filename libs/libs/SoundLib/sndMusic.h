#pragma once
GCC_SYSTEM
/***************************************************************************



***************************************************************************/

/* This file contains the public interface to the sound library */

#ifndef _SNDMUSIC_H_
#define _SNDMUSIC_H_

typedef struct SoundFadeManager SoundFadeManager;

typedef struct MusicFrame
{
	SoundSource **active;
} MusicFrame;

typedef struct MusicState
{
	char group_name[50];

	SoundSource **playing;
	MusicFrame **active;
	MusicFrame **on_hold;

	SoundFadeManager *fadeManager;
} MusicState;

extern MusicState music_state;



// Music functions used by clientcmds
MusicFrame* sndMusicCreateFrameForSource(SoundSource *source);

void sndMusicEnd(bool immediate);
void sndMusicReplace(char *eventname, char *filename, U32 entRef);
void sndMusicClear(bool immediate);
// Other music functions
void sndMusicCleanup(SoundSource *source);

U32 sndMusicIsPlaying(const char* event_name, SoundSource **sourceOut);
U32 sndMusicIsCurrent(SoundSource *s);


#endif

