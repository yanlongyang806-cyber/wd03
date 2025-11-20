
#pragma once

#include "stdtypes.h"
#include "ExpressionMinimal.h"

typedef struct AIConfig				AIConfig;
typedef struct AIVarsBase			AIVarsBase;
typedef struct AIVolumeEntry		AIVolumeEntry;
typedef struct Beacon				Beacon;
typedef struct CommandQueue			CommandQueue;
typedef struct Entity				Entity;
typedef struct ExprContext			ExprContext;
typedef struct FSMContext			FSMContext;
typedef struct MovementRequesterMsg	MovementRequesterMsg;
typedef struct MovementRequester	MovementRequester;
typedef struct MovementManager		MovementManager;
typedef struct NavPathWaypoint		NavPathWaypoint;
typedef struct GameInteractable		GameInteractable;
typedef struct GameInteractLocation GameInteractLocation;
typedef U32 EntityRef;

void aiMovementMsgHandler(SA_PARAM_NN_VALID const MovementRequesterMsg* msg);

F32 aiMovementGetPathEntHeight(SA_PARAM_NN_VALID Entity *e);

void aiMovementCreate(SA_PRE_NN_VALID SA_POST_NN_RELEMS(1) MovementRequester** mrOut, SA_PARAM_NN_VALID MovementManager* mm);

void aiMovementDestroy(SA_PRE_NN_NN_VALID SA_POST_NN_NULL MovementRequester** mrInOut);

typedef enum AIMovementTargetFlags {
	AI_MOVEMENT_TARGET_CRITICAL = 1 << 0,
	AI_MOVEMENT_TARGET_IGNORE_CAPSULE_FOR_DIST = 1 << 1,
	AI_MOVEMENT_TARGET_DONT_SHORTCUT = 1 << 2,
	AI_MOVEMENT_TARGET_ERROR_ON_NO_TARGET_BEACON = 1 << 3,
} AIMovementTargetFlags;

AUTO_ENUM;
typedef enum AIMovementOrderType
{
	AI_MOVEMENT_ORDER_NONE = 0,
	AI_MOVEMENT_ORDER_POS,
	AI_MOVEMENT_ORDER_ENT,
}AIMovementOrderType;

// These are purely descriptive - the actual bits set determine how it moves
//  The basic usage is that you can know what the AI is ultimately trying to do
//  For example, while doing a follow offset, you may sometimes need to just follow
AUTO_ENUM;
typedef enum AIMovementOrderEntDetail
{
	AI_MOVEMENT_ORDER_ENT_UNSPECIFIED,
	AI_MOVEMENT_ORDER_ENT_FOLLOW,
	AI_MOVEMENT_ORDER_ENT_FOLLOW_OFFSET,
	AI_MOVEMENT_ORDER_ENT_COMBAT,
	AI_MOVEMENT_ORDER_ENT_PATROL,
	AI_MOVEMENT_ORDER_ENT_PATROL_OFFSET,
	AI_MOVEMENT_ORDER_ENT_GET_IN_RANGE,
	AI_MOVEMENT_ORDER_ENT_GET_IN_RANGE_DIST,
	AI_MOVEMENT_ORDER_ENT_COMBAT_MOVETO_OFFSET,
} AIMovementOrderEntDetail;

int aiMovementGetTargetPosition(SA_PARAM_NN_VALID Entity *e, SA_PARAM_NN_VALID AIVarsBase *aib, Vec3 targetOut);
#define aiMovementSetTargetPosition(e, aib, target, didPathOut, flags) aiMovementSetTargetPositionEx(e, aib, target, didPathOut, flags, __FUNCTION__, __FILE__, __LINE__)
int aiMovementSetTargetPositionEx(SA_PARAM_NN_VALID Entity* e, SA_PARAM_NN_VALID AIVarsBase* aib, SA_PRE_OP_RELEMS(3) const Vec3 target, SA_PARAM_OP_VALID S32 *didPathfindOut, AIMovementTargetFlags movementTargetFlags, SA_PARAM_NN_VALID const char *func, SA_PARAM_NN_VALID const char *file, int line);
#define aiMovementSetTargetEntity(e, aib, target, offset, offsetRotRelative, detail, flags) aiMovementSetTargetEntityEx(e, aib, target, offset, offsetRotRelative, detail, flags, __FUNCTION__, __FILE__, __LINE__)
int aiMovementSetTargetEntityEx(SA_PARAM_NN_VALID Entity* e, SA_PARAM_NN_VALID AIVarsBase* aib, SA_PARAM_OP_VALID Entity* target, SA_PRE_OP_RELEMS(3) const Vec3 offset, int offsetRotRelative, AIMovementOrderEntDetail detail, AIMovementTargetFlags movementTargetFlags, SA_PARAM_NN_VALID const char *func, SA_PARAM_NN_VALID const char *file, int line);
void aiMovementClearMovementTarget(SA_PARAM_NN_VALID Entity* e, SA_PARAM_NN_VALID AIVarsBase* aib);
// Sets the settled flag for the AI_MOVEMENT_ORDER_ENT order
bool aiMovementSetTargetEntitySettledFlag(SA_PARAM_NN_VALID Entity *e, SA_PARAM_NN_VALID AIVarsBase *aib, EntityRef targetRef, bool bSettled, Vec3 vecSettledPos);

