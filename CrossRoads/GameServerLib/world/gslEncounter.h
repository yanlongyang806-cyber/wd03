/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

#include "Encounter_enums.h"
#include "Octree.h"
#include "WorldLibEnums.h"

typedef struct ContactLocation ContactLocation;
typedef struct CritterVar CritterVar;
typedef struct EncounterTemplate EncounterTemplate;
typedef struct Entity Entity;
typedef struct EventTracker EventTracker;
typedef struct ExprContext ExprContext;
typedef struct Expression Expression;
typedef struct ExprFuncTable ExprFuncTable;
typedef struct FSM FSM;
typedef struct GameEncounterGroup GameEncounterGroup;
typedef struct GameEvent GameEvent;
typedef struct GameEventParticipant GameEventParticipant;
typedef struct OldEncounterVariable OldEncounterVariable;
typedef struct StashTableImp *StashTable;
typedef struct WorldEncounter WorldEncounter;
typedef struct WorldInteractionProperties WorldInteractionProperties;
typedef struct WorldInteractionPropertyEntry WorldInteractionPropertyEntry;
typedef struct WorldLogicalGroup WorldLogicalGroup;
typedef struct WorldScope WorldScope;
typedef struct WorldVariable WorldVariable;
typedef struct WorldVolumeQueryCache WorldVolumeQueryCache;
typedef struct ZoneMap ZoneMap;


typedef struct GameEncounterEvents
{
	// Keeps track of events that can trigger certain behavior within the encounter
	StashTable stEventLog;						// Since map was started
	StashTable stEventLogSinceSpawn;			// Since Encounter last spawned
	StashTable stEventLogSinceComplete;			// Since Encounter was last completed

	// List of tracked events that were allocated by this encounter
	GameEvent **eaTrackedEvents;				// Since map was started
	GameEvent **eaTrackedEventsSinceSpawn;		// Since Encounter last spawned
	GameEvent **eaTrackedEventsSinceComplete;	// Since Encounter was last completed

	// Cached info for GameEvents relating to this Encounter
	GameEventParticipant *pGameEventInfo;

	// Event tracker
	EventTracker *pEventTracker;
} GameEncounterEvents;


typedef struct GameEncounterTimers
{
	// Tick counter for reduced time on some actions
	U16 uFallPhasesSkipped;
	U16 uActivePhasesSkipped;
	U16 uSpawnPhasesSkipped;

	// Times tracked for the encounter
	U32 uTimeLastSpawned;
	U32 uTimeLastCompleted;

	// Time remaining until next spawn
	F32 fSpawnTimer; 

	//Spawn rate multiplier.  Used for dynamic spawning
	F32 fSpawnRateMultiplier;

	//Used to delay spawning whence it is valid to spawn
	bool bSpawnDelayCountdown;
	F32 fSpawnDelayTimer;
} GameEncounterTimers;


typedef struct GameEncounterPlayers
{
	// One player who has credit for spawning this encounter (for AI, ambushing, etc)
	EntityRef uSpawningPlayer;

	// This is a timestamp to keep track of how long this encounter has been waiting for the spawning player to become initialized (Ent->pChar->bLoaded).
	// If this exceeds SECONDS_TO_WAIT_FOR_OWNER, then an error will be thrown and the encounter will spawn anyway (and will use the entity's exp level instead of combat level if needed)
	U32 uWaitingForInitializationTimestamp; 

	// EntityRefs of players who have "tagged" this Encounter
	EntityRef* eauEntsWithCredit;
} GameEncounterPlayers;


AUTO_STRUCT;
typedef struct GameEncounterCachedActorVar
{
	WorldVariable* pVariable;
	U32 iActorIndex;
} GameEncounterCachedActorVar;


