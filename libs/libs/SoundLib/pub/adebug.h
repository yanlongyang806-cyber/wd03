/***************************************************************************



***************************************************************************/
// Author - Greg Thompson

#pragma once

#ifndef _ADEBUG_H
#define _ADEBUG_H
GCC_SYSTEM

#include "stdtypes.h"
#include "ReferenceSystem.h"

typedef struct VoiceUser VoiceUser;
typedef struct VoiceChannel VoiceChannel;

// called from gclBaseStates.c
void aDebugOncePerFrame(F32 elapsed);
void aDebugDraw(void);

//
void aDebugUIToggle(void);

void svDebugUIUserLost(VoiceUser *user);
void svDebugUIChanLost(VoiceChannel *chan);

#endif