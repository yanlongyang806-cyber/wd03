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
typedef struct MovementRequesterMsg MovementRequesterMsg;

void	mmPushMsgHandler(const MovementRequesterMsg* msg);

void	mmPushStartWithVelocity(	MovementRequester* mr,
										const Vec3 vel,
										U32 startProcessCount);