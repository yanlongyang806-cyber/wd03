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

typedef struct MovementRequester	MovementRequester;

// TestMovement.

void mmTestSetDoTest(	MovementRequester* mr,
						U32 startTime);

