#pragma once

#include "referencesystem.h"

typedef struct Entity Entity;
typedef struct GameEncounter GameEncounter;
typedef struct AITeam AITeam;
typedef U32 EntityRef;
typedef struct AIMastermindDef AIMastermindDef;
typedef struct FSM FSM;

// -----------------------------------------------
// Runtime structs
// -----------------------------------------------

// -----------------------------------------------
typedef struct MMPlayer
{
	Vec3		vLastPlayerPos;
	Vec3		vMenacePos;
	
	F32			fExposeLevel;
	F32			fPreviousExposeLevel;
	
	EntityRef	erPlayer;

	U64			timeLastSpawnForPlayer;

	U32			bIsDead : 1;
} MMPlayer;

typedef enum MMSentEncounterState
{
	MMSentEncounterState_SEARCHING,
	MMSentEncounterState_COMBAT,
	MMSentEncounterState_DONE
} MMSentEncounterState;

typedef enum MMEndPursueReason
{
	MMEndPursueReason_NONE,
	MMEndPursueReason_TIMEOUT,
	MMEndPursueReason_PLAYER_LEASH,
	MMEndPursueReason_PLAYER_DEAD,
	MMEndPursueReason_EXPOSE_WIPE,

} MMEndPursueReason;

typedef struct MMExposeSentEncounter
{
	// the encounter this was spawned from
	GameEncounter			*pEncounter;
	int						iPartitionIdx;

	MMSentEncounterState	eState;

	// the expose level this encounter was spawned for
	S32						exposeLevelSpawn;

	// not to be referenced, 
	// this is just for validation that we're looking at the right spawned team
	AITeam					*pTeam;

	// the player the encounter is sent at
	EntityRef				erPlayer;

	// where we last send the encounter
	Vec3					vLastSentPos;

	// time we last got the player pos, changed pos;
	S64						timeatLastPlayerPos;
	S64						timeatSent;
	
	Vec3					vSpawnPosition;

	S32						waitingForDespawnCount;
	MMEndPursueReason		endPursueReason;

	U32 staticEncounter : 1;
	U32 leashing : 1;
	U32 pathFailed : 1;
	U32 playerHasDied : 1;
	U32 reachedPursuePoint : 1;
	U32 stoppedPursue : 1;
	U32 FSMControlled : 1;
	U32 shouldEndPursue : 1;
	U32 hadCombatWithPlayer : 1;
} MMExposeSentEncounter;



// -----------------------------------------------
typedef struct MMQueuedSpawn
{
	// what player we're spawning for
	EntityRef		erPlayer;

	EntityRef		*earNearbyEnts;
	S32				iCurrentEnt;

	S32				exposeLevel;

	const char *	pchEncGroupName;

	// when we find an enc, send to this pos
	Vec3			vLastPlayerPos;

	// time we last got the player pos
	S64				timeatLastPlayerPos;		

	// list of encounters we're using to search
	GameEncounter	**eaPotentialEncounters;
	S32				iCurrentEnc;

	// where the enc search originated from
	Vec3			vSearchCenter;

	// num times we've looped around these encounters checking
	S32				iTimesLooped;

	// time we started searching
	S64				timeatSearchStart;

	U32	didCheckForSpawnedEncounters : 1;
} MMQueuedSpawn;


// -----------------------------------------------
typedef struct AIMMExposeManager
{
	S64					timeatLastUpdate;

	// actively searching for spawn positions		
	MMQueuedSpawn		**eaActiveQueuedSpawns;

	MMQueuedSpawn		**eaQueuedSpawnPool;

	// list of encounters sent
	MMExposeSentEncounter	**eaSentEncounters;

	MMPlayer			**eaPlayers;

	F32					fOverrideExpose;
	F32					fExposeMax;

	U32		bOverrideExpose : 1;
	U32		bInitialized : 1;
	U32		bEnabled : 1;
} AIMMExposeManager;


void aiMastermindExpose_ReportPlayerCreated(Entity *e);

void aiMastermindExpose_Initialize();
void aiMastermindExpose_Shutdown();

void aiMastermindExpose_OnMapUnload();
void aiMastermindExpose_OnMapLoad();

void aiMastermindExpose_Update(AIMastermindDef *pDef);
void aiMastermindExpose_FirstTickInit(AIMastermindDef *pDef);
