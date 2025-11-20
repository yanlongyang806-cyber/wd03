#ifndef _DOOR_TRANSITION_H_
#define _DOOR_TRANSITION_H_
#pragma once
GCC_SYSTEM

#include "gslEntity.h"
#include "gslMapTransfer.h"

typedef struct DoorTransitionAnimation DoorTransitionAnimation;
typedef struct DoorTransitionSequenceDef DoorTransitionSequenceDef;
typedef struct MapDescription MapDescription;
typedef struct RegionRules RegionRules;

// Set entity flags for transition sequences
void gslEntitySetTransitionSequenceFlags(Entity* ent, S32 iEntFlags, bool bIncludePets);
void gslEntityClearTransitionSequenceFlags(Entity* ent, S32 iEntFlags, bool bIncludePets);

// Play an AnimList for an entity and the entity's pets
void gslEntityPlayAnimationList(Entity* e, DoorTransitionAnimation* pAnim, UserDataCallback pCallback, void* pCallbackData);

// Arrival Transition Sequences
void gslEntityPlaySpawnTransitionSequence(Entity* e, bool bDebug);
void gslHandleDoorTransitionSequenceSetup(Entity* e);

// Departure Transition Sequences
bool gslEntity_FindBestMapExitDirection(int iPartitionIdx, Entity* e, Vec3 vDir, F32* pfAngle, bool* pbMatchesFacing);
void gslEntityPlayTransitionSequenceThenMapMove(Entity *pEnt, MapDescription* pMapDesc, RegionRules* pCurrRules, RegionRules* pNextRules, DoorTransitionSequenceDef* pTransitionOverride, TransferFlags eFlags);
void gslEntityPlayTransitionSequenceThenMapMoveEx(Entity *pEnt, const char* pcMap, ZoneMapType eMapType, const char* pcSpawn, int iMapIndex, ContainerID uMapContainerID, U32 uPartitionID, GlobalType eOwnerType, ContainerID uOwnerID, const char* pchMapVars, RegionRules* pCurrRules, RegionRules* pNextRules, DoorTransitionSequenceDef* pTransOverride, TransferFlags eFlags);

#endif // _DOOR_TRANSITION_H_