NavPathWaypoint* aiMovementGetTargetWp(Entity *e, AIVarsBase *aib);

#define aiMovementGoToSpawnPos(e, aib, movementTargetFlags) aiMovementGoToSpawnPosEx(e, aib, movementTargetFlags, __FUNCTION__, __FILE__, __LINE__)
void aiMovementGoToSpawnPosEx(SA_PARAM_NN_VALID Entity* e, SA_PARAM_NN_VALID AIVarsBase* aib, AIMovementTargetFlags critical, SA_PARAM_NN_VALID const char *func, SA_PARAM_NN_VALID const char *file, int line);

#define aiMovementResetPath(e, aib) aiMovementResetPathEx(e, aib, __FILE__, __LINE__)
#define QueuedCommand_aiMovementResetPath(queue, e, aib) \
	QueuedCommand_aiMovementResetPathEx(queue, e, aib, __FILE__, __LINE__)

// resets the entity's path and adds a list of waypoints. F32* must be in multiples of 3 or function will error
int aiMovementSetWaypointsExplicit(SA_PARAM_NN_VALID Entity* e, const F32 *eafWaypoints);

void aiMovementResetPathEx(SA_PARAM_NN_VALID Entity* e, SA_PARAM_NN_VALID AIVarsBase* aib, SA_PARAM_NN_STR const char* filename, int linenumber);

void aiMovementSetSleeping(SA_PARAM_NN_VALID Entity* e, SA_PARAM_NN_VALID AIVarsBase* aib, bool sleeping);
void aiMovementSetWalkRunDist(Entity* e, AIVarsBase* aib, F32 distWalk, F32 distRun, U32 cheat);

void aiMovementFly(Entity *e, AIVarsBase *aib, int fly);
void aiMovementSetFlying(SA_PARAM_NN_VALID Entity* e, SA_PARAM_NN_VALID AIVarsBase* aib, bool flying);
void aiMovementSetDebugFlag(Entity *e, bool on);
int aiMovementGetFlying(SA_PARAM_NN_VALID Entity* e, SA_PARAM_NN_VALID AIVarsBase* aib);
int aiMovementGetFlyingEntRef(EntityRef ref);
int aiMovementGetCanFlyEntRef(EntityRef ref);
int aiMovementGetAlwaysFlyEntRef(EntityRef ref);
int aiMovementGetNeverFlyEntRef(EntityRef ref);
F32 aiMovementGetTurnRate(Entity* e);
F32 aiMovementGetTurnRateEntRef(EntityRef ref);
F32 aiMovementGetJumpHeightEntRef(EntityRef ref);
F32 aiMovementGetJumpCostEntRef(EntityRef ref);
F32 aiMovementGetJumpHeightMultEntRef(EntityRef ref);
F32 aiMovementGetJumpDistMultEntRef(EntityRef ref);
void aiMovementUpdateConfigSettings(SA_PARAM_NN_VALID Entity* e, SA_PARAM_NN_VALID AIVarsBase* aib, SA_PARAM_NN_VALID AIConfig* config);
void aiMovementUpdateSpawnOffset(SA_PARAM_NN_VALID Entity* e, SA_PARAM_NN_VALID AIVarsBase* aib);
void aiMovementHandleRegionChange(SA_PARAM_NN_VALID Entity* e, S32 prevRegion, S32 curRegion);

void aiMovementGetSplineTarget(SA_PARAM_NN_VALID Entity *e, SA_PARAM_NN_VALID AIVarsBase *aib, SA_PRE_NN_ELEMS(3) SA_POST_OP_VALID Vec3 target);

