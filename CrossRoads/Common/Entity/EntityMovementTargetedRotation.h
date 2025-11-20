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
typedef U32							EntityRef;

S32		mmTargetedRotationMovementSetEnabled(	MovementRequester* mr,
												S32 enabled,
												EntityRef erTarget);
