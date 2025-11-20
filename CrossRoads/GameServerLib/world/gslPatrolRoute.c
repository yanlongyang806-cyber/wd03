/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "beacon.h"
#include "Expression.h"
#include "gslPatrolRoute.h"
#include "StringCache.h"
#include "wlEncounter.h"
#include "WorldGrid.h"

#ifndef SDANGELO_REMOVE_PATROL_ROUTE
#include "oldencounter_common.h"
#include "error.h"
#endif


// ----------------------------------------------------------------------------------
// Static Data
// ----------------------------------------------------------------------------------

static GamePatrolRoute **s_eaPatrolRoutes = NULL;

bool g_EnablePatrolValidation = false;


// ----------------------------------------------------------------------------------
// Sole interfaces to searching s_eaPatrolRoutes.  No other function
// than patrolroute_GetByName should be searching s_eaPatrolRoutes.
// ----------------------------------------------------------------------------------
GamePatrolRoute *patrolroute_GetByEntry(WorldPatrolRoute *pWorldRoute)
{
	int i;

	for(i=eaSize(&s_eaPatrolRoutes)-1; i>=0; --i) {
		if (SAFE_MEMBER(s_eaPatrolRoutes[i], pWorldRoute) == pWorldRoute) {
			return s_eaPatrolRoutes[i];
		}
	}

	return NULL;
}


GamePatrolRoute *patrolroute_GetByName(const char *pcRouteName, const WorldScope *pScope)
{
	if (pScope && gUseScopedExpr) {
		WorldEncounterObject *pObject = worldScopeGetObject(pScope, pcRouteName);

		if (pObject && pObject->type == WL_ENC_PATROL_ROUTE) {
			WorldPatrolRoute *pWorldRoute = (WorldPatrolRoute *)pObject;
			GamePatrolRoute *pPatrolRoute = patrolroute_GetByEntry(pWorldRoute);
			if (pPatrolRoute) {
				return pPatrolRoute;
			}
		}
	} else {
		int i;
		for(i=eaSize(&s_eaPatrolRoutes)-1; i>=0; --i) {
			if (stricmp(pcRouteName, s_eaPatrolRoutes[i]->pcName) == 0) {
				return s_eaPatrolRoutes[i];
			}
		}
	}

	return NULL;
}


#define FOR_EACH_PATROL_ROUTE(it) { int i##it##Index; for(i##it##Index=eaSize(&s_eaPatrolRoutes)-1; i##it##Index>=0; --i##it##Index) { GamePatrolRoute *it = s_eaPatrolRoutes[i##it##Index];
#define FOR_EACH_PATROL_ROUTE2(outerIt, it) { int i##it##Index; for(i##it##Index=i##outerIt##Index-1; i##it##Index>=0; --i##it##Index) { GamePatrolRoute *it = s_eaPatrolRoutes[i##it##Index];
// ----------------------------------------------------------------------------------
// End of sole interfaces to searching s_eaPatrolRoutes.
// ----------------------------------------------------------------------------------

// ----------------------------------------------------------------------------------
// Access Functions
// ----------------------------------------------------------------------------------

WorldPatrolRouteType patrolroute_GetType(GamePatrolRoute *pPatrolRoute)
{
	if (!pPatrolRoute || !pPatrolRoute->pWorldRoute || !pPatrolRoute->pWorldRoute->properties) {
		return PATROL_PINGPONG;
	} else {
		return pPatrolRoute->pWorldRoute->properties->route_type;
	}
}


int patrolroute_GetNumPoints(GamePatrolRoute *pPatrolRoute)
{
	if (!pPatrolRoute || !pPatrolRoute->pWorldRoute || !pPatrolRoute->pWorldRoute->properties) {
		return 0;
	} else {
		return eaSize(&pPatrolRoute->pWorldRoute->properties->patrol_points);
	}
}


// This always offsets the location by 1 foot vertically
bool patrolroute_GetPointLocation(GamePatrolRoute *pPatrolRoute, int iIndex, Vec3 vLocation)
{
	if (!pPatrolRoute || !pPatrolRoute->pWorldRoute || !pPatrolRoute->pWorldRoute->properties || (iIndex >= eaSize(&pPatrolRoute->pWorldRoute->properties->patrol_points))) {
		zeroVec3(vLocation);
		return false;
	} else {
		copyVec3(pPatrolRoute->pWorldRoute->properties->patrol_points[iIndex]->pos, vLocation);
		vLocation[1] += 2.f;
		return true;
	}
}


// Beaconizer support
void patrolroute_GatherBeaconPositions(void)
{
	int i;

	FOR_EACH_PATROL_ROUTE(pRoute) {
		if (pRoute->pWorldRoute && pRoute->pWorldRoute->properties) {
			for(i=eaSize(&pRoute->pWorldRoute->properties->patrol_points)-1; i>=0; --i) {
				WorldPatrolPointProperties *pPoint = pRoute->pWorldRoute->properties->patrol_points[i];
				beaconAddUsefulPoint(pPoint->pos);
			}
		}
	} FOR_EACH_END;
}


// ----------------------------------------------------------------------------------
// Patrol Route List Logic
// ----------------------------------------------------------------------------------


static void patrolroute_Free(GamePatrolRoute *pPatrolRoute)
{
	if (pPatrolRoute->bEncLayerConverted) {
		StructDestroySafe(parse_WorldPatrolRoute, &pPatrolRoute->pWorldRoute);
	}
	free(pPatrolRoute);
}


