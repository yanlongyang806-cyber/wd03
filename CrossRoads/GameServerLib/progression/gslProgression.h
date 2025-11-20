/***************************************************************************
*     Copyright (c) 2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

#include "referencesystem.h"

typedef struct Entity Entity;
typedef struct MissionDef MissionDef;
typedef struct GameProgressionNodeDef GameProgressionNodeDef;

AUTO_STRUCT;
typedef struct SetProgressionByMissionCallbackParams
{
	U32 iEntContainerID;	
	REF_TO(GameProgressionNodeDef) hNode;
	bool bWarpToSpawnPoint;
	bool bGrantMission;
} SetProgressionByMissionCallbackParams;

// Evaluate a progression expression
bool gslProgression_EvalExpression(int iPartitionIdx, Expression* pExpr, Entity *pEnt);

void gslProgression_OncePerFrame(F32 fTimeStep);

// Adds a map to the list of tracked progression maps allowed for players
void gslProgression_AddTrackedProgressionMap(const char* pchAllowedMapName);

// Indicates whether the player is allowed to be on the given map based on player's progression
bool gslProgression_PlayerIsAllowedOnMap(SA_PARAM_NN_VALID Entity *pEnt, SA_PARAM_NN_STR const char *pchMapDescription);

// Update the current non-persisted progression information for the player
void gslProgression_UpdateCurrentProgression(Entity* pEnt, MissionDef* pMissionDef);