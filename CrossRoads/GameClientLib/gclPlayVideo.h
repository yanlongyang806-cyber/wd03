#pragma once
GCC_SYSTEM

#ifndef GCLPLAYVIDEO_H_
#define GCLPLAYVIDEO_H_

#include "DoorTransitionCommon.h"

void gclPlayVideo_Enter(void);
void gclPlayVideo_BeginFrame(void);
void gclPlayVideo_EndFrame(void);
void gclPlayVideo_Leave(void);
void gclPlayVideo_Start(const char* pchVideoPath, DoorTransitionType eTransitionType, bool bFullscreen);

#endif