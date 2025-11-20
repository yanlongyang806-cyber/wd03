/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

#include "StashTable.h"

typedef struct ContactDef ContactDef;
typedef struct DoorConn DoorConn;
typedef struct DoorTarget DoorTarget;
typedef struct Entity Entity;
typedef struct Expression Expression;
typedef struct ExprFuncTable ExprFuncTable;
typedef struct GameEvent GameEvent;
typedef struct GameInteractable GameInteractable;
typedef struct InteractionLootTracker InteractionLootTracker;
typedef struct InteractOption InteractOption;
typedef struct InteractTarget InteractTarget;
typedef struct InventoryBag InventoryBag;
typedef struct Team Team;
typedef struct WorldDoorInteractionProperties WorldDoorInteractionProperties;
typedef struct WorldRewardInteractionProperties WorldRewardInteractionProperties;
typedef struct WorldInteractionEntry WorldInteractionEntry;
typedef struct WorldGateInteractionProperties WorldGateInteractionProperties;
typedef struct WorldInteractionNode WorldInteractionNode;
typedef struct WorldInteractionPropertyEntry WorldInteractionPropertyEntry;
typedef struct WorldNamedInteractable WorldNamedInteractable;
typedef struct WorldScope WorldScope;
typedef struct WorldVariable WorldVariable;
typedef struct ZoneMap ZoneMap;
typedef struct WorldInteractLocationProperties WorldInteractLocationProperties;
typedef struct WorldAmbientJobInteractionProperties WorldAmbientJobInteractionProperties;
typedef struct OctreeEntry OctreeEntry;

typedef bool (*InteractableTestCallbackEnt)(Entity *pEnt, GameInteractable *pInteractable, void *pUserData);

#define I_STATE_HIDE		1
#define I_STATE_SHOW		0

#define I_NO_CHILD_INDEX	-1

typedef struct InteractableQuery
{
	int iPartitionIdx;
	U32 uClassMask;
	bool bCheckLOS;
	bool bCheckSelection;
	bool bCheckHidden;
	void *pUserData;
} InteractableQuery;

AUTO_STRUCT;
typedef struct GameInteractLocationPartition
{
	S64	cooldownTime;
	S64	secondaryCooldownTime; // Used for combat job cooldowns
	U8 bOccupied : 1; // read/write
	U8 bReachedLocation : 1; 
} GameInteractLocationPartition;

AUTO_STRUCT;
typedef struct GameInteractLocation
{
	WorldInteractLocationProperties *pWorldInteractLocationProperties; // holds a copy from the worldInteractable

	GameInteractLocationPartition **eaPartitions;
} GameInteractLocation;

AUTO_STRUCT;
typedef struct GameInteractableOverride
{
	const char *pcName;			AST(POOL_STRING)
	const char *pcFilename;		AST(FILENAME POOL_STRING)
	WorldInteractionPropertyEntry *pEntry;
} GameInteractableOverride;

AUTO_STRUCT;
typedef struct GameInteractablePartitionState
{
	int iPartitionIdx;
	GameInteractable *pInteractable;

	// General States
	bool bActive;				// Set to true while the interactable is active
	bool bSleeping;				// Set to true while the interactable is sleeping
	bool bHidden;				// True if self is hidden
	bool bDisabled;				// True if node is disabled
	bool bParentHidden;			// True if a parent is hidden
	bool bGateOpen;				// True if gate is in open state
	int  iGateWasOpeningState;	// 0 if we have not started interacting. -1 if closing, 1 if opening. Managed via im_Interact and im_EndInteract
	int iChildIndex;			// Current child index value
	EntityRef uEntToWaitFor;	// Entity to wait for on hide attempt

	// Use state
	int iPlayerOwnerID;			// The "owner" of this interactable, if this is an exclusive interaction
	int iLastInteractIndex;		// The InteractIndex used the last time this object was used
	S64	ambientJobCooldownTime;	// Timer for cooldown
	S64	combatJobCooldownTime;	// Timer for combat job cooldown

	// Tracks events for this partition
	StashTable stEventLog;					NO_AST
	GameEvent** eaTrackedEvents;			NO_AST

	// Door variables calculated the last time this door was used; this is
	// cleared after the active time of the interactable has passed and is mainly used
	// with bTeamUsableWhenActive flag
	DoorTarget *pLastDoorTarget;			NO_AST
	WorldVariable **eaLastDoorVariables;	NO_AST

	// Used to track reward loot on interactable
	InteractionLootTracker *pLootTracker;	NO_AST

} GameInteractablePartitionState;

