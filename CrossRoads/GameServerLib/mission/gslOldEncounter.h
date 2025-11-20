/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

#include "GlobalTypeEnum.h"
#include "referencesystem.h"
#include "Octree.h"
#include "entEnums.h"
#include "encounter_enums.h"
#include "WorldLibEnums.h"

typedef struct Array Array;
typedef struct ContactLocation ContactLocation;
typedef struct CritterDef CritterDef;
typedef struct CritterFaction CritterFaction;
typedef struct Entity Entity;
typedef struct EventTracker EventTracker;
typedef struct ExprContext ExprContext;
typedef struct Expression Expression;
typedef struct GameEvent GameEvent;
typedef struct GameEventParticipant GameEventParticipant;
typedef struct MinimapWaypoint MinimapWaypoint;
typedef struct Mission Mission;
typedef struct MissionDef MissionDef;
typedef struct Player Player;
typedef struct StashTableImp* StashTable;
typedef struct WorldInteractionEntry WorldInteractionEntry;
typedef struct WorldInteractionNode WorldInteractionNode;
typedef struct WorldScope WorldScope;
typedef struct WorldScopeNamePair WorldScopeNamePair;
typedef struct WorldVolume WorldVolume;
typedef struct ZoneMap ZoneMap;
typedef struct OldActor OldActor;
typedef struct OldStaticEncounter OldStaticEncounter;
typedef struct OldStaticEncounterGroup OldStaticEncounterGroup;
typedef struct EncounterDef EncounterDef;
typedef struct EncounterDefList EncounterDefList;
typedef struct OldEncounterGroup OldEncounterGroup;
typedef struct OldEncounterMasterLayer OldEncounterMasterLayer;

#ifndef SDANGELO_REMOVE_PATROL_ROUTE
typedef struct OldPatrolRoute OldPatrolRoute;
#endif

// Description of an actively running encounter
AUTO_STRUCT;
typedef struct OldEncounter
{
	int iPartitionIdx;

	// Absolute position and orientation of the encounter in the map
	Mat4 mat;

	// The definition of the encounter. Derived from the ref.
	REF_TO(OldStaticEncounter) staticEnc; AST(REFDICT(StaticEncounter))

	// Current state of the encounter
	EncounterState state;

	REF_TO(Entity) hOwner;				AST(COPYDICT(ENTITYPLAYER))

	// Keeps track of events that can trigger certain behavior within the encounter
	StashTable eventLog;				// Since map was started
	StashTable eventLogSinceSpawn;		// Since Encounter last spawned
	StashTable eventLogSinceComplete;	// Since Encounter was last completed

	// Faction/GangID that the majority of this encounter's actors belong to
	REF_TO(CritterFaction) hMajorityFaction;
	U32 uMajorityGangID;

	// List of tracked events that were allocated by this encounter
	GameEvent **eaTrackedEvents;					NO_AST
	GameEvent **eaTrackedEventsSinceSpawn;			NO_AST
	GameEvent **eaTrackedEventsSinceComplete;		NO_AST

	// Tracks Events scoped to the encounter
	EventTracker *eventTracker;				NO_AST

	// Keeps track of things the encounter may use for evaluation, not stored to the db
	ExprContext* context; NO_AST

	// Time the encounter was last spawned
	U32 lastSpawned;

	// Time the encounter was last completed
	U32 lastCompleted;

	// Time the encounter last went to sleep
	U32 lastSleep;

	// Time the encounter last checked for spawn condition expression
	U32 lastSpawnExprCheck;

	// Time remaining for the encounter to spawn
	F32 fSpawnTimer;

	// For dynamic respawn encounters, the speed at which the respawn timer advances
	F32 fSpawnRateMultiplier;

	// For wave attacks, whether the wave interval has already passed
	bool bWaveReady;

	// Contains number of nearby players when not spawned
	int iNumNearbyPlayers;

	// EArray of entities that belong to this encounter
	Entity** ents; NO_AST

	// Group this encounter is a part of
	OldEncounterGroup* encGroup; NO_AST

	// Used to attach the encounter to the octree
	OctreeEntry octreeEntry; NO_AST

	// Debugging status that helps explain why the current encounter is in the state it is in
	char status[64];

	// One player who has credit for spawning this encounter (for AI, ambushing, etc)
	EntityRef spawningPlayer; NO_AST

	// EntityRefs of players who have "tagged" this Encounter
	EntityRef* entsWithCredit; NO_AST

	WorldRegionType eRegionType; NO_AST
	U32 spawnRadius; NO_AST

	// Cached info for GameEvents relating to this Encounter
	GameEventParticipant *pGameEventInfo;	AST(SERVER_ONLY LATEBIND)
} OldEncounter;

extern ParseTable parse_OldEncounter[];
#define TYPE_parse_OldEncounter OldEncounter

// Group containing encounters, keeps a certain number of them populated and spawned
typedef struct OldEncounterGroup
{
	int iPartitionIdx;

	// TODO: This should probably be using references back to the enc layer
	// Pointer to the static encounter group that defines this group
	OldStaticEncounterGroup* groupDef;

	// List of encounters that this group created
	OldEncounter** childEncs;

	// List of sub encounter groups that this group owns
	OldEncounterGroup** subGroups;

	// Group that owns this encounter group
	OldEncounterGroup* parentGroup;
} OldEncounterGroup;

