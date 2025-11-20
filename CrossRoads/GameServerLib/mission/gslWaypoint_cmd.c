/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "Entity.h"
#include "beaconPath.h"
#include "gslWaypoint.h"
#include "mission_common.h"
#include "Player.h"
#include "stringcache.h"
#include "team.h"
#include "WorldColl.h"
#include "WorldGrid.h"
#include "WorldLib.h"

#include "mission_common_h_ast.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"


// ----------------------------------------------------------------------------------
// Player Commands: Waypoints
// ----------------------------------------------------------------------------------

AUTO_COMMAND ACMD_NAME(AddSavedWaypoint) ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE;
void waypoint_CmdAddSavedWaypoint(Entity *pPlayerEnt, Vec3 v3Position)
{
	MinimapWaypoint *pWaypoint = NULL;
	const char *pcMapname = zmapInfoGetPublicName(NULL);
	int i;

	if (!pPlayerEnt || !pPlayerEnt->pPlayer) {
		return;
	}

	// Look for an matching existing waypoint
	for (i = eaSize(&pPlayerEnt->pPlayer->ppMyWaypoints) - 1; i >= 0 && !pWaypoint; i--) {
		if (!stricmp(pPlayerEnt->pPlayer->ppMyWaypoints[i]->MapCreatedOn, pcMapname)) {
			pWaypoint = pPlayerEnt->pPlayer->ppMyWaypoints[i];
		}
	}

	// If not found, create a new one
	if (!pWaypoint) {
		pWaypoint = StructCreate(parse_MinimapWaypoint);
		eaPush(&pPlayerEnt->pPlayer->ppMyWaypoints, pWaypoint);
	}

	// This should not be automatic.  Maybe I don't want you adjusting my waypoint position.  Maybe this function should take a parameter
	// indicating whether this functionality is desired.  Also, seems like this code will sometimes make mistakes.  Or perhaps the map code
	// should do the work of determining the actual desired 3D location of the waypoint, period.  It will have to on NW.  [RMARR - 1/24/12]
#if 0
	// On ground maps, try to adjust waypoint so that it is positioned near the ground surface
	if (pPlayerEnt && entGetWorldRegionTypeOfEnt(pPlayerEnt) == WRT_Ground)
	{
		int iPartitionIdx = entGetPartitionIdx(pPlayerEnt);
		WorldCollCollideResults wcResults;
		Vec3 vSource, vFloor, vCeiling;
		F32 fRaycastDistance = 1000.0f;

		copyVec3(v3Position, vSource);
		copyVec3(v3Position, vFloor);
		copyVec3(v3Position, vCeiling);
		vFloor[1] -= fRaycastDistance;
		vCeiling[1] += fRaycastDistance;

		if (worldCollideRay(iPartitionIdx, vSource, vCeiling, WC_QUERY_BITS_WORLD_ALL, &wcResults))
		{
			vSource[1] = wcResults.posWorldImpact[1] - 1.0f;
		}
		else
		{
			vSource[1] += fRaycastDistance;
		}

		if(worldCollideRay(iPartitionIdx, vSource, vFloor, WC_QUERY_BITS_WORLD_ALL, &wcResults))
		{
			copyVec3(wcResults.posWorldImpact, v3Position);
			v3Position[1] += 3.0f;
		}
	}
#endif

	// Update the data
	pWaypoint->pos[0] = v3Position[0];
	pWaypoint->pos[1] = v3Position[1];
	pWaypoint->pos[2] = v3Position[2];
	pWaypoint->MapCreatedOn = allocAddString(pcMapname);
	pWaypoint->type = MinimapWaypointType_SavedWaypoint;

	entity_SetDirtyBit(pPlayerEnt, parse_Player, pPlayerEnt->pPlayer, false);
}


AUTO_COMMAND ACMD_NAME(RemoveSavedWaypoint) ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE;
void waypoint_CmdRemoveSavedWaypoint(Entity *pPlayerEnt)
{
	const char *pcNameName = zmapInfoGetPublicName(NULL);
	int i;

	if (!pPlayerEnt || !pPlayerEnt->pPlayer) {
		return;
	}

	for (i = eaSize(&pPlayerEnt->pPlayer->ppMyWaypoints) - 1; i >= 0; i--) {
		if (!stricmp(pPlayerEnt->pPlayer->ppMyWaypoints[i]->MapCreatedOn, pcNameName)) {
			MinimapWaypoint *pWaypoint = eaRemove(&pPlayerEnt->pPlayer->ppMyWaypoints, i);
			StructDestroy(parse_MinimapWaypoint, pWaypoint);
			entity_SetDirtyBit(pPlayerEnt, parse_Player, pPlayerEnt->pPlayer, false);
		}
	}
}


// Clear your saved waypoints
AUTO_COMMAND ACMD_NAME(RemoveAllSavedWaypoints) ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE;
void waypoint_CmdRemoveAllSavedWaypoints(Entity *pPlayerEnt)
{
	if (pPlayerEnt && pPlayerEnt->pPlayer)
	{
		eaClearStruct(&pPlayerEnt->pPlayer->ppMyWaypoints, parse_MinimapWaypoint);
		entity_SetDirtyBit(pPlayerEnt, parse_Player, pPlayerEnt->pPlayer, false);
	}
}

// ----------------------------------------------------------------------------------
// Waypoints to teammates
// ----------------------------------------------------------------------------------
//requests the server to update waypoints to our teammates.  Called by entGetPosClampedToRegion().

AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void waypoint_CmdGetRegionDoorPosForMapIcon(Entity *pPlayerEnt, EntityRef targetRef)
{
	Vec3 myPos, targetPos;
	const char* myRegion;
	const char* targetRegion;
	Entity* target = entFromEntityRefAnyPartition(targetRef);
	if(!target){
		return;
	}

	entGetPos(pPlayerEnt, myPos);
	myRegion = worldRegionGetRegionName(worldGetWorldRegionByPos(myPos));
	entGetPos(target, targetPos);
	targetRegion = worldRegionGetRegionName(worldGetWorldRegionByPos(targetPos));

	if (myRegion != targetRegion){
		beaconSetPathFindEntity(entGetRef(pPlayerEnt), 0, entGetHeight(pPlayerEnt));
		beaconFindNextConnection(entGetPartitionIdx(pPlayerEnt), entGetRef(pPlayerEnt), myPos, targetPos, targetPos, NULL);
	}
	ClientCmd_updateRegionDoorPosForMapIcon(pPlayerEnt, targetRef, targetPos, targetRegion, myRegion);
}


// ----------------------------------------------------------------------------------
// Debug Commands: Waypoints
// ----------------------------------------------------------------------------------

// MissionRefreshAllWaypoints: Flags a player's mission waypoints to reset
AUTO_COMMAND ACMD_NAME(MissionRefreshAllWaypoints);
void waypoint_CmdMissionRefreshAllWaypoints(Entity *pEnt)
{
	if (pEnt && pEnt->pPlayer && pEnt->pPlayer->missionInfo) {
		waypoint_FlagWaypointRefresh(pEnt->pPlayer->missionInfo);
	}
}


