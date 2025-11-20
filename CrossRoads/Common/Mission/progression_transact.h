/***************************************************************************
*     Copyright (c) 2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

typedef struct NOCONST(Entity) NOCONST(Entity);
typedef struct MissionDef MissionDef;
typedef struct GameProgressionNodeDef GameProgressionNodeDef;
typedef struct ProgressionUpdateParams ProgressionUpdateParams;
typedef enum enumTransactionOutcome enumTransactionOutcome;

bool progression_trh_IsValidStoryArcForPlayer(ATH_ARG NOCONST(Entity)* pEnt, SA_PARAM_OP_VALID GameProgressionNodeDef* pNodeDef);
#define progression_IsValidStoryArcForPlayer(pEnt, pNodeDef) progression_trh_IsValidStoryArcForPlayer(CONTAINER_NOCONST(Entity, pEnt), pNodeDef)

bool progression_trh_StoryWindBackCheck(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, GameProgressionNodeDef* pNodeJustCompleted);

bool progression_trh_IsMissionCompleteForNode(ATH_ARG NOCONST(Entity)* pEnt, const char* pchMissionName, GameProgressionNodeDef* pNodeDef);
#define progression_IsMissionCompleteForNode(pEnt, pchMissionName, pNodeDef) progression_trh_IsMissionCompleteForNode(CONTAINER_NOCONST(Entity, pEnt), pchMissionName, pNodeDef)

void progression_trh_UpdateCompletedMissionForTeam(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, MissionDef* pCompletedMissionDef);
void progression_trh_UpdateCompletedMissionForPlayer(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, MissionDef* pCompletedMissionDef);

bool progression_trh_AdvanceStoryProgress(ATR_ARGS, ATH_ARG NOCONST(Entity)* ent, S32 eStoryArc, bool bUpdate);

int progression_trh_PlayerFindWindBackMission(ATH_ARG NOCONST(Entity)* pEnt, const char* pchMissionName);
#define progression_PlayerFindWindBackMission(pEnt, pchMissionName) progression_trh_PlayerFindWindBackMission(CONTAINER_NOCONST(Entity, pEnt), pchMissionName)

bool progression_trh_ProgressionNodeCompleted(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, GameProgressionNodeDef* pNodeDef);
#define progression_ProgressionNodeCompleted(pEnt, pNodeDef) progression_trh_ProgressionNodeCompleted(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, pEnt), pNodeDef)

bool progression_trh_ProgressionNodeUnlocked(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, GameProgressionNodeDef* pNodeDef);
#define progression_ProgressionNodeUnlocked(pEnt, pNodeDef) progression_trh_ProgressionNodeUnlocked(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, pEnt), pNodeDef)

enumTransactionOutcome progression_trh_SetProgression(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ProgressionUpdateParams *pUpdateParams);

enumTransactionOutcome progression_trh_ExecutePostMissionAcceptTasks(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, MissionDef *pMissionDef);