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

// DeadMovement.

void mrDeadSetEnabled(	MovementRequester* mr,
						S32 enabled);

void mrDeadAddStanceNamesIfDead(MovementRequester* mr,
								const char*const* stanceNames);

void mrDeadSetDirection(MovementRequester *mr,
						const char *pcDirection);

void mrDeadSetFromNearDeath(MovementRequester *mr,
							S32 fromNearDeath);

void mrDeadSetFromKnockback(MovementRequester *mr,
							S32 fromKnockback);

U32 mrDeadWasFromNearDeath(MovementRequester *mr);