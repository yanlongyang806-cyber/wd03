#pragma once

#include "aiCivilianPrivate.h"


typedef struct AICivPOIManager AICivPOIManager;
typedef struct AICivPlayerKillEventManager AICivPlayerKillEventManager;
typedef struct AICivilianPedestrian AICivilianPedestrian;
typedef struct AICivWanderArea AICivWanderArea;
typedef struct Beacon Beacon;
typedef struct GameInteractable GameInteractable;

// ---------------------------------------------------------------
typedef struct AICalloutHistory
{
	EntityRef entCalloutEntity;
	U32 timestamp;
} AICalloutHistory;

typedef enum ECivilianState
{
	ECivilianState_NULL = -1,
	ECivilianState_DEFAULT = 0,
	ECivilianState_SCARED,
	ECivilianState_FSM,
	ECivilianState_POI,
	ECivilianState_WANDER,
	ECivilianState_COUNT
}ECivilianState;

#define CALLOUT_HISTORY_SIZE	5
#define CIVPREVIOUS_HISTORY_SIZE 4
typedef S16 S16Point[2];

typedef bool (*fpPathingUpdate)(Entity *e, AICivilianPedestrian *civ, const Vec3 vCurPos);
typedef void (*fpPathingComplete)(Entity *e, AICivilianPedestrian *civ, const Vec3 vCurPos, int bPathTimedOut);

// ---------------------------------------------------------------
typedef struct AICivilianPedestrian
{
	AICivilian				civBase;

	F32						distFromLeg;

	AICivCalloutInfo		calloutInfo;
	Vec3					calloutStartPos;
	AICalloutHistory		calloutHistory[CALLOUT_HISTORY_SIZE];

	AICivPOIUser			*pPOIUser;

	ECivilianState			eState;
	CommandQueue			*animListCmdQueue;

	// the source position of the scare
	S16Point				s16ScarePos;
	// the guy that scared me
	EntityRef				erScarer;
	
	Vec3					vTargetPathPos;
	F32						fTargetRot;
	fpPathingComplete		fpPathingStateComplete;

	F32						fAmbientJobTime;
	
	AICivWanderArea			*pWanderArea;
	const Beacon			*pBeacon;
	const Beacon			*pPrevBeacon;
	S64						lastWanderTime;

	U8						previousStates[CIVPREVIOUS_HISTORY_SIZE];

	// Timers;
	U32	lastScareTime;
	U32 idleTime;
	U32 lastPOITime;
	S64 timeLastStateChange;
	S64	startPathTime;
	U8	ticksNotMoving;
			
	U32 bIsUsingCrosswalk : 1;
	U32 bPlayerIsNearby : 1;

	// if we recieved a scare that we need to resond to
	U32	bScareReceived : 1; 
	U32 bSnapToTarget : 1;

	U32 bPOIFSM : 1;
	U32 bFailedPath : 1;

	U32 bUseDefaultRequesterMode : 1;

} AICivilianPedestrian;

// ---------------------------------------------------------------
void aiCivPedestrianInitializeData(void);
void aiCivPedestrianOncePerFrame(void);
void aiCivPedestrian_Tick(Entity *e, AICivilianPedestrian *civ);
void aiCivPedestrianShutdown(void);
AICivilianFunctionSet* aiCivPedestrianGetFunctionSet(void);

void aiCivPedestrian_FixupPedDef(AICivPedestrianDef *pPedDef);
int aiCivPedestrian_ValidatePedDef(AICivPedestrianDef *pPedDef, const char *pszFilename);
void aiCivPedestrian_FixupTypeDef(AICivPedestrianTypeDef *pPedTypeDef);
int aiCivPedestrian_ValidateTypeDef(AICivPedestrianTypeDef *pPedTypeDef, const char *pszFilename);


void aiCivilianStopFSM(Entity *e, AICivilianPedestrian *civ);
void aiCivPedestrian_InitWaypointDefault(AICivilianPedestrian *civ, AICivilianWaypoint *wp);

// ---------------------------------------------------------------
// POI 
AICivPOIManager* aiCivPOIManager_Create(void);
void aiCivPOIManager_Destroy(AICivPOIManager **ppManager);
void aiCivPOIManager_AddPOI(S32 iPartitionIdx, AICivPOIManager *pManager, GameInteractable *poi);
void aiCivPOIManager_Update(AICivPOIManager *pManager);
void aiCivilian_ReleaseAllPOIUsers(AICivilianPartitionState *pPartition);

// ---------------------------------------------------------------
// Kill event 
void aiCivPlayerKillEvents_Update(AICivPlayerKillEventManager *pManager);
void aiCivPlayerKillEvents_Destroy(AICivPlayerKillEventManager **ppManager);
AICivPlayerKillEventManager* aiCivPlayerKillEvents_Create(int partitionIdx);


