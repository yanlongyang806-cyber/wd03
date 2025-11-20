/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

typedef struct Entity Entity;
typedef struct MinimapWaypoint MinimapWaypoint;
typedef struct Mission Mission;
typedef struct MissionInfo MissionInfo;


// Puts a list of waypoints for this mission into waypointList
void waypoint_GetMissionWaypoints(MissionInfo *pInfo, Mission *pMission, MinimapWaypoint ***peaWaypointList, bool bRecursive);

// Flag's a Mission Waypoints as dirty, so they will refresh on the next tick
void waypoint_FlagWaypointRefresh(MissionInfo *pInfo);
void waypoint_FlagWaypointRefreshAllPlayers(void);

void waypoint_UpdateLandmarkWaypoints(Entity *pPlayerEnt);
void waypoint_UpdateTrackedContactWaypoints(Entity *pPlayerEnt);
void waypoint_UpdateTeamCorralWaypoints(Entity *pPlayerEnt);

void waypoint_DestroyMinimapWaypoint(MinimapWaypoint *pWaypoint);

void waypoint_UpdateMissionWaypoints(MissionInfo *pInfo, Mission *pMission);
void waypoint_ClearWaypoints(MissionInfo *pInfo, Mission *pMission, bool bRecursive);
void waypoint_UpdateAllMissionWaypoints(int iPartitionIdx, MissionInfo *pInfo);

bool waypoint_ClampWaypointToRegion(Entity *pEnt, MinimapWaypoint *pWaypoint);