AIMovementOrderType aiMovementGetMovementOrderType(SA_PARAM_NN_VALID Entity* e, SA_PARAM_NN_VALID AIVarsBase* aib);
AIMovementOrderEntDetail aiMovementGetMovementOrderEntDetail(SA_PARAM_NN_VALID Entity* e, SA_PARAM_NN_VALID AIVarsBase* aib);
int aiMovementGetOrderUseOffset(SA_PARAM_NN_VALID Entity* e, SA_PARAM_NN_VALID AIVarsBase* aib);
EntityRef aiMovementGetMovementTargetEnt(Entity* e, AIVarsBase *aib);
EntityRef aiMovementGetRotationTargetEnt(Entity* e, AIVarsBase *aib);

void aiMovementSetFinalFacePos(SA_PARAM_NN_VALID Entity* e, SA_PARAM_NN_VALID AIVarsBase* aib, SA_PRE_NN_RELEMS(3) const Vec3 finalFacePos);
void aiMovementSetFinalFaceRot(SA_PARAM_NN_VALID Entity* e, SA_PARAM_NN_VALID AIVarsBase* aib, SA_PRE_NN_RELEMS(4) const Quat finalFaceRot);
void aiMovementSetFinalFaceEntity(SA_PARAM_NN_VALID Entity* e, SA_PARAM_NN_VALID AIVarsBase* aib, SA_PARAM_NN_VALID Entity* target);
void aiMovementClearRotationTarget(SA_PARAM_NN_VALID Entity* e, SA_PARAM_NN_VALID AIVarsBase* aib);

void aiMovementAddHoldBit(Entity *e, U32 bitHandle);
void aiMovementClearAnimHold(Entity *e);
void aiMovementAddAnimBitHandle(SA_PARAM_NN_VALID Entity* e, U32 bitHandle, SA_PRE_NN_FREE SA_POST_NN_VALID int* handleOut);
void aiMovementRemoveAnimBitHandle(SA_PARAM_NN_VALID Entity* e, int aiMovementHandle);

void aiMovementAddFX(SA_PARAM_NN_VALID Entity* e, SA_PARAM_NN_STR const char* name, SA_PRE_NN_FREE SA_POST_NN_VALID int* handleOut);
void aiMovementFlashFX(SA_PARAM_NN_VALID Entity* e, SA_PARAM_NN_STR const char* name);
void aiMovementRemoveFX(SA_PARAM_NN_VALID Entity* e, SA_PARAM_NN_STR const char* name, int handle);

void aiMovementAvoidEntryAdd(SA_PARAM_NN_VALID Entity* e, SA_PARAM_NN_VALID AIVarsBase* aib, SA_PARAM_NN_VALID AIVolumeEntry* entry);
void aiMovementAvoidEntryRemove(SA_PARAM_NN_VALID Entity* e, SA_PARAM_NN_VALID AIVarsBase* aib, SA_PARAM_NN_VALID AIVolumeEntry* entry);

// Sets the override speed for the AI by adding a config mod.
// note that the config mod adding is order dependant, so if another source overrides the "overrideMovementSpeed"
// field of the aiconfig, this may get stomped, or stomp a previous "overrideMovementSpeed" override
void aiMovementSetOverrideSpeed(SA_PARAM_NN_VALID Entity *e, SA_PARAM_NN_VALID AIVarsBase* aib, F32 fSpeedOverride);
// Clears the "overrideMovementSpeed" config mod that the aiMovementSetOverrideSpeed() sets.
void aiMovementClearOverrideSpeed(SA_PARAM_NN_VALID Entity *e, SA_PARAM_NN_VALID AIVarsBase* aib);


// all of this should be somewhere nicer, but no time
//AUTO_ENUM AEN_APPEND_TO(fsmLocalDataType);
AUTO_ENUM;
typedef enum fsmLocalDataType
{
	FSM_LDT_ADDCONFIGMOD,
	FSM_LDT_ADDCONFIGMODSET,
	FSM_LDT_ANIMLIST,
	FSM_LDT_FOLLOW,
	FSM_LDT_COMBAT,
	FSM_LDT_AMBIENT,
	FSM_LDT_COMBATJOB,
	FSM_LDT_GENERICSETDATA,
	FSM_LDT_GENERICU64,
	FSM_LDT_GENERICU64EH,
	FSM_LDT_GENERICEARRAY,
	FSM_LDT_GOTOSPAWNPOS,
	FSM_LDT_PATROL,
	FSM_LDT_RETREAT,
	FSM_LDT_RUNINTODOOR,
	FSM_LDT_RUNOUTOFDOOR,
	FSM_LDT_RUNTOPOINT,
	FSM_LDT_SENDMESSAGE,
	FSM_LDT_WANDER,
	FSM_LDT_GRIEVEDHEAL,
	FSM_LDT_MMIND_PERSUE,
	FSM_LDT_DISORIENTED,
}fsmLocalDataType;