// NOTE: This structure is not created using StructCreate and it is not destroyed
// using StructDestroy.  It is only an AUTO_STRUCT so that it can be passed through
// an expression context.  The NO_AST's in here don't do anything except hide fields from the expression.
AUTO_STRUCT;
typedef struct GameInteractable
{
	// The interactable's name and logical group name with the map scope
	const char *pcName;
	const char *pcGroupName;					NO_AST

	// The world named interactable
	WorldNamedInteractable *pWorldInteractable;	NO_AST
	WorldInteractionEntry *pWorldEntry;			NO_AST
	OctreeEntry *pMainOctreeEntry;				NO_AST

	// The state.  There is one of these per partition
	GameInteractablePartitionState **eaPartitionStates;		NO_AST

	// Jobs
	GameInteractLocation **eaInteractLocations;	NO_AST	// Interact locations for Jobs
	WorldAmbientJobInteractionProperties *pAmbientJobProperties; NO_AST // Job properties
	OctreeEntry *pAmbientJobOctreeEntry;		NO_AST  // Entry for holding octree data for ambient job
	OctreeEntry *pCombatJobOctreeEntry;			NO_AST  // Entry for holding octree data for combat job

	// Event tracking
	GameEvent** eaUnsharedTrackedEvents;		NO_AST	// Filled in when expressions are generated

	// Parent/child data
	GameInteractable *pParent;					NO_AST
	int iChildIndexInParent;					NO_AST
	GameInteractable **eaChildren;				NO_AST

	// Other Info
	Expression *pCritterUseCond;				NO_AST
	GameInteractableOverride **eaOverrides;		NO_AST

	// A count stored post load that dictates how many overrides were present at load
	// Post-load overrides come from namespaced Missions and this count allows the server to skip namespaced
	// overrides on players who have no namespace Missions (see MissionInfo.bHasNamespaceMission)
	U32 uOverrideLoadCount;						NO_AST

	// Cached information about the interactable for performance
	bool bHasVisibleExpression;					NO_AST
	bool bHasChildExpression;					NO_AST
	bool bHasChildIndex;						NO_AST
	bool bIsVolumeTriggeredGate;				NO_AST
	bool bCanBeInteractedWith;					NO_AST
	bool bVisiblePerEntity;						NO_AST
	bool bIsDestructible;						NO_AST
	U32 uClassMask;								NO_AST
	const char *pcNodeName;						NO_AST
	const char **eaInteractableCategories;		NO_AST
	const char **eaTags;						NO_AST

	// Debug logging statement
    char *estrDebugLog;							NO_AST  // ESTRING				
} GameInteractable;

// Constant for use in accessing expression variable
extern const char *g_InteractableExprVarName;

// Debug constants
extern U32 g_InteractableDebug;
extern U32 g_InteractableVisible,g_InteractableVisibleOld;
extern U32 g_AllowAllInteractions;

// Search for interactables
bool interactable_InteractableExists(WorldScope *pScope, const char *pcInteractableName);
GameInteractable *interactable_GetByName(const char *pcInteractableName, const WorldScope *pScope);
GameInteractable *interactable_GetByEntry(WorldInteractionEntry *pEntry);
GameInteractable *interactable_GetByNode(WorldInteractionNode *pNode);
GameInteractablePartitionState *interactable_GetPartitionState(int iPartitionIdx, GameInteractable *pInteractable);
bool interactable_PartitionExists(int iPartitionIdx, GameInteractable *pInteractable);
// exactly the same as below, but I need one that doesn't assert
GameInteractLocationPartition* interactable_GetInteractLocationPartitionIfPresent(int iPartitionIdx, GameInteractLocation *pInteractLocation);
GameInteractLocationPartition* interactable_GetInteractLocationPartition(int iPartitionIdx, GameInteractLocation *pInteractLocation);

