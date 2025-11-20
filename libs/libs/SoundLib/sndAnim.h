/***************************************************************************



***************************************************************************/
// Author - Greg Thompson

#pragma once

#ifndef __SNDANIM_H
#define __SNDANIM_H
GCC_SYSTEM

#include "stdtypes.h"

//
// SoundAnim provides a system of driving animation data from sound sources
//

typedef struct SoundSource SoundSource;

typedef struct SoundAnimSource
{
	SoundSource *pSoundSource;
	U32 uiLastTime;
	U8 bTalkBit : 1;

} SoundAnimSource;

typedef struct SoundAnim
{
	SoundAnimSource **ppSources; // holds a list of sources that are played from an entity
} SoundAnim;

extern SoundAnim* g_SoundAnim;

SoundAnim* sndAnimCreate();

void sndAnimAddSource(SoundAnim *pSoundAnim, SoundSource *pSoundSource);
void sndAnimRemoveSource(SoundAnim *pSoundAnim, SoundSource *pSoundSource);

void sndAnimAddContactDialogSource(SoundSource *pSoundSource);
void sndAnimRemoveContactDialogSource(SoundSource *pSoundSource);
bool sndAnimIsContactDialogSourceAudible(void);

void sndAnimTick(SoundAnim *pSoundAnim);

#endif
