/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "beacon.h"
#include "beaconPath.h"
#include "contact_common.h"
#include "Entity.h"
#include "EntityIterator.h"
#include "gslContact.h"
#include "gslEncounter.h"
#include "gslInteractable.h"
#include "gslLandmark.h"
#include "gslMission.h"
#include "gslNamedPoint.h"
#include "gslOpenMission.h"
#include "gslVolume.h"
#include "mission_common.h"
#include "oldencounter_common.h"
#include "WorldGrid.h"
#include "WorldLib.h"
#include "gslTeamCorral.h"
#include "gslEntity.h"

#include "mission_common_h_ast.h"
#include "gslInteractable_h_ast.h"

// ----------------------------------------------------------------------------------
// Waypoint Logic
// ----------------------------------------------------------------------------------


// If the waypoint is in a different region, sets the waypoint to a door to that region
// Returns false on failure (the waypoint is in a different region, but no path can be found)
bool waypoint_ClampWaypointToRegion(Entity *pEnt, MinimapWaypoint *pWaypoint, bool * pbClamped)
{
	if (pbClamped)
		*pbClamped = false;
	if (pEnt && pWaypoint) {
		WorldRegion *pWaypointRegion, *pEntRegion;
		Vec3 vEntPos;

		entGetPos(pEnt, vEntPos);
		pWaypointRegion = worldGetWorldRegionByPos(pWaypoint->pos);
		pEntRegion = worldGetWorldRegionByPos(vEntPos);

		if (pWaypointRegion != pEntRegion) {
			beaconSetPathFindEntity(entGetRef(pEnt), 0, entGetHeight(pEnt));
			if (beaconFindNextConnection(entGetPartitionIdx(pEnt), entGetRef(pEnt), vEntPos, pWaypoint->pos, pWaypoint->pos, pWaypoint->pchMissionRefString)){
				// TODO - Make this a Door waypoint
				if (pbClamped)
					*pbClamped = true;
				pWaypoint->fRotation = 0;
				pWaypoint->fXAxisRadius = 0;
				pWaypoint->fYAxisRadius = 0;
			} else {
				// This is in a different region, but we can't find a door
				return false;
			}
		}
	}
	return true;
}


static bool waypoint_GetContactWaypointsForMission(Entity *pEnt, MissionDef *pMissionDef, MinimapWaypoint ***peaWaypointList)
{
	int i, n;
	int iPartitionIdx; 
	bool bMadeWaypoint = false;

	PERFINFO_AUTO_START_FUNC();

	iPartitionIdx = entGetPartitionIdx(pEnt);

	n = eaSize(&g_ContactLocations);
	for (i = 0; i < n; i++) {
		ContactDef *pContact = RefSystem_ReferentFromString(g_ContactDictionary, g_ContactLocations[i]->pchContactDefName);
		if (pContact) {
			ContactMissionOffer **eaOfferList = NULL;
			int j, m;

			contact_GetMissionOfferList(pContact, pEnt, &eaOfferList);
			m = eaSize(&eaOfferList);

			for (j = 0; j < m; j++) {
				ContactMissionOffer *pOffer = eaOfferList[j];
				if (GET_REF(pOffer->missionDef) == pMissionDef && (pOffer->allowGrantOrReturn == ContactMissionAllow_GrantAndReturn || pOffer->allowGrantOrReturn == ContactMissionAllow_ReturnOnly)) {
					MinimapWaypoint *pWPData = StructCreate(parse_MinimapWaypoint);
					int iHitFloor = false;

					pWPData->type = MinimapWaypointType_MissionReturnContact;
					pWPData->pchContactName = pContact->name;
					pWPData->pchStaticEncName = g_ContactLocations[i]->pchStaticEncName;
					copyVec3(g_ContactLocations[i]->loc, pWPData->pos);
					worldSnapPosToGround(iPartitionIdx, pWPData->pos, 3, -2, &iHitFloor);

					if (iHitFloor) {
						vecY(pWPData->pos) += 1;
					}
					if (pMissionDef) {
						pWPData->pchMissionRefString = pMissionDef->pchRefString;
					}

					// Pathfind from the player to the position to see if we need to go through a door
					if (waypoint_ClampWaypointToRegion(pEnt, pWPData, NULL)) {
						eaPush(peaWaypointList, pWPData);
					} else {
						StructDestroy(parse_MinimapWaypoint, pWPData);
					}
					
					bMadeWaypoint = true;
					break; // only once per contact, if the contact offers this mission twice somehow...
				}
			}
			if (eaOfferList) {
				eaDestroy(&eaOfferList);
			}
		}
	}

	PERFINFO_AUTO_STOP();
	return bMadeWaypoint;
}