// Tests on interactables
bool interactable_IsActiveByName(int iPartitionIdx, WorldScope *pScope, const char *pcClickableName);
bool interactable_IsInUseByName(int iPartitionIdx, WorldScope *pScope, const char *pcInteractableName);
bool interactable_IsHiddenByName(int iPartitionIdx, WorldScope *pScope, const char *pcInteractableName);
bool interactable_IsDirectlyHidden(int iPartitionIdx, GameInteractable *pInteractable);
bool interactable_IsHidden(int iPartitionIdx, GameInteractable *pInteractable);
bool interactable_IsDisabled(int iPartitionIdx, GameInteractable *pInteractable);
bool interactable_IsHiddenOrDisabled(int iPartitionIdx, GameInteractable *pInteractable);
bool interactable_IsUsableByName(int iPartitionIdx, WorldScope *pScope, const char *pcInteractableName);
bool interactable_IsHideableEntry(WorldInteractionEntry *pWorldEntry);
bool interactable_IsSelectable(GameInteractable *pInteractable);
bool interactable_IsInterruptable(GameInteractable *pInteractable, int iIndex);
bool interactable_IsDestructibleAndNotInteractable(GameInteractable *pInteractable);
bool interactable_CanEntityInteract(Entity *pPlayerEnt, GameInteractable *pInteractable);
bool interactable_CanEntityAttempt(SA_PARAM_NN_VALID Entity *pPlayerEnt, GameInteractable *pInteractable);
bool interactable_IsBusy(int iPartitionIdx, GameInteractable *pInteractable, Entity *pPlayerEnt);
bool interactable_IsNotDestructibleOrCanThrowObject(Entity* pEnt, GameInteractable *pInteractable, UserData *pData);
// Operations on interactables
// The estring is optional.
bool interactable_HideInteractableByName(int iPartitionIdx, WorldScope *pScope, const char *pcInteractableName, F32 fFadeOutTime, char **estrErrString);
bool interactable_ResetInteractableByName(int iPartitionIdx, WorldScope *pScope, const char* pcInteractableName, F32 fUnused, char **estrErrString);
bool interactable_ShowInteractableByName(int iPartitionIdx, WorldScope *pScope, const char *pcInteractableName, F32 fFadeInTime, char **estrErrString);
void interactable_SetHideState(int iPartitionIdx, GameInteractable *pInteractable, bool bHide, EntityRef refToWaitFor, bool bForce);
void interactable_SetDisabledState(int iPartitionIdx, GameInteractable *pInteractable, bool bDisabled);
bool interactable_HideInteractableGroup(int iPartitionIdx, WorldScope *pScope, const char *pcGroupName, F32 fFadeOutTime, char **estrErrString);
bool interactable_ShowInteractableGroup(int iPartitionIdx, WorldScope *pScope, const char *pcGroupName, F32 fFadeInTime, char **estrErrString);
void interactable_SetVisibleChild(int iPartitionIdx, GameInteractable *pInteractable, int iChildIndex, bool bForce);
bool interactable_SetVisibleChildByName(int iPartitionIdx, WorldScope *pScope, const char *pcInteractableName, int iChildIndex, char **estrErrString);

// Data request functions
ExprFuncTable* interactable_CreateExprFuncTable(void);
bool interactable_GetPosition(GameInteractable *pInteractable, Vec3 vPos);
bool interactable_GetPositionByName(WorldScope *pScope, const char *pcInteractableName, Vec3 vPos);
bool interactable_FindClosestDoor(const Vec3 vPos, Vec3 vClosestDoorPos);
bool interactable_FindClosest(Entity* pEnt, float* maxDist, Vec3 vClosestPos, const char** ppchPooledClasses,
							  bool bDoValidInteractCheck, bool bIgnoreHidden, bool bCheckRegionMatch);

void interactable_GetDoorConnections(DoorConn ***peaDoors);

// bIncludePostLoad if false returns the count at GameServer load - used to exclude namespaced overrides
int interactable_GetNumPropertyEntries(GameInteractable *pInteractable, bool bIncludePostLoad);
ContactDef *interactable_GetContactDef(GameInteractable *pInteractable, int iIndex);
WorldInteractionNode *interactable_GetWorldInteractionNode(GameInteractable *pGameInteractable);
WorldInteractionPropertyEntry *interactable_GetPropertyEntry(GameInteractable *pInteractable, int iIndex);
WorldInteractionPropertyEntry *interactable_GetPropertyEntryForPlayer(Entity* pPlayerEnt, GameInteractable *pInteractable, int iIndex);
F32 interactable_GetCooldownMultiplier(int iPartitionIdx, WorldInteractionNode *pNode, int iIndex);
bool interactable_MapHasPerEntVisExpressions(void);