typedef struct GameEncounterPartitionState
{
	// The partition this encounter is in, which sets the partition of everything it spawns
	int iPartitionIdx;

	// The encounter's current state
	EncounterState eState;

	// This is set when a wave has a delay
	bool bWaveReady;

	// This encounter should be managed by its logical group
	bool bShouldBeGroupManaged;

	// Critter entities that belong to this encounter
	Entity **eaEntities;

	// The number of entities spawned last time
	int iNumEntsSpawned;

	// The number of times this has spawned a wave
	int iNumTimesSpawned;

	// Timers used for tracking the encounter
	GameEncounterTimers timerData;

	// Players associated with the encounter
	GameEncounterPlayers playerData;

	// Used for event tracking on the encounter
	GameEncounterEvents eventData;

	// Debugging status that helps explain why the current encounter is in the state it is in
	char debugStatus[64];

	// Cached FSM Vars which have already been calculated and do not need to be calculated again
	GameEncounterCachedActorVar** eaCachedVars;	
} GameEncounterPartitionState;


// Note that although this is an auto-struct, it is never struct alloc'd or struct-freed
// All fields other than name should be NO_AST
AUTO_STRUCT;
typedef struct GameEncounter
{
	// The encounter's map-level name
	const char *pcName; // pooled string

	// The world spawn point
	WorldEncounter *pWorldEncounter;		NO_AST
	WorldEncounter *pWorldEncounterCopy;	NO_AST

	// The encounter group (if any) this encounter is part of
	GameEncounterGroup *pEncounterGroup;	NO_AST

	// Cached encounter template for this encounter
	EncounterTemplate *pCachedTemplate;		NO_AST

	// Cached values for performance
	Vec3 vPos;								NO_AST
	bool bIsDynamicSpawn;					NO_AST
	bool bIsNoDespawn;						NO_AST
	bool bIsWave;							NO_AST
	bool bIsDynamicMastermind;				NO_AST
	F32 fSpawnRadius;						NO_AST
	F32 fRespawnTime;						NO_AST
	WorldRegionType eRegionType;			NO_AST

	// List of tracked events that were allocated by this encounter
	// Note that real tracking is done in per-partition data
	GameEvent **eaUnsharedTrackedEvents;				NO_AST // Since map was started
	GameEvent **eaUnsharedTrackedEventsSinceSpawn;		NO_AST // Since Encounter last spawned
	GameEvent **eaUnsharedTrackedEventsSinceComplete;	NO_AST // Since Encounter was last completed

	// Used to attach the encounter to the octree for debugging
	OctreeEntry octreeEntry;				NO_AST

	GameEncounterPartitionState **eaPartitionStates; NO_AST
} GameEncounter;
extern ParseTable parse_GameEncounter[];
#define TYPE_parse_GameEncounter GameEncounter


typedef struct GameEncounterGroup
{
	// The encounter group's map-level name
	const char *pcName; // Pooled string

	// The world logical group
	WorldLogicalGroup *pWorldGroup;
	WorldLogicalGroup *pWorldGroupCopy;

	// The game encounters in this group
	GameEncounter **eaGameEncounters;
} GameEncounterGroup;

AUTO_STRUCT;
typedef struct PartitionLogicalGroupEncData
{
	// The encounter group name
	const char* pchLogicalGroup; AST(POOL_STRING KEY)

	// The encounters that have spawned once
	const char** ppchSpawnedEncountersOnce; AST(POOL_STRING)

	// This is set when the group should no longer be updated
	bool bDisableUpdates;
} PartitionLogicalGroupEncData;