static bool waypoint_GetRestartContactWaypointsForMission(Entity *pEnt, MissionDef *pMissionDef, MinimapWaypoint ***peaWaypointList)
{
	int i, n;
	bool bMadeWaypoint = false;

	PERFINFO_AUTO_START_FUNC();

	n = eaSize(&g_ContactLocations);
	for (i = 0; i < n; i++) {
		ContactDef *pContact = RefSystem_ReferentFromString(g_ContactDictionary, g_ContactLocations[i]->pchContactDefName);
		if (pContact) {
			ContactMissionOffer **eaOfferList = NULL;
			int j, m;

			contact_GetMissionOfferList(pContact, pEnt, &eaOfferList);
			m = eaSize(&eaOfferList);

			for (j = 0; j < m; j++) {
				ContactMissionOffer *pOffer = eaOfferList[j];
				if (GET_REF(pOffer->missionDef) == pMissionDef && (pOffer->allowGrantOrReturn == ContactMissionAllow_GrantAndReturn || pOffer->allowGrantOrReturn == ContactMissionAllow_GrantOnly)) {
					MinimapWaypoint *pWPData = StructCreate(parse_MinimapWaypoint);

					pWPData->type = MinimapWaypointType_MissionRestartContact;
					pWPData->pchContactName = pContact->name;
					pWPData->pchStaticEncName = g_ContactLocations[i]->pchStaticEncName;
					copyVec3(g_ContactLocations[i]->loc, pWPData->pos);

					if (pMissionDef) {
						pWPData->pchMissionRefString = pMissionDef->pchRefString;
					}

					// Pathfind from the player to the position to see if we need to go through a door
					if (waypoint_ClampWaypointToRegion(pEnt, pWPData, NULL)) {
						eaPush(peaWaypointList, pWPData);
					} else {
						StructDestroy(parse_MinimapWaypoint, pWPData);
					}

					bMadeWaypoint = true;
					break; // only once per contact, if the contact offers this mission twice somehow...
				}
			}
			if (eaOfferList) {
				eaDestroy(&eaOfferList);
			}
		}
	}

	PERFINFO_AUTO_STOP();
	return bMadeWaypoint;
}

//Changes the Y coordinate of the passed in point to the Y coordinate of the nearest beacon below the point
static void waypoint_SnapPosToGround(Entity *pEnt, Vec3 v3Pos)
{
	Beacon **eaBeacons = NULL;
	S32 i;
	F32 fMinVertDist = FLT_MAX;
	Beacon *pBestBeacon = NULL;

	beaconGetNearbyBeacons(&eaBeacons, v3Pos, 20, NULL, NULL);

	for (i = 0; i < eaSize(&eaBeacons); ++i)
	{
		Beacon *pBeacon = eaBeacons[i];

		if (pBeacon && pBeacon->pos[1] + 2 <= v3Pos[1])
		{
			F32 fHorizDist = distance3XZSquared(pBeacon->pos, v3Pos);
			F32 fVertDist = abs(pBeacon->pos[1] - v3Pos[1]);

			if (fHorizDist < 25 && fVertDist < fMinVertDist)
			{
				pBestBeacon = pBeacon;
				fMinVertDist = fVertDist;
			}
		}
	}

	if (pBestBeacon)
	{
		v3Pos[1] = pBestBeacon->pos[1];
	}

	eaDestroy(&eaBeacons);
}