GameInteractable* interactable_FindClosestInteractableWithCheck(Entity *pEnt, U32 uInteractClassMask, InteractableTestCallbackEnt fTestCallback, UserData pCallbackData, F32 fMaxDistance, bool bCheckRegionMatch, bool bCheckLoS, F32 *pfDistOut);
GameInteractable* interactable_FindClosestInteractable(SA_PARAM_NN_VALID Entity *pEnt, U32 uInteractClassMask, F32 *pfCheckDist);
U32 interactable_GetNodeInteractMaxRange(Entity *pEnt);
void interactable_GetCategories(GameInteractable *pInteractable, const char ***peaCategories, Entity *pPlayerEnt);
void interactable_GetTags(GameInteractable *pInteractable, const char ***peaTags, Entity *pPlayerEnt);
bool interactable_GetVisibleChild(int iPartitionIdx, WorldScope *pScope, const char *pcInteractableName, int *result, char **estrErrString);

void interactable_QuerySphere(int iPartitionIdx, U32 uClassMask, void *pUserData, const Vec3 vWorldMid, F32 fRadius, bool bCheckLOS, bool bCheckSelection, bool bCheckHidden, GameInteractable ***peaInteractables);
F32 interactable_GetSphereBounds(GameInteractable *pInteractable, Vec3 vWorldMid);
void interactable_GetWorldMid(GameInteractable *pInteractable, Vec3 vWorldMid);

// Adds all entries from the global list that might have been excluded because of the cutoff on the interaction or target range
void interactable_AddGlobalInteractables(int iPartitionIdx, U32 uClassMask, const Vec3 vCenter, F32 fRadius, F32 fRadiusCutoff, bool bCheckLOS, bool bCheckSelection, bool bCheckHidden, GameInteractable ***peaInteractables);
F32 interactable_GetCutoffDist(Entity* pEnt);

// Event processing functions
int interactable_EventCount(int parttionIdx, GameInteractable *pInteractable, const char *pcEventName);

// Interaction processing functions
void interactable_SetActive(int iPartitionIdx, GameInteractable *pInteractable, bool bActive);
void interactable_SetNoLongerActive(int iPartitionIdx, WorldInteractionNode *pNode, int iIndex, EntityRef playerEntRef);
void interactable_FinishCooldown(int iPartitionIdx, GameInteractable *pInteractable, WorldInteractionNode *pNode, int iIndex, Entity *pPlayerEnt);
void interactable_EndInteraction(WorldInteractionNode *pNode, int iIndex, Entity *pPlayerEnt, bool bSucceeded);
U32 interactable_RewardPropsGetRewardLevel(Entity* pPlayerEnt, WorldRewardInteractionProperties* pRewardProps);
S64 interactable_GetAmbientCooldownTime(int iPartitionIdx, GameInteractable *pInteractable);
S64 interactable_GetCombatJobCooldownTime(int iPartitionIdx, GameInteractable *pInteractable);
void interactable_SetAmbientCooldownTime(int iPartitionIdx, GameInteractable *pInteractable, S64 iTime);
void interactable_SetCombatJobCooldownTime(int iPartitionIdx, GameInteractable *pInteractable, S64 iTime);
int interactable_GetPlayerOwner(int iPartitionIdx, GameInteractable *pInteractable);
void interactable_SetPlayerOwner(int iPartitionIdx, GameInteractable *pInteractable, int iOwner);
void interactable_SetLastInteractIndex(int iPartitionIdx, GameInteractable *pInteractable, int iIndex);
DoorTarget *interactable_GetLastDoorTarget(int iPartitionIdx, GameInteractable *pInteractable);
void interactable_SetLastDoorTarget(int iPartitionIdx, GameInteractable *pInteractable, DoorTarget *pDoorTarget);
WorldVariable **interactable_GetLastDoorVariables(int iPartitionIdx, GameInteractable *pInteractable);
void interactable_SetLastDoorVariables(int iPartitionIdx, GameInteractable *pInteractable, WorldVariable **eaVars);
InteractionLootTracker *interactable_GetLootTracker(int iPartitionIdx, GameInteractable *pInteractable, bool bCreate);
InteractionLootTracker **interactable_GetLootTrackerAddress(int iPartitionIdx, GameInteractable *pInteractable);
void interactable_ClearLootTracker(int iPartitionIdx, GameInteractable *pInteractable);

