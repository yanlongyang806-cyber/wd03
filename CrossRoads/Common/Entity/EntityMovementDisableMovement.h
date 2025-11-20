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

typedef struct MovementManager		MovementManager;
typedef struct MovementRequester	MovementRequester;

S32 mrDisableCreate(MovementManager* mm,
					MovementRequester** mrOut);

// This flag will cause the movement requester to destroy itself.  NULL any pointers to it
// after calling this function.
S32 mrDisableSetDestroySelfFlag(MovementRequester** mrInOut);

