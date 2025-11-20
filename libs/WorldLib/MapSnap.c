#include "MapSnap.h"
#include "WorldGridPrivate.h"
#include "RoomConn.h"

#include "AutoGen/MapSnap_h_ast.c"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_World););

F32 gfCurrentMapOrthoSkewX = 0.0f;
F32 gfCurrentMapOrthoSkewZ = 0.0f;

void mapSnapCalculateRegionData(WorldRegion * pRegion)
{
	// make an attempt to figure out where the ground might be on this map
	WorldZoneMapScope *pScope;
	int i;

	// Get zone map scope
	pScope = zmapGetScope(worldRegionGetZoneMap(pRegion));
	
	for (i=0;i<eaSize(&pScope->spawn_points);i++)
	{
		WorldSpawnPoint * pArbitrarySpawnPoint = pScope->spawn_points[i];
		if (pRegion == worldGetWorldRegionByPos(pArbitrarySpawnPoint->spawn_pos))
		{
			pRegion->mapsnap_data.fGroundFocusHeight = pArbitrarySpawnPoint->spawn_pos[1];
			break;
		}
	}

	// this will be done in GfxMapSnap.c instead.  It's currently all client data
#if 0
	if(pRegion->room_conn_graph)
	{
		int j;
		for( j=0; j < eaSize(&pRegion->room_conn_graph->rooms); j++ )
		{
			int k;
			F32 fFocusHeight = pRegion->mapsnap_data.fGroundFocusHeight;
			Room *room = pRegion->room_conn_graph->rooms[j];
			for( k=0; k < eaSize(&room->partitions); k++ )
			{
				RoomPartition * pPartition = room->partitions[k];
				if (fFocusHeight < pPartition->bounds_min[1] || fFocusHeight > pPartition->bounds_max[1])
				{
					fFocusHeight = pPartition->bounds_min[1];
				}

				pPartition->mapSnapData.fFocusHeight = fFocusHeight;
			}
		}
	}
#endif
}

void mapSnapWorldPosToMapPos(const Vec3 v3RegionMin, const Vec3 v3RegionMax, const Vec3 v3WorldPos, Vec2 v2MapPos, F32 fRegionFocusHeight)
{
	F32 fWorldDistanceX,fWorldDistanceZ;
	Vec3 vAdjustedWorldPos;

	vAdjustedWorldPos[0] = v3WorldPos[0]+gfCurrentMapOrthoSkewX*(v3WorldPos[1]-fRegionFocusHeight);
	vAdjustedWorldPos[2] = v3WorldPos[2]+gfCurrentMapOrthoSkewZ*(v3WorldPos[1]-fRegionFocusHeight);

	fWorldDistanceX = vAdjustedWorldPos[0] - v3RegionMin[0];
	fWorldDistanceZ = v3RegionMax[2] - vAdjustedWorldPos[2];

	v2MapPos[0] = fWorldDistanceX;
	v2MapPos[1] = fWorldDistanceZ;
}

void mapSnapRegionGetMapBounds(WorldRegion *pRegion, Vec3 v3Min, Vec3 v3Max)
{
	S32 i = 0;
	RoomConnGraph * pGraph = worldRegionGetRoomConnGraph(pRegion);

	setVec3(v3Min, 1e9, 1e9, 1e9);
	setVec3(v3Max, -1e9, -1e9, -1e9);
	if (pGraph)
	{
		for (i = 0; i < eaSize(&pGraph->rooms); i++)
		{
			MINVEC3(v3Min, pGraph->rooms[i]->bounds_min, v3Min);
			MAXVEC3(v3Max, pGraph->rooms[i]->bounds_max, v3Max);
		}
	}
	if (i == 0 && pRegion)
	{
		worldRegionGetBounds(pRegion, v3Min, v3Max);
	}
}

void mapSnapGetExtendedBounds(Vec3 const v3Min, Vec3 const v3Max, Vec2 const vOrthoSkew,F32 fFocusHeight, Vec2 v2Min, Vec2 v2Max)
{
	v2Max[0] = v3Max[0]+vOrthoSkew[0]*(v3Max[1]-fFocusHeight);
	v2Max[1] = v3Max[2]+vOrthoSkew[1]*(v3Max[1]-fFocusHeight);

	v2Min[0] = v3Min[0]+vOrthoSkew[0]*(v3Min[1]-fFocusHeight);
	v2Min[1] = v3Min[2]+vOrthoSkew[1]*(v3Min[1]-fFocusHeight);
}

void mapSnapUpdateRegion(WorldRegion *pRegion)
{
	if (pRegion == NULL)
	{
		gfCurrentMapOrthoSkewX = 0.0f;
		gfCurrentMapOrthoSkewZ = 0.0f;
		return;
	}

	//disable skew if the region is UGC. Assumes that this region is currently loaded.
	if (mapSnapMapNameIsUGC(zmapGetName(worldRegionGetZoneMap(pRegion))))
	{
		gfCurrentMapOrthoSkewX = 0.0f;
		gfCurrentMapOrthoSkewZ = 0.0f;
	}
	else if (pRegion->type == WRT_Indoor)
	{
		gfCurrentMapOrthoSkewX = gConf.fMapSnapIndoorOrthoSkewX;
		gfCurrentMapOrthoSkewZ = gConf.fMapSnapIndoorOrthoSkewZ;
	}
	else
	{
		gfCurrentMapOrthoSkewX = gConf.fMapSnapOutdoorOrthoSkewX;
		gfCurrentMapOrthoSkewZ = gConf.fMapSnapOutdoorOrthoSkewZ;
	}
}

//This is kinda an ugly hack, so it's encapsulated here.  It's also called from UGC, so this
//function should force a modicum of consistency. UGC uses the mapname of a UIMinimap, so have to
//take the string instead of the stuct :(
bool mapSnapMapNameIsUGC(const char* mapName){
	return strStartsWith(mapName, "ugc");
}