static MinimapWaypoint* waypoint_GetWaypointCoordinates(Entity *pEnt, MissionDef *pMissionDef, MissionWaypoint *pWaypoint)
{
	MinimapWaypoint *pWPData = NULL;
	int iSize = 0;
	Vec3 vEntPos;

	PERFINFO_AUTO_START_FUNC();

	if (pEnt) {
		entGetPos(pEnt, vEntPos);
	}

	if (pWaypoint && pWaypoint->name) {
		switch(pWaypoint->type)
		{
			case MissionWaypointType_Clicky:
				{
					Vec3 vPos;
					if (interactable_GetPositionByName(NULL, pWaypoint->name, vPos)) {
						pWPData = StructCreate(parse_MinimapWaypoint);
						copyVec3(vPos, pWPData->pos);
						waypoint_SnapPosToGround(pEnt, pWPData->pos);
						break;
					}
					break;
				}
			case MissionWaypointType_Volume:
				{ 
					Vec3 vVolPos;
					if (volume_GetCenterPosition(entGetPartitionIdx(pEnt), pWaypoint->name, NULL, vVolPos)) {
						pWPData = StructCreate(parse_MinimapWaypoint);
						copyVec3(vVolPos, pWPData->pos);
						waypoint_SnapPosToGround(pEnt, pWPData->pos);
					}
					break;
				}
			case MissionWaypointType_NamedPoint:
				{
					GameNamedPoint *pPoint = namedpoint_GetByName(pWaypoint->name, NULL);
					if (pPoint) {
						pWPData = StructCreate(parse_MinimapWaypoint);
						namedpoint_GetPosition(pPoint, pWPData->pos, NULL);
						waypoint_SnapPosToGround(pEnt, pWPData->pos);
					}
					break;
				}
			case MissionWaypointType_Encounter:
				{
					GameEncounter *pEncounter = encounter_GetByName(pWaypoint->name, NULL);
					if (pEncounter) {
						pWPData = StructCreate(parse_MinimapWaypoint);
						encounter_GetPosition(pEncounter, pWPData->pos);
					} else if (gConf.bAllowOldEncounterData) {
						OldStaticEncounter *pOldEncounter = oldencounter_StaticEncounterFromName(pWaypoint->name);
						if (pOldEncounter) {
							pWPData = StructCreate(parse_MinimapWaypoint);
							copyVec3(pOldEncounter->encPos, pWPData->pos);
						}
					}
					break;
				}
			case MissionWaypointType_AreaVolume:
				{
					Vec3 vVolPos, vVolCenter, vVolLocalMin, vVolLocalMax;
					F32 fRot;
					if (volume_GetVolumeData(entGetPartitionIdx(pEnt), pWaypoint->name, NULL, vVolCenter, vVolPos, vVolLocalMin, vVolLocalMax, &fRot)) {
						pWPData = StructCreate(parse_MinimapWaypoint);

						copyVec3(vVolCenter, pWPData->pos);
						pWPData->fRotation = fRot;

						// Set axes of elipse
						pWPData->fXAxisRadius = (vVolLocalMax[0] - vVolLocalMin[0])/2;
						pWPData->fYAxisRadius = (vVolLocalMax[2] - vVolLocalMin[2])/2;
					}
				}
				break;
			default:
				return NULL;
		}
	}

	// Pathfind from the player to the position to see if we need to go through a door
	if (!waypoint_ClampWaypointToRegion(pEnt, pWPData, NULL)) {
		StructDestroy(parse_MinimapWaypoint, pWPData);
		pWPData = NULL;
	}

	if (pWPData)
	{
		devassert(lengthVec3Squared(pWPData->pos) < 1e10f);
	}

	PERFINFO_AUTO_STOP();
	return pWPData;
}

typedef struct WaypointDoorInfo
{
	bool bGotWaypoint;
	struct WaypointDoorInfo * pPathDoor; // If this is set, this door should be used instead, as this where the beacons told us to go
	int iPathDoorIndex; // this is only meaningful if pPathDoor is set
} WaypointDoorInfo;

static FoundDoorStruct ** g_ppDoorList = NULL;
static bool g_bDidDoorSearch = false;
static Entity * g_pDoorEntity = NULL;
// not an earray
static WaypointDoorInfo * g_paWaypointDoorInfo = NULL;

