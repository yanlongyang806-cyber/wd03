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

extern ParseTable parse_MRInteractionWaypoint[];
#define TYPE_parse_MRInteractionWaypoint MRInteractionWaypoint
extern ParseTable parse_MRInteractionPath[];
#define TYPE_parse_MRInteractionPath MRInteractionPath

AUTO_STRUCT;
typedef struct MRInteractionWaypointFlags {
	U32								snapToWaypoint			: 1;
	U32								releaseOnInput			: 1;
	U32								moveToOnRail			: 1;
	U32								playAnimDuringMove		: 1;
	U32								forceAnimDuringMove		: 1;
	U32								jumpHereOnRailFail		: 1;
	U32								notifyWhenReached		: 1;
	U32								waitHereUntilTriggered	: 1;
} MRInteractionWaypointFlags;

AUTO_STRUCT;
typedef struct MRInteractionWaypoint {
	Vec3							pos;
	Quat							rot;
	F32								seconds;
	U32*							animBitHandles;
	U32								animToStart;
	U32*							stances;			AST(NAME("Stances"))
	MRInteractionWaypointFlags		flags;
} MRInteractionWaypoint;

AUTO_STRUCT;
typedef struct MRInteractionPath {
	MRInteractionWaypoint**			wps;
} MRInteractionPath;

typedef enum MRInteractionOwnerMsgType {
	MR_INTERACTION_OWNER_MSG_FINISHED,
	MR_INTERACTION_OWNER_MSG_FAILED,
	MR_INTERACTION_OWNER_MSG_DESTROYED,
	MR_INTERACTION_OWNER_MSG_REACHED_WAYPOINT,
	MR_INTERACTION_OWNER_MSG_WAITING_FOR_TRIGGER,
} MRInteractionOwnerMsgType;

typedef struct MRInteractionOwnerMsg {
	MRInteractionOwnerMsgType		msgType;
	void*							userPointer;
	
	union {
		struct {
			U32						index;
		} reachedWaypoint;
		
		struct {
			U32						waypointIndex;
		} waitingForTrigger;
	};
} MRInteractionOwnerMsg;

typedef void (*MRInteractionOwnerMsgHandler)(const MRInteractionOwnerMsg* msg);

S32 mrInteractionCreate(MovementManager* mm,
						MovementRequester** mrOut);

S32 mrInteractionSetOwner(	MovementRequester* mr,
							MRInteractionOwnerMsgHandler msgHandler,
							void* userPointer);

S32 mrInteractionGetOwner(	MovementRequester* mr,
							MRInteractionOwnerMsgHandler msgHandler,
							void** userPointerOut);

S32 mrInteractionSetPath(	MovementRequester* mr,
							MRInteractionPath** pInOut);

S32 mrInteractionDestroy(MovementRequester** mrInOut);

S32 mrInteractionTriggerWaypoint(	MovementRequester* mr,
									U32 waypointIndex);

S32 mrInteractionCreatePathForSit(	MRInteractionPath** pOut,
									const Vec3 posFeet,
									const Vec3 posKnees,
									const Quat rot,
									const U32* bitHandlesPre,
									const U32* bitHandlesHold,
									F32 secondsToMove,
									F32 secondsPostHold);

