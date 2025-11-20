#pragma once

typedef struct Entity Entity;
typedef struct GameInteractLocation GameInteractLocation;
typedef struct FSM FSM;
typedef struct AIAnimList AIAnimList;

AST_PREFIX(WIKI(AUTO))

AUTO_STRUCT WIKI("AIAmbient");
typedef struct AIAmbient
{
	const char* pchFileName;			AST( CURRENTFILE )

	char* pchName;	AST(KEY POOL_STRING STRUCTPARAM)

	// wander speed (attrib mod for OverrideMovementSpeed)
	F32 fWanderSpeed;		AST(NAME("WanderSpeed") DEFAULT(4.0))
	
	// average time (in secs) to wander (before changing states)
	F32 fWanderDuration;	AST(NAME("WanderDuration") DEFAULT(10.0))
	
	// probability weighting of state being chosen
	F32 fWanderWeight;		AST(NAME("WanderProbabilityWeight") DEFAULT(1.0))
	
	// maximum distance to wander
	F32 fWanderDistance;		AST(NAME("WanderDistance") DEFAULT(40.0))

	// the average time that the critter will stay at it's wander position before moving again
	F32 fWanderIdleTime;		AST(NAME("WanderIdleTime") DEFAULT(4.0))

	// the maximum number of waypoints that the critter will get to wander before it stops idle for some time
	S32 iMaxWanderPath;		AST(NAME("MaxWanderPath") DEFAULT(1))

	// average time (in secs) to be chatting (before changing states)
	char *pchChatMessageKey; AST(NAME("ChatMessageKey"))

	// average time (in secs) to be chatting (before changing states)
	F32 fChatDuration;		AST(NAME("ChatDuration") DEFAULT(10.0))

	// probability weighting of state being chosen
	F32 fChatWeight;		AST(NAME("ChatProbabilityWeight") DEFAULT(1.0))

	// chat animation (note: currently will only allow one entry, no randomization)
	char *pchChatAnimation; AST(NAME("ChatAnimation"))


	// average time (in secs) to be idling (before changing states)
	F32 fIdleDuration;		AST(NAME("IdleDuration") DEFAULT(10.0))

	// probability weighting of state being chosen
	F32 fIdleWeight;		AST(NAME("IdleProbabilityWeight") DEFAULT(1.0))

	// idle animation (note: currently will only allow one entry, no randomization)
	char *pchIdleAnimation; AST(NAME("IdleAnimation"))


	// average time (in secs) before changing states
	F32 fJobDuration;		AST(NAME("JobDuration") DEFAULT(20.0))

	// probability weighting of state being chosen
	F32 fJobWeight;			AST(NAME("JobProbabilityWeight") DEFAULT(1.0))

	// Ambient Job Animation
	char *pchJobAnimation; AST(NAME("JobAnimation"))

	// is ambient job a choice
	U8 bIsJobActive : 1; AST(NAME("IsJobActive") DEFAULT(0))

	// how far a critter will look for a job
	F32 fJobAwarenessRadius;  AST(NAME("JobAwarenessRadius") DEFAULT(50.0))

	// The average cooldown on the job. Gets set once all the job locations have been filled on an ambient job
	F32 fJobCooldown;			AST(DEFAULT(30.f))

	// the average cooldown on the actual location
	F32 fJobLocationCooldown;	AST(DEFAULT(10.f))

	// the average cooldown on each critter
	F32 fJobCritterCooldown;	AST(DEFAULT(15.f))

	// height range
	F32 fWanderAirRange;	AST(NAME("WanderAirRange") DEFAULT(20))
	
	// (when flying) Determines how far ahead of a shortcut to attempt pseudospline
	F32 fDistBeforeWaypointToSpline; AST(NAME("DistBeforeWaypointToSpline") DEFAULT(3))

	// is wander a choice
	U8 bIsWanderActive : 1; AST(NAME("IsWanderActive") DEFAULT(1))

	// is idle a choice
	U8 bIsIdleActive : 1; AST(NAME("IsIdleActive") DEFAULT(1))

} AIAmbient;

// functions

bool aiAmbientChooseAvailableJob(int partitionIdx, 
								 SA_PARAM_OP_VALID GameInteractable *pBestInteract,
								 SA_PARAM_OP_VALID Entity* e, 
								 SA_PARAM_OP_VALID GameInteractLocation **ppWorldAmbientJob,
								 SA_PARAM_OP_VALID S32 *piSlot);

AIAmbient* aiAmbient_GetAmbientDef(SA_PARAM_OP_VALID const Vec3 vPos);
int aiAmbient_GetAction(GameInteractLocation *pJob, FSM **ppFSM, AIAnimList **ppAnimList);
bool aiAmbient_CanUseJob(int partitionIdx, Entity *e, GameInteractable *pJobInteractable, GameInteractLocation *pJob);
bool aiAmbient_ShouldEntityIgnoreJob(Entity* e, GameInteractable *pJobInteractable, GameInteractLocation *pJob);
void aiAmbientJob_CheckForOccupiedApplyCooldown(int iPartitionIdx, GameInteractable *pJobInteractable, F32 cooldownAvg);
float aiAmbientRandomDuration(F32 input, F32 variance);

extern ParseTable parse_AIAmbient[];
