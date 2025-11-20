#pragma once
GCC_SYSTEM

typedef struct WorldCollGridCell	WorldCollGridCell;
typedef struct PSDKActor			PSDKActor;

typedef enum
{
	STUCK_SLIDE = 1,
	STUCK_COMPLETELY,
} StuckType;

typedef S32 (*MotionStateActorIgnoredCB)(void* userPointer, const PSDKActor* psdkActor);

typedef struct MotionState
{
	WorldCollGridCell*			wcCell;
	void*						userPointer;
	MotionStateActorIgnoredCB	actorIgnoredCB;
	U32							filterBits;
	StuckType					stuck_head;
	Vec3						vel;
	Vec3						pos;
	Vec3						last_pos;
	Vec3						ground_normal;
	Vec3						surface_normal;

	// if step_height is 0 when given to the worldMoveMe function,
	// it will set the step height to a default depending if the is_player flag is set or not.
	F32							step_height;

	U32							use_sticky_ground : 1;
	U32							is_player : 1;
	U32							hit_ground : 1;
	U32							hit_surface : 1;
} MotionState;

void worldMoveMe(MotionState *motion);

