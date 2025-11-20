#include "dynFxInterface.h"
#include "mission_common.h"
#include "gclEntity.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

typedef struct WaypointFXInfo
{
	Vec3 v3WorldPos;
	dtFx guid;
	const char *pchFx;
	bool bFound : 1;
} WaypointFXInfo;

static WaypointFXInfo **s_eaWaypoints;

#define WAYPOINT_LARGE_FX_NAME "CFX_GroundItems_LargeWaypointMarker"
#define WAYPOINT_SMALL_FX_NAME "CFX_GroundItems_WaypointMarker"

// If two Vec3s are this close together, we consider them the same waypoint.
#define WAYPOINT_MIN_EQUAL_DISTANCE 0.5f

// If the distance from the player to the waypoint is less than this, use
// the smaller waypoint effect.
#define WAYPOINT_NEAR_DISTANCE 150.f

static const char *GetWaypointFxName(Entity *pPlayer, WaypointFXInfo *pInfo)
{
	Vec3 v3PlayerPos;
	entGetPos(pPlayer, v3PlayerPos);
	if (distance3(v3PlayerPos, pInfo->v3WorldPos) < WAYPOINT_NEAR_DISTANCE)
		return WAYPOINT_SMALL_FX_NAME;
	else
		return WAYPOINT_LARGE_FX_NAME;
}

void fcui_WaypointDestroy(WaypointFXInfo *pWaypoint)
{
	if (pWaypoint->guid)
		dtFxKill(pWaypoint->guid);
	free(pWaypoint);
}

void fcui_WaypointsStopAll(void)
{
	eaClearEx(&s_eaWaypoints, fcui_WaypointDestroy);
}

void WaypointFXOncePerFrame(void)
{
	Entity *pPlayer = entActivePlayerPtr();
	MissionInfo *pInfo = mission_GetInfoFromPlayer(pPlayer);
	if (pInfo)
	{
		S32 i, j;
		for (i = 0; i < eaSize(&s_eaWaypoints); i++)
			s_eaWaypoints[i]->bFound = false;
		for (i = 0; i < eaSize(&pInfo->waypointList); i++)
		{
			bool bFound = false;
			for (j = 0; j < eaSize(&s_eaWaypoints) && !bFound; j++)
			{
				if (distance3(s_eaWaypoints[j]->v3WorldPos, pInfo->waypointList[i]->pos) < WAYPOINT_MIN_EQUAL_DISTANCE)
				{
					s_eaWaypoints[j]->bFound = true;
					bFound = true;
				}
			}

			if (!bFound)
			{
				WaypointFXInfo *pWaypoint = calloc(1, sizeof(*pWaypoint));
				pWaypoint->bFound = true;
				copyVec3(pInfo->waypointList[i]->pos, pWaypoint->v3WorldPos);
				pWaypoint->pchFx = GetWaypointFxName(pPlayer, pWaypoint);
				pWaypoint->guid = dtAddFxAtLocation(0, pWaypoint->pchFx, NULL, pWaypoint->v3WorldPos, NULL, NULL, 0.f, 0, NULL, eDynFxSource_UI);
				eaPush(&s_eaWaypoints, pWaypoint);
			}
		}

		for (i = eaSize(&s_eaWaypoints) - 1; i >= 0; i--)
		{
			if (!s_eaWaypoints[i]->bFound)
				fcui_WaypointDestroy(eaRemove(&s_eaWaypoints, i));
			else if (pPlayer)
			{
				WaypointFXInfo *pWaypoint = s_eaWaypoints[i];
				const char *pchFx = GetWaypointFxName(pPlayer, pWaypoint);
				if (pchFx != pWaypoint->pchFx)
				{
					pWaypoint->pchFx = pchFx;
					dtFxKill(pWaypoint->guid);
					pWaypoint->guid = dtAddFxAtLocation(0, pWaypoint->pchFx, NULL, pWaypoint->v3WorldPos, NULL, NULL, 0.f, 0, NULL, eDynFxSource_UI);
				}
			}
		}
	}
	else
		fcui_WaypointsStopAll();
}
