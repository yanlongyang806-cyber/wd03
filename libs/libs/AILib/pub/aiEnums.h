#ifndef AIENUMS_H
#define AIENUMS_H

extern StaticDefineInt AILeashTypeEnum[];

AUTO_ENUM;
typedef enum AILeashType
{
	AI_LEASH_TYPE_DEFAULT,
	AI_LEASH_TYPE_RALLY_POSITION,
	AI_LEASH_TYPE_OWNER,
	AI_LEASH_TYPE_ENTITY,	
} AILeashType;


AUTO_ENUM;
typedef enum AINotifyType
{
	AI_NOTIFY_TYPE_DAMAGE,											ENAMES(damage)
	AI_NOTIFY_TYPE_STATUS,											ENAMES(status)
	AI_NOTIFY_TYPE_THREAT,											ENAMES(threat)
	AI_NOTIFY_TYPE_HEALING,											ENAMES(healing)
	AI_NOTIFY_TYPE_BASIC_TRACKED_COUNT,								EIGNORE

	AI_NOTIFY_TYPE_SCRATCH1 = AI_NOTIFY_TYPE_BASIC_TRACKED_COUNT,	ENAMES(scratch1)
	AI_NOTIFY_TYPE_SCRATCH2,										ENAMES(scratch2)
	AI_NOTIFY_TYPE_TRACKED_COUNT,									EIGNORE


	AI_NOTIFY_TYPE_AVOID,											ENAMES(avoid)
	AI_NOTIFY_TYPE_SOFT_AVOID = AI_NOTIFY_TYPE_TRACKED_COUNT,		ENAMES(softavoid)
	AI_NOTIFY_TYPE_BURST,											ENAMES(burst)
	AI_NOTIFY_TYPE_COUNT,											EIGNORE
}AINotifyType;

extern StaticDefineInt AINotifyTargetTypeEnum[];

AUTO_ENUM;
typedef enum AINotifyTargetType
{
	AI_NOTIFY_TARGET_SELF,									ENAMES(self)
	AI_NOTIFY_TARGET_FRIENDS,								ENAMES(friends)
	AI_NOTIFY_TARGET_COUNT
}AINotifyTargetType;


AUTO_ENUM; 
typedef enum AIMovementModeType
{
	AIMovementModeType_NONE = -1,
	AIMovementModeType_SPRINT,
	AIMovementModeType_AIM,
	AIMovementModeType_ROLL,
	AIMovementModeType_SAFETY_TELEPORT,
	AIMovementModeType_COUNT
} AIMovementModeType;

extern StaticDefineInt AIMovementModeTypeEnum[];


#define AI_DEFAULT_STEP_LENGTH 2

typedef enum AICollideRayFlag
{
	AICollideRayFlag_NONE = 0,
	AICollideRayFlag_DOWALKCHECK	= (1<<0),
	AICollideRayFlag_DOAVOIDCHECK	= (1<<1), 
	AICollideRayFlag_DOCAPSULECHECK	= (1<<2),
	AICollideRayFlag_SKIPRAYCAST	= (1<<3)
} AICollideRayFlag;

typedef enum AICollideRayResult
{
	AICollideRayResult_NONE,
	AICollideRayResult_START,
	AICollideRayResult_END,
	AICollideRayResult_WALK,
	AICollideRayResult_AVOID,
	AICollideRayResult_CAPSULE,
	AICollideRayResult_RAY,
} AICollideRayResult;

AUTO_STRUCT;
typedef struct AIConfigMod
{
	// the aiconfig setting this is modifying
	const char* setting; AST(REQUIRED POOL_STRING STRUCTPARAM)

	// the value of the aiconfig mod
	const char* value; AST(REQUIRED STRUCTPARAM)
}AIConfigMod;


#endif 