AUTO_STRUCT;
typedef struct ExprLocalData
{
	fsmLocalDataType type; AST(POLYPARENTTYPE)
	U64 key;
}ExprLocalData;

// Can't move this from here because latebind poly type is bad
AUTO_STRUCT;
typedef struct DestroyMyLocalData
{
	ExprLocalData* localdata;
}DestroyMyLocalData;

AUTO_STRUCT;
typedef struct FSMLDAddStructMod
{
	ExprLocalData baseData; AST(POLYCHILDTYPE(FSM_LDT_ADDCONFIGMOD))

	U32 id;

	U32 dataIsSet : 1;
}FSMLDAddStructMod;

AUTO_STRUCT;
typedef struct FSMLDAddStructModTagSet
{
	ExprLocalData baseData; AST(POLYCHILDTYPE(FSM_LDT_ADDCONFIGMODSET))

	U32 *ids;
	U32 tagBit;

	U32 dataIsSet : 1;
} FSMLDAddStructModTagSet;

AUTO_STRUCT;
typedef struct FSMLDAnimList
{
	ExprLocalData baseData;					AST(POLYCHILDTYPE(FSM_LDT_ANIMLIST))
	const char *pchCurrentAnimList;			AST(POOL_STRING)
	CommandQueue* animListCommandQueue;		NO_AST 
	U32 addedAnimList : 1;
}FSMLDAnimList;

AUTO_STRUCT;
typedef struct FSMLDCombat
{
	ExprLocalData baseData; AST(POLYCHILDTYPE(FSM_LDT_COMBAT))
	F32 combatMinimumThrottlePercentage;
	U32 combatMinThrottleConfigModHandle;
	U32 continuousCombatMovementOffHandle;
	U32 switchModsForLeashing : 1;
	U32 leashModeOn : 1;
	U32 setData : 1;
	U32 setCombatFSMData : 1;
}FSMLDCombat;

AUTO_STRUCT;
typedef struct FSMLDCombatJob
{
	ExprLocalData baseData; AST(POLYCHILDTYPE(FSM_LDT_COMBATJOB))

	FSMContext* jobFsmContext;										NO_AST
	ExprContext* jobExprContext;									NO_AST
	U32 setCombatJobFSMData : 1;
} FSMLDCombatJob;

AUTO_ENUM;
typedef enum FSMLDAmbientState
{
	FSM_AMBIENT_IDLE = 1, ENAMES(idle)
	FSM_AMBIENT_CHAT, ENAMES(chat)
	FSM_AMBIENT_WANDER, ENAMES(wander)
	FSM_AMBIENT_JOB, ENAMES(job)
	FSM_AMBIENT_GOTOSPAWN, ENAMES(spawn)
	FSM_AMBIENT_END_OF_LIST // end
} FSMLDAmbientState;

AUTO_ENUM;
typedef enum FSMLDAmbientWanderType
{
	FSM_AMBIENTTYPE_STANDARD = 0,
	FSM_AMBIENTTYPE_SPACE,

	FSM_AMBIENTTYPE_END_OF_LIST // end
} FSMLDAmbientWanderType;

typedef struct GameInteractable GameInteractable;
typedef struct GameInteractLocation GameInteractLocation;

