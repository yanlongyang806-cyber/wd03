#pragma once

#include "aiEnums.h"

typedef struct AIConfig AIConfig;
typedef struct AIDebugAggroTableHeader AIDebugAggroTableHeader;
typedef struct AIStatusTableEntry AIStatusTableEntry;
typedef struct AIDebugStatusTableEntry AIDebugStatusTableEntry;
typedef struct AITeam AITeam;
typedef struct AIVarsBase AIVarsBase;
typedef struct Expression	Expression;	
typedef struct ParseTable	ParseTable;
typedef struct Entity Entity;

extern StaticDefineInt AIAggroCounterTypeEnum[];
AUTO_ENUM AEN_APPEND_OTHER_TO_ME(AINotifyType);
typedef enum AIAggroCounterType
{
	AI_AGGRO_COUNTER_DATA = AI_NOTIFY_TYPE_COUNT,	ENAMES(Expression)
	
	AI_AGGRO_COUNTER_COUNT
}AIAggroCounterType;

extern StaticDefineInt AIAggroGaugeTypeEnum[];
AUTO_ENUM;
typedef enum AIAggroGaugeType
{
	AI_AGGRO_GAUGE_DIST,						ENAMES(Distance)
	AI_AGGRO_GAUGE_STICKY,						ENAMES(Sticky)
	AI_AGGRO_GAUGE_DATA,						ENAMES(Expression)

	AI_AGGRO_GAUGE_COUNT,						EIGNORE
}AIAggroGaugeType;

extern StaticDefineInt AIAggroGaugeScaleTypeEnum[];
AUTO_ENUM;
typedef enum AIAggroGaugeScaleType
{
	AI_AGGRO_GAUGE_SCALE_COUNTER,				ENAMES(Counter)
	AI_AGGRO_GAUGE_SCALE_GAUGE,					EIGNORE
	AI_AGGRO_GAUGE_SCALE_POST_COUNTERS,			ENAMES(PostCounters)
	AI_AGGRO_GAUGE_SCALE_POST_ALL,				ENAMES(PostAll)
	AI_AGGRO_GAUGE_SCALE_MAX_TOTALCOUNTER,		ENAMES(MaxTotalCounter)
}AIAggroGaugeScaleType;

AST_PREFIX(WIKI(AUTO))

AUTO_STRUCT;
typedef struct AIAggroDefBucketOverride
{
	const char *pchName;						AST(NAME("Name") POOL_STRING)
	
	//  an override postScale of 0 will be ignored and use the value in the aiAggroFile instead
	// if you wish to disable the bucket, use the field Enabled 
	F32	fPostScale;								AST(DEFAULT(0))

	// if this field is enabled or not, defaults to enabled
	U32 bEnabled : 1;							AST(NAME("Enabled") DEFAULT(1))

} AIAggroDefBucketOverride;


AUTO_STRUCT;
typedef struct AIAggroGaugeDef
{
	// used primarily for debug displaying
	const char* name;							AST(STRUCTPARAM POOL_STRING)
	const char* desc;

	// controls what the gauge uses to calculate it's value. 
	// Distance: Uses a normalized ratio of current distance over aggro radius
	// Sticky: if the entity is our current target apply the sticky value, otherwise do not.
	// Expression: evaluates the given expression 
	AIAggroGaugeType gaugeType;
	
	// If the gaugeType is 'Expression' this is the expression that will be evaluated to get the gauge value
	Expression* expr;							AST(NAME(ExprBlock), REDUNDANT_STRUCT(Expr, parse_Expression_StructParam), LATEBIND)
	
	// after the gauge is calculated, the scale applied to the gauge value
	F32	postScale;								AST(DEFAULT(1))

	// controls how the final aggro value is calculated from the gauge value. 
	// Counter: multiplies the result gauge value by the given scaleTargetType / scaleCounterType
	// PostCounters: multiplies the result gauge value by the sum of all counters
	// PostAll: multiplies the result gauge by the sum of all the counters and gauges processed up to that point. These PostAll gauges are processed in order.
	// MaxTotalCounter: multiplies the result gauge by the maximum counter total from all the entities a critter has aggro on
	AIAggroGaugeScaleType scaleType;

	// if the scaleType is Counter
	// damage: uses the damage counter
	// threat: uses the threat counter
	// healing: uses the healing counter
	AIAggroCounterType scaleCounterType;

	// valid only if scaleType is Counter
	// Self: anything done to me
	// Friends: anything done to teammates
	AINotifyTargetType scaleTargetType;

	// if this field is enabled or not, defaults to enabled
	U32 enabled : 1;							AST(DEFAULT(1))

}AIAggroGaugeDef;