void interactable_ChangeGateOpenState(int iPartitionIdx, GameInteractable *pInteractable, SA_PARAM_OP_VALID Entity *pPlayerEnt, bool bNewState);
bool interactable_HasGateProperties(WorldNamedInteractable *pWorldInteractable, WorldGateInteractionProperties **ppProperties);
bool interactable_IsGateOpen(int iPartitionIdx, GameInteractable *pInteractable);
int interactable_WasGateOpeningState(int iPartitionIdx, GameInteractable *pInteractable);
void interactable_SetGateWasOpeningState(int iPartitionIdx, GameInteractable *pInteractable, int bGateWasOpening);

typedef int (*VisitGameInteractable)(GameInteractable *p, void *pClientPtr);
bool interactable_ExecuteOnEachInteractable(VisitGameInteractable func, void *pClientPtr);

void interactable_Activate(Entity *pPlayerEnt, GameInteractable *pInteractable, int iIndex);

void interactable_ClearPlayerInteractableTrackingData(Entity *pPlayerEnt);
void interactable_ClearAllPlayerInteractableTrackingData();
void interactable_ClearPlayerRecentClickableData(Entity *pPlayerEnt);

void interactable_OncePerFrame(F32 fTimeStep);

// Map load and unload notification
void interactable_PartitionLoad(int iPartitionIdx);
void interactable_PartitionLoadLate(int iPartitionIdx);
void interactable_PartitionUnload(int iPartitionIdx);

void interactable_MapLoad(ZoneMap *pZoneMap);
void interactable_MapLoadLate(ZoneMap *pZoneMap);
void interactable_MapUnload(void);
void interactable_MapValidate(void);

void interactable_ResetInteractables(void);
bool interactable_ApplyInteractableOverride(const char *pcMissionName, const char *pcFilename, const char *pcInteractableName, const char *pcTagNamePooled, WorldInteractionPropertyEntry *pOverride);
void interactable_RemoveInteractableOverridesFromMission(const char *pcMissionName);
void interactable_InitOverridesMatchingName(const char *pcName);
void interactable_ValidateOverrides(void);
bool interactable_AreInteractablesLoaded(void);
bool interactable_HasVolumeTriggeredGates(void);
void interactable_ScanOverrideCounts(void);

void interactable_ComputeMaxInteractRange( ZoneMap *pZoneMap );

U32 interactable_GetMaxInteractRange(void);

U32 interactable_GetMaxTargetRange(void);

const char *interactable_GetDetailTexture(GameInteractable *pInteractable);
void interactable_CheckForRelevantTooltipInfo(GameInteractable* pInteractable, const char** ppchConditionInfoOut);
bool interactable_EvaluateVisibilityForEntity(Entity* ent, GameInteractable* pInteractable);

AUTO_STRUCT;
typedef struct FoundDoorStruct
{
	char *pDoorName; //where the door leads, usually
	Vec3 vPos;
} FoundDoorStruct;

void interactable_FindAllDoors(FoundDoorStruct ***pppDoors,Entity * pUsePlayerEnt, bool includeWarpActions);

// Finds all the nodes which have an entry of the specified category
void interactable_FindAllInteractablesOfCategory(GameInteractable ***peaInteractables, const char* pchCategory);

// Special search for finding ambient jobs within a sphere
void interactable_FindAmbientJobInteractables(Vec3 vPos, F32 fRadius, GameInteractable ***pppAmbientJobInteractables);

// Special search for finding ambient jobs within a sphere
void interactable_FindCombatJobInteractables(Vec3 vPos, F32 fRadius, GameInteractable ***pppCombatJobInteractables);

// gets the entire list of ambient job interactables
GameInteractable** interactable_GetAmbientJobInteractables(void);

// gets the entire list of combat job interactables
GameInteractable** interactable_GetCombatJobInteractables(void);

// Gather Beacon positions
void interactable_GatherBeaconPositions(void);

// Verify an ambient job exists (Ex: if you want to write to it)
bool interactable_AmbientJobExists(GameInteractable *pGameInteractable, GameInteractLocation *pGameInteractLocation);

// Verify a combat job exists (Ex: if you want to write to it)
bool interactable_CombatJobExists(GameInteractable *pGameInteractable, GameInteractLocation *pGameInteractLocation);
