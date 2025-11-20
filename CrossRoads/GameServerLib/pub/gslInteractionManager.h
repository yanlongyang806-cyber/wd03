/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#ifndef INTERACTIONMANAGER_H__
#define INTERACTIONMANAGER_H__

#include "GlobalTypeEnum.h"

typedef struct Entity			Entity;
typedef struct WorldInteractionNode WorldInteractionNode;
typedef struct WorldInteractionEntry WorldInteractionEntry;
typedef struct WorldInteractionPropertyEntry WorldInteractionPropertyEntry;
typedef struct Character			Character;
typedef struct Character		Character;
typedef struct CharacterAttribs		CharacterAttribs;
typedef struct GameInteractable GameInteractable;
typedef struct GameNamedVolume GameNamedVolume;
typedef struct SingleRespawnTimer SingleRespawnTimer;

extern bool g_bInteractionDebug;

Entity *im_InteractionNodeToEntity(int iPartitionIdx, SA_PARAM_NN_VALID WorldInteractionNode *pNode);
void im_ForceNodesToEntities(Entity* pSource, F32 fRange, const char* pchName, Entity ***entsOut);
void im_EntityDestroyed(Entity *pEnt);
void im_InteractionTimerTick(F32 fRate);
Entity *im_FindCritterforObject(int iPartitionIdx, const char *pchName);

LATELINK; 
bool im_EntityCleanupCheck(Entity *pEnt);

LATELINK;
bool im_EntityInCombatCheck(Entity *pEnt);

void im_onDeath(Entity *pEnt);

void im_ForceEndMotionForNode(int iPartitionIdx, WorldInteractionNode *node, bool bCleanupOnly);
void im_HandleNodeDestroy(WorldInteractionNode *node);
bool im_Interact(int iPartitionIdx, GameInteractable *pInteractable, Entity *pTargetEnt, GameNamedVolume *pVolume, WorldInteractionPropertyEntry *pEntry, int iIndex, Entity* pPlayerEnt);
void im_EndInteract(int iPartitionIdx, GameInteractable *pInteractable, Entity *pEntTarget, GameNamedVolume *pVolume, WorldInteractionPropertyEntry *pEntry, int iIndex, Entity *pPlayerEnt, bool bInterrupt);
void im_MotionTrackerTick(F32 fRate);

void im_DestroyChainedDestructibles( Entity* pEnt, WorldInteractionNode* pNode, EntityRef erKiller );

void im_InteractDestroyPathing(Entity *e);

bool im_InteractBeginPathing(Entity *pPlayerEnt, GameInteractable *pInteractable, Entity *pTargetEnt, WorldInteractionPropertyEntry *pEntry, int iIndex, GlobalType eTeammateType, ContainerID uTeammateID);

void im_RemoveNodeFromRespawnTimers(int iPartitionIdx, WorldInteractionNode *pNode);
void im_RemoveNodeFromEntityTimers(int iPartitionIdx, WorldInteractionNode *pNode);

U32 im_FindAllEntsWithName(int iPartitionIdx, const char* pchObjectName);

void interactionManager_PartitionUnload(int iPartitionIdx);

#endif