AUTO_STRUCT;
typedef struct AIAggroCounterDef
{
	// used primarily for debug displaying
	const char* name;							AST(STRUCTPARAM POOL_STRING)

	const char* desc;
	
	// damage: uses the damage counter
	// threat: uses the threat counter
	// healing: uses the healing counter
	// expression: uses the given expr
	AIAggroCounterType counterType;

	// Self: anything done to me
	// Friends: anything done to teammates
	AINotifyTargetType targetType;
	
	// If the counterType is 'expression' this is the expression that will be evaluated to get the counter value
	Expression* expr;							AST(NAME(ExprBlock), REDUNDANT_STRUCT(Expr, parse_Expression_StructParam), LATEBIND)
		
	// scale applied to the counter to calculate the aggro value
	F32 postScale;								AST(DEFAULT(1))

	// the decay deducted from the counter every AI tick (~0.5 seconds)
	Expression *pExprDecayRate;					AST(NAME(ExprDecayRateBlock), REDUNDANT_STRUCT(DecayRate, parse_Expression_StructParam), LATEBIND)

	// the time since the counterType was last touched before decay will kick in
	F32 decayDelay;

	// if this field is enabled or not, defaults to enabled
	U32 enabled : 1;							AST(DEFAULT(1))
	
}AIAggroCounterDef;

AUTO_STRUCT;
typedef struct AIAggroEvent
{
	const char* name;							AST(STRUCTPARAM)

	AINotifyType notifyType;

	Expression* expr;							AST(NAME(ExprBlock), REDUNDANT_STRUCT(Expr, parse_Expression_StructParam), LATEBIND)

	U32 enabled : 1;							AST(DEFAULT(1))
}AIAggroEvent;

AUTO_STRUCT WIKI("AIAggroDef");
typedef struct AIAggroDef
{
	const char* name;							AST(KEY STRUCTPARAM)
	
	// list of counters to be processed 
	AIAggroCounterDef** aggroCounters;			AST(NAME(Counter))

	// list of gauges to be processed
	AIAggroGaugeDef** aggroGauges;				AST(NAME(Gauge))
	
	AIAggroEvent** aggroEvents;					AST(NAME(Event))

	char* filename;								AST(CURRENTFILE)

	// if non-zero, when an encounter is first aggroed, this value will be applied to every critter in that
	// encounter to the entity that initiated the aggro
	S32 initialPullThreat;

	// if non-zero, when a Power misses, this value will be applied to the target as "THREAT" to the entity
	//  that used the Power
	S32 powerMissedThreat;

	// when evaluating the distance gauge, any distance less than this value will count as a full gauge value
	F32 fMeleeRange;							AST(DEFAULT(8) NAME(MeleeRange))

	// the time after a critter has spawned that it will start accruing healing aggro
	F32 fTimeSinceSpawnToIgnoreHealing;

	// scales the healing aggro among the entities that have the target on their status table.
	// Example: If there are 5 enemies and you heal for 500, then each enemy receives 100 healing aggro
	U32 scaleHealingByLegalTargets : 1;					
	
	// if true, separates healing done to an entity's current heal target from healing done to something that isn't its target
	// healing done to the current target goes into targetType: AI_NOTIFY_TARGET_SELF
	// healing done to an entity that isn't the current target goes into targetType: AI_NOTIFY_TARGET_FRIENDS
	U32 separateHealingByAttackTarget : 1;

	AST_STOP
	
	U32 hasDecay : 1;

}AIAggroDef;


int aiAggro_ShouldScaleHealAggroByLegalTargets();
int aiAggro_ShouldSeperateHealingByAttackTarget();
int aiAggro_ShouldIgnoreHealing(S32 iPartitionIdx, AIVarsBase* aib);

void aiAggro_DoInitialPullAggro(AITeam* team, Entity* initialPuller);
S32 aiAggro_DoPowerMissedAggro(Entity *target, Entity *source, F32 threatScale);

void aiAggro2_FillInDebugStatusTableEntry(	Entity *pEnt,
											const AIConfig *pConfig,
											AIDebugStatusTableEntry *pDebugEntry, 
											AIStatusTableEntry* status, 
											int bPostDecay, 
											int bUseScaled);

void aiAggro2_FillInDebugTableHeaders(	Entity *pEnt,
										const AIConfig *pConfig,
										AIDebugAggroTableHeader ***peaAggroTableHeader);