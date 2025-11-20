/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

typedef struct Entity Entity;
typedef struct Mission Mission;
typedef struct MissionDef MissionDef;
typedef struct MissionInfo MissionInfo;
typedef struct MissionRequest MissionRequest;


typedef struct MissionRequestFindItemData
{
	const char *pcPooledMissionName;
	bool bFound;
} MissionRequestFindItemData;


// Returns TRUE if a Mission Request exists for the given MissionDef, and the mission should be offered
bool missionrequest_MissionRequestIsOpen(Entity *pEnt, MissionDef *pDef);

// Returns TRUE if the mission requested by the Mission Request should be offered
bool missionrequest_IsRequestOpen(Entity *pEnt, MissionRequest *pRequest);

// Returns TRUE if the mission has made any requests
bool missionrequest_HasRequestsRecursive(MissionInfo *pInfo, Mission *pMission, Mission *pRootMission);

bool missionrequest_Update(int iPartitionIdx, MissionInfo *pInfo, MissionRequest *pRequest);
