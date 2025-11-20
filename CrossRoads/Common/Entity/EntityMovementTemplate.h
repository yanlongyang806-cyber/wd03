#pragma once
GCC_SYSTEM

/***************************************************************************
*     Copyright (c) 2005-Present, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#if !GAMESERVER && !GAMECLIENT
	#error No Movement code allowed here.
#endif

// NOTE: Copy this file and do the following:
//       1. Find-and-Replace the word "Template" (case sensitive) with your requester name.
//       2. Change the #if 0 to #if 1.

#if 0


typedef struct MovementRequesterMsg MovementRequesterMsg;

void mrTemplateMsgHandler(const MovementRequesterMsg* msg);

#endif