extern int g_EncounterReloadCounter;

typedef struct OldEncounterPartitionState
{
	int iPartitionIdx;

	OldEncounter **eaEncounters;
	OldEncounterGroup **eaEncounterGroups;
	Octree *pEncounterOctree;
	StashTable pEncounterFromStaticEncounterHash;

	bool bIsRunning;
} OldEncounterPartitionState;


// Main processing loop for the encounter system
void oldencounter_OncePerFrame(F32 fTimeStep);

// Loads all encounter defs and initializes all encounter related systems
void oldencounter_Startup(void);

OldEncounterPartitionState *oldencounter_GetPartitionState(int iPartitionIdx);

// Server validation for when the map is loaded
void oldencounter_PartitionLoad(int iPartitionIdx);
void oldencounter_PartitionUnload(int iPartitionIdx);
void oldencounter_MapLoad(ZoneMap *pZoneMap, bool bFullInit);
void oldencounter_MapUnload(void);
void oldencounter_MapValidate(void);

// Server fixup function for encounters and static encounters
int oldencounter_DefFixupPostProcess(EncounterDef* def);

// Returns the encounter def the encounter is using
EncounterDef* oldencounter_GetDef(OldEncounter* encounter);

// Finds an encounter from the static encounter name
OldEncounter* oldencounter_FromStaticEncounterName(int iPartitionIdx, const char* staticEncName);
OldEncounter* oldencounter_FromStaticEncounterNameIfExists(int iPartitionIdx, const char* staticEncName);

// Finds an encounter from a static encounter definition
OldEncounter* oldencounter_FromStaticEncounter(int iPartitionIdx, OldStaticEncounter* staticEnc);
OldEncounter* oldencounter_FromStaticEncounterIfExists(int iPartitionIdx, OldStaticEncounter* staticEnc);

// Returns true if an encounter is currently complete
bool oldencounter_IsComplete(OldEncounter* encounter);

// Returns an array of players that should get rewards and actions from the encounter
void oldencounter_GetRewardedPlayers(OldEncounter* encounter, Entity*** playersWithCredit);

// Returns an array of all entities that have credit for this encounter (including critters)
void oldencounter_GetRewardedEnts(OldEncounter* encounter, Entity*** entsWithCredit);

// Gets all players within a certain distance of the encounter
Entity*** oldencounter_GetNearbyPlayers(OldEncounter* encounter, F32 dist);

// Gets all currently spawned encounter entities
void oldencounter_GetAllSpawnedEntities(int iPartitionIdx, Entity*** entities);

OldEncounter** oldencounter_GetEncountersWithinDistance(OldEncounterPartitionState *pState, const Vec3 pos, F32 distance);
int oldencounter_GetNumEncounters(int iPartitionIdx);
int oldencounter_GetNumRunningEncounters(int iPartitionIdx);
U32 oldencounter_ActorGetMaxFSMCost(OldActor* actor);
U32 oldencounter_GetPotentialFSMCost(int iPartitionIdx);
void oldencounter_GatherBeaconPositions(void);

// Removes any attachment to the encounter from an actor
void oldencounter_RemoveActor(Entity* ent);

// Get contact locations
void oldencounter_GetContactLocations(ContactLocation ***peaContactLocations);

// Number of entities alive in the encounter
int oldencounter_NumLivingEnts(OldEncounter* encounter);

// Total number of entities in this encounter (for current team size)
int oldencounter_NumEntsToSpawn(SA_PARAM_NN_VALID OldEncounter* encounter);

void critter_ReplaceInPlace(Entity* critterEnt, char* defName, char* fsm, int iLevel);


// Utility Functions

const char* oldencounter_GetEncounterName(OldEncounter *encounter);
bool oldencounter_IsWaitingToSpawn(OldEncounter* encounter);
void oldencounter_Spawn(EncounterDef *def, OldEncounter* encounter);
void oldencounter_Reset(OldEncounter* encounter);
void oldencounter_AttachActor(Entity* ent, OldEncounter* encounter, OldActor* actor, U32 teamSize);

// Gets the world position of an actor
void oldencounter_GetEncounterActorPosition(OldEncounter *encounter, OldActor *actor, Vec3 outVec, Quat outQuat);
void oldencounter_GetStaticEncounterActorPosition(OldStaticEncounter *staticEnc, OldActor *actor, Vec3 outVec, Quat outQuat);

// Get an entity by a Encounter and Actor name
Entity* oldencounter_EntFromEncActorName(int iPartitionIdx, const char *pchStaticEncName, const char *pchActorName);

// Get an Actor position from an Encounter and Actor name
void oldencounter_ActorPosFromEncActorName(int iPartitionIdx, const char *pchStaticEncName, const char *pchActorName, Vec3 outPos);

WorldScopeNamePair **oldencounter_GetEncounterScopeNames(OldStaticEncounter *encounter);
WorldScopeNamePair **oldencounter_GetGroupScopeNames(OldStaticEncounterGroup *encounterGroup);