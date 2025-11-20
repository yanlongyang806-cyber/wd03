/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef CUTSCENE_H
#define CUTSCENE_H

#include "GlobalTypeEnum.h"

typedef struct CutsceneDef CutsceneDef;
typedef struct Entity Entity;
typedef U32 EntityRef;
typedef struct WorldVariable WorldVariable;
typedef struct MapDescription MapDescription;
typedef struct CutsceneWorldVars CutsceneWorldVars;

typedef void (*ActiveCutsceneCallback)(void *userdata);

AUTO_STRUCT;
typedef struct ActiveCutsceneAction
{
	ActiveCutsceneCallback	pCallback;		NO_AST
	void*					pCallbackData;	NO_AST
	F32						fActivateTime;
	EntityRef				erOwner;
} ActiveCutsceneAction;

AUTO_STRUCT;
typedef struct ActiveCutscene
{
	// Cutscene def
	CutsceneDef* pDef;

	// Partition index of this cutscene.
	int iPartitionIdx;

	// Earray of players watching this cutscene.
	EntityRef* pPlayerRefs;

	// list of ents made untargetable. 
	// note: if an entity was already untargetable, it does not get set to untargetable
	EntityRef* piUntargetableEnts;

	//Map to move to after the cutscene finishes
	MapDescription* pMapDescription;

	//The transfer flags to use after the cutscene finishes
	U32 eTransferFlags;
	
	//Cutscene actions
	ActiveCutsceneAction** eaActions;

	CutsceneWorldVars *pCutsceneVars;

	S64 startTime;
	S64 endTime;
	F32 fElapsedTime;

	Vec3 estimatedCameraPos;
} ActiveCutscene;

// End any cutscenes that have been running for too long
void cutscene_UpdateActiveCutscenes(F32 fTimeStep);

// Add and remove a player to the list of players who are watching cutscenes
void cutscene_GetPlayersInCutscenes(SA_PARAM_NN_VALID Entity*** playerEnts, int iPartitionIdx);
void cutscene_GetPlayersInCutscenesNearCritter(Entity *pCritterEnt, Entity*** playerEnts);

bool cutscene_StartOnServer(SA_PARAM_NN_VALID CutsceneDef* pCutscene, SA_PARAM_OP_VALID Entity* pEnt, bool bIsArrivalTransition);
bool cutscene_StartOnServerEx(CutsceneDef* pCutscene, Entity* pEnt, int iPartitionIdx, MapDescription* pMapDesc, U32 eTransferFlags, bool bIsArrivalTransition);

void cutscene_PlayerSkipCutscene(Entity* pEnt, bool bForce);

// Returns true if there are cutscenes within dist of centerPos
bool cutscene_GetNearbyCutscenes(int iPartitionIdx, Vec3 centerPos, F32 dist);

ActiveCutscene* cutscene_FindActiveCutsceneByName(SA_PARAM_NN_STR const char* cutsceneName, int iPartitionIdx);
ActiveCutscene* cutscene_ActiveCutsceneFromPlayer(Entity *pPlayerEnt);

void cutscene_PlayerEnteredMap(Entity *pEnt);
void cutscene_PlayerDoneLoading(Entity *pEnt);
void cutscene_PlayerLeftMap(Entity *pEnt);

void cutscene_MapValidate(void);

void cutscene_OncePerFrame(F32 fTimeStep);

bool cutscene_CreateAction(Entity* pEnt, ActiveCutscene* pActiveCutscene, ActiveCutsceneCallback pCallback, void* pCallbackData, F32 fActivateTime);

void cutscene_PartitionLoad(int iPartitionIdx);
void cutscene_PartitionUnload(int iPartitionIdx);
void cutscene_MapLoad();
void cutscene_MapUnload();


F32 cutscene_GetActiveHighSendRange(void);

#endif
