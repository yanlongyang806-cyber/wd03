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

S32 mrSwingSetMaxSpeed(	MovementRequester* mr,
						F32 maxSpeed);

void mrSwingSetFx(MovementRequester* mr, const char * pcSwingingFx);