typedef struct PartitionEncounterData
{
	int iPartitionIdx;
	U32 uPartition_dbg;

	// Time values
	U32 uStartTime;

	// Owner lookups
	U32 uFindOwnerStartTime;	// First time anyone tried to look up the owner
	U8 bOwnerFound;				// Set to true when the owner is actually found
	U8 bIsRunning;				// Set to true once known to be running

	// Used to calculate number of active players for encounter spawning.
	// Value which is only valid for the first NEARBY_TEAMMATE_COUNT_LIFESPAN seconds of the map.  It is set by
	// a player entering the map and is the number of players near the door of the map the player left from at the
	// time of departure.
	U32 uNearbyTeamSize;

	///  WOLF[22Nov11] The eaExcludeList is used specifically to prevent duplicate bridge officers from
	// appearing on player bridges. In the encounter spawn, the name of spawned named pets from a
	// pet contact list are added to this list. Therefore, pet contact list spawns will be unique per
	// partition. Pet contact lists used for contacts do not participate in this system.
	char **eaPetContactNameExcludeList;

	// Keeps track of encounters that have spawned for each logical group marked as LogicalGroupRandomType_OnceOnLoad
	PartitionLogicalGroupEncData** eaLogicalGroupData;

	WorldVolumeQueryCache *pPlayableVolumeQuery;
} PartitionEncounterData;


extern bool g_EncounterResetOnNextTick;
extern bool g_EncounterProcessing;
extern U32 g_ForceTeamSize;
extern int g_encounterDisableSleeping;
extern int g_encounterIgnoreProximity;
extern U32 g_AmbushCooldown;
extern U32 g_AmbushChance;
extern U32 g_AmbushSkipChance;
extern U32 g_AmbushDebugEnabled;


// Gets an encounter, if one exists
SA_RET_OP_VALID GameEncounter *encounter_GetByName(SA_PARAM_NN_STR const char *pcEncounterName, SA_PARAM_OP_VALID const WorldScope *pScope);
SA_RET_OP_VALID GameEncounterGroup *encounter_GetGroupByName(SA_PARAM_NN_STR const char *pcGroupName, SA_PARAM_OP_VALID const WorldScope *pScope);
SA_RET_NN_VALID GameEncounterPartitionState *encounter_GetPartitionState(int iPartitionIdx, GameEncounter *pEncounter);
SA_RET_NN_VALID GameEncounterPartitionState *encounter_GetPartitionStateIfExists(int iPartitionIdx, GameEncounter *pEncounter);

// Called each tick
void encounter_OncePerFrame(F32 fTimeStep);

// Functions to get expression context stash tables
ExprFuncTable* encPlayer_CreateExprFuncTable(void);
ExprFuncTable* encounter_CreateExprFuncTable(void);
ExprFuncTable* encounter_CreatePlayerExprFuncTable(void);
ExprFuncTable* encounter_CreateInteractExprFuncTable(void);

// Functions to evaluate expressions
int encPlayer_Evaluate(Entity *pEnt, Expression *pExpr, WorldScope *pScope);
int encounter_ExprInteractEvaluate(int iPartitionIdx, Entity *pEnt, Entity *pTarget, Expression *pExpr, WorldScope *pScope);

// Function to test if map owner is ready or not
bool encounter_IsMapOwnerAvailable(int iPartitionIdx);

// Get the longest interact range of any actor
U32 encounter_GetMaxInteractRange(void);

// Get the number of living ents in an encounter
int encounter_GetNumLivingEnts(GameEncounter *pEncounter, GameEncounterPartitionState *pState);

// Get the entities near the encounter
Entity ***encounter_GetNearbyPlayers(int iPartitionIdx, GameEncounter *pEncounter, F32 fDist);

// Get the players to reward for the encounter
void encounter_GetRewardedPlayers(int iPartitionIdx, GameEncounter *pEncounter, Entity ***peaRewardedPlayers);

// Gets the FSM for the specified actor.  Checks world actor first, then encounter template actor.
FSM *encounter_GetActorFSM(int iPartitionIdx, GameEncounter *pEncounter, int iActorIndex);

// Functions used by EncounterDebug
void encounter_GetAllSpawnedEntities(int iPartitionIdx, Entity ***peaEntities);
GameEncounter **encounter_GetEncountersWithinDistance(const Vec3 vPos, F32 fDistance);
int encounter_GetNumEncounters(int iPartitionIdx);
int encounter_GetNumRunningEncounters(int iPartitionIdx);
F32 encounter_GetActorMaxFSMCost(int iPartitionIdx, GameEncounter *pEncounter, int iActorIndex);
F32 encounter_GetPotentialFSMCost(int iPartitionIdx);

