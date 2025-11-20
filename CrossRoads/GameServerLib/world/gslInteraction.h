/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

#include "EntityInteraction.h"

typedef struct Critter Critter;
typedef struct CritterInteractInfo CritterInteractInfo;
typedef struct Entity Entity;
typedef struct Expression Expression;
typedef struct GameEncounter GameEncounter;
typedef struct GameInteractable GameInteractable;
typedef struct GameNamedVolume GameNamedVolume;
typedef struct OldInteractionProperties OldInteractionProperties;
typedef struct WorldDoorInteractionProperties WorldDoorInteractionProperties;
typedef struct WorldInteractionNode WorldInteractionNode;
typedef struct WorldInteractionPropertyEntry WorldInteractionPropertyEntry;


typedef enum InteractedState
{
	InteractedState_Active,
	InteractedState_Cooldown,
	InteractedState_NoRespawn,
} InteractedState;

// The state of an object with a cooldown
typedef struct InteractedObjectState
{
	InteractTarget target;
	EntityRef playerEntRef;
	U32 uiTeamID;
	InteractedState state;
	bool bRespawn;		// Whether this object should respawn when done, or stay forever "busy"
	bool bTeamUsableWhenActive;
	F32 fActiveTimeRemaining;
	F32 fCooldownTimeRemaining;
	F32 fCooldownMultiplier; // for dynamic cooldowns

	bool bForceReset;	// Tells interactable timer tick to force this to reset
} InteractedObjectState;

void interaction_PartitionLoad(int iPartitionIdx);
void interaction_PartitionUnload(int iPartitionIdx);

bool interaction_IsLootEntity(Entity *pCritterEnt);
bool interaction_IsLootEntityOwned(Entity *pCritterEnt, Entity *pPlayerEnt);

bool interaction_IsCritterInteractable(Entity *pPlayerEnt, Entity *pCritterEnt);
void interaction_GetCritterContacts(Entity *pPlayerEnt, Entity *pCritterEnt, bool bTestRange, ContactDef ***peaContacts, CritterInteractInfo ***peaInteractCritters);

void interaction_AddInteractedObject(int iPartitionIdx, const InteractTarget *pTarget, EntityRef playerEntRef, U32 uiTeamID, F32 fActiveTime, F32 fCooldownTime, bool bNoRespawn, bool bStartInCooldown, bool bTeamUsableWhenActive);
void interaction_RemoveInteractedObject(int iPartitionIdx, const InteractTarget *pTarget);
void interaction_ResetInteractedNode(int iPartitionIdx, const WorldInteractionNode *pNode);
void interaction_ClearInteractedList(void);

void interaction_RecordQueueDefInteraction(Entity *pEnt, const char *pchQueueName);

bool interaction_IsInteractTargetBusy2(int iPartitionIdx, const Entity *pPlayerEnt, EntityRef entRef, WorldInteractionNode *pNode, const char *pcVolumeNamePooled, int iIndex);
bool interaction_IsNodeOnCooldown(int iPartitionIdx, WorldInteractionNode *pNode);
bool interaction_IsExclusive(WorldInteractionPropertyEntry *pEntry, GameInteractable *pInteractable, Entity *pTargetEnt);
const OldInteractionProperties *interaction_GetPropsFromInteractTarget(const InteractTarget *pTarget, Entity *pPlayerEnt);
SA_RET_OP_VALID WorldInteractionPropertyEntry *interaction_GetPropertyEntryFromTarget(int iPartitionIdx, SA_PARAM_NN_VALID const InteractTarget *pTarget, SA_PARAM_OP_VALID GameInteractable **pInteractableTargetOut, SA_PARAM_OP_VALID Entity **pEntTargetOut, SA_PARAM_OP_VALID GameNamedVolume **pVolumeOut);
const InteractedObjectState* interaction_GetInteractedObjectFromNode(int iPartitionIdx, WorldInteractionNode *pNode);
bool interaction_GetValidInteractNodeOptions(GameInteractable *pInteractable, Entity *pPlayerEnt, InteractOption*** peaOptions, bool bRangeCheck, bool bExcludeDestructibles);

void interaction_CopyInteractTarget(InteractTarget *pDest, const InteractTarget *pSource);