static void patrolroute_AddRoute(const char *pcName, WorldPatrolRoute *pWorldRoute, bool bEncLayerConverted, const char *pchEncLayerFilename)
{
	GamePatrolRoute *pPatrolRoute = calloc(1,sizeof(GamePatrolRoute));
	if (pcName) {
		pPatrolRoute->pcName = allocAddString(pcName);
	}
	pPatrolRoute->pWorldRoute = pWorldRoute;
	pPatrolRoute->bEncLayerConverted = bEncLayerConverted;
	pPatrolRoute->pchEncLayerFilename = allocAddString(pchEncLayerFilename);
	eaPush(&s_eaPatrolRoutes, pPatrolRoute);
}


static void patrolroute_ClearList(void)
{
	eaDestroyEx(&s_eaPatrolRoutes, patrolroute_Free);
}


// Check if a patrol route exists
bool patrolroute_PatrolRouteExists(const char *pcName, const WorldScope *pScope)
{
	return patrolroute_GetByName(pcName, pScope) != NULL;
}


// ----------------------------------------------------------------------------------
// Map Load Logic
// ----------------------------------------------------------------------------------

static const char* patrolroute_GetFilename(GamePatrolRoute *pRoute)
{
	if (pRoute){
		if (pRoute->bEncLayerConverted){
			return pRoute->pchEncLayerFilename;
		} else {
			return layerGetFilename(pRoute->pWorldRoute->common_data.layer);
		}
	}
	return NULL;
}


void patrolroute_MapValidate(ZoneMap *pZoneMap)
{
	FOR_EACH_PATROL_ROUTE(pRoute){
		if (g_EnablePatrolValidation){
			int i;
			// Check that all patrol route points are in valid locations
			for (i = patrolroute_GetNumPoints(pRoute)-1; i>=0; --i){
				WorldPatrolPointProperties *pPoint = pRoute->pWorldRoute->properties->patrol_points[i];
				Vec3 pointLoc = {0};

				patrolroute_GetPointLocation(pRoute, i, pointLoc);

				// Check whether this position is valid
				if (!beaconIsPositionValid(worldGetAnyCollPartitionIdx(), pointLoc, NULL)){
					const char *pchFilename = patrolroute_GetFilename(pRoute);
					ErrorFilenamef(pchFilename, "Patrol Route %s: Patrol Point at <%f, %f, %f> appears to be unreachable!", pRoute->pcName, pPoint->pos[0], pPoint->pos[1], pPoint->pos[2]);
				}
			}
		}
	} FOR_EACH_END;
}


void patrolroute_MapLoad(ZoneMap *pZoneMap)
{
	WorldZoneMapScope *pScope;
	int i;

	// Clear all data
	patrolroute_ClearList();

	// Get zone map scope
	pScope = zmapGetScope(pZoneMap);

	// Find all patrol routes in all scopes
	if(pScope) {
		for(i=eaSize(&pScope->patrol_routes)-1; i>=0; --i) {
			const char *pcName = worldScopeGetObjectName(&pScope->scope, &pScope->patrol_routes[i]->common_data);
			patrolroute_AddRoute(pcName, pScope->patrol_routes[i], false, NULL);
		}
	}

#ifndef SDANGELO_REMOVE_PATROL_ROUTE
	// This is backward data compatibility code
	if (g_EncounterMasterLayer) {
		int j,k;
		bool bHasGeoRoutes = eaSize(&s_eaPatrolRoutes);

		// Find all patrol routes on encounter layers
		for(i=eaSize(&g_EncounterMasterLayer->encLayers)-1; i>=0; --i) {
			EncounterLayer *pLayer = g_EncounterMasterLayer->encLayers[i];
			bool bComplained = false;

			for(j=eaSize(&pLayer->oldNamedRoutes)-1; j>=0; --j) {
				OldPatrolRoute *pRoute = pLayer->oldNamedRoutes[j];
				WorldPatrolRoute *pWorldRoute;
				if (!bComplained && bHasGeoRoutes) {
					ErrorFilenamef(pLayer->pchFilename, "Found encounter layer defined patrol route on a map that also contains geo layer patrol routes.  This is not legal.");
				}
				if (!bComplained && !gConf.bAllowOldPatrolData) {
					ErrorFilenamef(pLayer->pchFilename, "Found encounter layer defined patrol route on a map.  This game does not allow patrol routes on encounter layers.");
				}

				// Create the world route from the Old structure
				pWorldRoute = StructCreate(parse_WorldPatrolRoute);
				assert(pWorldRoute);
				pWorldRoute->properties = StructCreate(parse_WorldPatrolProperties);
				assert(pWorldRoute->properties);

				switch(pRoute->routeType)
				{
					xcase OldPatrolRouteType_Circle:   pWorldRoute->properties->route_type = PATROL_CIRCLE;
					xcase OldPatrolRouteType_PingPong: pWorldRoute->properties->route_type = PATROL_PINGPONG;
					xcase OldPatrolRouteType_OneWay:   pWorldRoute->properties->route_type = PATROL_ONEWAY;
				}

				for(k=0; k<eaSize(&pRoute->patrolPoints); ++k) {
					WorldPatrolPointProperties *pPoint = StructCreate(parse_WorldPatrolPointProperties);
					copyVec3(pRoute->patrolPoints[k]->pointLoc, pPoint->pos);
					eaPush(&pWorldRoute->properties->patrol_points, pPoint);
				}
				
				// Add the route to data tracking
				patrolroute_AddRoute(pRoute->routeName, pWorldRoute, true, pLayer->pchFilename);
			}
		}
	}
#endif
}


void patrolroute_MapUnload(void)
{
	patrolroute_ClearList();
}


void patrolroute_ResetPatrols(void)
{
	patrolroute_MapUnload();
	patrolroute_MapLoad(worldGetActiveMap());
}