AUTO_STRUCT;
typedef struct FSMLDAmbient
{
	ExprLocalData baseData; AST(POLYCHILDTYPE(FSM_LDT_AMBIENT))

	FSMLDAmbientState currentState; // current ambient state (see enum list)
	
	S64 lastTimeOffset; // last time state changed
	S64 ambientJobCooldown;
	F32 stateDuration;	// how long to remain in current state before changing
	
	F32 wanderSpeed;	// wander speed
	F32 wanderDuration;	// how long to wander (in secs)
	F32 wanderWeight;	// (if isWanderActive) probability weighting
	F32 wanderDistance;	// maximum wander distance
	F32 wanderIdleTime; 
	S32 wanderMaxPath;

	F32 chatDuration;	// how long to chat (in secs)
	F32 chatWeight;		// (if isChatActive) probability weighting
	
	char *chatMessageKey;	// message key for chat text
	char *idleAnimation;	// idle anim list name
	char *chatAnimation;	// chat anim list name

	FSMContext* jobFsmContext;		
	ExprContext* jobExprContext;	NO_AST
	F32 jobDuration;  // how long to perform an ambient job
	F32 jobWeight;    // (if isJobActive) probability weighting
	Vec3 vTargetPos;  // target pos for a job

	F32 idleDuration;	// how long to idle (in secs)
	F32 idleWeight;		// (if isIdleActive) probability weighting

	int speedConfigModHandle; // config mod id for overriding speed
	int waypointSplineConfigModHandle; // config mod id for distBeforeWaypointToSpline 

	int groundRelative;
	F32 airWander;				// +/- distance on Y-axis 
	F32 distBeforeWaypointToSpline; // Determines how far ahead of a shortcut to attempt pseudospline

	F32 fJobAwarenessRadius; // determines how far a critter will look for a job
	S32 failedFindJobCount;
	
	GameInteractable *pTargetJobInteractable;		NO_AST // ambient job on GameInteractable
	GameInteractLocation *pTargetLocation;			NO_AST // ambient job on GameInteractable
	
	CommandQueue* animationCommandQueue;					NO_AST  // animation

	U32 isActive : 1; // are we actively processing the current state
	
	U32 onEntry : 1; // on a per state basis, determine if we are entering the state

	U32 isIdleActive : 1; // is idle a choice
	U32 isChatActive : 1; // is chat a choice
	U32 isWanderActive : 1; // is wander a choice
	U32 isJobActive : 1; // is ambient job a choice

	U32 addedAnimList : 1; // anim list has been added

	U32 setData : 1; // has this structure been initialized

	U32 isFlying : 1; // is flying?

	U32 bIsMovingToJobTarget : 1; // moving to job target?
	U32 bReachedJobTarget : 1; // has reached the job target
	U32 bStateDoesNotCheckDuration : 1;
	U32 bIgnoreFindJob : 1;
}FSMLDAmbient;

AUTO_STRUCT;
typedef struct FSMLDGenericSetData
{
	ExprLocalData baseData; AST(POLYCHILDTYPE(FSM_LDT_GENERICSETDATA))
	U32 setData : 1;
}FSMLDGenericSetData;

AUTO_STRUCT;
typedef struct FSMLDGenericU64
{
	ExprLocalData baseData; AST(POLYCHILDTYPE(FSM_LDT_GENERICU64))
	U64 myU64;
}FSMLDGenericU64;

AUTO_STRUCT;
typedef struct FSMLDGenericEArray
{
	ExprLocalData baseData; AST(POLYCHILDTYPE(FSM_LDT_GENERICEARRAY))

	void **myEArray;		NO_AST
} FSMLDGenericEArray;

AUTO_STRUCT;
typedef struct FSMLDGenericU64ExitHandlers
{
	ExprLocalData baseData; AST(POLYCHILDTYPE(FSM_LDT_GENERICU64EH))

	U64 myU64;

	U32 addedExitHandlers : 1;
} FSMLDGenericU64ExitHandlers;

AUTO_STRUCT;
typedef struct FSMLDGoToSpawnPos
{
	ExprLocalData baseData; AST(POLYCHILDTYPE(FSM_LDT_GOTOSPAWNPOS))
	U32 addedExitHandlers : 1;
}FSMLDGoToSpawnPos;

AUTO_STRUCT;
typedef struct FSMLDPatrol
{
	ExprLocalData baseData; AST(POLYCHILDTYPE(FSM_LDT_PATROL))

	const char* patrolName; NO_AST
	int lastPointPassed;
	U32 totalPoints;
	U32 leaderRef;

	U32 pingpongRev : 1;
	U32 amLeader : 1;
	U32 finishedOneRotation : 1;
	U32 dataSet : 1;
	U32 hadData : 1;
	U32 useOffset : 1;
}FSMLDPatrol;
extern ParseTable parse_FSMLDPatrol[];

AUTO_STRUCT;
typedef struct FSMLDFollow
{
	ExprLocalData baseData; AST(POLYCHILDTYPE(FSM_LDT_FOLLOW))
	U64 addedExitHandlers : 1;
}FSMLDFollow;

AUTO_STRUCT;
typedef struct FSMLDRunIntoDoor
{
	ExprLocalData baseData; AST(POLYCHILDTYPE(FSM_LDT_RUNINTODOOR))
	bool hasDoor; NO_AST
	Vec3 myDoorPos;

	U32 addedExitHandlers : 1;
	U32 finishedRunningIntoDoor : 1;
}FSMLDRunIntoDoor;