static bool waypoint_GenerateDoorWaypointsToMap(Entity * pEnt, char const * pchMapName,FoundDoorStruct ** ppDoorList,MissionDef * pMissionDef, MinimapWaypoint ***peaWaypointList)
{
	// find a door back to the mission return map
	int i;
	bool bWaypointCreated = false;
	int iNumDoors;

	char const * pchCurrentMapName = zmapInfoGetPublicName(NULL);
	if (pchMapName == NULL || pchMapName[0] == 0 || stricmp(pchMapName,pchCurrentMapName) == 0)
		return false;

	iNumDoors = eaSize(&ppDoorList);

	for (i=0;i<iNumDoors;i++)
	{
		char const * pchDoorMapName = ppDoorList[i]->pDoorName;
		WaypointDoorInfo * pDoorInfo = &g_paWaypointDoorInfo[i];
		int iPathDoorIndex = i;
		if (pDoorInfo->pPathDoor)
		{
			iPathDoorIndex = pDoorInfo->iPathDoorIndex;
			pDoorInfo = pDoorInfo->pPathDoor;
		}

		if (pDoorInfo->bGotWaypoint)
			continue;

		if (stricmp(ppDoorList[i]->pDoorName,"missionreturn") == 0)
			pchDoorMapName = GetExitMap(pEnt);

		if (stricmp(pchMapName,pchDoorMapName) == 0)
		{
			MinimapWaypoint *pPoint = NULL;
			bool bClamped;
			bool bKeepWaypoint = true;
			pPoint = StructCreate(parse_MinimapWaypoint);
			copyVec3(ppDoorList[iPathDoorIndex]->vPos, pPoint->pos);
			waypoint_SnapPosToGround(pEnt, pPoint->pos);
			devassert(lengthVec3Squared(pPoint->pos) < 1e10f);

			pPoint->bIsDoorWaypoint = true;
			if (pMissionDef)
			{
				if (missiondef_GetType(pMissionDef) == MissionType_OpenMission){
					pPoint->type = MinimapWaypointType_OpenMission;
				} else {
					pPoint->type = MinimapWaypointType_Mission;
				}
				pPoint->pchMissionRefString = pMissionDef->pchRefString;
				pPoint->pchDestinationMap = pchMapName;
			}

			if (g_paWaypointDoorInfo[i].pPathDoor == NULL)
			{
				// now see if we need to go through a door to get to our door
				if (waypoint_ClampWaypointToRegion(pEnt, pPoint, &bClamped))
				{
					if (bClamped)
					{
						F32 fClosestDistSq = 1e8f;
						int iClosestIndex = -1;

						// figure out what door we clamped to.  It would be more awesome if waypoint_ClampWaypointToRegion knew which door, and could tell us,
						// so that we can extend this code to the other codepaths, which may be necessary
						int j;
						for (j=0;j<iNumDoors;j++)
						{
							F32 fDistSqXZ = distance3SquaredXZ(ppDoorList[j]->vPos,pPoint->pos);
							F32 fDistY = fabsf(ppDoorList[j]->vPos[1]-pPoint->pos[1]);
							F32 fDistSq;

							// There is a height offset in the Y cause of beacons.  I'm going to tolerate it
							if (fDistSqXZ < 1e-3f && fDistY <= 1.01f)
							{
								iPathDoorIndex = j;
								break;
							}
							
							fDistSq = fDistSqXZ+fDistY*fDistY;
							if (fDistSq < fClosestDistSq)
							{
								fClosestDistSq = fDistSq;
								iClosestIndex = j;
							}
						}

						// we can fail to find the right door if the beacons are not up-to-date with the map, which can happen while you are editing.
						// Just use the closest one in this case.  Hopefully will not be wrong too often, and only during dev.
						if (j == iNumDoors)
							iPathDoorIndex = iClosestIndex;

						if (g_paWaypointDoorInfo[iPathDoorIndex].bGotWaypoint)
						{
							bKeepWaypoint = false;
						}
						g_paWaypointDoorInfo[iPathDoorIndex].bGotWaypoint = true;
						g_paWaypointDoorInfo[i].pPathDoor = &g_paWaypointDoorInfo[iPathDoorIndex];
						g_paWaypointDoorInfo[i].iPathDoorIndex = iPathDoorIndex;
					}
				}
				else
				{
					bKeepWaypoint = false;
				}
			}
			
			if (bKeepWaypoint)
			{
				pDoorInfo->bGotWaypoint = true;
				eaPush(peaWaypointList, pPoint);
				bWaypointCreated = true;
			}
			else
			{
				StructDestroy(parse_MinimapWaypoint, pPoint);
				pPoint = NULL;
			}
		}
	}

	return bWaypointCreated;
}

static bool waypoint_ShouldSearchDoorsForPerkMissionWaypoints()
{
	return g_MissionConfig.bShouldSearchDoorsForPerkMissionWaypoints;
}

static bool waypoint_ShouldSearchDoorsForWaypoints()
{
	return !g_MissionConfig.bDoNotSearchDoorsForWayPoints;
}

static void waypoint_LoadDoorListIfNecessary(Entity * pEnt)
{
	devassert(pEnt == g_pDoorEntity);
	if (!g_bDidDoorSearch)
	{
		interactable_FindAllDoors(&g_ppDoorList,pEnt,true);
		g_paWaypointDoorInfo = (WaypointDoorInfo *)calloc(sizeof(WaypointDoorInfo),eaSize(&g_ppDoorList));
		g_bDidDoorSearch = true;
	}
}

void waypoint_UseCachedDoorList(Entity * pEnt)
{
	g_pDoorEntity = pEnt;
	devassert(g_ppDoorList == NULL);
	eaCreate(&g_ppDoorList);
}

