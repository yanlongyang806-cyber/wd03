#pragma once

#include "referencesystem.h"

typedef struct AITeam AITeam;
typedef struct Entity Entity;
typedef struct AIDebugFormation AIDebugFormation;

typedef U32 EntityRef;

// ----------------------------------------------------------------------------------------------------------------
// def structs
// ----------------------------------------------------------------------------------------------------------------


AUTO_STRUCT;
typedef struct AIFormationSlotDef
{
	const char *pchName;							AST(POOL_STRING)

	// positive X is to the right
	F32 x;

	// positive Z is forward
	F32	z;

} AIFormationSlotDef;


AUTO_ENUM;
typedef enum AIFormationFidelity
{
	AIFormationFidelity_NULL = -1,
	AIFormationFidelity_LOW = 0,	ENAMES(low)
	AIFormationFidelity_MED,		ENAMES(medium)
	AIFormationFidelity_HIGH,		ENAMES(high)
	AIFormationFidelity_COUNT
} AIFormationFidelity;

AUTO_STRUCT;
typedef struct AIFormationDef
{
	AIFormationSlotDef	**eaSlots;			AST(NAME("Slot"))


	// the threshold of distance the leader moves before new formation positions are recalculated
	F32 distUpdateThreshold;

	// the threshold of rotation of the leader before new formation positions are recalculated
	// a value of 0 means to ignore rotation
	F32 rotUpdateThreshold;

	// controls the update time and the detail at which the slot validation is done
	AIFormationFidelity		fidelity;

	const char* pchName;					AST(KEY, STRUCTPARAM)
	char* pchFilename;						AST(CURRENTFILE)

	Vec2 vFormationCenter;					NO_AST

	// when the slot collides with something, the minimum 
	F32 fMinSlotTruncate;					AST(DEFAULT(7))

	// Walk and run distances used for AI movement
	F32 fWalkDistance;
	F32 fRunDistance;
	
	// cheating movement speed 
	U32 bDoCheatingMovement : 1;

	// If true, when validating a slot, allows the position to be moved inwards from the formation origin
	// if false, any collision will invalidate the slot
	U32 bDoCollisionSlotTruncating : 1;

	// if set, the formation center will NOT be recalculated to be the center of all formation slots
	U32 bDoNotRecenterFormation : 1;

	// if set, each time the formation updates it will reevaluate the best slot to assign each member
	U32 bAlwaysReassignSlots : 1;

	U32	autoSlotted : 1;
} AIFormationDef;


// ----------------------------------------------------------------------------------------------------------------
// run-time data structs
// ----------------------------------------------------------------------------------------------------------------

typedef struct AIFormationSlot
{
	Vec3	vPos;
	Vec3	vOffset;
	Quat	qRot;

	EntityRef	erAssignee;
	
	U32		isValid : 1;

} AIFormationSlot;

typedef struct AIFormation
{
	AITeam *pTeam;
	
	// the leader ref that the entities will be 
	EntityRef	erLeader;

	// the current formation position / orientation
	Vec3		vFormationPos;
	Vec3		vFormationPYR;
	Quat		qFormationRot;

	REF_TO(AIFormationDef)	hFormationDef;
	AIFormationDef			*pOwnedFormationDef;

	// current valid formation slots
	AIFormationSlot		**eaFormationSlots;

	S32			lastUpdateNumMembers;

	// for debugging - possibly temporary
	U32			uRefCount;

	// timers
	F32			updateFreqInSeconds;
	S64			lastUpdate;
	
	U32			bSettled : 1;
	U32			bIsPatrol : 1;
	U32			bIsDirty : 1;
	U32			bIsFlying : 1;

	// if set we are evaluating for spawn locations, 
	// so we will ignore some rules for formation updating like checking aiIsEntAlive
	U32			bUpdatingForSpawnLocations : 1;
} AIFormation;

// per AIVarsBase
typedef struct AIFormationData
{
	AIFormation *pFormation;

	// used for searching for best slot for a member
	AIFormationSlot *pClosestSlot;
	AIFormationSlot *pCurrentSlot;
	F32				fClosestSlotDist;

	U32		bAssignedFormationSlot : 1;
	U32		bIsPatrolling : 1;
	
} AIFormationData;

// 
void aiFormation_Startup();
void aiFormation_OnMapLoad();

void aiFormation_UpdateFormation(int iPartitionIdx, AIFormation *pFormation);

void aiFormation_Destroy(AIFormation **ppFormation);

bool aiFormation_AssignFormationByName(const char *pszFormationDefName, AITeam *pTeam, Entity * pOwner);
bool aiFormation_AssignDefaultFormation(AITeam *pTeam, Entity * pOwner);


void aiFormationData_Free(AIFormationData* formationData);

AIFormationDef* aiTeamGetFormationDef(SA_PARAM_NN_VALID AITeam* team);
bool aiFormation_CreateFormationForPatrol(SA_PARAM_NN_VALID AITeam* team);

void aiFormation_DoFormationMovementForMember(int iPartitionIdx, AITeam *pTeam, Entity *eMember);

void aiFormation_GetDebugFormationPositions(int iPartitionIdx, AIFormation * pFormation, AIDebugFormation *pDebugFormation);

void aiFormation_TryToAddToFormation(int iPartitionIdx, AITeam * pTeam, Entity * pEntity);

// gets the spawn position and rotation for the given entity.
bool aiFormation_GetSpawnLocationForEntity(SA_PARAM_NN_VALID Entity *pEntity, Vec3 vPos, Quat qRot);
