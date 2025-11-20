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

#include "referencesystem.h"

typedef struct MovementRequesterMsg MovementRequesterMsg;
typedef struct MovementRequester MovementRequester;

typedef void (*DoorCompleteCallback)(void *userdata);

void	mrDoorMsgHandler(const MovementRequesterMsg* msg);

void	mrDoorStart(MovementRequester* mr,  
					DoorCompleteCallback cb, 
					void *data,
					U32 foreground);

void	mrDoorStartWithDelay(MovementRequester *mr,
							 DoorCompleteCallback cb,
							 void *data,
							 U32 foreground,
							 U32 delay);

void	mrDoorStartWithTime(MovementRequester* mr,  
							DoorCompleteCallback cb, 
							void *data,
							U32 foreground,
							F32 seconds);

void	mrDoorStartWithPosition(MovementRequester* mr,  
								DoorCompleteCallback cb, 
								void *data,
								Vec3 position,
								U32 foreground);

void	mrDoorStartWithPositionAndTime(	MovementRequester* mr,  
										DoorCompleteCallback cb, 
										void *data,
										Vec3 position,
										U32 foreground,
										F32 seconds);

void	mrDoorStartWithFacingDirection(	MovementRequester* mr, 
										DoorCompleteCallback cb, 
										void *data,
										const Vec3 vDir,
										F32 seconds,
										bool roll);
										
void	mrDoorGeoMsgHandler(const MovementRequesterMsg* msg);

void	mrDoorGeoSetTarget(	MovementRequester* mr,
							const Vec3 pos,
							const Quat rot,
							F32 timeTotal);