void waypoint_ClearCachedDoorList()
{
	eaDestroyStruct(&g_ppDoorList, parse_FoundDoorStruct);
	g_ppDoorList = NULL;
	g_bDidDoorSearch = false;
	g_pDoorEntity = NULL;
	free(g_paWaypointDoorInfo);
	g_paWaypointDoorInfo = NULL;
}

// returns true if it created a waypoint
bool waypoint_GetMissionWaypoints(MissionInfo *pInfo, Mission *pMission, MinimapWaypoint ***peaWaypointList, bool bRecursive)
{
	MissionDef *pMissionDef = mission_GetDef(pMission);
	bool bDoorListOwned = (g_ppDoorList == NULL);
	bool bPerformDoorSearch = false;
	MissionDef *pRootMissionDef = pMissionDef;
	bool bWaypointCreated = false;
	const char *pcCurrentMapName = zmapInfoGetPublicName(NULL);

	// If there is no missiondef, there is a problem.
	if (!pMissionDef) {
		return false;
	}

	PERFINFO_AUTO_START_FUNC();

	if (bDoorListOwned)
	{
		waypoint_UseCachedDoorList(pInfo->parentEnt);
	}

	while(GET_REF(pRootMissionDef->parentDef))
		pRootMissionDef = GET_REF(pRootMissionDef->parentDef);

	// Recursively add waypoints for all sub-missions
	if (bRecursive && eaSize(&pMission->children))
	{
		int i;
		for (i=0;i<eaSize(&g_ppDoorList);i++)
		{
			g_paWaypointDoorInfo[i].bGotWaypoint = false;
		}

		for (i = 0; i < eaSize(&pMission->children); ++i)
		{
			bWaypointCreated |= waypoint_GetMissionWaypoints(pInfo, pMission->children[i], peaWaypointList, true);
		}
	}

	if (!bWaypointCreated)
	{
		// Do not do slow searching of doors for mission waypoints, if the mission is a Perk.
		// Our games can have 1000 perks and this function is called for all perks every time a player enters a map.
		// If map also has lots of interactibles, setting this can hang the server for several seconds.
		if(waypoint_ShouldSearchDoorsForWaypoints() && (waypoint_ShouldSearchDoorsForPerkMissionWaypoints() || missiondef_GetType(pRootMissionDef) != MissionType_Perk))
		{
			// If we're on an instance map, we don't want door waypoints for missions that have nothing to do with the map
			if (missiondef_GetType(pMissionDef) != MissionType_OpenMission && zmapInfoGetMapType(NULL) == ZMTYPE_MISSION)
			{
				if (pRootMissionDef)
				{
					int i;
					for(i = 0; i < eaSize(&pRootMissionDef->eaObjectiveMaps); i++)
					{
						if (pRootMissionDef->eaObjectiveMaps[i]->pchMapName == pcCurrentMapName)
						{
							bPerformDoorSearch = true;
							break;
						}
					}
				}
			}
			else
				bPerformDoorSearch = true;
		}

		if (pMission->state == MissionState_Succeeded)
		{
			bool bMadeWaypoint = false;
			
			// we don't look for contact waypoints for open missions
			if (pMission->infoOwner == pInfo)
				bMadeWaypoint = waypoint_GetContactWaypointsForMission(pInfo->parentEnt, pMissionDef, peaWaypointList);

			if (bPerformDoorSearch && !bMadeWaypoint)
			{
				waypoint_LoadDoorListIfNecessary(pInfo->parentEnt);
				bMadeWaypoint = waypoint_GenerateDoorWaypointsToMap(pInfo->parentEnt,pMissionDef->pchReturnMap,g_ppDoorList,pMissionDef,peaWaypointList);
			}
			bWaypointCreated |= bMadeWaypoint;
		}
		else if (pMission->state == MissionState_Failed && pMission->infoOwner == pInfo)
		{
			bool bMadeWaypoint = false;
			
			// we don't look for contact waypoints for open missions
			if (pMission->infoOwner == pInfo)
				bMadeWaypoint = waypoint_GetRestartContactWaypointsForMission(pInfo->parentEnt, pMissionDef, peaWaypointList);

			if (bPerformDoorSearch && !bMadeWaypoint)
			{
				waypoint_LoadDoorListIfNecessary(pInfo->parentEnt);
				bMadeWaypoint = waypoint_GenerateDoorWaypointsToMap(pInfo->parentEnt,pMissionDef->pchReturnMap,g_ppDoorList,pMissionDef,peaWaypointList);
			}
			bWaypointCreated |= bMadeWaypoint;
		}

		if (pMission->state == MissionState_InProgress || pMissionDef->bIsHandoff)
		{
			// Add the waypoints for this mission
			FOR_EACH_IN_EARRAY_FORWARDS(pMissionDef->eaWaypoints, MissionWaypoint, pWaypoint) {
				if (pWaypoint)
				{
					if (pWaypoint->bAnyMap || (pWaypoint->mapName && (stricmp(pWaypoint->mapName, pcCurrentMapName) == 0)))
					{
						MinimapWaypoint *pPoint = NULL;
						pPoint = waypoint_GetWaypointCoordinates(pInfo->parentEnt, pMissionDef, pWaypoint);
						if (pPoint)
						{
							if (pMissionDef)
							{
								if (missiondef_GetType(pMissionDef) == MissionType_OpenMission){
									pPoint->type = MinimapWaypointType_OpenMission;
								} else {
									pPoint->type = MinimapWaypointType_Mission;
								}
								pPoint->pchMissionRefString = pMissionDef->pchRefString;
							}
							eaPush(peaWaypointList, pPoint);
							bWaypointCreated = true;
						}
					}
					else if(bPerformDoorSearch)
					{
						// Waypoint is on a different map.  Find the door, and show it (maybe this should be in waypoint_GetWaypointCoordinates too)
						waypoint_LoadDoorListIfNecessary(pInfo->parentEnt);
						bWaypointCreated |= waypoint_GenerateDoorWaypointsToMap(pInfo->parentEnt, pWaypoint->mapName, g_ppDoorList, pMissionDef, peaWaypointList);
					}
				}
			}
			FOR_EACH_END

		}
				
		if (bPerformDoorSearch && !bWaypointCreated && pMission->state == MissionState_InProgress && pMissionDef->eaObjectiveMaps)
		{
			int i;
			waypoint_LoadDoorListIfNecessary(pInfo->parentEnt);
			for(i = 0; i < eaSize(&pMissionDef->eaObjectiveMaps); i++)
			{
				bWaypointCreated |= waypoint_GenerateDoorWaypointsToMap(pInfo->parentEnt,pMissionDef->eaObjectiveMaps[i]->pchMapName,g_ppDoorList,pMissionDef,peaWaypointList);
			}
		}
	}

	if (bDoorListOwned)
	{
		waypoint_ClearCachedDoorList();
	}

	PERFINFO_AUTO_STOP();

	return bWaypointCreated;
}