const char *encounter_GetName(GameEncounter *pEncounter);
const char *encounter_GetFilename(GameEncounter *pEncounter);
void encounter_GetEncounterNames(char ***peaNames);
void encounter_GetContactLocations(ContactLocation ***peaLocations);
void encounter_GetUsedEncounterTemplateNames(char ***peaNames);
void encounter_GetPosition(GameEncounter *pEncounter, Vec3 vPos);
bool encounter_IsNoDespawn(GameEncounter *pEncounter);
const char *encounter_GetPatrolRoute(GameEncounter *pEncounter);
int encounter_GetNumActors(GameEncounter *pEncounter);
bool encounter_HasActorName(GameEncounter *pEncounter, const char *pcActorName);
const char *encounter_GetActorName(GameEncounter *pEncounter, int iActorIndex);
Entity *encounter_GetActorEntity(int iPartitionIdx, GameEncounter *pEncounter, const char *pcActorName);
bool encounter_GetActorPosition(GameEncounter *pEncounter, int iActorIndex, Vec3 vPos, Vec3 vRot);
int encounter_GetActorNumInteractionEntries(GameEncounter *pEncounter, int iActorIndex, Critter *pCritter);
WorldInteractionPropertyEntry *encounter_GetActorInteractionEntry(GameEncounter *pEncounter, int iActorIndex, int iInteractIndex);
void encounter_GetEntities(int iPartitionIdx, GameEncounter *pEncounter, Entity ***peaEntities, bool bFilter, bool bIncludeDead);
EncounterTemplate *encounter_GetTemplate(GameEncounter *pEncounter);
void encounter_SetNearbyTeamsize(Entity* pEnt) ;
bool encounter_isMastermindType(GameEncounter *pEncounter);

// Returns all extern vars on a critter, even those overridden by others
void encfsm_GetAllExternVars(Entity *pEnt, ExprContext *pContext, WorldVariable ***peaVars, OldEncounterVariable ***peaOldVars, CritterVar ***peaCritterVars, CritterVar ***peaGroupVars);

// Functions used by beaconizer
void encounter_GatherBeaconPositions(void);

// Called when the entity is created
void encounter_AddActor(int iPartitionIdx, GameEncounter *pEncounter, int iActorIndex, Entity *pEntity, int iTeamSize, int iTeamLevel);

// Called when the entity dies
void encounter_RemoveActor(Entity *pEnt);

// Called when an entity needs to insta-leash
void encounter_MoveCritterToSpawn(Entity *pEnt);

// Exposed for use by old encounter system, but not otherwise called from outside gslEncounter
bool encounter_TryPlayerAmbush(Entity *pPlayerEnt);

// Force spawn an encounter
void encounter_SpawnEncounter(GameEncounter *pEncounter, GameEncounterPartitionState *pState);

// Force reset an encounter
void encounter_Reset(GameEncounter *pEncounter, GameEncounterPartitionState *pState);
// Force reset all encounters in the specified partition
void encounter_ResetAll(int iPartitionIdx);

// Disables encounter sleeping
void encounter_disableSleeping(int bDisable);

// Event count functions
int encounter_EventCount(int iPartitionIdx, GameEncounter *pEncounter, const char *pcEventName);
int encounter_EventCountSinceSpawn(int iPartitionIdx, GameEncounter *pEncounter, const char *pcEventName);
int encounter_EventCountSinceComplete(int iPartitionIdx, GameEncounter *pEncounter, const char *pcEventName);

// Called on map load and unload
void encounter_PartitionLoad(int iPartitionIdx, bool bFullInit);
void encounter_PartitionUnload(int iPartitionIdx);

void encounter_MapLoad(ZoneMap *pZoneMap, bool bFullInit);
void encounter_MapUnload(void);
void encounter_MapValidate(ZoneMap *pZoneMap);
