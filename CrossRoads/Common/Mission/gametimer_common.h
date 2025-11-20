#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

// Timer that displays in the UI.  Usually controlled by an FSM.
AUTO_STRUCT;
typedef struct GameTimer
{
	char* name;
	U32 timer;
	F32 durationSeconds;
	bool visible;

} GameTimer;

// May return less than 0
F32 gametimer_GetRemainingSeconds(GameTimer *timer);

void gametimer_AddTime(GameTimer *timer, F32 seconds);

// May return NULL if too many gametimers have been allocated
GameTimer *gametimer_Create(const char *name, F32 durationSeconds);

void gametimer_Destroy(GameTimer *timer);