void waypoint_DestroyMinimapWaypoint(MinimapWaypoint *pWaypoint)
{
	StructDestroy(parse_MinimapWaypoint, pWaypoint);
}


void waypoint_UpdateLandmarkWaypoints(Entity *pPlayerEnt)
{
	MissionInfo *pInfo = mission_GetInfoFromPlayer(pPlayerEnt);
	if (pInfo) {
		int i, n = eaSize(&pInfo->waypointList);
		LandmarkData **eaLandmarks;

		// Clear "Landmark" waypoints out of current waypoint list
		for (i = n-1; i >= 0; --i) {
			if (pInfo->waypointList[i]->type == MinimapWaypointType_Landmark) {
				MinimapWaypoint *pWaypoint = eaRemove(&pInfo->waypointList, i);
				waypoint_DestroyMinimapWaypoint(pWaypoint);
			}
		}
		
		eaLandmarks = landmark_GetLandmarkData();
		n = eaSize(&eaLandmarks);
		for (i = 0; i < n; i++) {
			// Check if landmarks are in the same region as the player
			WorldRegion *pLandmarkRegion, *pPlayerRegion;
			Vec3 vPlayerPos;
			Vec3 vCenterPos;
			entGetPos(pPlayerEnt,vPlayerPos);

			if (landmark_GetCenterPoint(eaLandmarks[i], vCenterPos)) {
				pLandmarkRegion = worldGetWorldRegionByPos(eaLandmarks[i]->vCenterPos);
				pPlayerRegion = worldGetWorldRegionByPos(vPlayerPos);
				if (pLandmarkRegion == pPlayerRegion) {
					MinimapWaypoint *pWaypoint = StructCreate(parse_MinimapWaypoint);
					pWaypoint->type = MinimapWaypointType_Landmark;
					if (eaLandmarks[i]->pcIconName) {
						pWaypoint->pchIconTexName = StructAllocString(eaLandmarks[i]->pcIconName);
					}
					pWaypoint->pchDescription = NULL;
					copyVec3(eaLandmarks[i]->vCenterPos, pWaypoint->pos);
					COPY_HANDLE(pWaypoint->hDisplayNameMsg, eaLandmarks[i]->hDisplayNameMsg);
					pWaypoint->bHideUnlessRevealed = eaLandmarks[i]->bHideUnlessRevealed;
					eaPush(&pInfo->waypointList, pWaypoint);
				}
			}
		}
		mission_FlagInfoAsDirty(pInfo);
	}
}


