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

#include "stdtypes.h"

typedef struct MovementRequesterMsg	MovementRequesterMsg;
typedef struct MovementRequester	MovementRequester;
typedef struct MovementManager		MovementManager;

void beaconMovementMsgHandler(const MovementRequesterMsg* msg);

void beaconMovementCreate(MovementRequester** mrOut, MovementManager* mm);
void beaconMovementDestroy(MovementRequester** mrInOut);

void beaconMovementSetTarget(MovementRequester* mr, const Vec3 target);
U32 beaconMovementReachedTarget(MovementRequester* mr);
U32 beaconMovementFailedOptionalTest(MovementRequester *mr);
U32 beaconMovementFinished(MovementRequester* mr);
void beaconMovementSetCount(MovementRequester* mr, int count);