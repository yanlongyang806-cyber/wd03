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
typedef U32							EntityRef;

AUTO_STRUCT;
typedef struct MRGrabActorFlags {
	U32					stopMoving	: 1;
	U32					moveToPos	: 1;
} MRGrabActorFlags;

AUTO_STRUCT;
typedef struct MRGrabActor {
	EntityRef			er;
	U32					mrHandle;
	Vec3				posTarget;
	U32*				animBitHandles;
	MRGrabActorFlags	flags;
} MRGrabActor;

AUTO_STRUCT;
typedef struct MRGrabConfigFlags {
	U32					isTarget : 1;
} MRGrabConfigFlags;

AUTO_STRUCT;
typedef struct MRGrabConfig {
	MRGrabActor			actorSource;
	MRGrabActor			actorTarget;
	F32					maxSecondsToReachTarget;
	F32					secondsToHold;
	F32					distanceToStartHold;
	F32					distanceToHold;
	MRGrabConfigFlags	flags;
} MRGrabConfig;

AUTO_ENUM;
typedef enum MRGrabStatus {
	MR_GRAB_STATUS_INVALID,
	MR_GRAB_STATUS_CHASE,
	MR_GRAB_STATUS_HOLDING,
	MR_GRAB_STATUS_DONE,
} MRGrabStatus;

S32 mrGrabCreate(	MovementManager* mm,
					MovementRequester** mrOut);

S32 mrGrabSetConfig(MovementRequester* mr,
					const MRGrabConfig* config);

S32 mrGrabGetStatus(MovementRequester* mr,
					MRGrabStatus* statusOut);
