#pragma once
GCC_SYSTEM

typedef struct	DefineContext DefineContext;

//moved encounter-related enums here, so it's easier to include just one file without getting all sorts
//of crazy autostruct stuff going on
// Current state of the encounter


#ifndef SDANGELO_REMOVE_PATROL_ROUTE
AUTO_ENUM;
typedef enum OldPatrolRouteType
{
	OldPatrolRouteType_PingPong,
	OldPatrolRouteType_Circle,
	OldPatrolRouteType_OneWay,
} OldPatrolRouteType;
#endif

AUTO_ENUM;
typedef enum EncounterState
{
	// Not near any players
	EncounterState_Asleep,
	// Waiting for a trigger to occur
	EncounterState_Waiting,

	// Critters spawned
	EncounterState_Spawned,
	// Players are close
	EncounterState_Active,
	// Creatures have been alerted
	EncounterState_Aware,

	// We completed successfully
	EncounterState_Success,
	// We completed in failure
	EncounterState_Failure,
	// We were supressed for some reason
	EncounterState_Off,
	// We were completely disabled and should never run again
	EncounterState_Disabled,
	// This encounter is managed by the logical group tick
	// rather than the regular encounter tick
	EncounterState_GroupManaged,

	EncounterState_Count,		EIGNORE
} EncounterState;

extern StaticDefineInt EncounterStateEnum[];

AUTO_ENUM;
typedef enum PowerStrength
{
	PowerStrength_Harmless,
	PowerStrength_Default,
	PowerStrength_Deadly,
	
	PowerStrength_Count,
} PowerStrength;

// This contains information about how this actor should spawn at a given player count
AUTO_ENUM;
typedef enum ActorScalingFlag
{
	ActorScalingFlag_Inherited = (1),
	ActorScalingFlag_One = (1 << 1),
	ActorScalingFlag_Two = (1 << 2),
	ActorScalingFlag_Three = (1 << 3),
	ActorScalingFlag_Four = (1 << 4),
	ActorScalingFlag_Five = (1 << 5),
} ActorScalingFlag;

AUTO_ENUM;
typedef enum Actor1CritterType
{
	Actor1CritterType_Normal,           // pulls from CritterDef or CritterGroup
	Actor1CritterType_Nemesis,          // Spawns a Nemesis, uses a CritterDef as default
	Actor1CritterType_NemesisMinion,    // Spawns a NemesisMinion, uses a CritterGroup as default
} Actor1CritterType;

extern StaticDefineInt Actor1CritterTypeEnum[];

extern DefineContext *s_pDefineEncounterDifficulties;
AUTO_ENUM AEN_EXTEND_WITH_DYNLIST(s_pDefineEncounterDifficulties);
typedef enum EncounterDifficulty
{
	// Defined in data/defs/EncounterDifficulties.def
} EncounterDifficulty;
extern StaticDefineInt EncounterDifficultyEnum[];