bool interaction_DoorGetDestination(WorldDoorInteractionProperties *pDoorProps, Entity *pPlayerEnt, GlobalType eOwnerType, ContainerID uOwnerID, int iSeed, DoorTarget *pDoorTarget);

void interaction_SetInteractTarget(Entity *pPlayerEnt, WorldInteractionNode *pNodeTarget, Entity *pEntTarget, GameNamedVolume *pVolume, int iIndex, GlobalType eTeammateType, ContainerID uTeammateID);
void interaction_FinishPathing(Entity *pPlayerEnt, GameInteractable *pInteractable, Entity *pTargetEnt, GameNamedVolume *pVolume, WorldInteractionPropertyEntry *pEntry, int iIndex, GlobalType eTeammateType, ContainerID uTeammateID);
void interaction_StartInteracting(Entity *pPlayerEnt, WorldInteractionNode *pNodeTarget, Entity *pEntTarget, GameNamedVolume *pVolume, int iIndex, GlobalType eTeammateType, ContainerID uTeammateID, bool bBeginPathing);
void interaction_ProcessInteraction(Entity *pPlayerEnt, GameInteractable *pInteractable, Entity *pEntTarget, GameNamedVolume *pVolume, WorldInteractionPropertyEntry *pEntry);
void interaction_DoneInteracting(int iPartitionIdx, SA_PARAM_NN_VALID Entity *pPlayerEnt, bool bSucceeded, bool bInterrupt);
void interaction_EndInteractionAndDialog(int iPartitionIdx, SA_PARAM_NN_VALID Entity *pPlayerEnt, bool bSucceeded, bool bInterrupt, bool bPersistCleanup);
void interaction_EndInteraction(int iPartitionIdx, SA_PARAM_NN_VALID Entity *pPlayerEnt, bool bSucceeded, bool bInterrupt, bool bPersistCleanup);

int interactable_EvaluateExpr(int iPartitionIdx, GameInteractable *pInteractable, Entity *pPlayerEnt, Expression *pExpression);
int interactable_EvaluateNonPlayerExpr(int iPartitionIdx, GameInteractable *pInteractable, Expression *pExpression);
bool interaction_AddOptionToInteract(int iPartitionIdx, InteractOption ***peaOptionList, WorldInteractionPropertyEntry *pEntry, GameInteractable *pInteractable, WorldInteractionNode *pNode, Entity *pCritterEnt, GameNamedVolume *pVolume, int iIndex, Entity *pPlayerEnt, bool bCanPickup);

int interaction_GetNumActorAndCritterEntries(GameEncounter* pEncounter, int iActorIndex, Critter* pCritter);
WorldInteractionPropertyEntry *interaction_GetActorOrCritterEntry(GameEncounter* pEncounter, int iActorIndex, Critter* pCritter, int iInteractIndex);

// Tick functions
void interaction_OncePerFrameScanTick(Entity *pEnt);
void interaction_OncePerFrameInteractTick(Entity *pEnt, F32 elapsed);
void interaction_OncePerFrameTimerUpdate(F32 fTimeStep, bool bForce);

bool interaction_FindInteractionEnts(Entity* e, Entity ***pppEntsOut);

bool interaction_PerformInteract(Entity *pPlayerEnt, EntityRef entRef, const char *pchNodeKey, const char *pcVolumeName, int iIndex, int eTeammateType, U32 uTeammateID, bool bForced);


void interaction_ContinueInteraction(GameInteractable *pInteractable, Entity *pEntTarget, GameNamedVolume *pVolume, int iIndex, GlobalType eTeammateType, ContainerID uTeammateID, Entity *pPlayerEnt);

LATELINK;
bool GameSpecific_InteractableGrantReward(Entity* pPlayerEnt, GameInteractable* pInteractable, WorldInteractionPropertyEntry* pEntry);

void interaction_SetCooldownForPlayer(Entity* pPlayerEnt, GameInteractable *pLootInteractable, int idxEntry, int iPartitionIdx);
void interaction_LootResolved(GameInteractable *pLootInteractable, int idxEntry, int iPartitionIdx);

int interaction_FillLootBag(Entity *pPlayerEnt, InventoryBag ***peaBags, WorldInteractionPropertyEntry *pEntry);
