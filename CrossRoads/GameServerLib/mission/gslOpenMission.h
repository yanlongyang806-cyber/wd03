/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

#include "mission_enums.h"

typedef struct GameEvent GameEvent;
typedef struct Mission Mission;
typedef struct MissionDef MissionDef;
typedef struct MissionInfo MissionInfo;
typedef struct OpenMission OpenMission;


typedef struct OpenMissionPartitionState
{
	int iPartitionIdx;

	OpenMission **eaOpenMissions;

	StashTable pOpenMissionTable;
} OpenMissionPartitionState;


// Open mission event tracking functions
void openmission_OpenMissionScoreEventCB(Mission *pMission, GameEvent *pSubscribeEvent, GameEvent *pSpecificEvent, int iValue);
void openmission_StopTrackingAllEvents(void);
void openmission_StartTrackingAll(void);
void openmission_StopTrackingAllForName(const char *pcMissionName);
void openmission_StartTrackingForName(const char *pcMissionName);

// Open mission lifecycle
void openmission_BeginOpenMission(int iPartitionIdx, MissionDef *pDef, bool bSendNotification);
void openmission_EndOpenMission(int iPartitionIdx,MissionDef *pDef);
void openmission_ResetOpenMissions(int iPartitionIdx);
void openmission_RefreshOpenMissions(int iPartitionIdx);

// Partition lifecycle
void openmission_PartitionLoad(int iPartitionIdx);
void openmission_PartitionUnload(int iPartitionIdx);
void openmission_MapLoad(bool bFullInit);
void openmission_MapUnload(void);

// Scoreboard Access
void openmission_GetScoreboardEnts(int iPartitionIdx, Mission *pMission, Entity*** peaPlayerEnts);

// Access functions
OpenMission *openmission_GetFromName(int iPartitionIdx, const char *pcMissionName);
bool openmission_HasMissionInStateByTag(int iPartitionIdx, S32 eTag, MissionState eState);
Mission *openmission_FindMissionFromRefString(int iPartitionIdx, const char *pcRefString);
void openmission_SetParticipants(OpenMission *pOpenMission);
bool openmission_DidEntParticipateInOpenMission(int iPartitionIdx, const char *pcMissionName, Entity *pEnt);

// Processing
void openmission_ProcessOpenMissionsForPlayer(int iPartitionIdx, Entity *pPlayerEnt, MissionInfo* info);
void openmission_OncePerFrame(F32 fTimeStep);

