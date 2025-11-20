/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

#include "WorldLibEnums.h"

typedef struct WorldScope WorldScope;
typedef struct WorldPatrolRoute WorldPatrolRoute;
typedef struct ZoneMap ZoneMap;

typedef struct GamePatrolRoute
{
	// The patrol route's map-level name
	const char *pcName;

	// The world patrol route
	WorldPatrolRoute *pWorldRoute;

#ifndef SDANGELO_REMOVE_PATROL_ROUTE
	// Fake entry if from encounter layer backward compatibility
	bool bEncLayerConverted;
	const char *pchEncLayerFilename;  // POOL_STRING
#endif
} GamePatrolRoute;

// Gets a patrol route, if one exists
SA_RET_OP_VALID GamePatrolRoute *patrolroute_GetByName(SA_PARAM_NN_STR const char *pcRouteName, SA_PARAM_OP_VALID const WorldScope *pScope);

// Check if a patrol route exists
bool patrolroute_PatrolRouteExists(const char *pcName, const WorldScope *pScope);

// Access functions
WorldPatrolRouteType patrolroute_GetType(GamePatrolRoute *pPatrolRoute);
int patrolroute_GetNumPoints(GamePatrolRoute *pPatrolRoute);
bool patrolroute_GetPointLocation(GamePatrolRoute *pPatrolRoute, int iIndex, Vec3 vLocation);

// Beaconizer support
void patrolroute_GatherBeaconPositions(void);

// Called on map load and unload
void patrolroute_MapLoad(ZoneMap *pZoneMap);
void patrolroute_MapUnload(void);
void patrolroute_MapValidate(ZoneMap *pZoneMap);
void patrolroute_ResetPatrols(void);