AUTO_STRUCT;
typedef struct FSMLDRunOutOfDoor
{
	ExprLocalData baseData; AST(POLYCHILDTYPE(FSM_LDT_RUNOUTOFDOOR))
	bool hasDoor; NO_AST
	Vec3 myDoorPos;
	U32 finishedRunningOutOfDoor : 1;
}FSMLDRunOutOfDoor;

AUTO_STRUCT;
typedef struct FSMLDRunToPoint
{
	ExprLocalData baseData; AST(POLYCHILDTYPE(FSM_LDT_RUNTOPOINT))
	U32 addedExitHandlers : 1;
}FSMLDRunToPoint;

AUTO_STRUCT;
typedef struct FSMLDRetreat
{
	ExprLocalData baseData; AST(POLYCHILDTYPE(FSM_LDT_RETREAT))
	U32 addedExitHandlers : 1;
} FSMLDRetreat;

AUTO_STRUCT;
typedef struct FSMLDSendMessage
{
	ExprLocalData baseData; AST(POLYCHILDTYPE(FSM_LDT_SENDMESSAGE))
	U32 handle;
	U32 animOn : 1;
}FSMLDSendMessage;

typedef enum EWanderState
{
	EWanderState_WANDERING,
	EWanderState_IDLE,
} EWanderState;

AUTO_STRUCT;
typedef struct FSMLDWander
{
	ExprLocalData baseData; AST(POLYCHILDTYPE(FSM_LDT_WANDER))
	Vec3 wanderHomePos;
	F32 wanderDistSQR;
	F32 wanderIdleTimeAvg;
	S64 lastPathFindTime;
	S32 wanderState;
	S32 wanderMaxPath;
	U32 addedExitHandlers	: 1;
	U32 airWander			: 1;
	U32 spawnRelative		: 1;
}FSMLDWander;

AUTO_STRUCT;
typedef struct FSMLDGrievedHeal
{
	ExprLocalData baseData; AST(POLYCHILDTYPE(FSM_LDT_GRIEVEDHEAL))

	U32 addedExitHandlers : 1;
} FSMLDGrievedHeal;


AUTO_STRUCT;
typedef struct FSMLDMastermindPursue
{
	ExprLocalData baseData; AST(POLYCHILDTYPE(FSM_LDT_MMIND_PERSUE))

	U32 leaderRef;

	U32 dataSet : 1;
	U32 addedExitHandlers : 1;

}FSMLDMastermindPursue;

AUTO_STRUCT;
typedef struct FSMLDDisoriented
{
	ExprLocalData baseData; AST(POLYCHILDTYPE(FSM_LDT_DISORIENTED))
	Vec3 vDestinationPos;
	Vec3 vJagStartPos;
	U32 addedExitHandlers	: 1;
}FSMLDDisoriented;

typedef struct FSMStateTrackerEntry FSMStateTrackerEntry;

// key is not necessary, but if you don't use it, you can only have a max of one of your data
// structs per FSM state
void* getMyData(SA_PARAM_NN_VALID ExprContext* context, SA_PARAM_NN_VALID ParseTable pti[], U64 key);
void* getMyDataIfExists(SA_PARAM_NN_VALID ExprContext* context, ParseTable pti[], U64 key);
void deleteMyData(ExprContext* context, ParseTable* pti, ExprLocalData ***localData, U64 key);

// Key is not necessary.  Gets local data from given local data.
void* getMyDataFromData(SA_PARAM_OP_VALID ExprLocalData ***localData, ParseTable pti[], U64 key);
void* getMyDataFromDataIfExists(SA_PARAM_OP_VALID ExprLocalData ***localData, ParseTable pti[], U64 key);

// returns whether a position was found, not whether the rays collided
int aiFindRunToPos(Entity* be, AIVarsBase* aib, const F32* myPos, const F32* targetPos,
				   const F32* idealVec, const F32* defaultPos, F32* outPos, F32 maxAngle);

int wanderAirInternal(Entity* be, ExprContext* context, Vec3 homePos, F32 minHeight, F32 maxHeight, int groundRelative, char **errString);
int wanderGroundInternal(Entity* be, ExprContext* context, Vec3 homePos, char** errString);
void aiCivilianSetSpeed(Entity *e, F32 fSpeed);
void aiMovementSetRotationFlag(Entity* be, U32 disabled);