void waypoint_UpdateTrackedContactWaypoints(Entity *pPlayerEnt)
{
	MissionInfo *pInfo = mission_GetInfoFromPlayer(pPlayerEnt);
	if (pInfo) {
		ContactDef *pContactDef = GET_REF(pInfo->hTrackedContact);
		int i, n = eaSize(&pInfo->waypointList);

		// Clear "TargetContact" waypoints out of current waypoint list
		for (i = n-1; i >= 0; --i) {
			if (pInfo->waypointList[i]->type == MinimapWaypointType_TrackedContact){
				MinimapWaypoint *pWaypoint = eaRemove(&pInfo->waypointList, i);
				waypoint_DestroyMinimapWaypoint(pWaypoint);
			}
		}
		
		if (pContactDef) {
			// Check if the Contact is on this map
			const char *pcContactMapName = pContactDef->pchMapName;
			const char *pcCurrentMap = zmapInfoGetPublicName(NULL);

			if (pcContactMapName && pcCurrentMap && !stricmp(pcContactMapName, pcCurrentMap)) {
				// Find all locations for this contact
				n = eaSize(&g_ContactLocations);
				for (i = 0; i < n; i++) {
					if (g_ContactLocations[i] && g_ContactLocations[i]->pchContactDefName == pContactDef->name) {
						// Create a waypoint at each location
						MinimapWaypoint *pWaypoint = StructCreate(parse_MinimapWaypoint);
						pWaypoint->type = MinimapWaypointType_TrackedContact;
						pWaypoint->pchStaticEncName = g_ContactLocations[i]->pchStaticEncName;
						copyVec3(g_ContactLocations[i]->loc, pWaypoint->pos);
						COPY_HANDLE(pWaypoint->hDisplayNameMsg, pContactDef->displayNameMsg.hMessage);

						// Pathfind from the player to the position to see if we need to go through a door
						if (!waypoint_ClampWaypointToRegion(pPlayerEnt, pWaypoint, NULL)) {
							StructDestroy(parse_MinimapWaypoint, pWaypoint);
						} else {
							eaPush(&pInfo->waypointList, pWaypoint);
						}
					}
				}
			} else if (pcContactMapName){
				// TODO - Path to a door to the contact's map
			}
		}
		
		mission_FlagInfoAsDirty(pInfo);
	}
}

void waypoint_UpdateTeamCorralWaypoints(Entity *pPlayerEnt)
{
	MissionInfo *pInfo = mission_GetInfoFromPlayer(pPlayerEnt);
	if (pInfo) {
		S32 i;
		TeamCorralInfo *pTeamCorral = gslTeam_FindTeamCorralInfo(pPlayerEnt);

		for (i = eaSize(&pInfo->waypointList)-1; i >= 0; --i)
		{
			if (pInfo->waypointList[i]->type == MinimapWaypointType_TeamCorral) {
				MinimapWaypoint *pWaypoint = eaRemove(&pInfo->waypointList, i);
				waypoint_DestroyMinimapWaypoint(pWaypoint);
			}
		}

		if (pTeamCorral)
		{
			MinimapWaypoint *pWaypoint = StructCreate(parse_MinimapWaypoint);
			copyVec3(pTeamCorral->v3CenterPoint, pWaypoint->pos);
			pWaypoint->type = MinimapWaypointType_TeamCorral;
			eaPush(&pInfo->waypointList, pWaypoint);
		}

		mission_FlagInfoAsDirty(pInfo);
	}
}


void waypoint_ClearWaypoints(MissionInfo *pInfo, Mission *pMission, bool bRecursive)
{
	if (pInfo && pMission) {
		MissionDef *pMissionDef = mission_GetDef(pMission);
		int i;

		// Remove all existing waypoints for this Mission
		if (pInfo && pMissionDef && pMissionDef->pchRefString) {
			for (i = eaSize(&pInfo->waypointList)-1; i >= 0; --i) {
				if (pInfo->waypointList[i]->pchMissionRefString && pInfo->waypointList[i]->pchMissionRefString == pMissionDef->pchRefString) {
					MinimapWaypoint *pWaypoint = eaRemove(&pInfo->waypointList, i);
					waypoint_DestroyMinimapWaypoint(pWaypoint);
				}
			}
		}

		if (bRecursive) {
			for (i = 0; i < eaSize(&pMission->children); i++){
				waypoint_ClearWaypoints(pInfo, pMission->children[i], true);
			}
		}

		mission_FlagInfoAsDirty(pInfo);
	}
}


void waypoint_UpdateMissionWaypoints(MissionInfo *pInfo, Mission *pMission)
{
	if (pInfo && pMission) {
		PERFINFO_AUTO_START_FUNC();

		// Clear old waypoints
		waypoint_ClearWaypoints(pInfo, pMission, false);

		// Get waypoints
		waypoint_GetMissionWaypoints(pInfo, pMission, &pInfo->waypointList, false);
		mission_FlagInfoAsDirty(pInfo);

		PERFINFO_AUTO_STOP();
	}
}


void waypoint_UpdateAllMissionWaypoints(int iPartitionIdx, MissionInfo *pInfo)
{
	int i, n;

	if (!pInfo) {
		return;
	}

	// If there isn't a g_EncounterMasterLayer, we're in the middle of a refresh.  Do nothing.
	if (gConf.bAllowOldEncounterData && !g_EncounterMasterLayer) {
		return;
	}
		
	// Clear "Mission" and "Contact" waypoints out of current waypoint list
	n = eaSize(&pInfo->waypointList);
	for (i = n-1; i >= 0; --i) {
		if (pInfo->waypointList[i]->type == MinimapWaypointType_Mission 
			|| pInfo->waypointList[i]->type == MinimapWaypointType_OpenMission 
			|| pInfo->waypointList[i]->type == MinimapWaypointType_MissionReturnContact
			|| pInfo->waypointList[i]->type == MinimapWaypointType_MissionRestartContact)
		{
			MinimapWaypoint *pWaypoint = eaRemove(&pInfo->waypointList, i);
			waypoint_DestroyMinimapWaypoint(pWaypoint);
		}
	}

	waypoint_UseCachedDoorList(pInfo->parentEnt);

	// Add waypoints for all current missions
	n = eaSize(&pInfo->missions);
	for (i = 0; i < n; ++i) {
		if (!pInfo->pTeamPrimaryMission || pInfo->missions[i]->missionNameOrig != pInfo->pTeamPrimaryMission->missionNameOrig) {
			waypoint_GetMissionWaypoints(pInfo, pInfo->missions[i], &pInfo->waypointList, true);
		}
	}

	n = eaSize(&pInfo->eaNonPersistedMissions);
	for (i = 0; i < n; ++i) {
		waypoint_GetMissionWaypoints(pInfo, pInfo->eaNonPersistedMissions[i], &pInfo->waypointList, true);
	}

	n = eaSize(&pInfo->eaDiscoveredMissions);
	for (i = 0; i < n; ++i) {
		waypoint_GetMissionWaypoints(pInfo, pInfo->eaDiscoveredMissions[i], &pInfo->waypointList, true);
	}

	if (pInfo->pTeamPrimaryMission){
		waypoint_GetMissionWaypoints(pInfo, pInfo->pTeamPrimaryMission, &pInfo->waypointList, true);
	}

	// Add waypoints for the player's current Open Mission, if any
	if (pInfo->pchCurrentOpenMission){
		OpenMission *pOpenMission = openmission_GetFromName(iPartitionIdx, pInfo->pchCurrentOpenMission);
		if (pOpenMission && pOpenMission->pMission){
			waypoint_GetMissionWaypoints(pInfo, pOpenMission->pMission, &pInfo->waypointList, true);
		}
	}

	waypoint_ClearCachedDoorList();

	mission_FlagInfoAsDirty(pInfo);
}


void waypoint_FlagWaypointRefresh(MissionInfo *pInfo)
{
	pInfo->bWaypointsNeedEval = 1;
}


void waypoint_FlagWaypointRefreshAllPlayers(void)
{
	EntityIterator *pIter = entGetIteratorSingleTypeAllPartitions(0, ENTITYFLAG_IGNORE, GLOBALTYPE_ENTITYPLAYER);
	Entity *pPlayer;

	while (pPlayer = EntityIteratorGetNext(pIter)) {
		MissionInfo *pInfo = mission_GetInfoFromPlayer(pPlayer);
		if (pInfo) {
			waypoint_FlagWaypointRefresh(pInfo);
		}
	}
	EntityIteratorRelease(pIter);
}


